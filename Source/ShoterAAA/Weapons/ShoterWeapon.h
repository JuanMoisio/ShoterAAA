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
	Glow,		// emissive energy strips (uses Stats.EnergyColor)
	Reticle		// faint night-sight marks on the iron sight tips (Stats.EnergyColor, kept small so they never bloom)
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

/** Where a hand holds the weapon: palm centre and hand orientation in weapon space (X forward, Y right, Z up, cm). */
USTRUCT()
struct SHOTERAAA_API FShoterHandGrip
{
	GENERATED_BODY()

	/** Centre of the palm. */
	UPROPERTY(EditDefaultsOnly)
	FVector Location = FVector::ZeroVector;

	/**
	 * Hand orientation. With a zero rotation the fingers point forward (+X) and the palm faces the weapon centre line
	 * (-Y for the right hand, +Y for the left hand). Pitch/yaw/roll rotate that frame.
	 */
	UPROPERTY(EditDefaultsOnly)
	FRotator Rotation = FRotator::ZeroRotator;

	/** 0 = open hand, 1 = fingers fully closed. */
	UPROPERTY(EditDefaultsOnly)
	float Curl = 0.8f;

	FShoterHandGrip() = default;
	FShoterHandGrip(const FVector& InLocation, const FRotator& InRotation, float InCurl) : Location(InLocation), Rotation(InRotation), Curl(InCurl) {}
};

/**
 * Reload choreography of a weapon (procedural, driven by reload progress 0..1). The support hand leaves its grip, takes the magazine
 * (MagParts) out along MagOut, drops it, goes down out of frame, comes back with a fresh one, seats it and returns. Rifle-like weapons
 * then rack the action with the same hand. Progress fractions must be increasing: Grab < Pull < Away < Back < Seat <= Charge < 1.
 */
USTRUCT()
struct SHOTERAAA_API FShoterReloadSpec
{
	GENERATED_BODY()

	/** Palm target on the seated magazine. */
	UPROPERTY(EditDefaultsOnly) FVector MagGrip = FVector::ZeroVector;
	UPROPERTY(EditDefaultsOnly) FRotator MagHandRot = FRotator::ZeroRotator;
	/** Centre, size and orientation of the magazine as a single box (for the dropped magazine), weapon space. */
	UPROPERTY(EditDefaultsOnly) FVector MagCenter = FVector::ZeroVector;
	UPROPERTY(EditDefaultsOnly) FVector MagSize = FVector(4.f, 4.f, 10.f);
	UPROPERTY(EditDefaultsOnly) FRotator MagRotation = FRotator::ZeroRotator;
	/** Direction the magazine leaves the weapon and how far it slides before it is free. */
	UPROPERTY(EditDefaultsOnly) FVector MagOut = FVector(0.f, 0.f, -1.f);
	UPROPERTY(EditDefaultsOnly) float MagTravel = 10.f;
	/** Where the hand goes for a fresh magazine, relative to the pulled-out magazine position (out of the player view). */
	UPROPERTY(EditDefaultsOnly) FVector HandDrop = FVector(-4.f, -4.f, -40.f);

	/** Rack the action after seating (charging handle, slide or pump) with the support hand. */
	UPROPERTY(EditDefaultsOnly) bool bCharge = false;
	UPROPERTY(EditDefaultsOnly) FVector ChargeLocation = FVector::ZeroVector;
	UPROPERTY(EditDefaultsOnly) FRotator ChargeRotation = FRotator::ZeroRotator;
	UPROPERTY(EditDefaultsOnly) float ChargeStroke = 7.f;

	// Progress fractions: hand on the magazine, magazine released, hand away, hand back, magazine seated, action racked.
	UPROPERTY(EditDefaultsOnly) float Grab = 0.14f;
	UPROPERTY(EditDefaultsOnly) float Pull = 0.27f;
	UPROPERTY(EditDefaultsOnly) float Away = 0.38f;
	UPROPERTY(EditDefaultsOnly) float Back = 0.60f;
	UPROPERTY(EditDefaultsOnly) float Seat = 0.70f;
	UPROPERTY(EditDefaultsOnly) float Charge = 0.88f;

	/** Scale of the whole-gun tilt while reloading. */
	UPROPERTY(EditDefaultsOnly) float TiltScale = 1.f;
};

/** One frame of the reload choreography (weapon space). */
struct FShoterReloadFrame
{
	FVector LeftLocation = FVector::ZeroVector;
	FRotator LeftRotation = FRotator::ZeroRotator;
	float LeftCurl = 0.8f;
	/** 0 = animation untouched, 1 = hand fully on the choreography. Blends the third-person arm in and out. */
	float LeftWeight = 0.f;
	FVector MagOffset = FVector::ZeroVector;
	bool bMagVisible = true;
	/** Multiplier of the magazine glow (fresh magazines are brighter, seating pulses). */
	float MagGlow = 1.f;
	/** Whole-gun tilt 0..1. */
	float Tilt = 0.f;
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
	UPROPERTY(EditDefaultsOnly, Category = "Audio") FName MagOutSound = TEXT("MagOut");
	UPROPERTY(EditDefaultsOnly, Category = "Audio") FName MagInSound = TEXT("MagIn");
	UPROPERTY(EditDefaultsOnly, Category = "Audio") FName ChargeSound = TEXT("Charge");
	UPROPERTY(EditDefaultsOnly, Category = "Audio") FName EquipSound = TEXT("WeaponEquip");
	UPROPERTY(EditDefaultsOnly, Category = "Audio") FName DryFireSound = TEXT("DryFire");

	// --- Visuals ---
	/** Rail-style instant beam instead of travelling tracers. */
	UPROPERTY(EditDefaultsOnly, Category = "Visual") bool bBeamTracer = false;
	UPROPERTY(EditDefaultsOnly, Category = "Visual") float TracerWidth = 1.2f;
	/** Visible bolt of hitscan weapons (cosmetic: hits stay instant). Travel speed in cm/s and length in cm (pellets use 55%). */
	UPROPERTY(EditDefaultsOnly, Category = "Visual") float BoltSpeed = 12000.f;
	UPROPERTY(EditDefaultsOnly, Category = "Visual") float BoltLength = 110.f;
	/** Shell ejection port relative to the grip (model space, right side of the receiver) and the size of the spent cell, in cm. */
	UPROPERTY(EditDefaultsOnly, Category = "Visual") FVector EjectOffset = FVector(10.f, 3.2f, 6.f);
	UPROPERTY(EditDefaultsOnly, Category = "Visual") float CasingLength = 3.4f;
	UPROPERTY(EditDefaultsOnly, Category = "Visual") float CasingDiameter = 1.f;
	/** Muzzle position relative to the grip (model space). */
	UPROPERTY(EditDefaultsOnly, Category = "Visual") FVector MuzzleOffset = FVector(55.f, 0.f, 5.f);
	/** Grip position relative to the camera when hip firing (X forward, Y right, Z up). */
	UPROPERTY(EditDefaultsOnly, Category = "Visual") FVector HipOffset = FVector(30.f, 15.f, -16.f);
	/** Grip position relative to the camera when aiming down sights (sights centered on screen). */
	UPROPERTY(EditDefaultsOnly, Category = "Visual") FVector AimOffset = FVector(22.f, 0.f, -9.f);
	/** Weapon-space position of the sight reticle. When set, aiming places the weapon so this point is on the view axis. */
	UPROPERTY(EditDefaultsOnly, Category = "Visual") FVector SightOffset = FVector::ZeroVector;
	/** Grip offset relative to the hand_r bone of the third-person body. */
	UPROPERTY(EditDefaultsOnly, Category = "Visual") FVector ThirdPersonOffset = FVector(0.f, 2.f, 0.f);
	UPROPERTY(EditDefaultsOnly, Category = "Visual") FRotator ThirdPersonRotation = FRotator(0.f, 90.f, 0.f);
	/** Uniform scale for the third-person model. */
	UPROPERTY(EditDefaultsOnly, Category = "Visual") float ThirdPersonScale = 1.f;
	/** Where the hands hold the weapon (weapon space). The right hand stays on RightGrip; the left one on LeftGrip except while reloading. */
	UPROPERTY(EditDefaultsOnly, Category = "Visual") FShoterHandGrip RightGrip;
	UPROPERTY(EditDefaultsOnly, Category = "Visual") FShoterHandGrip LeftGrip;
	UPROPERTY(EditDefaultsOnly, Category = "Visual") FShoterReloadSpec Reload;

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
	/** The reload choreography at the current progress (while not reloading: the left hand at its rest grip, magazine seated). */
	void GetReloadFrame(FShoterReloadFrame& OutFrame) const;
	/** Where the third-person model sits right now, from the body current pose (valid also in the middle of the body pose update). */
	bool GetThirdPersonModelTransform(FTransform& OutTransform) const;
	/** Adds a visual kick to the first-person model (the seating bump of a reload). */
	void AddViewKick(const FVector& Offset, const FRotator& Rotation);

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
	/**
	 * Where the first-person model sits (weapon pivot, view space) when fully aimed down sights. With iron sights the
	 * top of the front post (Stats.SightOffset) lands on the view axis. Otherwise derived from the
	 * weapon's own parts so the aim point and the area around it stay clear: the highest solid point sits a fixed
	 * angle below the eye line, the model is far enough from the eye to stay narrow on screen, and Y is centred.
	 * ForwardOffset is the extra constant forward offset the character adds on top of the hip/aim offsets.
	 */
	FVector GetAimViewOffset(float ForwardOffset) const;
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

	/** Iron sights, separate from the body so an optic add-on can replace them later. Built together with Parts. */
	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	TArray<FShoterWeaponPart> SightParts;

	/** The magazine / energy cell in its seated position (weapon space). Built on its own root so reloads can move it separately. */
	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	TArray<FShoterWeaponPart> MagParts;

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

	/** Magazine roots (children of the model roots) and their parts. */
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> FirstPersonMagRoot;

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> ThirdPersonMagRoot;

	/** Glow materials of the magazine alone, so it can pulse (first person, third person). */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FirstPersonMagGlow;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ThirdPersonMagGlow;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> PartMaterials;

	/** First-person variants: dimmer body and less glow, since the model is right next to the camera. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> FirstPersonPartMaterials;

	mutable bool bAimViewOffsetCached = false;
	mutable FVector CachedAimViewOffset = FVector::ZeroVector;

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
	void EjectCasing(bool bFirstPersonView);
	/** True when this machine renders the owner's own first-person view (viewmodel FOV/scale in effect). */
	bool IsFirstPersonView() const;
	void FinishReload();
	void CancelReload();
	/** Common start of a reload on every machine (cosmetic state). */
	void BeginReloadVisuals(float StartProgress);
	void UpdateReloadVisuals();
	void ResetReloadVisuals();
	void OnMagazineReleased();
	void OnMagazineSeated();
	void OnActionRacked();
	void PlayReloadSound(FName Sound, float Volume);
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
	/** Cosmetic reload events already played in this reload. */
	bool bReleaseDone = false;
	bool bSeatDone = false;
	bool bRackDone = false;
	bool bReloadVisualsActive = false;
	FVector KickOffset = FVector::ZeroVector;
	FRotator KickRotation = FRotator::ZeroRotator;

	FTimerHandle TimerHandle_Refire;
	FTimerHandle TimerHandle_Reload;
};
