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

	/**
	 * Iron sights, kept in their own part list so a future optic can replace them. A U-notch rear sight (two short
	 * posts) near the back and a thin front post near the muzzle, both ending at the same height TopZ (weapon space)
	 * on the centre line. The top centre of the front post is the aim point: Stats.SightOffset. The tips are marked
	 * with a faint night-sight glow. Everything stays below TopZ so the line above the aim point is clear.
	 */
	void AddIronSights(TArray<FShoterWeaponPart>& SightParts, FShoterWeaponStats& Stats, float RearX, float RearBaseTop, float FrontX, float FrontBaseTop, float TopZ,
		float FrontWidth = 0.35f, float NotchHalfGap = 0.f)
	{
		constexpr float RearWidth = 0.5f;
		constexpr float PostDepth = 0.5f;
		constexpr float Mark = 0.35f;
		// The gap should look about 5x the front post on screen: the rear sight is nearer, so it needs less width.
		const float EstimatedRearDistance = 48.f;
		if (NotchHalfGap <= 0.f)
		{
			NotchHalfGap = FMath::Max(0.45f, 2.5f * FrontWidth * EstimatedRearDistance / (EstimatedRearDistance + (FrontX - RearX)));
		}

		for (const float Side : { -1.f, 1.f })
		{
			const float Y = Side * (NotchHalfGap + RearWidth * 0.5f);
			const float Bottom = RearBaseTop - 0.3f;
			SightParts.Add(Box(FVector(RearX, Y, (Bottom + TopZ - Mark) * 0.5f), FVector(PostDepth, RearWidth, TopZ - Mark - Bottom), EPM::Dark));
			SightParts.Add(Box(FVector(RearX, Y, TopZ - Mark * 0.5f), FVector(PostDepth, RearWidth, Mark), EPM::Reticle));
		}
		// Front post with a small base block on the barrel.
		SightParts.Add(Box(FVector(FrontX, 0.f, FrontBaseTop + 0.2f), FVector(1.4f, 1.0f, 0.9f), EPM::Dark));
		const float FrontBottom = FrontBaseTop;
		SightParts.Add(Box(FVector(FrontX, 0.f, (FrontBottom + TopZ - Mark * 1.4f) * 0.5f), FVector(0.4f, FrontWidth, TopZ - Mark * 1.4f - FrontBottom), EPM::Dark));
		SightParts.Add(Box(FVector(FrontX, 0.f, TopZ - Mark * 0.7f), FVector(0.4f, FrontWidth, Mark * 1.4f), EPM::Reticle));
		Stats.SightOffset = FVector(FrontX, 0.f, TopZ);
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
	Stats.HipSpread = 0.9f;
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
	Stats.BoltLength = 90.f;
	Stats.EjectOffset = FVector(6.f, 1.9f, 6.f);
	Stats.CasingLength = 2.6f;
	Stats.CasingDiameter = 0.9f;
	Stats.HipOffset = FVector(30.f, 13.f, -15.f);
	Stats.AimOffset = FVector(22.f, 0.f, -8.8f);
	Stats.BuyCost = 300;

	// Hands: right on the raked grip, left wrapped over it from the other side. Reload: the cell slides out of the grip.
	Stats.RightGrip = FShoterHandGrip(FVector(-1.8f, 3.3f, -3.4f), FRotator(-12.f, 0.f, 0.f), 0.8f);
	Stats.LeftGrip = FShoterHandGrip(FVector(0.6f, -3.4f, -4.6f), FRotator(-12.f, 0.f, 0.f), 0.7f);
	Stats.Reload.MagGrip = FVector(-2.0f, -3.3f, -9.6f);
	Stats.Reload.MagHandRot = FRotator(-12.f, 0.f, 0.f);
	Stats.Reload.MagCenter = FVector(-1.04f, 0.f, -5.2f);
	Stats.Reload.MagSize = FVector(3.4f, 2.7f, 10.4f);
	Stats.Reload.MagRotation = FRotator(-12.f, 0.f, 0.f);
	Stats.Reload.MagOut = FVector(-0.208f, 0.f, -0.978f);
	Stats.Reload.MagTravel = 10.8f;
	Stats.Reload.HandDrop = FVector(-3.f, -4.f, -36.f);
	Stats.Reload.bCharge = true;
	Stats.Reload.ChargeLocation = FVector(1.5f, -2.9f, 5.6f);
	Stats.Reload.ChargeStroke = 6.f;

	Parts = {
		Box(FVector(8.f, 0.f, 5.f), FVector(20.f, 3.6f, 5.f), EPM::Body),
		Box(FVector(5.f, 0.f, 1.5f), FVector(16.f, 3.2f, 3.f), EPM::Dark),
		Tube(FVector(19.f, 0.f, 5.f), 2.f, 6.f, EPM::Dark),
		Box(FVector(-1.f, 0.f, -5.f), FVector(4.5f, 3.4f, 10.f), EPM::Dark, FRotator(-12.f, 0.f, 0.f)),
		Box(FVector(6.f, 0.f, -0.5f), FVector(5.f, 3.f, 1.f), EPM::Accent),
		Box(FVector(8.f, 1.9f, 5.5f), FVector(14.f, 0.3f, 1.f), EPM::Glow),
		Box(FVector(8.f, -1.9f, 5.5f), FVector(14.f, 0.3f, 1.f), EPM::Glow),
		Tube(FVector(-2.5f, 0.f, 5.5f), 2.5f, 3.f, EPM::Glow),
	};
	// Energy cell inside the grip: only its glowing base plate shows until it is pulled out.
	MagParts = {
		Box(FVector(-1.04f, 0.f, -5.2f), FVector(3.4f, 2.7f, 10.4f), EPM::Accent, FRotator(-12.f, 0.f, 0.f)),
		Box(FVector(-2.12f, 0.f, -10.3f), FVector(4.0f, 3.0f, 0.7f), EPM::Glow, FRotator(-12.f, 0.f, 0.f)),
	};
	AddIronSights(SightParts, Stats, 0.f, 7.5f, 16.5f, 7.5f, 8.6f);
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
	Stats.HipSpread = 1.3f;
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
	Stats.EjectOffset = FVector(14.f, 3.1f, 7.f);
	Stats.CasingLength = 3.4f;
	Stats.CasingDiameter = 1.f;
	Stats.HipOffset = FVector(30.f, 16.f, -18.f);
	Stats.AimOffset = FVector(13.f, 0.f, -13.4f);
	Stats.BuyCost = 1200;

	Stats.RightGrip = FShoterHandGrip(FVector(-5.4f, 3.4f, -5.4f), FRotator(-15.f, 0.f, 0.f), 0.8f);
	Stats.LeftGrip = FShoterHandGrip(FVector(31.4f, -0.4f, 1.1f), FRotator(0.f, 62.f, 75.f), 0.85f);
	Stats.Reload.MagGrip = FVector(6.4f, -3.9f, -7.8f);
	Stats.Reload.MagCenter = FVector(6.f, 0.f, -7.f);
	Stats.Reload.MagSize = FVector(6.f, 4.f, 12.f);
	Stats.Reload.MagRotation = FRotator(8.f, 0.f, 0.f);
	Stats.Reload.MagOut = FVector(0.139f, 0.f, -0.990f);
	Stats.Reload.MagTravel = 13.f;
	Stats.Reload.bCharge = true;
	Stats.Reload.ChargeLocation = FVector(4.f, -3.9f, 8.6f);
	Stats.Reload.ChargeStroke = 8.f;

	Parts = {
		Box(FVector(10.f, 0.f, 4.f), FVector(40.f, 6.f, 9.f), EPM::Body),
		Box(FVector(18.f, 0.f, 9.5f), FVector(30.f, 5.5f, 3.f), EPM::Dark),
		Tube(FVector(42.f, 0.f, 5.f), 2.6f, 26.f, EPM::Dark),
		Tube(FVector(34.f, 0.f, 5.f), 4.2f, 12.f, EPM::Accent),
		Tube(FVector(30.f, 0.f, 5.f), 4.7f, 1.2f, EPM::Glow),
		Tube(FVector(34.f, 0.f, 5.f), 4.7f, 1.2f, EPM::Glow),
		Tube(FVector(38.f, 0.f, 5.f), 4.7f, 1.2f, EPM::Glow),
		Tube(FVector(55.f, 0.f, 5.f), 3.4f, 3.f, EPM::Dark),
		Box(FVector(-4.f, 0.f, -6.f), FVector(4.f, 3.5f, 11.f), EPM::Dark, FRotator(-15.f, 0.f, 0.f)),
		Box(FVector(-20.f, 0.f, 3.f), FVector(18.f, 4.5f, 7.f), EPM::Body),
		Box(FVector(-29.5f, 0.f, 2.f), FVector(2.f, 5.f, 9.f), EPM::Dark),
		Box(FVector(12.f, 3.1f, 4.f), FVector(26.f, 0.4f, 1.2f), EPM::Glow),
		Box(FVector(12.f, -3.1f, 4.f), FVector(26.f, 0.4f, 1.2f), EPM::Glow),
	};
	MagParts = {
		Box(FVector(6.f, 0.f, -7.f), FVector(6.f, 4.f, 12.f), EPM::Accent, FRotator(8.f, 0.f, 0.f)),
		Box(FVector(6.f, 0.f, -2.5f), FVector(6.4f, 4.4f, 1.5f), EPM::Glow),
		Box(FVector(6.3f, 2.05f, -7.5f), FVector(4.f, 0.3f, 7.5f), EPM::Glow, FRotator(8.f, 0.f, 0.f)),
		Box(FVector(6.3f, -2.05f, -7.5f), FVector(4.f, 0.3f, 7.5f), EPM::Glow, FRotator(8.f, 0.f, 0.f)),
	};
	AddIronSights(SightParts, Stats, 4.f, 11.f, 50.f, 6.3f, 12.3f);
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
	Stats.BoltLength = 90.f;
	Stats.EjectOffset = FVector(8.f, 2.9f, 5.8f);
	Stats.CasingLength = 2.8f;
	Stats.CasingDiameter = 0.9f;
	Stats.HipOffset = FVector(28.f, 15.f, -16.f);
	Stats.AimOffset = FVector(12.f, 0.f, -11.3f);
	Stats.BuyCost = 900;

	Stats.RightGrip = FShoterHandGrip(FVector(-5.4f, 3.3f, -5.f), FRotator(-15.f, 0.f, 0.f), 0.8f);
	Stats.LeftGrip = FShoterHandGrip(FVector(16.f, -3.3f, -4.2f), FRotator::ZeroRotator, 0.9f);
	Stats.Reload.MagGrip = FVector(8.2f, -3.8f, -9.5f);
	Stats.Reload.MagCenter = FVector(8.f, 0.f, -9.f);
	Stats.Reload.MagSize = FVector(4.f, 3.5f, 14.f);
	Stats.Reload.MagOut = FVector(0.f, 0.f, -1.f);
	Stats.Reload.MagTravel = 14.5f;
	Stats.Reload.bCharge = true;
	Stats.Reload.ChargeLocation = FVector(3.f, -3.7f, 6.f);
	Stats.Reload.ChargeStroke = 6.f;

	Parts = {
		Box(FVector(6.f, 0.f, 3.f), FVector(26.f, 5.5f, 8.f), EPM::Body),
		Tube(FVector(24.f, 0.f, 4.f), 2.2f, 10.f, EPM::Dark),
		Tube(FVector(21.f, 0.f, 4.f), 4.f, 6.f, EPM::Accent),
		Box(FVector(-4.f, 0.f, -6.f), FVector(4.f, 3.4f, 10.f), EPM::Dark, FRotator(-15.f, 0.f, 0.f)),
		Box(FVector(16.f, 0.f, -4.f), FVector(3.f, 3.f, 7.f), EPM::Dark),
		Box(FVector(-13.f, 0.f, 2.f), FVector(10.f, 2.f, 4.f), EPM::Dark),
		Box(FVector(6.f, 2.9f, 3.f), FVector(20.f, 0.3f, 1.f), EPM::Glow),
		Box(FVector(6.f, -2.9f, 3.f), FVector(20.f, 0.3f, 1.f), EPM::Glow),
	};
	MagParts = {
		Box(FVector(8.f, 0.f, -9.f), FVector(4.f, 3.5f, 14.f), EPM::Accent),
		Box(FVector(8.f, 0.f, -3.5f), FVector(4.4f, 3.9f, 1.f), EPM::Glow),
		Box(FVector(8.f, 1.8f, -10.f), FVector(2.6f, 0.3f, 9.f), EPM::Glow),
		Box(FVector(8.f, -1.8f, -10.f), FVector(2.6f, 0.3f, 9.f), EPM::Glow),
	};
	AddIronSights(SightParts, Stats, -3.f, 7.f, 18.f, 7.f, 8.2f);
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
	Stats.BoltLength = 120.f;
	Stats.EjectOffset = FVector(12.f, 3.6f, 6.5f);
	Stats.CasingLength = 5.5f;
	Stats.CasingDiameter = 1.9f;
	Stats.HipOffset = FVector(30.f, 16.f, -18.f);
	Stats.AimOffset = FVector(14.f, 0.f, -12.f);
	Stats.BuyCost = 1500;

	// One chunky cell under the receiver, swapped in one go (no shell by shell loading), then the pump is racked.
	Stats.RightGrip = FShoterHandGrip(FVector(-5.4f, 3.4f, -5.5f), FRotator(-15.f, 0.f, 0.f), 0.8f);
	Stats.LeftGrip = FShoterHandGrip(FVector(26.f, 0.f, -3.8f), FRotator(0.f, 62.f, 75.f), 0.9f);
	Stats.Reload.MagGrip = FVector(19.f, -4.f, -5.8f);
	Stats.Reload.MagCenter = FVector(19.f, 0.f, -5.5f);
	Stats.Reload.MagSize = FVector(8.f, 5.f, 7.f);
	Stats.Reload.MagOut = FVector(0.f, 0.f, -1.f);
	Stats.Reload.MagTravel = 8.f;
	Stats.Reload.HandDrop = FVector(-3.f, -4.f, -38.f);
	Stats.Reload.bCharge = true;
	Stats.Reload.ChargeLocation = FVector(26.f, 0.f, -3.8f);
	Stats.Reload.ChargeRotation = FRotator(0.f, 62.f, 75.f);
	Stats.Reload.ChargeStroke = 9.f;
	Stats.Reload.Grab = 0.12f;
	Stats.Reload.Pull = 0.26f;
	Stats.Reload.Back = 0.58f;
	Stats.Reload.Seat = 0.68f;
	Stats.Reload.Charge = 0.90f;

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
	};
	MagParts = {
		Box(FVector(19.f, 0.f, -5.5f), FVector(8.f, 5.f, 7.f), EPM::Accent),
		Box(FVector(19.f, 2.55f, -5.5f), FVector(5.5f, 0.4f, 4.5f), EPM::Glow),
		Box(FVector(19.f, -2.55f, -5.5f), FVector(5.5f, 0.4f, 4.5f), EPM::Glow),
	};
	AddIronSights(SightParts, Stats, -3.f, 8.5f, 45.f, 7.5f, 10.4f, 0.6f, 1.6f);
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
	Stats.EjectOffset = FVector(10.f, 2.6f, 6.5f);
	Stats.CasingLength = 7.f;
	Stats.CasingDiameter = 2.4f;
	Stats.HipOffset = FVector(32.f, 16.f, -18.f);
	Stats.AimOffset = FVector(10.f, 0.f, -12.f);
	Stats.BuyCost = 2000;

	// Long, heavy cell; slower choreography; no racking.
	Stats.RightGrip = FShoterHandGrip(FVector(-5.4f, 3.4f, -5.2f), FRotator(-15.f, 0.f, 0.f), 0.8f);
	Stats.LeftGrip = FShoterHandGrip(FVector(36.f, 0.2f, -0.4f), FRotator(0.f, 62.f, 75.f), 0.85f);
	Stats.Reload.MagGrip = FVector(6.f, -3.9f, -6.8f);
	Stats.Reload.MagCenter = FVector(6.f, 0.f, -6.5f);
	Stats.Reload.MagSize = FVector(9.f, 4.f, 9.f);
	Stats.Reload.MagOut = FVector(0.f, 0.f, -1.f);
	Stats.Reload.MagTravel = 10.f;
	Stats.Reload.HandDrop = FVector(-4.f, -4.f, -44.f);
	Stats.Reload.Grab = 0.16f;
	Stats.Reload.Pull = 0.34f;
	Stats.Reload.Away = 0.46f;
	Stats.Reload.Back = 0.68f;
	Stats.Reload.Seat = 0.80f;
	Stats.Reload.Charge = 0.80f;
	Stats.Reload.TiltScale = 0.8f;

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
		Box(FVector(-4.f, 0.f, -6.f), FVector(4.f, 3.5f, 11.f), EPM::Dark, FRotator(-15.f, 0.f, 0.f)),
		Box(FVector(-22.f, 0.f, 2.f), FVector(22.f, 5.f, 9.f), EPM::Body),
		Box(FVector(-18.f, 0.f, 7.f), FVector(12.f, 4.f, 2.f), EPM::Accent),
	};
	MagParts = {
		Box(FVector(6.f, 0.f, -6.5f), FVector(9.f, 4.f, 9.f), EPM::Accent),
		Box(FVector(6.f, 2.05f, -6.5f), FVector(7.f, 0.3f, 7.f), EPM::Glow),
		Box(FVector(6.f, -2.05f, -6.5f), FVector(7.f, 0.3f, 7.f), EPM::Glow),
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
	Stats.EjectOffset = FVector(-2.f, 6.3f, 9.f);
	Stats.CasingLength = 7.f;
	Stats.CasingDiameter = 3.f;
	Stats.HipOffset = FVector(34.f, 18.f, -20.f);
	Stats.AimOffset = FVector(16.f, 0.f, -15.5f);
	Stats.BuyCost = 2500;

	// Big canister at the rear (never above the sight line), pulled straight back.
	Stats.RightGrip = FShoterHandGrip(FVector(-5.4f, 3.4f, -4.f), FRotator(-15.f, 0.f, 0.f), 0.8f);
	Stats.LeftGrip = FShoterHandGrip(FVector(22.f, -3.3f, -2.2f), FRotator::ZeroRotator, 0.9f);
	Stats.Reload.MagGrip = FVector(-19.f, -6.6f, 6.5f);
	Stats.Reload.MagCenter = FVector(-19.f, 0.f, 6.f);
	Stats.Reload.MagSize = FVector(12.f, 9.5f, 9.5f);
	Stats.Reload.MagOut = FVector(-1.f, 0.f, 0.f);
	Stats.Reload.MagTravel = 14.f;
	Stats.Reload.HandDrop = FVector(-3.f, -4.f, -38.f);
	Stats.Reload.Grab = 0.14f;
	Stats.Reload.Pull = 0.32f;
	Stats.Reload.Away = 0.44f;
	Stats.Reload.Back = 0.64f;
	Stats.Reload.Seat = 0.76f;
	Stats.Reload.Charge = 0.76f;

	Parts = {
		Tube(FVector(14.f, 0.f, 6.f), 12.f, 50.f, EPM::Body),
		Tube(FVector(39.f, 0.f, 6.f), 13.f, 3.f, EPM::Accent),
		Tube(FVector(40.6f, 0.f, 6.f), 9.f, 0.6f, EPM::Glow),
		Tube(FVector(-11.f, 0.f, 6.f), 12.5f, 3.f, EPM::Accent),
		Tube(FVector(8.f, 7.f, 6.f), 4.f, 14.f, EPM::Glow),
		Tube(FVector(8.f, -7.f, 6.f), 4.f, 14.f, EPM::Glow),
		Box(FVector(-4.f, 0.f, -4.f), FVector(4.f, 3.5f, 10.f), EPM::Dark, FRotator(-15.f, 0.f, 0.f)),
		Box(FVector(22.f, 0.f, -2.f), FVector(3.f, 3.f, 8.f), EPM::Dark),
	};
	MagParts = {
		Tube(FVector(-19.f, 0.f, 6.f), 9.5f, 12.f, EPM::Accent),
		Tube(FVector(-25.3f, 0.f, 6.f), 10.2f, 1.2f, EPM::Glow),
		Tube(FVector(-13.4f, 0.f, 6.f), 10.f, 1.f, EPM::Glow),
	};
	AddIronSights(SightParts, Stats, -6.f, 12.f, 39.f, 12.5f, 15.5f);
}

#undef LOCTEXT_NAMESPACE
