#pragma once

#include "CoreMinimal.h"
#include "Engine/LocalPlayer.h"
#include "ShoterLocalPlayer.generated.h"

/** Uses the name chosen in the options when playing without Steam (the engine sends it as ?Name= on login). */
UCLASS()
class SHOTERAAA_API UShoterLocalPlayer : public ULocalPlayer
{
	GENERATED_BODY()

public:
	virtual FString GetNickname() const override;
};
