#include "UI/ShoterSettingsWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Online/ShoterGameInstance.h"
#include "UI/ShoterUIStyle.h"

// Accented characters are written as universal character names so the file does not depend on the
// source encoding the compiler assumes.
#define LOCTEXT_NAMESPACE "ShoterSettings"

namespace
{
	constexpr float SettingsSensitivityMin = 0.1f;
	constexpr float SettingsSensitivityMax = 5.0f;
	constexpr float SettingsSensitivityStep = 0.05f;

	constexpr float SettingsAimSensitivityMin = 0.2f;
	constexpr float SettingsAimSensitivityMax = 1.5f;
	constexpr float SettingsAimSensitivityStep = 0.05f;

	constexpr float SettingsFieldOfViewMin = 80.f;
	constexpr float SettingsFieldOfViewMax = 115.f;
	constexpr float SettingsFieldOfViewStep = 1.f;

	constexpr float SettingsVolumeStep = 0.01f;
	constexpr int32 SettingsMaxPlayerNameLength = 24;
	constexpr int32 SettingsNumQualityLevels = 4;

	float SettingsToAlpha(float Value, float Min, float Max)
	{
		return Max > Min ? FMath::Clamp((Value - Min) / (Max - Min), 0.f, 1.f) : 0.f;
	}

	float SettingsFromAlpha(float Alpha, float Min, float Max, float Step)
	{
		const float Raw = FMath::Lerp(Min, Max, FMath::Clamp(Alpha, 0.f, 1.f));
		return FMath::Clamp(FMath::GridSnap(Raw, Step), Min, Max);
	}

	FText SettingsFormatDecimal(float Value)
	{
		return FText::FromString(FString::Printf(TEXT("%.2f"), Value));
	}

	FText SettingsFormatDegrees(float Value)
	{
		return FText::FromString(FString::Printf(TEXT("%d\u00B0"), FMath::RoundToInt(Value)));
	}

	FText SettingsFormatPercent(float Value)
	{
		return FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Value * 100.f)));
	}
}

void UShoterSettingsWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (!WidgetTree)
	{
		return;
	}

	UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	WidgetTree->RootWidget = Root;

	ShoterUI::AddV(Root, ShoterUI::MakeLabel(WidgetTree, LOCTEXT("Title", "OPCIONES"), 32, true), FMargin(0.f, 0.f, 0.f, 18.f));

	// --- Sliders ---
	SensitivitySlider = AddSliderRow(Root, LOCTEXT("Sensitivity", "Sensibilidad"), SensitivityValueText);
	SensitivitySlider->OnValueChanged.AddDynamic(this, &UShoterSettingsWidget::HandleSensitivityChanged);

	AimSensitivitySlider = AddSliderRow(Root, LOCTEXT("AimSensitivity", "Sensibilidad al apuntar"), AimSensitivityValueText);
	AimSensitivitySlider->OnValueChanged.AddDynamic(this, &UShoterSettingsWidget::HandleAimSensitivityChanged);

	FieldOfViewSlider = AddSliderRow(Root, LOCTEXT("FieldOfView", "Campo de visi\u00F3n"), FieldOfViewValueText);
	FieldOfViewSlider->OnValueChanged.AddDynamic(this, &UShoterSettingsWidget::HandleFieldOfViewChanged);

	MasterVolumeSlider = AddSliderRow(Root, LOCTEXT("MasterVolume", "Volumen general"), MasterVolumeValueText);
	MasterVolumeSlider->OnValueChanged.AddDynamic(this, &UShoterSettingsWidget::HandleMasterVolumeChanged);

	MusicVolumeSlider = AddSliderRow(Root, LOCTEXT("MusicVolume", "Volumen de m\u00FAsica"), MusicVolumeValueText);
	MusicVolumeSlider->OnValueChanged.AddDynamic(this, &UShoterSettingsWidget::HandleMusicVolumeChanged);

	// --- Graphics quality ---
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		ShoterUI::AddH(Row, ShoterUI::MakeSized(WidgetTree, ShoterUI::MakeLabel(WidgetTree, LOCTEXT("Quality", "Calidad gr\u00E1fica"), 18), 270.f), FMargin());

		const FText QualityNames[SettingsNumQualityLevels] =
		{
			LOCTEXT("QualityLow", "Baja"),
			LOCTEXT("QualityMedium", "Media"),
			LOCTEXT("QualityHigh", "Alta"),
			LOCTEXT("QualityEpic", "\u00C9pica")
		};

		for (int32 Level = 0; Level < SettingsNumQualityLevels; ++Level)
		{
			UShoterMenuButton* Button = ShoterUI::MakeButton(WidgetTree, QualityNames[Level], 17);
			Button->Index = Level;
			Button->OnMenuClicked.AddUObject(this, &UShoterSettingsWidget::HandleQualityClicked);
			QualityButtons.Add(Button);
			ShoterUI::AddH(Row, ShoterUI::MakeSized(WidgetTree, Button, 0.f, 40.f), FMargin(0.f, 0.f, Level + 1 < SettingsNumQualityLevels ? 8.f : 0.f, 0.f), 1.f);
		}
		ShoterUI::AddV(Root, Row, FMargin(0.f, 0.f, 0.f, 14.f));
	}

	// --- Check boxes ---
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

		InvertYCheckBox = ShoterUI::MakeCheckBox(WidgetTree, LOCTEXT("InvertY", "Invertir eje Y"));
		InvertYCheckBox->OnCheckChanged.AddUObject(this, &UShoterSettingsWidget::HandleCheckChanged);
		ShoterUI::AddH(Row, InvertYCheckBox, FMargin(0.f, 0.f, 24.f, 0.f), 1.f);

		ShowFpsCheckBox = ShoterUI::MakeCheckBox(WidgetTree, LOCTEXT("ShowFps", "Mostrar FPS"));
		ShowFpsCheckBox->OnCheckChanged.AddUObject(this, &UShoterSettingsWidget::HandleCheckChanged);
		ShoterUI::AddH(Row, ShowFpsCheckBox, FMargin(), 1.f);

		ShoterUI::AddV(Root, Row, FMargin(0.f, 0.f, 0.f, 14.f));
	}

	// --- Player name (used when there is no Steam) ---
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		ShoterUI::AddH(Row, ShoterUI::MakeSized(WidgetTree, ShoterUI::MakeLabel(WidgetTree, LOCTEXT("Name", "Nombre"), 18), 270.f), FMargin());

		NameTextBox = ShoterUI::MakeTextBox(WidgetTree, FString(), LOCTEXT("NameHint", "Nombre de jugador"));
		NameTextBox->OnTextChanged.AddDynamic(this, &UShoterSettingsWidget::HandleNameChanged);
		ShoterUI::AddH(Row, NameTextBox, FMargin(), 1.f);
		ShoterUI::AddV(Root, Row, FMargin(0.f, 0.f, 0.f, 4.f));

		NameHintText = ShoterUI::MakeLabel(WidgetTree, FText::GetEmpty(), 14, false, ShoterUI::TextMuted());
		ShoterUI::AddV(Root, NameHintText, FMargin(270.f, 0.f, 0.f, 0.f));
	}

	ShoterUI::AddV(Root, ShoterUI::MakeSpacer(WidgetTree), FMargin(), true);

	// --- Buttons ---
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

		FeedbackText = ShoterUI::MakeLabel(WidgetTree, FText::GetEmpty(), 16, false, ShoterUI::Accent());
		ShoterUI::AddH(Row, FeedbackText, FMargin(0.f, 0.f, 16.f, 0.f), 1.f);

		ApplyButton = ShoterUI::MakeButton(WidgetTree, LOCTEXT("Apply", "APLICAR"), 20, EShoterButtonKind::Primary);
		ApplyButton->OnMenuClicked.AddUObject(this, &UShoterSettingsWidget::HandleApplyClicked);
		ShoterUI::AddH(Row, ShoterUI::MakeSized(WidgetTree, ApplyButton, 190.f, 52.f), FMargin(0.f, 0.f, 12.f, 0.f));

		BackButton = ShoterUI::MakeButton(WidgetTree, LOCTEXT("Back", "VOLVER"), 20);
		BackButton->OnMenuClicked.AddUObject(this, &UShoterSettingsWidget::HandleBackClicked);
		ShoterUI::AddH(Row, ShoterUI::MakeSized(WidgetTree, BackButton, 190.f, 52.f), FMargin());

		ShoterUI::AddV(Root, Row, FMargin(0.f, 10.f, 0.f, 0.f));
	}

	RefreshFromSettings();
}

void UShoterSettingsWidget::NativeConstruct()
{
	Super::NativeConstruct();
	RefreshFromSettings();
}

USlider* UShoterSettingsWidget::AddSliderRow(UVerticalBox* Parent, const FText& Caption, TObjectPtr<UTextBlock>& OutValueText)
{
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	ShoterUI::AddH(Row, ShoterUI::MakeSized(WidgetTree, ShoterUI::MakeLabel(WidgetTree, Caption, 18), 270.f), FMargin());

	USlider* Slider = ShoterUI::MakeSlider(WidgetTree);
	ShoterUI::AddH(Row, Slider, FMargin(0.f, 0.f, 16.f, 0.f), 1.f);

	OutValueText = ShoterUI::MakeLabel(WidgetTree, FText::GetEmpty(), 18, true, ShoterUI::Accent());
	OutValueText->SetJustification(ETextJustify::Right);
	ShoterUI::AddH(Row, ShoterUI::MakeSized(WidgetTree, OutValueText, 80.f), FMargin());

	ShoterUI::AddV(Parent, Row, FMargin(0.f, 0.f, 0.f, 14.f));
	return Slider;
}

void UShoterSettingsWidget::RefreshFromSettings()
{
	if (const UShoterGameInstance* GameInstance = ShoterUI::GetShoterGameInstance(this))
	{
		Edited = GameInstance->GetUserSettings();

		if (NameHintText)
		{
			NameHintText->SetText(GameInstance->IsUsingSteam()
				? LOCTEXT("NameHintSteam", "Con Steam se usa tu nombre de Steam.")
				: LOCTEXT("NameHintNoSteam", "Se usa al jugar sin Steam (LAN / IP)."));
		}
	}
	else
	{
		Edited = FShoterUserSettings();
	}

	ClearFeedback();
	PushToControls();
}

void UShoterSettingsWidget::PushToControls()
{
	bPushingToControls = true;

	// SetValue does not raise OnValueChanged, so the labels are updated here as well.
	if (SensitivitySlider)
	{
		SensitivitySlider->SetValue(SettingsToAlpha(Edited.MouseSensitivity, SettingsSensitivityMin, SettingsSensitivityMax));
	}
	if (SensitivityValueText)
	{
		SensitivityValueText->SetText(SettingsFormatDecimal(Edited.MouseSensitivity));
	}

	if (AimSensitivitySlider)
	{
		AimSensitivitySlider->SetValue(SettingsToAlpha(Edited.AimSensitivityScale, SettingsAimSensitivityMin, SettingsAimSensitivityMax));
	}
	if (AimSensitivityValueText)
	{
		AimSensitivityValueText->SetText(SettingsFormatDecimal(Edited.AimSensitivityScale));
	}

	if (FieldOfViewSlider)
	{
		FieldOfViewSlider->SetValue(SettingsToAlpha(Edited.FieldOfView, SettingsFieldOfViewMin, SettingsFieldOfViewMax));
	}
	if (FieldOfViewValueText)
	{
		FieldOfViewValueText->SetText(SettingsFormatDegrees(Edited.FieldOfView));
	}

	if (MasterVolumeSlider)
	{
		MasterVolumeSlider->SetValue(FMath::Clamp(Edited.MasterVolume, 0.f, 1.f));
	}
	if (MasterVolumeValueText)
	{
		MasterVolumeValueText->SetText(SettingsFormatPercent(Edited.MasterVolume));
	}

	if (MusicVolumeSlider)
	{
		MusicVolumeSlider->SetValue(FMath::Clamp(Edited.MusicVolume, 0.f, 1.f));
	}
	if (MusicVolumeValueText)
	{
		MusicVolumeValueText->SetText(SettingsFormatPercent(Edited.MusicVolume));
	}

	if (InvertYCheckBox)
	{
		InvertYCheckBox->SetChecked(Edited.bInvertY);
	}
	if (ShowFpsCheckBox)
	{
		ShowFpsCheckBox->SetChecked(Edited.bShowFPS);
	}
	if (NameTextBox)
	{
		NameTextBox->SetText(FText::FromString(Edited.PlayerName));
	}

	RefreshQualityButtons();
	bPushingToControls = false;
}

void UShoterSettingsWidget::RefreshQualityButtons()
{
	for (int32 Level = 0; Level < QualityButtons.Num(); ++Level)
	{
		if (QualityButtons[Level])
		{
			QualityButtons[Level]->SetSelected(Level == Edited.GraphicsQuality);
		}
	}
}

void UShoterSettingsWidget::ClearFeedback()
{
	if (FeedbackText)
	{
		FeedbackText->SetText(FText::GetEmpty());
	}
}

// --- Handlers ---

void UShoterSettingsWidget::HandleSensitivityChanged(float Value)
{
	Edited.MouseSensitivity = SettingsFromAlpha(Value, SettingsSensitivityMin, SettingsSensitivityMax, SettingsSensitivityStep);
	if (SensitivityValueText)
	{
		SensitivityValueText->SetText(SettingsFormatDecimal(Edited.MouseSensitivity));
	}
	ClearFeedback();
}

void UShoterSettingsWidget::HandleAimSensitivityChanged(float Value)
{
	Edited.AimSensitivityScale = SettingsFromAlpha(Value, SettingsAimSensitivityMin, SettingsAimSensitivityMax, SettingsAimSensitivityStep);
	if (AimSensitivityValueText)
	{
		AimSensitivityValueText->SetText(SettingsFormatDecimal(Edited.AimSensitivityScale));
	}
	ClearFeedback();
}

void UShoterSettingsWidget::HandleFieldOfViewChanged(float Value)
{
	Edited.FieldOfView = SettingsFromAlpha(Value, SettingsFieldOfViewMin, SettingsFieldOfViewMax, SettingsFieldOfViewStep);
	if (FieldOfViewValueText)
	{
		FieldOfViewValueText->SetText(SettingsFormatDegrees(Edited.FieldOfView));
	}
	ClearFeedback();
}

void UShoterSettingsWidget::HandleMasterVolumeChanged(float Value)
{
	Edited.MasterVolume = SettingsFromAlpha(Value, 0.f, 1.f, SettingsVolumeStep);
	if (MasterVolumeValueText)
	{
		MasterVolumeValueText->SetText(SettingsFormatPercent(Edited.MasterVolume));
	}
	ClearFeedback();
}

void UShoterSettingsWidget::HandleMusicVolumeChanged(float Value)
{
	Edited.MusicVolume = SettingsFromAlpha(Value, 0.f, 1.f, SettingsVolumeStep);
	if (MusicVolumeValueText)
	{
		MusicVolumeValueText->SetText(SettingsFormatPercent(Edited.MusicVolume));
	}
	ClearFeedback();
}

void UShoterSettingsWidget::HandleNameChanged(const FText& Text)
{
	if (!bPushingToControls)
	{
		Edited.PlayerName = Text.ToString();
		ClearFeedback();
	}
}

void UShoterSettingsWidget::HandleQualityClicked(UShoterMenuButton* Button)
{
	if (Button)
	{
		Edited.GraphicsQuality = FMath::Clamp(Button->Index, 0, SettingsNumQualityLevels - 1);
		RefreshQualityButtons();
		ClearFeedback();
	}
}

void UShoterSettingsWidget::HandleCheckChanged(UShoterMenuCheckBox* CheckBox, bool bChecked)
{
	if (CheckBox == InvertYCheckBox)
	{
		Edited.bInvertY = bChecked;
	}
	else if (CheckBox == ShowFpsCheckBox)
	{
		Edited.bShowFPS = bChecked;
	}
	ClearFeedback();
}

void UShoterSettingsWidget::HandleApplyClicked(UShoterMenuButton* /*Button*/)
{
	UShoterGameInstance* GameInstance = ShoterUI::GetShoterGameInstance(this);
	if (!GameInstance)
	{
		return;
	}

	Edited.PlayerName = Edited.PlayerName.TrimStartAndEnd().Left(SettingsMaxPlayerNameLength);

	// Applies (graphics, audio), saves and broadcasts OnSettingsChanged.
	GameInstance->SetUserSettings(Edited);

	// Show whatever the game instance ended up storing (it may clamp values).
	Edited = GameInstance->GetUserSettings();
	PushToControls();

	if (FeedbackText)
	{
		FeedbackText->SetText(LOCTEXT("Applied", "Cambios aplicados."));
	}
}

void UShoterSettingsWidget::HandleBackClicked(UShoterMenuButton* /*Button*/)
{
	OnClosed.Broadcast();
}

#undef LOCTEXT_NAMESPACE
