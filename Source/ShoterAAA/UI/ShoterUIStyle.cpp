#include "UI/ShoterUIStyle.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/ButtonSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/PanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Core/ShoterAssets.h"
#include "Engine/World.h"
#include "Online/ShoterGameInstance.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SWidget.h"

// ---------------------------------------------------------------------------------------------
// UShoterMenuButton
// ---------------------------------------------------------------------------------------------

TSharedRef<SWidget> UShoterMenuButton::RebuildWidget()
{
	// RebuildWidget can run more than once for the same widget, hence the unique bindings.
	OnHovered.AddUniqueDynamic(this, &UShoterMenuButton::HandleHoveredSound);
	OnClicked.AddUniqueDynamic(this, &UShoterMenuButton::HandleClickedSound);
	return Super::RebuildWidget();
}

void UShoterMenuButton::HandleHoveredSound()
{
	UShoterAssets::PlaySound2D(this, TEXT("UI_Hover"));
}

void UShoterMenuButton::HandleClickedSound()
{
	UShoterAssets::PlaySound2D(this, TEXT("UI_Click"));
	OnMenuClicked.Broadcast(this);
}

void UShoterMenuButton::SetKind(EShoterButtonKind InKind)
{
	Kind = InKind;
	RefreshStyle();
}

void UShoterMenuButton::SetSelected(bool bInSelected)
{
	if (bSelected != bInSelected)
	{
		bSelected = bInSelected;
		RefreshStyle();
	}
}

void UShoterMenuButton::RefreshStyle()
{
	SetStyle(ShoterUI::MakeButtonStyle(Kind, bSelected));
}

void UShoterMenuButton::BuildLabel(UWidgetTree* Tree, const FText& Text, int32 FontSize, bool bLeftAlign)
{
	if (!Tree)
	{
		return;
	}

	LabelBlock = ShoterUI::MakeLabel(Tree, Text, FontSize, true, ShoterUI::TextWhite());

	UPanelSlot* PanelSlot = AddChild(LabelBlock);
	if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(PanelSlot))
	{
		ButtonSlot->SetHorizontalAlignment(bLeftAlign ? HAlign_Left : HAlign_Center);
		ButtonSlot->SetVerticalAlignment(VAlign_Center);
		ButtonSlot->SetPadding(bLeftAlign ? FMargin(24.f, 8.f, 12.f, 8.f) : FMargin(16.f, 8.f));
	}
}

void UShoterMenuButton::SetLabelText(const FText& Text)
{
	if (LabelBlock)
	{
		LabelBlock->SetText(Text);
	}
}

// ---------------------------------------------------------------------------------------------
// UShoterMenuCheckBox
// ---------------------------------------------------------------------------------------------

void UShoterMenuCheckBox::BuildContent(UWidgetTree* Tree, const FText& Text)
{
	if (!Tree)
	{
		return;
	}

	// Inner square, visible only while checked. The frame is not hit-testable so clicks reach the button.
	CheckMark = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
	CheckMark->SetBrush(ShoterUI::MakeBoxBrush(ShoterUI::Accent(), FLinearColor(0.f, 0.f, 0.f, 0.f), 2.f, 0.f));
	CheckMark->SetVisibility(ESlateVisibility::HitTestInvisible);

	UBorder* Frame = ShoterUI::MakePanel(Tree, CheckMark, FMargin(4.f), FLinearColor(0.01f, 0.02f, 0.03f, 0.9f), ShoterUI::Accent(), 3.f);
	Frame->SetVisibility(ESlateVisibility::HitTestInvisible);

	UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	ShoterUI::AddH(Row, ShoterUI::MakeSized(Tree, Frame, 24.f, 24.f), FMargin(0.f, 0.f, 12.f, 0.f));
	ShoterUI::AddH(Row, ShoterUI::MakeLabel(Tree, Text, 18, false), FMargin());

	UPanelSlot* PanelSlot = AddChild(Row);
	if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(PanelSlot))
	{
		ButtonSlot->SetHorizontalAlignment(HAlign_Left);
		ButtonSlot->SetVerticalAlignment(VAlign_Center);
		ButtonSlot->SetPadding(FMargin(10.f, 6.f));
	}

	OnMenuClicked.AddUObject(this, &UShoterMenuCheckBox::HandleToggled);
	RefreshMark();
}

void UShoterMenuCheckBox::SetChecked(bool bInChecked)
{
	bChecked = bInChecked;
	RefreshMark();
}

void UShoterMenuCheckBox::HandleToggled(UShoterMenuButton* /*Source*/)
{
	SetChecked(!bChecked);
	OnCheckChanged.Broadcast(this, bChecked);
}

void UShoterMenuCheckBox::RefreshMark()
{
	if (CheckMark)
	{
		CheckMark->SetVisibility(bChecked ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}
}

// ---------------------------------------------------------------------------------------------
// ShoterUI
// ---------------------------------------------------------------------------------------------

FSlateFontInfo ShoterUI::MakeFont(bool bBold, int32 Size)
{
	return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
}

FSlateBrush ShoterUI::MakeBoxBrush(const FLinearColor& Fill, const FLinearColor& Outline, float Radius, float OutlineWidth)
{
	FSlateBrush Brush;
	Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
	Brush.TintColor = FSlateColor(Fill);
	Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
	Brush.OutlineSettings.CornerRadii = FVector4(Radius, Radius, Radius, Radius);
	Brush.OutlineSettings.Color = FSlateColor(Outline);
	Brush.OutlineSettings.Width = OutlineWidth;
	return Brush;
}

FButtonStyle ShoterUI::MakeButtonStyle(EShoterButtonKind Kind, bool bSelected)
{
	const FLinearColor Clear(0.f, 0.f, 0.f, 0.f);

	FLinearColor Fill(0.03f, 0.05f, 0.08f, 0.92f);
	FLinearColor Outline = AccentDim();
	FLinearColor HoverFill(0.05f, 0.30f, 0.38f, 0.95f);
	FLinearColor HoverOutline = Accent();
	FLinearColor PressedFill(0.08f, 0.50f, 0.62f, 1.0f);
	float OutlineWidth = 1.f;

	if (Kind == EShoterButtonKind::Primary)
	{
		Fill = FLinearColor(0.04f, 0.24f, 0.31f, 0.95f);
		Outline = FLinearColor(0.10f, 0.85f, 1.00f, 0.80f);
		HoverFill = FLinearColor(0.07f, 0.42f, 0.52f, 0.98f);
		OutlineWidth = 1.5f;
	}
	else if (Kind == EShoterButtonKind::Flat)
	{
		Fill = Clear;
		Outline = Clear;
		HoverFill = FLinearColor(0.10f, 0.85f, 1.00f, 0.12f);
		HoverOutline = Clear;
		PressedFill = FLinearColor(0.10f, 0.85f, 1.00f, 0.25f);
		OutlineWidth = 0.f;
	}

	if (bSelected)
	{
		Fill = FLinearColor(0.05f, 0.36f, 0.46f, 0.96f);
		Outline = Accent();
		HoverFill = FLinearColor(0.08f, 0.48f, 0.60f, 0.98f);
		HoverOutline = Accent();
		OutlineWidth = 2.f;
	}

	const float HoverWidth = FMath::Max(OutlineWidth, HoverOutline.A > 0.f ? 1.5f : 0.f);

	FButtonStyle Style;
	Style.SetNormal(MakeBoxBrush(Fill, Outline, 4.f, OutlineWidth));
	Style.SetHovered(MakeBoxBrush(HoverFill, HoverOutline, 4.f, HoverWidth));
	Style.SetPressed(MakeBoxBrush(PressedFill, HoverOutline, 4.f, HoverWidth));
	if (Kind == EShoterButtonKind::Flat)
	{
		Style.SetDisabled(MakeBoxBrush(Clear, Clear, 4.f, 0.f));
	}
	else
	{
		Style.SetDisabled(MakeBoxBrush(FLinearColor(0.03f, 0.04f, 0.06f, 0.55f), FLinearColor(0.5f, 0.5f, 0.5f, 0.25f), 4.f, 1.f));
	}
	return Style;
}

UTextBlock* ShoterUI::MakeLabel(UWidgetTree* Tree, const FText& Text, int32 Size, bool bBold, const FLinearColor& Color)
{
	UTextBlock* Label = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Label->SetText(Text);
	Label->SetFont(MakeFont(bBold, Size));
	Label->SetColorAndOpacity(FSlateColor(Color));
	return Label;
}

UShoterMenuButton* ShoterUI::MakeButton(UWidgetTree* Tree, const FText& Text, int32 FontSize, EShoterButtonKind Kind, bool bLeftAlign)
{
	UShoterMenuButton* Button = Tree->ConstructWidget<UShoterMenuButton>(UShoterMenuButton::StaticClass());
	Button->SetKind(Kind);
	Button->BuildLabel(Tree, Text, FontSize, bLeftAlign);
	return Button;
}

UShoterMenuCheckBox* ShoterUI::MakeCheckBox(UWidgetTree* Tree, const FText& Text, bool bInitiallyChecked)
{
	UShoterMenuCheckBox* CheckBox = Tree->ConstructWidget<UShoterMenuCheckBox>(UShoterMenuCheckBox::StaticClass());
	CheckBox->SetKind(EShoterButtonKind::Flat);
	CheckBox->BuildContent(Tree, Text);
	CheckBox->SetChecked(bInitiallyChecked);
	return CheckBox;
}

UBorder* ShoterUI::MakePanel(UWidgetTree* Tree, UWidget* Content, const FMargin& Padding, const FLinearColor& Fill, const FLinearColor& Outline, float Radius)
{
	UBorder* Panel = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Panel->SetBrush(MakeBoxBrush(Fill, Outline, Radius, Outline.A > 0.f ? 1.f : 0.f));

	if (Content)
	{
		UPanelSlot* PanelSlot = Panel->SetContent(Content);
		if (UBorderSlot* BorderSlot = Cast<UBorderSlot>(PanelSlot))
		{
			BorderSlot->SetPadding(Padding);
			BorderSlot->SetHorizontalAlignment(HAlign_Fill);
			BorderSlot->SetVerticalAlignment(VAlign_Fill);
		}
	}
	return Panel;
}

USizeBox* ShoterUI::MakeSized(UWidgetTree* Tree, UWidget* Content, float Width, float Height)
{
	USizeBox* SizeBox = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	if (Width > 0.f)
	{
		SizeBox->SetWidthOverride(Width);
	}
	if (Height > 0.f)
	{
		SizeBox->SetHeightOverride(Height);
	}
	if (Content)
	{
		SizeBox->SetContent(Content);
	}
	return SizeBox;
}

USlider* ShoterUI::MakeSlider(UWidgetTree* Tree)
{
	USlider* Slider = Tree->ConstructWidget<USlider>(USlider::StaticClass());
	Slider->SetValue(0.f);
	Slider->SetSliderBarColor(FLinearColor(0.30f, 0.40f, 0.48f, 1.f));
	Slider->SetSliderHandleColor(Accent());
	return Slider;
}

UEditableTextBox* ShoterUI::MakeTextBox(UWidgetTree* Tree, const FString& InitialText, const FText& Hint)
{
	UEditableTextBox* TextBox = Tree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass());
	TextBox->SetText(FText::FromString(InitialText));
	TextBox->SetHintText(Hint);

	// Dark input look. This is the only place that touches the text box style members; if the engine
	// version in use renamed/deprecated any of them, adapt here.
	FEditableTextBoxStyle Style = TextBox->WidgetStyle;

	FTextBlockStyle TextStyle = Style.TextStyle;
	TextStyle.SetFont(MakeFont(false, 20));
	TextStyle.SetColorAndOpacity(FSlateColor(TextWhite()));
	Style.SetTextStyle(TextStyle);

	const FLinearColor InputFill(0.01f, 0.02f, 0.04f, 0.92f);
	Style.SetBackgroundImageNormal(MakeBoxBrush(InputFill, AccentDim(), 4.f, 1.f));
	Style.SetBackgroundImageHovered(MakeBoxBrush(InputFill, FLinearColor(0.10f, 0.85f, 1.00f, 0.70f), 4.f, 1.f));
	Style.SetBackgroundImageFocused(MakeBoxBrush(InputFill, Accent(), 4.f, 2.f));
	Style.SetBackgroundImageReadOnly(MakeBoxBrush(InputFill, AccentDim(), 4.f, 1.f));
	Style.SetForegroundColor(FSlateColor(TextWhite()));
	Style.SetFocusedForegroundColor(FSlateColor(TextWhite()));
	Style.SetReadOnlyForegroundColor(FSlateColor(TextMuted()));
	Style.SetPadding(FMargin(14.f, 8.f));
	TextBox->WidgetStyle = Style;
	return TextBox;
}

USpacer* ShoterUI::MakeSpacer(UWidgetTree* Tree)
{
	return Tree->ConstructWidget<USpacer>(USpacer::StaticClass());
}

UVerticalBoxSlot* ShoterUI::AddV(UVerticalBox* Box, UWidget* Child, const FMargin& Padding, bool bFill, EHorizontalAlignment HAlign)
{
	UVerticalBoxSlot* BoxSlot = Box ? Box->AddChildToVerticalBox(Child) : nullptr;
	if (!BoxSlot)
	{
		return nullptr;
	}

	BoxSlot->SetPadding(Padding);
	BoxSlot->SetHorizontalAlignment(HAlign);
	if (bFill)
	{
		BoxSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	return BoxSlot;
}

UHorizontalBoxSlot* ShoterUI::AddH(UHorizontalBox* Box, UWidget* Child, const FMargin& Padding, float FillWeight, EVerticalAlignment VAlign)
{
	UHorizontalBoxSlot* BoxSlot = Box ? Box->AddChildToHorizontalBox(Child) : nullptr;
	if (!BoxSlot)
	{
		return nullptr;
	}

	BoxSlot->SetPadding(Padding);
	BoxSlot->SetVerticalAlignment(VAlign);
	if (FillWeight > 0.f)
	{
		FSlateChildSize Size(ESlateSizeRule::Fill);
		Size.Value = FillWeight;
		BoxSlot->SetSize(Size);
	}
	return BoxSlot;
}

UShoterGameInstance* ShoterUI::GetShoterGameInstance(const UObject* Context)
{
	const UWorld* World = Context ? Context->GetWorld() : nullptr;
	return World ? World->GetGameInstance<UShoterGameInstance>() : nullptr;
}
