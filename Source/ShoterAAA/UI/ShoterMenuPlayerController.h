#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ShoterMenuPlayerController.generated.h"

class ACameraActor;
class UAudioComponent;
class UShoterGameInstance;
class UShoterMainMenuWidget;

/** Shows the main menu, mouse cursor and plays the menu music. */
UCLASS()
class SHOTERAAA_API AShoterMenuPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AShoterMenuPlayerController();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PlayerTick(float DeltaTime) override;

protected:
	UPROPERTY(Transient)
	TObjectPtr<UShoterMainMenuWidget> MainMenu;

	/** Cinematic camera orbiting the arena behind the menu. */
	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> BackdropCamera;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> MusicComponent;

	// --- Backdrop camera (centimeters / degrees) ---
	UPROPERTY(EditDefaultsOnly, Category = "Menu|Camera")
	float OrbitRadius = 2200.f;

	UPROPERTY(EditDefaultsOnly, Category = "Menu|Camera")
	float OrbitHeight = 650.f;

	UPROPERTY(EditDefaultsOnly, Category = "Menu|Camera")
	float OrbitDegreesPerSecond = 4.f;

	UPROPERTY(EditDefaultsOnly, Category = "Menu|Camera")
	FVector OrbitLookAt = FVector(0.f, 0.f, 150.f);

	UPROPERTY(EditDefaultsOnly, Category = "Menu|Camera")
	float BackdropFieldOfView = 70.f;

	void CreateMenuWidget();
	void StartBackdropCamera();
	void StartMenuMusic();
	void ApplyMusicVolume();
	FVector GetOrbitLocation(float AngleDegrees) const;

	/** Raised by the game instance after the user applies new settings. */
	void HandleSettingsChanged();

	/** Restarts the music if the sound asset does not loop by itself. */
	UFUNCTION()
	void HandleMusicFinished();

	float OrbitAngleDegrees = 0.f;
	float CurrentMusicVolume = 0.5f;

	TWeakObjectPtr<UShoterGameInstance> BoundGameInstance;
	FDelegateHandle SettingsChangedHandle;
};
