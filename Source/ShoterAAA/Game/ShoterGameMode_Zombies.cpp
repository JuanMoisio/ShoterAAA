#include "Game/ShoterGameMode_Zombies.h"
#include "ShoterAAA.h"
#include "Game/ShoterGameState.h"
#include "Game/ShoterZombieSpawnPoint.h"
#include "Player/ShoterCharacter.h"
#include "Player/ShoterPlayerController.h"
#include "Player/ShoterPlayerState.h"
#include "Weapons/ShoterWeapons.h"
#include "AI/ShoterZombie.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "ShoterZombiesMode"

AShoterGameMode_Zombies::AShoterGameMode_Zombies()
{
	MatchType = EShoterMatchType::Zombies;
	ZombieClass = AShoterZombie::StaticClass();
	ScoreLimit = 0;
	TimeLimitSeconds = 0;
	RespawnDelay = -1.f;		// dead players come back at the next wave
	DefaultGrenades = 2;

	DefaultLoadout.Reset();
	DefaultLoadout.Add(AShoterWeapon_PulsePistol::StaticClass());
}

void AShoterGameMode_Zombies::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	DesiredBots = 0;	// bots only fight other characters
}

uint8 AShoterGameMode_Zombies::ChooseTeam(AController* Controller) const
{
	return ShoterTeam::Blue;
}

void AShoterGameMode_Zombies::GenericPlayerInitialization(AController* C)
{
	Super::GenericPlayerInitialization(C);

	if (AShoterPlayerState* State = C ? C->GetPlayerState<AShoterPlayerState>() : nullptr)
	{
		if (State->GetZombiePoints() == 0 && State->GetScore() <= 0.f)
		{
			State->AddZombiePoints(StartingPoints);
		}
	}
}

void AShoterGameMode_Zombies::HandleMatchHasStarted()
{
	Super::HandleMatchHasStarted();

	CurrentWave = 0;
	bInBreak = true;
	bGameOver = false;
	BreakRemaining = FirstWaveDelaySeconds;
	UpdateGameStateWave();
}

bool AShoterGameMode_Zombies::CanDamage(AController* InstigatorController, AActor* Victim) const
{
	if (!IsMatchInProgress())
	{
		return false;
	}
	if (Cast<AShoterZombie>(Victim))
	{
		return true;
	}
	const APawn* VictimPawn = Cast<APawn>(Victim);
	AController* VictimController = VictimPawn ? VictimPawn->GetController() : nullptr;
	if (InstigatorController && VictimController && InstigatorController != VictimController)
	{
		// No friendly fire between players; zombie controllers have no player state.
		return InstigatorController->GetPlayerState<APlayerState>() == nullptr;
	}
	return true;
}

void AShoterGameMode_Zombies::ScheduleRespawn(AController* Controller, float Delay)
{
	// Players are respawned together when the wave ends (see EndWave).
}

void AShoterGameMode_Zombies::GameTimer()
{
	if (IsMatchInProgress() && !bGameOver)
	{
		if (bInBreak)
		{
			BreakRemaining = FMath::Max(0, BreakRemaining - 1);
			if (BreakRemaining <= 0)
			{
				StartWave(CurrentWave + 1);
			}
		}
		UpdateGameStateWave();
		return;
	}
	Super::GameTimer();
}

int32 AShoterGameMode_Zombies::CountAlivePlayers() const
{
	int32 Alive = 0;
	for (FConstControllerIterator It = GetWorld()->GetControllerIterator(); It; ++It)
	{
		const AController* Controller = It->Get();
		if (!Controller || !Controller->GetPlayerState<APlayerState>())
		{
			continue;
		}
		if (const AShoterCharacter* Character = Controller->GetPawn<AShoterCharacter>())
		{
			Alive += Character->IsAlive() ? 1 : 0;
		}
	}
	return Alive;
}

int32 AShoterGameMode_Zombies::ComputeWaveSize(int32 Wave) const
{
	const int32 Players = FMath::Max(1, GetNumHumanPlayers());
	return FMath::RoundToInt((4.f + 3.f * Wave) * (0.6f + 0.4f * Players));
}

EShoterZombieType AShoterGameMode_Zombies::PickZombieType(int32 Wave) const
{
	const float BruteChance = Wave >= 5 ? FMath::Min(0.05f + 0.02f * (Wave - 5), 0.2f) : 0.f;
	const float RunnerChance = Wave >= 3 ? FMath::Min(0.15f + 0.05f * (Wave - 3), 0.5f) : 0.f;
	const float Roll = FMath::FRand();
	if (Roll < BruteChance)
	{
		return EShoterZombieType::Brute;
	}
	if (Roll < BruteChance + RunnerChance)
	{
		return EShoterZombieType::Runner;
	}
	return EShoterZombieType::Walker;
}

void AShoterGameMode_Zombies::StartWave(int32 WaveNumber)
{
	CurrentWave = WaveNumber;
	bInBreak = false;
	ZombiesLeftToSpawn = ComputeWaveSize(WaveNumber);
	GetWorldTimerManager().SetTimer(TimerHandle_Spawn, this, &AShoterGameMode_Zombies::SpawnTick, SpawnInterval, true, 0.5f);
	UpdateGameStateWave();

	UE_LOG(LogShoter, Log, TEXT("Zombies: wave %d starts with %d zombies"), WaveNumber, ZombiesLeftToSpawn);

	const FText Title = FText::Format(LOCTEXT("WaveTitle", "OLEADA {0}"), WaveNumber);
	const FText Subtitle = LOCTEXT("WaveSubtitle", "¡SOBREVIVAN!");
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (AShoterPlayerController* PC = Cast<AShoterPlayerController>(It->Get()))
		{
			PC->ClientAnnounce(Title, Subtitle, FLinearColor(1.f, 0.2f, 0.1f), 3.5f, TEXT("Wave_Start"));
		}
	}
}

void AShoterGameMode_Zombies::SpawnTick()
{
	if (bGameOver || bInBreak)
	{
		GetWorldTimerManager().ClearTimer(TimerHandle_Spawn);
		return;
	}

	AliveZombies.RemoveAll([](const TWeakObjectPtr<AShoterZombie>& Zombie) { return !Zombie.IsValid() || !Zombie->IsAlive(); });
	if (ZombiesLeftToSpawn <= 0)
	{
		GetWorldTimerManager().ClearTimer(TimerHandle_Spawn);
		return;
	}

	const int32 Burst = FMath::Clamp(GetNumHumanPlayers(), 1, 3);
	for (int32 Index = 0; Index < Burst && ZombiesLeftToSpawn > 0 && AliveZombies.Num() < MaxAliveZombies; ++Index)
	{
		if (!SpawnZombie())
		{
			break;
		}
		--ZombiesLeftToSpawn;
	}
	UpdateGameStateWave();
}

AShoterZombieSpawnPoint* AShoterGameMode_Zombies::PickSpawnPoint() const
{
	TArray<FVector> PlayerLocations;
	for (TActorIterator<AShoterCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsAlive())
		{
			PlayerLocations.Add(It->GetActorLocation());
		}
	}

	TArray<AShoterZombieSpawnPoint*> Candidates;
	TArray<float> Weights;
	AShoterZombieSpawnPoint* Fallback = nullptr;
	for (TActorIterator<AShoterZombieSpawnPoint> It(GetWorld()); It; ++It)
	{
		AShoterZombieSpawnPoint* Point = *It;
		if (!Point->bEnabled || CurrentWave < Point->MinWave)
		{
			continue;
		}
		Fallback = Point;

		float Closest = TNumericLimits<float>::Max();
		for (const FVector& PlayerLocation : PlayerLocations)
		{
			Closest = FMath::Min(Closest, FVector::Dist(Point->GetActorLocation(), PlayerLocation));
		}
		if (Closest < 700.f)
		{
			continue;	// never pop up right next to someone
		}
		Candidates.Add(Point);
		Weights.Add(Closest < 3500.f ? 3.f : 1.f);	// prefer points near the action
	}

	if (Candidates.Num() == 0)
	{
		return Fallback;
	}

	float Total = 0.f;
	for (float Weight : Weights)
	{
		Total += Weight;
	}
	float Roll = FMath::FRandRange(0.f, Total);
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		Roll -= Weights[Index];
		if (Roll <= 0.f)
		{
			return Candidates[Index];
		}
	}
	return Candidates.Last();
}

bool AShoterGameMode_Zombies::SpawnZombie()
{
	AShoterZombieSpawnPoint* Point = PickSpawnPoint();
	if (!Point || !ZombieClass)
	{
		return false;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
	const FVector Location = Point->GetActorLocation() + FVector(FMath::FRandRange(-80.f, 80.f), FMath::FRandRange(-80.f, 80.f), 0.f);
	AShoterZombie* Zombie = GetWorld()->SpawnActor<AShoterZombie>(ZombieClass, Location, Point->GetActorRotation(), Params);
	if (!Zombie)
	{
		return false;
	}

	Zombie->InitZombie(PickZombieType(CurrentWave), CurrentWave);
	if (!Zombie->GetController())
	{
		Zombie->SpawnDefaultController();
	}
	AliveZombies.Add(Zombie);
	return true;
}

void AShoterGameMode_Zombies::OnZombieDamaged(AShoterZombie* Zombie, AController* InstigatorController, float Damage, bool bHeadshot)
{
	if (AShoterPlayerState* State = InstigatorController ? InstigatorController->GetPlayerState<AShoterPlayerState>() : nullptr)
	{
		State->AddZombiePoints(PointsPerHit);
	}
	if (AShoterPlayerController* PC = Cast<AShoterPlayerController>(InstigatorController))
	{
		PC->ClientConfirmHit(bHeadshot, false, Damage);
	}
}

void AShoterGameMode_Zombies::OnZombieKilled(AShoterZombie* Zombie, AController* Killer, bool bHeadshot)
{
	AliveZombies.Remove(Zombie);

	if (AShoterPlayerState* State = Killer ? Killer->GetPlayerState<AShoterPlayerState>() : nullptr)
	{
		const int32 Points = PointsPerKill + (bHeadshot ? PointsHeadshotBonus : 0);
		State->AddZombiePoints(Points);
		State->ScoreKill(bHeadshot, 0);
		if (AShoterPlayerController* PC = Cast<AShoterPlayerController>(Killer))
		{
			PC->ClientOnEliminatedEnemy(LOCTEXT("ZombieName", "ZOMBIE").ToString(), bHeadshot, Points);
		}
	}

	AliveZombies.RemoveAll([](const TWeakObjectPtr<AShoterZombie>& Other) { return !Other.IsValid() || !Other->IsAlive(); });
	UpdateGameStateWave();
	if (!bInBreak && ZombiesLeftToSpawn <= 0 && AliveZombies.Num() == 0)
	{
		EndWave();
	}
}

void AShoterGameMode_Zombies::EndWave()
{
	if (bInBreak || bGameOver)
	{
		return;
	}
	bInBreak = true;
	BreakRemaining = WaveBreakSeconds;
	GetWorldTimerManager().ClearTimer(TimerHandle_Spawn);

	// Survivors get a bonus and a grenade; the fallen come back.
	for (FConstControllerIterator It = GetWorld()->GetControllerIterator(); It; ++It)
	{
		AController* Controller = It->Get();
		AShoterPlayerState* State = Controller ? Controller->GetPlayerState<AShoterPlayerState>() : nullptr;
		AShoterCharacter* Character = Controller ? Controller->GetPawn<AShoterCharacter>() : nullptr;
		if (State && Character && Character->IsAlive())
		{
			State->AddZombiePoints(PointsWaveBonus);
			Character->AddGrenades(1);
		}
	}
	RespawnDeadPlayers();
	UpdateGameStateWave();

	const FText Title = FText::Format(LOCTEXT("WaveDone", "OLEADA {0} SUPERADA"), CurrentWave);
	const FText Subtitle = FText::Format(LOCTEXT("WaveBonus", "+{0} PUNTOS  ·  PRÓXIMA EN {1} S"), PointsWaveBonus, WaveBreakSeconds);
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (AShoterPlayerController* PC = Cast<AShoterPlayerController>(It->Get()))
		{
			PC->ClientAnnounce(Title, Subtitle, FLinearColor(0.3f, 1.f, 0.5f), 3.5f, TEXT("Wave_Complete"));
		}
	}
}

void AShoterGameMode_Zombies::RespawnDeadPlayers()
{
	TArray<AController*> ToRespawn;
	for (FConstControllerIterator It = GetWorld()->GetControllerIterator(); It; ++It)
	{
		AController* Controller = It->Get();
		if (!Controller || !Controller->GetPlayerState<APlayerState>())
		{
			continue;
		}
		const AShoterCharacter* Character = Controller->GetPawn<AShoterCharacter>();
		if (!Character || !Character->IsAlive())
		{
			ToRespawn.Add(Controller);
		}
	}
	for (AController* Controller : ToRespawn)
	{
		RestartPlayer(Controller);
	}
}

void AShoterGameMode_Zombies::OnCharacterKilled(AShoterCharacter* Victim, AController* Killer, AActor* DamageCauser, bool bHeadshot, const TArray<AController*>& Assisters)
{
	Super::OnCharacterKilled(Victim, Killer, DamageCauser, bHeadshot, Assisters);
	CheckGameOver();
}

void AShoterGameMode_Zombies::Logout(AController* Exiting)
{
	Super::Logout(Exiting);
	CheckGameOver();
}

void AShoterGameMode_Zombies::CheckGameOver()
{
	if (bGameOver || !IsMatchInProgress() || CurrentWave == 0)
	{
		return;
	}
	if (CountAlivePlayers() > 0)
	{
		return;
	}

	bGameOver = true;
	GetWorldTimerManager().ClearTimer(TimerHandle_Spawn);
	UE_LOG(LogShoter, Log, TEXT("Zombies: game over at wave %d"), CurrentWave);
	FinishMatch(ShoterTeam::None, nullptr);
}

void AShoterGameMode_Zombies::UpdateGameStateWave()
{
	if (AShoterGameState* ShoterGameState = GetShoterGameState())
	{
		ShoterGameState->SetWaveState(CurrentWave, ZombiesLeftToSpawn + AliveZombies.Num(), bInBreak);
		ShoterGameState->SetRemainingTime(bInBreak ? BreakRemaining : -1);
	}
}

#undef LOCTEXT_NAMESPACE
