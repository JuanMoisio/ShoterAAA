#pragma once

#include "CoreMinimal.h"

DECLARE_LOG_CATEGORY_EXTERN(LogShoter, Log, All);

/**
 * Custom collision channels, declared in Config/DefaultEngine.ini [/Script/Engine.CollisionProfile].
 * Weapon traces hit character meshes (physics asset bodies) so headshots can be detected,
 * while capsules ignore this channel.
 */
#define SHOTER_TRACE_WEAPON			ECC_GameTraceChannel1
#define SHOTER_OBJECT_PROJECTILE	ECC_GameTraceChannel2

/** Bone names used for headshot detection on the UE5 mannequin skeleton. */
#define SHOTER_HEAD_BONE			FName(TEXT("head"))
