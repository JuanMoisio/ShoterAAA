// Co-op wave survival against zombies, with points to buy weapons and ammo.
#pragma once

#include "CoreMinimal.h"
#include "Game/ShoterGameMode.h"
#include "ShoterGameMode_Zombies.generated.h"

class AShoterZombie;
class AShoterZombieSpawnPoint;

UCLASS(Config = Game)
class SHOTERAAA_API AShoterGameMode_Zombies : public AShoterGameMode
{
	GENERATED_BODY()

public:
	AShoterGameMode_Zombies();

	//~ AShoterGameMode
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void GenericPlayerInitialization(AController* C) override;
	virtual void HandleMatchHasStarted() override;
	virtual void OnCharacterKilled(AShoterCharacter* Victim, AController* Killer, AActor* DamageCauser, bool bHeadshot, const TArray<AController*>& Assisters) override;
	virtual bool CanDamage(AController* InstigatorController, AActor* Victim) const override;
	virtual void Logout(AController* Exiting) override;

	// --- Called by zombies (server) ---
	void OnZombieDamaged(AShoterZombie* Zombie, AController* InstigatorController, float Damage, bool bHeadshot);
	void OnZombieKilled(AShoterZombie* Zombie, AController* Killer, bool bHeadshot);

	int32 GetCurrentWave() const { return CurrentWave; }

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Zombies")
	TSubclassOf<AShoterZombie> ZombieClass;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Zombies")
	int32 WaveBreakSeconds = 12;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Zombies")
	int32 FirstWaveDelaySeconds = 8;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Zombies")
	int32 MaxAliveZombies = 24;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Zombies")
	float SpawnInterval = 1.0f;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Zombies")
	int32 PointsPerHit = 10;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Zombies")
	int32 PointsPerKill = 60;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Zombies")
	int32 PointsHeadshotBonus = 40;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Zombies")
	int32 PointsWaveBonus = 150;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Zombies")
	int32 StartingPoints = 500;

	int32 CurrentWave = 0;
	int32 ZombiesLeftToSpawn = 0;
	int32 BreakRemaining = 0;
	bool bInBreak = true;
	bool bGameOver = false;
	TArray<TWeakObjectPtr<AShoterZombie>> AliveZombies;
	FTimerHandle TimerHandle_Spawn;

	virtual void GameTimer() override;
	virtual uint8 ChooseTeam(AController* Controller) const override;
	virtual void ScheduleRespawn(AController* Controller, float Delay) override;

	void StartWave(int32 WaveNumber);
	void EndWave();
	void SpawnTick();
	bool SpawnZombie();
	void RespawnDeadPlayers();
	void CheckGameOver();
	void UpdateGameStateWave();
	int32 CountAlivePlayers() const;
	int32 ComputeWaveSize(int32 Wave) const;
	EShoterZombieType PickZombieType(int32 Wave) const;
	AShoterZombieSpawnPoint* PickSpawnPoint() const;
};
