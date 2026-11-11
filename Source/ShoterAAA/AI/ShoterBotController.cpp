#include "AI/ShoterBotController.h"
#include "Game/ShoterGameMode.h"
#include "Player/ShoterCharacter.h"
#include "Weapons/ShoterWeapon.h"
#include "AITypes.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"

namespace
{
	/** Seconds between decisions; aiming and trigger timing run every frame. */
	constexpr float BotThinkInterval = 0.15f;

	/** Enemies farther than this are never noticed. */
	constexpr float BotSightRange = 8000.f;

	/** Half angle of the 140 degree view cone. */
	constexpr float BotViewConeHalfAngleDegrees = 70.f;

	/** After taking damage the view cone is ignored for this long. */
	constexpr float BotDamageAwarenessTime = 2.f;

	/** How long the last known enemy position is remembered. */
	constexpr float BotEnemyMemoryTime = 5.f;

	constexpr float BotAdsMinDistance = 1500.f;

	constexpr float BotGrenadeMinDistance = 700.f;
	constexpr float BotGrenadeMaxDistance = 1500.f;
	constexpr float BotGrenadeCooldown = 15.f;

	constexpr float BotHuntAcceptanceRadius = 800.f;
	constexpr float BotHuntRepathInterval = 2.f;
	constexpr float BotWanderRadius = 1500.f;

	/** Below this horizontal speed, with a move active, the bot counts as stuck. */
	constexpr float BotStuckSpeedThreshold = 25.f;
	constexpr float BotStuckTimeLimit = 1.f;
}

AShoterBotController::AShoterBotController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Bots appear in the scoreboard like players.
	bWantsPlayerState = true;

	// We drive the control rotation ourselves (aim error, smoothing, look direction).
	bSetControlRotationFromPawnOrientation = false;

	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

AShoterCharacter* AShoterBotController::GetBotCharacter() const
{
	return Cast<AShoterCharacter>(GetPawn());
}

float AShoterBotController::GetClampedSkill() const
{
	return FMath::Clamp(Skill, 0.f, 1.f);
}

void AShoterBotController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	ResetState();
	bWasDead = false;

	// Spread the thinking of all bots over several frames.
	ThinkTimer = FMath::FRandRange(0.f, BotThinkInterval);

	const UWorld* World = GetWorld();
	const float Now = World ? static_cast<float>(World->GetTimeSeconds()) : 0.f;
	NextGrenadeTime = Now + FMath::FRandRange(3.f, 8.f);

	if (InPawn)
	{
		AimRotation = FRotator(0.f, InPawn->GetActorRotation().Yaw, 0.f);
		SetControlRotation(AimRotation);

		if (const AShoterCharacter* Bot = Cast<AShoterCharacter>(InPawn))
		{
			// Old damage must not look like a fresh hit.
			SeenDamageTimeValue = Bot->GetLastDamageTime();
		}
	}
}

void AShoterBotController::OnUnPossess()
{
	// The pawn is still reachable here; the base class clears it. Skip the calls that touch weapons and timers
	// while the world is going away (the controller can be unpossessed during level teardown).
	const UWorld* World = GetWorld();
	if (World && !World->bIsTearingDown)
	{
		if (AShoterCharacter* Bot = GetBotCharacter())
		{
			StopCombatActions(Bot);
		}
		StopMovement();
	}

	ResetState();

	Super::OnUnPossess();
}

void AShoterBotController::GameHasEnded(AActor* EndGameFocus, bool bIsWinner)
{
	bMatchOver = true;

	if (AShoterCharacter* Bot = GetBotCharacter())
	{
		StopCombatActions(Bot);
	}
	StopMovement();

	Super::GameHasEnded(EndGameFocus, bIsWinner);
}

void AShoterBotController::ResetState()
{
	Enemy.Reset();
	NearestEnemy.Reset();
	bEnemyVisible = false;
	bHasLastKnown = false;
	LastKnownEnemyLocation = FVector::ZeroVector;
	LastSeenEnemyTime = -1000.f;
	DamageNoticedTime = -1000.f;
	ReactionEndTime = 0.f;

	AimErrorYaw = 0.f;
	AimErrorPitch = 0.f;
	NextAimErrorTime = 0.f;
	TrackingTime = 0.f;
	bAimAtHead = false;
	bAimedAtEnemy = false;

	bTriggerDown = false;
	BurstEndTime = 0.f;
	NextBurstTime = 0.f;
	TapReleaseTime = 0.f;
	NextTapTime = 0.f;
	NextWeaponSwitchTime = 0.f;
	bPrefersAds = false;
	bSprintRequested = false;

	MoveMode = EMoveMode::None;
	NextStrafeTime = 0.f;
	NextRepathTime = 0.f;
	WanderUntilTime = 0.f;
	StuckTime = 0.f;
	JumpReleaseTime = 0.f;
	bJumpHeld = false;
}

void AShoterBotController::ReleaseTrigger(AShoterCharacter* Bot)
{
	if (Bot)
	{
		Bot->StopFire();
	}
	bTriggerDown = false;
}

void AShoterBotController::StopCombatActions(AShoterCharacter* Bot)
{
	if (!Bot)
	{
		return;
	}

	ReleaseTrigger(Bot);

	if (Bot->IsAiming())
	{
		Bot->SetAiming(false);
	}
	if (bSprintRequested)
	{
		Bot->SetSprinting(false);
		bSprintRequested = false;
	}
	if (bJumpHeld)
	{
		Bot->StopJumping();
		bJumpHeld = false;
	}
}

void AShoterBotController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// All AI runs on the server only. After the match nothing moves or shoots any more.
	if (!HasAuthority() || bMatchOver)
	{
		return;
	}

	AShoterCharacter* Bot = GetBotCharacter();
	if (!Bot)
	{
		return;
	}

	if (!Bot->IsAlive())
	{
		// Dead: drop everything once, then wait for the respawn (OnPossess resets the state again).
		if (!bWasDead)
		{
			bWasDead = true;
			StopCombatActions(Bot);
			StopMovement();
			ResetState();
		}
		return;
	}
	bWasDead = false;

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const float Now = static_cast<float>(World->GetTimeSeconds());

	ThinkTimer -= DeltaSeconds;
	if (ThinkTimer <= 0.f)
	{
		ThinkTimer = BotThinkInterval * FMath::FRandRange(0.85f, 1.15f);
		Think(Bot, Now);
	}

	UpdateStuckDetection(Bot, DeltaSeconds, Now);
	UpdateAim(Bot, DeltaSeconds, Now);
	UpdateFiring(Bot, Now);
}

void AShoterBotController::Think(AShoterCharacter* Bot, float Now)
{
	UpdatePerception(Bot, Now);
	UpdateMovement(Bot, Now);
	UpdateWeaponHandling(Bot, Now);
}

// --- Perception ---

FVector AShoterBotController::GetChestLocation(const AShoterCharacter* Target) const
{
	const float HalfHeight = Target->GetCapsuleComponent() ? Target->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
	return Target->GetActorLocation() + FVector(0.f, 0.f, HalfHeight * 0.30f);
}

FVector AShoterBotController::GetHeadLocation(const AShoterCharacter* Target) const
{
	const float HalfHeight = Target->GetCapsuleComponent() ? Target->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
	return Target->GetActorLocation() + FVector(0.f, 0.f, HalfHeight * 0.78f);
}

bool AShoterBotController::CanSeeCharacter(const AShoterCharacter* Bot, const AShoterCharacter* Target) const
{
	UWorld* World = GetWorld();
	if (!World || !Bot || !Target)
	{
		return false;
	}

	// Ignore our own pawn and the weapons attached to it.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ShoterBotSight), false, Bot);
	TArray<AActor*> AttachedActors;
	Bot->GetAttachedActors(AttachedActors);
	Params.AddIgnoredActors(AttachedActors);

	const FVector Eye = Bot->GetPawnViewLocation();
	const FVector Points[2] = { GetChestLocation(Target), GetHeadLocation(Target) };

	for (const FVector& Point : Points)
	{
		FHitResult Hit;
		const bool bBlocked = World->LineTraceSingleByChannel(Hit, Eye, Point, ECC_Visibility, Params);
		if (!bBlocked || Hit.GetActor() == Target)
		{
			return true;
		}
	}

	return false;
}

void AShoterBotController::UpdatePerception(AShoterCharacter* Bot, float Now)
{
	UWorld* World = GetWorld();
	AShoterGameMode* GameMode = World ? World->GetAuthGameMode<AShoterGameMode>() : nullptr;
	if (!GameMode)
	{
		return;
	}

	const float Skill01 = GetClampedSkill();

	// Taking damage makes us notice enemies all around us for a while.
	const float LastDamageTime = Bot->GetLastDamageTime();
	if (!FMath::IsNearlyEqual(LastDamageTime, SeenDamageTimeValue))
	{
		SeenDamageTimeValue = LastDamageTime;
		DamageNoticedTime = Now;
	}
	const bool bIgnoreCone = (Now - DamageNoticedTime) < BotDamageAwarenessTime;

	const FVector Eye = Bot->GetPawnViewLocation();
	const FVector Forward = GetControlRotation().Vector();
	const float MinDot = FMath::Cos(FMath::DegreesToRadians(BotViewConeHalfAngleDegrees));
	const AActor* CurrentEnemy = Enemy.Get();

	AShoterCharacter* BestVisible = nullptr;
	float BestVisibleScore = TNumericLimits<float>::Max();
	AShoterCharacter* Nearest = nullptr;
	float NearestDistance = TNumericLimits<float>::Max();

	for (TActorIterator<AShoterCharacter> It(World); It; ++It)
	{
		AShoterCharacter* Other = *It;
		if (Other == Bot || !IsValid(Other) || !Other->IsAlive())
		{
			continue;
		}

		AController* OtherController = Other->GetController();
		if (!OtherController || !GameMode->AreEnemies(this, OtherController))
		{
			continue;
		}

		const float Distance = static_cast<float>(FVector::Dist(Eye, Other->GetActorLocation()));
		if (Distance < NearestDistance)
		{
			NearestDistance = Distance;
			Nearest = Other;
		}

		// Spawn-protected enemies cannot be hurt: do not waste ammo on them.
		if (Distance > BotSightRange || Other->IsSpawnProtected())
		{
			continue;
		}

		if (!bIgnoreCone)
		{
			const FVector ToOther = (GetChestLocation(Other) - Eye).GetSafeNormal();
			if ((Forward | ToOther) < MinDot)
			{
				continue;
			}
		}

		// The current enemy gets a distance bonus so we do not flip-flop between two similar targets.
		const float Score = (Other == CurrentEnemy) ? Distance * 0.85f : Distance;
		if (Score >= BestVisibleScore)
		{
			continue;	// cannot beat the best one: skip the trace
		}

		if (!CanSeeCharacter(Bot, Other))
		{
			continue;
		}

		BestVisibleScore = Score;
		BestVisible = Other;
	}

	NearestEnemy = Nearest;

	if (BestVisible)
	{
		if (BestVisible != CurrentEnemy)
		{
			// New enemy acquired: it takes a moment to react before shooting.
			Enemy = BestVisible;
			ReactionEndTime = Now + FMath::Lerp(0.55f, 0.18f, Skill01);
			TrackingTime = 0.f;
			NextAimErrorTime = 0.f;

			// Better bots sometimes use the sights at long range (snipers always do).
			const AShoterWeapon* Weapon = Bot->GetCurrentWeapon();
			const bool bScoped = Weapon && Weapon->GetStats().bHasScope;
			bPrefersAds = Skill01 >= 0.3f && (bScoped || FMath::FRand() < Skill01 * 0.7f);
		}

		bEnemyVisible = true;
		bHasLastKnown = true;
		LastSeenEnemyTime = Now;
		LastKnownEnemyLocation = BestVisible->GetActorLocation();
	}
	else
	{
		bEnemyVisible = false;

		// Keep remembering the enemy for a few seconds, unless it died.
		const AShoterCharacter* Remembered = Cast<AShoterCharacter>(Enemy.Get());
		if (!Remembered || !Remembered->IsAlive() || (Now - LastSeenEnemyTime) > BotEnemyMemoryTime)
		{
			Enemy.Reset();
			bHasLastKnown = false;
		}
	}
}

// --- Aim (every frame) ---

void AShoterBotController::UpdateAim(AShoterCharacter* Bot, float DeltaSeconds, float Now)
{
	const float Skill01 = GetClampedSkill();
	const FVector Eye = Bot->GetPawnViewLocation();
	const AShoterCharacter* EnemyCharacter = Cast<AShoterCharacter>(Enemy.Get());

	// Default: hold the heading with a level view.
	FRotator DesiredRotation(0.f, AimRotation.Yaw, 0.f);
	float InterpSpeed = 6.f;
	bAimedAtEnemy = false;

	if (EnemyCharacter && bEnemyVisible)
	{
		TrackingTime += DeltaSeconds;
		InterpSpeed = FMath::Lerp(5.f, 14.f, Skill01);

		// The error is re-rolled every half second (and the head is picked more often by better bots).
		if (Now >= NextAimErrorTime)
		{
			NextAimErrorTime = Now + FMath::FRandRange(0.4f, 0.6f);
			bAimAtHead = FMath::FRand() < FMath::Lerp(0.10f, 0.45f, Skill01);

			const float MaxError = FMath::Lerp(5.f, 1.2f, Skill01);
			AimErrorYaw = FMath::FRandRange(-MaxError, MaxError);
			AimErrorPitch = FMath::FRandRange(-MaxError, MaxError) * 0.6f;
		}

		// The longer we track the same enemy, the smaller the error gets.
		const float ErrorScale = FMath::Lerp(1.f, 0.4f, FMath::Clamp(TrackingTime / 2.5f, 0.f, 1.f));

		FVector AimPoint = bAimAtHead ? GetHeadLocation(EnemyCharacter) : GetChestLocation(EnemyCharacter);

		// Lead moving targets when firing slow projectiles.
		const AShoterWeapon* Weapon = Bot->GetCurrentWeapon();
		if (Weapon && Weapon->GetWeaponType() == EShoterWeaponType::Launcher && Weapon->GetStats().ProjectileSpeed > 1.f)
		{
			const float TravelTime = static_cast<float>(FVector::Dist(Eye, AimPoint)) / Weapon->GetStats().ProjectileSpeed;
			AimPoint += EnemyCharacter->GetVelocity() * TravelTime * FMath::Lerp(0.4f, 1.f, Skill01);
		}

		const FRotator ToAimPoint = (AimPoint - Eye).Rotation();
		DesiredRotation = ToAimPoint + FRotator(AimErrorPitch * ErrorScale, AimErrorYaw * ErrorScale, 0.f);
		DesiredRotation.Normalize();

		// "Aimed" is measured against the enemy's chest, without the deliberate error, from where we really look.
		const FVector ChestPoint = GetChestLocation(EnemyCharacter);
		const FVector ToChest = (ChestPoint - Eye).GetSafeNormal();
		const float CosAngle = FMath::Clamp(static_cast<float>(AimRotation.Vector() | ToChest), -1.f, 1.f);
		const float AngleDegrees = FMath::RadiansToDegrees(FMath::Acos(CosAngle));
		const float Distance = FMath::Max(static_cast<float>(FVector::Dist(Eye, ChestPoint)), 1.f);
		const float AllowedDegrees = FMath::Max(4.f, FMath::RadiansToDegrees(FMath::Atan2(70.f, Distance)) + 2.f);
		bAimedAtEnemy = AngleDegrees <= AllowedDegrees;
	}
	else
	{
		TrackingTime = FMath::Max(0.f, TrackingTime - DeltaSeconds * 2.f);

		if (bHasLastKnown)
		{
			// Face the spot where the enemy was last seen.
			const FRotator ToSpot = (LastKnownEnemyLocation - Eye).Rotation();
			DesiredRotation = FRotator(0.f, ToSpot.Yaw, 0.f);
			InterpSpeed = FMath::Lerp(4.f, 8.f, Skill01);
		}
		else
		{
			// No enemy: look along the movement direction.
			const FVector Velocity = Bot->GetVelocity();
			if (Velocity.SizeSquared2D() > FMath::Square(60.f))
			{
				DesiredRotation = FRotator(0.f, Velocity.Rotation().Yaw, 0.f);
			}
		}
	}

	AimRotation = FMath::RInterpTo(AimRotation, DesiredRotation, DeltaSeconds, InterpSpeed);
	AimRotation.Pitch = FMath::ClampAngle(static_cast<float>(AimRotation.Pitch), -80.f, 80.f);
	AimRotation.Roll = 0.f;

	// Weapons shoot along the control rotation; the body follows it like it would for a player.
	SetControlRotation(AimRotation);
	Bot->FaceRotation(AimRotation, DeltaSeconds);
}

// --- Trigger discipline (every frame) ---

void AShoterBotController::UpdateFiring(AShoterCharacter* Bot, float Now)
{
	AShoterWeapon* Weapon = Bot->GetCurrentWeapon();
	const AShoterCharacter* EnemyCharacter = Cast<AShoterCharacter>(Enemy.Get());

	const bool bCanShoot = Weapon && EnemyCharacter && EnemyCharacter->IsAlive()
		&& bEnemyVisible && bAimedAtEnemy && Now >= ReactionEndTime && Weapon->CanFire();

	// Enemy lost or dead, not aimed yet, out of ammo, reloading...: hands off the trigger.
	if (!bCanShoot)
	{
		if (bTriggerDown)
		{
			ReleaseTrigger(Bot);
		}
		return;
	}

	if (Weapon->GetStats().bAutomatic)
	{
		// Bursts: 0.25 - 0.7 s on, 0.2 - 0.5 s off.
		if (bTriggerDown)
		{
			if (Now >= BurstEndTime)
			{
				ReleaseTrigger(Bot);
				NextBurstTime = Now + FMath::FRandRange(0.2f, 0.5f);
			}
		}
		else if (Now >= NextBurstTime)
		{
			Bot->StartFire();
			bTriggerDown = true;
			BurstEndTime = Now + FMath::FRandRange(0.25f, 0.7f);
		}
	}
	else
	{
		// Semi-automatic: one tap every 0.25 - 0.4 s (the trigger is held for a moment so the shot registers).
		if (bTriggerDown)
		{
			if (Now >= TapReleaseTime)
			{
				ReleaseTrigger(Bot);
			}
		}
		else if (Now >= NextTapTime)
		{
			Bot->StartFire();
			bTriggerDown = true;
			TapReleaseTime = Now + 0.1f;
			NextTapTime = Now + FMath::FRandRange(0.25f, 0.4f);
		}
	}
}

// --- Weapon decisions (think rate) ---

void AShoterBotController::UpdateWeaponHandling(AShoterCharacter* Bot, float Now)
{
	AShoterWeapon* Weapon = Bot->GetCurrentWeapon();
	if (!Weapon)
	{
		return;
	}

	const AShoterCharacter* EnemyCharacter = Cast<AShoterCharacter>(Enemy.Get());
	const bool bHasTarget = bEnemyVisible && EnemyCharacter;
	const float EnemyDistance = bHasTarget ? static_cast<float>(FVector::Dist(Bot->GetActorLocation(), EnemyCharacter->GetActorLocation())) : 0.f;

	// 1) Nothing left to shoot with: swap to a weapon that still has ammo.
	if (!Weapon->HasAnyAmmo())
	{
		if (Now >= NextWeaponSwitchTime)
		{
			NextWeaponSwitchTime = Now + 1.f;

			for (const TObjectPtr<AShoterWeapon>& Candidate : Bot->GetInventory())
			{
				if (Candidate && Candidate.Get() != Weapon && Candidate->HasAnyAmmo())
				{
					ReleaseTrigger(Bot);
					Bot->EquipWeapon(Candidate.Get());
					break;
				}
			}
		}
		return;
	}

	// 2) Reload when the clip is empty, or when nobody is around and the clip is under half.
	if (!Weapon->IsReloading() && Weapon->CanReload())
	{
		const int32 Clip = Weapon->GetAmmoInClip();
		const bool bClipEmpty = Clip <= 0;
		const bool bQuietAndLow = !bEnemyVisible && !Enemy.IsValid() && (Clip * 2 < Weapon->GetStats().MagSize);

		if (bClipEmpty || bQuietAndLow)
		{
			ReleaseTrigger(Bot);
			Bot->Reload();
		}
	}

	// 3) Aim down sights at long range (some of the time, better bots only).
	bool bWantAim = false;
	if (bHasTarget && bPrefersAds)
	{
		const float Threshold = Bot->IsAiming() ? BotAdsMinDistance * 0.8f : BotAdsMinDistance;	// hysteresis
		bWantAim = EnemyDistance > Threshold;
	}
	if (bWantAim != Bot->IsAiming())
	{
		Bot->SetAiming(bWantAim);
	}

	// 4) Grenade at mid range, once in a while.
	if (bHasTarget && Now >= NextGrenadeTime && Now >= ReactionEndTime && Bot->GetGrenades() > 0
		&& EnemyDistance >= BotGrenadeMinDistance && EnemyDistance <= BotGrenadeMaxDistance
		&& FMath::FRand() < 0.05f)
	{
		Bot->ThrowGrenade();
		NextGrenadeTime = Now + BotGrenadeCooldown;
	}

	// 5) Sprint while traveling (hunting, heading to the last known spot), never while fighting.
	const bool bWantSprint = !bHasTarget && MoveMode != EMoveMode::None && !Weapon->IsReloading() && !Bot->IsAiming();
	if (bWantSprint != bSprintRequested)
	{
		bSprintRequested = bWantSprint;
		Bot->SetSprinting(bWantSprint);
	}
}

// --- Movement ---

bool AShoterBotController::PickWanderPoint(const FVector& Origin, float Radius, FVector& OutPoint) const
{
	UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetCurrent(GetWorld());
	if (!NavSystem)
	{
		return false;
	}

	FNavLocation Result;
	if (NavSystem->GetRandomReachablePointInRadius(Origin, Radius, Result))
	{
		OutPoint = Result.Location;
		return true;
	}
	return false;
}

bool AShoterBotController::PickStrafePoint(const FVector& Origin, const FVector& EnemyLocation, FVector& OutPoint) const
{
	UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetCurrent(GetWorld());
	if (!NavSystem)
	{
		return false;
	}

	const FVector ToEnemy = (EnemyLocation - Origin).GetSafeNormal2D();
	const float EnemyDistance = static_cast<float>(FVector::Dist2D(EnemyLocation, Origin));

	// Far enemy: edge closer while sidestepping. Very close enemy: edge away. Otherwise a pure sidestep.
	const float DesiredAlong = EnemyDistance > 2200.f ? 0.5f : (EnemyDistance < 400.f ? -0.5f : 0.f);

	// Sample reachable points around us and keep the one that best matches "300 - 600 cm to the side".
	bool bFound = false;
	float BestScore = -TNumericLimits<float>::Max();

	for (int32 Attempt = 0; Attempt < 6; ++Attempt)
	{
		FNavLocation Candidate;
		if (!NavSystem->GetRandomReachablePointInRadius(Origin, 600.f, Candidate))
		{
			continue;
		}

		const FVector Offset = Candidate.Location - Origin;
		const float Distance = static_cast<float>(Offset.Size2D());
		if (Distance < 250.f)
		{
			continue;
		}

		const float Along = static_cast<float>(Offset.GetSafeNormal2D() | ToEnemy);
		float Score = -FMath::Abs(Along - DesiredAlong);
		if (Distance < 300.f)
		{
			Score -= 0.3f;
		}
		Score += FMath::FRand() * 0.15f;

		if (Score > BestScore)
		{
			BestScore = Score;
			OutPoint = Candidate.Location;
			bFound = true;
		}
	}

	return bFound;
}

void AShoterBotController::StartWander(const FVector& Origin, float Now)
{
	MoveMode = EMoveMode::Wander;
	NextRepathTime = Now + FMath::FRandRange(3.f, 5.f);

	FVector Point;
	if (PickWanderPoint(Origin, BotWanderRadius, Point))
	{
		MoveToLocation(Point, 60.f, true, true, false, true);
	}
}

void AShoterBotController::ForceNewMove(float Now)
{
	StopMovement();
	MoveMode = EMoveMode::None;
	NextStrafeTime = 0.f;
	NextRepathTime = 0.f;

	// Out of sight of any enemy: roam somewhere else for a moment instead of retrying the same blocked path.
	WanderUntilTime = bEnemyVisible ? 0.f : Now + 1.5f;
}

void AShoterBotController::UpdateMovement(AShoterCharacter* Bot, float Now)
{
	UWorld* World = GetWorld();
	if (!World || !UNavigationSystemV1::GetCurrent(World))
	{
		return;	// no nav mesh in this level: nothing to path on
	}

	const FVector BotLocation = Bot->GetActorLocation();
	const EPathFollowingStatus::Type Status = GetMoveStatus();
	const AShoterCharacter* EnemyCharacter = Cast<AShoterCharacter>(Enemy.Get());

	// 1) Enemy in sight: strafe to a random reachable point every 1 - 2 s.
	if (bEnemyVisible && EnemyCharacter)
	{
		if (MoveMode != EMoveMode::Strafe)
		{
			MoveMode = EMoveMode::Strafe;
			NextStrafeTime = 0.f;
		}

		if (Now >= NextStrafeTime || Status == EPathFollowingStatus::Idle)
		{
			FVector Point;
			if (PickStrafePoint(BotLocation, EnemyCharacter->GetActorLocation(), Point))
			{
				MoveToLocation(Point, 40.f, true, true, false, true);
				NextStrafeTime = Now + FMath::FRandRange(1.f, 2.f);
			}
			else
			{
				NextStrafeTime = Now + 0.4f;
			}
		}
		return;
	}

	// 2) Enemy lost: walk to where it was last seen.
	if (bHasLastKnown)
	{
		if (FVector::Dist2D(BotLocation, LastKnownEnemyLocation) < 200.f)
		{
			// Nothing here: stop remembering and start hunting.
			bHasLastKnown = false;
		}
		else
		{
			if (MoveMode != EMoveMode::LastKnown || Status == EPathFollowingStatus::Idle)
			{
				MoveMode = EMoveMode::LastKnown;
				const EPathFollowingRequestResult::Type Result = MoveToLocation(LastKnownEnemyLocation, 100.f, true, true, true, true);
				if (Result == EPathFollowingRequestResult::Failed)
				{
					bHasLastKnown = false;	// unreachable
				}
			}
			return;
		}
	}

	// 3a) No path to anyone (or too close to see them): roam for a few seconds before trying again.
	if (Now < WanderUntilTime)
	{
		if (Status == EPathFollowingStatus::Idle)
		{
			StartWander(BotLocation, Now);
		}
		return;
	}

	// 3b) Hunt: move toward the nearest enemy, repathing every 2 s.
	AActor* Hunted = NearestEnemy.Get();
	if (Hunted)
	{
		if (MoveMode != EMoveMode::Hunt || Now >= NextRepathTime || Status == EPathFollowingStatus::Idle)
		{
			MoveMode = EMoveMode::Hunt;
			NextRepathTime = Now + BotHuntRepathInterval;

			const EPathFollowingRequestResult::Type Result = MoveToActor(Hunted, BotHuntAcceptanceRadius, true, true, true);
			if (Result != EPathFollowingRequestResult::RequestSuccessful)
			{
				// Failed: no nav path. AlreadyAtGoal: close but cannot see them. Either way, roam for a bit.
				WanderUntilTime = Now + FMath::FRandRange(3.f, 5.f);
				StartWander(BotLocation, Now);
			}
		}
		return;
	}

	// 3c) Nobody to hunt: wander.
	if (MoveMode != EMoveMode::Wander || Status == EPathFollowingStatus::Idle)
	{
		StartWander(BotLocation, Now);
	}
}

void AShoterBotController::UpdateStuckDetection(AShoterCharacter* Bot, float DeltaSeconds, float Now)
{
	if (bJumpHeld && Now >= JumpReleaseTime)
	{
		Bot->StopJumping();
		bJumpHeld = false;
	}

	// "Stuck" = a move is active but we are barely moving.
	const bool bMoveActive = GetMoveStatus() == EPathFollowingStatus::Moving;
	if (bMoveActive && Bot->GetVelocity().SizeSquared2D() < FMath::Square(BotStuckSpeedThreshold))
	{
		StuckTime += DeltaSeconds;
		if (StuckTime >= BotStuckTimeLimit)
		{
			StuckTime = 0.f;

			Bot->Jump();
			bJumpHeld = true;
			JumpReleaseTime = Now + 0.25f;

			ForceNewMove(Now);
		}
	}
	else
	{
		StuckTime = 0.f;
	}
}
