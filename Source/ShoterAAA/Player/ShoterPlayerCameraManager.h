// Camera: user FOV, aim-down-sights zoom, sprint FOV kick, trauma-based shake and death cam.
#pragma once

#include "CoreMinimal.h"
#include "Camera/PlayerCameraManager.h"
#include "ShoterPlayerCameraManager.generated.h"

UCLASS()
class SHOTERAAA_API AShoterPlayerCameraManager : public APlayerCameraManager
{
	GENERATED_BODY()

public:
	AShoterPlayerCameraManager();

	virtual void UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime) override;

	/** Adds camera shake (0..1, accumulates and decays). */
	void AddTrauma(float Amount);

	/** Shakes every local player camera near Location, scaled by distance. */
	static void AddTraumaAtLocation(const UObject* WorldContextObject, const FVector& Location, float Radius, float MaxTrauma);

	/** Third-person camera that looks at the killer after death. */
	void StartDeathCam(const FVector& DeathLocation, AActor* FocusActor);
	void StopDeathCam();
	bool IsDeathCamActive() const { return bDeathCam; }

protected:
	float Trauma = 0.f;
	float ShakeTime = 0.f;
	/** Extra FOV while sprinting, smoothed. */
	float SprintFOVKick = 0.f;

	bool bDeathCam = false;
	FVector DeathCamLocation = FVector::ZeroVector;
	TWeakObjectPtr<AActor> DeathCamFocus;
	float DeathCamStartTime = 0.f;
};
