// Weapon tuning. Effective health is 200 (100 shield + 100 health).
#include "Weapons/ShoterWeapons.h"
#include "Weapons/ShoterProjectile.h"

#define LOCTEXT_NAMESPACE "ShoterWeapons"

namespace
{
	using EPM = EShoterPartMaterial;

	/** Basic cylinders and cones point up (Z); this makes them point forward (X). */
	const FRotator AlongX(-90.f, 0.f, 0.f);

	FShoterWeaponPart Box(const FVector& Location, const FVector& Size, EPM Material, const FRotator& Rotation = FRotator::ZeroRotator)
	{
		return FShoterWeaponPart(EShoterShape::Cube, Location, Size, Material, Rotation);
	}

	FShoterWeaponPart Tube(const FVector& Location, float Diameter, float Length, EPM Material)
	{
		return FShoterWeaponPart(EShoterShape::Cylinder, Location, FVector(Diameter, Diameter, Length), Material, AlongX);
	}
}

// ---------------------------------------------------------------------------
// Pulse Pistol: 7 body shots / 4 headshots
// ---------------------------------------------------------------------------
AShoterWeapon_PulsePistol::AShoterWeapon_PulsePistol()
{
	Stats.DisplayName = LOCTEXT("PulsePistol", "PULSE PISTOL");
	Stats.Type = EShoterWeaponType::Pistol;
	Stats.EnergyColor = FLinearColor(0.2f, 1.f, 0.55f);
	Stats.BodyColor = FLinearColor(0.82f, 0.84f, 0.86f);
	Stats.AccentColor = FLinearColor(0.08f, 0.1f, 0.12f);

	Stats.Damage = 32.f;
	Stats.HeadshotMultiplier = 2.f;
	Stats.Range = 15000.f;
	Stats.FalloffStart = 1800.f;
	Stats.FalloffEnd = 4500.f;
	Stats.MinDamageScale = 0.6f;
	Stats.FireRate = 400.f;
	Stats.bAutomatic = false;
	Stats.MagSize = 12;
	Stats.MaxReserve = 84;
	Stats.ReloadTime = 1.35f;
	Stats.HipSpread = 1.3f;
	Stats.AimSpread = 0.15f;
	Stats.SpreadPerShot = 0.7f;
	Stats.MaxBloom = 3.f;
	Stats.BloomRecovery = 9.f;
	Stats.MovingSpread = 1.f;
	Stats.AirSpread = 2.5f;
	Stats.RecoilPitch = 1.2f;
	Stats.RecoilYaw = 0.35f;
	Stats.AimRecoilScale = 0.7f;
	Stats.KickBack = 3.5f;
	Stats.AimFOV = 82.f;
	Stats.AimTime = 0.14f;
	Stats.EquipTime = 0.3f;
	Stats.MoveSpeedScale = 1.05f;
	Stats.FireSound = TEXT("PulsePistol_Fire");
	Stats.FireSoundRange = EShoterSoundRange::Medium;
	Stats.TracerWidth = 1.f;
	Stats.MuzzleOffset = FVector(23.f, 0.f, 5.f);
	Stats.HipOffset = FVector(30.f, 13.f, -15.f);
	Stats.AimOffset = FVector(22.f, 0.f, -8.8f);
	Stats.BuyCost = 300;

	Parts = {
		Box(FVector(8.f, 0.f, 5.f), FVector(20.f, 3.6f, 5.f), EPM::Body),
		Box(FVector(5.f, 0.f, 1.5f), FVector(16.f, 3.2f, 3.f), EPM::Dark),
		Tube(FVector(19.f, 0.f, 5.f), 2.f, 6.f, EPM::Dark),
		Box(FVector(-1.f, 0.f, -5.f), FVector(4.5f, 3.4f, 10.f), EPM::Dark, FRotator(-12.f, 0.f, 0.f)),
		Box(FVector(6.f, 0.f, -0.5f), FVector(5.f, 3.f, 1.f), EPM::Accent),
		Box(FVector(8.f, 1.9f, 5.5f), FVector(14.f, 0.3f, 1.f), EPM::Glow),
		Box(FVector(8.f, -1.9f, 5.5f), FVector(14.f, 0.3f, 1.f), EPM::Glow),
		Tube(FVector(-2.5f, 0.f, 5.5f), 2.5f, 3.f, EPM::Glow),
		Box(FVector(0.f, 0.f, 8.f), FVector(1.f, 2.5f, 1.2f), EPM::Dark),
		Box(FVector(16.f, 0.f, 8.f), FVector(1.f, 1.f, 1.2f), EPM::Glow),
	};
}

// ---------------------------------------------------------------------------
// Pulse Rifle: 8 body shots (~0.7 s) / 4 headshots
// ---------------------------------------------------------------------------
AShoterWeapon_PulseRifle::AShoterWeapon_PulseRifle()
{
	Stats.DisplayName = LOCTEXT("PulseRifle", "PULSE RIFLE");
	Stats.Type = EShoterWeaponType::Rifle;
	Stats.EnergyColor = FLinearColor(0.1f, 0.85f, 1.f);
	Stats.BodyColor = FLinearColor(0.78f, 0.8f, 0.84f);
	Stats.AccentColor = FLinearColor(0.07f, 0.08f, 0.1f);

	Stats.Damage = 28.f;
	Stats.HeadshotMultiplier = 1.8f;
	Stats.Range = 25000.f;
	Stats.FalloffStart = 3000.f;
	Stats.FalloffEnd = 7000.f;
	Stats.MinDamageScale = 0.65f;
	Stats.FireRate = 620.f;
	Stats.bAutomatic = true;
	Stats.MagSize = 32;
	Stats.MaxReserve = 192;
	Stats.ReloadTime = 1.9f;
	Stats.HipSpread = 1.8f;
	Stats.AimSpread = 0.12f;
	Stats.SpreadPerShot = 0.3f;
	Stats.MaxBloom = 3.f;
	Stats.BloomRecovery = 9.f;
	Stats.MovingSpread = 1.5f;
	Stats.AirSpread = 3.5f;
	Stats.RecoilPitch = 0.75f;
	Stats.RecoilYaw = 0.3f;
	Stats.AimRecoilScale = 0.6f;
	Stats.KickBack = 2.5f;
	Stats.AimFOV = 70.f;
	Stats.AimTime = 0.2f;
	Stats.EquipTime = 0.5f;
	Stats.MoveSpeedScale = 0.97f;
	Stats.FireSound = TEXT("PulseRifle_Fire");
	Stats.TracerWidth = 1.2f;
	Stats.MuzzleOffset = FVector(57.f, 0.f, 5.f);
	Stats.HipOffset = FVector(30.f, 16.f, -18.f);
	Stats.AimOffset = FVector(13.f, 0.f, -13.4f);
	Stats.BuyCost = 1200;

	Parts = {
		Box(FVector(10.f, 0.f, 4.f), FVector(40.f, 6.f, 9.f), EPM::Body),
		Box(FVector(18.f, 0.f, 9.5f), FVector(30.f, 5.5f, 3.f), EPM::Dark),
		Tube(FVector(42.f, 0.f, 5.f), 2.6f, 26.f, EPM::Dark),
		Tube(FVector(34.f, 0.f, 5.f), 4.2f, 12.f, EPM::Accent),
		Tube(FVector(30.f, 0.f, 5.f), 4.7f, 1.2f, EPM::Glow),
		Tube(FVector(34.f, 0.f, 5.f), 4.7f, 1.2f, EPM::Glow),
		Tube(FVector(38.f, 0.f, 5.f), 4.7f, 1.2f, EPM::Glow),
		Tube(FVector(55.f, 0.f, 5.f), 3.4f, 3.f, EPM::Dark),
		Box(FVector(6.f, 0.f, -7.f), FVector(6.f, 4.f, 12.f), EPM::Accent, FRotator(8.f, 0.f, 0.f)),
		Box(FVector(6.f, 0.f, -2.5f), FVector(6.4f, 4.4f, 1.5f), EPM::Glow),
		Box(FVector(-4.f, 0.f, -6.f), FVector(4.f, 3.5f, 11.f), EPM::Dark, FRotator(-15.f, 0.f, 0.f)),
		Box(FVector(-20.f, 0.f, 3.f), FVector(18.f, 4.5f, 7.f), EPM::Body),
		Box(FVector(-29.5f, 0.f, 2.f), FVector(2.f, 5.f, 9.f), EPM::Dark),
		Box(FVector(12.f, 3.1f, 4.f), FVector(26.f, 0.4f, 1.2f), EPM::Glow),
		Box(FVector(12.f, -3.1f, 4.f), FVector(26.f, 0.4f, 1.2f), EPM::Glow),
		Box(FVector(14.f, 0.f, 12.f), FVector(8.f, 3.f, 2.5f), EPM::Dark),
		Box(FVector(17.5f, 0.f, 13.6f), FVector(0.4f, 2.f, 1.6f), EPM::Glow),
	};
}

// ---------------------------------------------------------------------------
// Ripper SMG: 11 body shots (~0.67 s), short range
// ---------------------------------------------------------------------------
AShoterWeapon_Ripper::AShoterWeapon_Ripper()
{
	Stats.DisplayName = LOCTEXT("Ripper", "RIPPER");
	Stats.Type = EShoterWeaponType::SMG;
	Stats.EnergyColor = FLinearColor(0.85f, 0.2f, 1.f);
	Stats.BodyColor = FLinearColor(0.22f, 0.22f, 0.26f);
	Stats.AccentColor = FLinearColor(0.55f, 0.56f, 0.6f);

	Stats.Damage = 19.f;
	Stats.HeadshotMultiplier = 1.6f;
	Stats.Range = 12000.f;
	Stats.FalloffStart = 1200.f;
	Stats.FalloffEnd = 3500.f;
	Stats.MinDamageScale = 0.5f;
	Stats.FireRate = 900.f;
	Stats.bAutomatic = true;
	Stats.MagSize = 40;
	Stats.MaxReserve = 240;
	Stats.ReloadTime = 1.7f;
	Stats.HipSpread = 2.2f;
	Stats.AimSpread = 0.6f;
	Stats.SpreadPerShot = 0.25f;
	Stats.MaxBloom = 3.5f;
	Stats.BloomRecovery = 10.f;
	Stats.MovingSpread = 0.8f;
	Stats.AirSpread = 2.5f;
	Stats.RecoilPitch = 0.5f;
	Stats.RecoilYaw = 0.45f;
	Stats.AimRecoilScale = 0.7f;
	Stats.KickBack = 1.8f;
	Stats.AimFOV = 78.f;
	Stats.AimTime = 0.15f;
	Stats.EquipTime = 0.35f;
	Stats.MoveSpeedScale = 1.03f;
	Stats.FireSound = TEXT("Ripper_Fire");
	Stats.TracerWidth = 1.f;
	Stats.MuzzleOffset = FVector(30.f, 0.f, 4.f);
	Stats.HipOffset = FVector(28.f, 15.f, -16.f);
	Stats.AimOffset = FVector(12.f, 0.f, -11.3f);
	Stats.BuyCost = 900;

	Parts = {
		Box(FVector(6.f, 0.f, 3.f), FVector(26.f, 5.5f, 8.f), EPM::Body),
		Tube(FVector(24.f, 0.f, 4.f), 2.2f, 10.f, EPM::Dark),
		Tube(FVector(21.f, 0.f, 4.f), 4.f, 6.f, EPM::Accent),
		Box(FVector(8.f, 0.f, -9.f), FVector(4.f, 3.5f, 14.f), EPM::Accent),
		Box(FVector(8.f, 0.f, -3.5f), FVector(4.4f, 3.9f, 1.f), EPM::Glow),
		Box(FVector(-4.f, 0.f, -6.f), FVector(4.f, 3.4f, 10.f), EPM::Dark, FRotator(-15.f, 0.f, 0.f)),
		Box(FVector(16.f, 0.f, -4.f), FVector(3.f, 3.f, 7.f), EPM::Dark),
		Box(FVector(-13.f, 0.f, 2.f), FVector(10.f, 2.f, 4.f), EPM::Dark),
		Box(FVector(6.f, 2.9f, 3.f), FVector(20.f, 0.3f, 1.f), EPM::Glow),
		Box(FVector(6.f, -2.9f, 3.f), FVector(20.f, 0.3f, 1.f), EPM::Glow),
		Box(FVector(8.f, 0.f, 9.f), FVector(6.f, 2.6f, 2.f), EPM::Dark),
		Box(FVector(10.5f, 0.f, 10.6f), FVector(0.4f, 1.8f, 1.4f), EPM::Glow),
	};
}

// ---------------------------------------------------------------------------
// Scatter shotgun: 9 pellets x 17 = 153 point blank
// ---------------------------------------------------------------------------
AShoterWeapon_Scatter::AShoterWeapon_Scatter()
{
	Stats.DisplayName = LOCTEXT("Scatter", "SCATTER");
	Stats.Type = EShoterWeaponType::Shotgun;
	Stats.EnergyColor = FLinearColor(1.f, 0.5f, 0.1f);
	Stats.BodyColor = FLinearColor(0.3f, 0.32f, 0.35f);
	Stats.AccentColor = FLinearColor(0.85f, 0.45f, 0.12f);

	Stats.Damage = 17.f;
	Stats.Pellets = 9;
	Stats.HeadshotMultiplier = 1.5f;
	Stats.Range = 4000.f;
	Stats.FalloffStart = 600.f;
	Stats.FalloffEnd = 1600.f;
	Stats.MinDamageScale = 0.25f;
	Stats.FireRate = 75.f;
	Stats.bAutomatic = false;
	Stats.MagSize = 6;
	Stats.MaxReserve = 36;
	Stats.ReloadTime = 2.3f;
	Stats.HipSpread = 5.5f;
	Stats.AimSpread = 4.f;
	Stats.SpreadPerShot = 1.f;
	Stats.MaxBloom = 2.f;
	Stats.BloomRecovery = 4.f;
	Stats.MovingSpread = 0.5f;
	Stats.AirSpread = 1.5f;
	Stats.RecoilPitch = 4.f;
	Stats.RecoilYaw = 1.f;
	Stats.AimRecoilScale = 0.8f;
	Stats.KickBack = 8.f;
	Stats.AimFOV = 85.f;
	Stats.AimTime = 0.2f;
	Stats.EquipTime = 0.55f;
	Stats.MoveSpeedScale = 0.95f;
	Stats.FireSound = TEXT("Scatter_Fire");
	Stats.TracerWidth = 0.8f;
	Stats.MuzzleOffset = FVector(47.f, 0.f, 5.f);
	Stats.HipOffset = FVector(30.f, 16.f, -18.f);
	Stats.AimOffset = FVector(14.f, 0.f, -12.f);
	Stats.BuyCost = 1500;

	Parts = {
		Box(FVector(10.f, 0.f, 4.f), FVector(34.f, 7.f, 9.f), EPM::Body),
		Tube(FVector(34.f, -1.8f, 5.f), 3.f, 22.f, EPM::Dark),
		Tube(FVector(34.f, 1.8f, 5.f), 3.f, 22.f, EPM::Dark),
		Box(FVector(45.f, 0.f, 5.f), FVector(3.f, 8.f, 5.f), EPM::Accent),
		Box(FVector(30.f, 0.f, 9.f), FVector(14.f, 5.f, 0.6f), EPM::Glow),
		Box(FVector(24.f, 0.f, 0.f), FVector(12.f, 5.f, 4.f), EPM::Dark),
		Box(FVector(14.f, 0.f, -1.f), FVector(10.f, 7.4f, 2.f), EPM::Glow),
		Box(FVector(-4.f, 0.f, -6.f), FVector(4.5f, 3.5f, 11.f), EPM::Dark, FRotator(-15.f, 0.f, 0.f)),
		Box(FVector(-20.f, 0.f, 2.f), FVector(18.f, 5.f, 8.f), EPM::Body),
		Box(FVector(-29.5f, 0.f, 1.f), FVector(2.f, 5.5f, 10.f), EPM::Accent),
		Box(FVector(12.f, 0.f, 10.5f), FVector(6.f, 2.f, 2.f), EPM::Dark),
	};
}

// ---------------------------------------------------------------------------
// Rail sniper: 2 body shots, 1 headshot
// ---------------------------------------------------------------------------
AShoterWeapon_Rail::AShoterWeapon_Rail()
{
	Stats.DisplayName = LOCTEXT("Rail", "RAIL");
	Stats.Type = EShoterWeaponType::Sniper;
	Stats.EnergyColor = FLinearColor(0.45f, 0.65f, 1.f);
	Stats.BodyColor = FLinearColor(0.9f, 0.91f, 0.93f);
	Stats.AccentColor = FLinearColor(0.1f, 0.12f, 0.16f);

	Stats.Damage = 110.f;
	Stats.HeadshotMultiplier = 2.5f;
	Stats.Range = 40000.f;
	Stats.FalloffStart = 50000.f;
	Stats.FalloffEnd = 60000.f;
	Stats.MinDamageScale = 1.f;
	Stats.FireRate = 45.f;
	Stats.bAutomatic = false;
	Stats.MagSize = 4;
	Stats.MaxReserve = 24;
	Stats.ReloadTime = 2.6f;
	Stats.HipSpread = 6.f;
	Stats.AimSpread = 0.f;
	Stats.SpreadPerShot = 3.f;
	Stats.MaxBloom = 6.f;
	Stats.BloomRecovery = 6.f;
	Stats.MovingSpread = 2.f;
	Stats.AirSpread = 6.f;
	Stats.RecoilPitch = 5.f;
	Stats.RecoilYaw = 1.f;
	Stats.AimRecoilScale = 0.6f;
	Stats.KickBack = 10.f;
	Stats.AimFOV = 25.f;
	Stats.AimTime = 0.3f;
	Stats.EquipTime = 0.7f;
	Stats.MoveSpeedScale = 0.88f;
	Stats.bHasScope = true;
	Stats.FireSound = TEXT("Rail_Fire");
	Stats.FireSoundRange = EShoterSoundRange::VeryLong;
	Stats.bBeamTracer = true;
	Stats.TracerWidth = 2.2f;
	Stats.MuzzleOffset = FVector(76.f, 0.f, 5.f);
	Stats.HipOffset = FVector(32.f, 16.f, -18.f);
	Stats.AimOffset = FVector(10.f, 0.f, -12.f);
	Stats.BuyCost = 2000;

	Parts = {
		Box(FVector(12.f, 0.f, 4.f), FVector(46.f, 5.f, 8.f), EPM::Body),
		Tube(FVector(55.f, 0.f, 5.f), 2.8f, 40.f, EPM::Dark),
		Box(FVector(48.f, 0.f, 8.f), FVector(34.f, 4.f, 1.2f), EPM::Glow),
		Box(FVector(48.f, 0.f, 2.f), FVector(34.f, 4.f, 1.2f), EPM::Glow),
		Box(FVector(48.f, 0.f, 5.f), FVector(34.f, 1.f, 5.f), EPM::Accent),
		Tube(FVector(12.f, 0.f, 12.f), 4.5f, 20.f, EPM::Dark),
		Tube(FVector(22.3f, 0.f, 12.f), 4.f, 0.5f, EPM::Glow),
		Box(FVector(7.f, 0.f, 9.f), FVector(2.f, 2.f, 3.f), EPM::Dark),
		Box(FVector(17.f, 0.f, 9.f), FVector(2.f, 2.f, 3.f), EPM::Dark),
		Box(FVector(6.f, 0.f, -6.f), FVector(7.f, 3.5f, 8.f), EPM::Accent),
		Box(FVector(-4.f, 0.f, -6.f), FVector(4.f, 3.5f, 11.f), EPM::Dark, FRotator(-15.f, 0.f, 0.f)),
		Box(FVector(-22.f, 0.f, 2.f), FVector(22.f, 5.f, 9.f), EPM::Body),
		Box(FVector(-18.f, 0.f, 7.f), FVector(12.f, 4.f, 2.f), EPM::Accent),
	};
}

// ---------------------------------------------------------------------------
// Plasma launcher: explosive orbs (see AShoterPlasmaOrb)
// ---------------------------------------------------------------------------
AShoterWeapon_PlasmaLauncher::AShoterWeapon_PlasmaLauncher()
{
	Stats.DisplayName = LOCTEXT("PlasmaLauncher", "PLASMA LAUNCHER");
	Stats.Type = EShoterWeaponType::Launcher;
	Stats.EnergyColor = FLinearColor(0.4f, 1.f, 0.2f);
	Stats.BodyColor = FLinearColor(0.2f, 0.24f, 0.22f);
	Stats.AccentColor = FLinearColor(0.6f, 0.62f, 0.58f);

	Stats.ProjectileClass = AShoterPlasmaOrb::StaticClass();
	Stats.ProjectileSpeed = 3200.f;
	Stats.FireRate = 70.f;
	Stats.bAutomatic = false;
	Stats.MagSize = 4;
	Stats.MaxReserve = 16;
	Stats.ReloadTime = 2.5f;
	Stats.HipSpread = 0.5f;
	Stats.AimSpread = 0.2f;
	Stats.SpreadPerShot = 0.5f;
	Stats.MaxBloom = 1.5f;
	Stats.BloomRecovery = 4.f;
	Stats.MovingSpread = 0.3f;
	Stats.AirSpread = 0.8f;
	Stats.RecoilPitch = 3.5f;
	Stats.RecoilYaw = 0.8f;
	Stats.AimRecoilScale = 0.8f;
	Stats.KickBack = 9.f;
	Stats.AimFOV = 80.f;
	Stats.AimTime = 0.22f;
	Stats.EquipTime = 0.7f;
	Stats.MoveSpeedScale = 0.85f;
	Stats.FireSound = TEXT("Plasma_Fire");
	Stats.FireSoundRange = EShoterSoundRange::VeryLong;
	Stats.MuzzleOffset = FVector(42.f, 0.f, 6.f);
	Stats.HipOffset = FVector(34.f, 18.f, -20.f);
	Stats.AimOffset = FVector(16.f, 0.f, -15.5f);
	Stats.BuyCost = 2500;

	Parts = {
		Tube(FVector(14.f, 0.f, 6.f), 12.f, 50.f, EPM::Body),
		Tube(FVector(39.f, 0.f, 6.f), 13.f, 3.f, EPM::Accent),
		Tube(FVector(40.6f, 0.f, 6.f), 9.f, 0.6f, EPM::Glow),
		Tube(FVector(-11.f, 0.f, 6.f), 12.5f, 3.f, EPM::Accent),
		Tube(FVector(8.f, 7.f, 6.f), 4.f, 14.f, EPM::Glow),
		Tube(FVector(8.f, -7.f, 6.f), 4.f, 14.f, EPM::Glow),
		Box(FVector(-4.f, 0.f, -4.f), FVector(4.f, 3.5f, 10.f), EPM::Dark, FRotator(-15.f, 0.f, 0.f)),
		Box(FVector(22.f, 0.f, -2.f), FVector(3.f, 3.f, 8.f), EPM::Dark),
		Box(FVector(12.f, 0.f, 13.5f), FVector(8.f, 2.5f, 3.f), EPM::Dark),
		Box(FVector(15.f, 0.f, 15.5f), FVector(0.4f, 1.8f, 1.4f), EPM::Glow),
	};
}

#undef LOCTEXT_NAMESPACE
