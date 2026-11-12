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

	/** 1: AimSensitivityScale was a flat multiplier (default 0.7). 2: it is relative to the zoom (default 1.0). */
	static constexpr int32 CurrentVersion = 2;

	UPROPERTY()
	int32 Version = CurrentVersion;

	UPROPERTY()
	FShoterUserSettings Settings;
};
