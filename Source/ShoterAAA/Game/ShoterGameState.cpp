#include "Game/ShoterGameState.h"
#include "Player/ShoterPlayerState.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

AShoterGameState::AShoterGameState()
{
}

void AShoterGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AShoterGameState, MatchType);
	DOREPLIFETIME(AShoterGameState, BlueScore);
	DOREPLIFETIME(AShoterGameState, RedScore);
	DOREPLIFETIME(AShoterGameState, ScoreLimit);
	DOREPLIFETIME(AShoterGameState, RemainingTime);
	DOREPLIFETIME(AShoterGameState, Wave);
	DOREPLIFETIME(AShoterGameState, ZombiesRemaining);
	DOREPLIFETIME(AShoterGameState, bWaveBreak);
	DOREPLIFETIME(AShoterGameState, WinningTeam);
	DOREPLIFETIME(AShoterGameState, WinningPlayer);
}

int32 AShoterGameState::GetTeamScore(uint8 Team) const
{
	switch (Team)
	{
	case ShoterTeam::Blue:	return BlueScore;
	case ShoterTeam::Red:	return RedScore;
	default:				return 0;
	}
}

void AShoterGameState::AddTeamScore(uint8 Team, int32 Delta)
{
	if (Team == ShoterTeam::Blue)
	{
		BlueScore += Delta;
	}
	else if (Team == ShoterTeam::Red)
	{
		RedScore += Delta;
	}
}

void AShoterGameState::SetWaveState(int32 InWave, int32 InZombiesRemaining, bool bInBreak)
{
	Wave = InWave;
	ZombiesRemaining = InZombiesRemaining;
	bWaveBreak = bInBreak;
}

void AShoterGameState::SetWinner(uint8 Team, APlayerState* Player)
{
	WinningTeam = Team;
	WinningPlayer = Player;
}

TArray<AShoterPlayerState*> AShoterGameState::GetSortedPlayers() const
{
	TArray<AShoterPlayerState*> Players;
	for (APlayerState* State : PlayerArray)
	{
		if (AShoterPlayerState* ShoterState = Cast<AShoterPlayerState>(State))
		{
			Players.Add(ShoterState);
		}
	}
	Players.Sort([](const AShoterPlayerState& A, const AShoterPlayerState& B)
	{
		if (A.GetScore() != B.GetScore())
		{
			return A.GetScore() > B.GetScore();
		}
		return A.GetKills() > B.GetKills();
	});
	return Players;
}

TArray<AShoterPlayerState*> AShoterGameState::GetSortedPlayersOfTeam(uint8 Team) const
{
	TArray<AShoterPlayerState*> Players = GetSortedPlayers();
	Players.RemoveAll([Team](const AShoterPlayerState* State) { return !State || State->GetTeam() != Team; });
	return Players;
}

void AShoterGameState::MulticastKillFeed_Implementation(const FShoterKillFeedEntry& Entry)
{
	FShoterKillFeedEntry& Added = KillFeed.Add_GetRef(Entry);
	Added.LocalTimestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	while (KillFeed.Num() > 8)
	{
		KillFeed.RemoveAt(0);
	}
}
