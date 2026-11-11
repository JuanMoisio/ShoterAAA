#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ShoterMenuGameMode.generated.h"

/** Game mode of the MainMenu map: no pawn, just the menu UI over a slow cinematic camera. */
UCLASS()
class SHOTERAAA_API AShoterMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AShoterMenuGameMode();

	/** Nobody gets a pawn in the menu (no player start needed, nothing logged). */
	virtual void RestartPlayer(AController* NewPlayer) override;
};
