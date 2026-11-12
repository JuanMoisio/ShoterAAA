// Server-authoritative replicated projectiles: plasma orbs and grenades.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShoterProjectile.generated.h"

class USphereComponent;
class UProjectileMovementComponent;
class UStaticMeshComponent;
class UPointLightComponent;

UCLASS(Abstract)
class SHOTERAAA_API AShoterProjectile : public AActor
{
	GENERATED_BODY()

public:
	AShoterProjectile();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;

	/** Server: call right after spawning. */
	void InitProjectile(const FVector& Direction, float Speed, AController* InInstigatorController, const FString& InWeaponName);

	/** Name shown in the kill feed. */
	const FString& GetWeaponName() const { return WeaponName; }

protected:
	UPROPERTY(VisibleAnywhere, Category = "Projectile")
	TObjectPtr<USphereComponent> Collision;

	UPROPERTY(VisibleAnywhere, Category = "Projectile")
	TObjectPtr<UProjectileMovementComponent> Movement;

	UPROPERTY(VisibleAnywhere, Category = "Projectile")
	TObjectPtr<UStaticMeshComponent> Visual;

	UPROPERTY(VisibleAnywhere, Category = "Projectile")
	TObjectPtr<UPointLightComponent> Light;

	/** Cosmetic glow tail and soft halo so the orb reads clearly in flight (only for projectiles that point along their velocity). */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Trail;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Halo;

	UPROPERTY(EditDefaultsOnly, Category = "Projectile") float DirectHitDamage = 30.f;
	UPROPERTY(EditDefaultsOnly, Category = "Projectile") float ExplosionDamage = 95.f;
	UPROPERTY(EditDefaultsOnly, Category = "Projectile") float ExplosionMinDamage = 10.f;
	UPROPERTY(EditDefaultsOnly, Category = "Projectile") float ExplosionInnerRadius = 120.f;
	UPROPERTY(EditDefaultsOnly, Category = "Projectile") float ExplosionRadius = 420.f;
	/** Seconds until it explodes by itself (0 = only on impact). */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile") float FuseTime = 0.f;
	UPROPERTY(EditDefaultsOnly, Category = "Projectile") bool bExplodeOnImpact = true;
	UPROPERTY(EditDefaultsOnly, Category = "Projectile") FLinearColor Color = FLinearColor(0.6f, 0.2f, 1.f);
	UPROPERTY(EditDefaultsOnly, Category = "Projectile") float VisualDiameter = 14.f;
	UPROPERTY(EditDefaultsOnly, Category = "Projectile") FName BounceSound = NAME_None;

	UPROPERTY(ReplicatedUsing = OnRep_Exploded)
	bool bExploded = false;

	UPROPERTY(Replicated)
	FVector_NetQuantize ExplosionLocation;

	UFUNCTION()
	void OnRep_Exploded();

	UFUNCTION()
	void OnProjectileStop(const FHitResult& ImpactResult);

	UFUNCTION()
	void OnProjectileBounce(const FHitResult& ImpactResult, const FVector& ImpactVelocity);

	/** Server */
	void Explode(const FHitResult& Impact);
	void OnFuseExpired();
	void PlayExplosionEffects();

	TWeakObjectPtr<AController> InstigatorController;
	FString WeaponName;
	FTimerHandle TimerHandle_Fuse;
};

/** Fast glowing orb that explodes on impact (Plasma Launcher). */
UCLASS()
class SHOTERAAA_API AShoterPlasmaOrb : public AShoterProjectile
{
	GENERATED_BODY()
public:
	AShoterPlasmaOrb();
};

/** Bouncing grenade with a fuse (G key). */
UCLASS()
class SHOTERAAA_API AShoterGrenade : public AShoterProjectile
{
	GENERATED_BODY()
public:
	AShoterGrenade();
};
