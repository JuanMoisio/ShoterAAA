#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "Core/ShoterTypes.h"
#include "ShoterPlayerState.generated.h"

DECLARE_MULTICAST_DELEGATE(FShoterTeamChangedSignature);

UCLASS()
class SHOTERAAA_API AShoterPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	AShoterPlayerState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Reset() override;
	virtual void CopyProperties(APlayerState* PlayerState) override;

	uint8 GetTeam() const { return Team; }
	int32 GetKills() const { return Kills; }
	int32 GetDeaths() const { return Deaths; }
	int32 GetAssists() const { return Assists; }
	int32 GetHeadshots() const { return Headshots; }
	int32 GetKillStreak() const { return KillStreak; }
	int32 GetZombiePoints() const { return ZombiePoints; }

	// --- Server ---
	void SetTeam(uint8 NewTeam);
	void ScoreKill(bool bHeadshot, int32 ScorePoints);
	void ScoreAssist(int32 ScorePoints);
	void ScoreDeath();
	void AddZombiePoints(int32 Delta);
	/** Returns false (and spends nothing) if there are not enough points. */
	bool SpendZombiePoints(int32 Cost);
	void ResetMatchStats();

	/** Fires on every machine when the team changes. */
	FShoterTeamChangedSignature OnTeamChanged;

protected:
	UPROPERTY(ReplicatedUsing = OnRep_Team)
	uint8 Team = ShoterTeam::None;

	UPROPERTY(Replicated)
	int32 Kills = 0;

	UPROPERTY(Replicated)
	int32 Deaths = 0;

	UPROPERTY(Replicated)
	int32 Assists = 0;

	UPROPERTY(Replicated)
	int32 Headshots = 0;

	UPROPERTY(Replicated)
	int32 KillStreak = 0;

	UPROPERTY(Replicated)
	int32 ZombiePoints = 0;

	UFUNCTION()
	void OnRep_Team();
};
