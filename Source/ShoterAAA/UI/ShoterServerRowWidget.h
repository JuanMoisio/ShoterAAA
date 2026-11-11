// One row of the server browser (name, mode, map, players, ping, JOIN button).
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Core/ShoterTypes.h"
#include "ShoterServerRowWidget.generated.h"

class UBorder;
class UShoterMenuButton;
class UTextBlock;
class UWidget;
class UWidgetTree;

/** Column layout shared by the browser header and its rows. */
namespace ShoterServerColumns
{
	inline constexpr float Name = 3.2f;
	inline constexpr float Mode = 2.4f;
	inline constexpr float Map = 2.2f;
	inline constexpr float Players = 1.3f;
	inline constexpr float Ping = 1.0f;
	inline constexpr float JoinWidth = 130.f;
}

UCLASS()
class SHOTERAAA_API UShoterServerRowWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Fills the row. RowNumber only alternates the background shade. */
	void SetServerInfo(const FShoterServerInfo& Info, int32 RowNumber);

	int32 GetSearchResultIndex() const { return SearchResultIndex; }

	/** Column captions row, built in the given tree (the owner adds it above the list). */
	static UWidget* MakeHeaderRow(UWidgetTree* Tree);

protected:
	virtual void NativeOnInitialized() override;

	UFUNCTION()
	void HandleJoinClicked();

	UPROPERTY(Transient)
	TObjectPtr<UBorder> Background;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> NameText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ModeText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> MapText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PlayersText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PingText;

	UPROPERTY(Transient)
	TObjectPtr<UShoterMenuButton> JoinButton;

	/** Index into the game instance's last search results. */
	int32 SearchResultIndex = INDEX_NONE;
};
