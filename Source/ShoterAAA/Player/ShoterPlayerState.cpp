#include "Player/ShoterPlayerState.h"
#include "Net/UnrealNetwork.h"

AShoterPlayerState::AShoterPlayerState()
{
}

void AShoterPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AShoterPlayerState, Team);
	DOREPLIFETIME(AShoterPlayerState, Kills);
	DOREPLIFETIME(AShoterPlayerState, Deaths);
	DOREPLIFETIME(AShoterPlayerState, Assists);
	DOREPLIFETIME(AShoterPlayerState, Headshots);
	DOREPLIFETIME(AShoterPlayerState, KillStreak);
	DOREPLIFETIME(AShoterPlayerState, ZombiePoints);
}

void AShoterPlayerState::Reset()
{
	Super::Reset();
	ResetMatchStats();
}

void AShoterPlayerState::CopyProperties(APlayerState* PlayerState)
{
	Super::CopyProperties(PlayerState);

	// Seamless travel (match restart): keep the team, stats start over.
	if (AShoterPlayerState* Target = Cast<AShoterPlayerState>(PlayerState))
	{
		Target->Team = Team;
	}
}

void AShoterPlayerState::SetTeam(uint8 NewTeam)
{
	if (Team != NewTeam)
	{
		Team = NewTeam;
		OnRep_Team();
	}
}

void AShoterPlayerState::ScoreKill(bool bHeadshot, int32 ScorePoints)
{
	++Kills;
	++KillStreak;
	if (bHeadshot)
	{
		++Headshots;
	}
	SetScore(GetScore() + ScorePoints);
}

void AShoterPlayerState::ScoreAssist(int32 ScorePoints)
{
	++Assists;
	SetScore(GetScore() + ScorePoints);
}

void AShoterPlayerState::ScoreDeath()
{
	++Deaths;
	KillStreak = 0;
}

void AShoterPlayerState::AddZombiePoints(int32 Delta)
{
	ZombiePoints = FMath::Max(0, ZombiePoints + Delta);
	if (Delta > 0)
	{
		SetScore(GetScore() + Delta);
	}
}

bool AShoterPlayerState::SpendZombiePoints(int32 Cost)
{
	if (Cost < 0 || ZombiePoints < Cost)
	{
		return false;
	}
	ZombiePoints -= Cost;
	return true;
}

void AShoterPlayerState::ResetMatchStats()
{
	Kills = 0;
	Deaths = 0;
	Assists = 0;
	Headshots = 0;
	KillStreak = 0;
	ZombiePoints = 0;
	SetScore(0.f);
}

void AShoterPlayerState::OnRep_Team()
{
	OnTeamChanged.Broadcast();
}
