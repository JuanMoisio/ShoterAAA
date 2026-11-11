#include "Game/ShoterGameMode.h"
#include "ShoterAAA.h"
#include "Game/ShoterGameState.h"
#include "Game/ShoterPlayerStart.h"
#include "Player/ShoterCharacter.h"
#include "Player/ShoterPlayerController.h"
#include "Player/ShoterPlayerState.h"
#include "Weapons/ShoterWeapons.h"
#include "Weapons/ShoterProjectile.h"
#include "AI/ShoterBotController.h"
#include "AI/ShoterZombie.h"
#include "UI/ShoterHUD.h"
#include "GameFramework/GameSession.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "ShoterGameMode"

AShoterGameMode::AShoterGameMode()
{
	DefaultPawnClass = AShoterCharacter::StaticClass();
	PlayerControllerClass = AShoterPlayerController::StaticClass();
	PlayerStateClass = AShoterPlayerState::StaticClass();
	GameStateClass = AShoterGameState::StaticClass();
	HUDClass = AShoterHUD::StaticClass();
	BotControllerClass = AShoterBotController::StaticClass();

	bUseSeamlessTravel = true;
	bDelayedStart = false;

	DefaultLoadout.Add(AShoterWeapon_PulseRifle::StaticClass());
	DefaultLoadout.Add(AShoterWeapon_PulsePistol::StaticClass());
}

void AShoterGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	CurrentMapName = UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName());
	DesiredBots = FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("Bots"), DesiredBots), 0, 15);
	ScoreLimit = FMath::Max(0, UGameplayStatics::GetIntOption(Options, TEXT("ScoreLimit"), ScoreLimit));
	TimeLimitSeconds = FMath::Max(0, UGameplayStatics::GetIntOption(Options, TEXT("TimeLimit"), TimeLimitSeconds));

	UE_LOG(LogShoter, Log, TEXT("InitGame: map=%s mode=%s bots=%d"), *CurrentMapName, *ShoterMatch::GetGameAlias(MatchType), DesiredBots);
}

void AShoterGameMode::InitGameState()
{
	Super::InitGameState();

	if (AShoterGameState* ShoterGameState = GetShoterGameState())
	{
		ShoterGameState->SetMatchType(MatchType);
		ShoterGameState->SetScoreLimit(ScoreLimit);
		ShoterGameState->SetRemainingTime(TimeLimitSeconds > 0 ? TimeLimitSeconds : -1);
	}
}

AShoterGameState* AShoterGameMode::GetShoterGameState() const
{
	return GetGameState<AShoterGameState>();
}

FString AShoterGameMode::InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal)
{
	return Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);
}

void AShoterGameMode::GenericPlayerInitialization(AController* C)
{
	Super::GenericPlayerInitialization(C);

	if (AShoterPlayerState* State = C ? C->GetPlayerState<AShoterPlayerState>() : nullptr)
	{
		State->SetTeam(ChooseTeam(C));
	}
}

void AShoterGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	UpdateBotCount();

	if (const APlayerState* State = NewPlayer ? NewPlayer->GetPlayerState<APlayerState>() : nullptr)
	{
		UE_LOG(LogShoter, Log, TEXT("Player joined: %s"), *State->GetPlayerName());
		const FText Text = FText::Format(LOCTEXT("PlayerJoined", "{0} SE UNIÓ"), FText::FromString(State->GetPlayerName().ToUpper()));
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			if (AShoterPlayerController* PC = Cast<AShoterPlayerController>(It->Get()))
			{
				if (PC != NewPlayer)
				{
					PC->ClientNotify(Text, FLinearColor(0.6f, 0.8f, 1.f), NAME_None);
				}
			}
		}
	}
}

void AShoterGameMode::Logout(AController* Exiting)
{
	if (FTimerHandle* Handle = RespawnTimers.Find(Exiting))
	{
		GetWorldTimerManager().ClearTimer(*Handle);
		RespawnTimers.Remove(Exiting);
	}
	Super::Logout(Exiting);

	// Refill with a bot (deferred so the leaving player is no longer counted).
	FTimerHandle Unused;
	GetWorldTimerManager().SetTimer(Unused, this, &AShoterGameMode::UpdateBotCount, 0.5f, false);
}

uint8 AShoterGameMode::ChooseTeam(AController* Controller) const
{
	return ShoterTeam::None;
}

bool AShoterGameMode::AreEnemies(const AController* A, const AController* B) const
{
	if (!A || !B || A == B)
	{
		return false;
	}
	const AShoterPlayerState* StateA = A->GetPlayerState<AShoterPlayerState>();
	const AShoterPlayerState* StateB = B->GetPlayerState<AShoterPlayerState>();
	if (!StateA || !StateB)
	{
		// Zombies (no player state) are everyone's enemy.
		return true;
	}
	if (StateA->GetTeam() == ShoterTeam::None || StateB->GetTeam() == ShoterTeam::None)
	{
		return true;
	}
	return StateA->GetTeam() != StateB->GetTeam();
}

bool AShoterGameMode::CanDamage(AController* InstigatorController, AActor* Victim) const
{
	if (!IsMatchInProgress())
	{
		return false;
	}
	const APawn* VictimPawn = Cast<APawn>(Victim);
	AController* VictimController = VictimPawn ? VictimPawn->GetController() : nullptr;
	if (!InstigatorController || !VictimController || InstigatorController == VictimController)
	{
		return true;
	}
	return AreEnemies(InstigatorController, VictimController);
}

float AShoterGameMode::ModifyDamage(float Damage, AActor* Victim, AController* InstigatorController, AActor* DamageCauser) const
{
	if (!CanDamage(InstigatorController, Victim))
	{
		return 0.f;
	}
	if (const AShoterCharacter* Character = Cast<AShoterCharacter>(Victim))
	{
		if (Character->IsSpawnProtected())
		{
			return 0.f;
		}
		if (InstigatorController && InstigatorController == Character->GetController())
		{
			return Damage * 0.5f;
		}
	}
	return Damage;
}

FString AShoterGameMode::GetDamageCauserName(const AActor* DamageCauser)
{
	if (const AShoterWeapon* Weapon = Cast<AShoterWeapon>(DamageCauser))
	{
		return Weapon->GetDisplayName().ToString();
	}
	if (const AShoterProjectile* Projectile = Cast<AShoterProjectile>(DamageCauser))
	{
		return Projectile->GetWeaponName();
	}
	if (Cast<AShoterZombie>(DamageCauser))
	{
		return LOCTEXT("ZombieCause", "ZOMBIE").ToString();
	}
	return LOCTEXT("EnvironmentCause", "ENTORNO").ToString();
}

// ---------------------------------------------------------------------------
// Spawning
// ---------------------------------------------------------------------------

bool AShoterGameMode::ShouldSpawnAtStartSpot(AController* Player)
{
	return false;
}

bool AShoterGameMode::IsSpawnAllowedFor(const APlayerStart* Start, AController* Player) const
{
	if (const AShoterPlayerStart* ShoterStart = Cast<AShoterPlayerStart>(Start))
	{
		return MatchType == EShoterMatchType::Zombies ? ShoterStart->bUseInZombies : ShoterStart->bUseInPvP;
	}
	return true;
}

float AShoterGameMode::RatePlayerStart(const APlayerStart* Start, AController* Player) const
{
	const FVector Location = Start->GetActorLocation();
	float ClosestEnemy = 6000.f;

	for (TActorIterator<AShoterCharacter> It(GetWorld()); It; ++It)
	{
		const AShoterCharacter* Other = *It;
		if (!Other->IsAlive())
		{
			continue;
		}
		const float Distance = FVector::Dist(Location, Other->GetActorLocation());
		if (Distance < 150.f)
		{
			return -10000.f;
		}
		if (AreEnemies(Player, Other->GetController()))
		{
			ClosestEnemy = FMath::Min(ClosestEnemy, Distance);
		}
	}
	for (TActorIterator<AShoterZombie> It(GetWorld()); It; ++It)
	{
		if (It->IsAlive())
		{
			ClosestEnemy = FMath::Min(ClosestEnemy, FVector::Dist(Location, It->GetActorLocation()));
		}
	}
	return ClosestEnemy;
}

AActor* AShoterGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	TArray<TPair<APlayerStart*, float>> Rated;
	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
	{
		APlayerStart* Start = *It;
		if (IsSpawnAllowedFor(Start, Player))
		{
			Rated.Emplace(Start, RatePlayerStart(Start, Player) + FMath::FRandRange(0.f, 400.f));
		}
	}
	if (Rated.Num() == 0)
	{
		return Super::ChoosePlayerStart_Implementation(Player);
	}

	Rated.Sort([](const TPair<APlayerStart*, float>& A, const TPair<APlayerStart*, float>& B) { return A.Value > B.Value; });
	const int32 Pick = FMath::RandRange(0, FMath::Min(1, Rated.Num() - 1));
	return Rated[Pick].Key;
}

void AShoterGameMode::RestartPlayer(AController* NewPlayer)
{
	if (!NewPlayer || NewPlayer->IsPendingKillPending() || HasMatchEnded())
	{
		return;
	}
	if (APawn* OldPawn = NewPlayer->GetPawn())
	{
		NewPlayer->UnPossess();
		OldPawn->Destroy();
	}
	Super::RestartPlayer(NewPlayer);
}

void AShoterGameMode::SetPlayerDefaults(APawn* PlayerPawn)
{
	Super::SetPlayerDefaults(PlayerPawn);

	if (AShoterCharacter* Character = Cast<AShoterCharacter>(PlayerPawn))
	{
		Character->DestroyInventory();
		for (int32 Index = 0; Index < DefaultLoadout.Num(); ++Index)
		{
			Character->GiveWeapon(DefaultLoadout[Index], Index == 0);
		}
		Character->Grenades = DefaultGrenades;
		Character->RestoreHealth();
	}
}

void AShoterGameMode::ScheduleRespawn(AController* Controller, float Delay)
{
	if (!Controller)
	{
		return;
	}
	FTimerHandle& Handle = RespawnTimers.FindOrAdd(Controller);
	const FTimerDelegate Delegate = FTimerDelegate::CreateUObject(this, &AShoterGameMode::RespawnController, TWeakObjectPtr<AController>(Controller));
	GetWorldTimerManager().SetTimer(Handle, Delegate, FMath::Max(0.1f, Delay), false);
}

void AShoterGameMode::RespawnController(TWeakObjectPtr<AController> Controller)
{
	AController* ToRespawn = Controller.Get();
	RespawnTimers.Remove(Controller);
	if (ToRespawn && IsMatchInProgress())
	{
		RestartPlayer(ToRespawn);
	}
}

// ---------------------------------------------------------------------------
// Match flow
// ---------------------------------------------------------------------------

bool AShoterGameMode::ReadyToStartMatch_Implementation()
{
	return Super::ReadyToStartMatch_Implementation();
}

bool AShoterGameMode::ReadyToEndMatch_Implementation()
{
	// Every mode ends the match explicitly through FinishMatch.
	return false;
}

void AShoterGameMode::HandleMatchHasStarted()
{
	Super::HandleMatchHasStarted();

	TimeRemaining = TimeLimitSeconds;
	if (AShoterGameState* ShoterGameState = GetShoterGameState())
	{
		ShoterGameState->SetRemainingTime(TimeLimitSeconds > 0 ? TimeLimitSeconds : -1);
	}
	GetWorldTimerManager().SetTimer(TimerHandle_GameTimer, this, &AShoterGameMode::GameTimer, 1.f, true);

	UpdateBotCount();

	const FText Title = LOCTEXT("MatchStart", "¡A PELEAR!");
	const FText Subtitle = ShoterMatch::GetDisplayName(MatchType);
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (AShoterPlayerController* PC = Cast<AShoterPlayerController>(It->Get()))
		{
			PC->ClientAnnounce(Title, Subtitle, FLinearColor(0.2f, 0.9f, 1.f), 3.f, TEXT("Match_Start"));
		}
	}
}

void AShoterGameMode::GameTimer()
{
	AShoterGameState* ShoterGameState = GetShoterGameState();

	if (IsMatchInProgress())
	{
		if (TimeLimitSeconds > 0)
		{
			TimeRemaining = FMath::Max(0, TimeRemaining - 1);
			if (ShoterGameState)
			{
				ShoterGameState->SetRemainingTime(TimeRemaining);
			}
			if (TimeRemaining <= 0)
			{
				OnTimeLimitReached();
			}
		}
	}
	else if (GetMatchState() == MatchState::WaitingPostMatch)
	{
		PostMatchRemaining = FMath::Max(0, PostMatchRemaining - 1);
		if (ShoterGameState)
		{
			ShoterGameState->SetRemainingTime(PostMatchRemaining);
		}
		if (PostMatchRemaining <= 0)
		{
			GetWorldTimerManager().ClearTimer(TimerHandle_GameTimer);
			RestartMatchTravel();
		}
	}
}

void AShoterGameMode::FinishMatch(uint8 WinningTeam, APlayerState* WinningPlayer)
{
	if (!IsMatchInProgress())
	{
		return;
	}
	if (AShoterGameState* ShoterGameState = GetShoterGameState())
	{
		ShoterGameState->SetWinner(WinningTeam, WinningPlayer);
	}
	PostMatchRemaining = PostMatchSeconds;
	EndMatch();
}

void AShoterGameMode::HandleMatchHasEnded()
{
	Super::HandleMatchHasEnded();

	const AShoterGameState* ShoterGameState = GetShoterGameState();
	for (FConstControllerIterator It = GetWorld()->GetControllerIterator(); It; ++It)
	{
		AController* Controller = It->Get();
		if (!Controller)
		{
			continue;
		}

		bool bWinner = false;
		const AShoterPlayerState* State = Controller->GetPlayerState<AShoterPlayerState>();
		if (ShoterGameState && State)
		{
			bWinner = (ShoterGameState->GetWinningTeam() != ShoterTeam::None && State->GetTeam() == ShoterGameState->GetWinningTeam())
				|| ShoterGameState->GetWinningPlayer() == State;
		}

		if (AShoterCharacter* Character = Controller->GetPawn<AShoterCharacter>())
		{
			Character->StopFire();
		}
		Controller->GameHasEnded(nullptr, bWinner);
		if (APawn* Pawn = Controller->GetPawn())
		{
			Pawn->TurnOff();
		}
	}

	for (TPair<TWeakObjectPtr<AController>, FTimerHandle>& Entry : RespawnTimers)
	{
		GetWorldTimerManager().ClearTimer(Entry.Value);
	}
	RespawnTimers.Empty();

	if (AShoterGameState* MutableGameState = GetShoterGameState())
	{
		MutableGameState->SetRemainingTime(PostMatchRemaining);
	}
}

FString AShoterGameMode::BuildTravelURL() const
{
	return FString::Printf(TEXT("%s?game=%s?Bots=%d?listen"), *CurrentMapName, *ShoterMatch::GetGameAlias(MatchType), DesiredBots);
}

void AShoterGameMode::RestartMatchTravel()
{
	UE_LOG(LogShoter, Log, TEXT("Restarting match: %s"), *BuildTravelURL());
	GetWorld()->ServerTravel(BuildTravelURL(), false);
}

// ---------------------------------------------------------------------------
// Kills
// ---------------------------------------------------------------------------

void AShoterGameMode::OnCharacterKilled(AShoterCharacter* Victim, AController* Killer, AActor* DamageCauser, bool bHeadshot, const TArray<AController*>& Assisters)
{
	if (!Victim)
	{
		return;
	}

	AController* VictimController = Victim->GetController();
	AShoterPlayerState* VictimState = VictimController ? VictimController->GetPlayerState<AShoterPlayerState>() : Victim->GetShoterPlayerState();
	AShoterPlayerState* KillerState = Killer ? Killer->GetPlayerState<AShoterPlayerState>() : nullptr;
	const bool bSuicide = !Killer || Killer == VictimController;
	const bool bValidKill = !bSuicide && AreEnemies(Killer, VictimController);
	const FString WeaponName = bSuicide && !DamageCauser ? LOCTEXT("SuicideCause", "SUICIDIO").ToString() : GetDamageCauserName(DamageCauser);
	const FString VictimName = VictimState ? VictimState->GetPlayerName() : FString(TEXT("?"));

	UE_LOG(LogShoter, Log, TEXT("Kill: %s -> %s [%s]%s"), KillerState ? *KillerState->GetPlayerName() : *WeaponName,
		*VictimName, *WeaponName, bHeadshot ? TEXT(" headshot") : TEXT(""));

	if (VictimState)
	{
		VictimState->ScoreDeath();
	}

	if (KillerState && bValidKill)
	{
		const int32 Points = KillScore + (bHeadshot ? HeadshotBonusScore : 0);
		KillerState->ScoreKill(bHeadshot, Points);

		if (AShoterPlayerController* KillerPC = Cast<AShoterPlayerController>(Killer))
		{
			KillerPC->ClientOnEliminatedEnemy(VictimName, bHeadshot, Points);

			const int32 Streak = KillerState->GetKillStreak();
			if (Streak == 3 || Streak == 5 || Streak == 8 || Streak == 12)
			{
				KillerPC->ClientNotify(FText::Format(LOCTEXT("Streak", "RACHA DE {0} BAJAS"), Streak), FLinearColor(1.f, 0.6f, 0.1f), TEXT("KillConfirm"));
			}
		}
	}

	for (AController* Assister : Assisters)
	{
		AShoterPlayerState* AssisterState = Assister ? Assister->GetPlayerState<AShoterPlayerState>() : nullptr;
		if (AssisterState && Assister != Killer && AreEnemies(Assister, VictimController))
		{
			AssisterState->ScoreAssist(AssistScore);
			if (AShoterPlayerController* AssisterPC = Cast<AShoterPlayerController>(Assister))
			{
				AssisterPC->ClientNotify(FText::Format(LOCTEXT("Assist", "+{0}  ASISTENCIA"), AssistScore), FLinearColor(0.7f, 0.85f, 1.f), NAME_None);
			}
		}
	}

	FShoterKillFeedEntry Entry;
	Entry.KillerName = KillerState ? KillerState->GetPlayerName() : (bSuicide ? FString() : WeaponName);
	Entry.KillerTeam = KillerState ? KillerState->GetTeam() : ShoterTeam::None;
	Entry.KillerPlayerState = KillerState;
	Entry.VictimName = VictimName;
	Entry.VictimTeam = VictimState ? VictimState->GetTeam() : ShoterTeam::None;
	Entry.VictimPlayerState = VictimState;
	Entry.WeaponName = WeaponName;
	Entry.bHeadshot = bHeadshot;
	if (AShoterGameState* ShoterGameState = GetShoterGameState())
	{
		ShoterGameState->MulticastKillFeed(Entry);
	}

	if (AShoterPlayerController* VictimPC = Cast<AShoterPlayerController>(VictimController))
	{
		const FString KillerName = KillerState ? KillerState->GetPlayerName() : (bSuicide ? FString() : WeaponName);
		VictimPC->ClientOnDeath(KillerName, WeaponName, RespawnDelay, Victim->GetActorLocation(), Killer ? Killer->GetPawn() : nullptr);
	}

	ScheduleRespawn(VictimController, RespawnDelay);
	CheckScoreLimit();
}

// ---------------------------------------------------------------------------
// Bots
// ---------------------------------------------------------------------------

int32 AShoterGameMode::GetNumHumanPlayers() const
{
	int32 Count = 0;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		const APlayerState* State = PC ? PC->GetPlayerState<APlayerState>() : nullptr;
		if (State && !State->IsABot())
		{
			++Count;
		}
	}
	return Count;
}

void AShoterGameMode::UpdateBotCount()
{
	if (!IsMatchInProgress() || !BotControllerClass)
	{
		return;
	}

	const int32 MaxPlayers = GameSession ? GameSession->MaxPlayers : 8;
	const int32 TargetBots = FMath::Clamp(DesiredBots, 0, FMath::Max(0, MaxPlayers - GetNumHumanPlayers()));

	Bots.RemoveAll([](const TWeakObjectPtr<AShoterBotController>& Bot) { return !Bot.IsValid(); });
	while (Bots.Num() < TargetBots)
	{
		if (!AddBot())
		{
			break;
		}
	}
	while (Bots.Num() > TargetBots)
	{
		RemoveBot();
	}
}

AShoterBotController* AShoterGameMode::AddBot()
{
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AShoterBotController* Bot = GetWorld()->SpawnActor<AShoterBotController>(BotControllerClass, FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (!Bot)
	{
		return nullptr;
	}

	static const TCHAR* BotNames[] =
	{
		TEXT("Nova"), TEXT("Vex"), TEXT("Kairo"), TEXT("Lynx"), TEXT("Orion"), TEXT("Zephyr"),
		TEXT("Rook"), TEXT("Echo"), TEXT("Juno"), TEXT("Talon"), TEXT("Nyx"), TEXT("Atlas")
	};
	if (AShoterPlayerState* State = Bot->GetPlayerState<AShoterPlayerState>())
	{
		// Unique names; the scoreboard already tags bots with [BOT].
		TArray<FString> UsedNames;
		for (const TWeakObjectPtr<AShoterBotController>& Other : Bots)
		{
			if (const APlayerState* OtherState = Other.IsValid() ? Other->GetPlayerState<APlayerState>() : nullptr)
			{
				UsedNames.Add(OtherState->GetPlayerName());
			}
		}
		FString BotName = BotNames[FMath::RandRange(0, UE_ARRAY_COUNT(BotNames) - 1)];
		for (int32 Attempt = 0; Attempt < 32 && UsedNames.Contains(BotName); ++Attempt)
		{
			BotName = BotNames[FMath::RandRange(0, UE_ARRAY_COUNT(BotNames) - 1)];
		}
		if (UsedNames.Contains(BotName))
		{
			BotName = FString::Printf(TEXT("%s %d"), *BotName, Bots.Num() + 1);
		}
		State->SetPlayerName(BotName);
		State->SetIsABot(true);
		State->SetTeam(ChooseTeam(Bot));
	}
	Bot->Skill = FMath::FRandRange(0.35f, 0.7f);
	Bots.Add(Bot);

	RestartPlayer(Bot);
	return Bot;
}

void AShoterGameMode::RemoveBot()
{
	if (Bots.Num() == 0)
	{
		return;
	}
	if (AShoterBotController* Bot = Bots.Last().Get())
	{
		if (APawn* Pawn = Bot->GetPawn())
		{
			Pawn->Destroy();
		}
		Bot->Destroy();
	}
	Bots.Pop();
}

#undef LOCTEXT_NAMESPACE
