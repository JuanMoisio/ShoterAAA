// Character movement with network-predicted sprint, aim-walk, slide and a thruster double jump.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ShoterCharacterMovement.generated.h"

UENUM(BlueprintType)
enum class EShoterCustomMovement : uint8
{
	None = 0,
	Slide = 1
};

UCLASS()
class SHOTERAAA_API UShoterCharacterMovement : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UShoterCharacterMovement();

	// --- Tunables ---
	UPROPERTY(EditDefaultsOnly, Category = "Shoter|Sprint")
	float SprintSpeedMultiplier = 1.55f;

	UPROPERTY(EditDefaultsOnly, Category = "Shoter|Aim")
	float AimSpeedMultiplier = 0.6f;

	/** Minimum horizontal speed to start/keep sliding. */
	UPROPERTY(EditDefaultsOnly, Category = "Shoter|Slide")
	float SlideMinSpeed = 420.f;

	UPROPERTY(EditDefaultsOnly, Category = "Shoter|Slide")
	float SlideEnterBoost = 380.f;

	UPROPERTY(EditDefaultsOnly, Category = "Shoter|Slide")
	float SlideGravityForce = 4000.f;

	UPROPERTY(EditDefaultsOnly, Category = "Shoter|Slide")
	float SlideFriction = 0.35f;

	UPROPERTY(EditDefaultsOnly, Category = "Shoter|Slide")
	float SlideBrakingDeceleration = 650.f;

	/** Horizontal speed given by the mid-air (second) jump in the input direction. */
	UPROPERTY(EditDefaultsOnly, Category = "Shoter|Jump")
	float AirJumpHorizontalSpeed = 560.f;

	/** Multiplier from the equipped weapon (heavier weapons are slower). Set by the character. */
	float WeaponSpeedMultiplier = 1.f;

	// --- Input state (predicted through saved moves) ---
	void SetWantsToSprint(bool bNewWants) { bWantsToSprint = bNewWants; }
	void SetWantsToAim(bool bNewWants) { bWantsToAim = bNewWants; }
	bool WantsToSprint() const { return bWantsToSprint; }
	bool WantsToAim() const { return bWantsToAim; }

	/** Sprinting right now (wants to, on ground, moving forward, not aiming/crouching). */
	bool IsSprinting() const;
	bool IsSliding() const;

	//~ UCharacterMovementComponent
	virtual float GetMaxSpeed() const override;
	virtual float GetMaxBrakingDeceleration() const override;
	virtual bool IsMovingOnGround() const override;
	virtual bool CanCrouchInCurrentState() const override;
	virtual bool CanAttemptJump() const override;
	virtual bool DoJump(bool bReplayingMoves, float DeltaTime) override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
	virtual void PhysCustom(float DeltaTime, int32 Iterations) override;

protected:
	uint8 bWantsToSprint : 1;
	uint8 bWantsToAim : 1;

	bool IsSprintDirectionValid() const;
	bool CanEnterSlide() const;
	void EnterSlide();
	void ExitSlide();
	void PhysSlide(float DeltaTime, int32 Iterations);
	bool FindSlideSurface(FHitResult& OutHit) const;

	friend class FSavedMove_Shoter;
};
