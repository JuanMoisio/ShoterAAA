#include "Player/ShoterInputConfig.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputCoreTypes.h"

UInputAction* UShoterInputConfig::MakeAction(FName Name, bool bAxis2D)
{
	UInputAction* Action = NewObject<UInputAction>(this, Name);
	Action->ValueType = bAxis2D ? EInputActionValueType::Axis2D : EInputActionValueType::Boolean;
	return Action;
}

void UShoterInputConfig::Initialize()
{
	Move = MakeAction(TEXT("IA_Move"), true);
	Look = MakeAction(TEXT("IA_Look"), true);
	LookGamepad = MakeAction(TEXT("IA_LookGamepad"), true);
	Jump = MakeAction(TEXT("IA_Jump"), false);
	Fire = MakeAction(TEXT("IA_Fire"), false);
	Aim = MakeAction(TEXT("IA_Aim"), false);
	Sprint = MakeAction(TEXT("IA_Sprint"), false);
	Crouch = MakeAction(TEXT("IA_Crouch"), false);
	Reload = MakeAction(TEXT("IA_Reload"), false);
	Interact = MakeAction(TEXT("IA_Interact"), false);
	Grenade = MakeAction(TEXT("IA_Grenade"), false);
	NextWeapon = MakeAction(TEXT("IA_NextWeapon"), false);
	PrevWeapon = MakeAction(TEXT("IA_PrevWeapon"), false);
	Weapon1 = MakeAction(TEXT("IA_Weapon1"), false);
	Weapon2 = MakeAction(TEXT("IA_Weapon2"), false);
	Weapon3 = MakeAction(TEXT("IA_Weapon3"), false);
	Scoreboard = MakeAction(TEXT("IA_Scoreboard"), false);
	Pause = MakeAction(TEXT("IA_Pause"), false);

	GameplayMappings = NewObject<UInputMappingContext>(this, TEXT("IMC_Gameplay"));
	GlobalMappings = NewObject<UInputMappingContext>(this, TEXT("IMC_Global"));

	// Digital keys feed (1,0,0); swizzle turns it into (0,1,0) for the forward axis, negate flips it.
	auto AddSwizzle = [this](FEnhancedActionKeyMapping& Mapping)
	{
		UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>(this);
		Swizzle->Order = EInputAxisSwizzle::YXZ;
		Mapping.Modifiers.Add(Swizzle);
	};
	auto AddNegate = [this](FEnhancedActionKeyMapping& Mapping)
	{
		Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(this));
	};
	auto AddDeadZone = [this](FEnhancedActionKeyMapping& Mapping)
	{
		UInputModifierDeadZone* DeadZone = NewObject<UInputModifierDeadZone>(this);
		DeadZone->LowerThreshold = 0.18f;
		Mapping.Modifiers.Add(DeadZone);
	};

	UInputMappingContext* G = GameplayMappings;

	// Movement: X = right, Y = forward
	{
		FEnhancedActionKeyMapping& Forward = G->MapKey(Move, EKeys::W);
		AddSwizzle(Forward);
	}
	{
		FEnhancedActionKeyMapping& Back = G->MapKey(Move, EKeys::S);
		AddSwizzle(Back);
		AddNegate(Back);
	}
	G->MapKey(Move, EKeys::D);
	{
		FEnhancedActionKeyMapping& Left = G->MapKey(Move, EKeys::A);
		AddNegate(Left);
	}
	{
		FEnhancedActionKeyMapping& Stick = G->MapKey(Move, EKeys::Gamepad_Left2D);
		AddDeadZone(Stick);
	}

	// Looking
	G->MapKey(Look, EKeys::Mouse2D);
	{
		FEnhancedActionKeyMapping& Stick = G->MapKey(LookGamepad, EKeys::Gamepad_Right2D);
		AddDeadZone(Stick);
	}

	// Actions
	G->MapKey(Jump, EKeys::SpaceBar);
	G->MapKey(Jump, EKeys::Gamepad_FaceButton_Bottom);
	G->MapKey(Fire, EKeys::LeftMouseButton);
	G->MapKey(Fire, EKeys::Gamepad_RightTrigger);
	G->MapKey(Aim, EKeys::RightMouseButton);
	G->MapKey(Aim, EKeys::Gamepad_LeftTrigger);
	G->MapKey(Sprint, EKeys::LeftShift);
	G->MapKey(Sprint, EKeys::Gamepad_LeftThumbstick);
	G->MapKey(Crouch, EKeys::LeftControl);
	G->MapKey(Crouch, EKeys::C);
	G->MapKey(Crouch, EKeys::Gamepad_FaceButton_Right);
	G->MapKey(Reload, EKeys::R);
	G->MapKey(Reload, EKeys::Gamepad_FaceButton_Left);
	G->MapKey(Interact, EKeys::E);
	G->MapKey(Interact, EKeys::F);
	G->MapKey(Interact, EKeys::Gamepad_DPad_Up);
	G->MapKey(Grenade, EKeys::G);
	G->MapKey(Grenade, EKeys::Gamepad_RightShoulder);
	G->MapKey(NextWeapon, EKeys::MouseScrollDown);
	G->MapKey(NextWeapon, EKeys::Gamepad_FaceButton_Top);
	G->MapKey(PrevWeapon, EKeys::MouseScrollUp);
	G->MapKey(Weapon1, EKeys::One);
	G->MapKey(Weapon2, EKeys::Two);
	G->MapKey(Weapon3, EKeys::Three);

	// Always available
	GlobalMappings->MapKey(Scoreboard, EKeys::Tab);
	GlobalMappings->MapKey(Scoreboard, EKeys::Gamepad_Special_Left);
	GlobalMappings->MapKey(Pause, EKeys::Escape);
	GlobalMappings->MapKey(Pause, EKeys::P);
	GlobalMappings->MapKey(Pause, EKeys::Gamepad_Special_Right);
}
