#include "Online/ShoterGameInstance.h"
#include "ShoterAAA.h"
#include "Core/ShoterSaveGame.h"
#include "Player/ShoterPlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "Online/OnlineSessionNames.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Interfaces/OnlineExternalUIInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "AudioDevice.h"

#define LOCTEXT_NAMESPACE "ShoterGameInstance"

namespace ShoterSession
{
	const FName SettingMatchType(TEXT("SHOTER_MODE"));
	const FName SettingServerName(TEXT("SHOTER_NAME"));
	const FName SettingGameId(TEXT("SHOTER_GAME"));
	/** Steam AppID 480 is shared by many test projects: only list sessions carrying this tag. */
	const FString GameIdValue(TEXT("ShoterAAA_v1"));
}

UShoterGameInstance::UShoterGameInstance()
{
}

void UShoterGameInstance::Init()
{
	Super::Init();

	LoadSettings();
	ApplySettings();

	if (GEngine)
	{
		GEngine->OnNetworkFailure().AddUObject(this, &UShoterGameInstance::HandleNetworkFailure);
		GEngine->OnTravelFailure().AddUObject(this, &UShoterGameInstance::HandleTravelFailure);
	}

	if (IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		InviteAcceptedHandle = Sessions->AddOnSessionUserInviteAcceptedDelegate_Handle(
			FOnSessionUserInviteAcceptedDelegate::CreateUObject(this, &UShoterGameInstance::OnSessionUserInviteAccepted));
	}

	UE_LOG(LogShoter, Log, TEXT("Online service: %s"), *GetOnlineServiceName());

#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("ShoterAutoShots")))
	{
		AutoShotsHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UShoterGameInstance::TickAutoShots), 0.f);
	}
#endif
}

bool UShoterGameInstance::TickAutoShots(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World || !World->HasBegunPlay())
	{
		return true;
	}

	// Give shaders and the match time to settle, then one shot every few seconds.
	constexpr float FirstShotTime = 14.f;
	constexpr float ShotInterval = 2.5f;
	constexpr int32 NumShots = 6;

#if WITH_EDITOR
	// Running from the editor build: wait until material shaders are compiled, otherwise shots show placeholder materials.
	if (AutoShotIndex == 0 && GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0)
	{
		AutoShotTimer = FMath::Min(AutoShotTimer, FirstShotTime - 5.f);
	}
#endif

	AutoShotTimer += DeltaTime;
	const float NextShotTime = FirstShotTime + AutoShotIndex * ShotInterval;

	if (AutoShotIndex < NumShots && !bAutoShotPrepared && AutoShotTimer >= NextShotTime - 0.7f)
	{
		bAutoShotPrepared = true;
		if (AShoterPlayerController* PC = Cast<AShoterPlayerController>(GetFirstLocalPlayerController(World)))
		{
			PC->DebugAutoShotStep(AutoShotIndex);
		}
	}
	if (AutoShotIndex < NumShots && AutoShotTimer >= NextShotTime)
	{
		// Captures the next rendered frame including the HUD and widgets.
		const FString ShotPath = FPaths::ProjectSavedDir() / FString::Printf(TEXT("Screenshots/Shoter/AutoShot_%02d.png"), AutoShotIndex + 1);
		FScreenshotRequest::RequestScreenshot(ShotPath, true, false);
		++AutoShotIndex;
		bAutoShotPrepared = false;
	}
	if (AutoShotIndex >= NumShots && AutoShotTimer >= FirstShotTime + NumShots * ShotInterval + 1.5f)
	{
		GEngine->Exec(World, TEXT("quit"));
		return false;
	}
	return true;
}

void UShoterGameInstance::Shutdown()
{
	if (AutoShotsHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(AutoShotsHandle);
	}
	if (GEngine)
	{
		GEngine->OnNetworkFailure().RemoveAll(this);
		GEngine->OnTravelFailure().RemoveAll(this);
	}
	if (IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		Sessions->ClearOnSessionUserInviteAcceptedDelegate_Handle(InviteAcceptedHandle);
		if (Sessions->GetNamedSession(NAME_GameSession))
		{
			Sessions->DestroySession(NAME_GameSession);
		}
	}
	Super::Shutdown();
}

IOnlineSessionPtr UShoterGameInstance::GetSessionInterface() const
{
	IOnlineSubsystem* Subsystem = IOnlineSubsystem::Get();
	return Subsystem ? Subsystem->GetSessionInterface() : nullptr;
}

bool UShoterGameInstance::IsUsingSteam() const
{
	const IOnlineSubsystem* Subsystem = IOnlineSubsystem::Get();
	return Subsystem && Subsystem->GetSubsystemName() == FName(TEXT("STEAM"));
}

FString UShoterGameInstance::GetOnlineServiceName() const
{
	const IOnlineSubsystem* Subsystem = IOnlineSubsystem::Get();
	return Subsystem ? Subsystem->GetSubsystemName().ToString() : FString(TEXT("NONE"));
}

void UShoterGameInstance::Status(const FText& Message)
{
	UE_LOG(LogShoter, Log, TEXT("Session: %s"), *Message.ToString());
	OnSessionStatus.Broadcast(Message);
}

FText UShoterGameInstance::ConsumePendingMenuMessage()
{
	FText Message = PendingMenuMessage;
	PendingMenuMessage = FText::GetEmpty();
	return Message;
}

// ---------------------------------------------------------------------------
// Hosting
// ---------------------------------------------------------------------------

void UShoterGameInstance::HostMatch(EShoterMatchType MatchType, FName MapName, int32 MaxPlayers, int32 NumBots, bool bLAN)
{
	PendingMatchType = MatchType;
	PendingMap = MapName.IsNone() ? GetAvailableMaps()[0] : MapName;
	PendingMaxPlayers = FMath::Clamp(MaxPlayers, 2, 8);
	PendingBots = MatchType == EShoterMatchType::Zombies ? 0 : FMath::Clamp(NumBots, 0, 7);
	bPendingLAN = bLAN || !IsUsingSteam();
	bHostPending = true;

	if (!GetSessionInterface().IsValid())
	{
		TravelToHostedMap();
		return;
	}

	Status(LOCTEXT("Creating", "Creando partida..."));
	DestroySessionThen([this]() { CreateHostSession(); });
}

void UShoterGameInstance::CreateHostSession()
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		TravelToHostedMap();
		return;
	}

	FOnlineSessionSettings Settings;
	Settings.NumPublicConnections = PendingMaxPlayers;
	Settings.NumPrivateConnections = 0;
	Settings.bIsLANMatch = bPendingLAN;
	Settings.bShouldAdvertise = true;
	Settings.bAllowJoinInProgress = true;
	Settings.bAllowInvites = true;
	Settings.bUsesPresence = true;
	Settings.bAllowJoinViaPresence = true;
	Settings.bUseLobbiesIfAvailable = true;
	Settings.Set(SETTING_MAPNAME, PendingMap.ToString(), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Settings.Set(ShoterSession::SettingMatchType, ShoterMatch::GetGameAlias(PendingMatchType), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Settings.Set(ShoterSession::SettingServerName, GetLocalPlayerName(), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Settings.Set(ShoterSession::SettingGameId, ShoterSession::GameIdValue, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	CreateSessionHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(
		FOnCreateSessionCompleteDelegate::CreateUObject(this, &UShoterGameInstance::OnCreateSessionComplete));

	const ULocalPlayer* LocalPlayer = GetFirstGamePlayer();
	const FUniqueNetIdRepl UserId = LocalPlayer ? LocalPlayer->GetPreferredUniqueNetId() : FUniqueNetIdRepl();
	const bool bStarted = UserId.IsValid()
		? Sessions->CreateSession(*UserId, NAME_GameSession, Settings)
		: Sessions->CreateSession(0, NAME_GameSession, Settings);

	if (!bStarted)
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionHandle);
		Status(LOCTEXT("CreateFailedSync", "No se pudo publicar la partida online; se abre igual (tus amigos pueden entrar por IP)."));
		TravelToHostedMap();
	}
}

void UShoterGameInstance::OnCreateSessionComplete(FName SessionName, bool bWasSuccessful)
{
	if (IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionHandle);
	}
	if (!bWasSuccessful)
	{
		Status(LOCTEXT("CreateFailed", "No se pudo publicar la partida online; se abre igual (tus amigos pueden entrar por IP)."));
	}
	TravelToHostedMap();
}

void UShoterGameInstance::TravelToHostedMap()
{
	bHostPending = false;
	const FString Options = FString::Printf(TEXT("listen?game=%s?Bots=%d?MaxPlayers=%d"),
		*ShoterMatch::GetGameAlias(PendingMatchType), PendingBots, PendingMaxPlayers);
	UE_LOG(LogShoter, Log, TEXT("Hosting %s with options %s"), *GetMapPackagePath(PendingMap), *Options);
	UGameplayStatics::OpenLevel(this, FName(*GetMapPackagePath(PendingMap)), true, Options);
}

// ---------------------------------------------------------------------------
// Finding & joining
// ---------------------------------------------------------------------------

void UShoterGameInstance::FindMatches(bool bLAN)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		ServerList.Reset();
		OnServerListUpdated.Broadcast(false);
		return;
	}
	if (bSearching)
	{
		return;
	}

	ServerList.Reset();
	SessionSearch = MakeShared<FOnlineSessionSearch>();
	SessionSearch->bIsLanQuery = bLAN || !IsUsingSteam();
	SessionSearch->MaxSearchResults = 200;
	SessionSearch->PingBucketSize = 50;
	if (!SessionSearch->bIsLanQuery)
	{
		SessionSearch->QuerySettings.Set(SEARCH_LOBBIES, true, EOnlineComparisonOp::Equals);
	}

	FindSessionsHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(
		FOnFindSessionsCompleteDelegate::CreateUObject(this, &UShoterGameInstance::OnFindSessionsComplete));

	bSearching = true;
	Status(LOCTEXT("Searching", "Buscando partidas..."));

	const ULocalPlayer* LocalPlayer = GetFirstGamePlayer();
	const FUniqueNetIdRepl UserId = LocalPlayer ? LocalPlayer->GetPreferredUniqueNetId() : FUniqueNetIdRepl();
	const bool bStarted = UserId.IsValid()
		? Sessions->FindSessions(*UserId, SessionSearch.ToSharedRef())
		: Sessions->FindSessions(0, SessionSearch.ToSharedRef());

	if (!bStarted)
	{
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsHandle);
		bSearching = false;
		Status(LOCTEXT("SearchFailed", "No se pudo iniciar la búsqueda."));
		OnServerListUpdated.Broadcast(false);
	}
}

void UShoterGameInstance::OnFindSessionsComplete(bool bWasSuccessful)
{
	if (IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsHandle);
	}
	bSearching = false;
	ServerList.Reset();

	if (SessionSearch.IsValid())
	{
		for (int32 Index = 0; Index < SessionSearch->SearchResults.Num(); ++Index)
		{
			const FOnlineSessionSearchResult& Result = SessionSearch->SearchResults[Index];
			if (!Result.IsValid())
			{
				continue;
			}
			FString GameId;
			Result.Session.SessionSettings.Get(ShoterSession::SettingGameId, GameId);
			if (GameId != ShoterSession::GameIdValue)
			{
				continue;
			}

			FShoterServerInfo Info;
			Result.Session.SessionSettings.Get(ShoterSession::SettingServerName, Info.ServerName);
			if (Info.ServerName.IsEmpty())
			{
				Info.ServerName = Result.Session.OwningUserName;
			}
			FString MapName;
			Result.Session.SessionSettings.Get(SETTING_MAPNAME, MapName);
			Info.MapName = GetMapDisplayName(FName(*MapName)).ToString();
			FString ModeAlias;
			Result.Session.SessionSettings.Get(ShoterSession::SettingMatchType, ModeAlias);
			Info.MatchType = ShoterMatch::FromAlias(ModeAlias);
			Info.MaxPlayers = Result.Session.SessionSettings.NumPublicConnections;
			Info.CurrentPlayers = FMath::Max(0, Info.MaxPlayers - Result.Session.NumOpenPublicConnections);
			Info.PingMs = Result.PingInMs;
			Info.SearchResultIndex = Index;
			ServerList.Add(Info);
		}
	}

	Status(ServerList.Num() > 0
		? FText::Format(LOCTEXT("Found", "{0} partida(s) encontrada(s)"), ServerList.Num())
		: LOCTEXT("NoneFound", "No se encontraron partidas"));
	OnServerListUpdated.Broadcast(bWasSuccessful);
}

void UShoterGameInstance::JoinMatch(int32 SearchResultIndex)
{
	if (!SessionSearch.IsValid() || !SessionSearch->SearchResults.IsValidIndex(SearchResultIndex))
	{
		Status(LOCTEXT("JoinInvalid", "Esa partida ya no está disponible. Actualizá la lista."));
		return;
	}
	JoinSearchResult(SessionSearch->SearchResults[SearchResultIndex]);
}

void UShoterGameInstance::JoinSearchResult(const FOnlineSessionSearchResult& Result)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		return;
	}

	if (Sessions->GetNamedSession(NAME_GameSession))
	{
		PendingJoinResult = Result;
		DestroySessionThen([this]()
		{
			if (PendingJoinResult.IsSet())
			{
				const FOnlineSessionSearchResult Pending = PendingJoinResult.GetValue();
				PendingJoinResult.Reset();
				JoinSearchResult(Pending);
			}
		});
		return;
	}

	Status(LOCTEXT("Joining", "Uniéndose a la partida..."));
	JoinSessionHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(
		FOnJoinSessionCompleteDelegate::CreateUObject(this, &UShoterGameInstance::OnJoinSessionComplete));

	const ULocalPlayer* LocalPlayer = GetFirstGamePlayer();
	const FUniqueNetIdRepl UserId = LocalPlayer ? LocalPlayer->GetPreferredUniqueNetId() : FUniqueNetIdRepl();
	const bool bStarted = UserId.IsValid()
		? Sessions->JoinSession(*UserId, NAME_GameSession, Result)
		: Sessions->JoinSession(0, NAME_GameSession, Result);

	if (!bStarted)
	{
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionHandle);
		Status(LOCTEXT("JoinStartFailed", "No se pudo unir a la partida."));
	}
}

void UShoterGameInstance::OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (Sessions.IsValid())
	{
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionHandle);
	}

	if (Result != EOnJoinSessionCompleteResult::Success)
	{
		FText Reason;
		switch (Result)
		{
		case EOnJoinSessionCompleteResult::SessionIsFull:			Reason = LOCTEXT("Full", "La partida está llena."); break;
		case EOnJoinSessionCompleteResult::SessionDoesNotExist:		Reason = LOCTEXT("Gone", "La partida ya no existe."); break;
		case EOnJoinSessionCompleteResult::CouldNotRetrieveAddress:	Reason = LOCTEXT("NoAddress", "No se pudo obtener la dirección del host."); break;
		case EOnJoinSessionCompleteResult::AlreadyInSession:		Reason = LOCTEXT("Already", "Ya estás en una partida."); break;
		default:													Reason = LOCTEXT("Unknown", "Error desconocido al unirse."); break;
		}
		Status(Reason);
		return;
	}

	FString ConnectString;
	if (!Sessions.IsValid() || !Sessions->GetResolvedConnectString(SessionName, ConnectString))
	{
		Status(LOCTEXT("NoConnectString", "No se pudo resolver la dirección del host."));
		return;
	}

	if (APlayerController* PC = GetFirstLocalPlayerController())
	{
		Status(LOCTEXT("Travelling", "Conectando..."));
		PC->ClientTravel(ConnectString, TRAVEL_Absolute);
	}
}

void UShoterGameInstance::JoinByAddress(const FString& Address)
{
	const FString Trimmed = Address.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		Status(LOCTEXT("EmptyAddress", "Escribí la IP del host."));
		return;
	}
	if (APlayerController* PC = GetFirstLocalPlayerController())
	{
		Status(FText::Format(LOCTEXT("ConnectingTo", "Conectando a {0}..."), FText::FromString(Trimmed)));
		PC->ClientTravel(Trimmed, TRAVEL_Absolute);
	}
}

void UShoterGameInstance::OnSessionUserInviteAccepted(const bool bWasSuccessful, const int32 ControllerId, FUniqueNetIdPtr UserId, const FOnlineSessionSearchResult& InviteResult)
{
	if (!bWasSuccessful || !InviteResult.IsValid())
	{
		PendingMenuMessage = LOCTEXT("InviteFailed", "No se pudo aceptar la invitación.");
		Status(PendingMenuMessage);
		return;
	}
	UE_LOG(LogShoter, Log, TEXT("Invite accepted, joining %s"), *InviteResult.Session.OwningUserName);
	JoinSearchResult(InviteResult);
}

// ---------------------------------------------------------------------------
// Leaving
// ---------------------------------------------------------------------------

void UShoterGameInstance::DestroySessionThen(TFunction<void()> Next)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid() || !Sessions->GetNamedSession(NAME_GameSession))
	{
		if (Next)
		{
			Next();
		}
		return;
	}

	AfterDestroy = MoveTemp(Next);
	DestroySessionHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(
		FOnDestroySessionCompleteDelegate::CreateUObject(this, &UShoterGameInstance::OnDestroySessionComplete));
	if (!Sessions->DestroySession(NAME_GameSession))
	{
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionHandle);
		TFunction<void()> Callback = MoveTemp(AfterDestroy);
		AfterDestroy = nullptr;
		if (Callback)
		{
			Callback();
		}
	}
}

void UShoterGameInstance::OnDestroySessionComplete(FName SessionName, bool bWasSuccessful)
{
	if (IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionHandle);
	}
	TFunction<void()> Callback = MoveTemp(AfterDestroy);
	AfterDestroy = nullptr;
	if (Callback)
	{
		Callback();
	}
}

void UShoterGameInstance::ReturnToMainMenuWithMessage(const FText& Message)
{
	if (!Message.IsEmpty())
	{
		PendingMenuMessage = Message;
	}
	DestroySessionThen([this]()
	{
		UGameplayStatics::OpenLevel(this, FName(*GetMainMenuMapPath()), true);
	});
}

void UShoterGameInstance::ShowInviteUI()
{
	IOnlineSubsystem* Subsystem = IOnlineSubsystem::Get();
	IOnlineExternalUIPtr ExternalUI = Subsystem ? Subsystem->GetExternalUIInterface() : nullptr;
	if (ExternalUI.IsValid())
	{
		ExternalUI->ShowInviteUI(0, NAME_GameSession);
	}
}

void UShoterGameInstance::QuitGame()
{
	UKismetSystemLibrary::QuitGame(GetWorld(), GetFirstLocalPlayerController(), EQuitPreference::Quit, false);
}

void UShoterGameInstance::HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString)
{
	UE_LOG(LogShoter, Warning, TEXT("Network failure: %s (%s)"), ENetworkFailure::ToString(FailureType), *ErrorString);

	switch (FailureType)
	{
	case ENetworkFailure::ConnectionLost:
	case ENetworkFailure::ConnectionTimeout:
		PendingMenuMessage = LOCTEXT("ConnectionLost", "Se perdió la conexión con el host.");
		break;
	case ENetworkFailure::PendingConnectionFailure:
		PendingMenuMessage = LOCTEXT("ConnectFailed", "No se pudo conectar. Revisá la IP y el puerto 7777, o usen la búsqueda por Steam.");
		break;
	case ENetworkFailure::OutdatedClient:
	case ENetworkFailure::OutdatedServer:
		PendingMenuMessage = LOCTEXT("Outdated", "Tu versión del juego es distinta a la del host. Usen el mismo build.");
		break;
	default:
		PendingMenuMessage = FText::Format(LOCTEXT("NetError", "Error de red: {0}"), FText::FromString(ErrorString));
		break;
	}
	// The engine itself travels back to the default map (main menu); just clean up the session.
	DestroySessionThen(nullptr);
}

void UShoterGameInstance::HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString)
{
	UE_LOG(LogShoter, Warning, TEXT("Travel failure: %s (%s)"), ETravelFailure::ToString(FailureType), *ErrorString);
	PendingMenuMessage = FText::Format(LOCTEXT("TravelError", "No se pudo entrar a la partida: {0}"), FText::FromString(ErrorString));
	DestroySessionThen(nullptr);
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

void UShoterGameInstance::LoadSettings()
{
	if (UShoterSaveGame* Save = Cast<UShoterSaveGame>(UGameplayStatics::LoadGameFromSlot(UShoterSaveGame::SlotName, 0)))
	{
		UserSettings = Save->Settings;
	}
	else
	{
		UserSettings = FShoterUserSettings();
		UserSettings.PlayerName = FString::Printf(TEXT("Jugador%d"), FMath::RandRange(100, 999));
	}
}

void UShoterGameInstance::SaveSettings()
{
	if (UShoterSaveGame* Save = Cast<UShoterSaveGame>(UGameplayStatics::CreateSaveGameObject(UShoterSaveGame::StaticClass())))
	{
		Save->Settings = UserSettings;
		UGameplayStatics::SaveGameToSlot(Save, UShoterSaveGame::SlotName, 0);
	}
}

void UShoterGameInstance::ApplySettings()
{
	if (UGameUserSettings* GameSettings = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		const int32 Quality = FMath::Clamp(UserSettings.GraphicsQuality, 0, 3);
		if (GameSettings->GetOverallScalabilityLevel() != Quality)
		{
			GameSettings->SetOverallScalabilityLevel(Quality);
			GameSettings->ApplySettings(false);
		}
	}

	if (GEngine)
	{
		if (FAudioDeviceHandle AudioDevice = GEngine->GetMainAudioDevice())
		{
			AudioDevice->SetTransientPrimaryVolume(FMath::Clamp(UserSettings.MasterVolume, 0.f, 1.f));
		}
	}
}

void UShoterGameInstance::SetUserSettings(const FShoterUserSettings& NewSettings)
{
	UserSettings = NewSettings;
	UserSettings.PlayerName = UserSettings.PlayerName.Left(20).TrimStartAndEnd();
	ApplySettings();
	SaveSettings();
	OnSettingsChanged.Broadcast();
}

FString UShoterGameInstance::GetLocalPlayerName() const
{
	if (IsUsingSteam())
	{
		if (IOnlineIdentityPtr Identity = IOnlineSubsystem::Get()->GetIdentityInterface())
		{
			const FString Nickname = Identity->GetPlayerNickname(0);
			if (!Nickname.IsEmpty())
			{
				return Nickname;
			}
		}
	}
	if (!UserSettings.PlayerName.IsEmpty())
	{
		return UserSettings.PlayerName;
	}
	return LOCTEXT("DefaultName", "Jugador").ToString();
}

// ---------------------------------------------------------------------------
// Maps
// ---------------------------------------------------------------------------

TArray<FName> UShoterGameInstance::GetAvailableMaps()
{
	return { FName(TEXT("Arena_Neon")) };
}

FText UShoterGameInstance::GetMapDisplayName(FName MapName)
{
	if (MapName == FName(TEXT("Arena_Neon")))
	{
		return LOCTEXT("MapArenaNeon", "Arena Neón");
	}
	return FText::FromName(MapName);
}

FString UShoterGameInstance::GetMapPackagePath(FName MapName)
{
	return FString::Printf(TEXT("/Game/Shoter/Maps/%s"), *MapName.ToString());
}

FString UShoterGameInstance::GetMainMenuMapPath()
{
	return TEXT("/Game/Shoter/Maps/MainMenu");
}

#undef LOCTEXT_NAMESPACE
