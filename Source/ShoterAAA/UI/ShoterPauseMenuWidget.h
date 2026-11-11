// In-match pause menu built in C++. Created by AShoterPlayerController::TogglePauseMenu().
// Opening/closing and input modes are handled by the player controller, not by this widget.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ShoterPauseMenuWidget.generated.h"

class UShoterMenuButton;
class UShoterSettingsWidget;
class UWidgetSwitcher;

UCLASS()
class SHOTERAAA_API UShoterPauseMenuWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** One handler for the whole button column; the button's Index says which one. */
	void HandleButtonClicked(UShoterMenuButton* Button);
	void HandleSettingsClosed();

	/** Shows the main pause page (index 0) or the options page (index 1). */
	void ShowPage(int32 PageIndex);

	UPROPERTY(Transient)
	TObjectPtr<UWidgetSwitcher> PageSwitcher;

	UPROPERTY(Transient)
	TObjectPtr<UShoterSettingsWidget> SettingsWidget;

	UPROPERTY(Transient)
	TObjectPtr<UShoterMenuButton> InviteButton;

	FDelegateHandle SettingsClosedHandle;
};
