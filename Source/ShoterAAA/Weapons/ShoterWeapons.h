// Concrete weapons. Each constructor only fills stats and the procedural model.
#pragma once

#include "CoreMinimal.h"
#include "Weapons/ShoterWeapon.h"
#include "ShoterWeapons.generated.h"

/** Semi-auto sidearm. Everyone spawns with it. */
UCLASS()
class SHOTERAAA_API AShoterWeapon_PulsePistol : public AShoterWeapon
{
	GENERATED_BODY()
public:
	AShoterWeapon_PulsePistol();
};

/** Full-auto assault rifle. Default primary in PvP. */
UCLASS()
class SHOTERAAA_API AShoterWeapon_PulseRifle : public AShoterWeapon
{
	GENERATED_BODY()
public:
	AShoterWeapon_PulseRifle();
};

/** SMG: very high fire rate, short range. */
UCLASS()
class SHOTERAAA_API AShoterWeapon_Ripper : public AShoterWeapon
{
	GENERATED_BODY()
public:
	AShoterWeapon_Ripper();
};

/** Energy shotgun: 9 pellets, devastating up close. */
UCLASS()
class SHOTERAAA_API AShoterWeapon_Scatter : public AShoterWeapon
{
	GENERATED_BODY()
public:
	AShoterWeapon_Scatter();
};

/** Rail sniper: scoped, one-shot headshots, instant beam. */
UCLASS()
class SHOTERAAA_API AShoterWeapon_Rail : public AShoterWeapon
{
	GENERATED_BODY()
public:
	AShoterWeapon_Rail();
};

/** Plasma launcher: slow explosive orbs with splash damage. */
UCLASS()
class SHOTERAAA_API AShoterWeapon_PlasmaLauncher : public AShoterWeapon
{
	GENERATED_BODY()
public:
	AShoterWeapon_PlasmaLauncher();
};
