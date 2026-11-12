#include "Player/ShoterPlayerController.h"
#include "ShoterAAA.h"
#include "Player/ShoterCharacter.h"
#include "Player/ShoterInputConfig.h"
#include "Player/ShoterPlayerCameraManager.h"
#include "Weapons/ShoterWeapon.h"
#include "UObject/UObjectIterator.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
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
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "GameFramework/HUD.h"
#include "AI/ShoterZombie.h"
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

float AShoterPlayerController::GetMouseLookScale(float EasedAimAlpha, float WeaponAimFOV) const
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
	// Aim sensitivity is relative to the zoom: focal-length ratio of the (16:9 reference) FOVs keeps the angular
	// speed per screen distance constant, AimSensitivityScale then tunes it. Blended continuously with the aim alpha.
	const float HipFOV = GetUserFieldOfView();
	const float AimFOV = FMath::Clamp(WeaponAimFOV, 5.f, HipFOV);
	const float ZoomRatio = FMath::Tan(FMath::DegreesToRadians(AimFOV * 0.5f)) / FMath::Tan(FMath::DegreesToRadians(HipFOV * 0.5f));
	const float AimTarget = FMath::Clamp(Settings.AimSensitivityScale, 0.1f, 2.f) * ZoomRatio;
	const float AimScale = FMath::Lerp(1.f, AimTarget, FMath::Clamp(EasedAimAlpha, 0.f, 1.f));
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

namespace
{
#if !UE_BUILD_SHIPPING
	/** Dev only (-ShoterShotMirror): a camera glued to Target, Distance cm away at Azimuth degrees (0 = in front, 180 = behind). */
	void FrameTarget(AShoterPlayerController* PC, AActor* Target, float Azimuth, float Distance, float Height = 40.f)
	{
		static TWeakObjectPtr<ACameraActor> ShotCamera;
		if (!PC || !Target)
		{
			return;
		}
		UWorld* World = PC->GetWorld();
		if (!ShotCamera.IsValid())
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ShotCamera = World->SpawnActor<ACameraActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
		}
		if (ACameraActor* Camera = ShotCamera.Get())
		{
			Camera->GetCameraComponent()->SetFieldOfView(60.f);
			Camera->GetCameraComponent()->bConstrainAspectRatio = false;
			Camera->AttachToActor(Target, FAttachmentTransformRules::KeepRelativeTransform);
			const float Radians = FMath::DegreesToRadians(Azimuth);
			const FVector Local(FMath::Cos(Radians) * Distance, FMath::Sin(Radians) * Distance, Height);
			Camera->SetActorRelativeLocation(Local);
			Camera->SetActorRelativeRotation((FVector(0.f, 0.f, 20.f) - Local).Rotation());
			PC->SetViewTarget(Camera);
			if (PC->PlayerCameraManager)
			{
				PC->PlayerCameraManager->SetFOV(60.f);
			}
		}
	}

	/** Dev only (-ShoterShotOverview): a free camera at a fixed world position looking at a world point. */
	void PlaceFreeCamera(AShoterPlayerController* PC, const FVector& Location, const FVector& LookAt, float Fov)
	{
		static TWeakObjectPtr<ACameraActor> FreeCamera;
		if (!PC)
		{
			return;
		}
		if (!FreeCamera.IsValid())
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			FreeCamera = PC->GetWorld()->SpawnActor<ACameraActor>(Location, FRotator::ZeroRotator, Params);
		}
		if (ACameraActor* Camera = FreeCamera.Get())
		{
			Camera->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			Camera->SetActorLocationAndRotation(Location, (LookAt - Location).Rotation());
			Camera->GetCameraComponent()->bConstrainAspectRatio = false;
			Camera->GetCameraComponent()->SetFieldOfView(Fov);
			PC->SetViewTarget(Camera);
			if (PC->PlayerCameraManager)
			{
				PC->PlayerCameraManager->SetFOV(Fov);
			}
		}
	}

	APawn* FindNearestOtherPawn(AShoterPlayerController* PC)
	{
		APawn* Self = PC->GetPawn();
		APawn* Best = nullptr;
		float BestDistance = TNumericLimits<float>::Max();
		for (TActorIterator<APawn> It(PC->GetWorld()); It; ++It)
		{
			APawn* Other = *It;
			if (Other == Self || !Self)
			{
				continue;
			}
			if (const AShoterCharacter* OtherChar = Cast<AShoterCharacter>(Other))
			{
				if (!OtherChar->IsAlive())
				{
					continue;
				}
			}
			else if (const AShoterZombie* Zombie = Cast<AShoterZombie>(Other))
			{
				if (!Zombie->IsAlive())
				{
					continue;
				}
			}
			else
			{
				continue;
			}
			const float Distance = FVector::Dist(Other->GetActorLocation(), Self->GetActorLocation());
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Best = Other;
			}
		}
		return Best;
	}
#endif
}

void AShoterPlayerController::DebugAutoShotStep(int32 Step)
{
#if !UE_BUILD_SHIPPING
	if (Step >= 200)
	{
		// -ShoterShotBurst: 200 sets the scene up (first person: optional -ShoterShotYaw=, -ShoterShotPitch=, -ShoterShotFireAds, then fire;
		// -ShoterShotMirror: a camera on the nearest bot, which shoots by itself), 201 runs every frame of the burst (re-pulls the trigger of
		// semi-automatic weapons).
		AShoterCharacter* Self = GetPawn<AShoterCharacter>();
		const bool bMirror = FParse::Param(FCommandLine::Get(), TEXT("ShoterShotMirror"));
		// -ShoterShotReload: fire one round, then reload (the local pawn, or with -ShoterShotMirror the nearest bot; -ShoterShotMirrorSelf
		// looks at the local pawn instead) so the burst frames cover the whole reload. -ShoterShotNoFire: never pull the trigger (hip views).
		const bool bReload = FParse::Param(FCommandLine::Get(), TEXT("ShoterShotReload"));
		const bool bNoFire = FParse::Param(FCommandLine::Get(), TEXT("ShoterShotNoFire")) || bReload;
		const bool bMirrorSelf = FParse::Param(FCommandLine::Get(), TEXT("ShoterShotMirrorSelf"));
		AShoterCharacter* Subject = (bMirror && !bMirrorSelf) ? Cast<AShoterCharacter>(FindNearestOtherPawn(this)) : Self;
		static bool bReloadStarted = false;
		if (Step == 200)
		{
			bReloadStarted = false;
			if (AHUD* Hud = GetHUD())
			{
				Hud->bShowHUD = FParse::Param(FCommandLine::Get(), TEXT("ShoterShotHud"));
			}
			float Yaw = 0.f;
			float Pitch = 0.f;
			FParse::Value(FCommandLine::Get(), TEXT("ShoterShotYaw="), Yaw);
			FParse::Value(FCommandLine::Get(), TEXT("ShoterShotPitch="), Pitch);
			if (bMirror)
			{
				float Azimuth = 15.f;
				float Distance = 350.f;
				FParse::Value(FCommandLine::Get(), TEXT("ShoterShotAzimuth="), Azimuth);
				FParse::Value(FCommandLine::Get(), TEXT("ShoterShotDistance="), Distance);
				FrameTarget(this, bMirrorSelf ? static_cast<AActor*>(Self) : FindNearestOtherPawn(this), Azimuth, Distance);
			}
			if (bReload && Subject)
			{
				// One round out of the magazine so there is something to reload.
				Subject->StartFire();
				Subject->StopFire();
			}
			if (!bMirror && Self)
			{
				if (FParse::Param(FCommandLine::Get(), TEXT("ShoterShotLane")))
				{
					// Start of the long north lane (see ShoterShotOverview step 8): 58 m of clear sight line along +X.
					Self->TeleportTo(FVector(-2700.f, 1600.f, 110.f), FRotator(0.f, Yaw, 0.f));
				}
				SetControlRotation(FRotator(Pitch, Yaw, 0.f));
				Self->SetAiming(FParse::Param(FCommandLine::Get(), TEXT("ShoterShotFireAds")));
				if (!bNoFire)
				{
					Self->StartFire();
				}
			}
		}
		else
		{
			if (bReload && Subject && !bReloadStarted)
			{
				bReloadStarted = true;
				Subject->Reload();
			}
			if (Self && !bMirror && !bNoFire)
			{
				Self->StopFire();
				Self->StartFire();
			}
		}
		return;
	}
#endif
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("ShoterShotOverview")) && Step > 0)
	{
		// Map review: 1/2 opposite high corners, 3 north catwalk deck, 4 centre platform deck, 5/6 inside the blue bunker (window, doorway),
		// 7 blue base mouth looking at the centre, 8 down the north lane. Stand-up eye height is 170.
		if (AHUD* Hud = GetHUD())
		{
			Hud->bShowHUD = false;
		}
		switch (Step)
		{
		case 1:	PlaceFreeCamera(this, FVector(-2850.f, -1900.f, 1500.f), FVector(1200.f, 600.f, 0.f), 80.f); break;
		case 2:	PlaceFreeCamera(this, FVector(2850.f, 1900.f, 1500.f), FVector(-1200.f, -600.f, 0.f), 80.f); break;
		case 3:	PlaceFreeCamera(this, FVector(-650.f, 1900.f, 500.f), FVector(500.f, 400.f, 250.f), 90.f); break;
		case 4:	PlaceFreeCamera(this, FVector(-350.f, -250.f, 370.f), FVector(-3000.f, 0.f, 200.f), 90.f); break;
		case 5:	PlaceFreeCamera(this, FVector(-1700.f, 500.f, 170.f), FVector(0.f, 500.f, 150.f), 90.f); break;
		case 6:	PlaceFreeCamera(this, FVector(-1400.f, 500.f, 170.f), FVector(-3000.f, 520.f, 150.f), 90.f); break;
		case 7:	PlaceFreeCamera(this, FVector(-2250.f, 350.f, 170.f), FVector(0.f, 0.f, 170.f), 90.f); break;
		case 8:	PlaceFreeCamera(this, FVector(-2900.f, 1600.f, 170.f), FVector(2900.f, 1600.f, 170.f), 90.f); break;
		default: break;
		}
		return;
	}
#endif
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("ShoterShotTeams")) && Step > 0)
	{
		// Team readability: a blue and a red dummy side by side in the north lane (1, 26 m) and the south lane (2, 26 m), then 10 m (3).
		if (AHUD* Hud = GetHUD())
		{
			Hud->bShowHUD = false;
		}
		static TWeakObjectPtr<AShoterCharacter> Dummies[2];
		UWorld* World = GetWorld();
		const bool bNorth = Step != 2;
		const FVector Base = bNorth ? FVector(-1250.f, 1300.f, 100.f) : FVector(1250.f, -1300.f, 100.f);
		const float Yaw = bNorth ? 0.f : 180.f;
		const FVector Side = FVector(0.f, bNorth ? 70.f : -70.f, 0.f);
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const FVector Location = Base + (Index == 0 ? -Side : Side);
			if (!Dummies[Index].IsValid())
			{
				const FTransform Transform(FRotator(0.f, Yaw, 0.f), Location);
				AShoterCharacter* Dummy = World->SpawnActorDeferred<AShoterCharacter>(AShoterCharacter::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				if (Dummy)
				{
					Dummy->DebugAccentOverride = Index == 0 ? ShoterTeam::GetColor(ShoterTeam::Blue) : ShoterTeam::GetColor(ShoterTeam::Red);
					Dummy->FinishSpawning(Transform);
					Dummies[Index] = Dummy;
				}
			}
			else
			{
				Dummies[Index]->TeleportTo(Location, FRotator(0.f, Yaw, 0.f));
			}
		}
		const float Dir = bNorth ? 1.f : -1.f;
		const float Distance = Step == 3 ? 1000.f : 2600.f;
		PlaceFreeCamera(this, Base + FVector(Dir * Distance, 0.f, 70.f), Base + FVector(0.f, 0.f, 20.f), 60.f);
		return;
	}
#endif
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("ShoterShotMirror")) && Step > 0)
	{
		// Third-person close-ups: 1 self front, 2 self back, 3/4 nearest other pawn (bot or zombie), 5 far (25 m) on the nearest other pawn, 6 profile, 7 ragdoll, 8 far (32 m).
		AShoterCharacter* Self = GetPawn<AShoterCharacter>();
		if (Self && Self->IsAlive() && Step >= 5)
		{
			Self->StopFire();
		}
		FRotator View = GetControlRotation();
		switch (Step)
		{
		case 1:	FrameTarget(this, Self, 0.f, 330.f); break;
		case 2:	FrameTarget(this, Self, 160.f, 330.f, 60.f); break;
		case 3:	FrameTarget(this, FindNearestOtherPawn(this), 15.f, 350.f); break;
		case 4:	FrameTarget(this, FindNearestOtherPawn(this), 150.f, 400.f, 70.f); break;
		case 5:	FrameTarget(this, FindNearestOtherPawn(this), 20.f, 1600.f, 250.f); break;
		case 6:	FrameTarget(this, Self, 90.f, 350.f); break;
		case 7:
			FrameTarget(this, Self, 30.f, 380.f, 90.f);
			ServerSuicide();
			break;
		case 8:	FrameTarget(this, FindNearestOtherPawn(this), 200.f, 2300.f, 450.f); break;
		default: break;
		}
		return;
	}
#endif
	AShoterCharacter* ShoterCharacter = GetPawn<AShoterCharacter>();
	FRotator View = GetControlRotation();
	switch (Step)
	{
	case 0:		// optional: -ShoterShotWeapon=Rail equips AShoterWeapon_Rail (class name without the prefix) for the shots
	{
		FString WeaponName;
		if (ShoterCharacter && FParse::Value(FCommandLine::Get(), TEXT("ShoterShotWeapon="), WeaponName))
		{
			const FString ClassName = TEXT("ShoterWeapon_") + WeaponName;
			for (TObjectIterator<UClass> It; It; ++It)
			{
				if (It->IsChildOf(AShoterWeapon::StaticClass()) && !It->HasAnyClassFlags(CLASS_Abstract) && It->GetName() == ClassName)
				{
					ShoterCharacter->GiveWeapon(*It, true);
					break;
				}
			}
		}
		break;
	}
	case 1:		// look somewhere else
		View.Yaw += 140.f;
		SetControlRotation(View);
		break;
	case 2:		// aim down sights
		if (ShoterCharacter) { ShoterCharacter->SetAiming(true); }
		break;
	case 3:		// shoot (muzzle flash, tracers, impacts)
		if (ShoterCharacter)
		{
			// -ShoterShotFireAds keeps the sights up while firing (to check the sight stays on the centre).
			ShoterCharacter->SetAiming(FParse::Param(FCommandLine::Get(), TEXT("ShoterShotFireAds")));
			ShoterCharacter->StartFire();
		}
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
