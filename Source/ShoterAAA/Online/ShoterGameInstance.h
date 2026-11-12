// Sessions (Steam lobbies or LAN), direct IP, invites, network errors and persisted user settings.
#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Engine/EngineBaseTypes.h"
#include "Containers/Ticker.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "Core/ShoterTypes.h"
#include "ShoterGameInstance.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FShoterServerListUpdatedSignature, bool /*bSuccess*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FShoterSessionStatusSignature, const FText& /*Message*/);
DECLARE_MULTICAST_DELEGATE(FShoterSettingsChangedSignature);

UCLASS(Config = Game)
class SHOTERAAA_API UShoterGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	UShoterGameInstance();

	virtual void Init() override;
	virtual void Shutdown() override;

	// --- Hosting / joining ---

	/** Creates a session (Steam lobby, or LAN without Steam) and opens the map as a listen server. */
	void HostMatch(EShoterMatchType MatchType, FName MapName, int32 MaxPlayers, int32 NumBots, bool bLAN);

	/** Searches Steam lobbies (or LAN). Results arrive through OnServerListUpdated. */
	void FindMatches(bool bLAN);

	bool IsSearching() const { return bSearching; }
	const TArray<FShoterServerInfo>& GetServerList() const { return ServerList; }

	/** Joins a result from the last search (FShoterServerInfo::SearchResultIndex). */
	void JoinMatch(int32 SearchResultIndex);

	/** Direct connection: "1.2.3.4" or "1.2.3.4:7777". */
	void JoinByAddress(const FString& Address);

	/** Leaves the match (destroying the session) and loads the main menu, optionally showing a message there. */
	void ReturnToMainMenuWithMessage(const FText& Message = FText::GetEmpty());

	/** Opens the Steam overlay invite dialog. No-op without Steam. */
	void ShowInviteUI();

	void QuitGame();

	bool IsUsingSteam() const;
	/** "Steam", "NULL" (LAN/IP)... */
	FString GetOnlineServiceName() const;

	/** Message for the main menu (connection lost, join failed...). Returns it once, then clears it. */
	FText ConsumePendingMenuMessage();

	FShoterServerListUpdatedSignature OnServerListUpdated;
	FShoterSessionStatusSignature OnSessionStatus;

	// --- Settings ---
	const FShoterUserSettings& GetUserSettings() const { return UserSettings; }

	/** Applies (graphics, audio), saves and broadcasts OnSettingsChanged. */
	void SetUserSettings(const FShoterUserSettings& NewSettings);

	FShoterSettingsChangedSignature OnSettingsChanged;

	/** Applies graphics quality and master volume from the current settings. */
	void ApplySettings();

	/** Steam persona name, or the name from the settings, or "Jugador". */
	FString GetLocalPlayerName() const;

	/** Development only: with -ShoterAutoShots, takes a series of screenshots and quits (Tools/Build/Screenshots.ps1). */
	bool TickAutoShots(float DeltaTime);

	// --- Maps ---
	static TArray<FName> GetAvailableMaps();
	static FText GetMapDisplayName(FName MapName);
	static FString GetMapPackagePath(FName MapName);
	static FString GetMainMenuMapPath();

protected:
	UPROPERTY(Transient)
	FShoterUserSettings UserSettings;

	TSharedPtr<FOnlineSessionSearch> SessionSearch;
	TArray<FShoterServerInfo> ServerList;
	bool bSearching = false;
	FText PendingMenuMessage;

	// Pending host request (waiting for an old session to be destroyed / the new one to be created)
	bool bHostPending = false;
	EShoterMatchType PendingMatchType = EShoterMatchType::TeamDeathmatch;
	FName PendingMap;
	int32 PendingMaxPlayers = 8;
	int32 PendingBots = 0;
	bool bPendingLAN = false;

	// Pending join (waiting for an old session to be destroyed)
	TOptional<FOnlineSessionSearchResult> PendingJoinResult;

	FDelegateHandle CreateSessionHandle;
	FDelegateHandle FindSessionsHandle;
	FDelegateHandle JoinSessionHandle;
	FDelegateHandle DestroySessionHandle;
	FDelegateHandle InviteAcceptedHandle;

	FTSTicker::FDelegateHandle AutoShotsHandle;
	int32 AutoShotIndex = 0;
	float AutoShotTimer = 0.f;
	bool bAutoShotPrepared = false;
	bool bBurstStarted = false;
	float BurstStartTime = 0.f;
	float BurstEndTime = 0.f;
	float LastBurstShotTime = 0.f;

	IOnlineSessionPtr GetSessionInterface() const;
	void CreateHostSession();
	void TravelToHostedMap();
	void JoinSearchResult(const FOnlineSessionSearchResult& Result);
	void DestroySessionThen(TFunction<void()> Next);
	TFunction<void()> AfterDestroy;

	void OnCreateSessionComplete(FName SessionName, bool bWasSuccessful);
	void OnFindSessionsComplete(bool bWasSuccessful);
	void OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
	void OnDestroySessionComplete(FName SessionName, bool bWasSuccessful);
	void OnSessionUserInviteAccepted(const bool bWasSuccessful, const int32 ControllerId, FUniqueNetIdPtr UserId, const FOnlineSessionSearchResult& InviteResult);
	void HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString);

	void LoadSettings();
	void SaveSettings();
	void Status(const FText& Message);
};
