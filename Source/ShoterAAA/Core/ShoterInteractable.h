#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ShoterInteractable.generated.h"

class AShoterCharacter;

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UShoterInteractable : public UInterface
{
	GENERATED_BODY()
};

/**
 * Anything the player can use with the Interact key (E): buy stations, weapon lockers...
 * The local character traces from the camera every frame to find the focused interactable,
 * shows GetInteractionPrompt() on the HUD and sends ServerInteract() when E is pressed.
 */
class SHOTERAAA_API IShoterInteractable
{
	GENERATED_BODY()

public:
	/** HUD prompt, e.g. "[E] Comprar Pulse Rifle (1000)". Called on the local client. */
	virtual FText GetInteractionPrompt(const AShoterCharacter* User) const { return FText::GetEmpty(); }

	/** Whether the prompt shows / interaction is allowed. Called on client and server. */
	virtual bool CanInteract(const AShoterCharacter* User) const { return false; }

	/** Server only. Performs the interaction. */
	virtual void Interact(AShoterCharacter* User) {}

	/** Max distance from the user's eyes, in cm. */
	virtual float GetInteractionRange() const { return 250.f; }
};
