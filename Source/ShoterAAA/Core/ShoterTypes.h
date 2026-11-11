// Shared enums and structs used by gameplay, UI and networking.
#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "ShoterTypes.generated.h"

class APlayerState;

/** Game mode a match is played in. */
UENUM(BlueprintType)
enum class EShoterMatchType : uint8
{
	TeamDeathmatch,
	FreeForAll,
	Zombies
};

/** Zombie variants. Stats also scale with the wave number. */
UENUM(BlueprintType)
enum class EShoterZombieType : uint8
{
	Walker,		// average speed and health
	Runner,		// fast and fragile
	Brute		// big, slow, tanky, hits hard
};

/** Team ids. Free-for-all players use None. */
namespace ShoterTeam
{
	inline constexpr uint8 Blue = 0;
	inline constexpr uint8 Red = 1;
	inline constexpr uint8 None = 255;

	SHOTERAAA_API FLinearColor GetColor(uint8 TeamId);
	SHOTERAAA_API FText GetDisplayName(uint8 TeamId);
}

namespace ShoterMatch
{
	/** Alias used in travel URLs (?game=TDM). Must match GameModeClassAliases in DefaultEngine.ini. */
	SHOTERAAA_API FString GetGameAlias(EShoterMatchType Type);
	SHOTERAAA_API FText GetDisplayName(EShoterMatchType Type);
	SHOTERAAA_API EShoterMatchType FromAlias(const FString& Alias);
}

/** One kill feed line, multicast by the game state. */
USTRUCT(BlueprintType)
struct SHOTERAAA_API FShoterKillFeedEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FString KillerName;

	UPROPERTY(BlueprintReadOnly)
	uint8 KillerTeam = ShoterTeam::None;

	/** Null for zombies or environment kills. */
	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<APlayerState> KillerPlayerState = nullptr;

	UPROPERTY(BlueprintReadOnly)
	FString VictimName;

	UPROPERTY(BlueprintReadOnly)
	uint8 VictimTeam = ShoterTeam::None;

	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<APlayerState> VictimPlayerState = nullptr;

	UPROPERTY(BlueprintReadOnly)
	FString WeaponName;

	UPROPERTY(BlueprintReadOnly)
	bool bHeadshot = false;

	/** World time when the entry arrived on this machine (filled locally, never replicated). */
	UPROPERTY(NotReplicated)
	float LocalTimestamp = 0.f;
};

/** A hit the shooting client detected and asks the server to validate. */
USTRUCT()
struct SHOTERAAA_API FShoterHitReport
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<AActor> HitActor = nullptr;

	UPROPERTY()
	FVector_NetQuantize HitLocation;

	UPROPERTY()
	FVector_NetQuantizeNormal HitNormal;

	UPROPERTY()
	FName BoneName = NAME_None;
};

/** One row of the server browser. */
USTRUCT(BlueprintType)
struct SHOTERAAA_API FShoterServerInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FString ServerName;

	UPROPERTY(BlueprintReadOnly)
	FString MapName;

	UPROPERTY(BlueprintReadOnly)
	EShoterMatchType MatchType = EShoterMatchType::TeamDeathmatch;

	UPROPERTY(BlueprintReadOnly)
	int32 CurrentPlayers = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 MaxPlayers = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 PingMs = 0;

	/** Index into the game instance's last search results; pass it to UShoterGameInstance::JoinMatch. */
	UPROPERTY(BlueprintReadOnly)
	int32 SearchResultIndex = INDEX_NONE;
};

/** Player-facing options, persisted in a SaveGame slot by UShoterGameInstance. */
USTRUCT(BlueprintType)
struct SHOTERAAA_API FShoterUserSettings
{
	GENERATED_BODY()

	/** Used when playing without Steam (Steam uses the persona name). */
	UPROPERTY(BlueprintReadWrite)
	FString PlayerName;

	/** Mouse sensitivity multiplier (0.1 - 5). */
	UPROPERTY(BlueprintReadWrite)
	float MouseSensitivity = 1.0f;

	/** Extra sensitivity multiplier while aiming down sights (0.2 - 1.5). */
	UPROPERTY(BlueprintReadWrite)
	float AimSensitivityScale = 0.7f;

	UPROPERTY(BlueprintReadWrite)
	bool bInvertY = false;

	/** Horizontal field of view in degrees (80 - 115). */
	UPROPERTY(BlueprintReadWrite)
	float FieldOfView = 100.f;

	/** 0 - 1 */
	UPROPERTY(BlueprintReadWrite)
	float MasterVolume = 1.0f;

	/** 0 - 1 */
	UPROPERTY(BlueprintReadWrite)
	float MusicVolume = 0.5f;

	/** Scalability preset: 0 low, 1 medium, 2 high, 3 epic. */
	UPROPERTY(BlueprintReadWrite)
	int32 GraphicsQuality = 2;

	UPROPERTY(BlueprintReadWrite)
	bool bShowFPS = false;
};

/** Local-only HUD feedback: where damage came from. */
USTRUCT()
struct SHOTERAAA_API FShoterDamageIndicator
{
	GENERATED_BODY()

	FVector SourceLocation = FVector::ZeroVector;
	float Time = 0.f;
	float Damage = 0.f;
};

/** Local-only HUD feedback: small center-screen line, e.g. "+100 ELIMINACIÓN". */
USTRUCT()
struct SHOTERAAA_API FShoterNotification
{
	GENERATED_BODY()

	FText Text;
	FLinearColor Color = FLinearColor::White;
	float Time = 0.f;
};

/** Local-only HUD feedback: big centered announcement, e.g. "OLEADA 3". */
USTRUCT()
struct SHOTERAAA_API FShoterAnnouncement
{
	GENERATED_BODY()

	FText Title;
	FText Subtitle;
	FLinearColor Color = FLinearColor::White;
	float StartTime = -1000.f;
	float Duration = 0.f;
};
