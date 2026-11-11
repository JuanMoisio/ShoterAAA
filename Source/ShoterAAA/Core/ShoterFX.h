// Purely cosmetic, local-only effects built from basic shapes and glow materials (no assets needed).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShoterFX.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UPointLightComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;

/** Spawns cosmetic effects. Never replicated: call it on every machine that should see the effect. */
struct SHOTERAAA_API FShoterFX
{
	/** Bright streak travelling from Start to End. */
	static void SpawnTracer(UWorld* World, const FVector& Start, const FVector& End, const FLinearColor& Color, float Width = 1.2f, float Speed = 30000.f);

	/** Instant beam that fades out (rail gun). */
	static void SpawnBeam(UWorld* World, const FVector& Start, const FVector& End, const FLinearColor& Color, float Width = 2.5f, float Lifetime = 0.6f);

	/** Sparks + flash where a shot hit. bFlesh = organic target (no sparks spray, darker). */
	static void SpawnImpact(UWorld* World, const FVector& Location, const FVector& Normal, const FLinearColor& Color, bool bFlesh);

	/** Short flash attached to a component (weapon muzzle). */
	static void SpawnMuzzleFlash(USceneComponent* AttachTo, const FVector& RelativeLocation, const FLinearColor& Color, float Scale = 1.f, bool bOnlyOwnerSee = false, bool bOwnerNoSee = false);

	/** Big plasma explosion. Also shakes the camera of nearby local players. */
	static void SpawnExplosion(UWorld* World, const FVector& Location, float Radius, const FLinearColor& Color);

	/** Materialize effect for spawns/respawns. */
	static void SpawnTeleportFlash(UWorld* World, const FVector& Location, const FLinearColor& Color);
};

/** What an AShoterFXActor does over its lifetime. */
UENUM()
enum class EShoterFXBehavior : uint8
{
	Fade,		// parts scale/fade out in place
	Travel,		// the whole actor moves along Velocity (tracers)
	Burst		// each part flies with its own velocity + gravity (sparks)
};

/** Generic short-lived effect actor used by FShoterFX. */
UCLASS(NotBlueprintable, Transient)
class SHOTERAAA_API AShoterFXActor : public AActor
{
	GENERATED_BODY()

public:
	AShoterFXActor();

	virtual void Tick(float DeltaSeconds) override;

	UStaticMeshComponent* AddPart(UStaticMesh* Mesh, UMaterialInterface* Material, const FTransform& RelativeTransform, const FVector& PartVelocity = FVector::ZeroVector);
	UPointLightComponent* AddLight(const FLinearColor& Color, float Intensity, float Radius);

	EShoterFXBehavior Behavior = EShoterFXBehavior::Fade;
	float Lifetime = 0.2f;
	FVector Velocity = FVector::ZeroVector;		// Travel
	float TravelDistance = 0.f;					// Travel: destroy after this distance
	float GravityZ = -980.f;					// Burst
	float EndScale = 0.f;						// Fade/Burst: scale multiplier reached at end of life
	float GlowIntensity = 20.f;

private:
	struct FPart
	{
		TObjectPtr<UStaticMeshComponent> Mesh;
		TObjectPtr<UMaterialInstanceDynamic> Material;
		FVector StartScale = FVector::OneVector;
		FVector Velocity = FVector::ZeroVector;
	};

	TArray<FPart> Parts;

	UPROPERTY()
	TArray<TObjectPtr<UObject>> KeepAlive;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> Light;

	float StartLightIntensity = 0.f;
	float Age = 0.f;
	float Travelled = 0.f;
};
