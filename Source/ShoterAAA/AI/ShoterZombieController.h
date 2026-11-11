#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "ShoterZombieController.generated.h"

class AShoterZombie;
class AShoterCharacter;

/** Chases the closest living player on the nav mesh and attacks in melee range. */
UCLASS()
class SHOTERAAA_API AShoterZombieController : public AAIController
{
	GENERATED_BODY()

public:
	AShoterZombieController(const FObjectInitializer& ObjectInitializer);

	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void Tick(float DeltaSeconds) override;

	AActor* GetTarget() const { return Target.Get(); }

protected:
	TWeakObjectPtr<AActor> Target;

	/** Target whose location the current move request was built for. */
	TWeakObjectPtr<AActor> MoveRequestTarget;

	/** Where the target was when the last move request was issued. */
	FVector MoveRequestTargetLocation = FVector::ZeroVector;

	/** Counts down to the next target search / repath decision. */
	float RetargetTimer = 0.f;

	/** The last MoveToActor request failed: push the pawn straight toward the target instead. */
	bool bFallbackMovement = false;

	/** True while standing still to attack (so StopMovement/ClearFocus only run on the transition). */
	bool bAttackStance = false;

	/** True while no target exists and the zombie is idling. */
	bool bIdle = false;

	AShoterZombie* GetZombie() const;

	/** Nearest living character; the current target gets a small distance bonus to avoid flip-flopping. */
	AShoterCharacter* FindNearestTarget(const FVector& From, const AShoterCharacter* Current) const;
};
