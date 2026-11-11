#include "Player/ShoterCharacterMovement.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"

// ---------------------------------------------------------------------------
// Saved move: carries sprint/aim flags so the server simulates the same speed
// as the predicting client (no corrections when sprinting or aiming).
// ---------------------------------------------------------------------------

class FSavedMove_Shoter : public FSavedMove_Character
{
public:
	typedef FSavedMove_Character Super;

	uint8 bSavedWantsToSprint : 1;
	uint8 bSavedWantsToAim : 1;

	FSavedMove_Shoter()
		: bSavedWantsToSprint(0)
		, bSavedWantsToAim(0)
	{
	}

	virtual void Clear() override
	{
		Super::Clear();
		bSavedWantsToSprint = 0;
		bSavedWantsToAim = 0;
	}

	virtual uint8 GetCompressedFlags() const override
	{
		uint8 Result = Super::GetCompressedFlags();
		if (bSavedWantsToSprint)
		{
			Result |= FLAG_Custom_0;
		}
		if (bSavedWantsToAim)
		{
			Result |= FLAG_Custom_1;
		}
		return Result;
	}

	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const override
	{
		const FSavedMove_Shoter* Other = static_cast<const FSavedMove_Shoter*>(NewMove.Get());
		if (bSavedWantsToSprint != Other->bSavedWantsToSprint || bSavedWantsToAim != Other->bSavedWantsToAim)
		{
			return false;
		}
		return Super::CanCombineWith(NewMove, InCharacter, MaxDelta);
	}

	virtual void SetMoveFor(ACharacter* Character, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData) override
	{
		Super::SetMoveFor(Character, InDeltaTime, NewAccel, ClientData);
		if (const UShoterCharacterMovement* Movement = Cast<UShoterCharacterMovement>(Character->GetCharacterMovement()))
		{
			bSavedWantsToSprint = Movement->bWantsToSprint;
			bSavedWantsToAim = Movement->bWantsToAim;
		}
	}

	virtual void PrepMoveFor(ACharacter* Character) override
	{
		Super::PrepMoveFor(Character);
		if (UShoterCharacterMovement* Movement = Cast<UShoterCharacterMovement>(Character->GetCharacterMovement()))
		{
			Movement->bWantsToSprint = bSavedWantsToSprint;
			Movement->bWantsToAim = bSavedWantsToAim;
		}
	}
};

class FNetworkPredictionData_Client_Shoter : public FNetworkPredictionData_Client_Character
{
public:
	typedef FNetworkPredictionData_Client_Character Super;

	explicit FNetworkPredictionData_Client_Shoter(const UCharacterMovementComponent& ClientMovement)
		: Super(ClientMovement)
	{
	}

	virtual FSavedMovePtr AllocateNewMove() override
	{
		return FSavedMovePtr(new FSavedMove_Shoter());
	}
};

// ---------------------------------------------------------------------------

UShoterCharacterMovement::UShoterCharacterMovement()
{
	bWantsToSprint = false;
	bWantsToAim = false;

	MaxWalkSpeed = 520.f;
	MaxWalkSpeedCrouched = 250.f;
	MaxAcceleration = 2800.f;
	BrakingDecelerationWalking = 2400.f;
	GroundFriction = 9.f;
	JumpZVelocity = 640.f;
	AirControl = 0.4f;
	GravityScale = 1.35f;
	SetCrouchedHalfHeight(58.f);
	NavAgentProps.bCanCrouch = true;
	NavAgentProps.bCanJump = true;
	bCanWalkOffLedgesWhenCrouching = true;
	bOrientRotationToMovement = false;
	bUseControllerDesiredRotation = false;
}

FNetworkPredictionData_Client* UShoterCharacterMovement::GetPredictionData_Client() const
{
	check(PawnOwner != nullptr);
	if (ClientPredictionData == nullptr)
	{
		UShoterCharacterMovement* MutableThis = const_cast<UShoterCharacterMovement*>(this);
		MutableThis->ClientPredictionData = new FNetworkPredictionData_Client_Shoter(*this);
		MutableThis->ClientPredictionData->MaxSmoothNetUpdateDist = 92.f;
		MutableThis->ClientPredictionData->NoSmoothNetUpdateDist = 140.f;
	}
	return ClientPredictionData;
}

void UShoterCharacterMovement::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);
	bWantsToSprint = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
	bWantsToAim = (Flags & FSavedMove_Character::FLAG_Custom_1) != 0;
}

bool UShoterCharacterMovement::IsSprintDirectionValid() const
{
	if (!UpdatedComponent)
	{
		return false;
	}
	const FVector Accel2D(Acceleration.X, Acceleration.Y, 0.f);
	if (Accel2D.SizeSquared() < KINDA_SMALL_NUMBER)
	{
		return false;
	}
	return FVector::DotProduct(Accel2D.GetSafeNormal(), UpdatedComponent->GetForwardVector()) > 0.5f;
}

bool UShoterCharacterMovement::IsSprinting() const
{
	return bWantsToSprint && !bWantsToAim && IsMovingOnGround() && !IsCrouching() && !IsSliding() && IsSprintDirectionValid();
}

bool UShoterCharacterMovement::IsSliding() const
{
	return MovementMode == MOVE_Custom && CustomMovementMode == static_cast<uint8>(EShoterCustomMovement::Slide);
}

float UShoterCharacterMovement::GetMaxSpeed() const
{
	if (IsSliding())
	{
		return MaxWalkSpeed * SprintSpeedMultiplier * 1.6f;
	}

	float Speed = Super::GetMaxSpeed();
	if ((MovementMode == MOVE_Walking || MovementMode == MOVE_NavWalking || MovementMode == MOVE_Falling) && !IsCrouching())
	{
		if (bWantsToAim)
		{
			Speed *= AimSpeedMultiplier;
		}
		else if (bWantsToSprint && IsSprintDirectionValid())
		{
			Speed *= SprintSpeedMultiplier;
		}
	}
	return Speed * WeaponSpeedMultiplier;
}

float UShoterCharacterMovement::GetMaxBrakingDeceleration() const
{
	if (IsSliding())
	{
		return SlideBrakingDeceleration;
	}
	return Super::GetMaxBrakingDeceleration();
}

bool UShoterCharacterMovement::IsMovingOnGround() const
{
	return Super::IsMovingOnGround() || IsSliding();
}

bool UShoterCharacterMovement::CanCrouchInCurrentState() const
{
	return Super::CanCrouchInCurrentState();
}

bool UShoterCharacterMovement::CanAttemptJump() const
{
	return Super::CanAttemptJump() || (IsSliding() && IsJumpAllowed());
}

bool UShoterCharacterMovement::DoJump(bool bReplayingMoves, float DeltaTime)
{
	const bool bWasFalling = IsFalling();
	if (!Super::DoJump(bReplayingMoves, DeltaTime))
	{
		return false;
	}

	// Mid-air jump: thruster burst toward the input direction.
	if (bWasFalling)
	{
		const FVector InputDirection = FVector(Acceleration.X, Acceleration.Y, 0.f).GetSafeNormal();
		if (!InputDirection.IsNearlyZero())
		{
			const float HorizontalSpeed = FMath::Max(FVector(Velocity.X, Velocity.Y, 0.f).Size(), AirJumpHorizontalSpeed);
			Velocity.X = InputDirection.X * HorizontalSpeed;
			Velocity.Y = InputDirection.Y * HorizontalSpeed;
		}
	}
	return true;
}

void UShoterCharacterMovement::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	if (MovementMode == MOVE_Walking && bWantsToCrouch && !IsCrouching() && CanEnterSlide())
	{
		EnterSlide();
	}
	else if (IsSliding() && !bWantsToCrouch)
	{
		ExitSlide();
	}

	Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);
}

bool UShoterCharacterMovement::CanEnterSlide() const
{
	return bWantsToSprint && !bWantsToAim && IsSprintDirectionValid()
		&& FVector(Velocity.X, Velocity.Y, 0.f).SizeSquared() > FMath::Square(SlideMinSpeed);
}

void UShoterCharacterMovement::EnterSlide()
{
	const FVector Velocity2D(Velocity.X, Velocity.Y, 0.f);
	const float CurrentSpeed = Velocity2D.Size();
	// Cap the boost so chaining slides cannot build infinite speed.
	const float TargetSpeed = FMath::Min(CurrentSpeed + SlideEnterBoost, MaxWalkSpeed * SprintSpeedMultiplier + SlideEnterBoost);
	const FVector NewVelocity2D = Velocity2D.GetSafeNormal() * TargetSpeed;
	Velocity.X = NewVelocity2D.X;
	Velocity.Y = NewVelocity2D.Y;
	SetMovementMode(MOVE_Custom, static_cast<uint8>(EShoterCustomMovement::Slide));
}

void UShoterCharacterMovement::ExitSlide()
{
	SetMovementMode(MOVE_Walking);
}

void UShoterCharacterMovement::PhysCustom(float DeltaTime, int32 Iterations)
{
	Super::PhysCustom(DeltaTime, Iterations);

	if (CustomMovementMode == static_cast<uint8>(EShoterCustomMovement::Slide))
	{
		PhysSlide(DeltaTime, Iterations);
	}
}

bool UShoterCharacterMovement::FindSlideSurface(FHitResult& OutHit) const
{
	if (!UpdatedComponent || !CharacterOwner)
	{
		return false;
	}
	const FVector Start = UpdatedComponent->GetComponentLocation();
	const float HalfHeight = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector End = Start + FVector::DownVector * (HalfHeight + 25.f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ShoterSlideSurface), false, CharacterOwner);
	return GetWorld()->LineTraceSingleByChannel(OutHit, Start, End, UpdatedComponent->GetCollisionObjectType(), Params);
}

void UShoterCharacterMovement::PhysSlide(float DeltaTime, int32 Iterations)
{
	if (DeltaTime < MIN_TICK_TIME)
	{
		return;
	}

	RestorePreAdditiveRootMotionVelocity();

	FHitResult SurfaceHit;
	if (!FindSlideSurface(SurfaceHit) || FVector(Velocity.X, Velocity.Y, 0.f).SizeSquared() < FMath::Square(SlideMinSpeed))
	{
		ExitSlide();
		StartNewPhysics(DeltaTime, Iterations);
		return;
	}

	// Gravity pulls along slopes (downhill slides speed up).
	Velocity += SlideGravityForce * FVector::DownVector * DeltaTime;

	// Only strafing input steers a slide.
	const FVector Right = UpdatedComponent->GetRightVector();
	if (FMath::Abs(FVector::DotProduct(Acceleration.GetSafeNormal(), Right)) > 0.5f)
	{
		Acceleration = Acceleration.ProjectOnTo(Right);
	}
	else
	{
		Acceleration = FVector::ZeroVector;
	}

	if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
	{
		CalcVelocity(DeltaTime, SlideFriction, false, GetMaxBrakingDeceleration());
	}
	ApplyRootMotionToVelocity(DeltaTime);

	Iterations++;
	bJustTeleported = false;

	const FVector OldLocation = UpdatedComponent->GetComponentLocation();
	const FQuat Rotation = UpdatedComponent->GetComponentQuat();
	const FVector Delta = Velocity * DeltaTime;
	FHitResult Hit(1.f);
	SafeMoveUpdatedComponent(Delta, Rotation, true, Hit);

	if (Hit.Time < 1.f)
	{
		HandleImpact(Hit, DeltaTime, Delta);
		SlideAlongSurface(Delta, 1.f - Hit.Time, Hit.Normal, Hit, true);
	}

	FHitResult NewSurfaceHit;
	if (!FindSlideSurface(NewSurfaceHit) || FVector(Velocity.X, Velocity.Y, 0.f).SizeSquared() < FMath::Square(SlideMinSpeed))
	{
		ExitSlide();
	}

	if (!bJustTeleported && !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
	{
		Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / DeltaTime;
	}
}
