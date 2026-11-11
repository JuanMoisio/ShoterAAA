#include "Game/ShoterGameMode_PvP.h"
#include "Game/ShoterGameState.h"
#include "Game/ShoterPlayerStart.h"
#include "Player/ShoterPlayerState.h"
#include "Player/ShoterCharacter.h"

// ---------------------------------------------------------------------------
// Team Deathmatch
// ---------------------------------------------------------------------------

AShoterGameMode_TDM::AShoterGameMode_TDM()
{
	MatchType = EShoterMatchType::TeamDeathmatch;
	ScoreLimit = 50;
	TimeLimitSeconds = 600;
}

uint8 AShoterGameMode_TDM::ChooseTeam(AController* Controller) const
{
	int32 BlueCount = 0;
	int32 RedCount = 0;
	if (const AShoterGameState* ShoterGameState = GetShoterGameState())
	{
		const APlayerState* OwnState = Controller ? Controller->GetPlayerState<APlayerState>() : nullptr;
		for (APlayerState* State : ShoterGameState->PlayerArray)
		{
			const AShoterPlayerState* ShoterState = Cast<AShoterPlayerState>(State);
			if (!ShoterState || ShoterState == OwnState)
			{
				continue;
			}
			BlueCount += ShoterState->GetTeam() == ShoterTeam::Blue ? 1 : 0;
			RedCount += ShoterState->GetTeam() == ShoterTeam::Red ? 1 : 0;
		}
	}
	if (BlueCount == RedCount)
	{
		return FMath::RandBool() ? ShoterTeam::Blue : ShoterTeam::Red;
	}
	return BlueCount < RedCount ? ShoterTeam::Blue : ShoterTeam::Red;
}

bool AShoterGameMode_TDM::IsSpawnAllowedFor(const APlayerStart* Start, AController* Player) const
{
	if (!Super::IsSpawnAllowedFor(Start, Player))
	{
		return false;
	}
	const AShoterPlayerStart* ShoterStart = Cast<AShoterPlayerStart>(Start);
	if (!ShoterStart || ShoterStart->TeamId == ShoterTeam::None)
	{
		return true;
	}
	const AShoterPlayerState* State = Player ? Player->GetPlayerState<AShoterPlayerState>() : nullptr;
	return !State || State->GetTeam() == ShoterStart->TeamId;
}

void AShoterGameMode_TDM::OnCharacterKilled(AShoterCharacter* Victim, AController* Killer, AActor* DamageCauser, bool bHeadshot, const TArray<AController*>& Assisters)
{
	AController* VictimController = Victim ? Victim->GetController() : nullptr;
	if (Killer && VictimController && AreEnemies(Killer, VictimController))
	{
		if (const AShoterPlayerState* KillerState = Killer->GetPlayerState<AShoterPlayerState>())
		{
			if (AShoterGameState* ShoterGameState = GetShoterGameState())
			{
				ShoterGameState->AddTeamScore(KillerState->GetTeam(), 1);
			}
		}
	}
	Super::OnCharacterKilled(Victim, Killer, DamageCauser, bHeadshot, Assisters);
}

void AShoterGameMode_TDM::CheckScoreLimit()
{
	const AShoterGameState* ShoterGameState = GetShoterGameState();
	if (!ShoterGameState || ScoreLimit <= 0)
	{
		return;
	}
	if (ShoterGameState->GetTeamScore(ShoterTeam::Blue) >= ScoreLimit)
	{
		FinishMatch(ShoterTeam::Blue, nullptr);
	}
	else if (ShoterGameState->GetTeamScore(ShoterTeam::Red) >= ScoreLimit)
	{
		FinishMatch(ShoterTeam::Red, nullptr);
	}
}

void AShoterGameMode_TDM::OnTimeLimitReached()
{
	const AShoterGameState* ShoterGameState = GetShoterGameState();
	const int32 Blue = ShoterGameState ? ShoterGameState->GetTeamScore(ShoterTeam::Blue) : 0;
	const int32 Red = ShoterGameState ? ShoterGameState->GetTeamScore(ShoterTeam::Red) : 0;
	FinishMatch(Blue == Red ? ShoterTeam::None : (Blue > Red ? ShoterTeam::Blue : ShoterTeam::Red), nullptr);
}

// ---------------------------------------------------------------------------
// Free-for-all
// ---------------------------------------------------------------------------

AShoterGameMode_FFA::AShoterGameMode_FFA()
{
	MatchType = EShoterMatchType::FreeForAll;
	ScoreLimit = 25;
	TimeLimitSeconds = 600;
}

uint8 AShoterGameMode_FFA::ChooseTeam(AController* Controller) const
{
	return ShoterTeam::None;
}

APlayerState* AShoterGameMode_FFA::FindLeader() const
{
	const AShoterGameState* ShoterGameState = GetShoterGameState();
	if (!ShoterGameState)
	{
		return nullptr;
	}
	AShoterPlayerState* Leader = nullptr;
	for (APlayerState* State : ShoterGameState->PlayerArray)
	{
		AShoterPlayerState* ShoterState = Cast<AShoterPlayerState>(State);
		if (!ShoterState)
		{
			continue;
		}
		if (!Leader || ShoterState->GetKills() > Leader->GetKills()
			|| (ShoterState->GetKills() == Leader->GetKills() && ShoterState->GetScore() > Leader->GetScore()))
		{
			Leader = ShoterState;
		}
	}
	return Leader;
}

void AShoterGameMode_FFA::CheckScoreLimit()
{
	if (ScoreLimit <= 0)
	{
		return;
	}
	if (AShoterPlayerState* Leader = Cast<AShoterPlayerState>(FindLeader()))
	{
		if (Leader->GetKills() >= ScoreLimit)
		{
			FinishMatch(ShoterTeam::None, Leader);
		}
	}
}

void AShoterGameMode_FFA::OnTimeLimitReached()
{
	FinishMatch(ShoterTeam::None, FindLeader());
}
