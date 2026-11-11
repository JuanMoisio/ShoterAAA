// Canvas-drawn gameplay HUD (crosshair, vitals, ammo, kill feed, scoreboard, match/wave info).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "ShoterHUD.generated.h"

class UFont;
class AShoterPlayerController;
class AShoterCharacter;
class AShoterWeapon;
class AShoterGameState;
class AShoterPlayerState;

/** Horizontal anchor used by the text helpers. */
enum class EShoterHUDAlign : uint8
{
	Left,
	Center,
	Right
};

UCLASS()
class SHOTERAAA_API AShoterHUD : public AHUD
{
	GENERATED_BODY()

public:
	AShoterHUD();

	virtual void DrawHUD() override;

protected:
	// --- Cached font (engine Roboto, falls back to the engine medium font) ---
	UPROPERTY(Transient)
	TObjectPtr<UFont> HUDFont;

	/** Height in pixels of one text line at font scale 1. Measured once; text sizes are expressed in pixels. */
	float FontLineHeight = 16.f;
	bool bFontLoadAttempted = false;
	bool bFontMeasured = false;

	// --- Per-frame context. Raw pointers are only valid inside DrawHUD and are cleared at the end of every frame. ---
	float UIScale = 1.f;
	float ScreenW = 0.f;
	float ScreenH = 0.f;
	float CenterX = 0.f;
	float CenterY = 0.f;
	float Now = 0.f;
	float FrameDelta = 0.f;
	bool bPawnAlive = false;
	uint8 LocalTeam = 255;
	AShoterPlayerController* LocalPC = nullptr;
	AShoterCharacter* LocalPawn = nullptr;
	AShoterWeapon* LocalWeapon = nullptr;
	AShoterGameState* ShoterGS = nullptr;
	AShoterPlayerState* LocalPS = nullptr;

	// --- Persistent animation state ---
	float SmoothedFrameTime = 1.f / 60.f;
	float CrosshairGap = 8.f;
	float SprintFade = 1.f;
	float LastShieldSeen = -1.f;
	float ShieldBreakTime = -100.f;
	float DeathScreenStartTime = -1.f;
	float MatchEndStartTime = -1.f;
	/** Pawn the shield tracking belongs to (a new pawn resets it). */
	TWeakObjectPtr<AActor> TrackedPawn;

	// --- Frame setup ---
	bool BeginFrame();
	void EndFrame();
	bool EnsureFont();

	// --- Primitive helpers. Every coordinate and size is in screen pixels (already scaled), except U(). ---
	float U(float Value) const { return Value * UIScale; }
	float HudStr(const FString& Text, float X, float Y, float Px, const FLinearColor& Color, EShoterHUDAlign Align = EShoterHUDAlign::Left, bool bBold = false, bool bShadow = true);
	float HudStrWidth(const FString& Text, float Px) const;
	FString FitStr(const FString& Text, float Px, float MaxWidth) const;
	void HudRect(float X, float Y, float W, float H, const FLinearColor& Color);
	void HudFrame(float X, float Y, float W, float H, float Thickness, const FLinearColor& Color);
	void HudLine(float X0, float Y0, float X1, float Y1, const FLinearColor& Color, float Thickness);
	void HudArc(float CX, float CY, float Radius, float StartDeg, float EndDeg, int32 Segments, const FLinearColor& Color, float Thickness);
	void HudCircle(float CX, float CY, float Radius, int32 Segments, const FLinearColor& Color, float Thickness);
	void HudPanel(float X, float Y, float W, float H, const FLinearColor& Accent, float Alpha = 1.f);
	void HudBrackets(float X, float Y, float W, float H, float Len, float Thickness, const FLinearColor& Color);
	void HudSegmentBar(float X, float Y, float W, float H, float Fraction, int32 Segments, float Gap, const FLinearColor& Fill, const FLinearColor& Empty);
	void HudEdgeGlow(const FLinearColor& Color, float Intensity, int32 LayerCount, float Thickness);
	void HudStatBox(float X, float Y, float W, float H, const FString& Label, const FString& Value, const FLinearColor& Accent, bool bHighlight);
	FLinearColor GetRelationColor(uint8 Team, const APlayerState* PS) const;

	// --- One helper per HUD element ---
	void DrawCrosshair();
	void DrawSniperScope(float Fade);
	void DrawHitMarkers();
	void DrawLowHealthVignette();
	void DrawDamageIndicators();
	void DrawVitals();
	void DrawZombiePoints();
	void DrawWeaponPanel();
	void DrawSpawnProtection();
	void DrawTopCenter();
	void DrawKillFeed();
	void DrawNotifications();
	void DrawAnnouncement();
	void DrawInteractionPrompt();
	void DrawDeathScreen();
	void DrawScoreboard();
	/** Draws one scoreboard table; MinRows keeps side-by-side team tables the same height. */
	void DrawScoreTable(float X, float Y, float W, const FString& Title, const FString& TitleValue, const FLinearColor& Accent, const TArray<AShoterPlayerState*>& Players, bool bZombies, int32 MinRows);
	void DrawMatchEnd();
	void DrawFPS();
};
