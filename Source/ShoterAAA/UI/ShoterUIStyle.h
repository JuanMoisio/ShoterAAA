// Shared look & feel of the menus that are built in C++: palette, fonts, brushes, button/checkbox
// widgets and small widget factories. Every factory constructs its widgets inside the given widget tree.
#pragma once

#include "CoreMinimal.h"
#include "Components/Button.h"
#include "Fonts/SlateFontInfo.h"
#include "Layout/Margin.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"
#include "Types/SlateEnums.h"
#include "ShoterUIStyle.generated.h"

class UBorder;
class UEditableTextBox;
class UHorizontalBox;
class UHorizontalBoxSlot;
class USizeBox;
class USlider;
class USpacer;
class UTextBlock;
class UVerticalBox;
class UVerticalBoxSlot;
class UWidget;
class UWidgetTree;
class UShoterGameInstance;
class UShoterMenuButton;
class UShoterMenuCheckBox;

/** Visual variants of UShoterMenuButton. */
enum class EShoterButtonKind : uint8
{
	Standard,	// dark fill, cyan tint on hover
	Primary,	// call to action: cyan outline and tinted fill
	Flat		// transparent until hovered (used by check boxes)
};

/** Native-only: bind with AddUObject / AddWeakLambda. */
DECLARE_MULTICAST_DELEGATE_OneParam(FShoterMenuButtonClickedSignature, UShoterMenuButton* /*Button*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FShoterMenuCheckChangedSignature, UShoterMenuCheckBox* /*CheckBox*/, bool /*bChecked*/);

/**
 * Button used by every menu. Plays UI_Hover / UI_Click and exposes a native click delegate that
 * passes the button itself, so one handler can serve a whole row of buttons (see Index).
 */
UCLASS()
class SHOTERAAA_API UShoterMenuButton : public UButton
{
	GENERATED_BODY()

public:
	/** Fired after the click sound. */
	FShoterMenuButtonClickedSignature OnMenuClicked;

	/** Free value for the owner: option index, stepper direction... */
	int32 Index = INDEX_NONE;

	void SetKind(EShoterButtonKind InKind);

	/** Toggle look (cyan fill and outline). */
	void SetSelected(bool bInSelected);
	bool IsSelected() const { return bSelected; }

	/** Creates the text child. Call once, right after construction. */
	void BuildLabel(UWidgetTree* Tree, const FText& Text, int32 FontSize, bool bLeftAlign);
	void SetLabelText(const FText& Text);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	UFUNCTION()
	void HandleHoveredSound();

	UFUNCTION()
	void HandleClickedSound();

	void RefreshStyle();

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> LabelBlock;

	EShoterButtonKind Kind = EShoterButtonKind::Standard;
	bool bSelected = false;
};

/** "[box] text" toggle built on top of the menu button. SetChecked does not fire OnCheckChanged; user clicks do. */
UCLASS()
class SHOTERAAA_API UShoterMenuCheckBox : public UShoterMenuButton
{
	GENERATED_BODY()

public:
	/** Builds the content. Call once, right after construction. */
	void BuildContent(UWidgetTree* Tree, const FText& Text);

	void SetChecked(bool bInChecked);
	bool IsChecked() const { return bChecked; }

	FShoterMenuCheckChangedSignature OnCheckChanged;

protected:
	void HandleToggled(UShoterMenuButton* Source);
	void RefreshMark();

	UPROPERTY(Transient)
	TObjectPtr<UBorder> CheckMark;

	bool bChecked = false;
};

namespace ShoterUI
{
	// --- Palette ---
	inline FLinearColor Accent()		{ return FLinearColor(0.10f, 0.85f, 1.00f, 1.0f); }
	inline FLinearColor AccentDim()		{ return FLinearColor(0.10f, 0.85f, 1.00f, 0.35f); }
	inline FLinearColor PanelFill()		{ return FLinearColor(0.02f, 0.03f, 0.05f, 0.85f); }
	inline FLinearColor InsetFill()		{ return FLinearColor(0.01f, 0.02f, 0.03f, 0.70f); }
	inline FLinearColor TextWhite()		{ return FLinearColor(1.00f, 1.00f, 1.00f, 1.0f); }
	inline FLinearColor TextMuted()		{ return FLinearColor(0.62f, 0.70f, 0.76f, 1.0f); }
	inline FLinearColor TextWarning()	{ return FLinearColor(1.00f, 0.72f, 0.20f, 1.0f); }

	// --- Fonts, brushes, styles ---
	SHOTERAAA_API FSlateFontInfo MakeFont(bool bBold, int32 Size);

	/** Flat or rounded box. Radius 0 gives a plain rectangle; OutlineWidth 0 draws no outline. */
	SHOTERAAA_API FSlateBrush MakeBoxBrush(const FLinearColor& Fill, const FLinearColor& Outline = FLinearColor(0.f, 0.f, 0.f, 0.f), float Radius = 4.f, float OutlineWidth = 0.f);

	SHOTERAAA_API FButtonStyle MakeButtonStyle(EShoterButtonKind Kind, bool bSelected);

	// --- Widget factories ---
	SHOTERAAA_API UTextBlock* MakeLabel(UWidgetTree* Tree, const FText& Text, int32 Size = 16, bool bBold = false, const FLinearColor& Color = FLinearColor(1.f, 1.f, 1.f, 1.f));
	SHOTERAAA_API UShoterMenuButton* MakeButton(UWidgetTree* Tree, const FText& Text, int32 FontSize = 20, EShoterButtonKind Kind = EShoterButtonKind::Standard, bool bLeftAlign = false);
	SHOTERAAA_API UShoterMenuCheckBox* MakeCheckBox(UWidgetTree* Tree, const FText& Text, bool bInitiallyChecked = false);

	/** Border with the given content; Padding is applied to the content slot. */
	SHOTERAAA_API UBorder* MakePanel(UWidgetTree* Tree, UWidget* Content, const FMargin& Padding, const FLinearColor& Fill, const FLinearColor& Outline = FLinearColor(0.f, 0.f, 0.f, 0.f), float Radius = 6.f);

	/** Size box; a Width / Height of 0 leaves that dimension free. */
	SHOTERAAA_API USizeBox* MakeSized(UWidgetTree* Tree, UWidget* Content, float Width = 0.f, float Height = 0.f);

	/** Slider normalized to 0..1 (owners map it to the real range). */
	SHOTERAAA_API USlider* MakeSlider(UWidgetTree* Tree);

	SHOTERAAA_API UEditableTextBox* MakeTextBox(UWidgetTree* Tree, const FString& InitialText, const FText& Hint);
	SHOTERAAA_API USpacer* MakeSpacer(UWidgetTree* Tree);

	// --- Layout helpers ---
	SHOTERAAA_API UVerticalBoxSlot* AddV(UVerticalBox* Box, UWidget* Child, const FMargin& Padding = FMargin(), bool bFill = false, EHorizontalAlignment HAlign = HAlign_Fill);

	/** FillWeight <= 0 sizes the slot automatically. */
	SHOTERAAA_API UHorizontalBoxSlot* AddH(UHorizontalBox* Box, UWidget* Child, const FMargin& Padding = FMargin(), float FillWeight = 0.f, EVerticalAlignment VAlign = VAlign_Center);

	// --- Misc ---
	SHOTERAAA_API UShoterGameInstance* GetShoterGameInstance(const UObject* Context);
}
