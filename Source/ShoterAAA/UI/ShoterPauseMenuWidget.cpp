#include "UI/ShoterPauseMenuWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/WidgetSwitcher.h"
#include "Online/ShoterGameInstance.h"
#include "Player/ShoterPlayerController.h"
#include "UI/ShoterSettingsWidget.h"
#include "UI/ShoterUIStyle.h"

// Accented characters are written as universal character names so the file does not depend on the
// source encoding the compiler assumes.
#define LOCTEXT_NAMESPACE "ShoterPauseMenu"

namespace
{
	enum EPauseButton : int32
	{
		PauseButton_Continue,
		PauseButton_Options,
		PauseButton_Invite,
		PauseButton_ExitToMenu,
		PauseButton_QuitGame
	};

	constexpr int32 PausePageMain = 0;
	constexpr int32 PausePageOptions = 1;
}

void UShoterPauseMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (!WidgetTree)
	{
		return;
	}

	const FLinearColor Clear(0.f, 0.f, 0.f, 0.f);

	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());
	WidgetTree->RootWidget = Root;

	// Full-screen dim. The border is hit-testable, so clicks do not reach anything behind the menu.
	Root->AddChildToOverlay(ShoterUI::MakePanel(WidgetTree, nullptr, FMargin(), FLinearColor(0.f, 0.f, 0.f, 0.60f), Clear, 0.f));

	// --- Page 0: pause menu ---
	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());

	UTextBlock* Title = ShoterUI::MakeLabel(WidgetTree, LOCTEXT("Title", "PAUSA"), 44, true);
	Title->SetJustification(ETextJustify::Center);
	ShoterUI::AddV(Column, Title, FMargin(0.f, 0.f, 0.f, 6.f));

	UTextBlock* Subtitle = ShoterUI::MakeLabel(WidgetTree, LOCTEXT("Subtitle", "La partida sigue en curso mientras este men\u00FA est\u00E1 abierto."), 15, false, ShoterUI::TextMuted());
	Subtitle->SetJustification(ETextJustify::Center);
	Subtitle->SetAutoWrapText(true);
	ShoterUI::AddV(Column, Subtitle, FMargin(0.f, 0.f, 0.f, 14.f));

	ShoterUI::AddV(Column, ShoterUI::MakeSized(WidgetTree, ShoterUI::MakePanel(WidgetTree, nullptr, FMargin(), ShoterUI::Accent(), Clear, 0.f), 0.f, 3.f), FMargin(0.f, 0.f, 0.f, 26.f));

	struct FButtonDef
	{
		int32 Index;
		FText Label;
		EShoterButtonKind Kind;
	};

	const FButtonDef Buttons[] =
	{
		{ PauseButton_Continue,		LOCTEXT("Continue", "CONTINUAR"),				EShoterButtonKind::Primary },
		{ PauseButton_Options,		LOCTEXT("Options", "OPCIONES"),					EShoterButtonKind::Standard },
		{ PauseButton_Invite,		LOCTEXT("InviteFriends", "INVITAR AMIGOS"),		EShoterButtonKind::Standard },
		{ PauseButton_ExitToMenu,	LOCTEXT("ExitToMenu", "SALIR AL MEN\u00DA"),	EShoterButtonKind::Standard },
		{ PauseButton_QuitGame,		LOCTEXT("QuitGame", "SALIR DEL JUEGO"),			EShoterButtonKind::Standard }
	};

	for (const FButtonDef& Def : Buttons)
	{
		UShoterMenuButton* Button = ShoterUI::MakeButton(WidgetTree, Def.Label, 22, Def.Kind);
		Button->Index = Def.Index;
		Button->OnMenuClicked.AddUObject(this, &UShoterPauseMenuWidget::HandleButtonClicked);
		ShoterUI::AddV(Column, ShoterUI::MakeSized(WidgetTree, Button, 0.f, 56.f), FMargin(0.f, 0.f, 0.f, 12.f));

		if (Def.Index == PauseButton_Invite)
		{
			InviteButton = Button;
		}
	}

	// --- Page 1: options ---
	SettingsWidget = CreateWidget<UShoterSettingsWidget>(this, UShoterSettingsWidget::StaticClass());

	PageSwitcher = WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass());
	PageSwitcher->AddChild(Column);
	if (SettingsWidget)
	{
		PageSwitcher->AddChild(SettingsWidget);
	}
	PageSwitcher->SetActiveWidgetIndex(PausePageMain);

	UBorder* Panel = ShoterUI::MakePanel(WidgetTree, PageSwitcher, FMargin(40.f), ShoterUI::PanelFill(), ShoterUI::AccentDim(), 8.f);
	UOverlaySlot* PanelSlot = Root->AddChildToOverlay(ShoterUI::MakeSized(WidgetTree, Panel, 760.f));
	if (PanelSlot)
	{
		PanelSlot->SetHorizontalAlignment(HAlign_Center);
		PanelSlot->SetVerticalAlignment(VAlign_Center);
	}
}

void UShoterPauseMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (SettingsWidget && !SettingsClosedHandle.IsValid())
	{
		SettingsClosedHandle = SettingsWidget->OnClosed.AddUObject(this, &UShoterPauseMenuWidget::HandleSettingsClosed);
	}

	if (InviteButton)
	{
		const UShoterGameInstance* GameInstance = ShoterUI::GetShoterGameInstance(this);
		const bool bSteam = GameInstance && GameInstance->IsUsingSteam();
		InviteButton->SetVisibility(bSteam ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	ShowPage(PausePageMain);
}

void UShoterPauseMenuWidget::NativeDestruct()
{
	if (SettingsWidget && SettingsClosedHandle.IsValid())
	{
		SettingsWidget->OnClosed.Remove(SettingsClosedHandle);
	}
	SettingsClosedHandle.Reset();

	Super::NativeDestruct();
}

void UShoterPauseMenuWidget::ShowPage(int32 PageIndex)
{
	if (PageIndex == PausePageOptions && SettingsWidget)
	{
		// Discard edits that were never applied.
		SettingsWidget->RefreshFromSettings();
	}
	if (PageSwitcher)
	{
		PageSwitcher->SetActiveWidgetIndex(PageIndex);
	}
}

void UShoterPauseMenuWidget::HandleSettingsClosed()
{
	ShowPage(PausePageMain);
}

void UShoterPauseMenuWidget::HandleButtonClicked(UShoterMenuButton* Button)
{
	if (!Button)
	{
		return;
	}

	UShoterGameInstance* GameInstance = ShoterUI::GetShoterGameInstance(this);

	// The controller may remove this widget while handling a click, so nothing is touched afterwards.
	switch (Button->Index)
	{
	case PauseButton_Continue:
		if (AShoterPlayerController* PlayerController = Cast<AShoterPlayerController>(GetOwningPlayer()))
		{
			PlayerController->TogglePauseMenu();
		}
		break;

	case PauseButton_Options:
		ShowPage(PausePageOptions);
		break;

	case PauseButton_Invite:
		if (GameInstance)
		{
			GameInstance->ShowInviteUI();
		}
		break;

	case PauseButton_ExitToMenu:
		if (GameInstance)
		{
			GameInstance->ReturnToMainMenuWithMessage();
		}
		break;

	case PauseButton_QuitGame:
		if (GameInstance)
		{
			GameInstance->QuitGame();
		}
		break;

	default:
		break;
	}
}

#undef LOCTEXT_NAMESPACE
