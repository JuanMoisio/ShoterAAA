// Player controller: input setup, menus, and local feedback state consumed by the HUD.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Core/ShoterTypes.h"
#include "ShoterPlayerController.generated.h"

class UShoterInputConfig;
class UShoterPauseMenuWidget;
class UAudioComponent;
struct FInputActionValue;

UCLASS()
class SHOTERAAA_API AShoterPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AShoterPlayerController();

	//~ APlayerController
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupInputComponent() override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void AcknowledgePossession(APawn* P) override;
	virtual void PlayerTick(float DeltaTime) override;
	virtual void ClientGameEnded_Implementation(AActor* EndGameFocus, bool bIsWinner) override;

	/** Runtime-created Enhanced Input actions shared with the possessed pawn (local controllers only). */
	UShoterInputConfig* GetInputConfig();

	/** Re-applies the saved user settings (FOV, volume, graphics). Called on BeginPlay and from the options menu. */
	void ApplyUserSettings();

	/** Degrees of rotation per mouse count, including the user sensitivity. */
	float GetMouseLookScale(bool bAiming) const;
	bool IsLookInverted() const;
	float GetUserFieldOfView() const;

	// --- Menus ---
	void TogglePauseMenu();
	bool IsPauseMenuOpen() const;
	void SetScoreboardVisible(bool bVisible) { bScoreboardVisible = bVisible; }
	bool IsScoreboardVisible() const { return bScoreboardVisible; }
	bool HasMatchEnded() const { return bMatchEnded; }
	bool DidWinMatch() const { return bWonMatch; }

	// --- Local feedback for the HUD ---
	/** Predicted hit marker from the local weapon trace. */
	void NotifyLocalHit(bool bHeadshot);
	float GetLastHitMarkerTime() const { return LastHitMarkerTime; }
	bool WasLastHitHeadshot() const { return bLastHitHeadshot; }
	float GetLastKillMarkerTime() const { return LastKillMarkerTime; }

	const TArray<FShoterDamageIndicator>& GetDamageIndicators() const { return DamageIndicators; }
	const TArray<FShoterNotification>& GetNotifications() const { return Notifications; }
	const FShoterAnnouncement& GetAnnouncement() const { return Announcement; }

	void ShowAnnouncement(const FText& Title, const FText& Subtitle, const FLinearColor& Color, float Duration, FName SoundName);
	void AddNotification(const FText& Text, const FLinearColor& Color);

	/** Seconds until respawn (<= 0 if not waiting). Zombies mode uses -1: "respawn next wave". */
	float GetRespawnTimeRemaining() const;
	bool IsDeadAndWaiting() const { return bDeadAndWaiting; }
	const FString& GetLastKillerName() const { return LastKillerName; }
	const FString& GetLastKillerWeapon() const { return LastKillerWeapon; }

	// --- Server -> owning client ---
	UFUNCTION(Client, Unreliable)
	void ClientConfirmHit(bool bHeadshot, bool bKilled, float DamageDealt);

	UFUNCTION(Client, Unreliable)
	void ClientReceivedDamage(FVector_NetQuantize SourceLocation, float Damage, bool bShieldBroken);

	/** RespawnDelay < 0 means "respawn at the next wave" (zombies). */
	UFUNCTION(Client, Reliable)
	void ClientOnDeath(const FString& KillerName, const FString& WeaponName, float RespawnDelay, FVector_NetQuantize DeathLocation, AActor* KillerActor);

	UFUNCTION(Client, Reliable)
	void ClientOnEliminatedEnemy(const FString& VictimName, bool bHeadshot, int32 Points);

	UFUNCTION(Client, Reliable)
	void ClientAnnounce(const FText& Title, const FText& Subtitle, FLinearColor Color, float Duration, FName SoundName);

	/** Small center-screen notification for this player, with an optional 2D sound (e.g. "Denied", "Purchase"). */
	UFUNCTION(Client, Reliable)
	void ClientNotify(const FText& Text, FLinearColor Color, FName SoundName);

	// --- Testing helpers (console) ---
	UFUNCTION(Exec)
	void ShoterSuicide();

	/** Development: action performed before each automatic screenshot (see UShoterGameInstance::TickAutoShots). */
	void DebugAutoShotStep(int32 Step);

	UFUNCTION(Server, Reliable)
	void ServerSuicide();

protected:
	UPROPERTY(Transient)
	TObjectPtr<UShoterInputConfig> InputConfig;

	UPROPERTY(Transient)
	TObjectPtr<UShoterPauseMenuWidget> PauseMenu;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> AmbienceComponent;

	bool bScoreboardVisible = false;
	bool bMatchEnded = false;
	bool bWonMatch = false;

	float LastHitMarkerTime = -100.f;
	bool bLastHitHeadshot = false;
	float LastKillMarkerTime = -100.f;

	TArray<FShoterDamageIndicator> DamageIndicators;
	TArray<FShoterNotification> Notifications;
	FShoterAnnouncement Announcement;

	bool bDeadAndWaiting = false;
	float RespawnAtTime = 0.f;
	bool bRespawnNextWave = false;
	FString LastKillerName;
	FString LastKillerWeapon;

	void Input_ScoreboardPressed();
	void Input_ScoreboardReleased();
	void Input_Pause();

	void AddGameplayMappings();
	void StartAmbience();
	void ResetDeathState();

	FDelegateHandle SettingsChangedHandle;
};
