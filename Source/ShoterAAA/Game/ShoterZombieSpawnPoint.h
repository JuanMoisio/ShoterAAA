#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShoterZombieSpawnPoint.generated.h"

class UArrowComponent;
class UBillboardComponent;

/** Where zombies can appear. Place them around the map, ideally out of the players' sight. */
UCLASS()
class SHOTERAAA_API AShoterZombieSpawnPoint : public AActor
{
	GENERATED_BODY()

public:
	AShoterZombieSpawnPoint();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shoter")
	bool bEnabled = true;

	/** Only used from this wave on (to open new areas later). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shoter")
	int32 MinWave = 1;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UArrowComponent> ArrowComponent;

	UPROPERTY()
	TObjectPtr<UBillboardComponent> SpriteComponent;
#endif
};
