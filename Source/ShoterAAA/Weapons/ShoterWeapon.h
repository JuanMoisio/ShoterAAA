// Base weapon: client-predicted hitscan with server validation, or server-spawned projectiles.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/ShoterAssets.h"
#include "Core/ShoterTypes.h"
#include "ShoterWeapon.generated.h"

class AShoterCharacter;
class AShoterProjectile;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

UENUM(BlueprintType)
enum class EShoterWeaponType : uint8
{
	Pistol,
	SMG,
	Rifle,
	Shotgun,
	Sniper,
	Launcher
};

UENUM()
enum class EShoterPartMaterial : uint8
{
	Body,		// painted armor plating
	Dark,		// gunmetal, grips
	Accent,		// secondary color
	Glow		// emissive energy strips (uses Stats.EnergyColor)
};

/** One primitive of a procedurally assembled weapon model. X = forward, Z = up, in cm, relative to the grip. */
USTRUCT()
struct SHOTERAAA_API FShoterWeaponPart
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly)
	EShoterShape Shape = EShoterShape::Cube;

	UPROPERTY(EditDefaultsOnly)
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditDefaultsOnly)
	FRotator Rotation = FRotator::ZeroRotator;

	/** Final size in cm (basic shapes are 100 cm, so scale = Size / 100). */
	UPROPERTY(EditDefaultsOnly)
	FVector Size = FVector(10.f);

	UPROPERTY(EditDefaultsOnly)
	EShoterPartMaterial Material = EShoterPartMaterial::Body;

	FShoterWeaponPart() = default;
	FShoterWeaponPart(EShoterShape InShape, const FVector& InLocation, const FVector& InSize, EShoterPartMaterial InMaterial, const FRotator& InRotation = FRotator::ZeroRotator)
		: Shape(InShape), Location(InLocation), Rotation(InRotation), Size(InSize), Material(InMaterial)
	{
	}
};

USTRUCT(BlueprintType)
struct SHOTERAAA_API FShoterWeaponStats
{
	GENERATED_BODY()

	// --- Identity ---
	UPROPERTY(EditDefaultsOnly, Category = "Identity") FText DisplayName;
	UPROPERTY(EditDefaultsOnly, Category = "Identity") EShoterWeaponType Type = EShoterWeaponType::Rifle;
	UPROPERTY(EditDefaultsOnly, Category = "Identity") FLinearColor EnergyColor = FLinearColor(0.1f, 0.8f, 1.f);
	UPROPERTY(EditDefaultsOnly, Category = "Identity") FLinearColor BodyColor = FLinearColor(0.75f, 0.78f, 0.82f);
	UPROPERTY(EditDefaultsOnly, Category = "Identity") FLinearColor AccentColor = FLinearColor(0.08f, 0.09f, 0.11f);

	// --- Damage ---
	UPROPERTY(EditDefaultsOnly, Category = "Damage") float Damage = 20.f;
	UPROPERTY(EditDefaultsOnly, Category = "Damage") float HeadshotMultiplier = 1.8f;
	UPROPERTY(EditDefaultsOnly, Category = "Damage") float Range = 20000.f;
	UPROPERTY(EditDefaultsOnly, Category = "Damage") float FalloffStart = 2500.f;
	UPROPERTY(EditDefaultsOnly, Category = "Damage") float FalloffEnd = 6000.f;
	UPROPERTY(EditDefaultsOnly, Category = "Damage") float MinDamageScale = 0.6f;
	UPROPERTY(EditDefaultsOnly, Category = "Damage") int32 Pellets = 1;

	// --- Fire ---
	/** Rounds per minute. */
	UPROPERTY(EditDefaultsOnly, Category = "Fire") float FireRate = 600.f;
	UPROPERTY(EditDefaultsOnly, Category = "Fire") bool bAutomatic = true;

	// --- Ammo ---
	UPROPERTY(EditDefaultsOnly, Category = "Ammo") int32 MagSize = 30;
	UPROPERTY(EditDefaultsOnly, Category = "Ammo") int32 MaxReserve = 180;
	UPROPERTY(EditDefaultsOnly, Category = "Ammo") float ReloadTime = 1.8f;

	// --- Accuracy (cone half-angle, degrees) ---
	UPROPERTY(EditDefaultsOnly, Category = "Accuracy") float HipSpread = 2.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Accuracy") float AimSpread = 0.15f;
	UPROPERTY(EditDefaultsOnly, Category = "Accuracy") float SpreadPerShot = 0.35f;
	UPROPERTY(EditDefaultsOnly, Category = "Accuracy") float MaxBloom = 3.5f;
	/** Bloom recovered per second. */
	UPROPERTY(EditDefaultsOnly, Category = "Accuracy") float BloomRecovery = 8.f;
	UPROPERTY(EditDefaultsOnly, Category = "Accuracy") float MovingSpread = 1.5f;
	UPROPERTY(EditDefaultsOnly, Category = "Accuracy") float AirSpread = 3.0f;

	// --- Recoil (degrees per shot) ---
	UPROPERTY(EditDefaultsOnly, Category = "Recoil") float RecoilPitch = 0.8f;
	UPROPERTY(EditDefaultsOnly, Category = "Recoil") float RecoilYaw = 0.3f;
	UPROPERTY(EditDefaultsOnly, Category = "Recoil") float AimRecoilScale = 0.65f;
	/** Visual kick of the first-person model, in cm. */
	UPROPERTY(EditDefaultsOnly, Category = "Recoil") float KickBack = 3.f;

	// --- Handling ---
	UPROPERTY(EditDefaultsOnly, Category = "Handling") float AimFOV = 70.f;
	UPROPERTY(EditDefaultsOnly, Category = "Handling") float AimTime = 0.18f;
	UPROPERTY(EditDefaultsOnly, Category = "Handling") float EquipTime = 0.45f;
	UPROPERTY(EditDefaultsOnly, Category = "Handling") float MoveSpeedScale = 1.f;
	/** Sniper: full-screen scope overlay when fully aimed (the model is hidden). */
	UPROPERTY(EditDefaultsOnly, Category = "Handling") bool bHasScope = false;

	// --- Projectile weapons ---
	UPROPERTY(EditDefaultsOnly, Category = "Projectile") TSubclassOf<AShoterProjectile> ProjectileClass;
	UPROPERTY(EditDefaultsOnly, Category = "Projectile") float ProjectileSpeed = 4500.f;

	// --- Audio (short names resolved by UShoterAssets) ---
	UPROPERTY(EditDefaultsOnly, Category = "Audio") FName FireSound = TEXT("PulseRifle_Fire");
	UPROPERTY(EditDefaultsOnly, Category = "Audio") EShoterSoundRange FireSoundRange = EShoterSoundRange::Long;
	UPROPERTY(EditDefaultsOnly, Category = "Audio") FName ReloadSound = TEXT("Reload");
	UPROPERTY(EditDefaultsOnly, Category = "Audio") FName EquipSound = TEXT("WeaponEquip");
	UPROPERTY(EditDefaultsOnly, Category = "Audio") FName DryFireSound = TEXT("DryFire");

	// --- Visuals ---
	/** Rail-style instant beam instead of travelling tracers. */
	UPROPERTY(EditDefaultsOnly, Category = "Visual") bool bBeamTracer = false;
	UPROPERTY(EditDefaultsOnly, Category = "Visual") float TracerWidth = 1.2f;
	/** Muzzle position relative to the grip (model space). */
	UPROPERTY(EditDefaultsOnly, Category = "Visual") FVector MuzzleOffset = FVector(55.f, 0.f, 5.f);
	/** Grip position relative to the camera when hip firing (X forward, Y right, Z up). */
	UPROPERTY(EditDefaultsOnly, Category = "Visual") FVector HipOffset = FVector(30.f, 15.f, -16.f);
	/** Grip position relative to the camera when aiming down sights (sights centered on screen). */
	UPROPERTY(EditDefaultsOnly, Category = "Visual") FVector AimOffset = FVector(22.f, 0.f, -9.f);
	/** Grip offset relative to the hand_r bone of the third-person body. */
	UPROPERTY(EditDefaultsOnly, Category = "Visual") FVector ThirdPersonOffset = FVector(0.f, 2.f, 0.f);
	UPROPERTY(EditDefaultsOnly, Category = "Visual") FRotator ThirdPersonRotation = FRotator(0.f, 90.f, 0.f);
	/** Uniform scale for the third-person model. */
	UPROPERTY(EditDefaultsOnly, Category = "Visual") float ThirdPersonScale = 1.f;

	// --- Zombies economy ---
	UPROPERTY(EditDefaultsOnly, Category = "Economy") int32 BuyCost = 1000;
};

UCLASS(Abstract, NotPlaceable)
class SHOTERAAA_API AShoterWeapon : public AActor
{
	GENERATED_BODY()

public:
	AShoterWeapon();

	//~ AActor
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void Destroyed() override;

	// --- Ownership ---
	/** Server: assigns the owner (also sets Owner/Instigator so owner-only replication works). */
	void SetOwningCharacter(AShoterCharacter* NewOwner);
	AShoterCharacter* GetOwningCharacter() const { return OwningCharacter; }

	// --- Equip lifecycle (called by AShoterCharacter on every machine) ---
	void OnEquip(const AShoterWeapon* LastWeapon);
	void OnUnequip();
	bool IsEquipped() const { return bIsEquipped; }
	bool IsEquipping() const;
	/** 0 at equip start, 1 when ready. Used for the raise animation. */
	float GetEquipAlpha() const;

	// --- Actions (local player, or the server for AI) ---
	void StartFire();
	void StopFire();
	void StartReload();
	bool CanFire() const;
	bool CanReload() const;
	bool WantsToFire() const { return bWantsToFire; }
	bool IsReloading() const { return bReloading; }
	/** 0..1 during a reload. */
	float GetReloadProgress() const;

	// --- Queries ---
	const FShoterWeaponStats& GetStats() const { return Stats; }
	FText GetDisplayName() const { return Stats.DisplayName; }
	EShoterWeaponType GetWeaponType() const { return Stats.Type; }
	int32 GetAmmoInClip() const { return AmmoInClip; }
	int32 GetReserveAmmo() const { return ReserveAmmo; }
	bool HasAnyAmmo() const { return AmmoInClip > 0 || ReserveAmmo > 0; }
	bool IsAmmoFull() const;
	/** Current cone half-angle in degrees, including movement/air/aim (for the HUD crosshair). */
	float GetCurrentSpread() const;
	float GetTimeSinceLastFire() const;
	FVector GetKickOffset() const { return KickOffset; }
	FRotator GetKickRotation() const { return KickRotation; }
	USceneComponent* GetFirstPersonRoot() const { return FirstPersonRoot; }
	USceneComponent* GetThirdPersonRoot() const { return ThirdPersonRoot; }
	/** Muzzle in world space, from the representation this machine renders. */
	FVector GetMuzzleLocation() const;

	// --- Ammo (server) ---
	void AddReserveAmmo(int32 Amount);
	void RefillAmmo();

	// --- Zombies economy ---
	int32 GetBuyCost() const { return Stats.BuyCost; }
	int32 GetAmmoCost() const { return FMath::Max(100, Stats.BuyCost / 2); }

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	FShoterWeaponStats Stats;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	TArray<FShoterWeaponPart> Parts;

	UPROPERTY(VisibleAnywhere, Category = "Weapon")
	TObjectPtr<USceneComponent> SceneRoot;

	/** First-person model, only visible to the owner. Attached to the character's weapon pivot. */
	UPROPERTY(VisibleAnywhere, Category = "Weapon")
	TObjectPtr<USceneComponent> FirstPersonRoot;

	/** Third-person model, hidden from the owner. Attached to the body's right hand. */
	UPROPERTY(VisibleAnywhere, Category = "Weapon")
	TObjectPtr<USceneComponent> ThirdPersonRoot;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> FirstPersonParts;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> ThirdPersonParts;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> PartMaterials;

	UPROPERTY(ReplicatedUsing = OnRep_OwningCharacter)
	TObjectPtr<AShoterCharacter> OwningCharacter;

	UPROPERTY(Replicated)
	int32 AmmoInClip = 0;

	UPROPERTY(Replicated)
	int32 ReserveAmmo = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Reloading)
	bool bReloading = false;

	UFUNCTION()
	void OnRep_OwningCharacter();

	UFUNCTION()
	void OnRep_Reloading();

	// --- RPCs ---
	UFUNCTION(Server, Reliable)
	void ServerFireHitscan(FVector_NetQuantize Origin, FVector_NetQuantizeNormal AimDirection, const TArray<FVector_NetQuantize>& ImpactPoints, uint32 FleshMask, const TArray<FShoterHitReport>& Hits);

	UFUNCTION(Server, Reliable)
	void ServerFireProjectile(FVector_NetQuantize Origin, FVector_NetQuantizeNormal Direction);

	UFUNCTION(Server, Reliable)
	void ServerStartReload();

	/** Cosmetic shot replication for everyone except the shooter (who already played it). */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastFireEffects(FVector_NetQuantize Origin, const TArray<FVector_NetQuantize>& ImpactPoints, uint32 FleshMask);

	// --- Internals ---
	void HandleFiring();
	void FireHitscan(const FVector& Origin, const FVector& AimDir);
	void FireProjectile(const FVector& Origin, const FVector& AimDir);
	void SpawnProjectile(const FVector& Origin, const FVector& Direction);
	bool TraceShot(const FVector& Start, const FVector& Dir, FHitResult& OutHit) const;
	void ConsumeAmmo();
	void ApplyLocalFireFeedback();
	void ProcessHitsAuthority(const FVector& Origin, const TArray<FShoterHitReport>& Hits);
	bool ValidateHit(const FVector& Origin, const FShoterHitReport& Hit) const;
	float ComputeDamage(float Distance, bool bHeadshot) const;
	void PlayShotEffects(const FVector& Origin, const TArray<FVector_NetQuantize>& ImpactPoints, uint32 FleshMask);
	void FinishReload();
	void CancelReload();
	void BuildModel(USceneComponent* Parent, TArray<TObjectPtr<UStaticMeshComponent>>& OutParts, bool bFirstPerson);
	void UpdateAttachment();
	float GetFireInterval() const;
	bool IsOwnerLocallyControlled() const;

	bool bIsEquipped = false;
	bool bWantsToFire = false;
	bool bModelBuilt = false;
	float LastFireTime = -100.f;
	float LastServerFireTime = -100.f;
	/** Server: shots the owner may still fire right now (token bucket refilled at the fire rate). */
	float ServerFireBudget = 3.f;
	float EquipStartTime = -100.f;
	float ReloadStartTime = -100.f;
	float SpreadBloom = 0.f;
	FVector KickOffset = FVector::ZeroVector;
	FRotator KickRotation = FRotator::ZeroRotator;

	FTimerHandle TimerHandle_Refire;
	FTimerHandle TimerHandle_Reload;
};
