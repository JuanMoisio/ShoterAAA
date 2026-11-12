#include "Core/ShoterCharacterKit.h"
#include "ShoterAAA.h"
#include "AnimationRuntime.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Engine/Texture.h"

namespace
{
	/** Reference-pose mesh space has X = character left, Y = forward; the actor frame is X forward, Y right. */
	FVector MeshToActor(const FVector& V)
	{
		return FVector(V.Y, -V.X, V.Z);
	}

	FVector ActorToMesh(const FVector& V)
	{
		return FVector(-V.Y, V.X, V.Z);
	}

	FName MirrorBone(FName Bone)
	{
		FString Name = Bone.ToString();
		if (Name.EndsWith(TEXT("_r")))
		{
			Name = Name.Left(Name.Len() - 2) + TEXT("_l");
			return FName(*Name);
		}
		return Bone;
	}
}

void ShoterCharacterKit::Build(AActor* Owner, USkinnedMeshComponent* Mesh, TConstArrayView<FShoterKitPiece> Pieces, bool bCastShadow, TArray<FShoterKitPart>& OutParts)
{
	if (!Owner || !Mesh || !Mesh->GetSkinnedAsset() || OutParts.Num() > 0 || Owner->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	UShoterAssets* Assets = UShoterAssets::Get(Owner);
	UWorld* World = Owner->GetWorld();
	if (!Assets || !World || World->IsNetMode(NM_DedicatedServer))
	{
		return;
	}

	const FReferenceSkeleton& Ref = Mesh->GetSkinnedAsset()->GetRefSkeleton();

	if (FParse::Param(FCommandLine::Get(), TEXT("ShoterKitLog")))
	{
		for (int32 Index = 0; Index < Ref.GetNum(); ++Index)
		{
			const FTransform CS = FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, Index);
			const FVector Loc = MeshToActor(CS.GetLocation());
			const FVector Dir = MeshToActor(CS.GetRotation().GetAxisX());
			UE_LOG(LogShoter, Log, TEXT("KitBone %s loc=(%.1f %.1f %.1f) xAxis=(%.2f %.2f %.2f)"), *Ref.GetBoneName(Index).ToString(), Loc.X, Loc.Y, Loc.Z, Dir.X, Dir.Y, Dir.Z);
		}
	}

	const FQuat MeshFromActor(FVector::UpVector, UE_HALF_PI);	// +90 degrees of yaw maps actor X onto mesh Y

	auto AddPiece = [&](const FShoterKitPiece& Piece, FName Bone, FName EndBone, bool bMirrored)
	{
		const int32 BoneIndex = Ref.FindBoneIndex(Bone);
		const int32 EndIndex = EndBone.IsNone() ? INDEX_NONE : Ref.FindBoneIndex(EndBone);
		if (BoneIndex == INDEX_NONE || (!EndBone.IsNone() && EndIndex == INDEX_NONE))
		{
			UE_LOG(LogShoter, Warning, TEXT("Character kit: bone %s (or %s) not found in the skeleton, piece skipped."), *Bone.ToString(), *EndBone.ToString());
			return;
		}

		const FTransform BoneCS = FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, BoneIndex);
		FVector Offset = Piece.Offset;
		FRotator Rot = Piece.Rot;
		if (bMirrored)
		{
			Offset.Y = -Offset.Y;
			Rot.Yaw = -Rot.Yaw;
			Rot.Roll = -Rot.Roll;
		}

		// Everything below happens in the actor frame of the reference pose.
		FVector Location = MeshToActor(BoneCS.GetLocation());
		FQuat Orientation = FQuat::Identity;
		if (EndIndex != INDEX_NONE)
		{
			const FVector EndLocation = MeshToActor(FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, EndIndex).GetLocation());
			const FVector Axis = (EndLocation - Location).GetSafeNormal();
			Orientation = FRotationMatrix::MakeFromZX(Axis, FVector::ForwardVector).ToQuat();
			Location = FMath::Lerp(Location, EndLocation, Piece.T);
		}
		Location += Offset;
		Orientation = Orientation * Rot.Quaternion();

		const FTransform PieceCS(MeshFromActor * Orientation, ActorToMesh(Location));
		const FTransform Relative = PieceCS.GetRelativeTransform(FTransform(BoneCS.GetRotation(), BoneCS.GetLocation()));

		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(Owner);
		Part->SetMobility(EComponentMobility::Movable);
		Part->SetStaticMesh(Assets->GetShape(Piece.Shape));
		Part->SetupAttachment(Mesh, Bone);
		Part->SetRelativeLocation(Relative.GetLocation());
		Part->SetRelativeRotation(Relative.GetRotation());
		Part->SetRelativeScale3D(Piece.Size / 100.f);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetGenerateOverlapEvents(false);
		Part->SetCanEverAffectNavigation(false);
		Part->bReceivesDecals = false;
		Part->SetCastShadow(bCastShadow);
		Part->bCastHiddenShadow = bCastShadow;
		Part->SetOwnerNoSee(Mesh->bOwnerNoSee);
		Part->FirstPersonPrimitiveType = Mesh->FirstPersonPrimitiveType;
		Part->RegisterComponent();

		FShoterKitPart& Built = OutParts.AddDefaulted_GetRef();
		Built.Component = Part;
		Built.Mat = Piece.Mat;
	};

	for (const FShoterKitPiece& Piece : Pieces)
	{
		AddPiece(Piece, Piece.Bone, Piece.EndBone, false);
		if (Piece.bMirror)
		{
			AddPiece(Piece, MirrorBone(Piece.Bone), MirrorBone(Piece.EndBone), true);
		}
	}
}

void ShoterCharacterKit::UpdateMaterials(UObject* Outer, const FShoterKitPalette& Palette, FShoterKitMaterials& Materials, const TArray<FShoterKitPart>& Parts)
{
	UShoterAssets* Assets = UShoterAssets::Get(Outer);
	if (!Assets || Parts.Num() == 0)
	{
		return;
	}

	if (!Materials.Armor)
	{
		Materials.Armor = Assets->MakeSurfaceMaterial(Outer, Palette.Armor, Palette.ArmorMetallic, Palette.ArmorRoughness);
		Materials.Trim = Assets->MakeSurfaceMaterial(Outer, Palette.Trim, 0.9f, 0.3f);
		Materials.Accent = Assets->MakeSurfaceMaterial(Outer, Palette.AccentColor * 0.5f, 0.5f, 0.35f, Palette.AccentColor, Palette.AccentEmissive);
		Materials.Glow = Assets->MakeGlowMaterial(Outer, Palette.AccentColor, Palette.GlowIntensity);
	}
	else
	{
		Materials.Armor->SetVectorParameterValue(TEXT("BaseColor"), Palette.Armor);
		Materials.Armor->SetScalarParameterValue(TEXT("Metallic"), Palette.ArmorMetallic);
		Materials.Armor->SetScalarParameterValue(TEXT("Roughness"), Palette.ArmorRoughness);
		Materials.Trim->SetVectorParameterValue(TEXT("BaseColor"), Palette.Trim);
		Materials.Accent->SetVectorParameterValue(TEXT("BaseColor"), Palette.AccentColor * 0.5f);
		Materials.Accent->SetVectorParameterValue(TEXT("EmissiveColor"), Palette.AccentColor);
		Materials.Accent->SetScalarParameterValue(TEXT("EmissiveStrength"), Palette.AccentEmissive);
		Materials.Glow->SetVectorParameterValue(TEXT("Color"), Palette.AccentColor);
		Materials.Glow->SetScalarParameterValue(TEXT("Intensity"), Palette.GlowIntensity);
	}

	for (const FShoterKitPart& Part : Parts)
	{
		if (!Part.Component)
		{
			continue;
		}
		UMaterialInterface* Material = Materials.Armor;
		switch (Part.Mat)
		{
		case EShoterKitMat::Trim:	Material = Materials.Trim; break;
		case EShoterKitMat::Accent:	Material = Materials.Accent; break;
		case EShoterKitMat::Glow:	Material = Materials.Glow; break;
		default: break;
		}
		Part.Component->SetMaterial(0, Material);
	}
}

void ShoterCharacterKit::MakeArmorSlotMaterials(UObject* Outer, USkinnedMeshComponent* Mesh, TArray<TObjectPtr<UMaterialInstanceDynamic>>& InOutSlots)
{
	UShoterAssets* Assets = UShoterAssets::Get(Outer);
	if (!Assets || !Mesh || InOutSlots.Num() > 0)
	{
		return;
	}
	UMaterialInterface* TrooperBase = Assets->GetMaterial(TEXT("M_Shoter_Trooper"));
	for (int32 Index = 0; Index < Mesh->GetNumMaterials(); ++Index)
	{
		UMaterialInterface* SlotMaterial = Mesh->GetMaterial(Index);
		UMaterialInstanceDynamic* SlotInstance = nullptr;
		if (TrooperBase && SlotMaterial)
		{
			SlotInstance = UMaterialInstanceDynamic::Create(TrooperBase, Outer);
			UTexture* Texture = nullptr;
			if (SlotMaterial->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("Base Texture")), Texture) && Texture)
			{
				SlotInstance->SetTextureParameterValue(TEXT("BaseTex"), Texture);
			}
			if (SlotMaterial->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("BNormal")), Texture) && Texture)
			{
				SlotInstance->SetTextureParameterValue(TEXT("NormalTex"), Texture);
			}
			if (SlotMaterial->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("MRA")), Texture) && Texture)
			{
				SlotInstance->SetTextureParameterValue(TEXT("MRATex"), Texture);
			}
		}
		else if (SlotMaterial)
		{
			SlotInstance = UMaterialInstanceDynamic::Create(SlotMaterial, Outer);
		}
		InOutSlots.Add(SlotInstance);
		if (SlotInstance)
		{
			Mesh->SetMaterial(Index, SlotInstance);
		}
	}
}

// ---------------------------------------------------------------------------
// Kit tables. Offsets are in the actor frame of the reference pose (X forward, Y right, Z up), cm.
// ---------------------------------------------------------------------------

namespace
{
	using EM = EShoterKitMat;
	constexpr EShoterShape Cube = EShoterShape::Cube;
	constexpr EShoterShape Sphere = EShoterShape::Sphere;
	constexpr EShoterShape Cyl = EShoterShape::Cylinder;
	constexpr EShoterShape Cone = EShoterShape::Cone;
	const FName None = NAME_None;
	const FRotator R0 = FRotator::ZeroRotator;
}

// Columns: shape, bone, end bone, T, offset, size, rotation (pitch, yaw, roll), material, mirror.
TConstArrayView<FShoterKitPiece> ShoterCharacterKit::GetTrooperKit()
{
	static const FShoterKitPiece Pieces[] =
	{
		// Helmet: shell, glowing visor band, crest, cheek guards, chin.
		{ Sphere, "head", None, 0.f, FVector(0.5f, 0.f, 7.5f),		FVector(24.f, 21.f, 25.f),	R0,							EM::Armor, false },
		{ Cube,   "head", None, 0.f, FVector(10.8f, 0.f, 8.f),		FVector(4.f, 18.f, 6.f),	R0,							EM::Glow,  false },
		{ Cube,   "head", None, 0.f, FVector(10.f, 0.f, 3.5f),		FVector(5.f, 19.f, 3.f),	R0,							EM::Trim,  false },
		{ Cube,   "head", None, 0.f, FVector(-0.5f, 0.f, 18.f),		FVector(16.f, 3.5f, 5.f),	FRotator(0.f, 0.f, 0.f),	EM::Trim,  false },
		{ Cube,   "head", None, 0.f, FVector(3.f, 8.8f, 4.f),		FVector(10.f, 3.f, 10.f),	R0,							EM::Armor, true },
		{ Cube,   "head", None, 0.f, FVector(8.f, 0.f, -1.5f),		FVector(7.f, 11.f, 5.f),	R0,							EM::Trim,  false },
		{ Cyl,    "head", None, 0.f, FVector(-7.f, 8.f, 12.f),		FVector(1.2f, 1.2f, 14.f),	FRotator(0.f, 0.f, 12.f),	EM::Trim,  false },
		// Chest: plate, belly plate, light strip, accent chevrons.
		{ Cube,   "spine_04", None, 0.f, FVector(10.5f, 0.f, 7.f),	FVector(5.f, 25.f, 14.f),	R0,							EM::Armor, false },
		{ Cube,   "spine_04", None, 0.f, FVector(8.5f, 0.f, 18.f),	FVector(4.f, 28.f, 7.f),	FRotator(-28.f, 0.f, 0.f),	EM::Armor, false },
		{ Cube,   "spine_04", None, 0.f, FVector(9.f, 0.f, -6.f),	FVector(3.5f, 14.f, 7.f),	FRotator(12.f, 0.f, 0.f),	EM::Trim,  false },
		{ Cube,   "spine_04", None, 0.f, FVector(13.2f, 0.f, 13.5f),	FVector(1.f, 20.f, 1.2f),	R0,							EM::Glow,  false },
		{ Cube,   "spine_04", None, 0.f, FVector(13.2f, 8.5f, 4.f),	FVector(1.2f, 5.f, 13.f),	FRotator(0.f, 0.f, 18.f),	EM::Accent, true },
		// Back unit with exhausts and a light strip.
		{ Cube,   "spine_04", None, 0.f, FVector(-12.f, 0.f, 9.f),	FVector(10.f, 22.f, 26.f),	R0,							EM::Armor, false },
		{ Cyl,    "spine_04", None, 0.f, FVector(-14.f, 6.5f, 20.f),FVector(5.f, 5.f, 16.f),	FRotator(12.f, 0.f, 0.f),	EM::Trim,  true },
		{ Cube,   "spine_04", None, 0.f, FVector(-17.4f, 0.f, 8.f),	FVector(1.f, 4.5f, 22.f),	R0,							EM::Glow,  false },
		// Shoulders.
		{ Cube,   "upperarm_r", None, 0.f, FVector(0.f, 4.5f, 8.f),	FVector(15.f, 13.f, 3.f),	FRotator(0.f, 0.f, 30.f),	EM::Armor, true },
		{ Cube,   "upperarm_r", None, 0.f, FVector(0.f, 8.5f, 4.5f),FVector(13.f, 7.f, 2.5f),	FRotator(0.f, 0.f, 52.f),	EM::Armor, true },
		{ Cube,   "upperarm_r", None, 0.f, FVector(0.f, 9.8f, 6.f),	FVector(12.f, 2.f, 3.f),	FRotator(0.f, 0.f, 52.f),	EM::Accent, true },
		// Forearm guards with an accent ring.
		{ Cyl,    "lowerarm_r", "hand_r", 0.5f, FVector(0.f),		FVector(10.5f, 10.5f, 17.f),R0,							EM::Armor, true },
		{ Cyl,    "lowerarm_r", "hand_r", 0.2f, FVector(0.f),		FVector(11.2f, 11.2f, 1.6f),R0,							EM::Accent, true },
		// Belt.
		{ Cube,   "pelvis", None, 0.f, FVector(10.f, 0.f, 6.f),		FVector(4.f, 18.f, 7.f),	R0,							EM::Trim,  false },
		{ Cube,   "pelvis", None, 0.f, FVector(12.2f, 0.f, 6.f),	FVector(0.8f, 6.f, 2.f),	R0,							EM::Glow,  false },
		{ Cube,   "pelvis", None, 0.f, FVector(1.f, 15.f, 2.f),	FVector(6.f, 3.f, 7.f),	R0,							EM::Armor, true },
		{ Cube,   "pelvis", None, 0.f, FVector(-9.f, 0.f, 5.f),		FVector(4.f, 18.f, 8.f),	R0,							EM::Trim,  false },
		// Legs: thigh plate, knee pad, shin guard with accent strip.
		{ Cube,   "thigh_r", "calf_r", 0.4f, FVector(8.3f, 2.5f, 0.f),	FVector(2.8f, 9.f, 22.f),FRotator(0.f, 12.f, 0.f),	EM::Armor, true },
		{ Cube,   "thigh_r", "calf_r", 0.4f, FVector(1.f, 9.3f, 1.f),	FVector(12.f, 2.5f, 19.f),R0,						EM::Armor, true },
		{ Cube,   "thigh_r", "calf_r", 0.4f, FVector(10.f, 2.8f, 0.f),	FVector(0.8f, 2.2f, 16.f),FRotator(0.f, 12.f, 0.f),	EM::Accent, true },
		{ Cube,   "calf_r", None, 0.f, FVector(8.8f, 1.5f, 2.f),	FVector(4.f, 10.f, 6.f),	FRotator(-18.f, 0.f, 0.f),	EM::Trim,  true },
		{ Cube,   "calf_r", "foot_r", 0.5f, FVector(6.5f, 0.f, 0.f),	FVector(4.5f, 10.f, 28.f),R0,						EM::Armor, true },
		{ Cube,   "calf_r", "foot_r", 0.5f, FVector(9.f, 0.f, 0.f),	FVector(1.f, 2.5f, 22.f),	R0,							EM::Accent, true },
	};
	return Pieces;
}

TConstArrayView<FShoterKitPiece> ShoterCharacterKit::GetTrooperArmKit()
{
	static const FShoterKitPiece Pieces[] =
	{
		// Slim forearm guard hugging the arm, ending well before the wrist, with a thin accent ring (the arm bones are slimmed to 0.78).
		{ Cyl,    "lowerarm_r", "hand_r", 0.30f, FVector(0.f),		FVector(9.2f, 9.2f, 10.f),	R0,							EM::Armor, true },
		{ Cyl,    "lowerarm_r", "hand_r", 0.14f, FVector(0.f),		FVector(9.8f, 9.8f, 0.9f),	R0,							EM::Accent, true },
	};
	return Pieces;
}

TConstArrayView<FShoterKitPiece> ShoterCharacterKit::GetZombieKit(int32 TypeIndex)
{
	// Eyes, chest core, glowing cracks (spine, left forearm, right thigh), one broken shoulder plate and a torn chest piece: 8 pieces.
	// The brute swaps the thigh crack for a second broken plate and has bigger plates.
	static const FShoterKitPiece Walker[] =
	{
		{ Cube,   "head", None, 0.f, FVector(11.f, 3.4f, 6.f),		FVector(3.5f, 3.8f, 2.2f),	FRotator(0.f, 0.f, 10.f),	EM::Glow,  true },
		{ Sphere, "spine_04", None, 0.f, FVector(11.f, 0.f, 6.f),	FVector(7.f, 7.f, 7.f),		R0,							EM::Glow,  false },
		{ Cube,   "upperarm_l", None, 0.f, FVector(0.f, -5.f, 7.5f),FVector(14.f, 11.f, 2.5f),	FRotator(0.f, 15.f, -32.f),	EM::Armor, false },
		{ Cube,   "spine_04", None, 0.f, FVector(10.f, 6.f, 11.f),	FVector(3.f, 12.f, 13.f),	FRotator(0.f, 12.f, 10.f),	EM::Armor, false },
		{ Cube,   "spine_04", None, 0.f, FVector(-10.4f, 0.f, 6.f),	FVector(0.8f, 1.8f, 30.f),	R0,							EM::Glow,  false },
		{ Cube,   "lowerarm_l", "hand_l", 0.5f, FVector(3.2f, 0.f, 0.f), FVector(0.8f, 1.8f, 15.f), R0,						EM::Glow,  false },
		{ Cube,   "thigh_r", "calf_r", 0.5f, FVector(8.f, 1.5f, 0.f), FVector(0.8f, 1.8f, 22.f),R0,							EM::Glow,  false },
	};
	static const FShoterKitPiece Brute[] =
	{
		{ Cube,   "head", None, 0.f, FVector(11.f, 3.4f, 6.f),		FVector(3.5f, 3.8f, 2.2f),	FRotator(0.f, 0.f, 10.f),	EM::Glow,  true },
		{ Sphere, "spine_04", None, 0.f, FVector(11.f, 0.f, 6.f),	FVector(9.f, 9.f, 9.f),		R0,							EM::Glow,  false },
		{ Cube,   "upperarm_l", None, 0.f, FVector(0.f, -5.f, 8.f),	FVector(18.f, 14.f, 3.f),	FRotator(0.f, 15.f, -32.f),	EM::Armor, false },
		{ Cube,   "spine_04", None, 0.f, FVector(10.f, 6.f, 11.f),	FVector(3.f, 14.f, 15.f),	FRotator(0.f, 12.f, 10.f),	EM::Armor, false },
		{ Cube,   "spine_04", None, 0.f, FVector(-10.4f, 0.f, 6.f),	FVector(0.8f, 2.2f, 30.f),	R0,							EM::Glow,  false },
		{ Cube,   "lowerarm_l", "hand_l", 0.5f, FVector(3.2f, 0.f, 0.f), FVector(0.8f, 2.2f, 15.f), R0,						EM::Glow,  false },
		{ Cube,   "upperarm_r", None, 0.f, FVector(0.f, 4.f, 7.f),	FVector(13.f, 9.f, 3.f),	FRotator(0.f, -10.f, 35.f),	EM::Trim,  false },
	};
	return TypeIndex == 2 ? TConstArrayView<FShoterKitPiece>(Brute) : TConstArrayView<FShoterKitPiece>(Walker);
}
