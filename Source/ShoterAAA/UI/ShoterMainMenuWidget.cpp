#include "UI/ShoterMainMenuWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/PanelSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/WidgetSwitcher.h"
#include "Online/ShoterGameInstance.h"
#include "UI/ShoterServerRowWidget.h"
#include "UI/ShoterSettingsWidget.h"
#include "UI/ShoterUIStyle.h"

// Accented characters are written as universal character names so the file does not depend on the
// source encoding the compiler assumes.
#define LOCTEXT_NAMESPACE "ShoterMainMenu"

namespace
{
	/** Right-side panels, in WidgetSwitcher order. */
	enum EMenuPanelIndex : int32
	{
		MenuPanel_Host,
		MenuPanel_Browse,
		MenuPanel_Direct,
		MenuPanel_Options
	};

	/** Left column buttons. The first four open the panel with the same index. */
	enum EMenuNavIndex : int32
	{
		MenuNav_Play,
		MenuNav_Browse,
		MenuNav_Direct,
		MenuNav_Options,
		MenuNav_Quit,
		MenuNav_Count
	};

	enum EMenuStepperIndex : int32
	{
		MenuStepper_BotsDown,
		MenuStepper_BotsUp,
		MenuStepper_PlayersDown,
		MenuStepper_PlayersUp
	};

	constexpr int32 MenuNumModes = 3;
	constexpr int32 MenuMinBots = 0;
	constexpr int32 MenuMaxBots = 7;
	constexpr int32 MenuMinMaxPlayers = 2;
	constexpr int32 MenuMaxMaxPlayers = 8;

	constexpr float MenuLeftColumnWidth = 420.f;
	constexpr float MenuRightPanelWidth = 920.f;
	constexpr float MenuRightPanelHeight = 700.f;

	const TCHAR* const MenuDefaultAddress = TEXT("127.0.0.1");

	FText MenuModeDescription(EShoterMatchType Mode)
	{
		switch (Mode)
		{
		case EShoterMatchType::FreeForAll:
			return LOCTEXT("DescFFA", "Cada jugador por su cuenta. Gana quien consiga m\u00E1s eliminaciones.");
		case EShoterMatchType::Zombies:
			return LOCTEXT("DescZombies", "Cooperativo: sobrevive a oleadas de zombis junto a tu equipo.");
		default:
			return LOCTEXT("DescTDM", "Dos equipos se enfrentan; gana el primero en alcanzar la puntuaci\u00F3n l\u00EDmite.");
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Tree construction
// ---------------------------------------------------------------------------------------------

void UShoterMainMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (!WidgetTree)
	{
		return;
	}

	Maps = UShoterGameInstance::GetAvailableMaps();

	UHorizontalBox* Root = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	WidgetTree->RootWidget = Root;

	// Left column: fixed width, full height.
	ShoterUI::AddH(Root, ShoterUI::MakeSized(WidgetTree, BuildLeftColumn(), MenuLeftColumnWidth), FMargin(), 0.f, VAlign_Fill);

	// Right side: the switchable panels (same order as EMenuPanelIndex) plus the message line.
	PanelSwitcher = WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass());
	PanelSwitcher->AddChild(BuildHostPanel());
	PanelSwitcher->AddChild(BuildBrowsePanel());
	PanelSwitcher->AddChild(BuildDirectPanel());
	PanelSwitcher->AddChild(BuildOptionsPanel());

	MessageText = ShoterUI::MakeLabel(WidgetTree, FText::GetEmpty(), 15, false, ShoterUI::TextMuted());
	MessageText->SetAutoWrapText(true);

	UVerticalBox* PanelColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	ShoterUI::AddV(PanelColumn, PanelSwitcher, FMargin(), true);
	ShoterUI::AddV(PanelColumn, MessageText, FMargin(0.f, 14.f, 0.f, 0.f));

	UBorder* Panel = ShoterUI::MakePanel(WidgetTree, PanelColumn, FMargin(36.f), ShoterUI::PanelFill(), ShoterUI::AccentDim(), 8.f);

	UOverlay* RightArea = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());
	UOverlaySlot* PanelSlot = RightArea->AddChildToOverlay(ShoterUI::MakeSized(WidgetTree, Panel, MenuRightPanelWidth, MenuRightPanelHeight));
	if (PanelSlot)
	{
		PanelSlot->SetHorizontalAlignment(HAlign_Left);
		PanelSlot->SetVerticalAlignment(VAlign_Center);
		PanelSlot->SetPadding(FMargin(40.f, 36.f));
	}
	ShoterUI::AddH(Root, RightArea, FMargin(), 1.f, VAlign_Fill);

	RefreshHostPanel();
	ShowPanel(MenuPanel_Host);
}

UWidget* UShoterMainMenuWidget::BuildLeftColumn()
{
	const FLinearColor Clear(0.f, 0.f, 0.f, 0.f);

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());

	// Title: "SHOTER" in white, "AAA" in cyan.
	UHorizontalBox* TitleRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	ShoterUI::AddH(TitleRow, ShoterUI::MakeLabel(WidgetTree, LOCTEXT("TitleMain", "SHOTER"), 76, true), FMargin(0.f, 0.f, 12.f, 0.f), 0.f, VAlign_Bottom);
	ShoterUI::AddH(TitleRow, ShoterUI::MakeLabel(WidgetTree, LOCTEXT("TitleAccent", "AAA"), 76, true, ShoterUI::Accent()), FMargin(), 0.f, VAlign_Bottom);
	ShoterUI::AddV(Column, TitleRow, FMargin(0.f, 0.f, 0.f, 4.f));

	ShoterUI::AddV(Column, ShoterUI::MakeSized(WidgetTree, ShoterUI::MakePanel(WidgetTree, nullptr, FMargin(), ShoterUI::Accent(), Clear, 0.f), 0.f, 3.f), FMargin(0.f, 0.f, 120.f, 10.f));
	ShoterUI::AddV(Column, ShoterUI::MakeLabel(WidgetTree, LOCTEXT("Tagline", "PVP Y COOPERATIVO CONTRA ZOMBIS"), 14, true, ShoterUI::TextMuted()), FMargin(0.f, 0.f, 0.f, 44.f));

	// Navigation
	const FText Captions[MenuNav_Count] =
	{
		LOCTEXT("NavPlay", "JUGAR"),
		LOCTEXT("NavBrowse", "BUSCAR PARTIDAS"),
		LOCTEXT("NavDirect", "CONECTAR POR IP"),
		LOCTEXT("NavOptions", "OPCIONES"),
		LOCTEXT("NavQuit", "SALIR")
	};

	for (int32 Nav = 0; Nav < MenuNav_Count; ++Nav)
	{
		UShoterMenuButton* Button = ShoterUI::MakeButton(WidgetTree, Captions[Nav], 24, EShoterButtonKind::Standard, true);
		Button->Index = Nav;
		Button->OnMenuClicked.AddUObject(this, &UShoterMainMenuWidget::HandleNavClicked);
		NavButtons.Add(Button);
		ShoterUI::AddV(Column, ShoterUI::MakeSized(WidgetTree, Button, 0.f, 58.f), FMargin(0.f, 0.f, 0.f, 10.f));
	}

	ShoterUI::AddV(Column, ShoterUI::MakeSpacer(WidgetTree), FMargin(), true);

	OnlineStatusText = ShoterUI::MakeLabel(WidgetTree, FText::GetEmpty(), 15, false, ShoterUI::TextMuted());
	ShoterUI::AddV(Column, OnlineStatusText);

	return ShoterUI::MakePanel(WidgetTree, Column, FMargin(44.f, 56.f, 44.f, 36.f), FLinearColor(0.01f, 0.02f, 0.03f, 0.82f), Clear, 0.f);
}

UWidget* UShoterMainMenuWidget::BuildHostPanel()
{
	UVerticalBox* Panel = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());

	auto MakeSection = [this](const FText& Text)
	{
		return ShoterUI::MakeLabel(WidgetTree, Text, 14, true, ShoterUI::Accent());
	};

	// [-] value [+]. DownIndex / UpIndex are EMenuStepperIndex values.
	auto MakeStepper = [this](int32 DownIndex, int32 UpIndex, TObjectPtr<UTextBlock>& OutValue) -> UWidget*
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

		UShoterMenuButton* Down = ShoterUI::MakeButton(WidgetTree, FText::FromString(TEXT("-")), 28);
		Down->Index = DownIndex;
		Down->OnMenuClicked.AddUObject(this, &UShoterMainMenuWidget::HandleStepperClicked);
		ShoterUI::AddH(Row, ShoterUI::MakeSized(WidgetTree, Down, 56.f, 48.f), FMargin());

		OutValue = ShoterUI::MakeLabel(WidgetTree, FText::GetEmpty(), 28, true, ShoterUI::Accent());
		OutValue->SetJustification(ETextJustify::Center);
		ShoterUI::AddH(Row, ShoterUI::MakeSized(WidgetTree, OutValue, 80.f), FMargin());

		UShoterMenuButton* Up = ShoterUI::MakeButton(WidgetTree, FText::FromString(TEXT("+")), 28);
		Up->Index = UpIndex;
		Up->OnMenuClicked.AddUObject(this, &UShoterMainMenuWidget::HandleStepperClicked);
		ShoterUI::AddH(Row, ShoterUI::MakeSized(WidgetTree, Up, 56.f, 48.f), FMargin());
		return Row;
	};

	ShoterUI::AddV(Panel, ShoterUI::MakeLabel(WidgetTree, LOCTEXT("HostTitle", "CREAR PARTIDA"), 32, true), FMargin(0.f, 0.f, 0.f, 18.f));

	// --- Mode (toggle buttons) ---
	ShoterUI::AddV(Panel, MakeSection(LOCTEXT("SectionMode", "MODO DE JUEGO")), FMargin(0.f, 0.f, 0.f, 8.f));

	UHorizontalBox* ModeRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	for (int32 Mode = 0; Mode < MenuNumModes; ++Mode)
	{
		UShoterMenuButton* Button = ShoterUI::MakeButton(WidgetTree, ShoterMatch::GetDisplayName(static_cast<EShoterMatchType>(Mode)), 18);
		Button->Index = Mode;
		Button->OnMenuClicked.AddUObject(this, &UShoterMainMenuWidget::HandleModeClicked);
		ModeButtons.Add(Button);
		ShoterUI::AddH(ModeRow, ShoterUI::MakeSized(WidgetTree, Button, 0.f, 52.f), FMargin(0.f, 0.f, Mode + 1 < MenuNumModes ? 10.f : 0.f, 0.f), 1.f);
	}
	ShoterUI::AddV(Panel, ModeRow, FMargin(0.f, 0.f, 0.f, 8.f));

	ModeDescriptionText = ShoterUI::MakeLabel(WidgetTree, FText::GetEmpty(), 15, false, ShoterUI::TextMuted());
	ModeDescriptionText->SetAutoWrapText(true);
	ShoterUI::AddV(Panel, ModeDescriptionText, FMargin(0.f, 0.f, 0.f, 18.f));

	// --- Map ---
	ShoterUI::AddV(Panel, MakeSection(LOCTEXT("SectionMap", "MAPA")), FMargin(0.f, 0.f, 0.f, 8.f));

	UHorizontalBox* MapRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	if (Maps.Num() == 0)
	{
		ShoterUI::AddH(MapRow, ShoterUI::MakeLabel(WidgetTree, LOCTEXT("NoMaps", "No hay mapas disponibles."), 17, false, ShoterUI::TextWarning()), FMargin(), 1.f);
	}
	for (int32 MapIndex = 0; MapIndex < Maps.Num(); ++MapIndex)
	{
		UShoterMenuButton* Button = ShoterUI::MakeButton(WidgetTree, UShoterGameInstance::GetMapDisplayName(Maps[MapIndex]), 18);
		Button->Index = MapIndex;
		Button->OnMenuClicked.AddUObject(this, &UShoterMainMenuWidget::HandleMapClicked);
		MapButtons.Add(Button);
		ShoterUI::AddH(MapRow, ShoterUI::MakeSized(WidgetTree, Button, 0.f, 52.f), FMargin(0.f, 0.f, MapIndex + 1 < Maps.Num() ? 10.f : 0.f, 0.f), 1.f);
	}
	ShoterUI::AddV(Panel, MapRow, FMargin(0.f, 0.f, 0.f, 18.f));

	// --- Bots and max players ---
	UHorizontalBox* CountsRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

	BotsColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	ShoterUI::AddV(BotsColumn, MakeSection(LOCTEXT("SectionBots", "BOTS")), FMargin(0.f, 0.f, 0.f, 8.f));
	ShoterUI::AddV(BotsColumn, MakeStepper(MenuStepper_BotsDown, MenuStepper_BotsUp, BotsValueText), FMargin(), false, HAlign_Left);
	ShoterUI::AddH(CountsRow, BotsColumn, FMargin(0.f, 0.f, 24.f, 0.f), 1.f, VAlign_Top);

	UVerticalBox* PlayersColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	ShoterUI::AddV(PlayersColumn, MakeSection(LOCTEXT("SectionMaxPlayers", "M\u00C1XIMO DE JUGADORES")), FMargin(0.f, 0.f, 0.f, 8.f));
	ShoterUI::AddV(PlayersColumn, MakeStepper(MenuStepper_PlayersDown, MenuStepper_PlayersUp, MaxPlayersValueText), FMargin(), false, HAlign_Left);
	ShoterUI::AddH(CountsRow, PlayersColumn, FMargin(), 1.f, VAlign_Top);

	ShoterUI::AddV(Panel, CountsRow, FMargin(0.f, 0.f, 0.f, 16.f));

	// --- LAN ---
	HostLanCheckBox = ShoterUI::MakeCheckBox(WidgetTree, LOCTEXT("HostLan", "Partida LAN"));
	HostLanCheckBox->OnCheckChanged.AddUObject(this, &UShoterMainMenuWidget::HandleLanChanged);
	ShoterUI::AddV(Panel, HostLanCheckBox, FMargin(), false, HAlign_Left);

	ShoterUI::AddV(Panel, ShoterUI::MakeSpacer(WidgetTree), FMargin(), true);

	// --- Create ---
	CreateButton = ShoterUI::MakeButton(WidgetTree, LOCTEXT("Create", "CREAR PARTIDA"), 30, EShoterButtonKind::Primary);
	CreateButton->OnMenuClicked.AddUObject(this, &UShoterMainMenuWidget::HandleCreateClicked);
	ShoterUI::AddV(Panel, ShoterUI::MakeSized(WidgetTree, CreateButton, 0.f, 76.f));

	return Panel;
}

UWidget* UShoterMainMenuWidget::BuildBrowsePanel()
{
	UVerticalBox* Panel = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());

	// Header: title, LAN check box, refresh button.
	UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	ShoterUI::AddH(Header, ShoterUI::MakeLabel(WidgetTree, LOCTEXT("BrowseTitle", "BUSCAR PARTIDAS"), 32, true), FMargin(0.f, 0.f, 16.f, 0.f), 1.f);

	BrowseLanCheckBox = ShoterUI::MakeCheckBox(WidgetTree, LOCTEXT("BrowseLan", "Partida LAN"));
	BrowseLanCheckBox->OnCheckChanged.AddUObject(this, &UShoterMainMenuWidget::HandleLanChanged);
	ShoterUI::AddH(Header, BrowseLanCheckBox, FMargin(0.f, 0.f, 16.f, 0.f));

	RefreshButton = ShoterUI::MakeButton(WidgetTree, LOCTEXT("Refresh", "ACTUALIZAR"), 18, EShoterButtonKind::Primary);
	RefreshButton->OnMenuClicked.AddUObject(this, &UShoterMainMenuWidget::HandleRefreshClicked);
	ShoterUI::AddH(Header, ShoterUI::MakeSized(WidgetTree, RefreshButton, 190.f, 46.f), FMargin());
	ShoterUI::AddV(Panel, Header, FMargin(0.f, 0.f, 0.f, 18.f));

	// Column captions, aligned with the rows.
	ShoterUI::AddV(Panel, UShoterServerRowWidget::MakeHeaderRow(WidgetTree), FMargin(0.f, 0.f, 0.f, 6.f));

	// Scrollable list with the "searching / nothing found" text centered over it.
	ServerListBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());

	ServerListStatusText = ShoterUI::MakeLabel(WidgetTree, FText::GetEmpty(), 22, false, ShoterUI::TextMuted());
	ServerListStatusText->SetJustification(ETextJustify::Center);
	ServerListStatusText->SetVisibility(ESlateVisibility::Collapsed);

	UOverlay* ListArea = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());
	ListArea->AddChildToOverlay(ServerListBox);
	UOverlaySlot* StatusSlot = ListArea->AddChildToOverlay(ServerListStatusText);
	if (StatusSlot)
	{
		StatusSlot->SetHorizontalAlignment(HAlign_Center);
		StatusSlot->SetVerticalAlignment(VAlign_Center);
	}

	ShoterUI::AddV(Panel, ShoterUI::MakePanel(WidgetTree, ListArea, FMargin(6.f), ShoterUI::InsetFill(), FLinearColor(0.f, 0.f, 0.f, 0.f), 4.f), FMargin(), true);
	return Panel;
}

UWidget* UShoterMainMenuWidget::BuildDirectPanel()
{
	UVerticalBox* Panel = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());

	ShoterUI::AddV(Panel, ShoterUI::MakeLabel(WidgetTree, LOCTEXT("DirectTitle", "CONECTAR POR IP"), 32, true), FMargin(0.f, 0.f, 0.f, 18.f));
	ShoterUI::AddV(Panel, ShoterUI::MakeLabel(WidgetTree, LOCTEXT("SectionAddress", "DIRECCI\u00D3N DEL HOST"), 14, true, ShoterUI::Accent()), FMargin(0.f, 0.f, 0.f, 8.f));

	AddressTextBox = ShoterUI::MakeTextBox(WidgetTree, MenuDefaultAddress, LOCTEXT("AddressHint", "192.168.1.10 o 192.168.1.10:7777"));
	AddressTextBox->OnTextCommitted.AddDynamic(this, &UShoterMainMenuWidget::HandleAddressCommitted);
	ShoterUI::AddV(Panel, AddressTextBox, FMargin(0.f, 0.f, 0.f, 20.f));

	UShoterMenuButton* ConnectButton = ShoterUI::MakeButton(WidgetTree, LOCTEXT("Connect", "CONECTAR"), 24, EShoterButtonKind::Primary);
	ConnectButton->OnMenuClicked.AddUObject(this, &UShoterMainMenuWidget::HandleConnectClicked);
	ShoterUI::AddV(Panel, ShoterUI::MakeSized(WidgetTree, ConnectButton, 300.f, 60.f), FMargin(0.f, 0.f, 0.f, 24.f), false, HAlign_Left);

	UTextBlock* Hint = ShoterUI::MakeLabel(WidgetTree, LOCTEXT("DirectHint", "El host tiene que compartir su IP (p\u00FAblica o de Radmin VPN / ZeroTier). Puerto UDP 7777."), 16, false, ShoterUI::TextMuted());
	Hint->SetAutoWrapText(true);
	ShoterUI::AddV(Panel, Hint);

	ShoterUI::AddV(Panel, ShoterUI::MakeSpacer(WidgetTree), FMargin(), true);
	return Panel;
}

UWidget* UShoterMainMenuWidget::BuildOptionsPanel()
{
	SettingsWidget = CreateWidget<UShoterSettingsWidget>(this, UShoterSettingsWidget::StaticClass());
	if (SettingsWidget)
	{
		return SettingsWidget;
	}

	// Keep the switcher indices stable even if the widget could not be created.
	return WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
}
// ---------------------------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------------------------

void UShoterMainMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	UShoterGameInstance* GameInstance = ShoterUI::GetShoterGameInstance(this);
	BoundGameInstance = GameInstance;

	if (GameInstance)
	{
		if (!ServerListUpdatedHandle.IsValid())
		{
			ServerListUpdatedHandle = GameInstance->OnServerListUpdated.AddUObject(this, &UShoterMainMenuWidget::HandleServerListUpdated);
		}
		if (!SessionStatusHandle.IsValid())
		{
			SessionStatusHandle = GameInstance->OnSessionStatus.AddUObject(this, &UShoterMainMenuWidget::HandleSessionStatus);
		}
	}

	if (SettingsWidget && !SettingsClosedHandle.IsValid())
	{
		SettingsClosedHandle = SettingsWidget->OnClosed.AddUObject(this, &UShoterMainMenuWidget::HandleSettingsClosed);
	}

	// Without Steam only LAN / direct IP works: force the LAN check boxes on and lock them.
	const bool bSteam = GameInstance && GameInstance->IsUsingSteam();
	auto SetupLanCheckBox = [bSteam](UShoterMenuCheckBox* CheckBox)
	{
		if (CheckBox)
		{
			if (!bSteam)
			{
				CheckBox->SetChecked(true);
			}
			CheckBox->SetIsEnabled(bSteam);
		}
	};
	SetupLanCheckBox(HostLanCheckBox);
	SetupLanCheckBox(BrowseLanCheckBox);

	RefreshOnlineStatus();

	// A message left by the previous session (connection lost, join failed...).
	if (GameInstance)
	{
		const FText PendingMessage = GameInstance->ConsumePendingMenuMessage();
		if (!PendingMessage.IsEmpty())
		{
			ShowMessage(PendingMessage, ShoterUI::TextWarning());
		}
	}
}

void UShoterMainMenuWidget::NativeDestruct()
{
	if (UShoterGameInstance* GameInstance = BoundGameInstance.Get())
	{
		if (ServerListUpdatedHandle.IsValid())
		{
			GameInstance->OnServerListUpdated.Remove(ServerListUpdatedHandle);
		}
		if (SessionStatusHandle.IsValid())
		{
			GameInstance->OnSessionStatus.Remove(SessionStatusHandle);
		}
	}
	ServerListUpdatedHandle.Reset();
	SessionStatusHandle.Reset();
	BoundGameInstance.Reset();

	if (SettingsWidget && SettingsClosedHandle.IsValid())
	{
		SettingsWidget->OnClosed.Remove(SettingsClosedHandle);
	}
	SettingsClosedHandle.Reset();

	Super::NativeDestruct();
}

// ---------------------------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------------------------

void UShoterMainMenuWidget::ShowPanel(int32 PanelIndex)
{
	ActivePanel = PanelIndex;

	if (PanelSwitcher)
	{
		PanelSwitcher->SetActiveWidgetIndex(PanelIndex);
	}

	for (int32 Nav = 0; Nav < NavButtons.Num(); ++Nav)
	{
		if (NavButtons[Nav])
		{
			NavButtons[Nav]->SetSelected(Nav == PanelIndex);
		}
	}
}

void UShoterMainMenuWidget::HandleNavClicked(UShoterMenuButton* Button)
{
	if (!Button)
	{
		return;
	}

	switch (Button->Index)
	{
	case MenuNav_Play:
		ShowPanel(MenuPanel_Host);
		break;

	case MenuNav_Browse:
		if (ActivePanel != MenuPanel_Browse)
		{
			ShowPanel(MenuPanel_Browse);
			RefreshServers(); // auto-refresh once when the panel opens
		}
		break;

	case MenuNav_Direct:
		ShowPanel(MenuPanel_Direct);
		break;

	case MenuNav_Options:
		if (SettingsWidget)
		{
			SettingsWidget->RefreshFromSettings();
		}
		ShowPanel(MenuPanel_Options);
		break;

	case MenuNav_Quit:
		if (UShoterGameInstance* GameInstance = ShoterUI::GetShoterGameInstance(this))
		{
			GameInstance->QuitGame();
		}
		break;

	default:
		break;
	}
}

void UShoterMainMenuWidget::HandleSettingsClosed()
{
	ShowPanel(MenuPanel_Host);
}

// ---------------------------------------------------------------------------------------------
// Create match
// ---------------------------------------------------------------------------------------------

void UShoterMainMenuWidget::HandleModeClicked(UShoterMenuButton* Button)
{
	if (Button)
	{
		SelectedMode = static_cast<EShoterMatchType>(FMath::Clamp(Button->Index, 0, MenuNumModes - 1));
		RefreshHostPanel();
	}
}

void UShoterMainMenuWidget::HandleMapClicked(UShoterMenuButton* Button)
{
	if (Button)
	{
		SelectedMapIndex = FMath::Clamp(Button->Index, 0, FMath::Max(0, Maps.Num() - 1));
		RefreshHostPanel();
	}
}

void UShoterMainMenuWidget::HandleStepperClicked(UShoterMenuButton* Button)
{
	if (!Button)
	{
		return;
	}

	switch (Button->Index)
	{
	case MenuStepper_BotsDown:		SelectedBots = FMath::Max(MenuMinBots, SelectedBots - 1); break;
	case MenuStepper_BotsUp:		SelectedBots = FMath::Min(MenuMaxBots, SelectedBots + 1); break;
	case MenuStepper_PlayersDown:	SelectedMaxPlayers = FMath::Max(MenuMinMaxPlayers, SelectedMaxPlayers - 1); break;
	case MenuStepper_PlayersUp:		SelectedMaxPlayers = FMath::Min(MenuMaxMaxPlayers, SelectedMaxPlayers + 1); break;
	default: break;
	}
	RefreshHostPanel();
}

void UShoterMainMenuWidget::RefreshHostPanel()
{
	for (int32 Mode = 0; Mode < ModeButtons.Num(); ++Mode)
	{
		if (ModeButtons[Mode])
		{
			ModeButtons[Mode]->SetSelected(Mode == static_cast<int32>(SelectedMode));
		}
	}

	for (int32 MapIndex = 0; MapIndex < MapButtons.Num(); ++MapIndex)
	{
		if (MapButtons[MapIndex])
		{
			MapButtons[MapIndex]->SetSelected(MapIndex == SelectedMapIndex);
		}
	}

	if (ModeDescriptionText)
	{
		ModeDescriptionText->SetText(MenuModeDescription(SelectedMode));
	}
	if (BotsValueText)
	{
		BotsValueText->SetText(FText::FromString(FString::FromInt(SelectedBots)));
	}
	if (MaxPlayersValueText)
	{
		MaxPlayersValueText->SetText(FText::FromString(FString::FromInt(SelectedMaxPlayers)));
	}

	// Zombies is co-op: no bots.
	if (BotsColumn)
	{
		BotsColumn->SetVisibility(SelectedMode == EShoterMatchType::Zombies ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	}
	if (CreateButton)
	{
		CreateButton->SetIsEnabled(Maps.Num() > 0);
	}
}

void UShoterMainMenuWidget::HandleCreateClicked(UShoterMenuButton* /*Button*/)
{
	UShoterGameInstance* GameInstance = ShoterUI::GetShoterGameInstance(this);
	if (!GameInstance)
	{
		return;
	}

	if (Maps.Num() == 0)
	{
		ShowMessage(LOCTEXT("NoMapsMessage", "No hay mapas disponibles para crear la partida."), ShoterUI::TextWarning());
		return;
	}

	const FName MapName = Maps[FMath::Clamp(SelectedMapIndex, 0, Maps.Num() - 1)];
	const int32 NumBots = (SelectedMode == EShoterMatchType::Zombies) ? 0 : SelectedBots;

	ShowMessage(LOCTEXT("Creating", "Creando partida..."), ShoterUI::Accent());
	GameInstance->HostMatch(SelectedMode, MapName, SelectedMaxPlayers, NumBots, IsLanSelected());
}

bool UShoterMainMenuWidget::IsLanSelected() const
{
	const UShoterGameInstance* GameInstance = ShoterUI::GetShoterGameInstance(this);
	if (GameInstance && !GameInstance->IsUsingSteam())
	{
		return true;
	}

	// The two LAN check boxes are kept in sync.
	return HostLanCheckBox && HostLanCheckBox->IsChecked();
}

void UShoterMainMenuWidget::HandleLanChanged(UShoterMenuCheckBox* CheckBox, bool bChecked)
{
	if (HostLanCheckBox && CheckBox != HostLanCheckBox)
	{
		HostLanCheckBox->SetChecked(bChecked);
	}
	if (BrowseLanCheckBox && CheckBox != BrowseLanCheckBox)
	{
		BrowseLanCheckBox->SetChecked(bChecked);
	}
}

// ---------------------------------------------------------------------------------------------
// Server browser
// ---------------------------------------------------------------------------------------------

void UShoterMainMenuWidget::HandleRefreshClicked(UShoterMenuButton* /*Button*/)
{
	RefreshServers();
}

void UShoterMainMenuWidget::RefreshServers()
{
	UShoterGameInstance* GameInstance = ShoterUI::GetShoterGameInstance(this);
	if (!GameInstance)
	{
		return;
	}

	bLastSearchFailed = false;
	bWaitingForResults = true;
	GameInstance->FindMatches(IsLanSelected());

	// If the game instance did not start a search (or already reported back), there is nothing to wait for.
	if (!GameInstance->IsSearching())
	{
		bWaitingForResults = false;
	}
	RebuildServerList();
}

void UShoterMainMenuWidget::RebuildServerList()
{
	if (!ServerListBox)
	{
		return;
	}

	ServerListBox->ClearChildren();

	const UShoterGameInstance* GameInstance = ShoterUI::GetShoterGameInstance(this);
	const bool bSearching = bWaitingForResults || (GameInstance && GameInstance->IsSearching());

	int32 NumRows = 0;
	if (GameInstance)
	{
		for (const FShoterServerInfo& Info : GameInstance->GetServerList())
		{
			UShoterServerRowWidget* Row = CreateWidget<UShoterServerRowWidget>(this, UShoterServerRowWidget::StaticClass());
			if (!Row)
			{
				continue;
			}

			Row->SetServerInfo(Info, NumRows);
			UPanelSlot* RowSlot = ServerListBox->AddChild(Row);
			if (UScrollBoxSlot* ScrollSlot = Cast<UScrollBoxSlot>(RowSlot))
			{
				ScrollSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
			}
			++NumRows;
		}
	}

	if (ServerListStatusText)
	{
		if (NumRows > 0)
		{
			ServerListStatusText->SetVisibility(ESlateVisibility::Collapsed);
		}
		else
		{
			ServerListStatusText->SetText(bSearching
				? LOCTEXT("Searching", "Buscando...")
				: (bLastSearchFailed ? LOCTEXT("SearchFailed", "No se pudo completar la b\u00FAsqueda") : LOCTEXT("NoServers", "No se encontraron partidas")));
			ServerListStatusText->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
	}

	if (RefreshButton)
	{
		RefreshButton->SetIsEnabled(!(GameInstance && GameInstance->IsSearching()));
	}
}

void UShoterMainMenuWidget::HandleServerListUpdated(bool bSuccess)
{
	bWaitingForResults = false;
	bLastSearchFailed = !bSuccess;
	RebuildServerList();
}

// ---------------------------------------------------------------------------------------------
// Connect by IP
// ---------------------------------------------------------------------------------------------

void UShoterMainMenuWidget::HandleConnectClicked(UShoterMenuButton* /*Button*/)
{
	ConnectToAddress();
}

void UShoterMainMenuWidget::HandleAddressCommitted(const FText& /*Text*/, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter)
	{
		ConnectToAddress();
	}
}

void UShoterMainMenuWidget::ConnectToAddress()
{
	UShoterGameInstance* GameInstance = ShoterUI::GetShoterGameInstance(this);
	if (!GameInstance)
	{
		return;
	}

	const FString Address = AddressTextBox ? AddressTextBox->GetText().ToString().TrimStartAndEnd() : FString();
	if (Address.IsEmpty())
	{
		ShowMessage(LOCTEXT("AddressEmpty", "Escribe la direcci\u00F3n IP del host."), ShoterUI::TextWarning());
		return;
	}

	ShowMessage(FText::Format(LOCTEXT("Connecting", "Conectando a {0}..."), FText::FromString(Address)), ShoterUI::Accent());
	GameInstance->JoinByAddress(Address);
}

// ---------------------------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------------------------

void UShoterMainMenuWidget::HandleSessionStatus(const FText& Message)
{
	ShowMessage(Message, ShoterUI::Accent());
}

void UShoterMainMenuWidget::ShowMessage(const FText& Message, const FLinearColor& Color)
{
	if (MessageText)
	{
		MessageText->SetText(Message);
		MessageText->SetColorAndOpacity(FSlateColor(Color));
	}
}

void UShoterMainMenuWidget::RefreshOnlineStatus()
{
	if (!OnlineStatusText)
	{
		return;
	}

	const UShoterGameInstance* GameInstance = ShoterUI::GetShoterGameInstance(this);
	if (GameInstance && GameInstance->IsUsingSteam())
	{
		OnlineStatusText->SetText(FText::Format(LOCTEXT("SteamConnected", "Steam conectado como {0}"), FText::FromString(GameInstance->GetLocalPlayerName())));
	}
	else
	{
		OnlineStatusText->SetText(LOCTEXT("NoSteam", "Modo LAN / IP (Steam no detectado)"));
	}
}

#undef LOCTEXT_NAMESPACE
