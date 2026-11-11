#include "AI/ShoterZombieController.h"
#include "AI/ShoterZombie.h"
#include "Player/ShoterCharacter.h"
#include "AITypes.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Navigation/PathFollowingComponent.h"

namespace
{
	/** Seconds between target searches and repath decisions. */
	constexpr float ZombieAIRetargetInterval = 0.3f;

	/** The chased target must move this far from where the path was built before we repath. */
	constexpr float ZombieAIRepathDistance = 120.f;

	/** A different target must be this much closer (cm) to steal the chase from the current one. */
	constexpr float ZombieAITargetSwitchMargin = 150.f;
}

AShoterZombieController::AShoterZombieController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
	bWantsPlayerState = false;
}

void AShoterZombieController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	Target.Reset();
	MoveRequestTarget.Reset();
	MoveRequestTargetLocation = FVector::ZeroVector;
	bFallbackMovement = false;
	bAttackStance = false;
	bIdle = false;

	// Spread the searches of a whole wave over several frames.
	RetargetTimer = FMath::FRandRange(0.f, ZombieAIRetargetInterval);
}

void AShoterZombieController::OnUnPossess()
{
	StopMovement();
	ClearFocus(EAIFocusPriority::Gameplay);
	Target.Reset();
	MoveRequestTarget.Reset();
	bFallbackMovement = false;
	bAttackStance = false;

	Super::OnUnPossess();
}

AShoterZombie* AShoterZombieController::GetZombie() const
{
	return Cast<AShoterZombie>(GetPawn());
}

AShoterCharacter* AShoterZombieController::FindNearestTarget(const FVector& From, const AShoterCharacter* Current) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	AShoterCharacter* Best = nullptr;
	float BestDistance = TNumericLimits<float>::Max();

	for (TActorIterator<AShoterCharacter> It(World); It; ++It)
	{
		AShoterCharacter* Candidate = *It;
		if (!IsValid(Candidate) || !Candidate->IsAlive())
		{
			continue;
		}

		float Distance = static_cast<float>(FVector::Dist(From, Candidate->GetActorLocation()));
		if (Candidate == Current)
		{
			Distance -= ZombieAITargetSwitchMargin;
		}

		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = Candidate;
		}
	}

	return Best;
}

void AShoterZombieController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// All AI runs on the server only.
	if (!HasAuthority())
	{
		return;
	}

	AShoterZombie* Zombie = GetZombie();
	if (!Zombie || !Zombie->IsAlive())
	{
		return;
	}

	// --- Pick the target every ~0.3 s (immediately when the current one died) ---
	RetargetTimer -= DeltaSeconds;
	const AShoterCharacter* CurrentCharacter = Cast<AShoterCharacter>(Target.Get());
	const bool bTargetLost = !CurrentCharacter || !CurrentCharacter->IsAlive();
	const bool bThink = RetargetTimer <= 0.f || (bTargetLost && !bIdle);

	if (bThink)
	{
		RetargetTimer = ZombieAIRetargetInterval;
		Target = FindNearestTarget(Zombie->GetActorLocation(), CurrentCharacter);
	}

	AActor* CurrentTarget = Target.Get();

	// --- Nobody left alive: idle ---
	if (!CurrentTarget)
	{
		if (!bIdle)
		{
			bIdle = true;
			bAttackStance = false;
			bFallbackMovement = false;
			MoveRequestTarget.Reset();
			StopMovement();
			ClearFocus(EAIFocusPriority::Gameplay);
		}
		return;
	}
	bIdle = false;

	// --- In melee range (or mid-swing): stand still and hit ---
	if (Zombie->IsAttacking() || Zombie->IsTargetInAttackRange(CurrentTarget))
	{
		if (!bAttackStance)
		{
			bAttackStance = true;
			bFallbackMovement = false;
			MoveRequestTarget.Reset();
			StopMovement();
		}

		SetFocus(CurrentTarget);
		Zombie->StartAttack(CurrentTarget);
		return;
	}

	if (bAttackStance)
	{
		bAttackStance = false;
		ClearFocus(EAIFocusPriority::Gameplay);
	}

	// --- Chase over the nav mesh ---
	const EPathFollowingStatus::Type MoveStatus = GetMoveStatus();

	if (bThink)
	{
		const FVector TargetLocation = CurrentTarget->GetActorLocation();
		const bool bTargetChanged = MoveRequestTarget.Get() != CurrentTarget;
		const bool bTargetMoved = FVector::DistSquared(TargetLocation, MoveRequestTargetLocation) > FMath::Square(ZombieAIRepathDistance);
		const bool bMoveEnded = MoveStatus == EPathFollowingStatus::Idle;

		if (bTargetChanged || bTargetMoved || bMoveEnded)
		{
			const EPathFollowingRequestResult::Type Result = MoveToActor(CurrentTarget, Zombie->GetAttackRange() * 0.6f, true, true, false);
			bFallbackMovement = (Result == EPathFollowingRequestResult::Failed);
			MoveRequestTarget = CurrentTarget;
			MoveRequestTargetLocation = TargetLocation;
		}
	}

	// No path (or the path ended short of the target): walk straight at it.
	if (bFallbackMovement || GetMoveStatus() == EPathFollowingStatus::Idle)
	{
		FVector Direction = CurrentTarget->GetActorLocation() - Zombie->GetActorLocation();
		Direction.Z = 0.f;
		if (!Direction.IsNearlyZero())
		{
			Zombie->AddMovementInput(Direction.GetSafeNormal(), 1.f);
		}
	}
}
