// Base rules shared by every mode: teams, spawning, loadouts, damage rules, kills, bots, match end.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameMode.h"
#include "Core/ShoterTypes.h"
#include "ShoterGameMode.generated.h"

class AShoterCharacter;
class AShoterWeapon;
class AShoterPlayerState;
class AShoterGameState;
class AShoterBotController;
class APlayerStart;

UCLASS(Abstract, Config = Game)
class SHOTERAAA_API AShoterGameMode : public AGameMode
{
	GENERATED_BODY()

public:
	AShoterGameMode();

	//~ AGameModeBase / AGameMode
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void InitGameState() override;
	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal) override;
	virtual void GenericPlayerInitialization(AController* C) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	virtual bool ShouldSpawnAtStartSpot(AController* Player) override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual void SetPlayerDefaults(APawn* PlayerPawn) override;
	virtual bool ReadyToStartMatch_Implementation() override;
	virtual bool ReadyToEndMatch_Implementation() override;
	virtual void HandleMatchHasStarted() override;
	virtual void HandleMatchHasEnded() override;

	// --- Rules (server) ---
	EShoterMatchType GetMatchType() const { return MatchType; }

	/** Called by AShoterCharacter::Die. Assisters = other controllers that damaged the victim recently. */
	virtual void OnCharacterKilled(AShoterCharacter* Victim, AController* Killer, AActor* DamageCauser, bool bHeadshot, const TArray<AController*>& Assisters);

	/** Final damage after friendly-fire, self-damage and spawn-protection rules (0 = no damage). */
	virtual float ModifyDamage(float Damage, AActor* Victim, AController* InstigatorController, AActor* DamageCauser) const;

	/** Whether InstigatorController may hurt Victim at all. */
	virtual bool CanDamage(AController* InstigatorController, AActor* Victim) const;

	/** True if both controllers are on opposing sides (always true in FFA, false for the same controller). */
	bool AreEnemies(const AController* A, const AController* B) const;

	/** Kill feed name for a damage causer (weapon, projectile, zombie...). */
	static FString GetDamageCauserName(const AActor* DamageCauser);

	/** Reloads the same map and mode, keeping everyone connected. */
	void RestartMatchTravel();

protected:
	/** Weapons given on every spawn, first one equipped. */
	UPROPERTY(EditDefaultsOnly, Category = "Shoter")
	TArray<TSubclassOf<AShoterWeapon>> DefaultLoadout;

	UPROPERTY(EditDefaultsOnly, Category = "Shoter")
	int32 DefaultGrenades = 2;

	UPROPERTY(EditDefaultsOnly, Category = "Shoter")
	TSubclassOf<AShoterBotController> BotControllerClass;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Shoter")
	int32 ScoreLimit = 50;

	/** Seconds, 0 = no time limit. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Shoter")
	int32 TimeLimitSeconds = 600;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Shoter")
	float RespawnDelay = 3.f;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Shoter")
	int32 PostMatchSeconds = 12;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Shoter")
	int32 KillScore = 100;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Shoter")
	int32 HeadshotBonusScore = 25;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Shoter")
	int32 AssistScore = 50;

	EShoterMatchType MatchType = EShoterMatchType::TeamDeathmatch;
	int32 DesiredBots = 0;
	int32 TimeRemaining = 0;
	int32 PostMatchRemaining = 0;
	FString CurrentMapName;

	FTimerHandle TimerHandle_GameTimer;
	TArray<TWeakObjectPtr<AShoterBotController>> Bots;
	TMap<TWeakObjectPtr<AController>, FTimerHandle> RespawnTimers;

	/** Runs every second while the match exists. */
	virtual void GameTimer();
	virtual void CheckScoreLimit() {}
	virtual void OnTimeLimitReached() {}
	virtual uint8 ChooseTeam(AController* Controller) const;
	virtual bool IsSpawnAllowedFor(const APlayerStart* Start, AController* Player) const;
	virtual void ScheduleRespawn(AController* Controller, float Delay);

	/** Ends the match with a team (TDM) or a player (FFA) as winner. */
	virtual void FinishMatch(uint8 WinningTeam, APlayerState* WinningPlayer);

	void RespawnController(TWeakObjectPtr<AController> Controller);
	void UpdateBotCount();
	AShoterBotController* AddBot();
	void RemoveBot();
	int32 GetNumHumanPlayers() const;
	AShoterGameState* GetShoterGameState() const;
	FString BuildTravelURL() const;
	float RatePlayerStart(const APlayerStart* Start, AController* Player) const;
};
