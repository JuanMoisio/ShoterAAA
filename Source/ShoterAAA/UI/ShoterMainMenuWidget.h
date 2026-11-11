// Main menu built entirely in C++ (UMG widget tree constructed in code, no widget blueprints).
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Core/ShoterTypes.h"
#include "Types/SlateEnums.h"
#include "ShoterMainMenuWidget.generated.h"

class UEditableTextBox;
class UScrollBox;
class UShoterGameInstance;
class UShoterMenuButton;
class UShoterMenuCheckBox;
class UShoterSettingsWidget;
class UTextBlock;
class UVerticalBox;
class UWidget;
class UWidgetSwitcher;

/**
 * Left column (title + navigation) and, on the right, a panel that switches between:
 * create match, server browser, connect by IP and options.
 */
UCLASS()
class SHOTERAAA_API UShoterMainMenuWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// --- Tree building (NativeOnInitialized) ---
	UWidget* BuildLeftColumn();
	UWidget* BuildHostPanel();
	UWidget* BuildBrowsePanel();
	UWidget* BuildDirectPanel();
	UWidget* BuildOptionsPanel();

	// --- Navigation ---
	void ShowPanel(int32 PanelIndex);
	void HandleNavClicked(UShoterMenuButton* Button);

	// --- Create match ---
	void HandleModeClicked(UShoterMenuButton* Button);
	void HandleMapClicked(UShoterMenuButton* Button);
	void HandleStepperClicked(UShoterMenuButton* Button);
	void HandleCreateClicked(UShoterMenuButton* Button);
	void RefreshHostPanel();

	// --- Server browser ---
	void HandleRefreshClicked(UShoterMenuButton* Button);
	void HandleLanChanged(UShoterMenuCheckBox* CheckBox, bool bChecked);
	void RefreshServers();
	void RebuildServerList();

	// --- Connect by IP ---
	void HandleConnectClicked(UShoterMenuButton* Button);
	void ConnectToAddress();

	UFUNCTION()
	void HandleAddressCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	// --- Options ---
	void HandleSettingsClosed();

	// --- Game instance delegates ---
	void HandleServerListUpdated(bool bSuccess);
	void HandleSessionStatus(const FText& Message);

	// --- Status / messages ---
	void ShowMessage(const FText& Message, const FLinearColor& Color);
	void RefreshOnlineStatus();
	bool IsLanSelected() const;

	// --- Widgets (all kept alive through UPROPERTY) ---
	UPROPERTY(Transient)
	TObjectPtr<UWidgetSwitcher> PanelSwitcher;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UShoterMenuButton>> NavButtons;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UShoterMenuButton>> ModeButtons;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UShoterMenuButton>> MapButtons;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ModeDescriptionText;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> BotsColumn;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> BotsValueText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> MaxPlayersValueText;

	UPROPERTY(Transient)
	TObjectPtr<UShoterMenuCheckBox> HostLanCheckBox;

	UPROPERTY(Transient)
	TObjectPtr<UShoterMenuButton> CreateButton;

	UPROPERTY(Transient)
	TObjectPtr<UShoterMenuCheckBox> BrowseLanCheckBox;

	UPROPERTY(Transient)
	TObjectPtr<UShoterMenuButton> RefreshButton;

	UPROPERTY(Transient)
	TObjectPtr<UScrollBox> ServerListBox;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ServerListStatusText;

	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> AddressTextBox;

	UPROPERTY(Transient)
	TObjectPtr<UShoterSettingsWidget> SettingsWidget;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> OnlineStatusText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> MessageText;

	// --- State ---
	TArray<FName> Maps;
	EShoterMatchType SelectedMode = EShoterMatchType::TeamDeathmatch;
	int32 SelectedMapIndex = 0;
	int32 SelectedBots = 3;
	int32 SelectedMaxPlayers = 8;
	int32 ActivePanel = 0;
	bool bWaitingForResults = false;
	bool bLastSearchFailed = false;

	// --- Native delegate bookkeeping (bound in NativeConstruct, removed in NativeDestruct) ---
	TWeakObjectPtr<UShoterGameInstance> BoundGameInstance;
	FDelegateHandle ServerListUpdatedHandle;
	FDelegateHandle SessionStatusHandle;
	FDelegateHandle SettingsClosedHandle;
};
