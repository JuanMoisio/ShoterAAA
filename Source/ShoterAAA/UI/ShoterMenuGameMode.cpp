#include "UI/ShoterMenuGameMode.h"

#include "GameFramework/HUD.h"
#include "UI/ShoterMenuPlayerController.h"

AShoterMenuGameMode::AShoterMenuGameMode()
{
	PlayerControllerClass = AShoterMenuPlayerController::StaticClass();
	DefaultPawnClass = nullptr;
	HUDClass = AHUD::StaticClass();
}

void AShoterMenuGameMode::RestartPlayer(AController* /*NewPlayer*/)
{
	// Intentionally empty: the menu has no pawn. The player controller shows the UI and looks through
	// a camera actor, so neither a PlayerStart nor a default pawn class is needed.
}
