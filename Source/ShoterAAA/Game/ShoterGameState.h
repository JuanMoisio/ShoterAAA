#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameState.h"
#include "Core/ShoterTypes.h"
#include "ShoterGameState.generated.h"

class AShoterPlayerState;

UCLASS()
class SHOTERAAA_API AShoterGameState : public AGameState
{
	GENERATED_BODY()

public:
	AShoterGameState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	EShoterMatchType GetMatchType() const { return MatchType; }
	bool IsTeamMode() const { return MatchType == EShoterMatchType::TeamDeathmatch; }
	bool IsZombiesMode() const { return MatchType == EShoterMatchType::Zombies; }

	int32 GetTeamScore(uint8 Team) const;
	int32 GetScoreLimit() const { return ScoreLimit; }

	/** Seconds left in the match, or in the current zombies wave break. -1 = no limit. */
	int32 GetRemainingTime() const { return RemainingTime; }

	int32 GetWave() const { return Wave; }
	/** Zombies still to spawn + alive. */
	int32 GetZombiesRemaining() const { return ZombiesRemaining; }
	bool IsWaveBreak() const { return bWaveBreak; }

	/** Valid after the match ended. Team winner (TDM) or ShoterTeam::None. */
	uint8 GetWinningTeam() const { return WinningTeam; }
	/** Valid after the match ended in FFA. */
	APlayerState* GetWinningPlayer() const { return WinningPlayer; }

	/** All players sorted by score (desc), then kills. */
	TArray<AShoterPlayerState*> GetSortedPlayers() const;
	TArray<AShoterPlayerState*> GetSortedPlayersOfTeam(uint8 Team) const;

	/** Local kill feed, newest last. */
	const TArray<FShoterKillFeedEntry>& GetKillFeed() const { return KillFeed; }

	UFUNCTION(NetMulticast, Reliable)
	void MulticastKillFeed(const FShoterKillFeedEntry& Entry);

	// --- Server setters ---
	void SetMatchType(EShoterMatchType InType) { MatchType = InType; }
	void AddTeamScore(uint8 Team, int32 Delta);
	void SetScoreLimit(int32 InLimit) { ScoreLimit = InLimit; }
	void SetRemainingTime(int32 InSeconds) { RemainingTime = InSeconds; }
	void SetWaveState(int32 InWave, int32 InZombiesRemaining, bool bInBreak);
	void SetWinner(uint8 Team, APlayerState* Player);

protected:
	UPROPERTY(Replicated)
	EShoterMatchType MatchType = EShoterMatchType::TeamDeathmatch;

	UPROPERTY(Replicated)
	int32 BlueScore = 0;

	UPROPERTY(Replicated)
	int32 RedScore = 0;

	UPROPERTY(Replicated)
	int32 ScoreLimit = 0;

	UPROPERTY(Replicated)
	int32 RemainingTime = -1;

	UPROPERTY(Replicated)
	int32 Wave = 0;

	UPROPERTY(Replicated)
	int32 ZombiesRemaining = 0;

	UPROPERTY(Replicated)
	bool bWaveBreak = false;

	UPROPERTY(Replicated)
	uint8 WinningTeam = ShoterTeam::None;

	UPROPERTY(Replicated)
	TObjectPtr<APlayerState> WinningPlayer;

	TArray<FShoterKillFeedEntry> KillFeed;
};
