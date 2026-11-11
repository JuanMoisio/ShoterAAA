#pragma once

#include "CoreMinimal.h"
#include "Game/ShoterGameMode.h"
#include "ShoterGameMode_PvP.generated.h"

/** Team Deathmatch: Blue vs Red, first team to ScoreLimit kills wins. */
UCLASS(Config = Game)
class SHOTERAAA_API AShoterGameMode_TDM : public AShoterGameMode
{
	GENERATED_BODY()

public:
	AShoterGameMode_TDM();

	virtual void OnCharacterKilled(AShoterCharacter* Victim, AController* Killer, AActor* DamageCauser, bool bHeadshot, const TArray<AController*>& Assisters) override;

protected:
	virtual uint8 ChooseTeam(AController* Controller) const override;
	virtual bool IsSpawnAllowedFor(const APlayerStart* Start, AController* Player) const override;
	virtual void CheckScoreLimit() override;
	virtual void OnTimeLimitReached() override;
};

/** Free-for-all: everyone for themselves, first player to ScoreLimit kills wins. */
UCLASS(Config = Game)
class SHOTERAAA_API AShoterGameMode_FFA : public AShoterGameMode
{
	GENERATED_BODY()

public:
	AShoterGameMode_FFA();

protected:
	virtual uint8 ChooseTeam(AController* Controller) const override;
	virtual void CheckScoreLimit() override;
	virtual void OnTimeLimitReached() override;

	APlayerState* FindLeader() const;
};
