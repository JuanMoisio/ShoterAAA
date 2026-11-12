// Pooled, capped, kinematic cosmetic effects: travelling shot bolts, lingering rail beams and falling debris (shell casings,
// dropped magazines). One world subsystem owns fixed pools of static mesh components, so nothing is spawned per shot and the
// live counts are hard-capped. It does not exist on a dedicated server (Get() returns null: every call site just no-ops).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameFramework/Actor.h"
#include "Core/ShoterAssets.h"
#include "ShoterFXPool.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;

/** A glowing bolt that visibly travels from Start to End (the shot itself is already resolved: this is cosmetic). */
struct SHOTERAAA_API FShoterBoltParams
{
	FVector Start = FVector::ZeroVector;
	FVector End = FVector::ZeroVector;
	FLinearColor Color = FLinearColor(0.1f, 0.8f, 1.f);
	/** cm/s */
	float Speed = 12000.f;
	/** Visible length in cm (bright head + fading tail). */
	float Length = 110.f;
	/** Thickness in cm. */
	float Width = 1.5f;
	/** Emissive intensity of the head (the tail uses a fraction). Keep it low when the bolt is close to the camera. */
	float Intensity = 9.f;
	/** Spawn the impact effect (sparks, decal, sound) when the head arrives at End. */
	bool bImpact = false;
	FVector ImpactNormal = FVector::UpVector;
	bool bImpactFlesh = false;
};

/** Instant beam that lingers and fades (rail gun). */
struct SHOTERAAA_API FShoterBeamParams
{
	FVector Start = FVector::ZeroVector;
	FVector End = FVector::ZeroVector;
	FLinearColor Color = FLinearColor(0.45f, 0.65f, 1.f);
	float Width = 4.f;
	float Lifetime = 0.9f;
	float Intensity = 14.f;
};

/**
 * A piece of falling debris moved kinematically (gravity, one or more floor bounces, spin, rest, shrink away). No actor, no
 * collision with players, no physics simulation. Used for shell casings and meant for dropped magazines.
 */
struct SHOTERAAA_API FShoterDebrisParams
{
	/** Engine basic shape. All basic shapes are 100 cm; cylinders/cones have their axis along local Z. */
	EShoterShape Shape = EShoterShape::Cylinder;
	/** Final size in cm along the local axes. */
	FVector Size = FVector(1.f, 1.f, 3.f);
	/** Lit surface colour once cooled (metal). */
	FLinearColor BaseColor = FLinearColor(0.03f, 0.03f, 0.035f);
	float Metallic = 0.9f;
	float Roughness = 0.4f;
	/** Emissive colour and strength at spawn; the glow fades out over GlowTime seconds (0 strength = never glows). */
	FLinearColor GlowColor = FLinearColor::Black;
	float GlowStrength = 0.f;
	float GlowTime = 0.6f;

	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	/** cm/s, world space */
	FVector Velocity = FVector::ZeroVector;
	/** Spin in degrees/s about the world axes (x = roll, y = pitch, z = yaw). */
	FVector AngularVelocity = FVector::ZeroVector;

	float GravityZ = -980.f;
	/** Seconds until it disappears (it shrinks away over the last FadeTime). */
	float Lifetime = 2.5f;
	float FadeTime = 0.4f;
	/** Bounce off the world (static geometry only). After MaxBounces floor/wall hits the piece comes to rest. */
	bool bCollide = true;
	int32 MaxBounces = 1;
	float Restitution = 0.35f;
	/** Name of a UShoterAssets sound played (quiet, rate limited, spatialized) on each bounce. None = silent. */
	FName BounceSound = NAME_None;
	float BounceSoundVolume = 0.15f;
	float BounceSoundPitch = 2.f;

	/**
	 * Render through the first-person viewmodel transform (viewmodel FOV and scale), like the first-person weapon parts. Location and
	 * Velocity are then plain world-space values around the *raw* weapon component positions (not the on-screen positions), and the
	 * piece is drawn exactly like the gun it leaves. Use for short motion right next to the gun; world collision still uses the raw
	 * position, so give it a short Lifetime or bCollide = false. Local viewer only (do not use for other players' guns).
	 * With false (default) the piece is a normal world object: pass a position from UShoterFXPool::ToFirstPersonSpace() when it
	 * has to start visually at a spot on the first-person gun.
	 */
	bool bFirstPerson = false;
};

/** Holds the pooled components. */
UCLASS(NotBlueprintable, Transient)
class AShoterFXPoolHost : public AActor
{
	GENERATED_BODY()

public:
	AShoterFXPoolHost();
};

UCLASS()
class SHOTERAAA_API UShoterFXPool : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Hard caps on live items. When a pool is full the oldest item is recycled (its impact, if any, fires immediately). */
	static constexpr int32 MaxBolts = 48;
	static constexpr int32 MaxBeams = 6;
	static constexpr int32 MaxDebris = 64;
	static constexpr int32 MaxStreaks = 32;

	/** Null on a dedicated server (or without a game world). */
	static UShoterFXPool* Get(const UObject* WorldContextObject);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	void SpawnBolt(const FShoterBoltParams& Params);
	void SpawnBeam(const FShoterBeamParams& Params);
	bool SpawnDebris(const FShoterDebrisParams& Params);

	/**
	 * Where a point of the first-person weapon model (a raw world position, e.g. a muzzle or ejection port component position) appears
	 * on screen, expressed as a real world position at the unscaled distance, so a full-size world object spawned there looks right.
	 * Falls back to the input when there is no first-person camera.
	 */
	static FVector ToFirstPersonSpace(const UObject* WorldContextObject, const FVector& RawWorldPosition);

	/** Live counts, for debugging. */
	int32 GetNumLiveBolts() const { return NumLiveBolts; }
	int32 GetNumLiveDebris() const { return NumLiveDebris; }

private:
	struct FBolt
	{
		UStaticMeshComponent* Head = nullptr;
		UStaticMeshComponent* Tail = nullptr;
		bool bActive = false;
		bool bImpactFired = false;
		FVector Start = FVector::ZeroVector;
		FVector Dir = FVector::ForwardVector;
		FQuat Rotation = FQuat::Identity;
		float Distance = 0.f;
		float HeadPos = 0.f;
		float Speed = 0.f;
		float Length = 0.f;
		float Width = 0.f;
		double SpawnTime = 0.0;
		FShoterBoltParams Params;
	};

	struct FBeam
	{
		UStaticMeshComponent* Core = nullptr;
		UStaticMeshComponent* Halo = nullptr;
		UMaterialInstanceDynamic* CoreMaterial = nullptr;
		UMaterialInstanceDynamic* HaloMaterial = nullptr;
		bool bActive = false;
		float Age = 0.f;
		FShoterBeamParams Params;
		double SpawnTime = 0.0;
	};

	/** Thin glowing line along the first stretch of a bolt path: keeps a bolt readable when seen nearly end-on. Fades and thins in ~0.12 s. */
	struct FStreak
	{
		UStaticMeshComponent* Mesh = nullptr;
		UMaterialInstanceDynamic* Material = nullptr;
		bool bActive = false;
		float Age = 0.f;
		float Life = 0.12f;
		float Width = 1.f;
		float Intensity = 3.f;
		float Length = 1000.f;
		FVector Start = FVector::ZeroVector;
		FQuat Rotation = FQuat::Identity;
		FVector Dir = FVector::ForwardVector;
		double SpawnTime = 0.0;
	};

	struct FDebris
	{
		UStaticMeshComponent* Mesh = nullptr;
		UMaterialInstanceDynamic* Material = nullptr;
		bool bActive = false;
		bool bResting = false;
		bool bGlowing = false;
		int32 Bounces = 0;
		float Age = 0.f;
		FVector Velocity = FVector::ZeroVector;
		FVector Location = FVector::ZeroVector;
		FQuat Rotation = FQuat::Identity;
		double SpawnTime = 0.0;
		FShoterDebrisParams Params;
	};

	void EnsurePools();
	UStaticMeshComponent* MakeComponent(UStaticMesh* Mesh, UMaterialInterface* Material);
	UMaterialInstanceDynamic* GetBoltMaterial(const FLinearColor& Color, float Intensity);
	void ReleaseBolt(FBolt& Bolt);
	void FireBoltImpact(FBolt& Bolt);
	void ReleaseDebris(FDebris& Piece);
	void TickBolts(float DeltaTime);
	void TickBeams(float DeltaTime);
	void SpawnStreak(const FShoterBoltParams& Params, const FVector& Dir, float Distance);
	void TickStreaks(float DeltaTime);
	void TickDebris(float DeltaTime);
	void PlayBounceSound(const FShoterDebrisParams& Params, const FVector& Location);

	UPROPERTY(Transient)
	TObjectPtr<AShoterFXPoolHost> Host;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> KeepAlive;

	/** Shared bolt materials, keyed by colour and intensity. */
	TMap<uint32, UMaterialInstanceDynamic*> BoltMaterials;

	TArray<FBolt> Bolts;
	TArray<FBeam> Beams;
	TArray<FStreak> Streaks;
	TArray<FDebris> Debris;
	bool bPoolsBuilt = false;
	int32 NumLiveBolts = 0;
	int32 NumLiveDebris = 0;
	double LastBounceSoundTime = -10.0;
	int32 BounceSoundsThisSecond = 0;
};
