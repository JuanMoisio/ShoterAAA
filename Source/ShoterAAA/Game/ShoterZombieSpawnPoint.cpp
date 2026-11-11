#include "Game/ShoterZombieSpawnPoint.h"
#include "Components/ArrowComponent.h"
#include "Components/BillboardComponent.h"

AShoterZombieSpawnPoint::AShoterZombieSpawnPoint()
{
	PrimaryActorTick.bCanEverTick = false;
	SetHidden(true);
	SetCanBeDamaged(false);

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

#if WITH_EDITORONLY_DATA
	ArrowComponent = CreateEditorOnlyDefaultSubobject<UArrowComponent>(TEXT("Arrow"));
	if (ArrowComponent)
	{
		ArrowComponent->SetupAttachment(Root);
		ArrowComponent->ArrowColor = FColor(170, 40, 255);
		ArrowComponent->ArrowSize = 1.5f;
	}

	SpriteComponent = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));
	if (SpriteComponent)
	{
		SpriteComponent->SetupAttachment(Root);
	}
#endif
}
