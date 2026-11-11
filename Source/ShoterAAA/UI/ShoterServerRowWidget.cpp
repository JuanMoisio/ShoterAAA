#include "UI/ShoterServerRowWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Online/ShoterGameInstance.h"
#include "UI/ShoterUIStyle.h"

#define LOCTEXT_NAMESPACE "ShoterServerRow"

namespace
{
	/** Adds one text column to a row / header with the shared weights. */
	void ServerRowAddColumn(UHorizontalBox* Row, UTextBlock* Text, float Weight, bool bCentered)
	{
		if (bCentered)
		{
			Text->SetJustification(ETextJustify::Center);
		}
		ShoterUI::AddH(Row, Text, FMargin(0.f, 0.f, 10.f, 0.f), Weight);
	}
}

void UShoterServerRowWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (!WidgetTree)
	{
		return;
	}

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

	NameText = ShoterUI::MakeLabel(WidgetTree, FText::GetEmpty(), 19, true);
	ModeText = ShoterUI::MakeLabel(WidgetTree, FText::GetEmpty(), 17, false, ShoterUI::TextMuted());
	MapText = ShoterUI::MakeLabel(WidgetTree, FText::GetEmpty(), 17, false, ShoterUI::TextMuted());
	PlayersText = ShoterUI::MakeLabel(WidgetTree, FText::GetEmpty(), 18, true);
	PingText = ShoterUI::MakeLabel(WidgetTree, FText::GetEmpty(), 17, false, ShoterUI::TextMuted());

	ServerRowAddColumn(Row, NameText, ShoterServerColumns::Name, false);
	ServerRowAddColumn(Row, ModeText, ShoterServerColumns::Mode, false);
	ServerRowAddColumn(Row, MapText, ShoterServerColumns::Map, false);
	ServerRowAddColumn(Row, PlayersText, ShoterServerColumns::Players, true);
	ServerRowAddColumn(Row, PingText, ShoterServerColumns::Ping, true);

	JoinButton = ShoterUI::MakeButton(WidgetTree, LOCTEXT("Join", "UNIRSE"), 17, EShoterButtonKind::Primary);
	JoinButton->OnClicked.AddDynamic(this, &UShoterServerRowWidget::HandleJoinClicked);
	ShoterUI::AddH(Row, ShoterUI::MakeSized(WidgetTree, JoinButton, ShoterServerColumns::JoinWidth, 40.f), FMargin());

	Background = ShoterUI::MakePanel(WidgetTree, Row, FMargin(16.f, 6.f), FLinearColor(0.04f, 0.07f, 0.11f, 0.85f), FLinearColor(0.f, 0.f, 0.f, 0.f), 4.f);
	WidgetTree->RootWidget = Background;
}

void UShoterServerRowWidget::SetServerInfo(const FShoterServerInfo& Info, int32 RowNumber)
{
	SearchResultIndex = Info.SearchResultIndex;

	if (NameText)
	{
		NameText->SetText(FText::FromString(Info.ServerName));
	}
	if (ModeText)
	{
		ModeText->SetText(ShoterMatch::GetDisplayName(Info.MatchType));
	}
	if (MapText)
	{
		MapText->SetText(Info.MapName.IsEmpty() ? FText::FromString(TEXT("-")) : UShoterGameInstance::GetMapDisplayName(FName(*Info.MapName)));
	}

	const bool bFull = Info.MaxPlayers > 0 && Info.CurrentPlayers >= Info.MaxPlayers;
	if (PlayersText)
	{
		PlayersText->SetText(FText::FromString(FString::Printf(TEXT("%d/%d"), Info.CurrentPlayers, Info.MaxPlayers)));
		PlayersText->SetColorAndOpacity(FSlateColor(bFull ? ShoterUI::TextWarning() : ShoterUI::TextWhite()));
	}
	if (PingText)
	{
		PingText->SetText(Info.PingMs > 0 ? FText::FromString(FString::Printf(TEXT("%d ms"), Info.PingMs)) : FText::FromString(TEXT("-")));
	}

	if (Background)
	{
		const FLinearColor Fill = (RowNumber % 2 == 0) ? FLinearColor(0.04f, 0.07f, 0.11f, 0.85f) : FLinearColor(0.02f, 0.04f, 0.07f, 0.85f);
		Background->SetBrush(ShoterUI::MakeBoxBrush(Fill, FLinearColor(0.f, 0.f, 0.f, 0.f), 4.f, 0.f));
	}
	if (JoinButton)
	{
		JoinButton->SetIsEnabled(!bFull && Info.SearchResultIndex != INDEX_NONE);
	}
}

UWidget* UShoterServerRowWidget::MakeHeaderRow(UWidgetTree* Tree)
{
	if (!Tree)
	{
		return nullptr;
	}

	UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

	auto MakeCaption = [Tree](const FText& Text)
	{
		return ShoterUI::MakeLabel(Tree, Text, 13, true, ShoterUI::Accent());
	};

	ServerRowAddColumn(Row, MakeCaption(LOCTEXT("ColServer", "SERVIDOR")), ShoterServerColumns::Name, false);
	ServerRowAddColumn(Row, MakeCaption(LOCTEXT("ColMode", "MODO")), ShoterServerColumns::Mode, false);
	ServerRowAddColumn(Row, MakeCaption(LOCTEXT("ColMap", "MAPA")), ShoterServerColumns::Map, false);
	ServerRowAddColumn(Row, MakeCaption(LOCTEXT("ColPlayers", "JUGADORES")), ShoterServerColumns::Players, true);
	ServerRowAddColumn(Row, MakeCaption(LOCTEXT("ColPing", "PING")), ShoterServerColumns::Ping, true);
	ShoterUI::AddH(Row, ShoterUI::MakeSized(Tree, nullptr, ShoterServerColumns::JoinWidth, 1.f), FMargin());

	// Same horizontal padding as the rows so the columns line up.
	return ShoterUI::MakePanel(Tree, Row, FMargin(16.f, 0.f), FLinearColor(0.f, 0.f, 0.f, 0.f), FLinearColor(0.f, 0.f, 0.f, 0.f), 0.f);
}

void UShoterServerRowWidget::HandleJoinClicked()
{
	if (SearchResultIndex == INDEX_NONE)
	{
		return;
	}

	if (UShoterGameInstance* GameInstance = ShoterUI::GetShoterGameInstance(this))
	{
		GameInstance->JoinMatch(SearchResultIndex);
	}
}

#undef LOCTEXT_NAMESPACE
