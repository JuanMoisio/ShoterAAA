#include "Core/ShoterAssets.h"
#include "ShoterAAA.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"
#include "Components/AudioComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/PackageName.h"

namespace
{
	const TCHAR* SoundFolder = TEXT("/Game/Shoter/Audio/");
	const TCHAR* MaterialFolder = TEXT("/Game/Shoter/Materials/");
	const TCHAR* FallbackMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

	/** "/Game/A/B" -> "/Game/A/B.B" */
	FString ToObjectPath(const FString& Path)
	{
		if (Path.Contains(TEXT(".")))
		{
			return Path;
		}
		return Path + TEXT(".") + FPackageName::GetShortName(Path);
	}

	/** Sounds used constantly during a match; loaded up front to avoid hitches on first use. */
	const TCHAR* PreloadedSounds[] =
	{
		TEXT("PulsePistol_Fire"), TEXT("PulseRifle_Fire"), TEXT("Ripper_Fire"), TEXT("Scatter_Fire"), TEXT("Rail_Fire"),
		TEXT("Plasma_Fire"), TEXT("Explosion"), TEXT("Reload"), TEXT("DryFire"), TEXT("WeaponEquip"), TEXT("Impact_Surface"),
		TEXT("Impact_Flesh"), TEXT("HitMarker"), TEXT("Headshot"), TEXT("KillConfirm"), TEXT("ShieldBreak"),
		TEXT("Footstep_01"), TEXT("Footstep_02"), TEXT("Footstep_03"), TEXT("Footstep_04")
	};
}

UShoterAssets* UShoterAssets::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject || !GEngine)
	{
		return nullptr;
	}
	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UShoterAssets>() : nullptr;
}

void UShoterAssets::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	auto MakeAttenuation = [this](EShoterSoundRange Range, float InnerRadius, float FalloffDistance)
	{
		USoundAttenuation* Attenuation = NewObject<USoundAttenuation>(this);
		FSoundAttenuationSettings& Settings = Attenuation->Attenuation;
		Settings.bAttenuate = true;
		Settings.bSpatialize = true;
		Settings.AttenuationShape = EAttenuationShape::Sphere;
		Settings.AttenuationShapeExtents = FVector(InnerRadius, 0.f, 0.f);
		Settings.FalloffDistance = FalloffDistance;
		Settings.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
		Settings.dBAttenuationAtMax = -50.f;
		Settings.bAttenuateWithLPF = true;
		Settings.LPFRadiusMin = InnerRadius * 3.f;
		Settings.LPFRadiusMax = FalloffDistance;
		Attenuations.Add(Range, Attenuation);
	};

	MakeAttenuation(EShoterSoundRange::Short, 200.f, 2500.f);
	MakeAttenuation(EShoterSoundRange::Medium, 300.f, 5000.f);
	MakeAttenuation(EShoterSoundRange::Long, 500.f, 12000.f);
	MakeAttenuation(EShoterSoundRange::VeryLong, 800.f, 20000.f);

	for (const TCHAR* SoundName : PreloadedSounds)
	{
		GetSound(FName(SoundName));
	}
}

UObject* UShoterAssets::LoadAsset(const FString& Path)
{
	if (Path.IsEmpty())
	{
		return nullptr;
	}

	const FString ObjectPath = ToObjectPath(Path);
	if (const TObjectPtr<UObject>* Found = Cache.Find(ObjectPath))
	{
		return Found->Get();
	}
	if (MissingAssets.Contains(ObjectPath))
	{
		return nullptr;
	}

	UObject* Loaded = nullptr;
	const FString PackageName = FPackageName::ObjectPathToPackageName(ObjectPath);
	if (FPackageName::DoesPackageExist(PackageName))
	{
		Loaded = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	if (!Loaded)
	{
		MissingAssets.Add(ObjectPath);
		UE_LOG(LogShoter, Warning, TEXT("Asset not found (the game keeps running without it): %s"), *ObjectPath);
		return nullptr;
	}

	Cache.Add(ObjectPath, Loaded);
	return Loaded;
}

USoundBase* UShoterAssets::GetSound(FName Name)
{
	if (Name.IsNone())
	{
		return nullptr;
	}

	FString AssetName = Name.ToString();
	if (!AssetName.StartsWith(TEXT("SFX_")) && !AssetName.StartsWith(TEXT("MUS_")) && !AssetName.StartsWith(TEXT("AMB_")))
	{
		AssetName = TEXT("SFX_") + AssetName;
	}
	return Cast<USoundBase>(LoadAsset(FString(SoundFolder) + AssetName));
}

USoundAttenuation* UShoterAssets::GetAttenuation(EShoterSoundRange Range)
{
	const TObjectPtr<USoundAttenuation>* Found = Attenuations.Find(Range);
	return Found ? Found->Get() : nullptr;
}

UMaterialInterface* UShoterAssets::GetMaterial(FName Name)
{
	if (Name.IsNone())
	{
		return nullptr;
	}
	return Cast<UMaterialInterface>(LoadAsset(FString(MaterialFolder) + Name.ToString()));
}

UStaticMesh* UShoterAssets::GetShape(EShoterShape Shape)
{
	const TCHAR* Path = TEXT("/Engine/BasicShapes/Cube.Cube");
	switch (Shape)
	{
	case EShoterShape::Sphere:		Path = TEXT("/Engine/BasicShapes/Sphere.Sphere"); break;
	case EShoterShape::Cylinder:	Path = TEXT("/Engine/BasicShapes/Cylinder.Cylinder"); break;
	case EShoterShape::Cone:		Path = TEXT("/Engine/BasicShapes/Cone.Cone"); break;
	case EShoterShape::Plane:		Path = TEXT("/Engine/BasicShapes/Plane.Plane"); break;
	default: break;
	}
	return Cast<UStaticMesh>(LoadAsset(Path));
}

UMaterialInstanceDynamic* UShoterAssets::MakeSurfaceMaterial(UObject* Outer, const FLinearColor& BaseColor, float Metallic, float Roughness,
	const FLinearColor& EmissiveColor, float EmissiveStrength)
{
	UMaterialInterface* Parent = GetMaterial(TEXT("M_Shoter_Surface"));
	const bool bFallback = (Parent == nullptr);
	if (bFallback)
	{
		Parent = Cast<UMaterialInterface>(LoadAsset(FallbackMaterialPath));
	}
	if (!Parent)
	{
		return nullptr;
	}

	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Parent, Outer ? Outer : this);
	if (bFallback)
	{
		// BasicShapeMaterial only has a "Color" parameter.
		Material->SetVectorParameterValue(TEXT("Color"), BaseColor + EmissiveColor * FMath::Clamp(EmissiveStrength * 0.1f, 0.f, 1.f));
	}
	else
	{
		Material->SetVectorParameterValue(TEXT("BaseColor"), BaseColor);
		Material->SetScalarParameterValue(TEXT("Metallic"), Metallic);
		Material->SetScalarParameterValue(TEXT("Roughness"), Roughness);
		Material->SetVectorParameterValue(TEXT("EmissiveColor"), EmissiveColor);
		Material->SetScalarParameterValue(TEXT("EmissiveStrength"), EmissiveStrength);
	}
	return Material;
}

UMaterialInstanceDynamic* UShoterAssets::MakeGlowMaterial(UObject* Outer, const FLinearColor& Color, float Intensity)
{
	UMaterialInterface* Parent = GetMaterial(TEXT("M_Shoter_Glow"));
	const bool bFallback = (Parent == nullptr);
	if (bFallback)
	{
		Parent = Cast<UMaterialInterface>(LoadAsset(FallbackMaterialPath));
	}
	if (!Parent)
	{
		return nullptr;
	}

	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Parent, Outer ? Outer : this);
	Material->SetVectorParameterValue(TEXT("Color"), Color);
	if (!bFallback)
	{
		Material->SetScalarParameterValue(TEXT("Intensity"), Intensity);
	}
	return Material;
}

void UShoterAssets::PlaySound2D(const UObject* WorldContextObject, FName Name, float Volume, float Pitch)
{
	UShoterAssets* Assets = Get(WorldContextObject);
	USoundBase* Sound = Assets ? Assets->GetSound(Name) : nullptr;
	if (Sound)
	{
		UGameplayStatics::PlaySound2D(WorldContextObject, Sound, Volume, Pitch);
	}
}

void UShoterAssets::PlaySoundAt(const UObject* WorldContextObject, FName Name, const FVector& Location, EShoterSoundRange Range, float Volume, float Pitch)
{
	UShoterAssets* Assets = Get(WorldContextObject);
	USoundBase* Sound = Assets ? Assets->GetSound(Name) : nullptr;
	if (!Sound)
	{
		return;
	}

	if (Range == EShoterSoundRange::UI)
	{
		UGameplayStatics::PlaySound2D(WorldContextObject, Sound, Volume, Pitch);
		return;
	}
	UGameplayStatics::PlaySoundAtLocation(WorldContextObject, Sound, Location, FRotator::ZeroRotator, Volume, Pitch, 0.f, Assets->GetAttenuation(Range));
}

UAudioComponent* UShoterAssets::PlaySoundAttached(FName Name, USceneComponent* AttachTo, EShoterSoundRange Range, float Volume, float Pitch)
{
	if (!AttachTo)
	{
		return nullptr;
	}
	UShoterAssets* Assets = Get(AttachTo);
	USoundBase* Sound = Assets ? Assets->GetSound(Name) : nullptr;
	if (!Sound)
	{
		return nullptr;
	}
	USoundAttenuation* Attenuation = (Range == EShoterSoundRange::UI) ? nullptr : Assets->GetAttenuation(Range);
	return UGameplayStatics::SpawnSoundAttached(Sound, AttachTo, NAME_None, FVector::ZeroVector, EAttachLocation::KeepRelativeOffset,
		true, Volume, Pitch, 0.f, Attenuation);
}
