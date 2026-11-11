// Canvas-drawn gameplay HUD. One helper per element; everything is scaled by Canvas->ClipY / 1080.
#include "UI/ShoterHUD.h"

#include "Core/ShoterTypes.h"
#include "Game/ShoterGameState.h"
#include "Online/ShoterGameInstance.h"
#include "Player/ShoterCharacter.h"
#include "Player/ShoterPlayerController.h"
#include "Player/ShoterPlayerState.h"
#include "Weapons/ShoterWeapon.h"

#include "Camera/PlayerCameraManager.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "Misc/App.h"

// Defensive: unity builds concatenate .cpp files, so make sure no earlier file leaked its namespace.
#undef LOCTEXT_NAMESPACE
#define LOCTEXT_NAMESPACE "ShoterHUD"

// Everything in this anonymous namespace is prefixed "Hud" so it cannot collide with helpers from other .cpp files in a unity build.
namespace
{
	// Palette (clean sci-fi: dark glass panels, cyan accent, red for danger).
	const FLinearColor HudColPanel(0.02f, 0.03f, 0.05f, 0.75f);
	const FLinearColor HudColCyan(0.10f, 0.85f, 1.00f, 1.0f);
	const FLinearColor HudColRed(1.00f, 0.25f, 0.20f, 1.0f);
	const FLinearColor HudColWhite(1.0f, 1.0f, 1.0f, 1.0f);
	const FLinearColor HudColOrange(1.00f, 0.45f, 0.10f, 1.0f);
	const FLinearColor HudColYellow(1.00f, 0.85f, 0.15f, 1.0f);
	const FLinearColor HudColGreen(0.20f, 1.00f, 0.65f, 1.0f);
	const FLinearColor HudColFriend(0.25f, 0.88f, 1.00f, 1.0f);
	const FLinearColor HudColSelf(0.65f, 1.00f, 0.95f, 1.0f);

	FORCEINLINE float HudSat(float V)
	{
		return FMath::Clamp(V, 0.f, 1.f);
	}

	FORCEINLINE float HudEaseOut(float T)
	{
		const float Inv = 1.f - HudSat(T);
		return 1.f - Inv * Inv;
	}

	/** Same color with an absolute alpha. */
	FORCEINLINE FLinearColor HudWithAlpha(const FLinearColor& C, float A)
	{
		return FLinearColor(C.R, C.G, C.B, A);
	}

	/** Same color with its alpha multiplied. */
	FORCEINLINE FLinearColor HudMulAlpha(const FLinearColor& C, float A)
	{
		return FLinearColor(C.R, C.G, C.B, C.A * A);
	}

	FORCEINLINE FLinearColor HudLerpColor(const FLinearColor& A, const FLinearColor& B, float T)
	{
		return FLinearColor(
			FMath::Lerp(A.R, B.R, T),
			FMath::Lerp(A.G, B.G, T),
			FMath::Lerp(A.B, B.B, T),
			FMath::Lerp(A.A, B.A, T));
	}

	/** "m:ss". */
	FString HudFormatClock(int32 TotalSeconds)
	{
		TotalSeconds = FMath::Max(0, TotalSeconds);
		return FString::Printf(TEXT("%d:%02d"), TotalSeconds / 60, TotalSeconds % 60);
	}

	/** Formats a pattern such as "OLEADA {0}" with a plain (ungrouped) integer. */
	FString HudFormatInt(const FText& Pattern, int32 Value)
	{
		return FText::Format(Pattern, FText::AsNumber(Value, &FNumberFormattingOptions::DefaultNoGrouping())).ToString();
	}

	// Layout constants, in 1080p pixels.
	constexpr float HudKillFeedLifetime = 6.f;
	constexpr float HudKillFeedFadeTime = 1.f;
	constexpr int32 HudKillFeedMaxEntries = 5;
	constexpr float HudDamageIndicatorLifetime = 1.2f;
	constexpr float HudNotificationLifetime = 2.5f;
	constexpr float HudHitMarkerLifetime = 0.25f;
	constexpr float HudKillMarkerLifetime = 0.4f;
	constexpr float HudLowHealthFraction = 0.35f;
}

AShoterHUD::AShoterHUD()
{
}

// ---------------------------------------------------------------------------------------------
// Frame setup
// ---------------------------------------------------------------------------------------------

bool AShoterHUD::EnsureFont()
{
	if (!HUDFont && !bFontLoadAttempted)
	{
		bFontLoadAttempted = true;
		HUDFont = LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto"));
		if (!HUDFont && GEngine)
		{
			HUDFont = GEngine->GetMediumFont();
		}
	}

	if (!HUDFont)
	{
		return false;
	}

	if (!bFontMeasured && Canvas)
	{
		// Measure the native line height once so every text size can be requested in pixels.
		float W = 0.f;
		float H = 0.f;
		Canvas->TextSize(HUDFont.Get(), TEXT("Ag"), W, H, 1.f, 1.f);
		if (H > 1.f)
		{
			FontLineHeight = H;
			bFontMeasured = true;
		}
		// Otherwise keep the 16 px default and measure again next frame (font cache may not be ready yet).
	}
	return true;
}

bool AShoterHUD::BeginFrame()
{
	UWorld* World = GetWorld();
	LocalPC = Cast<AShoterPlayerController>(GetOwningPlayerController());
	if (!Canvas || !World || !LocalPC)
	{
		return false;
	}

	ScreenW = Canvas->ClipX;
	ScreenH = Canvas->ClipY;
	if (ScreenW < 32.f || ScreenH < 32.f)
	{
		return false;
	}

	UIScale = FMath::Max(0.25f, ScreenH / 1080.f);
	CenterX = ScreenW * 0.5f;
	CenterY = ScreenH * 0.5f;
	Now = static_cast<float>(World->GetTimeSeconds());

	const float RawDelta = static_cast<float>(FApp::GetDeltaTime());
	FrameDelta = FMath::Clamp(RawDelta, 0.f, 0.25f);
	if (RawDelta > 0.f)
	{
		SmoothedFrameTime = FMath::Lerp(SmoothedFrameTime, FMath::Min(RawDelta, 0.5f), 0.06f);
	}

	LocalPawn = Cast<AShoterCharacter>(GetOwningPawn());
	bPawnAlive = LocalPawn && LocalPawn->IsAlive();
	LocalWeapon = LocalPawn ? LocalPawn->GetCurrentWeapon() : nullptr;
	ShoterGS = World->GetGameState<AShoterGameState>();
	LocalPS = LocalPC->GetPlayerState<AShoterPlayerState>();
	LocalTeam = LocalPS ? LocalPS->GetTeam() : ShoterTeam::None;

	return EnsureFont();
}

void AShoterHUD::EndFrame()
{
	LocalPC = nullptr;
	LocalPawn = nullptr;
	LocalWeapon = nullptr;
	ShoterGS = nullptr;
	LocalPS = nullptr;
}

void AShoterHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!BeginFrame())
	{
		EndFrame();
		return;
	}

	// While the pause widget is up only the scoreboard / match end banner stay (the widget draws on top of them).
	if (LocalPC->IsPauseMenuOpen())
	{
		DrawMatchEnd();
		DrawScoreboard();
		EndFrame();
		return;
	}

	DrawLowHealthVignette();
	DrawDamageIndicators();
	DrawCrosshair();
	DrawHitMarkers();
	DrawVitals();
	DrawZombiePoints();
	DrawWeaponPanel();
	DrawSpawnProtection();
	DrawTopCenter();
	DrawKillFeed();
	DrawNotifications();
	DrawInteractionPrompt();
	DrawAnnouncement();
	DrawDeathScreen();
	DrawMatchEnd();
	DrawScoreboard();
	DrawFPS();

	EndFrame();
}

// ---------------------------------------------------------------------------------------------
// Primitive helpers
// ---------------------------------------------------------------------------------------------

float AShoterHUD::HudStrWidth(const FString& Text, float Px) const
{
	if (!Canvas || !HUDFont || Text.IsEmpty() || FontLineHeight <= 0.f)
	{
		return 0.f;
	}
	const float Sc = Px / FontLineHeight;
	float W = 0.f;
	float H = 0.f;
	Canvas->TextSize(HUDFont.Get(), Text, W, H, Sc, Sc);
	return W;
}

float AShoterHUD::HudStr(const FString& Text, float X, float Y, float Px, const FLinearColor& Color, EShoterHUDAlign Align, bool bBold, bool bShadow)
{
	if (!Canvas || !HUDFont || Text.IsEmpty() || Color.A <= 0.003f || FontLineHeight <= 0.f)
	{
		return 0.f;
	}

	const float Sc = Px / FontLineHeight;
	float W = 0.f;
	float H = 0.f;
	Canvas->TextSize(HUDFont.Get(), Text, W, H, Sc, Sc);

	float DrawX = X;
	if (Align == EShoterHUDAlign::Center)
	{
		DrawX = X - W * 0.5f;
	}
	else if (Align == EShoterHUDAlign::Right)
	{
		DrawX = X - W;
	}
	DrawX = FMath::RoundToFloat(DrawX);
	const float DrawY = FMath::RoundToFloat(Y);
	const float ShadowOffset = FMath::Max(1.f, FMath::RoundToFloat(Px * 0.05f));

	FCanvasTextItem Item(FVector2D(DrawX, DrawY), FText::FromString(Text), HUDFont.Get(), Color);
	Item.Scale = FVector2D(Sc, Sc);
	if (bShadow)
	{
		Item.EnableShadow(FLinearColor(0.f, 0.f, 0.f, 0.8f * Color.A), FVector2D(ShadowOffset, ShadowOffset));
	}
	Canvas->DrawItem(Item);

	if (bBold)
	{
		// Faux bold: the same text again, shifted by a fraction of the glyph height.
		FCanvasTextItem Bold(FVector2D(DrawX + FMath::Max(1.f, FMath::RoundToFloat(Px * 0.045f)), DrawY), FText::FromString(Text), HUDFont.Get(), Color);
		Bold.Scale = FVector2D(Sc, Sc);
		Canvas->DrawItem(Bold);
	}
	return W;
}

FString AShoterHUD::FitStr(const FString& Text, float Px, float MaxWidth) const
{
	if (MaxWidth <= 0.f || HudStrWidth(Text, Px) <= MaxWidth)
	{
		return Text;
	}
	FString Out = Text;
	while (Out.Len() > 1)
	{
		Out = Out.Left(Out.Len() - 1);
		const FString Candidate = Out + TEXT("...");
		if (HudStrWidth(Candidate, Px) <= MaxWidth)
		{
			return Candidate;
		}
	}
	return Out;
}

void AShoterHUD::HudRect(float X, float Y, float W, float H, const FLinearColor& Color)
{
	if (!Canvas || Color.A <= 0.002f || W <= 0.f || H <= 0.f)
	{
		return;
	}
	DrawRect(Color, X, Y, W, H);
}

void AShoterHUD::HudFrame(float X, float Y, float W, float H, float Thickness, const FLinearColor& Color)
{
	const float T = FMath::Max(1.f, Thickness);
	HudRect(X, Y, W, T, Color);
	HudRect(X, Y + H - T, W, T, Color);
	HudRect(X, Y + T, T, H - 2.f * T, Color);
	HudRect(X + W - T, Y + T, T, H - 2.f * T, Color);
}

void AShoterHUD::HudLine(float X0, float Y0, float X1, float Y1, const FLinearColor& Color, float Thickness)
{
	if (!Canvas || Color.A <= 0.002f)
	{
		return;
	}
	DrawLine(X0, Y0, X1, Y1, Color, FMath::Max(1.f, Thickness));
}

void AShoterHUD::HudArc(float CX, float CY, float Radius, float StartDeg, float EndDeg, int32 Segments, const FLinearColor& Color, float Thickness)
{
	if (Segments < 1 || Color.A <= 0.002f)
	{
		return;
	}
	// Screen angles: 0 = up, positive = clockwise.
	float PrevX = 0.f;
	float PrevY = 0.f;
	for (int32 Index = 0; Index <= Segments; ++Index)
	{
		const float Alpha = static_cast<float>(Index) / static_cast<float>(Segments);
		const float Rad = FMath::DegreesToRadians(FMath::Lerp(StartDeg, EndDeg, Alpha));
		const float PX = CX + Radius * FMath::Sin(Rad);
		const float PY = CY - Radius * FMath::Cos(Rad);
		if (Index > 0)
		{
			HudLine(PrevX, PrevY, PX, PY, Color, Thickness);
		}
		PrevX = PX;
		PrevY = PY;
	}
}

void AShoterHUD::HudCircle(float CX, float CY, float Radius, int32 Segments, const FLinearColor& Color, float Thickness)
{
	HudArc(CX, CY, Radius, 0.f, 360.f, Segments, Color, Thickness);
}

void AShoterHUD::HudBrackets(float X, float Y, float W, float H, float Len, float Thickness, const FLinearColor& Color)
{
	const float T = FMath::Max(1.f, Thickness);
	// Top-left
	HudRect(X, Y, Len, T, Color);
	HudRect(X, Y, T, Len, Color);
	// Top-right
	HudRect(X + W - Len, Y, Len, T, Color);
	HudRect(X + W - T, Y, T, Len, Color);
	// Bottom-left
	HudRect(X, Y + H - T, Len, T, Color);
	HudRect(X, Y + H - Len, T, Len, Color);
	// Bottom-right
	HudRect(X + W - Len, Y + H - T, Len, T, Color);
	HudRect(X + W - T, Y + H - Len, T, Len, Color);
}

void AShoterHUD::HudPanel(float X, float Y, float W, float H, const FLinearColor& Accent, float Alpha)
{
	HudRect(X, Y, W, H, HudMulAlpha(HudColPanel, Alpha));
	HudRect(X, Y, W, FMath::Max(1.f, U(2.f)), HudWithAlpha(Accent, 0.9f * Alpha));
	HudFrame(X, Y, W, H, FMath::Max(1.f, U(1.f)), HudWithAlpha(Accent, 0.16f * Alpha));
	HudBrackets(X, Y, W, H, U(9.f), FMath::Max(1.f, U(1.5f)), HudWithAlpha(Accent, 0.65f * Alpha));
}

void AShoterHUD::HudSegmentBar(float X, float Y, float W, float H, float Fraction, int32 Segments, float Gap, const FLinearColor& Fill, const FLinearColor& Empty)
{
	if (Segments < 1)
	{
		return;
	}
	const float SegW = (W - Gap * static_cast<float>(Segments - 1)) / static_cast<float>(Segments);
	if (SegW <= 0.5f)
	{
		return;
	}
	const float Clamped = HudSat(Fraction);
	for (int32 Index = 0; Index < Segments; ++Index)
	{
		const float SegX = FMath::RoundToFloat(X + (SegW + Gap) * static_cast<float>(Index));
		const float Filled = HudSat(Clamped * static_cast<float>(Segments) - static_cast<float>(Index));
		HudRect(SegX, Y, SegW, H, Empty);
		if (Filled > 0.f)
		{
			HudRect(SegX, Y, SegW * Filled, H, Fill);
		}
	}
}

void AShoterHUD::HudEdgeGlow(const FLinearColor& Color, float Intensity, int32 LayerCount, float Thickness)
{
	if (Intensity <= 0.003f || LayerCount < 1 || Color.A <= 0.f)
	{
		return;
	}
	// Per-layer alpha chosen so the outermost pixels end up at (Intensity * Color.A) after all layers stack.
	const float Target = HudSat(Intensity * Color.A);
	const float LayerAlpha = 1.f - FMath::Pow(1.f - Target, 1.f / static_cast<float>(LayerCount));
	const FLinearColor LayerColor = HudWithAlpha(Color, LayerAlpha);
	for (int32 Index = 0; Index < LayerCount; ++Index)
	{
		const float T = Thickness * static_cast<float>(Index + 1) / static_cast<float>(LayerCount);
		HudRect(0.f, 0.f, ScreenW, T, LayerColor);
		HudRect(0.f, ScreenH - T, ScreenW, T, LayerColor);
		HudRect(0.f, T, T, ScreenH - 2.f * T, LayerColor);
		HudRect(ScreenW - T, T, T, ScreenH - 2.f * T, LayerColor);
	}
}

void AShoterHUD::HudStatBox(float X, float Y, float W, float H, const FString& Label, const FString& Value, const FLinearColor& Accent, bool bHighlight)
{
	HudPanel(X, Y, W, H, Accent);
	if (bHighlight)
	{
		HudFrame(X, Y, W, H, FMath::Max(1.f, U(2.f)), HudWithAlpha(Accent, 0.9f));
	}
	HudStr(Label, X + W * 0.5f, Y + U(3.f), U(15.f), HudWithAlpha(Accent, 0.9f), EShoterHUDAlign::Center, true);
	HudStr(Value, X + W * 0.5f, Y + U(19.f), U(36.f), HudColWhite, EShoterHUDAlign::Center, true);
}

FLinearColor AShoterHUD::GetRelationColor(uint8 Team, const APlayerState* PS) const
{
	if (PS && LocalPS && PS == LocalPS)
	{
		return HudColSelf;
	}
	if (ShoterGS && ShoterGS->IsZombiesMode())
	{
		// Co-op: every real player is a friend, everything else (zombies, environment) is hostile.
		return PS ? HudColFriend : HudColRed;
	}
	if (ShoterGS && ShoterGS->IsTeamMode())
	{
		return (Team != ShoterTeam::None && Team == LocalTeam) ? HudColFriend : HudColRed;
	}
	// Free-for-all: everyone except the local player is an enemy.
	return HudColRed;
}

// ---------------------------------------------------------------------------------------------
// 1. Crosshair, sniper scope
// ---------------------------------------------------------------------------------------------

void AShoterHUD::DrawCrosshair()
{
	// Smooth fade while sprinting (updated even when nothing is drawn so it never pops).
	const bool bSprinting = LocalPawn && bPawnAlive && LocalPawn->IsSprinting();
	SprintFade = FMath::FInterpTo(SprintFade, bSprinting ? 0.f : 1.f, FrameDelta, 12.f);

	if (!LocalPawn || !bPawnAlive)
	{
		return;
	}

	const float AimAlpha = HudSat(LocalPawn->GetAimAlpha());
	if (LocalWeapon && LocalWeapon->GetStats().bHasScope && AimAlpha > 0.9f)
	{
		DrawSniperScope(HudSat((AimAlpha - 0.9f) / 0.08f));
		return;
	}

	// Gap follows the weapon spread: gap_px = tan(spread) / tan(FOV / 2) * (ClipX / 2).
	float SpreadDeg = 1.5f;
	if (LocalWeapon)
	{
		SpreadDeg = LocalWeapon->GetCurrentSpread();
	}
	float FovDeg = 90.f;
	if (PlayerOwner && PlayerOwner->PlayerCameraManager)
	{
		FovDeg = PlayerOwner->PlayerCameraManager->GetFOVAngle();
	}
	FovDeg = FMath::Clamp(FovDeg, 20.f, 150.f);
	const float TanHalfFov = FMath::Tan(FMath::DegreesToRadians(FovDeg * 0.5f));
	const float TanSpread = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(SpreadDeg, 0.f, 45.f)));
	const float TargetGap = FMath::Clamp((TanSpread / FMath::Max(0.05f, TanHalfFov)) * (ScreenW * 0.5f), U(3.f), ScreenH * 0.25f);
	CrosshairGap = FMath::FInterpTo(CrosshairGap, TargetGap, FrameDelta, 22.f);

	float Fade = SprintFade;
	if (LocalWeapon && LocalWeapon->IsEquipping())
	{
		Fade *= 0.35f;
	}
	else if (LocalWeapon && LocalWeapon->IsReloading())
	{
		Fade *= 0.7f;
	}

	const float LineAlpha = (1.f - AimAlpha) * Fade;
	const float DotAlpha = (0.55f + 0.45f * AimAlpha) * Fade;

	const float CX = FMath::RoundToFloat(CenterX);
	const float CY = FMath::RoundToFloat(CenterY);
	const float Thick = FMath::Max(1.f, FMath::RoundToFloat(U(2.f)));
	const float Len = FMath::RoundToFloat(U(11.f));
	const float Gap = FMath::RoundToFloat(CrosshairGap);
	const float Half = FMath::FloorToFloat(Thick * 0.5f);
	const float Outline = FMath::Max(1.f, FMath::RoundToFloat(U(1.f)));

	if (LineAlpha > 0.01f)
	{
		const FLinearColor Shadow(0.f, 0.f, 0.f, 0.55f * LineAlpha);
		const FLinearColor Line(1.f, 1.f, 1.f, 0.95f * LineAlpha);

		// Left, right, top, bottom arms (shadow first, then the arm).
		const float ArmX[4] = { CX - Gap - Len, CX + Gap, CX - Half, CX - Half };
		const float ArmY[4] = { CY - Half, CY - Half, CY - Gap - Len, CY + Gap };
		const float ArmW[4] = { Len, Len, Thick, Thick };
		const float ArmH[4] = { Thick, Thick, Len, Len };
		for (int32 Arm = 0; Arm < 4; ++Arm)
		{
			HudRect(ArmX[Arm] - Outline, ArmY[Arm] - Outline, ArmW[Arm] + 2.f * Outline, ArmH[Arm] + 2.f * Outline, Shadow);
		}
		for (int32 Arm = 0; Arm < 4; ++Arm)
		{
			HudRect(ArmX[Arm], ArmY[Arm], ArmW[Arm], ArmH[Arm], Line);
		}
	}

	if (DotAlpha > 0.01f)
	{
		const float Dot = FMath::Max(2.f, FMath::RoundToFloat(U(2.5f)));
		const float DotHalf = FMath::FloorToFloat(Dot * 0.5f);
		HudRect(CX - DotHalf - Outline, CY - DotHalf - Outline, Dot + 2.f * Outline, Dot + 2.f * Outline, FLinearColor(0.f, 0.f, 0.f, 0.55f * DotAlpha));
		HudRect(CX - DotHalf, CY - DotHalf, Dot, Dot, FLinearColor(0.85f, 1.f, 1.f, DotAlpha));
	}
}

void AShoterHUD::DrawSniperScope(float Fade)
{
	if (Fade <= 0.01f)
	{
		return;
	}

	const float Radius = FMath::Min(ScreenH * 0.47f, ScreenW * 0.5f);
	const FLinearColor Black(0.f, 0.f, 0.f, Fade);
	const float Top = CenterY - Radius;
	const float Bottom = CenterY + Radius;

	// Black everywhere outside the circle: full-width bars above/below, then thin bands left and right of the chord.
	HudRect(0.f, 0.f, ScreenW, Top, Black);
	HudRect(0.f, Bottom, ScreenW, ScreenH - Bottom, Black);
	const float Step = FMath::Max(2.f, U(3.f));
	for (float Y = Top; Y < Bottom; Y += Step)
	{
		const float BandH = FMath::Min(Step, Bottom - Y);
		const float DY = (Y + BandH * 0.5f) - CenterY;
		const float Chord = FMath::Sqrt(FMath::Max(0.f, Radius * Radius - DY * DY));
		HudRect(0.f, Y, CenterX - Chord, BandH, Black);
		HudRect(CenterX + Chord, Y, ScreenW - (CenterX + Chord), BandH, Black);
	}

	// A black ring hides the stair-stepped edge, then the cyan tint ring sits on top of it.
	HudCircle(CenterX, CenterY, Radius + U(3.f), 96, Black, U(7.f));
	HudCircle(CenterX, CenterY, Radius, 96, FLinearColor(HudColCyan.R, HudColCyan.G, HudColCyan.B, 0.6f * Fade), U(2.5f));
	HudCircle(CenterX, CenterY, Radius - U(14.f), 96, FLinearColor(HudColCyan.R, HudColCyan.G, HudColCyan.B, 0.14f * Fade), U(1.f));
	for (int32 Ring = 0; Ring < 3; ++Ring)
	{
		// Soft darkening toward the lens border.
		HudCircle(CenterX, CenterY, Radius - U(5.f + 9.f * static_cast<float>(Ring)), 72, FLinearColor(0.f, 0.f, 0.f, (0.24f - 0.07f * static_cast<float>(Ring)) * Fade), U(9.f));
	}

	// Thin crosshair lines (dark outline + light core so they read on any background) with range ticks.
	const float Thick = FMath::Max(1.f, FMath::RoundToFloat(U(1.5f)));
	const float Gap = U(16.f);
	const FLinearColor Outline(0.f, 0.f, 0.f, 0.5f * Fade);
	const FLinearColor Core(0.92f, 1.f, 1.f, 0.85f * Fade);
	const float HalfT = FMath::FloorToFloat(Thick * 0.5f);
	const float CX = FMath::RoundToFloat(CenterX);
	const float CY = FMath::RoundToFloat(CenterY);
	const float Reach = Radius - U(4.f);

	HudRect(CX - Reach, CY - HalfT - 1.f, Reach - Gap, Thick + 2.f, Outline);
	HudRect(CX + Gap, CY - HalfT - 1.f, Reach - Gap, Thick + 2.f, Outline);
	HudRect(CX - HalfT - 1.f, CY - Reach, Thick + 2.f, Reach - Gap, Outline);
	HudRect(CX - HalfT - 1.f, CY + Gap, Thick + 2.f, Reach - Gap, Outline);
	HudRect(CX - Reach, CY - HalfT, Reach - Gap, Thick, Core);
	HudRect(CX + Gap, CY - HalfT, Reach - Gap, Thick, Core);
	HudRect(CX - HalfT, CY - Reach, Thick, Reach - Gap, Core);
	HudRect(CX - HalfT, CY + Gap, Thick, Reach - Gap, Core);

	const float TickSpacing = U(48.f);
	const float TickHalf = U(5.f);
	for (float Offset = Gap + TickSpacing; Offset < Reach - U(20.f); Offset += TickSpacing)
	{
		HudRect(CX - Offset - HalfT, CY - TickHalf, Thick, TickHalf * 2.f, Core);
		HudRect(CX + Offset - HalfT, CY - TickHalf, Thick, TickHalf * 2.f, Core);
		HudRect(CX - TickHalf, CY - Offset - HalfT, TickHalf * 2.f, Thick, Core);
		HudRect(CX - TickHalf, CY + Offset - HalfT, TickHalf * 2.f, Thick, Core);
	}

	const float Dot = FMath::Max(2.f, FMath::RoundToFloat(U(3.f)));
	HudRect(CX - Dot * 0.5f, CY - Dot * 0.5f, Dot, Dot, FLinearColor(HudColRed.R, HudColRed.G, HudColRed.B, 0.95f * Fade));
}

// ---------------------------------------------------------------------------------------------
// 2. Hit markers
// ---------------------------------------------------------------------------------------------

void AShoterHUD::DrawHitMarkers()
{
	if (!LocalPC)
	{
		return;
	}

	// An X made of four diagonal ticks around the center. Inner/Len are radial distances in pixels.
	auto DrawX = [this](float Inner, float Len, float Thick, const FLinearColor& Color)
	{
		constexpr float Diag = 0.70710678f;
		const FLinearColor Shadow(0.f, 0.f, 0.f, 0.5f * Color.A);
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			const FLinearColor& C = (Pass == 0) ? Shadow : Color;
			const float T = (Pass == 0) ? Thick + U(2.f) : Thick;
			for (int32 Quadrant = 0; Quadrant < 4; ++Quadrant)
			{
				const float SX = (Quadrant & 1) ? 1.f : -1.f;
				const float SY = (Quadrant & 2) ? 1.f : -1.f;
				HudLine(CenterX + SX * Inner * Diag, CenterY + SY * Inner * Diag,
					CenterX + SX * (Inner + Len) * Diag, CenterY + SY * (Inner + Len) * Diag, C, T);
			}
		}
	};

	const float HitAge = Now - LocalPC->GetLastHitMarkerTime();
	if (HitAge >= 0.f && HitAge < HudHitMarkerLifetime)
	{
		const float T = HitAge / HudHitMarkerLifetime;
		const float Alpha = 1.f - T * T;
		const FLinearColor Color = LocalPC->WasLastHitHeadshot()
			? FLinearColor(1.f, 0.35f, 0.08f, Alpha)
			: FLinearColor(1.f, 1.f, 1.f, Alpha);
		DrawX(U(7.f) + U(5.f) * HudEaseOut(T), U(10.f), U(2.2f), Color);
	}

	const float KillAge = Now - LocalPC->GetLastKillMarkerTime();
	if (KillAge >= 0.f && KillAge < HudKillMarkerLifetime)
	{
		const float T = KillAge / HudKillMarkerLifetime;
		const float Alpha = 1.f - T * T;
		DrawX(U(10.f) + U(10.f) * HudEaseOut(T), U(17.f), U(3.2f), FLinearColor(HudColRed.R, HudColRed.G, HudColRed.B, Alpha));
	}
}

// ---------------------------------------------------------------------------------------------
// Screen-edge effects: low-health vignette, damage flash and shield break flash
// ---------------------------------------------------------------------------------------------

void AShoterHUD::DrawLowHealthVignette()
{
	// A new pawn (respawn) resets the shield tracking so a fresh spawn never flashes.
	if (TrackedPawn.Get() != LocalPawn)
	{
		TrackedPawn = LocalPawn;
		LastShieldSeen = -1.f;
		ShieldBreakTime = -100.f;
	}

	if (!LocalPawn || !bPawnAlive)
	{
		return;
	}

	const float Shield = LocalPawn->GetShield();
	if (LastShieldSeen > 0.5f && Shield <= 0.5f)
	{
		ShieldBreakTime = Now;
	}
	LastShieldSeen = Shield;

	const float MaxHealth = FMath::Max(1.f, LocalPawn->GetMaxHealth());
	const float HealthFrac = HudSat(LocalPawn->GetHealth() / MaxHealth);

	// Pulsing red vignette under 35 % health (stacked translucent rectangles along the edges).
	if (HealthFrac < HudLowHealthFraction)
	{
		const float Severity = 1.f - HealthFrac / HudLowHealthFraction;
		const float Pulse = 0.5f + 0.5f * FMath::Sin(Now * (5.f + 5.f * Severity));
		const float Intensity = (0.35f + 0.65f * Severity) * (0.55f + 0.45f * Pulse);
		HudEdgeGlow(FLinearColor(0.85f, 0.02f, 0.02f, 1.f), Intensity * 0.75f, 8, ScreenH * 0.17f);
	}

	// Brief red flash right after taking damage.
	const float SinceDamage = Now - LocalPawn->GetLastDamageTime();
	if (SinceDamage >= 0.f && SinceDamage < 0.35f)
	{
		HudEdgeGlow(HudColRed, (1.f - SinceDamage / 0.35f) * 0.22f, 5, ScreenH * 0.10f);
	}

	// Cyan flash when the shield breaks.
	const float SinceBreak = Now - ShieldBreakTime;
	if (SinceBreak >= 0.f && SinceBreak < 0.5f)
	{
		HudEdgeGlow(HudColCyan, (1.f - SinceBreak / 0.5f) * 0.38f, 5, ScreenH * 0.09f);
	}
}

// ---------------------------------------------------------------------------------------------
// 7. Damage indicators
// ---------------------------------------------------------------------------------------------

void AShoterHUD::DrawDamageIndicators()
{
	if (!LocalPC || !PlayerOwner || !PlayerOwner->PlayerCameraManager)
	{
		return;
	}
	const TArray<FShoterDamageIndicator>& Indicators = LocalPC->GetDamageIndicators();
	if (Indicators.Num() == 0)
	{
		return;
	}

	const FVector CamLoc = PlayerOwner->PlayerCameraManager->GetCameraLocation();
	const float CamYaw = static_cast<float>(PlayerOwner->PlayerCameraManager->GetCameraRotation().Yaw);
	const float Radius = U(235.f);
	float UndirectedAlpha = 0.f;
	int32 Drawn = 0;

	for (int32 Index = Indicators.Num() - 1; Index >= 0 && Drawn < 8; --Index)
	{
		const FShoterDamageIndicator& Indicator = Indicators[Index];
		const float Age = Now - Indicator.Time;
		if (Age < 0.f || Age > HudDamageIndicatorLifetime)
		{
			continue;
		}
		++Drawn;

		const float Life = 1.f - Age / HudDamageIndicatorLifetime;
		const float Alpha = HudSat(Life * 1.6f) * (0.4f + 0.6f * Life);

		const FVector ToSource = Indicator.SourceLocation - CamLoc;
		if (Indicator.SourceLocation.IsNearlyZero() || (ToSource.X * ToSource.X + ToSource.Y * ToSource.Y) < 25.0)
		{
			// No usable direction (fall damage, self damage...): fall back to a plain edge flash.
			UndirectedAlpha = FMath::Max(UndirectedAlpha, Alpha);
			continue;
		}

		const float SourceYaw = FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(ToSource.Y), static_cast<float>(ToSource.X)));
		const float Rel = FMath::FindDeltaAngleDegrees(CamYaw, SourceYaw);

		const float Severity = HudSat(Indicator.Damage / 40.f);
		const float HalfArc = FMath::Lerp(11.f, 22.f, Severity);
		const float Thick = U(6.f) * (0.8f + 0.5f * Severity);

		// Soft glow arc, solid arc and an arrow head pointing outward, toward the attacker.
		HudArc(CenterX, CenterY, Radius + U(2.f), Rel - HalfArc - 4.f, Rel + HalfArc + 4.f, 8, FLinearColor(1.f, 0.1f, 0.05f, 0.18f * Alpha), Thick * 2.6f);
		HudArc(CenterX, CenterY, Radius, Rel - HalfArc, Rel + HalfArc, 8, FLinearColor(1.f, 0.2f, 0.15f, 0.9f * Alpha), Thick);

		const float Rad = FMath::DegreesToRadians(Rel);
		const float DirX = FMath::Sin(Rad);
		const float DirY = -FMath::Cos(Rad);
		const float PerpX = -DirY;
		const float PerpY = DirX;
		const float TipR = Radius + U(32.f);
		const float BaseR = Radius + U(6.f);
		const float Wing = U(15.f);
		const float TipX = CenterX + DirX * TipR;
		const float TipY = CenterY + DirY * TipR;
		const FLinearColor Arrow(1.f, 0.25f, 0.18f, Alpha);
		HudLine(CenterX + DirX * BaseR - PerpX * Wing, CenterY + DirY * BaseR - PerpY * Wing, TipX, TipY, Arrow, Thick * 0.85f);
		HudLine(CenterX + DirX * BaseR + PerpX * Wing, CenterY + DirY * BaseR + PerpY * Wing, TipX, TipY, Arrow, Thick * 0.85f);
	}

	if (UndirectedAlpha > 0.f)
	{
		HudEdgeGlow(HudColRed, UndirectedAlpha * 0.2f, 4, ScreenH * 0.08f);
	}
}

// ---------------------------------------------------------------------------------------------
// 3. Vitals (bottom-left): shield above health, plus zombies points above them
// ---------------------------------------------------------------------------------------------

void AShoterHUD::DrawVitals()
{
	if (!LocalPawn || !bPawnAlive)
	{
		return;
	}

	const float Health = LocalPawn->GetHealth();
	const float MaxHealth = FMath::Max(1.f, LocalPawn->GetMaxHealth());
	const float Shield = LocalPawn->GetShield();
	const float MaxShield = FMath::Max(1.f, LocalPawn->GetMaxShield());
	const float HealthFrac = HudSat(Health / MaxHealth);
	const float ShieldFrac = HudSat(Shield / MaxShield);
	const bool bShieldEmpty = Shield <= 0.5f;

	const float PW = U(430.f);
	const float PH = U(118.f);
	const float PX = U(40.f);
	const float PY = ScreenH - U(40.f) - PH;
	const float Pad = U(16.f);
	const float InnerW = PW - 2.f * Pad;
	const float BarX = PX + Pad;

	HudPanel(PX, PY, PW, PH, HudColCyan);

	// --- Shield: 10 segments, cyan ---
	const FLinearColor ShieldColor = bShieldEmpty ? HudColRed : HudColCyan;
	HudStr(LOCTEXT("ShieldLabel", "ESCUDO").ToString(), BarX, PY + U(11.f), U(15.f), HudWithAlpha(ShieldColor, 0.85f), EShoterHUDAlign::Left, true);
	HudStr(FString::FromInt(FMath::CeilToInt(Shield)), PX + PW - Pad, PY + U(7.f), U(22.f), ShieldColor, EShoterHUDAlign::Right, true);

	const float ShieldBarY = PY + U(36.f);
	const float ShieldBarH = U(12.f);
	HudSegmentBar(BarX, ShieldBarY, InnerW, ShieldBarH, ShieldFrac, 10, U(3.f), HudWithAlpha(HudColCyan, 0.95f), FLinearColor(1.f, 1.f, 1.f, 0.10f));

	// Shield flash: blinks fast right after it breaks, then settles into a slow warning pulse while it stays at 0.
	const float SinceBreak = Now - ShieldBreakTime;
	if (bShieldEmpty)
	{
		const float Pulse = (SinceBreak >= 0.f && SinceBreak < 1.5f)
			? 0.5f + 0.5f * FMath::Sin(Now * 22.f)
			: 0.5f + 0.5f * FMath::Sin(Now * 4.f);
		const float Strength = (SinceBreak >= 0.f && SinceBreak < 1.5f) ? 1.f : 0.55f;
		HudRect(BarX, ShieldBarY, InnerW, ShieldBarH, HudWithAlpha(HudColRed, (0.10f + 0.20f * Pulse) * Strength));
		HudFrame(BarX - U(3.f), ShieldBarY - U(3.f), InnerW + U(6.f), ShieldBarH + U(6.f), FMath::Max(1.f, U(1.5f)), HudWithAlpha(HudColRed, (0.25f + 0.65f * Pulse) * Strength));
	}
	if (SinceBreak >= 0.f && SinceBreak < 0.4f)
	{
		HudRect(BarX, ShieldBarY, InnerW, ShieldBarH, FLinearColor(1.f, 1.f, 1.f, (1.f - SinceBreak / 0.4f) * 0.8f));
	}

	// --- Health: white, turning red under 35 % ---
	const bool bLowHealth = HealthFrac < HudLowHealthFraction;
	const FLinearColor HealthColor = bLowHealth ? HudLerpColor(HudColRed, HudColWhite, HealthFrac / HudLowHealthFraction) : HudColWhite;
	const float Blink = bLowHealth ? 0.7f + 0.3f * FMath::Sin(Now * 10.f) : 1.f;
	HudStr(LOCTEXT("HealthLabel", "SALUD").ToString(), BarX, PY + U(66.f), U(15.f), HudWithAlpha(HealthColor, 0.8f), EShoterHUDAlign::Left, true);
	HudStr(FString::FromInt(FMath::CeilToInt(Health)), PX + PW - Pad, PY + U(55.f), U(32.f), HudWithAlpha(HealthColor, Blink), EShoterHUDAlign::Right, true);

	const float HealthBarY = PY + U(92.f);
	const float HealthBarH = U(16.f);
	const float FillW = InnerW * HealthFrac;
	HudRect(BarX, HealthBarY, InnerW, HealthBarH, FLinearColor(1.f, 1.f, 1.f, 0.10f));
	HudRect(BarX, HealthBarY, FillW, HealthBarH, HudWithAlpha(HealthColor, 0.95f * Blink));
	HudRect(BarX, HealthBarY, FillW, HealthBarH * 0.35f, FLinearColor(1.f, 1.f, 1.f, 0.22f));
	for (int32 Tick = 1; Tick < 4; ++Tick)
	{
		HudRect(FMath::RoundToFloat(BarX + InnerW * 0.25f * static_cast<float>(Tick)), HealthBarY, FMath::Max(1.f, U(1.f)), HealthBarH, FLinearColor(0.f, 0.f, 0.f, 0.45f));
	}
	HudFrame(BarX, HealthBarY, InnerW, HealthBarH, FMath::Max(1.f, U(1.f)), FLinearColor(1.f, 1.f, 1.f, 0.16f));

	const float SinceDamage = Now - LocalPawn->GetLastDamageTime();
	if (SinceDamage >= 0.f && SinceDamage < 0.25f)
	{
		HudRect(BarX, HealthBarY, FillW, HealthBarH, FLinearColor(1.f, 1.f, 1.f, (1.f - SinceDamage / 0.25f) * 0.6f));
	}
}

void AShoterHUD::DrawZombiePoints()
{
	if (!ShoterGS || !ShoterGS->IsZombiesMode() || !LocalPS)
	{
		return;
	}

	const float VitalsTop = ScreenH - U(40.f) - U(118.f);
	const float X = U(44.f);
	HudStr(LOCTEXT("PointsLabel", "PUNTOS").ToString(), X, VitalsTop - U(84.f), U(17.f), HudWithAlpha(HudColCyan, 0.8f), EShoterHUDAlign::Left, true);
	HudStr(FString::FromInt(LocalPS->GetZombiePoints()), X - U(2.f), VitalsTop - U(68.f), U(58.f), HudColCyan, EShoterHUDAlign::Left, true);
}

// ---------------------------------------------------------------------------------------------
// 4. Weapon panel (bottom-right)
// ---------------------------------------------------------------------------------------------

void AShoterHUD::DrawWeaponPanel()
{
	if (!LocalPawn || !bPawnAlive)
	{
		return;
	}

	const float PW = U(380.f);
	const float PH = U(150.f);
	const float PX = ScreenW - U(40.f) - PW;
	const float PY = ScreenH - U(40.f) - PH;
	const float Pad = U(16.f);
	const float RightX = PX + PW - Pad;

	// --- Weapon slots "1 2 3" above the panel, current one highlighted (inventory order) ---
	{
		const TArray<TObjectPtr<AShoterWeapon>>& Inventory = LocalPawn->GetInventory();
		const int32 SlotCount = FMath::Clamp(FMath::Max(Inventory.Num(), LocalPawn->GetMaxWeapons()), 1, 6);
		const float SlotW = U(40.f);
		const float SlotH = U(28.f);
		const float SlotGap = U(6.f);
		const float SlotY = PY - SlotH - U(8.f);
		const float SlotX0 = PX + PW - (SlotW * static_cast<float>(SlotCount) + SlotGap * static_cast<float>(SlotCount - 1));
		for (int32 Slot = 0; Slot < SlotCount; ++Slot)
		{
			const AShoterWeapon* SlotWeapon = Inventory.IsValidIndex(Slot) ? Inventory[Slot].Get() : nullptr;
			const bool bCurrent = SlotWeapon && SlotWeapon == LocalWeapon;
			const float SlotX = SlotX0 + (SlotW + SlotGap) * static_cast<float>(Slot);
			const FString Number = FString::FromInt(Slot + 1);
			if (bCurrent)
			{
				HudRect(SlotX, SlotY, SlotW, SlotH, HudWithAlpha(HudColCyan, 0.92f));
				HudStr(Number, SlotX + SlotW * 0.5f, SlotY + U(3.f), U(22.f), FLinearColor(0.02f, 0.05f, 0.08f, 1.f), EShoterHUDAlign::Center, true, false);
			}
			else
			{
				HudRect(SlotX, SlotY, SlotW, SlotH, HudColPanel);
				HudFrame(SlotX, SlotY, SlotW, SlotH, FMath::Max(1.f, U(1.f)), FLinearColor(1.f, 1.f, 1.f, SlotWeapon ? 0.3f : 0.12f));
				HudStr(Number, SlotX + SlotW * 0.5f, SlotY + U(3.f), U(22.f), FLinearColor(1.f, 1.f, 1.f, SlotWeapon ? 0.85f : 0.25f), EShoterHUDAlign::Center, true);
			}
			if (SlotWeapon)
			{
				const FLinearColor Energy = SlotWeapon->GetStats().EnergyColor;
				HudRect(SlotX + U(6.f), SlotY + SlotH - U(3.f), SlotW - U(12.f), FMath::Max(1.f, U(2.f)), HudWithAlpha(Energy, bCurrent ? 1.f : 0.65f));
			}
		}
	}

	const FLinearColor Accent = LocalWeapon ? HudWithAlpha(LocalWeapon->GetStats().EnergyColor, 1.f) : HudColCyan;
	HudPanel(PX, PY, PW, PH, Accent);

	if (LocalWeapon)
	{
		const FShoterWeaponStats& Stats = LocalWeapon->GetStats();
		const int32 Clip = LocalWeapon->GetAmmoInClip();
		const int32 Reserve = LocalWeapon->GetReserveAmmo();
		const int32 MagSize = FMath::Max(1, Stats.MagSize);
		const bool bReloading = LocalWeapon->IsReloading();
		const bool bClipLow = static_cast<float>(Clip) < 0.25f * static_cast<float>(MagSize);

		// Name
		const float NameAlpha = LocalWeapon->IsEquipping() ? 0.5f : 1.f;
		HudRect(PX + Pad, PY + U(14.f), U(4.f), U(18.f), HudWithAlpha(Accent, 0.9f));
		HudStr(LocalWeapon->GetDisplayName().ToString().ToUpper(), RightX, PY + U(10.f), U(24.f), HudWithAlpha(HudColWhite, NameAlpha), EShoterHUDAlign::Right, true);

		// Big clip / small reserve
		const float ReserveW = HudStr(FString::FromInt(Reserve), RightX, PY + U(72.f), U(34.f), FLinearColor(1.f, 1.f, 1.f, 0.65f), EShoterHUDAlign::Right);
		const FString Slash = TEXT("/");
		const float SlashW = HudStrWidth(Slash, U(40.f));
		const float SlashX = RightX - ReserveW - U(8.f);
		HudStr(Slash, SlashX, PY + U(68.f), U(40.f), FLinearColor(1.f, 1.f, 1.f, 0.35f), EShoterHUDAlign::Right);

		FLinearColor ClipColor = HudColWhite;
		if (bClipLow)
		{
			ClipColor = HudColRed;
			if (Clip <= 0)
			{
				ClipColor.A = 0.55f + 0.45f * (0.5f + 0.5f * FMath::Sin(Now * 12.f));
			}
		}
		if (bReloading)
		{
			ClipColor.A *= 0.4f;
		}
		HudStr(FString::FromInt(Clip), SlashX - SlashW - U(8.f), PY + U(38.f), U(78.f), ClipColor, EShoterHUDAlign::Right, true);

		// Status text on the left of the numbers
		const float StatusX = PX + Pad;
		const float StatusY = PY + U(58.f);
		if (bReloading)
		{
			const float Pulse = 0.65f + 0.35f * FMath::Sin(Now * 9.f);
			HudStr(LOCTEXT("Reloading", "RECARGANDO").ToString(), StatusX, StatusY, U(20.f), HudWithAlpha(HudColCyan, Pulse), EShoterHUDAlign::Left, true);
		}
		else if (Clip <= 0 && Reserve <= 0)
		{
			HudStr(LOCTEXT("NoAmmo", "SIN MUNICI\u00D3N").ToString(), StatusX, StatusY, U(20.f), HudColRed, EShoterHUDAlign::Left, true);
		}
		else if (Clip <= 0)
		{
			const float Pulse = 0.55f + 0.45f * FMath::Sin(Now * 12.f);
			HudStr(LOCTEXT("ReloadHint", "RECARGAR").ToString(), StatusX, StatusY, U(20.f), HudWithAlpha(HudColRed, Pulse), EShoterHUDAlign::Left, true);
		}
		else if (bClipLow && Reserve > 0)
		{
			HudStr(LOCTEXT("LowAmmo", "MUNICI\u00D3N BAJA").ToString(), StatusX, StatusY, U(18.f), HudWithAlpha(HudColOrange, 0.95f), EShoterHUDAlign::Left, true);
		}

		// Reload progress along the bottom edge
		if (bReloading)
		{
			const float Progress = HudSat(LocalWeapon->GetReloadProgress());
			const float BarY = PY + PH - U(9.f);
			const float BarH = FMath::Max(2.f, U(4.f));
			HudRect(PX + Pad, BarY, PW - 2.f * Pad, BarH, FLinearColor(1.f, 1.f, 1.f, 0.12f));
			HudRect(PX + Pad, BarY, (PW - 2.f * Pad) * Progress, BarH, HudWithAlpha(HudColCyan, 0.95f));
		}
	}
	else
	{
		HudStr(LOCTEXT("NoWeapon", "SIN ARMA").ToString(), RightX, PY + U(10.f), U(24.f), FLinearColor(1.f, 1.f, 1.f, 0.5f), EShoterHUDAlign::Right, true);
	}

	// Grenade pips
	{
		const int32 MaxGrenades = FMath::Clamp(LocalPawn->GetMaxGrenades(), 0, 8);
		const int32 Grenades = FMath::Clamp(LocalPawn->GetGrenades(), 0, MaxGrenades);
		const float RowY = PY + U(122.f);
		const float LabelW = HudStr(LOCTEXT("GrenadesLabel", "GRANADAS").ToString(), PX + Pad, RowY - U(1.f), U(15.f), FLinearColor(1.f, 1.f, 1.f, 0.6f), EShoterHUDAlign::Left, true);
		const float PipW = U(14.f);
		const float PipH = U(14.f);
		const float PipGap = U(5.f);
		float PipX = PX + Pad + LabelW + U(12.f);
		for (int32 Pip = 0; Pip < MaxGrenades; ++Pip)
		{
			if (Pip < Grenades)
			{
				HudRect(PipX, RowY + U(1.f), PipW, PipH, HudWithAlpha(HudColOrange, 0.95f));
				HudRect(PipX, RowY + U(1.f), PipW, FMath::Max(1.f, U(2.f)), FLinearColor(1.f, 1.f, 1.f, 0.4f));
			}
			else
			{
				HudFrame(PipX, RowY + U(1.f), PipW, PipH, FMath::Max(1.f, U(1.f)), FLinearColor(1.f, 1.f, 1.f, 0.25f));
			}
			PipX += PipW + PipGap;
		}
	}
}

// ---------------------------------------------------------------------------------------------
// 12. Spawn protection
// ---------------------------------------------------------------------------------------------

void AShoterHUD::DrawSpawnProtection()
{
	if (!LocalPawn || !bPawnAlive || !LocalPawn->IsSpawnProtected())
	{
		return;
	}

	const FString Label = LOCTEXT("SpawnProtection", "PROTECCI\u00D3N").ToString();
	const float TextPx = U(16.f);
	const float PillW = HudStrWidth(Label, TextPx) + U(28.f);
	const float PillH = U(26.f);
	const float RightEdge = U(40.f) + U(430.f);
	const float PillX = RightEdge - PillW;
	const float PillY = ScreenH - U(40.f) - U(118.f) - PillH - U(8.f);
	const float Pulse = 0.65f + 0.35f * FMath::Sin(Now * 6.f);

	HudPanel(PillX, PillY, PillW, PillH, HudWithAlpha(HudColCyan, Pulse));
	HudStr(Label, PillX + PillW * 0.5f, PillY + U(4.f), TextPx, HudWithAlpha(HudColCyan, Pulse), EShoterHUDAlign::Center, true);
}

// ---------------------------------------------------------------------------------------------
// 5. Top center: match info per mode
// ---------------------------------------------------------------------------------------------

void AShoterHUD::DrawTopCenter()
{
	if (!ShoterGS)
	{
		return;
	}

	const float Y = U(18.f);
	const float BoxH = U(58.f);
	const float TimeW = U(150.f);
	const float SideW = U(124.f);
	const float Gap = U(8.f);
	const int32 Remaining = ShoterGS->GetRemainingTime();
	const float TimeX = CenterX - TimeW * 0.5f;

	if (ShoterGS->IsZombiesMode())
	{
		const float PanelW = U(340.f);
		const float PanelX = CenterX - PanelW * 0.5f;
		const int32 Wave = ShoterGS->GetWave();
		const FString Title = Wave > 0
			? HudFormatInt(LOCTEXT("WaveTitle", "OLEADA {0}"), Wave)
			: LOCTEXT("WavePreparation", "PREPARACI\u00D3N").ToString();

		HudPanel(PanelX, Y, PanelW, BoxH, HudColCyan);
		HudStr(Title, CenterX, Y + U(9.f), U(40.f), HudColWhite, EShoterHUDAlign::Center, true);

		const float SubY = Y + BoxH + U(6.f);
		const float SubH = U(30.f);
		HudPanel(PanelX + U(30.f), SubY, PanelW - U(60.f), SubH, HudColCyan, 0.9f);
		if (ShoterGS->IsWaveBreak())
		{
			const FString Text = Remaining >= 0
				? HudFormatInt(LOCTEXT("NextWaveIn", "PR\u00D3XIMA OLEADA EN {0}"), Remaining)
				: LOCTEXT("NextWave", "PR\u00D3XIMA OLEADA").ToString();
			const float Pulse = 0.75f + 0.25f * FMath::Sin(Now * 5.f);
			HudStr(Text, CenterX, SubY + U(5.f), U(19.f), HudWithAlpha(HudColYellow, Pulse), EShoterHUDAlign::Center, true);
		}
		else
		{
			HudStr(HudFormatInt(LOCTEXT("ZombiesLeft", "ZOMBIES RESTANTES: {0}"), ShoterGS->GetZombiesRemaining()), CenterX, SubY + U(5.f), U(19.f), HudColWhite, EShoterHUDAlign::Center, true);
		}
		return;
	}

	// Clock (TDM and FFA).
	const bool bUrgent = Remaining >= 0 && Remaining <= 30;
	const float UrgentPulse = bUrgent ? 0.5f + 0.5f * FMath::Sin(Now * 8.f) : 0.f;
	const FString ClockText = Remaining >= 0 ? HudFormatClock(Remaining) : FString(TEXT("--:--"));
	HudPanel(TimeX, Y, TimeW, BoxH, bUrgent ? HudLerpColor(HudColCyan, HudColRed, UrgentPulse) : HudColCyan);
	HudStr(LOCTEXT("TimeLabel", "TIEMPO").ToString(), CenterX, Y + U(3.f), U(15.f), HudWithAlpha(HudColCyan, 0.85f), EShoterHUDAlign::Center, true);
	HudStr(ClockText, CenterX, Y + U(19.f), U(36.f), bUrgent ? HudLerpColor(HudColWhite, HudColRed, UrgentPulse) : HudColWhite, EShoterHUDAlign::Center, true);

	if (ShoterGS->IsTeamMode())
	{
		const uint8 Teams[2] = { ShoterTeam::Blue, ShoterTeam::Red };
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const uint8 Team = Teams[Side];
			const float BoxX = (Side == 0) ? (TimeX - Gap - SideW) : (TimeX + TimeW + Gap);
			HudStatBox(BoxX, Y, SideW, BoxH, ShoterTeam::GetDisplayName(Team).ToString(), FString::FromInt(ShoterGS->GetTeamScore(Team)), ShoterTeam::GetColor(Team), LocalTeam == Team);
		}
	}
	else
	{
		// Free-for-all: your kills vs the leader's (or the runner-up when you are the leader).
		const TArray<AShoterPlayerState*> Sorted = ShoterGS->GetSortedPlayers();
		const AShoterPlayerState* Rival = Sorted.Num() > 0 ? Sorted[0] : nullptr;
		FString RivalLabel = LOCTEXT("LeaderLabel", "L\u00CDDER").ToString();
		if (Rival && Rival == LocalPS)
		{
			Rival = Sorted.Num() > 1 ? Sorted[1] : nullptr;
			RivalLabel = LOCTEXT("RunnerUpLabel", "SEGUNDO").ToString();
		}

		const FString OwnKills = FString::FromInt(LocalPS ? LocalPS->GetKills() : 0);
		const FString RivalKills = Rival ? FString::FromInt(Rival->GetKills()) : FString(TEXT("-"));
		HudStatBox(TimeX - Gap - SideW, Y, SideW, BoxH, LOCTEXT("YouLabel", "T\u00DA").ToString(), OwnKills, HudColCyan, true);
		HudStatBox(TimeX + TimeW + Gap, Y, SideW, BoxH, RivalLabel, RivalKills, HudColOrange, false);
	}

	// Score limit under the clock ("META 50").
	if (ShoterGS->GetScoreLimit() > 0)
	{
		HudStr(HudFormatInt(LOCTEXT("ScoreLimit", "META {0}"), ShoterGS->GetScoreLimit()), CenterX, Y + BoxH + U(5.f), U(16.f), FLinearColor(1.f, 1.f, 1.f, 0.7f), EShoterHUDAlign::Center, true);
	}
}

// ---------------------------------------------------------------------------------------------
// 6. Kill feed (top-right)
// ---------------------------------------------------------------------------------------------

void AShoterHUD::DrawKillFeed()
{
	if (!ShoterGS)
	{
		return;
	}
	const TArray<FShoterKillFeedEntry>& Feed = ShoterGS->GetKillFeed();
	if (Feed.Num() == 0)
	{
		return;
	}

	// The feed is oldest-first: collect the newest entries that are still young enough, keeping chronological order.
	TArray<int32> Visible;
	for (int32 Index = Feed.Num() - 1; Index >= 0 && Visible.Num() < HudKillFeedMaxEntries; --Index)
	{
		if (Now - Feed[Index].LocalTimestamp > HudKillFeedLifetime)
		{
			break;
		}
		Visible.Insert(Index, 0);
	}

	const float RightX = ScreenW - U(32.f);
	const float RowH = U(30.f);
	const float RowGap = U(6.f);
	const float NamePx = U(19.f);
	const float TagPx = U(15.f);
	const float PadX = U(10.f);
	const float Space = U(10.f);
	const float MaxName = U(220.f);
	float RowY = U(32.f);

	for (const int32 Index : Visible)
	{
		const FShoterKillFeedEntry& Entry = Feed[Index];
		const float Age = FMath::Max(0.f, Now - Entry.LocalTimestamp);
		const float Alpha = HudSat((HudKillFeedLifetime - Age) / HudKillFeedFadeTime) * HudSat(Age / 0.1f);
		const float OffsetX = (1.f - HudEaseOut(Age / 0.18f)) * U(40.f);

		const bool bShowKiller = !Entry.KillerName.IsEmpty() && Entry.KillerName != Entry.VictimName;
		const FString KillerText = bShowKiller ? FitStr(Entry.KillerName, NamePx, MaxName) : FString();
		const FString VictimText = FitStr(Entry.VictimName, NamePx, MaxName);
		const FString TagText = Entry.WeaponName.IsEmpty() ? FString() : FString::Printf(TEXT("[%s]"), *Entry.WeaponName.ToUpper());

		const FLinearColor KillerColor = HudMulAlpha(GetRelationColor(Entry.KillerTeam, Entry.KillerPlayerState.Get()), Alpha);
		const FLinearColor VictimColor = HudMulAlpha(GetRelationColor(Entry.VictimTeam, Entry.VictimPlayerState.Get()), Alpha);

		const float KillerW = HudStrWidth(KillerText, NamePx);
		const float TagW = HudStrWidth(TagText, TagPx);
		const float VictimW = HudStrWidth(VictimText, NamePx);
		const float IconW = Entry.bHeadshot ? U(16.f) : 0.f;

		float ContentW = VictimW;
		ContentW += bShowKiller ? KillerW + Space : 0.f;
		ContentW += TagW > 0.f ? TagW + Space : 0.f;
		ContentW += IconW > 0.f ? IconW + Space * 0.6f : 0.f;
		const float RowW = ContentW + PadX * 2.f;
		const float RowX = RightX - RowW + OffsetX;

		// Highlight rows that involve the local player.
		const bool bLocalKiller = LocalPS && Entry.KillerPlayerState.Get() == LocalPS;
		const bool bLocalVictim = LocalPS && Entry.VictimPlayerState.Get() == LocalPS;
		const FLinearColor Accent = bLocalVictim ? HudColRed : HudColCyan;
		HudRect(RowX, RowY, RowW, RowH, HudMulAlpha(HudColPanel, Alpha));
		HudRect(RowX, RowY, FMath::Max(1.f, U(2.f)), RowH, HudWithAlpha(Accent, (bLocalKiller || bLocalVictim ? 0.95f : 0.35f) * Alpha));
		if (bLocalKiller || bLocalVictim)
		{
			HudFrame(RowX, RowY, RowW, RowH, FMath::Max(1.f, U(1.f)), HudWithAlpha(Accent, 0.55f * Alpha));
		}

		float CursorX = RowX + PadX;
		const float TextY = RowY + (RowH - NamePx) * 0.5f;
		if (bShowKiller)
		{
			HudStr(KillerText, CursorX, TextY, NamePx, KillerColor, EShoterHUDAlign::Left, true);
			CursorX += KillerW + Space;
		}
		if (TagW > 0.f)
		{
			HudStr(TagText, CursorX, RowY + (RowH - TagPx) * 0.5f, TagPx, FLinearColor(0.75f, 0.92f, 1.f, 0.9f * Alpha), EShoterHUDAlign::Left, true);
			CursorX += TagW + Space;
		}
		if (IconW > 0.f)
		{
			// Headshot marker: a small orange reticle.
			const float IconX = CursorX + IconW * 0.5f;
			const float IconY = RowY + RowH * 0.5f;
			const float Ring = U(5.f);
			const FLinearColor IconColor(1.f, 0.4f, 0.08f, Alpha);
			HudCircle(IconX, IconY, Ring, 12, IconColor, FMath::Max(1.f, U(1.6f)));
			HudRect(IconX - U(1.f), IconY - U(1.f), U(2.f), U(2.f), IconColor);
			HudRect(IconX - U(1.f), IconY - Ring - U(3.f), U(2.f), U(3.f), IconColor);
			HudRect(IconX - U(1.f), IconY + Ring, U(2.f), U(3.f), IconColor);
			HudRect(IconX - Ring - U(3.f), IconY - U(1.f), U(3.f), U(2.f), IconColor);
			HudRect(IconX + Ring, IconY - U(1.f), U(3.f), U(2.f), IconColor);
			CursorX += IconW + Space * 0.6f;
		}
		HudStr(VictimText, CursorX, TextY, NamePx, VictimColor, EShoterHUDAlign::Left, true);

		RowY += RowH + RowGap;
	}
}

// ---------------------------------------------------------------------------------------------
// 8. Notifications (under the crosshair)
// ---------------------------------------------------------------------------------------------

void AShoterHUD::DrawNotifications()
{
	if (!LocalPC)
	{
		return;
	}
	const TArray<FShoterNotification>& Notes = LocalPC->GetNotifications();
	if (Notes.Num() == 0)
	{
		return;
	}

	const float LineH = U(29.f);
	const float BaseY = CenterY + U(78.f);
	int32 Row = 0;

	// Newest first, closest to the crosshair; older lines stack below and fade out.
	for (int32 Index = Notes.Num() - 1; Index >= 0 && Row < 4; --Index)
	{
		const FShoterNotification& Note = Notes[Index];
		const float Age = Now - Note.Time;
		if (Age < 0.f || Age > HudNotificationLifetime)
		{
			continue;
		}
		const float Alpha = HudSat((1.f - Age / HudNotificationLifetime) / 0.6f) * HudSat(Age / 0.08f);
		const float Slide = (1.f - HudEaseOut(Age / 0.15f)) * U(-10.f);
		HudStr(Note.Text.ToString(), CenterX, BaseY + LineH * static_cast<float>(Row) + Slide, U(24.f), HudMulAlpha(Note.Color, Alpha), EShoterHUDAlign::Center, true);
		++Row;
	}
}

// ---------------------------------------------------------------------------------------------
// 9. Announcement (big centered title + subtitle)
// ---------------------------------------------------------------------------------------------

void AShoterHUD::DrawAnnouncement()
{
	if (!LocalPC)
	{
		return;
	}
	const FShoterAnnouncement& Announcement = LocalPC->GetAnnouncement();
	const float Age = Now - Announcement.StartTime;
	if (Announcement.Duration <= 0.f || Age < 0.f || Age > Announcement.Duration)
	{
		return;
	}

	const FString Title = Announcement.Title.ToString().ToUpper();
	const FString Subtitle = Announcement.Subtitle.ToString();
	if (Title.IsEmpty() && Subtitle.IsEmpty())
	{
		return;
	}

	// Fade in over 0.2 s, fade out over the last 0.5 s.
	float Alpha = Age < 0.2f ? Age / 0.2f : 1.f;
	const float TimeLeft = Announcement.Duration - Age;
	if (TimeLeft < 0.5f)
	{
		Alpha = FMath::Min(Alpha, TimeLeft / 0.5f);
	}
	Alpha = HudSat(Alpha);
	if (Alpha <= 0.003f)
	{
		return;
	}

	const FLinearColor Color = HudWithAlpha(Announcement.Color, Alpha);
	const float Pop = 1.f + 0.18f * (1.f - HudEaseOut(Age / 0.25f));
	const float TitlePx = U(84.f) * Pop;
	const float SubPx = U(30.f);
	const float MidY = ScreenH * 0.24f;
	const float BlockH = (Title.IsEmpty() ? 0.f : TitlePx) + (Subtitle.IsEmpty() ? 0.f : SubPx + U(14.f));
	const float TopY = MidY - BlockH * 0.5f;

	// Soft dark band behind the text (stacked translucent rectangles, thinner toward the edges).
	const float BandH = BlockH + U(70.f);
	for (int32 Layer = 0; Layer < 6; ++Layer)
	{
		const float LayerH = BandH * (1.f - static_cast<float>(Layer) / 7.f);
		HudRect(0.f, MidY - LayerH * 0.5f, ScreenW, LayerH, FLinearColor(0.f, 0.f, 0.02f, 0.09f * Alpha));
	}

	float CursorY = TopY;
	if (!Title.IsEmpty())
	{
		const float TitleW = HudStrWidth(Title, TitlePx);
		HudStr(Title, CenterX, CursorY, TitlePx, Color, EShoterHUDAlign::Center, true);
		CursorY += TitlePx;

		const float LineW = TitleW + U(180.f);
		const float LineY = CursorY + U(2.f);
		HudRect(CenterX - LineW * 0.5f, LineY, LineW, FMath::Max(1.f, U(2.f)), HudWithAlpha(Announcement.Color, 0.75f * Alpha));
		HudRect(CenterX - LineW * 0.5f, LineY - U(5.f), FMath::Max(1.f, U(2.f)), U(12.f), HudWithAlpha(Announcement.Color, 0.75f * Alpha));
		HudRect(CenterX + LineW * 0.5f - FMath::Max(1.f, U(2.f)), LineY - U(5.f), FMath::Max(1.f, U(2.f)), U(12.f), HudWithAlpha(Announcement.Color, 0.75f * Alpha));
		CursorY += U(14.f);
	}
	if (!Subtitle.IsEmpty())
	{
		HudStr(Subtitle, CenterX, CursorY, SubPx, HudWithAlpha(HudLerpColor(HudColWhite, Announcement.Color, 0.25f), 0.95f * Alpha), EShoterHUDAlign::Center, true);
	}
}

// ---------------------------------------------------------------------------------------------
// 10. Interaction prompt
// ---------------------------------------------------------------------------------------------

void AShoterHUD::DrawInteractionPrompt()
{
	if (!LocalPawn || !bPawnAlive)
	{
		return;
	}
	const FText Prompt = LocalPawn->GetInteractionPrompt();
	if (Prompt.IsEmpty())
	{
		return;
	}

	const FString Text = Prompt.ToString();
	const float Px = U(22.f);
	const float PlateW = HudStrWidth(Text, Px) + U(44.f);
	const float PlateH = U(40.f);
	const float PlateX = CenterX - PlateW * 0.5f;
	const float PlateY = CenterY + U(204.f);

	HudPanel(PlateX, PlateY, PlateW, PlateH, HudColCyan);
	HudStr(Text, CenterX, PlateY + (PlateH - Px) * 0.5f, Px, HudColWhite, EShoterHUDAlign::Center, false);
}

// ---------------------------------------------------------------------------------------------
// 11. Death screen
// ---------------------------------------------------------------------------------------------

void AShoterHUD::DrawDeathScreen()
{
	if (!LocalPC || !LocalPC->IsDeadAndWaiting())
	{
		DeathScreenStartTime = -1.f;
		return;
	}
	if (DeathScreenStartTime < 0.f)
	{
		DeathScreenStartTime = Now;
	}
	const float Fade = HudSat((Now - DeathScreenStartTime) / 0.6f);

	HudRect(0.f, 0.f, ScreenW, ScreenH, FLinearColor(0.03f, 0.0f, 0.0f, 0.38f * Fade));
	HudEdgeGlow(FLinearColor(0.8f, 0.02f, 0.02f, 1.f), 0.55f * Fade, 8, ScreenH * 0.2f);

	const FString& Killer = LocalPC->GetLastKillerName();
	const FString& KillerWeapon = LocalPC->GetLastKillerWeapon();

	float Y = ScreenH * 0.31f;
	if (!Killer.IsEmpty())
	{
		HudStr(LOCTEXT("EliminatedBy", "ELIMINADO POR").ToString(), CenterX, Y, U(26.f), HudWithAlpha(HudColRed, Fade), EShoterHUDAlign::Center, true);
		Y += U(36.f);
		HudStr(FitStr(Killer, U(72.f), ScreenW * 0.8f), CenterX, Y, U(72.f), FLinearColor(1.f, 1.f, 1.f, Fade), EShoterHUDAlign::Center, true);
		Y += U(82.f);
		if (!KillerWeapon.IsEmpty())
		{
			HudStr(FString::Printf(TEXT("[%s]"), *KillerWeapon.ToUpper()), CenterX, Y, U(26.f), HudWithAlpha(HudColCyan, Fade), EShoterHUDAlign::Center, true);
		}
	}
	else
	{
		HudStr(LOCTEXT("Eliminated", "ELIMINADO").ToString(), CenterX, Y, U(72.f), HudWithAlpha(HudColRed, Fade), EShoterHUDAlign::Center, true);
	}

	// Respawn countdown. A negative remaining time means "next wave" (zombies).
	const float Remaining = LocalPC->GetRespawnTimeRemaining();
	FString RespawnText;
	FLinearColor RespawnColor = HudColWhite;
	if (Remaining < 0.f)
	{
		RespawnText = LOCTEXT("RespawnNextWave", "REAPARECER\u00C1S EN LA PR\u00D3XIMA OLEADA").ToString();
		RespawnColor = HudColYellow;
	}
	else if (Remaining <= 0.05f)
	{
		RespawnText = LOCTEXT("Respawning", "REAPARECIENDO...").ToString();
	}
	else
	{
		RespawnText = HudFormatInt(LOCTEXT("RespawnIn", "REAPARECES EN {0}"), FMath::CeilToInt(Remaining));
	}

	const float RespawnY = ScreenH * 0.62f;
	const float RespawnPx = U(38.f);
	const float RespawnW = HudStrWidth(RespawnText, RespawnPx);
	HudRect(CenterX - RespawnW * 0.5f - U(40.f), RespawnY - U(12.f), RespawnW + U(80.f), RespawnPx + U(24.f), FLinearColor(0.f, 0.f, 0.02f, 0.45f * Fade));
	HudRect(CenterX - RespawnW * 0.5f - U(40.f), RespawnY - U(12.f), RespawnW + U(80.f), FMath::Max(1.f, U(2.f)), HudWithAlpha(RespawnColor, 0.7f * Fade));
	HudStr(RespawnText, CenterX, RespawnY, RespawnPx, HudWithAlpha(RespawnColor, Fade), EShoterHUDAlign::Center, true);
}

// ---------------------------------------------------------------------------------------------
// 13. Scoreboard
// ---------------------------------------------------------------------------------------------

namespace
{
	constexpr float HudScoreHeaderH = 46.f;
	constexpr float HudScoreColumnHeaderH = 28.f;
	constexpr float HudScoreRowH = 34.f;
	constexpr float HudScoreFooterH = 8.f;
	constexpr int32 HudScoreMaxRows = 12;
}

void AShoterHUD::DrawScoreTable(float X, float Y, float W, const FString& Title, const FString& TitleValue, const FLinearColor& Accent, const TArray<AShoterPlayerState*>& Players, bool bZombies, int32 MinRows)
{
	TArray<const AShoterPlayerState*> Valid;
	Valid.Reserve(Players.Num());
	for (const AShoterPlayerState* Candidate : Players)
	{
		if (Candidate && Valid.Num() < HudScoreMaxRows)
		{
			Valid.Add(Candidate);
		}
	}

	const int32 Rows = FMath::Max(1, FMath::Max(Valid.Num(), MinRows));
	const float Pad = U(16.f);
	const float HeaderH = U(HudScoreHeaderH);
	const float ColumnHeaderH = U(HudScoreColumnHeaderH);
	const float RowH = U(HudScoreRowH);
	const float TotalH = HeaderH + ColumnHeaderH + RowH * static_cast<float>(Rows) + U(HudScoreFooterH);

	// Panel + title bar (title on the left, e.g. the team score on the right).
	HudPanel(X, Y, W, TotalH, Accent);
	HudRect(X, Y, W, HeaderH, HudWithAlpha(Accent, 0.16f));
	HudStr(Title, X + Pad, Y + (HeaderH - U(26.f)) * 0.5f, U(26.f), HudColWhite, EShoterHUDAlign::Left, true);
	if (!TitleValue.IsEmpty())
	{
		HudStr(TitleValue, X + W - Pad, Y + (HeaderH - U(34.f)) * 0.5f, U(34.f), HudWithAlpha(HudLerpColor(Accent, HudColWhite, 0.35f), 1.f), EShoterHUDAlign::Right, true);
	}

	// Column right edges: K | D | A | score | ping.
	const float PingW = U(70.f);
	const float ScoreW = U(96.f);
	const float StatW = U(56.f);
	const float PingR = X + W - Pad;
	const float ScoreR = PingR - PingW;
	const float AssistR = ScoreR - ScoreW;
	const float DeathR = AssistR - StatW;
	const float KillR = DeathR - StatW;
	const float NameL = X + Pad;
	const float NameMax = (KillR - StatW) - NameL - U(10.f);

	const float ColumnY = Y + HeaderH + U(6.f);
	const float ColumnPx = U(15.f);
	const FLinearColor HeadColor(0.55f, 0.85f, 0.95f, 0.85f);
	HudStr(LOCTEXT("ColumnPlayer", "JUGADOR").ToString(), NameL, ColumnY, ColumnPx, HeadColor, EShoterHUDAlign::Left, true);
	HudStr(LOCTEXT("ColumnKills", "K").ToString(), KillR, ColumnY, ColumnPx, HeadColor, EShoterHUDAlign::Right, true);
	HudStr(LOCTEXT("ColumnDeaths", "D").ToString(), DeathR, ColumnY, ColumnPx, HeadColor, EShoterHUDAlign::Right, true);
	HudStr(LOCTEXT("ColumnAssists", "A").ToString(), AssistR, ColumnY, ColumnPx, HeadColor, EShoterHUDAlign::Right, true);
	HudStr(bZombies ? LOCTEXT("ColumnPoints", "PUNTOS").ToString() : LOCTEXT("ColumnScore", "PUNT.").ToString(), ScoreR, ColumnY, ColumnPx, HeadColor, EShoterHUDAlign::Right, true);
	HudStr(LOCTEXT("ColumnPing", "PING").ToString(), PingR, ColumnY, ColumnPx, HeadColor, EShoterHUDAlign::Right, true);
	HudRect(X + Pad, Y + HeaderH + ColumnHeaderH - U(2.f), W - 2.f * Pad, FMath::Max(1.f, U(1.f)), HudWithAlpha(Accent, 0.4f));

	// Rows
	const float RowsTop = Y + HeaderH + ColumnHeaderH;
	const float TextPx = U(21.f);
	for (int32 Row = 0; Row < Valid.Num(); ++Row)
	{
		const AShoterPlayerState* PS = Valid[Row];
		const float RowY = RowsTop + RowH * static_cast<float>(Row);
		const bool bLocal = (PS == LocalPS);
		const bool bBot = PS->IsABot();

		if (bLocal)
		{
			HudRect(X + 1.f, RowY, W - 2.f, RowH, HudWithAlpha(HudColCyan, 0.17f));
			HudRect(X + 1.f, RowY, FMath::Max(2.f, U(3.f)), RowH, HudWithAlpha(HudColCyan, 0.95f));
		}
		else if ((Row & 1) == 1)
		{
			HudRect(X + 1.f, RowY, W - 2.f, RowH, FLinearColor(1.f, 1.f, 1.f, 0.035f));
		}

		FString Name = PS->GetPlayerName();
		if (bBot)
		{
			Name += TEXT(" [BOT]");
		}
		Name = FitStr(Name, TextPx, NameMax);

		const FLinearColor RowColor = bLocal ? HudColSelf : FLinearColor(1.f, 1.f, 1.f, 0.9f);
		const float TextY = RowY + (RowH - TextPx) * 0.5f;
		const int32 ScoreValue = bZombies ? PS->GetZombiePoints() : FMath::RoundToInt(PS->GetScore());
		const float PingMs = PS->GetPingInMilliseconds();
		const FLinearColor PingColor = PingMs <= 60.f ? FLinearColor(0.45f, 1.f, 0.6f, 0.85f) : (PingMs <= 120.f ? FLinearColor(1.f, 0.85f, 0.3f, 0.85f) : FLinearColor(1.f, 0.4f, 0.3f, 0.85f));

		HudStr(Name, NameL + U(6.f), TextY, TextPx, RowColor, EShoterHUDAlign::Left, bLocal);
		HudStr(FString::FromInt(PS->GetKills()), KillR, TextY, TextPx, RowColor, EShoterHUDAlign::Right);
		HudStr(FString::FromInt(PS->GetDeaths()), DeathR, TextY, TextPx, RowColor, EShoterHUDAlign::Right);
		HudStr(FString::FromInt(PS->GetAssists()), AssistR, TextY, TextPx, RowColor, EShoterHUDAlign::Right);
		HudStr(FString::FromInt(ScoreValue), ScoreR, TextY, TextPx, bLocal ? HudColSelf : HudColCyan, EShoterHUDAlign::Right, true);
		HudStr(bBot ? FString(TEXT("-")) : FString::FromInt(FMath::RoundToInt(PingMs)), PingR, TextY, U(18.f), bBot ? FLinearColor(1.f, 1.f, 1.f, 0.4f) : PingColor, EShoterHUDAlign::Right);
	}
}

void AShoterHUD::DrawScoreboard()
{
	if (!LocalPC || !ShoterGS)
	{
		return;
	}
	const bool bMatchOver = LocalPC->HasMatchEnded() || ShoterGS->HasMatchEnded();
	if (!LocalPC->IsScoreboardVisible() && !bMatchOver)
	{
		return;
	}

	// With the match end banner on screen the tables start lower so both fit.
	const bool bBannerShown = LocalPC->HasMatchEnded();
	if (!bBannerShown)
	{
		HudRect(0.f, 0.f, ScreenW, ScreenH, FLinearColor(0.f, 0.f, 0.02f, 0.28f));
	}

	auto TableHeight = [this](int32 Rows)
	{
		return U(HudScoreHeaderH) + U(HudScoreColumnHeaderH) + U(HudScoreRowH) * static_cast<float>(FMath::Max(1, Rows)) + U(HudScoreFooterH);
	};
	auto TableTop = [this, bBannerShown](float Height)
	{
		return bBannerShown ? ScreenH * 0.36f : FMath::Max(U(80.f), (ScreenH - Height) * 0.5f);
	};

	if (ShoterGS->IsTeamMode())
	{
		const TArray<AShoterPlayerState*> Blue = ShoterGS->GetSortedPlayersOfTeam(ShoterTeam::Blue);
		const TArray<AShoterPlayerState*> Red = ShoterGS->GetSortedPlayersOfTeam(ShoterTeam::Red);
		const int32 Rows = FMath::Clamp(FMath::Max(Blue.Num(), Red.Num()), 1, HudScoreMaxRows);
		const float Gap = U(24.f);
		const float TableW = FMath::Min(U(580.f), (ScreenW - U(60.f) - Gap) * 0.5f);
		const float X0 = CenterX - (TableW * 2.f + Gap) * 0.5f;
		const float Y0 = TableTop(TableHeight(Rows));

		const uint8 Teams[2] = { ShoterTeam::Blue, ShoterTeam::Red };
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const uint8 Team = Teams[Side];
			const FString Title = FText::Format(LOCTEXT("TeamTableTitle", "EQUIPO {0}"), ShoterTeam::GetDisplayName(Team)).ToString();
			DrawScoreTable(X0 + (TableW + Gap) * static_cast<float>(Side), Y0, TableW, Title, FString::FromInt(ShoterGS->GetTeamScore(Team)),
				ShoterTeam::GetColor(Team), (Side == 0) ? Blue : Red, false, Rows);
		}
		return;
	}

	// Free-for-all and zombies: a single table.
	const TArray<AShoterPlayerState*> Players = ShoterGS->GetSortedPlayers();
	const bool bZombies = ShoterGS->IsZombiesMode();
	const int32 Rows = FMath::Clamp(Players.Num(), 1, HudScoreMaxRows);
	const float TableW = FMath::Min(U(900.f), ScreenW - U(60.f));
	const float X0 = CenterX - TableW * 0.5f;
	const float Y0 = TableTop(TableHeight(Rows));

	const FString Title = ShoterMatch::GetDisplayName(ShoterGS->GetMatchType()).ToString().ToUpper();
	FString TitleValue;
	if (bZombies)
	{
		if (ShoterGS->GetWave() > 0)
		{
			TitleValue = HudFormatInt(LOCTEXT("WaveTitle", "OLEADA {0}"), ShoterGS->GetWave());
		}
	}
	else if (ShoterGS->GetRemainingTime() >= 0)
	{
		TitleValue = HudFormatClock(ShoterGS->GetRemainingTime());
	}
	DrawScoreTable(X0, Y0, TableW, Title, TitleValue, HudColCyan, Players, bZombies, Rows);
}

// ---------------------------------------------------------------------------------------------
// 14. Match end banner
// ---------------------------------------------------------------------------------------------

void AShoterHUD::DrawMatchEnd()
{
	if (!LocalPC || !LocalPC->HasMatchEnded())
	{
		MatchEndStartTime = -1.f;
		return;
	}
	if (MatchEndStartTime < 0.f)
	{
		MatchEndStartTime = Now;
	}
	const float Age = Now - MatchEndStartTime;
	const float Fade = HudSat(Age / 0.5f);

	const bool bZombies = ShoterGS && ShoterGS->IsZombiesMode();
	const bool bWon = LocalPC->DidWinMatch();

	FString Title;
	FString Subtitle;
	FLinearColor Color = HudColWhite;
	if (bZombies)
	{
		Title = LOCTEXT("GameOver", "FIN DE LA PARTIDA").ToString();
		Color = HudColCyan;
		const int32 Waves = ShoterGS ? ShoterGS->GetWave() : 0;
		if (Waves > 0)
		{
			Subtitle = HudFormatInt(Waves == 1 ? LOCTEXT("SurvivedOneWave", "SOBREVIVIERON {0} OLEADA") : LOCTEXT("SurvivedWaves", "SOBREVIVIERON {0} OLEADAS"), Waves);
		}
	}
	else
	{
		Title = bWon ? LOCTEXT("Victory", "VICTORIA").ToString() : LOCTEXT("Defeat", "DERROTA").ToString();
		Color = bWon ? HudColGreen : HudColRed;
		if (ShoterGS && ShoterGS->IsTeamMode())
		{
			const uint8 Winner = ShoterGS->GetWinningTeam();
			Subtitle = (Winner == ShoterTeam::None)
				? LOCTEXT("Draw", "EMPATE").ToString()
				: FText::Format(LOCTEXT("TeamWins", "GANA EL EQUIPO {0}"), ShoterTeam::GetDisplayName(Winner)).ToString();
		}
		else if (ShoterGS && ShoterGS->GetWinningPlayer())
		{
			Subtitle = FText::Format(LOCTEXT("PlayerWins", "GANADOR: {0}"), FText::FromString(ShoterGS->GetWinningPlayer()->GetPlayerName())).ToString();
		}
	}

	// Dim the world and put a soft band behind the title.
	HudRect(0.f, 0.f, ScreenW, ScreenH, FLinearColor(0.f, 0.f, 0.02f, 0.5f * Fade));
	const float Pop = 1.f + 0.2f * (1.f - HudEaseOut(Age / 0.35f));
	const float TitlePx = U(120.f) * Pop;
	const float TitleY = ScreenH * 0.10f;
	const float BandMid = TitleY + TitlePx * 0.5f + U(20.f);
	const float BandH = TitlePx + U(120.f);
	for (int32 Layer = 0; Layer < 6; ++Layer)
	{
		const float LayerH = BandH * (1.f - static_cast<float>(Layer) / 7.f);
		HudRect(0.f, BandMid - LayerH * 0.5f, ScreenW, LayerH, HudWithAlpha(Color, 0.045f * Fade));
	}

	const float TitleW = HudStrWidth(Title, TitlePx);
	HudStr(Title, CenterX, TitleY + (U(120.f) - TitlePx) * 0.5f, TitlePx, HudWithAlpha(Color, Fade), EShoterHUDAlign::Center, true);

	const float LineW = TitleW + U(200.f);
	const float LineY = TitleY + U(120.f) + U(8.f);
	HudRect(CenterX - LineW * 0.5f, LineY, LineW, FMath::Max(1.f, U(2.f)), HudWithAlpha(Color, 0.75f * Fade));
	if (!Subtitle.IsEmpty())
	{
		HudStr(Subtitle, CenterX, LineY + U(14.f), U(34.f), FLinearColor(1.f, 1.f, 1.f, 0.95f * Fade), EShoterHUDAlign::Center, true);
	}
}

// ---------------------------------------------------------------------------------------------
// 15. FPS counter
// ---------------------------------------------------------------------------------------------

void AShoterHUD::DrawFPS()
{
	UWorld* World = GetWorld();
	const UShoterGameInstance* GameInstance = World ? World->GetGameInstance<UShoterGameInstance>() : nullptr;
	if (!GameInstance || !GameInstance->GetUserSettings().bShowFPS)
	{
		return;
	}

	const float Fps = 1.f / FMath::Max(SmoothedFrameTime, 0.0001f);
	const FLinearColor Color = Fps >= 55.f ? FLinearColor(0.45f, 1.f, 0.6f, 1.f) : (Fps >= 30.f ? HudColYellow : HudColRed);
	const FString Text = FString::Printf(TEXT("%d FPS"), FMath::RoundToInt(Fps));
	const float Px = U(18.f);
	const float PlateW = HudStrWidth(Text, Px) + U(24.f);
	const float PlateH = U(30.f);
	HudPanel(U(16.f), U(16.f), PlateW, PlateH, Color, 0.9f);
	HudStr(Text, U(16.f) + PlateW * 0.5f, U(16.f) + (PlateH - Px) * 0.5f, Px, Color, EShoterHUDAlign::Center, true);
}

#undef LOCTEXT_NAMESPACE
