#include "Player/ShoterPlayerController.h"
#include "ShoterAAA.h"
#include "Player/ShoterCharacter.h"
#include "Player/ShoterInputConfig.h"
#include "Player/ShoterPlayerCameraManager.h"
#include "Online/ShoterGameInstance.h"
#include "UI/ShoterPauseMenuWidget.h"
#include "Core/ShoterAssets.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

#define LOCTEXT_NAMESPACE "ShoterPlayerController"

AShoterPlayerController::AShoterPlayerController()
{
	PlayerCameraManagerClass = AShoterPlayerCameraManager::StaticClass();
	bShowMouseCursor = false;
}

void AShoterPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalController())
	{
		return;
	}

	AddGameplayMappings();
	SetInputMode(FInputModeGameOnly());
	bShowMouseCursor = false;

	if (UShoterGameInstance* GameInstance = GetGameInstance<UShoterGameInstance>())
	{
		SettingsChangedHandle = GameInstance->OnSettingsChanged.AddUObject(this, &AShoterPlayerController::ApplyUserSettings);
	}
	ApplyUserSettings();
	StartAmbience();
}

void AShoterPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UShoterGameInstance* GameInstance = GetGameInstance<UShoterGameInstance>())
	{
		GameInstance->OnSettingsChanged.Remove(SettingsChangedHandle);
	}
	if (AmbienceComponent)
	{
		AmbienceComponent->Stop();
	}
	Super::EndPlay(EndPlayReason);
}

UShoterInputConfig* AShoterPlayerController::GetInputConfig()
{
	if (!InputConfig)
	{
		InputConfig = NewObject<UShoterInputConfig>(this, TEXT("ShoterInputConfig"));
		InputConfig->Initialize();
	}
	return InputConfig;
}

void AShoterPlayerController::AddGameplayMappings()
{
	UShoterInputConfig* Config = GetInputConfig();
	if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			Subsystem->ClearAllMappings();
			Subsystem->AddMappingContext(Config->GameplayMappings, 0);
			Subsystem->AddMappingContext(Config->GlobalMappings, 1);
		}
	}
}

void AShoterPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(InputComponent))
	{
		UShoterInputConfig* Config = GetInputConfig();
		Input->BindAction(Config->Scoreboard, ETriggerEvent::Started, this, &AShoterPlayerController::Input_ScoreboardPressed);
		Input->BindAction(Config->Scoreboard, ETriggerEvent::Completed, this, &AShoterPlayerController::Input_ScoreboardReleased);
		Input->BindAction(Config->Pause, ETriggerEvent::Started, this, &AShoterPlayerController::Input_Pause);
	}
}

void AShoterPlayerController::Input_ScoreboardPressed()
{
	bScoreboardVisible = true;
}

void AShoterPlayerController::Input_ScoreboardReleased()
{
	bScoreboardVisible = false;
}

void AShoterPlayerController::Input_Pause()
{
	TogglePauseMenu();
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

void AShoterPlayerController::ApplyUserSettings()
{
	if (!IsLocalController())
	{
		return;
	}
	if (UShoterGameInstance* GameInstance = GetGameInstance<UShoterGameInstance>())
	{
		GameInstance->ApplySettings();
		if (AmbienceComponent)
		{
			AmbienceComponent->SetVolumeMultiplier(0.35f * GameInstance->GetUserSettings().MusicVolume);
		}
	}
}

float AShoterPlayerController::GetMouseLookScale(bool bAiming) const
{
	// Degrees per mouse count at sensitivity 1 (similar to popular competitive shooters).
	constexpr float BaseDegreesPerCount = 0.07f;
	const UShoterGameInstance* GameInstance = GetGameInstance<UShoterGameInstance>();
	if (!GameInstance)
	{
		return BaseDegreesPerCount;
	}
	const FShoterUserSettings& Settings = GameInstance->GetUserSettings();
	const float Sensitivity = FMath::Clamp(Settings.MouseSensitivity, 0.05f, 10.f);
	const float AimScale = bAiming ? FMath::Clamp(Settings.AimSensitivityScale, 0.1f, 2.f) : 1.f;
	return BaseDegreesPerCount * Sensitivity * AimScale;
}

bool AShoterPlayerController::IsLookInverted() const
{
	const UShoterGameInstance* GameInstance = GetGameInstance<UShoterGameInstance>();
	return GameInstance && GameInstance->GetUserSettings().bInvertY;
}

float AShoterPlayerController::GetUserFieldOfView() const
{
	const UShoterGameInstance* GameInstance = GetGameInstance<UShoterGameInstance>();
	return GameInstance ? FMath::Clamp(GameInstance->GetUserSettings().FieldOfView, 70.f, 120.f) : 100.f;
}

void AShoterPlayerController::StartAmbience()
{
	if (AmbienceComponent)
	{
		return;
	}
	UShoterAssets* Assets = UShoterAssets::Get(this);
	USoundBase* Ambience = Assets ? Assets->GetSound(TEXT("AMB_Arena_Loop")) : nullptr;
	if (Ambience)
	{
		const UShoterGameInstance* GameInstance = GetGameInstance<UShoterGameInstance>();
		const float Volume = 0.35f * (GameInstance ? GameInstance->GetUserSettings().MusicVolume : 0.5f);
		AmbienceComponent = UGameplayStatics::SpawnSound2D(this, Ambience, Volume, 1.f, 0.f, nullptr, false, false);
	}
}

// ---------------------------------------------------------------------------
// Menus
// ---------------------------------------------------------------------------

void AShoterPlayerController::TogglePauseMenu()
{
	if (!IsLocalController())
	{
		return;
	}

	if (IsPauseMenuOpen())
	{
		PauseMenu->RemoveFromParent();
		PauseMenu = nullptr;
		SetInputMode(FInputModeGameOnly());
		bShowMouseCursor = false;
		SetIgnoreLookInput(false);
		UShoterAssets::PlaySound2D(this, TEXT("UI_Click"));
		return;
	}

	PauseMenu = CreateWidget<UShoterPauseMenuWidget>(this, UShoterPauseMenuWidget::StaticClass());
	if (!PauseMenu)
	{
		return;
	}
	PauseMenu->SetIsFocusable(true);
	PauseMenu->AddToViewport(50);

	FInputModeGameAndUI InputMode;
	InputMode.SetWidgetToFocus(PauseMenu->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	SetInputMode(InputMode);
	bShowMouseCursor = true;
	SetIgnoreLookInput(true);

	if (AShoterCharacter* ShoterCharacter = GetPawn<AShoterCharacter>())
	{
		ShoterCharacter->StopFire();
		ShoterCharacter->SetAiming(false);
	}
	UShoterAssets::PlaySound2D(this, TEXT("UI_Click"));
}

bool AShoterPlayerController::IsPauseMenuOpen() const
{
	return PauseMenu && PauseMenu->IsInViewport();
}

// ---------------------------------------------------------------------------
// Local feedback
// ---------------------------------------------------------------------------

void AShoterPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	const float Now = GetWorld()->GetTimeSeconds();
	DamageIndicators.RemoveAll([Now](const FShoterDamageIndicator& Indicator) { return Now - Indicator.Time > 1.5f; });
	Notifications.RemoveAll([Now](const FShoterNotification& Notification) { return Now - Notification.Time > 3.f; });
}

void AShoterPlayerController::NotifyLocalHit(bool bHeadshot)
{
	LastHitMarkerTime = GetWorld()->GetTimeSeconds();
	bLastHitHeadshot = bHeadshot;
	UShoterAssets::PlaySound2D(this, bHeadshot ? FName(TEXT("Headshot")) : FName(TEXT("HitMarker")), bHeadshot ? 0.8f : 0.6f);
}

void AShoterPlayerController::ClientConfirmHit_Implementation(bool bHeadshot, bool bKilled, float DamageDealt)
{
	const float Now = GetWorld()->GetTimeSeconds();
	// Hitscan hits were already predicted locally; projectiles and explosions were not.
	if (Now - LastHitMarkerTime > 0.15f)
	{
		NotifyLocalHit(bHeadshot);
	}
	if (bKilled)
	{
		LastKillMarkerTime = Now;
	}
}

void AShoterPlayerController::ClientReceivedDamage_Implementation(FVector_NetQuantize SourceLocation, float Damage, bool bShieldBroken)
{
	FShoterDamageIndicator& Indicator = DamageIndicators.AddDefaulted_GetRef();
	Indicator.SourceLocation = SourceLocation;
	Indicator.Damage = Damage;
	Indicator.Time = GetWorld()->GetTimeSeconds();

	if (bShieldBroken)
	{
		UShoterAssets::PlaySound2D(this, TEXT("ShieldBreak"), 0.9f);
	}
	if (AShoterPlayerCameraManager* CameraManager = Cast<AShoterPlayerCameraManager>(PlayerCameraManager))
	{
		CameraManager->AddTrauma(FMath::Clamp(Damage / 120.f, 0.08f, 0.45f));
	}
}

void AShoterPlayerController::ClientOnDeath_Implementation(const FString& KillerName, const FString& WeaponName, float RespawnDelay,
	FVector_NetQuantize DeathLocation, AActor* KillerActor)
{
	bDeadAndWaiting = true;
	LastKillerName = KillerName;
	LastKillerWeapon = WeaponName;
	bRespawnNextWave = RespawnDelay < 0.f;
	RespawnAtTime = GetWorld()->GetTimeSeconds() + FMath::Max(0.f, RespawnDelay);

	if (AShoterPlayerCameraManager* CameraManager = Cast<AShoterPlayerCameraManager>(PlayerCameraManager))
	{
		CameraManager->StartDeathCam(DeathLocation, KillerActor);
	}
}

void AShoterPlayerController::ResetDeathState()
{
	bDeadAndWaiting = false;
	bRespawnNextWave = false;
	if (AShoterPlayerCameraManager* CameraManager = Cast<AShoterPlayerCameraManager>(PlayerCameraManager))
	{
		CameraManager->StopDeathCam();
	}
}

void AShoterPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	if (IsLocalController())
	{
		ResetDeathState();
	}
}

void AShoterPlayerController::AcknowledgePossession(APawn* P)
{
	Super::AcknowledgePossession(P);
	ResetDeathState();
}

float AShoterPlayerController::GetRespawnTimeRemaining() const
{
	if (!bDeadAndWaiting)
	{
		return 0.f;
	}
	if (bRespawnNextWave)
	{
		return -1.f;
	}
	return FMath::Max(0.f, RespawnAtTime - GetWorld()->GetTimeSeconds());
}

void AShoterPlayerController::ClientOnEliminatedEnemy_Implementation(const FString& VictimName, bool bHeadshot, int32 Points)
{
	LastKillMarkerTime = GetWorld()->GetTimeSeconds();
	UShoterAssets::PlaySound2D(this, TEXT("KillConfirm"), 0.8f);

	const FText Text = bHeadshot
		? FText::Format(LOCTEXT("KillHeadshot", "+{0}  ELIMINASTE A {1}  (TIRO A LA CABEZA)"), Points, FText::FromString(VictimName.ToUpper()))
		: FText::Format(LOCTEXT("Kill", "+{0}  ELIMINASTE A {1}"), Points, FText::FromString(VictimName.ToUpper()));
	AddNotification(Text, bHeadshot ? FLinearColor(1.f, 0.55f, 0.1f) : FLinearColor(0.2f, 0.9f, 1.f));
}

void AShoterPlayerController::ClientAnnounce_Implementation(const FText& Title, const FText& Subtitle, FLinearColor Color, float Duration, FName SoundName)
{
	ShowAnnouncement(Title, Subtitle, Color, Duration, SoundName);
}

void AShoterPlayerController::ClientNotify_Implementation(const FText& Text, FLinearColor Color, FName SoundName)
{
	AddNotification(Text, Color);
	if (!SoundName.IsNone())
	{
		UShoterAssets::PlaySound2D(this, SoundName, 0.8f);
	}
}

void AShoterPlayerController::ShowAnnouncement(const FText& Title, const FText& Subtitle, const FLinearColor& Color, float Duration, FName SoundName)
{
	Announcement.Title = Title;
	Announcement.Subtitle = Subtitle;
	Announcement.Color = Color;
	Announcement.Duration = Duration;
	Announcement.StartTime = GetWorld()->GetTimeSeconds();
	if (!SoundName.IsNone())
	{
		UShoterAssets::PlaySound2D(this, SoundName, 0.9f);
	}
}

void AShoterPlayerController::AddNotification(const FText& Text, const FLinearColor& Color)
{
	FShoterNotification& Notification = Notifications.AddDefaulted_GetRef();
	Notification.Text = Text;
	Notification.Color = Color;
	Notification.Time = GetWorld()->GetTimeSeconds();
	if (Notifications.Num() > 5)
	{
		Notifications.RemoveAt(0);
	}
}

void AShoterPlayerController::ClientGameEnded_Implementation(AActor* EndGameFocus, bool bIsWinner)
{
	Super::ClientGameEnded_Implementation(EndGameFocus, bIsWinner);

	bMatchEnded = true;
	bWonMatch = bIsWinner;
	SetIgnoreMoveInput(true);

	if (IsPauseMenuOpen())
	{
		TogglePauseMenu();
	}
	if (AShoterCharacter* ShoterCharacter = GetPawn<AShoterCharacter>())
	{
		ShoterCharacter->StopFire();
	}
	UShoterAssets::PlaySound2D(this, bIsWinner ? FName(TEXT("Match_Victory")) : FName(TEXT("Match_Defeat")));
}

// ---------------------------------------------------------------------------
// Testing helpers
// ---------------------------------------------------------------------------

void AShoterPlayerController::ShoterSuicide()
{
	ServerSuicide();
}

void AShoterPlayerController::DebugAutoShotStep(int32 Step)
{
	AShoterCharacter* ShoterCharacter = GetPawn<AShoterCharacter>();
	FRotator View = GetControlRotation();
	switch (Step)
	{
	case 1:		// look somewhere else
		View.Yaw += 140.f;
		SetControlRotation(View);
		break;
	case 2:		// aim down sights
		if (ShoterCharacter) { ShoterCharacter->SetAiming(true); }
		break;
	case 3:		// shoot (muzzle flash, tracers, impacts)
		if (ShoterCharacter) { ShoterCharacter->SetAiming(false); ShoterCharacter->StartFire(); }
		break;
	case 4:		// scoreboard
		if (ShoterCharacter) { ShoterCharacter->StopFire(); }
		bScoreboardVisible = true;
		break;
	case 5:		// wide look over the arena
		bScoreboardVisible = false;
		View.Yaw += 90.f;
		View.Pitch = -12.f;
		SetControlRotation(View);
		break;
	default:
		break;
	}
}

void AShoterPlayerController::ServerSuicide_Implementation()
{
	if (AShoterCharacter* ShoterCharacter = GetPawn<AShoterCharacter>())
	{
		if (ShoterCharacter->IsAlive())
		{
			ShoterCharacter->Die(this, nullptr, false, FVector::UpVector);
		}
	}
}

#undef LOCTEXT_NAMESPACE
