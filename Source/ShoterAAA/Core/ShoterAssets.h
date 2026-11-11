// Resolves project assets by short name so gameplay code never hard-codes content paths.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ShoterAssets.generated.h"

class USoundBase;
class USoundAttenuation;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UStaticMesh;
class UAudioComponent;
class USceneComponent;

/** Attenuation presets created at runtime (no assets needed). */
UENUM()
enum class EShoterSoundRange : uint8
{
	UI,			// 2D, not spatialized
	Short,		// footsteps, reloads, impacts (~25 m)
	Medium,		// zombies, equips, pistols (~50 m)
	Long,		// gunfire (~120 m)
	VeryLong	// explosions, rail gun (~200 m)
};

/** Engine basic shapes (always available). All are 100 cm in size. */
UENUM()
enum class EShoterShape : uint8
{
	Cube,
	Sphere,
	Cylinder,
	Cone,
	Plane
};

/**
 * Asset lookup with folder conventions. Loaded assets are cached (and kept alive) for the whole
 * session. Missing assets return nullptr and are logged once, so the game keeps running before
 * the content has been generated.
 *
 *   Sounds:    /Game/Shoter/Audio/SFX_<Name>       e.g. GetSound("PulseRifle_Fire")
 *              names starting with MUS_ or AMB_ are used as-is, e.g. GetSound("MUS_Menu_Loop")
 *   Materials: /Game/Shoter/Materials/<Name>       e.g. GetMaterial("M_Shoter_Surface")
 *
 * Material parameter contract (created by Tools/Editor scripts):
 *   M_Shoter_Surface  (opaque, lit):    BaseColor (vector), Metallic, Roughness, EmissiveColor (vector), EmissiveStrength
 *   M_Shoter_Glow     (unlit, additive): Color (vector), Intensity
 */
UCLASS()
class SHOTERAAA_API UShoterAssets : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UShoterAssets* Get(const UObject* WorldContextObject);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	USoundBase* GetSound(FName Name);
	USoundAttenuation* GetAttenuation(EShoterSoundRange Range);
	UMaterialInterface* GetMaterial(FName Name);
	UStaticMesh* GetShape(EShoterShape Shape);

	/** Loads any object by full path ("/Game/Folder/Asset.Asset" or "/Game/Folder/Asset"), cached. */
	UObject* LoadAsset(const FString& ObjectPath);

	template <typename T>
	T* LoadAssetAs(const FString& ObjectPath)
	{
		return Cast<T>(LoadAsset(ObjectPath));
	}

	/** Opaque lit material instance (falls back to the engine BasicShapeMaterial). */
	UMaterialInstanceDynamic* MakeSurfaceMaterial(UObject* Outer, const FLinearColor& BaseColor, float Metallic = 0.8f, float Roughness = 0.35f,
		const FLinearColor& EmissiveColor = FLinearColor::Black, float EmissiveStrength = 0.f);

	/** Unlit additive glow material for tracers, flashes, sparks and holograms. */
	UMaterialInstanceDynamic* MakeGlowMaterial(UObject* Outer, const FLinearColor& Color, float Intensity = 20.f);

	// --- Static helpers, safe to call with null/missing assets ---

	/** Non-spatialized sound for the local player (UI, hit markers, announcements). */
	static void PlaySound2D(const UObject* WorldContextObject, FName Name, float Volume = 1.f, float Pitch = 1.f);

	static void PlaySoundAt(const UObject* WorldContextObject, FName Name, const FVector& Location, EShoterSoundRange Range, float Volume = 1.f, float Pitch = 1.f);

	static UAudioComponent* PlaySoundAttached(FName Name, USceneComponent* AttachTo, EShoterSoundRange Range, float Volume = 1.f, float Pitch = 1.f);

private:
	UPROPERTY()
	TMap<FString, TObjectPtr<UObject>> Cache;

	UPROPERTY()
	TMap<EShoterSoundRange, TObjectPtr<USoundAttenuation>> Attenuations;

	TSet<FString> MissingAssets;
};
