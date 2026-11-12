// Mutated sci-fi zombie for the co-op mode. Melee attacker driven by AShoterZombieController.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Core/ShoterTypes.h"
#include "Core/ShoterCharacterKit.h"
#include "ShoterZombie.generated.h"

class UMaterialInstanceDynamic;
class UStaticMeshComponent;

UCLASS(Config = Game)
class SHOTERAAA_API AShoterZombie : public ACharacter
{
	GENERATED_BODY()

public:
	AShoterZombie(const FObjectInitializer& ObjectInitializer);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	virtual void FellOutOfWorld(const UDamageType& DmgType) override;

	/** Server: configure stats for the wave. Called by AShoterGameMode_Zombies right after spawning. */
	void InitZombie(EShoterZombieType InType, int32 InWave);

	EShoterZombieType GetZombieType() const { return ZombieType; }
	bool IsAlive() const { return !bDead && Health > 0.f; }
	float GetHealth() const { return Health; }
	float GetMaxHealth() const { return MaxHealth; }
	float GetAttackRange() const { return AttackRange; }

	/** Server: starts a melee attack toward Target (ignored while attacking or on cooldown). */
	void StartAttack(AActor* Target);
	bool IsAttacking() const { return bAttacking; }

	/** True when Target is within AttackRange * RangeScale (flat distance, with a generous vertical tolerance). */
	bool IsTargetInAttackRange(const AActor* Target, float RangeScale = 1.f) const;

protected:
	UPROPERTY(ReplicatedUsing = OnRep_ZombieType)
	EShoterZombieType ZombieType = EShoterZombieType::Walker;

	UPROPERTY(Replicated)
	float Health = 100.f;

	UPROPERTY(Replicated)
	float MaxHealth = 100.f;

	UPROPERTY(ReplicatedUsing = OnRep_Dead)
	bool bDead = false;

	UPROPERTY(ReplicatedUsing = OnRep_Attacking)
	bool bAttacking = false;

	UPROPERTY(EditDefaultsOnly, Category = "Zombie")
	float AttackRange = 150.f;

	UPROPERTY(EditDefaultsOnly, Category = "Zombie")
	float AttackDamage = 30.f;

	/** Seconds between the attack start and the damage check. */
	UPROPERTY(EditDefaultsOnly, Category = "Zombie")
	float AttackWindup = 0.45f;

	UPROPERTY(EditDefaultsOnly, Category = "Zombie")
	float AttackCooldown = 1.2f;

	/** Third-person body (same skeleton as the players), configurable in DefaultGame.ini. */
	UPROPERTY(Config)
	FSoftObjectPath BodyMeshPath;

	UPROPERTY(Config)
	FSoftClassPath BodyAnimClassPath;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BodyMaterial;

	/** Dynamic instances of the mesh's own slot materials (corrupted palette). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> BodySlotMaterials;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> RimOverlayMaterial;

	/** Cosmetic kit (eyes, chest core, armour remnants), built locally. */
	UPROPERTY(Transient)
	TArray<FShoterKitPart> KitParts;

	UPROPERTY(Transient)
	FShoterKitMaterials KitMaterials;

	int32 KitBuiltForType = -1;

	/** Glowing eyes of the placeholder body. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> EyeMaterial;

	/** Placeholder body (basic shapes), only built when the skeletal mesh is missing. Pivot at the feet. */
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> PlaceholderRoot;

	/** Shoulder pivot of the placeholder: the arms swing with it. */
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> PlaceholderArms;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> PlaceholderParts;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> PlaceholderEyes;

	UFUNCTION()
	void OnRep_ZombieType();

	UFUNCTION()
	void OnRep_Dead();

	UFUNCTION()
	void OnRep_Attacking();

	// --- Body and look (every machine) ---
	void SetupBody();
	void BuildPlaceholderBody();
	void ApplyLook();

	// --- Combat (server) ---
	void HandleDeath(AController* Killer, bool bHeadshot, const FVector& HitDirection);
	void ResolveAttack();
	void FinishAttack();
	void EndStagger();

	// --- Death presentation (every machine) ---
	void StartDeathPresentation(const FVector& ImpulseDirection);

	// --- Cosmetic growls (local timer, not replicated) ---
	void ScheduleGrowl();
	void PlayGrowl();
	void PlayAttackSound();

	/** Walk speed the wave scaling asked for (the stagger temporarily halves the real one). */
	float BaseWalkSpeed = 180.f;
	float LastAttackTime = -1000.f;
	float PlaceholderAnimTime = 0.f;
	float PlaceholderArmPitch = 0.f;
	bool bDeathPresented = false;

	TWeakObjectPtr<AActor> AttackTarget;

	FTimerHandle TimerHandle_Growl;
	FTimerHandle TimerHandle_Attack;
	FTimerHandle TimerHandle_Stagger;
};
