#include "Player/ShoterCharacter.h"
#include "ShoterAAA.h"
#include "Player/ShoterCharacterMovement.h"
#include "Player/ShoterPlayerController.h"
#include "Player/ShoterPlayerState.h"
#include "Player/ShoterInputConfig.h"
#include "Weapons/ShoterWeapon.h"
#include "Weapons/ShoterProjectile.h"
#include "Game/ShoterGameMode.h"
#include "Core/ShoterAssets.h"
#include "Core/ShoterFX.h"
#include "Core/ShoterInteractable.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "Animation/AnimInstance.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "ShoterCharacter"

namespace
{
	const FName VisorTag(TEXT("Visor"));
}

AShoterCharacter::AShoterCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UShoterCharacterMovement>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;

	UCapsuleComponent* Capsule = GetCapsuleComponent();
	Capsule->InitCapsuleSize(38.f, 92.f);
	Capsule->SetCollisionResponseToChannel(SHOTER_TRACE_WEAPON, ECR_Ignore);

	BaseEyeHeight = CameraBaseHeight;
	CrouchedEyeHeight = 40.f;
	JumpMaxCount = 2;
	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(Capsule);
	FirstPersonCamera->SetRelativeLocation(FVector(0.f, 0.f, CameraBaseHeight));
	FirstPersonCamera->bUsePawnControlRotation = true;
	FirstPersonCamera->SetFieldOfView(100.f);
	// First-person rendering: weapons keep a fixed viewmodel FOV and never clip into walls.
	FirstPersonCamera->bEnableFirstPersonFieldOfView = true;
	FirstPersonCamera->bEnableFirstPersonScale = true;
	FirstPersonCamera->FirstPersonFieldOfView = 85.f;
	FirstPersonCamera->FirstPersonScale = 0.6f;

	WeaponPivot = CreateDefaultSubobject<USceneComponent>(TEXT("WeaponPivot"));
	WeaponPivot->SetupAttachment(FirstPersonCamera);

	USkeletalMeshComponent* Body = GetMesh();
	Body->SetRelativeLocation(FVector(0.f, 0.f, -92.f));
	Body->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
	Body->SetOwnerNoSee(true);
	Body->bCastHiddenShadow = true;
	Body->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::WorldSpaceRepresentation;
	Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Body->SetCollisionObjectType(ECC_Pawn);
	Body->SetCollisionResponseToAllChannels(ECR_Ignore);
	Body->SetCollisionResponseToChannel(SHOTER_TRACE_WEAPON, ECR_Block);
	Body->SetGenerateOverlapEvents(false);
	Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
}

void AShoterCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AShoterCharacter, Health);
	DOREPLIFETIME(AShoterCharacter, Shield);
	DOREPLIFETIME(AShoterCharacter, CurrentWeapon);
	DOREPLIFETIME(AShoterCharacter, Inventory);
	DOREPLIFETIME_CONDITION(AShoterCharacter, Grenades, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AShoterCharacter, bIsAiming, COND_SkipOwner);
	DOREPLIFETIME(AShoterCharacter, bSpawnProtected);
	DOREPLIFETIME(AShoterCharacter, bIsDying);
}

void AShoterCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	if (HasAuthority())
	{
		Health = MaxHealth;
		Shield = MaxShield;
	}
	SetupBodyMesh();
}

void AShoterCharacter::SetupBodyMesh()
{
	USkeletalMeshComponent* Body = GetMesh();
	if (!Body)
	{
		return;
	}
	if (!BodyMeshPath.IsNull())
	{
		if (USkeletalMesh* MeshAsset = Cast<USkeletalMesh>(BodyMeshPath.TryLoad()))
		{
			Body->SetSkeletalMeshAsset(MeshAsset);
		}
	}
	if (Body->GetSkeletalMeshAsset() && !BodyAnimClassPath.IsNull())
	{
		if (UClass* AnimClass = BodyAnimClassPath.TryLoadClass<UAnimInstance>())
		{
			Body->SetAnimInstanceClass(AnimClass);
		}
	}
}

void AShoterCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (!GetMesh()->GetSkeletalMeshAsset())
	{
		BuildPlaceholderBody();
	}
	BindToPlayerState();
	RefreshTeamColor();
	LastControlRotation = GetControlRotation();

	if (HasAuthority())
	{
		bSpawnProtected = true;
		GetWorldTimerManager().SetTimer(TimerHandle_SpawnProtection, this, &AShoterCharacter::ClearSpawnProtection, SpawnProtectionTime, false);
	}

	const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	FShoterFX::SpawnTeleportFlash(GetWorld(), GetActorLocation() - FVector(0.f, 0.f, HalfHeight), ShoterTeam::GetColor(GetTeam()));
}

void AShoterCharacter::Destroyed()
{
	if (AShoterPlayerState* OldState = BoundPlayerState.Get())
	{
		OldState->OnTeamChanged.Remove(TeamChangedHandle);
	}
	if (HasAuthority())
	{
		DestroyInventory();
	}
	Super::Destroyed();
}

void AShoterCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	BindToPlayerState();
	RefreshTeamColor();
}

void AShoterCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	BindToPlayerState();
	RefreshTeamColor();
}

void AShoterCharacter::PawnClientRestart()
{
	Super::PawnClientRestart();
	LastControlRotation = GetControlRotation();
}

void AShoterCharacter::BindToPlayerState()
{
	AShoterPlayerState* NewState = GetShoterPlayerState();
	if (BoundPlayerState.Get() == NewState)
	{
		return;
	}
	if (AShoterPlayerState* OldState = BoundPlayerState.Get())
	{
		OldState->OnTeamChanged.Remove(TeamChangedHandle);
	}
	BoundPlayerState = NewState;
	if (NewState)
	{
		TeamChangedHandle = NewState->OnTeamChanged.AddUObject(this, &AShoterCharacter::RefreshTeamColor);
	}
}

AShoterPlayerState* AShoterCharacter::GetShoterPlayerState() const
{
	return GetPlayerState<AShoterPlayerState>();
}

uint8 AShoterCharacter::GetTeam() const
{
	const AShoterPlayerState* State = GetShoterPlayerState();
	return State ? State->GetTeam() : ShoterTeam::None;
}

FString AShoterCharacter::GetPlayerDisplayName() const
{
	const AShoterPlayerState* State = GetShoterPlayerState();
	return State ? State->GetPlayerName() : FString(TEXT("?"));
}

UShoterCharacterMovement* AShoterCharacter::GetShoterMovement() const
{
	return Cast<UShoterCharacterMovement>(GetCharacterMovement());
}

// ---------------------------------------------------------------------------
// Visuals
// ---------------------------------------------------------------------------

void AShoterCharacter::BuildPlaceholderBody()
{
	UShoterAssets* Assets = UShoterAssets::Get(this);
	if (!Assets || PlaceholderParts.Num() > 0)
	{
		return;
	}

	struct FPiece
	{
		EShoterShape Shape;
		FVector Location;
		FVector Size;
		FRotator Rotation;
		FName Tag;
	};

	// Capsule center is at 0, feet at -92, top of the head at about +85.
	const FPiece Pieces[] =
	{
		{ EShoterShape::Cube,		FVector(0.f, 0.f, 22.f),	FVector(28.f, 40.f, 50.f),	FRotator::ZeroRotator,		NAME_None },	// torso
		{ EShoterShape::Cube,		FVector(0.f, 0.f, -12.f),	FVector(24.f, 34.f, 18.f),	FRotator::ZeroRotator,		NAME_None },	// pelvis
		{ EShoterShape::Cylinder,	FVector(0.f, -10.f, -56.f),	FVector(14.f, 14.f, 72.f),	FRotator::ZeroRotator,		NAME_None },	// left leg
		{ EShoterShape::Cylinder,	FVector(0.f, 10.f, -56.f),	FVector(14.f, 14.f, 72.f),	FRotator::ZeroRotator,		NAME_None },	// right leg
		{ EShoterShape::Cylinder,	FVector(4.f, -27.f, 20.f),	FVector(11.f, 11.f, 50.f),	FRotator(0.f, 0.f, 8.f),	NAME_None },	// left arm
		{ EShoterShape::Cylinder,	FVector(16.f, 24.f, 30.f),	FVector(11.f, 11.f, 46.f),	FRotator(-70.f, 0.f, 0.f),	NAME_None },	// right arm, raised (holds the gun)
		{ EShoterShape::Cube,		FVector(0.f, -25.f, 46.f),	FVector(22.f, 16.f, 10.f),	FRotator(0.f, 0.f, -15.f),	NAME_None },	// shoulder pads
		{ EShoterShape::Cube,		FVector(0.f, 25.f, 46.f),	FVector(22.f, 16.f, 10.f),	FRotator(0.f, 0.f, 15.f),	NAME_None },
		{ EShoterShape::Sphere,		FVector(0.f, 0.f, 66.f),	FVector(26.f, 24.f, 30.f),	FRotator::ZeroRotator,		SHOTER_HEAD_BONE },	// helmet
		{ EShoterShape::Cube,		FVector(11.f, 0.f, 68.f),	FVector(5.f, 18.f, 7.f),	FRotator::ZeroRotator,		VisorTag },	// glowing visor
	};

	for (const FPiece& Piece : Pieces)
	{
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
		Part->SetStaticMesh(Assets->GetShape(Piece.Shape));
		Part->SetupAttachment(GetCapsuleComponent());
		Part->SetRelativeLocation(Piece.Location);
		Part->SetRelativeRotation(Piece.Rotation);
		Part->SetRelativeScale3D(Piece.Size / 100.f);
		Part->SetOwnerNoSee(true);
		Part->bCastHiddenShadow = true;
		Part->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Part->SetCollisionObjectType(ECC_Pawn);
		Part->SetCollisionResponseToAllChannels(ECR_Ignore);
		Part->SetCollisionResponseToChannel(SHOTER_TRACE_WEAPON, ECR_Block);
		Part->SetGenerateOverlapEvents(false);
		if (!Piece.Tag.IsNone())
		{
			Part->ComponentTags.Add(Piece.Tag);
			if (Piece.Tag == VisorTag)
			{
				// The visor counts as head for headshots too.
				Part->ComponentTags.Add(SHOTER_HEAD_BONE);
			}
		}
		Part->RegisterComponent();
		PlaceholderParts.Add(Part);
	}
}

void AShoterCharacter::RefreshTeamColor()
{
	UShoterAssets* Assets = UShoterAssets::Get(this);
	if (!Assets)
	{
		return;
	}
	const FLinearColor TeamColor = ShoterTeam::GetColor(GetTeam());

	if (!BodyMaterial)
	{
		if (UMaterialInterface* CharacterMaterial = Assets->GetMaterial(TEXT("M_Shoter_Character")))
		{
			BodyMaterial = UMaterialInstanceDynamic::Create(CharacterMaterial, this);
		}
		else
		{
			BodyMaterial = Assets->MakeSurfaceMaterial(this, FLinearColor(0.1f, 0.11f, 0.13f), 0.8f, 0.35f, TeamColor, 0.6f);
		}
	}
	if (!BodyMaterial)
	{
		return;
	}

	// Unknown parameters are simply ignored, so this works with the character material and the fallbacks.
	const FLinearColor Armor = FMath::Lerp(FLinearColor(0.07f, 0.08f, 0.09f), TeamColor, 0.22f);
	BodyMaterial->SetVectorParameterValue(TEXT("BaseColor"), Armor);
	BodyMaterial->SetVectorParameterValue(TEXT("TeamColor"), TeamColor);
	BodyMaterial->SetScalarParameterValue(TEXT("RimStrength"), bSpawnProtected ? 14.f : 4.f);
	BodyMaterial->SetVectorParameterValue(TEXT("EmissiveColor"), TeamColor);
	BodyMaterial->SetScalarParameterValue(TEXT("EmissiveStrength"), bSpawnProtected ? 3.f : 0.6f);
	BodyMaterial->SetVectorParameterValue(TEXT("Color"), Armor + TeamColor * 0.3f);

	USkeletalMeshComponent* Body = GetMesh();
	if (Body && Body->GetSkeletalMeshAsset())
	{
		for (int32 Index = 0; Index < Body->GetNumMaterials(); ++Index)
		{
			Body->SetMaterial(Index, BodyMaterial);
		}
	}
	for (UStaticMeshComponent* Part : PlaceholderParts)
	{
		if (!Part)
		{
			continue;
		}
		if (Part->ComponentHasTag(VisorTag))
		{
			Part->SetMaterial(0, Assets->MakeGlowMaterial(Part, TeamColor, 15.f));
		}
		else
		{
			Part->SetMaterial(0, BodyMaterial);
		}
	}
}

// ---------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------

void AShoterCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bIsDying)
	{
		return;
	}

	if (HasAuthority())
	{
		UpdateRegeneration(DeltaSeconds);
	}

	if (IsLocallyControlled())
	{
		const AShoterWeapon* Weapon = CurrentWeapon;
		const bool bCanAim = bIsAiming && Weapon && !Weapon->IsReloading() && !IsSprinting();
		const float AimTime = Weapon ? FMath::Max(Weapon->GetStats().AimTime, 0.05f) : 0.2f;
		AimAlpha = FMath::FInterpConstantTo(AimAlpha, bCanAim ? 1.f : 0.f, DeltaSeconds, 1.f / AimTime);

		if (IsPlayerControlled())
		{
			ApplyPendingRecoil(DeltaSeconds);
			UpdateFirstPersonView(DeltaSeconds);
			UpdateInteractionFocus();
		}
	}

	const bool bSlidingNow = IsSliding();
	if (bSlidingNow && !bWasSliding)
	{
		UShoterAssets::PlaySoundAt(this, TEXT("Slide"), GetActorLocation(), EShoterSoundRange::Short, 0.7f);
	}
	bWasSliding = bSlidingNow;

	UpdateFootsteps(DeltaSeconds);
}

void AShoterCharacter::UpdateRegeneration(float DeltaSeconds)
{
	if (!IsAlive())
	{
		return;
	}

	const float SinceDamage = GetWorld()->GetTimeSeconds() - LastDamageTime;
	if (Shield < MaxShield && SinceDamage >= ShieldRegenDelay)
	{
		const bool bWasEmpty = Shield <= 0.f;
		Shield = FMath::Min(MaxShield, Shield + ShieldRegenRate * DeltaSeconds);
		if (bWasEmpty && IsLocallyControlled() && IsPlayerControlled())
		{
			UShoterAssets::PlaySound2D(this, TEXT("ShieldRecharge"), 0.6f);
		}
	}
	else if (Shield >= MaxShield && Health < MaxHealth && SinceDamage >= HealthRegenDelay)
	{
		Health = FMath::Min(MaxHealth, Health + HealthRegenRate * DeltaSeconds);
	}
}

void AShoterCharacter::UpdateFirstPersonView(float DeltaSeconds)
{
	// Crouching glides the camera down instead of snapping.
	CrouchCameraOffset = FMath::FInterpTo(CrouchCameraOffset, 0.f, DeltaSeconds, 12.f);
	FirstPersonCamera->SetRelativeLocation(FVector(0.f, 0.f, CameraBaseHeight + CrouchCameraOffset));

	AShoterWeapon* Weapon = CurrentWeapon;
	const FShoterWeaponStats* Stats = Weapon ? &Weapon->GetStats() : nullptr;
	const float Aim = FMath::InterpEaseInOut(0.f, 1.f, AimAlpha, 2.f);

	// Sway: the weapon lags behind camera rotation.
	const FRotator ControlRotation = GetControlRotation();
	const FRotator LookDelta = (ControlRotation - LastControlRotation).GetNormalized();
	LastControlRotation = ControlRotation;
	const float SwayScale = FMath::Lerp(1.f, 0.2f, Aim);
	const FRotator TargetSway(
		FMath::Clamp(-LookDelta.Pitch * 0.9f, -5.f, 5.f) * SwayScale,
		FMath::Clamp(-LookDelta.Yaw * 0.9f, -5.f, 5.f) * SwayScale,
		FMath::Clamp(-LookDelta.Yaw * 1.5f, -7.f, 7.f) * SwayScale);
	SwayRotation = FMath::RInterpTo(SwayRotation, TargetSway, DeltaSeconds, 9.f);
	const FVector TargetSwayOffset(0.f, FMath::Clamp(-LookDelta.Yaw * 0.25f, -2.f, 2.f), FMath::Clamp(-LookDelta.Pitch * 0.25f, -2.f, 2.f));
	SwayOffset = FMath::VInterpTo(SwayOffset, TargetSwayOffset * SwayScale, DeltaSeconds, 9.f);

	// Walk bob (figure eight), reduced while aiming.
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	const bool bGrounded = Movement && Movement->IsMovingOnGround() && !IsSliding();
	const float BobIntensity = bGrounded ? FMath::Clamp(GetVelocity().Size2D() / 520.f, 0.f, 1.6f) : 0.f;
	if (BobIntensity > 0.05f)
	{
		BobTime += DeltaSeconds * (IsSprinting() ? 14.f : 10.f);
	}
	const float BobScale = BobIntensity * FMath::Lerp(1.f, 0.12f, Aim);
	const FVector Bob(0.f, FMath::Sin(BobTime) * 0.9f * BobScale, FMath::Sin(BobTime * 2.f) * 0.6f * BobScale);

	SprintBlend = FMath::FInterpTo(SprintBlend, IsSprinting() ? 1.f : 0.f, DeltaSeconds, 9.f);
	LandingDip = FMath::FInterpTo(LandingDip, 0.f, DeltaSeconds, 7.f);

	const float EquipDown = Weapon ? 1.f - FMath::InterpEaseOut(0.f, 1.f, Weapon->GetEquipAlpha(), 2.f) : 0.f;
	const float ReloadAlpha = (Weapon && Weapon->IsReloading()) ? FMath::Sin(Weapon->GetReloadProgress() * PI) : 0.f;

	const FVector Hip = Stats ? Stats->HipOffset : FVector(30.f, 15.f, -16.f);
	const FVector AimPosition = Stats ? Stats->AimOffset : FVector(22.f, 0.f, -9.f);

	// Weapon offsets are tuned for the grip; keep the whole model a bit further from the eye so the receiver
	// never fills the screen while aiming (first-person FOV/scale squash it on screen anyway).
	constexpr float WeaponForwardOffset = 18.f;
	FVector Location = FMath::Lerp(Hip, AimPosition, Aim) + FVector(WeaponForwardOffset, 0.f, 0.f);
	Location += Bob + SwayOffset;
	Location.Z -= LandingDip;
	Location += FVector(-4.f, -3.f, -7.f) * SprintBlend;
	Location += FVector(0.f, 0.f, -28.f) * EquipDown;
	Location += FVector(-2.f, 0.f, -9.f) * ReloadAlpha;

	FRotator Rotation = SwayRotation;
	Rotation += FRotator(-12.f, 28.f, -10.f) * SprintBlend;
	Rotation += FRotator(-25.f, 0.f, 0.f) * EquipDown;
	Rotation += FRotator(-18.f, 12.f, 28.f) * ReloadAlpha;

	if (Weapon)
	{
		Location += Weapon->GetKickOffset();
		Rotation += Weapon->GetKickRotation();
	}

	WeaponPivot->SetRelativeLocationAndRotation(Location, Rotation);

	// Scoped weapons hide their model while looking through the scope (the HUD draws the scope).
	if (Weapon && Stats->bHasScope && Weapon->IsEquipped())
	{
		if (USceneComponent* FirstPersonModel = Weapon->GetFirstPersonRoot())
		{
			FirstPersonModel->SetVisibility(AimAlpha < 0.9f, true);
		}
	}
}

void AShoterCharacter::ApplyPendingRecoil(float DeltaSeconds)
{
	if (!Controller || PendingRecoil.IsNearlyZero(0.001f))
	{
		return;
	}
	const float Alpha = FMath::Clamp(DeltaSeconds * 25.f, 0.f, 1.f);
	const FRotator Step = PendingRecoil * Alpha;
	PendingRecoil -= Step;

	FRotator NewRotation = Controller->GetControlRotation();
	NewRotation.Pitch += Step.Pitch;
	NewRotation.Yaw += Step.Yaw;
	Controller->SetControlRotation(NewRotation);
}

void AShoterCharacter::AddRecoil(float PitchDegrees, float YawDegrees)
{
	PendingRecoil.Pitch += PitchDegrees;
	PendingRecoil.Yaw += YawDegrees;
}

void AShoterCharacter::UpdateInteractionFocus()
{
	FocusedInteractable = nullptr;
	if (!IsAlive() || !FirstPersonCamera)
	{
		return;
	}

	const FVector Start = FirstPersonCamera->GetComponentLocation();
	const FVector End = Start + GetControlRotation().Vector() * 320.f;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ShoterInteract), false, this);
	if (!GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(10.f), Params))
	{
		return;
	}

	// Focus even when it cannot be used right now, so its prompt can explain why (e.g. "Disponible en 12 s").
	// The server checks CanInteract before acting.
	AActor* HitActor = Hit.GetActor();
	IShoterInteractable* Interactable = Cast<IShoterInteractable>(HitActor);
	if (Interactable && Hit.Distance <= Interactable->GetInteractionRange())
	{
		FocusedInteractable = HitActor;
	}
}

FText AShoterCharacter::GetInteractionPrompt() const
{
	if (IShoterInteractable* Interactable = Cast<IShoterInteractable>(FocusedInteractable.Get()))
	{
		return Interactable->GetInteractionPrompt(this);
	}
	return FText::GetEmpty();
}

void AShoterCharacter::UpdateFootsteps(float DeltaSeconds)
{
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!IsAlive() || !Movement || !Movement->IsMovingOnGround() || IsSliding())
	{
		FootstepDistance = 0.f;
		return;
	}

	const float Speed = GetVelocity().Size2D();
	if (Speed < 150.f)
	{
		FootstepDistance = 0.f;
		return;
	}

	FootstepDistance += Speed * DeltaSeconds;
	const float Stride = bIsCrouched ? 140.f : (Speed > 650.f ? 240.f : 195.f);
	if (FootstepDistance < Stride)
	{
		return;
	}
	FootstepDistance = 0.f;

	static const FName Steps[] = { TEXT("Footstep_01"), TEXT("Footstep_02"), TEXT("Footstep_03"), TEXT("Footstep_04") };
	const bool bOwnSteps = IsLocallyControlled() && IsPlayerControlled();
	const float Volume = bIsCrouched ? 0.25f : (bOwnSteps ? 0.4f : 0.9f);
	const FVector FeetLocation = GetActorLocation() - FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	UShoterAssets::PlaySoundAt(this, Steps[FMath::RandRange(0, 3)], FeetLocation, EShoterSoundRange::Short, Volume, FMath::FRandRange(0.92f, 1.08f));
}

// ---------------------------------------------------------------------------
// Movement events
// ---------------------------------------------------------------------------

void AShoterCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);

	const float FallSpeed = FMath::Abs(GetCharacterMovement()->Velocity.Z);
	LandingDip = FMath::Clamp(FallSpeed / 120.f, 0.f, 8.f);
	if (FallSpeed > 450.f)
	{
		const FVector FeetLocation = GetActorLocation() - FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		UShoterAssets::PlaySoundAt(this, TEXT("Land"), FeetLocation, EShoterSoundRange::Short, FMath::Clamp(FallSpeed / 1200.f, 0.4f, 1.f));
	}
}

void AShoterCharacter::OnJumped_Implementation()
{
	Super::OnJumped_Implementation();

	if (JumpCurrentCount > 1)
	{
		UShoterAssets::PlaySoundAt(this, TEXT("DoubleJump"), GetActorLocation(), EShoterSoundRange::Medium, 0.8f);
	}
	else
	{
		UShoterAssets::PlaySoundAt(this, TEXT("Jump"), GetActorLocation(), EShoterSoundRange::Short, 0.5f);
	}
}

bool AShoterCharacter::CanJumpInternal_Implementation() const
{
	// Allows the mid-air double jump and jumping out of a slide.
	return IsAlive() && JumpIsAllowedInternal();
}

void AShoterCharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	CrouchCameraOffset += ScaledHalfHeightAdjust;
}

void AShoterCharacter::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	CrouchCameraOffset -= ScaledHalfHeightAdjust;
}

FVector AShoterCharacter::GetPawnViewLocation() const
{
	return FirstPersonCamera ? FirstPersonCamera->GetComponentLocation() : Super::GetPawnViewLocation();
}

bool AShoterCharacter::IsSprinting() const
{
	const UShoterCharacterMovement* Movement = GetShoterMovement();
	return Movement && Movement->IsSprinting();
}

bool AShoterCharacter::IsSliding() const
{
	const UShoterCharacterMovement* Movement = GetShoterMovement();
	return Movement && Movement->IsSliding();
}

// ---------------------------------------------------------------------------
// Health, damage and death
// ---------------------------------------------------------------------------

void AShoterCharacter::RestoreHealth()
{
	if (HasAuthority())
	{
		Health = MaxHealth;
		Shield = MaxShield;
	}
}

void AShoterCharacter::ClearSpawnProtection()
{
	if (!HasAuthority())
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(TimerHandle_SpawnProtection);
	if (bSpawnProtected)
	{
		bSpawnProtected = false;
		RefreshTeamColor();
	}
}

void AShoterCharacter::OnRep_SpawnProtected()
{
	RefreshTeamColor();
}

void AShoterCharacter::OnRep_Health(float OldHealth)
{
	if (Health < OldHealth)
	{
		LastDamageTime = GetWorld()->GetTimeSeconds();
	}
}

void AShoterCharacter::OnRep_Shield(float OldShield)
{
	if (Shield < OldShield)
	{
		LastDamageTime = GetWorld()->GetTimeSeconds();
	}
	else if (OldShield <= 0.f && Shield > 0.f && IsLocallyControlled())
	{
		UShoterAssets::PlaySound2D(this, TEXT("ShieldRecharge"), 0.6f);
	}
}

float AShoterCharacter::TakeDamage(float Damage, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (!HasAuthority() || !IsAlive() || Damage <= 0.f)
	{
		return 0.f;
	}

	// Super computes radial falloff for explosions and notifies blueprints.
	float FinalDamage = Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);
	if (const AShoterGameMode* GameMode = GetWorld()->GetAuthGameMode<AShoterGameMode>())
	{
		FinalDamage = GameMode->ModifyDamage(FinalDamage, this, EventInstigator, DamageCauser);
	}
	if (FinalDamage <= 0.f)
	{
		return 0.f;
	}

	bool bHeadshot = false;
	FVector HitDirection = FVector::ZeroVector;
	FVector SourceLocation = DamageCauser ? DamageCauser->GetActorLocation() : GetActorLocation();

	if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		const FPointDamageEvent& PointEvent = static_cast<const FPointDamageEvent&>(DamageEvent);
		bHeadshot = PointEvent.HitInfo.BoneName == SHOTER_HEAD_BONE;
		HitDirection = PointEvent.ShotDirection;
		if (EventInstigator && EventInstigator->GetPawn())
		{
			SourceLocation = EventInstigator->GetPawn()->GetActorLocation();
		}
	}
	else if (DamageEvent.IsOfType(FRadialDamageEvent::ClassID))
	{
		const FRadialDamageEvent& RadialEvent = static_cast<const FRadialDamageEvent&>(DamageEvent);
		SourceLocation = RadialEvent.Origin;
		HitDirection = (GetActorLocation() - RadialEvent.Origin).GetSafeNormal();
	}
	else if (DamageCauser)
	{
		HitDirection = (GetActorLocation() - DamageCauser->GetActorLocation()).GetSafeNormal();
	}

	// Shield absorbs damage first.
	bool bShieldBroken = false;
	float Remaining = FinalDamage;
	if (Shield > 0.f)
	{
		const float Absorbed = FMath::Min(Shield, Remaining);
		Shield -= Absorbed;
		Remaining -= Absorbed;
		bShieldBroken = Shield <= 0.f;
	}
	Health = FMath::Max(0.f, Health - Remaining);
	LastDamageTime = GetWorld()->GetTimeSeconds();

	if (EventInstigator && EventInstigator != GetController())
	{
		DamageContributors.FindOrAdd(EventInstigator) += FinalDamage;
	}

	const bool bKilled = Health <= 0.f;
	NotifyDamageFeedback(FinalDamage, EventInstigator, bShieldBroken, bHeadshot, bKilled, SourceLocation);

	if (bKilled)
	{
		Die(EventInstigator, DamageCauser, bHeadshot, HitDirection);
	}
	return FinalDamage;
}

void AShoterCharacter::NotifyDamageFeedback(float Damage, AController* EventInstigator, bool bShieldBroken, bool bHeadshot, bool bKilled, const FVector& SourceLocation)
{
	if (AShoterPlayerController* VictimPC = Cast<AShoterPlayerController>(GetController()))
	{
		VictimPC->ClientReceivedDamage(SourceLocation, Damage, bShieldBroken);
	}
	if (AShoterPlayerController* InstigatorPC = Cast<AShoterPlayerController>(EventInstigator))
	{
		if (InstigatorPC != GetController())
		{
			InstigatorPC->ClientConfirmHit(bHeadshot, bKilled, Damage);
		}
	}
}

void AShoterCharacter::FellOutOfWorld(const UDamageType& DmgType)
{
	if (HasAuthority() && IsAlive())
	{
		Die(nullptr, nullptr, false, FVector::UpVector);
	}
}

void AShoterCharacter::Die(AController* Killer, AActor* DamageCauser, bool bHeadshot, const FVector& HitDirection)
{
	if (!HasAuthority() || bIsDying)
	{
		return;
	}
	bIsDying = true;
	Health = 0.f;
	StopFire();

	TArray<AController*> Assisters;
	for (const TPair<TWeakObjectPtr<AController>, float>& Contribution : DamageContributors)
	{
		AController* Contributor = Contribution.Key.Get();
		if (Contributor && Contributor != Killer && Contributor != GetController() && Contribution.Value >= 25.f)
		{
			Assisters.Add(Contributor);
		}
	}
	DamageContributors.Empty();

	// The game mode needs our controller (victim feedback, respawn), so notify before detaching.
	if (AShoterGameMode* GameMode = GetWorld()->GetAuthGameMode<AShoterGameMode>())
	{
		GameMode->OnCharacterKilled(this, Killer, DamageCauser, bHeadshot, Assisters);
	}

	DestroyInventory();
	MulticastDeath(HitDirection, bHeadshot);
	DetachFromControllerPendingDestroy();
	SetLifeSpan(8.f);
}

void AShoterCharacter::MulticastDeath_Implementation(FVector_NetQuantizeNormal HitDirection, bool bHeadshot)
{
	bIsDying = true;
	bWantsToFire = false;
	FocusedInteractable = nullptr;

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	StartRagdoll(HitDirection, bHeadshot);
	UShoterAssets::PlaySoundAt(this, TEXT("ShieldBreak"), GetActorLocation(), EShoterSoundRange::Medium, 0.8f, 0.6f);
	OnDied.Broadcast(this);
}

void AShoterCharacter::StartRagdoll(const FVector& HitDirection, bool bHeadshot)
{
	const FVector Impulse = (HitDirection.IsNearlyZero() ? -GetActorForwardVector() : HitDirection) * 450.f + FVector(0.f, 0.f, 150.f);

	USkeletalMeshComponent* Body = GetMesh();
	if (Body && Body->GetSkeletalMeshAsset() && Body->GetPhysicsAsset())
	{
		Body->SetOwnerNoSee(false);
		Body->SetCollisionProfileName(TEXT("Ragdoll"));
		Body->SetCollisionResponseToChannel(SHOTER_TRACE_WEAPON, ECR_Ignore);
		Body->SetAllBodiesSimulatePhysics(true);
		Body->SetSimulatePhysics(true);
		Body->WakeAllRigidBodies();
		Body->bBlendPhysics = true;
		Body->AddImpulse(Impulse, bHeadshot ? SHOTER_HEAD_BONE : FName(TEXT("spine_03")), true);
		return;
	}

	for (UStaticMeshComponent* Part : PlaceholderParts)
	{
		if (!Part)
		{
			continue;
		}
		Part->SetOwnerNoSee(false);
		Part->SetCollisionProfileName(TEXT("PhysicsActor"));
		Part->SetCollisionResponseToChannel(SHOTER_TRACE_WEAPON, ECR_Ignore);
		Part->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		Part->SetSimulatePhysics(true);
		Part->AddImpulse(Impulse + FMath::VRand() * 120.f, NAME_None, true);
	}
}

// ---------------------------------------------------------------------------
// Weapons
// ---------------------------------------------------------------------------

AShoterWeapon* AShoterCharacter::FindWeapon(TSubclassOf<AShoterWeapon> WeaponClass) const
{
	for (AShoterWeapon* Weapon : Inventory)
	{
		if (Weapon && Weapon->GetClass() == WeaponClass)
		{
			return Weapon;
		}
	}
	return nullptr;
}

AShoterWeapon* AShoterCharacter::GiveWeapon(TSubclassOf<AShoterWeapon> WeaponClass, bool bEquip)
{
	if (!HasAuthority() || !WeaponClass || !IsAlive())
	{
		return nullptr;
	}

	if (AShoterWeapon* Existing = FindWeapon(WeaponClass))
	{
		Existing->RefillAmmo();
		if (bEquip)
		{
			EquipWeapon(Existing);
		}
		return Existing;
	}

	// Full inventory: the new weapon replaces the one in hand.
	if (Inventory.Num() >= MaxWeapons && CurrentWeapon)
	{
		AShoterWeapon* Replaced = CurrentWeapon;
		Inventory.Remove(Replaced);
		SetCurrentWeapon(nullptr, Replaced);
		Replaced->Destroy();
	}

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.Instigator = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AShoterWeapon* Weapon = GetWorld()->SpawnActor<AShoterWeapon>(WeaponClass, GetActorTransform(), Params);
	if (!Weapon)
	{
		return nullptr;
	}

	Weapon->SetOwningCharacter(this);
	Inventory.Add(Weapon);
	if (bEquip || !CurrentWeapon)
	{
		EquipWeapon(Weapon);
	}
	return Weapon;
}

void AShoterCharacter::DestroyInventory()
{
	if (!HasAuthority())
	{
		return;
	}
	for (AShoterWeapon* Weapon : Inventory)
	{
		if (Weapon)
		{
			Weapon->Destroy();
		}
	}
	Inventory.Empty();
	CurrentWeapon = nullptr;
}

void AShoterCharacter::RefillAllAmmo()
{
	if (!HasAuthority())
	{
		return;
	}
	for (AShoterWeapon* Weapon : Inventory)
	{
		if (Weapon)
		{
			Weapon->RefillAmmo();
		}
	}
	Grenades = MaxGrenades;
}

void AShoterCharacter::AddGrenades(int32 Amount)
{
	if (HasAuthority())
	{
		Grenades = FMath::Clamp(Grenades + Amount, 0, MaxGrenades);
	}
}

void AShoterCharacter::EquipWeapon(AShoterWeapon* Weapon)
{
	if (!Weapon || Weapon == CurrentWeapon || !Inventory.Contains(Weapon) || !IsAlive())
	{
		return;
	}
	SetCurrentWeapon(Weapon, CurrentWeapon);
	if (!HasAuthority())
	{
		ServerEquipWeapon(Weapon);
	}
}

void AShoterCharacter::ServerEquipWeapon_Implementation(AShoterWeapon* Weapon)
{
	EquipWeapon(Weapon);
}

void AShoterCharacter::OnRep_CurrentWeapon(AShoterWeapon* LastWeapon)
{
	SetCurrentWeapon(CurrentWeapon, LastWeapon);
}

void AShoterCharacter::SetCurrentWeapon(AShoterWeapon* NewWeapon, AShoterWeapon* LastWeapon)
{
	if (LastWeapon && LastWeapon != NewWeapon)
	{
		LastWeapon->OnUnequip();
	}

	CurrentWeapon = NewWeapon;

	if (NewWeapon)
	{
		NewWeapon->SetOwningCharacter(this);
		if (!NewWeapon->IsEquipped())
		{
			NewWeapon->OnEquip(LastWeapon);
		}
	}

	if (UShoterCharacterMovement* Movement = GetShoterMovement())
	{
		Movement->WeaponSpeedMultiplier = NewWeapon ? NewWeapon->GetStats().MoveSpeedScale : 1.f;
	}
}

void AShoterCharacter::EquipWeaponSlot(int32 SlotIndex)
{
	if (Inventory.IsValidIndex(SlotIndex))
	{
		EquipWeapon(Inventory[SlotIndex]);
	}
}

void AShoterCharacter::CycleWeapon(int32 Direction)
{
	if (Inventory.Num() < 2)
	{
		return;
	}
	const int32 CurrentIndex = FMath::Max(0, Inventory.IndexOfByKey(CurrentWeapon));
	const int32 NextIndex = (CurrentIndex + Direction + Inventory.Num()) % Inventory.Num();
	EquipWeapon(Inventory[NextIndex]);
}

void AShoterCharacter::StartFire()
{
	if (!IsAlive())
	{
		return;
	}
	bWantsToFire = true;
	if (IsSprinting())
	{
		SetSprinting(false);
	}
	if (CurrentWeapon)
	{
		CurrentWeapon->StartFire();
	}
}

void AShoterCharacter::StopFire()
{
	bWantsToFire = false;
	if (CurrentWeapon)
	{
		CurrentWeapon->StopFire();
	}
}

void AShoterCharacter::Reload()
{
	if (IsAlive() && CurrentWeapon)
	{
		CurrentWeapon->StartReload();
	}
}

void AShoterCharacter::SetAiming(bool bNewAiming)
{
	if (bNewAiming && !IsAlive())
	{
		return;
	}
	bIsAiming = bNewAiming;
	if (UShoterCharacterMovement* Movement = GetShoterMovement())
	{
		Movement->SetWantsToAim(bNewAiming);
	}
	if (!HasAuthority())
	{
		ServerSetAiming(bNewAiming);
	}
}

void AShoterCharacter::ServerSetAiming_Implementation(bool bNewAiming)
{
	bIsAiming = bNewAiming;
}

void AShoterCharacter::SetSprinting(bool bNewSprinting)
{
	bSprintHeld = bNewSprinting;
	if (UShoterCharacterMovement* Movement = GetShoterMovement())
	{
		Movement->SetWantsToSprint(bNewSprinting);
	}
}

void AShoterCharacter::ThrowGrenade()
{
	if (!IsAlive() || Grenades <= 0)
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - LastGrenadeTime < 0.8f)
	{
		return;
	}
	LastGrenadeTime = Now;

	const FRotator AimRotation = GetControlRotation();
	const FVector Direction = (AimRotation + FRotator(7.f, 0.f, 0.f)).Vector();
	const FVector Origin = GetPawnViewLocation() + AimRotation.Vector() * 45.f + FVector(0.f, 0.f, -10.f);

	UShoterAssets::PlaySoundAt(this, TEXT("Grenade_Throw"), Origin, EShoterSoundRange::Short, 0.8f);
	if (HasAuthority())
	{
		ServerThrowGrenade_Implementation(Origin, Direction);
	}
	else
	{
		ServerThrowGrenade(Origin, Direction);
	}
}

void AShoterCharacter::ServerThrowGrenade_Implementation(FVector_NetQuantize Origin, FVector_NetQuantizeNormal Direction)
{
	if (!IsAlive() || Grenades <= 0)
	{
		return;
	}
	--Grenades;

	FVector SpawnLocation = Origin;
	const FVector Eye = GetPawnViewLocation();
	if (FVector::DistSquared(SpawnLocation, Eye) > FMath::Square(200.f))
	{
		SpawnLocation = Eye;
	}

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.Instigator = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AShoterGrenade* Grenade = GetWorld()->SpawnActor<AShoterGrenade>(AShoterGrenade::StaticClass(), SpawnLocation, Direction.Rotation(), Params))
	{
		Grenade->InitProjectile(Direction, 1500.f, GetController(), LOCTEXT("GrenadeName", "GRANADA").ToString());
	}
	ClearSpawnProtection();
}

void AShoterCharacter::Interact()
{
	if (AActor* Target = FocusedInteractable.Get())
	{
		ServerInteract(Target);
	}
}

void AShoterCharacter::ServerInteract_Implementation(AActor* Target)
{
	if (!Target || !IsAlive())
	{
		return;
	}
	IShoterInteractable* Interactable = Cast<IShoterInteractable>(Target);
	if (!Interactable)
	{
		return;
	}
	// Generous: the client measured to the hit point, we measure to the actor origin.
	if (FVector::Dist(GetPawnViewLocation(), Target->GetActorLocation()) > Interactable->GetInteractionRange() + 250.f)
	{
		return;
	}
	if (Interactable->CanInteract(this))
	{
		Interactable->Interact(this);
	}
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void AShoterCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	AShoterPlayerController* PC = Cast<AShoterPlayerController>(GetController());
	UShoterInputConfig* Config = PC ? PC->GetInputConfig() : nullptr;
	if (!Input || !Config)
	{
		UE_LOG(LogShoter, Warning, TEXT("SetupPlayerInputComponent: missing Enhanced Input component or input config."));
		return;
	}

	Input->BindAction(Config->Move, ETriggerEvent::Triggered, this, &AShoterCharacter::Input_Move);
	Input->BindAction(Config->Look, ETriggerEvent::Triggered, this, &AShoterCharacter::Input_Look);
	Input->BindAction(Config->LookGamepad, ETriggerEvent::Triggered, this, &AShoterCharacter::Input_LookGamepad);
	Input->BindAction(Config->Jump, ETriggerEvent::Started, this, &AShoterCharacter::Input_JumpPressed);
	Input->BindAction(Config->Jump, ETriggerEvent::Completed, this, &AShoterCharacter::Input_JumpReleased);
	Input->BindAction(Config->Fire, ETriggerEvent::Started, this, &AShoterCharacter::Input_FirePressed);
	Input->BindAction(Config->Fire, ETriggerEvent::Completed, this, &AShoterCharacter::Input_FireReleased);
	Input->BindAction(Config->Aim, ETriggerEvent::Started, this, &AShoterCharacter::Input_AimPressed);
	Input->BindAction(Config->Aim, ETriggerEvent::Completed, this, &AShoterCharacter::Input_AimReleased);
	Input->BindAction(Config->Sprint, ETriggerEvent::Started, this, &AShoterCharacter::Input_SprintPressed);
	Input->BindAction(Config->Sprint, ETriggerEvent::Completed, this, &AShoterCharacter::Input_SprintReleased);
	Input->BindAction(Config->Crouch, ETriggerEvent::Started, this, &AShoterCharacter::Input_CrouchPressed);
	Input->BindAction(Config->Crouch, ETriggerEvent::Completed, this, &AShoterCharacter::Input_CrouchReleased);
	Input->BindAction(Config->Reload, ETriggerEvent::Started, this, &AShoterCharacter::Input_Reload);
	Input->BindAction(Config->Interact, ETriggerEvent::Started, this, &AShoterCharacter::Input_Interact);
	Input->BindAction(Config->Grenade, ETriggerEvent::Started, this, &AShoterCharacter::Input_Grenade);
	Input->BindAction(Config->NextWeapon, ETriggerEvent::Started, this, &AShoterCharacter::Input_NextWeapon);
	Input->BindAction(Config->PrevWeapon, ETriggerEvent::Started, this, &AShoterCharacter::Input_PrevWeapon);
	Input->BindAction(Config->Weapon1, ETriggerEvent::Started, this, &AShoterCharacter::Input_Weapon1);
	Input->BindAction(Config->Weapon2, ETriggerEvent::Started, this, &AShoterCharacter::Input_Weapon2);
	Input->BindAction(Config->Weapon3, ETriggerEvent::Started, this, &AShoterCharacter::Input_Weapon3);
}

namespace
{
	/** Gameplay input is ignored while the pause menu is open or after the match ended. */
	bool IsGameplayInputBlocked(const AController* Controller)
	{
		const AShoterPlayerController* PC = Cast<AShoterPlayerController>(Controller);
		return PC && (PC->IsPauseMenuOpen() || PC->HasMatchEnded());
	}
}

void AShoterCharacter::Input_Move(const FInputActionValue& Value)
{
	if (!Controller || !IsAlive())
	{
		return;
	}
	const FVector2D Axis = Value.Get<FVector2D>();
	const FRotator YawRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);
	AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X), Axis.Y);
	AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y), Axis.X);
}

void AShoterCharacter::Input_Look(const FInputActionValue& Value)
{
	const AShoterPlayerController* PC = Cast<AShoterPlayerController>(Controller);
	if (!PC)
	{
		return;
	}
	const FVector2D Axis = Value.Get<FVector2D>();
	const float Scale = PC->GetMouseLookScale(AimAlpha > 0.5f);
	AddControllerYawInput(Axis.X * Scale);
	AddControllerPitchInput(Axis.Y * Scale * (PC->IsLookInverted() ? -1.f : 1.f));
}

void AShoterCharacter::Input_LookGamepad(const FInputActionValue& Value)
{
	const AShoterPlayerController* PC = Cast<AShoterPlayerController>(Controller);
	if (!PC)
	{
		return;
	}
	const FVector2D Axis = Value.Get<FVector2D>();
	const float DeltaSeconds = GetWorld()->GetDeltaSeconds();
	// Rate based: degrees per second at full deflection, scaled by the same sensitivity setting.
	const float Sensitivity = PC->GetMouseLookScale(AimAlpha > 0.5f) / 0.07f;
	AddControllerYawInput(Axis.X * 220.f * Sensitivity * DeltaSeconds);
	AddControllerPitchInput(Axis.Y * 150.f * Sensitivity * DeltaSeconds * (PC->IsLookInverted() ? -1.f : 1.f));
}

void AShoterCharacter::Input_JumpPressed()
{
	if (IsGameplayInputBlocked(Controller))
	{
		return;
	}
	if (bIsCrouched)
	{
		UnCrouch();
	}
	Jump();
}

void AShoterCharacter::Input_JumpReleased()
{
	StopJumping();
}

void AShoterCharacter::Input_FirePressed()
{
	if (!IsGameplayInputBlocked(Controller))
	{
		StartFire();
	}
}

void AShoterCharacter::Input_FireReleased()
{
	StopFire();
}

void AShoterCharacter::Input_AimPressed()
{
	if (!IsGameplayInputBlocked(Controller))
	{
		SetAiming(true);
	}
}

void AShoterCharacter::Input_AimReleased()
{
	SetAiming(false);
}

void AShoterCharacter::Input_SprintPressed()
{
	SetSprinting(true);
}

void AShoterCharacter::Input_SprintReleased()
{
	SetSprinting(false);
}

void AShoterCharacter::Input_CrouchPressed()
{
	Crouch();
}

void AShoterCharacter::Input_CrouchReleased()
{
	UnCrouch();
}

void AShoterCharacter::Input_Reload()
{
	if (!IsGameplayInputBlocked(Controller))
	{
		Reload();
	}
}

void AShoterCharacter::Input_Interact()
{
	if (!IsGameplayInputBlocked(Controller))
	{
		Interact();
	}
}

void AShoterCharacter::Input_Grenade()
{
	if (!IsGameplayInputBlocked(Controller))
	{
		ThrowGrenade();
	}
}

void AShoterCharacter::Input_NextWeapon()
{
	CycleWeapon(1);
}

void AShoterCharacter::Input_PrevWeapon()
{
	CycleWeapon(-1);
}

void AShoterCharacter::Input_Weapon1()
{
	EquipWeaponSlot(0);
}

void AShoterCharacter::Input_Weapon2()
{
	EquipWeaponSlot(1);
}

void AShoterCharacter::Input_Weapon3()
{
	EquipWeaponSlot(2);
}

#undef LOCTEXT_NAMESPACE
