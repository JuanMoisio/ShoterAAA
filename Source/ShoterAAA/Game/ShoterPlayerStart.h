#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerStart.h"
#include "ShoterPlayerStart.generated.h"

/** Player start with a team restriction for Team Deathmatch. */
UCLASS()
class SHOTERAAA_API AShoterPlayerStart : public APlayerStart
{
	GENERATED_BODY()

public:
	AShoterPlayerStart(const FObjectInitializer& ObjectInitializer);

	/** Team allowed to spawn here in Team Deathmatch: 0 = Blue, 1 = Red, 255 = any. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shoter")
	uint8 TeamId = 255;

	/** Usable in Zombies mode (players start grouped together). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shoter")
	bool bUseInZombies = true;

	/** Usable in PvP modes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shoter")
	bool bUseInPvP = true;
};
