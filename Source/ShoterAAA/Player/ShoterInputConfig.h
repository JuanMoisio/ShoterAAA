// Enhanced Input actions and key mappings created at runtime, so no input assets are needed.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ShoterInputConfig.generated.h"

class UInputAction;
class UInputMappingContext;

UCLASS()
class SHOTERAAA_API UShoterInputConfig : public UObject
{
	GENERATED_BODY()

public:
	/** Creates every action plus keyboard/mouse and gamepad mappings. */
	void Initialize();

	/** Gameplay controls (movement, weapons). Priority 0. */
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> GameplayMappings;

	/** Always-on controls (scoreboard, pause). Priority 1. */
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> GlobalMappings;

	UPROPERTY(Transient) TObjectPtr<UInputAction> Move;			// Axis2D: X = right, Y = forward
	UPROPERTY(Transient) TObjectPtr<UInputAction> Look;			// Axis2D: mouse delta (counts)
	UPROPERTY(Transient) TObjectPtr<UInputAction> LookGamepad;	// Axis2D: stick (-1..1), rate based
	UPROPERTY(Transient) TObjectPtr<UInputAction> Jump;
	UPROPERTY(Transient) TObjectPtr<UInputAction> Fire;
	UPROPERTY(Transient) TObjectPtr<UInputAction> Aim;
	UPROPERTY(Transient) TObjectPtr<UInputAction> Sprint;
	UPROPERTY(Transient) TObjectPtr<UInputAction> Crouch;
	UPROPERTY(Transient) TObjectPtr<UInputAction> Reload;
	UPROPERTY(Transient) TObjectPtr<UInputAction> Interact;
	UPROPERTY(Transient) TObjectPtr<UInputAction> Grenade;
	UPROPERTY(Transient) TObjectPtr<UInputAction> NextWeapon;
	UPROPERTY(Transient) TObjectPtr<UInputAction> PrevWeapon;
	UPROPERTY(Transient) TObjectPtr<UInputAction> Weapon1;
	UPROPERTY(Transient) TObjectPtr<UInputAction> Weapon2;
	UPROPERTY(Transient) TObjectPtr<UInputAction> Weapon3;
	UPROPERTY(Transient) TObjectPtr<UInputAction> Scoreboard;
	UPROPERTY(Transient) TObjectPtr<UInputAction> Pause;

private:
	UInputAction* MakeAction(FName Name, bool bAxis2D);
};
