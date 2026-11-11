#include "UI/ShoterMenuPlayerController.h"

#include "Blueprint/UserWidget.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "Core/ShoterAssets.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Online/ShoterGameInstance.h"
#include "Sound/SoundBase.h"
#include "UI/ShoterMainMenuWidget.h"

AShoterMenuPlayerController::AShoterMenuPlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
	bShowMouseCursor = true;

	// There is no pawn to follow: keep the backdrop camera as the view target.
	bAutoManageActiveCameraTarget = false;
}

void AShoterMenuPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalController())
	{
		return;
	}

	StartBackdropCamera();
	CreateMenuWidget();

	// Music volume follows the options menu.
	UShoterGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance<UShoterGameInstance>() : nullptr;
	if (GameInstance)
	{
		CurrentMusicVolume = FMath::Clamp(GameInstance->GetUserSettings().MusicVolume, 0.f, 1.f);
		BoundGameInstance = GameInstance;
		SettingsChangedHandle = GameInstance->OnSettingsChanged.AddUObject(this, &AShoterMenuPlayerController::HandleSettingsChanged);
	}

	StartMenuMusic();
}

void AShoterMenuPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UShoterGameInstance* GameInstance = BoundGameInstance.Get())
	{
		if (SettingsChangedHandle.IsValid())
		{
			GameInstance->OnSettingsChanged.Remove(SettingsChangedHandle);
		}
	}
	SettingsChangedHandle.Reset();
	BoundGameInstance.Reset();

	if (MusicComponent)
	{
		// Unbind first: stopping the component would otherwise restart it through HandleMusicFinished.
		MusicComponent->OnAudioFinished.RemoveAll(this);
		MusicComponent->Stop();
		MusicComponent = nullptr;
	}

	if (MainMenu)
	{
		MainMenu->RemoveFromParent();
		MainMenu = nullptr;
	}

	// The backdrop camera is a regular world actor and goes away with the level.
	Super::EndPlay(EndPlayReason);
}

void AShoterMenuPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	if (BackdropCamera)
	{
		OrbitAngleDegrees = FMath::Fmod(OrbitAngleDegrees + OrbitDegreesPerSecond * DeltaTime, 360.f);

		const FVector Location = GetOrbitLocation(OrbitAngleDegrees);
		BackdropCamera->SetActorLocationAndRotation(Location, (OrbitLookAt - Location).Rotation());
	}
}

// ---------------------------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------------------------

void AShoterMenuPlayerController::CreateMenuWidget()
{
	MainMenu = CreateWidget<UShoterMainMenuWidget>(this, UShoterMainMenuWidget::StaticClass());
	if (!MainMenu)
	{
		return;
	}

	MainMenu->SetIsFocusable(true);
	MainMenu->AddToViewport(0);

	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(MainMenu->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
	bShowMouseCursor = true;
}

FVector AShoterMenuPlayerController::GetOrbitLocation(float AngleDegrees) const
{
	const float Radians = FMath::DegreesToRadians(AngleDegrees);
	return FVector(FMath::Cos(Radians) * OrbitRadius, FMath::Sin(Radians) * OrbitRadius, OrbitHeight);
}

void AShoterMenuPlayerController::StartBackdropCamera()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FVector StartLocation = GetOrbitLocation(OrbitAngleDegrees);

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.ObjectFlags |= RF_Transient;

	BackdropCamera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), StartLocation, (OrbitLookAt - StartLocation).Rotation(), SpawnParams);
	if (!BackdropCamera)
	{
		return;
	}

	if (UCameraComponent* CameraComponent = BackdropCamera->GetCameraComponent())
	{
		CameraComponent->SetFieldOfView(BackdropFieldOfView);
		CameraComponent->bConstrainAspectRatio = false; // no letterboxing on non-16:9 windows
	}

	SetViewTarget(BackdropCamera);
}

void AShoterMenuPlayerController::StartMenuMusic()
{
	// The asset may not exist yet (content is generated separately): then the menu is just silent.
	UShoterAssets* Assets = UShoterAssets::Get(this);
	USoundBase* Music = Assets ? Assets->GetSound(TEXT("MUS_Menu_Loop")) : nullptr;
	if (!Music)
	{
		return;
	}

	// bAutoDestroy = false so the component survives the end of the sound and can be restarted.
	MusicComponent = UGameplayStatics::SpawnSound2D(this, Music, CurrentMusicVolume, 1.f, 0.f, nullptr, false, false);
	if (MusicComponent)
	{
		MusicComponent->OnAudioFinished.AddDynamic(this, &AShoterMenuPlayerController::HandleMusicFinished);
	}
}

void AShoterMenuPlayerController::ApplyMusicVolume()
{
	if (const UShoterGameInstance* GameInstance = BoundGameInstance.Get())
	{
		CurrentMusicVolume = FMath::Clamp(GameInstance->GetUserSettings().MusicVolume, 0.f, 1.f);
	}

	if (MusicComponent)
	{
		MusicComponent->SetVolumeMultiplier(CurrentMusicVolume);

		// A sound played at zero volume can end up stopped; bring it back once it is audible again.
		if (CurrentMusicVolume > KINDA_SMALL_NUMBER && !MusicComponent->IsPlaying())
		{
			MusicComponent->Play();
		}
	}
}

void AShoterMenuPlayerController::HandleSettingsChanged()
{
	ApplyMusicVolume();
}

void AShoterMenuPlayerController::HandleMusicFinished()
{
	if (MusicComponent && CurrentMusicVolume > KINDA_SMALL_NUMBER)
	{
		MusicComponent->Play();
	}
}
