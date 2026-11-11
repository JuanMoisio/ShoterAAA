// PvP bot: drives an AShoterCharacter like a player would (move, aim, shoot, reload).
#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "ShoterBotController.generated.h"

class AShoterCharacter;
class AShoterWeapon;

UCLASS()
class SHOTERAAA_API AShoterBotController : public AAIController
{
	GENERATED_BODY()

public:
	AShoterBotController(const FObjectInitializer& ObjectInitializer);

	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GameHasEnded(AActor* EndGameFocus = nullptr, bool bIsWinner = false) override;

	/** 0 = easy, 1 = hard. Affects reaction time, aim error and burst discipline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoter")
	float Skill = 0.5f;

	AShoterCharacter* GetBotCharacter() const;
	AActor* GetEnemy() const { return Enemy.Get(); }

protected:
	TWeakObjectPtr<AActor> Enemy;
	bool bMatchOver = false;

	/** What the movement layer is currently doing. */
	enum class EMoveMode : uint8
	{
		None,
		Strafe,		// enemy in sight: sidestep around it
		LastKnown,	// enemy lost: walk to where it was last seen
		Hunt,		// no enemy known: walk toward the nearest one
		Wander		// no nav path to anyone: roam
	};

	// --- Perception (think rate) ---
	/** Nearest living enemy regardless of visibility; used to hunt. */
	TWeakObjectPtr<AActor> NearestEnemy;
	bool bEnemyVisible = false;
	bool bHasLastKnown = false;
	FVector LastKnownEnemyLocation = FVector::ZeroVector;
	float LastSeenEnemyTime = -1000.f;
	/** Last GetLastDamageTime() value we saw, and when we noticed it change. */
	float SeenDamageTimeValue = -1000.f;
	float DamageNoticedTime = -1000.f;
	/** The enemy cannot be shot before this world time (reaction delay after acquiring it). */
	float ReactionEndTime = 0.f;

	// --- Aim (every frame) ---
	FRotator AimRotation = FRotator::ZeroRotator;
	float AimErrorYaw = 0.f;
	float AimErrorPitch = 0.f;
	float NextAimErrorTime = 0.f;
	float TrackingTime = 0.f;
	bool bAimAtHead = false;
	bool bAimedAtEnemy = false;

	// --- Weapon handling ---
	bool bTriggerDown = false;
	float BurstEndTime = 0.f;
	float NextBurstTime = 0.f;
	float TapReleaseTime = 0.f;
	float NextTapTime = 0.f;
	float NextGrenadeTime = 0.f;
	float NextWeaponSwitchTime = 0.f;
	bool bPrefersAds = false;
	bool bSprintRequested = false;

	// --- Movement ---
	EMoveMode MoveMode = EMoveMode::None;
	float NextStrafeTime = 0.f;
	float NextRepathTime = 0.f;
	float WanderUntilTime = 0.f;
	float StuckTime = 0.f;
	float JumpReleaseTime = 0.f;
	bool bJumpHeld = false;

	// --- Timers ---
	float ThinkTimer = 0.f;
	bool bWasDead = false;

	// --- Per-frame ---
	void Think(AShoterCharacter* Bot, float Now);
	void UpdateAim(AShoterCharacter* Bot, float DeltaSeconds, float Now);
	void UpdateFiring(AShoterCharacter* Bot, float Now);
	void UpdateStuckDetection(AShoterCharacter* Bot, float DeltaSeconds, float Now);

	// --- Think steps ---
	void UpdatePerception(AShoterCharacter* Bot, float Now);
	void UpdateWeaponHandling(AShoterCharacter* Bot, float Now);
	void UpdateMovement(AShoterCharacter* Bot, float Now);

	// --- Helpers ---
	bool CanSeeCharacter(const AShoterCharacter* Bot, const AShoterCharacter* Target) const;
	FVector GetChestLocation(const AShoterCharacter* Target) const;
	FVector GetHeadLocation(const AShoterCharacter* Target) const;
	bool PickStrafePoint(const FVector& Origin, const FVector& EnemyLocation, FVector& OutPoint) const;
	bool PickWanderPoint(const FVector& Origin, float Radius, FVector& OutPoint) const;
	void StartWander(const FVector& Origin, float Now);
	void ForceNewMove(float Now);
	void ReleaseTrigger(AShoterCharacter* Bot);
	void StopCombatActions(AShoterCharacter* Bot);
	void ResetState();
	float GetClampedSkill() const;
};
