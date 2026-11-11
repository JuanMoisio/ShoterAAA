// Options panel shared by the main menu and the pause menu.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Core/ShoterTypes.h"
#include "ShoterSettingsWidget.generated.h"

class UEditableTextBox;
class USlider;
class UTextBlock;
class UVerticalBox;
class UShoterMenuButton;
class UShoterMenuCheckBox;

DECLARE_MULTICAST_DELEGATE(FShoterSettingsClosedSignature);

UCLASS()
class SHOTERAAA_API UShoterSettingsWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Broadcast when the user presses "Volver". The owner removes/hides the widget. */
	FShoterSettingsClosedSignature OnClosed;

	/** Discards unapplied edits and reloads every control from the game instance's current settings. */
	void RefreshFromSettings();

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;

	// --- Slider handlers (sliders work in 0..1 and are mapped to the real ranges) ---
	UFUNCTION()
	void HandleSensitivityChanged(float Value);

	UFUNCTION()
	void HandleAimSensitivityChanged(float Value);

	UFUNCTION()
	void HandleFieldOfViewChanged(float Value);

	UFUNCTION()
	void HandleMasterVolumeChanged(float Value);

	UFUNCTION()
	void HandleMusicVolumeChanged(float Value);

	UFUNCTION()
	void HandleNameChanged(const FText& Text);

	// --- Native button / checkbox handlers ---
	void HandleQualityClicked(UShoterMenuButton* Button);
	void HandleApplyClicked(UShoterMenuButton* Button);
	void HandleBackClicked(UShoterMenuButton* Button);
	void HandleCheckChanged(UShoterMenuCheckBox* CheckBox, bool bChecked);

	/** Adds "caption | slider | value" to Parent and returns the slider. */
	USlider* AddSliderRow(UVerticalBox* Parent, const FText& Caption, TObjectPtr<UTextBlock>& OutValueText);

	/** Pushes Edited into every control (without firing their change events into Edited). */
	void PushToControls();
	void RefreshQualityButtons();
	void ClearFeedback();

	/** The copy being edited; APLICAR hands it to the game instance. */
	FShoterUserSettings Edited;

	bool bPushingToControls = false;

	UPROPERTY(Transient)
	TObjectPtr<USlider> SensitivitySlider;

	UPROPERTY(Transient)
	TObjectPtr<USlider> AimSensitivitySlider;

	UPROPERTY(Transient)
	TObjectPtr<USlider> FieldOfViewSlider;

	UPROPERTY(Transient)
	TObjectPtr<USlider> MasterVolumeSlider;

	UPROPERTY(Transient)
	TObjectPtr<USlider> MusicVolumeSlider;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SensitivityValueText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> AimSensitivityValueText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> FieldOfViewValueText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> MasterVolumeValueText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> MusicVolumeValueText;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UShoterMenuButton>> QualityButtons;

	UPROPERTY(Transient)
	TObjectPtr<UShoterMenuCheckBox> InvertYCheckBox;

	UPROPERTY(Transient)
	TObjectPtr<UShoterMenuCheckBox> ShowFpsCheckBox;

	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> NameTextBox;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> NameHintText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> FeedbackText;

	UPROPERTY(Transient)
	TObjectPtr<UShoterMenuButton> ApplyButton;

	UPROPERTY(Transient)
	TObjectPtr<UShoterMenuButton> BackButton;
};
