#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Core/ShoterTypes.h"
#include "ShoterSaveGame.generated.h"

/** Persisted user options (slot "ShoterSettings"). */
UCLASS()
class SHOTERAAA_API UShoterSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	static constexpr const TCHAR* SlotName = TEXT("ShoterSettings");

	UPROPERTY()
	int32 Version = 1;

	UPROPERTY()
	FShoterUserSettings Settings;
};
