// Cosmetic armour kit: rigid basic-shape pieces attached to skeleton bones of a character mesh.
#pragma once

#include "CoreMinimal.h"
#include "Core/ShoterAssets.h"
#include "ShoterCharacterKit.generated.h"

class AActor;
class USkinnedMeshComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

/** Which material of the kit a piece uses. */
UENUM()
enum class EShoterKitMat : uint8
{
	Armor,		// near-black plate
	Trim,		// lighter graphite (belts, joints, details)
	Accent,		// lit plate in the accent colour (shoulder / chest marking)
	Glow		// unlit additive light (visor, strips, eyes, core)
};

/**
 * One piece of a kit, authored in the character frame of the reference pose (X forward, Y right, Z up, cm)
 * relative to a bone. Pieces marked bMirror are created twice: for the bone as written (a "_r" bone) and for
 * the "_l" bone, with the lateral offset and rotation mirrored.
 *
 * If EndBone is set the piece is centred at lerp(Bone, EndBone, T) and its Z axis (cylinder axis) points from
 * Bone to EndBone; Size.Z is then the length along the segment.
 */
struct FShoterKitPiece
{
	EShoterShape Shape = EShoterShape::Cube;
	FName Bone;
	FName EndBone;
	float T = 0.5f;
	FVector Offset = FVector::ZeroVector;
	FVector Size = FVector(10.f);
	FRotator Rot = FRotator::ZeroRotator;
	EShoterKitMat Mat = EShoterKitMat::Armor;
	bool bMirror = false;
};

/** A built piece with its material slot, so materials can be refreshed when the team colour changes. */
USTRUCT()
struct FShoterKitPart
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Component = nullptr;

	UPROPERTY()
	EShoterKitMat Mat = EShoterKitMat::Armor;
};

/** Shared dynamic materials of one character's kit (four instances, not one per piece). */
USTRUCT()
struct FShoterKitMaterials
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> Armor = nullptr;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> Trim = nullptr;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> Accent = nullptr;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> Glow = nullptr;
};

struct FShoterKitPalette
{
	FLinearColor Armor = FLinearColor(0.035f, 0.038f, 0.046f);
	FLinearColor Trim = FLinearColor(0.10f, 0.105f, 0.12f);
	FLinearColor AccentColor = FLinearColor(0.05f, 0.55f, 1.f);
	float ArmorMetallic = 0.85f;
	float ArmorRoughness = 0.38f;
	float AccentEmissive = 1.5f;
	float GlowIntensity = 2.4f;
};

namespace ShoterCharacterKit
{
	/** Builds the pieces on Mesh (which must already have its skeletal mesh asset). Skipped on dedicated servers. */
	SHOTERAAA_API void Build(AActor* Owner, USkinnedMeshComponent* Mesh, TConstArrayView<FShoterKitPiece> Pieces, bool bCastShadow, TArray<FShoterKitPart>& OutParts);

	/** Creates (once) and refreshes the shared materials and assigns them to the parts. */
	SHOTERAAA_API void UpdateMaterials(UObject* Outer, const FShoterKitPalette& Palette, FShoterKitMaterials& Materials, const TArray<FShoterKitPart>& Parts);

	/**
	 * Replaces every material slot of Mesh by a dynamic instance of M_Shoter_Trooper fed with the slot's own textures
	 * (so the mannequin's panel/normal detail survives, for Manny and Quinn alike). Returns the instances; no-op if already done.
	 */
	SHOTERAAA_API void MakeArmorSlotMaterials(UObject* Outer, USkinnedMeshComponent* Mesh, TArray<TObjectPtr<UMaterialInstanceDynamic>>& InOutSlots);

	/** Pieces of the trooper kit (players and bots). */
	SHOTERAAA_API TConstArrayView<FShoterKitPiece> GetTrooperKit();

	/** Only the forearm guards of the trooper kit, for the first-person arms. */
	SHOTERAAA_API TConstArrayView<FShoterKitPiece> GetTrooperArmKit();

	/** Pieces of the zombie kit for a type index (0 walker, 1 runner, 2 brute). */
	SHOTERAAA_API TConstArrayView<FShoterKitPiece> GetZombieKit(int32 TypeIndex);
}
