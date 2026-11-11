#include "Player/ShoterPlayerCameraManager.h"
#include "Player/ShoterCharacter.h"
#include "Player/ShoterPlayerController.h"
#include "Weapons/ShoterWeapon.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

AShoterPlayerCameraManager::AShoterPlayerCameraManager()
{
	ViewPitchMin = -88.f;
	ViewPitchMax = 88.f;
	DefaultFOV = 100.f;
	bAlwaysApplyModifiers = true;
}

void AShoterPlayerCameraManager::UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime)
{
	Super::UpdateViewTarget(OutVT, DeltaTime);

	const AShoterPlayerController* ShoterPC = Cast<AShoterPlayerController>(PCOwner);
	const float UserFOV = ShoterPC ? ShoterPC->GetUserFieldOfView() : DefaultFOV;

	// --- Field of view: user setting, aim-down-sights zoom and a small sprint kick ---
	float TargetFOV = UserFOV;
	float SprintKick = 0.f;
	if (const AShoterCharacter* Character = Cast<AShoterCharacter>(OutVT.Target))
	{
		const float Aim = Character->GetAimAlpha();
		if (const AShoterWeapon* Weapon = Character->GetCurrentWeapon())
		{
			TargetFOV = FMath::Lerp(UserFOV, Weapon->GetStats().AimFOV, FMath::InterpEaseInOut(0.f, 1.f, Aim, 2.f));
		}
		if (Character->IsSprinting())
		{
			SprintKick = 6.f * (1.f - Aim);
		}
	}
	SprintFOVKick = FMath::FInterpTo(SprintFOVKick, SprintKick, DeltaTime, 8.f);
	OutVT.POV.FOV = TargetFOV + SprintFOVKick;

	// --- Death cam: pull back from where we died and look at the killer ---
	if (bDeathCam)
	{
		const float Elapsed = GetWorld()->GetTimeSeconds() - DeathCamStartTime;
		const FVector Focus = DeathCamFocus.IsValid() ? DeathCamFocus->GetActorLocation() : DeathCamLocation;
		const FVector Eye = DeathCamLocation + FVector(0.f, 0.f, 60.f);
		FVector Away = (DeathCamLocation - Focus).GetSafeNormal2D();
		if (Away.IsNearlyZero())
		{
			Away = -OutVT.POV.Rotation.Vector().GetSafeNormal2D();
		}
		const FVector Desired = DeathCamLocation + FVector(0.f, 0.f, 220.f) + Away * 260.f;

		FVector CameraLocation = Desired;
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ShoterDeathCam), false);
		if (GetWorld()->LineTraceSingleByChannel(Hit, Eye, Desired, ECC_Camera, Params))
		{
			CameraLocation = Hit.Location + Hit.ImpactNormal * 15.f;
		}

		const float Blend = FMath::InterpEaseOut(0.f, 1.f, FMath::Clamp(Elapsed / 0.7f, 0.f, 1.f), 2.f);
		OutVT.POV.Location = FMath::Lerp(Eye, CameraLocation, Blend);
		OutVT.POV.Rotation = (Focus + FVector(0.f, 0.f, 40.f) - OutVT.POV.Location).Rotation();
		OutVT.POV.FOV = UserFOV;
	}

	// --- Trauma shake (explosions, heavy weapons, taking damage) ---
	if (Trauma > 0.f)
	{
		ShakeTime += DeltaTime;
		const float Shake = Trauma * Trauma;
		const float Frequency = 18.f;
		OutVT.POV.Rotation.Pitch += 2.5f * Shake * FMath::PerlinNoise1D(ShakeTime * Frequency);
		OutVT.POV.Rotation.Yaw += 2.5f * Shake * FMath::PerlinNoise1D(ShakeTime * Frequency + 37.f);
		OutVT.POV.Rotation.Roll += 3.5f * Shake * FMath::PerlinNoise1D(ShakeTime * Frequency + 71.f);
		Trauma = FMath::Max(0.f, Trauma - DeltaTime * 1.5f);
	}
}

void AShoterPlayerCameraManager::AddTrauma(float Amount)
{
	Trauma = FMath::Clamp(Trauma + Amount, 0.f, 1.f);
}

void AShoterPlayerCameraManager::AddTraumaAtLocation(const UObject* WorldContextObject, const FVector& Location, float Radius, float MaxTrauma)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World || Radius <= 0.f)
	{
		return;
	}

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC || !PC->IsLocalController())
		{
			continue;
		}
		if (AShoterPlayerCameraManager* CameraManager = Cast<AShoterPlayerCameraManager>(PC->PlayerCameraManager))
		{
			const float Distance = FVector::Dist(CameraManager->GetCameraLocation(), Location);
			if (Distance < Radius)
			{
				CameraManager->AddTrauma(MaxTrauma * (1.f - Distance / Radius));
			}
		}
	}
}

void AShoterPlayerCameraManager::StartDeathCam(const FVector& DeathLocation, AActor* FocusActor)
{
	bDeathCam = true;
	DeathCamLocation = DeathLocation;
	DeathCamFocus = FocusActor;
	DeathCamStartTime = GetWorld()->GetTimeSeconds();
}

void AShoterPlayerCameraManager::StopDeathCam()
{
	bDeathCam = false;
	DeathCamFocus.Reset();
}
