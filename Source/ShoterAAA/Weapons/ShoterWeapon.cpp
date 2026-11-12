#include "Weapons/ShoterWeapon.h"
#include "ShoterAAA.h"
#include "Weapons/ShoterProjectile.h"
#include "Player/ShoterCharacter.h"
#include "Player/ShoterPlayerController.h"
#include "Player/ShoterPlayerState.h"
#include "Player/ShoterPlayerCameraManager.h"
#include "AI/ShoterZombie.h"
#include "Core/ShoterFX.h"
#include "Core/ShoterFXPool.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

namespace
{
	constexpr int32 MaxReportedPellets = 16;

	bool IsTargetAlive(const AActor* Target)
	{
		if (const AShoterCharacter* Character = Cast<AShoterCharacter>(Target))
		{
			return Character->IsAlive();
		}
		if (const AShoterZombie* Zombie = Cast<AShoterZombie>(Target))
		{
			return Zombie->IsAlive();
		}
		return false;
	}

	/** Client-side guess used only for hit markers: no marker on teammates. */
	bool IsLikelyEnemy(const AShoterCharacter* Shooter, const AActor* Target)
	{
		const AShoterCharacter* TargetCharacter = Cast<AShoterCharacter>(Target);
		if (!TargetCharacter || !Shooter)
		{
			return true;
		}
		const uint8 ShooterTeam = Shooter->GetTeam();
		return ShooterTeam == ShoterTeam::None || ShooterTeam != TargetCharacter->GetTeam();
	}

	struct FAggregatedHit
	{
		float Damage = 0.f;
		bool bHeadshot = false;
		FShoterHitReport Report;
	};
}

AShoterWeapon::AShoterWeapon()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bNetUseOwnerRelevancy = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	FirstPersonRoot = CreateDefaultSubobject<USceneComponent>(TEXT("FirstPersonRoot"));
	FirstPersonRoot->SetupAttachment(SceneRoot);

	ThirdPersonRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ThirdPersonRoot"));
	ThirdPersonRoot->SetupAttachment(SceneRoot);
}

void AShoterWeapon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AShoterWeapon, OwningCharacter);
	DOREPLIFETIME_CONDITION(AShoterWeapon, AmmoInClip, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AShoterWeapon, ReserveAmmo, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AShoterWeapon, bReloading, COND_SkipOwner);
}

void AShoterWeapon::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		AmmoInClip = Stats.MagSize;
		ReserveAmmo = Stats.MaxReserve;
	}

	BuildModel(FirstPersonRoot, FirstPersonParts, true);
	BuildModel(ThirdPersonRoot, ThirdPersonParts, false);
	bModelBuilt = true;

	UpdateAttachment();
	FirstPersonRoot->SetVisibility(bIsEquipped, true);
	ThirdPersonRoot->SetVisibility(bIsEquipped, true);
}

void AShoterWeapon::Destroyed()
{
	GetWorldTimerManager().ClearTimer(TimerHandle_Refire);
	GetWorldTimerManager().ClearTimer(TimerHandle_Reload);

	// The model roots may be attached to the owner's components; take them down with us.
	for (UStaticMeshComponent* Part : FirstPersonParts)
	{
		if (Part)
		{
			Part->DestroyComponent();
		}
	}
	for (UStaticMeshComponent* Part : ThirdPersonParts)
	{
		if (Part)
		{
			Part->DestroyComponent();
		}
	}
	if (FirstPersonMagRoot)
	{
		FirstPersonMagRoot->DestroyComponent();
	}
	if (ThirdPersonMagRoot)
	{
		ThirdPersonMagRoot->DestroyComponent();
	}
	Super::Destroyed();
}

void AShoterWeapon::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	SpreadBloom = FMath::FInterpConstantTo(SpreadBloom, 0.f, DeltaSeconds, Stats.BloomRecovery);
	KickOffset = FMath::VInterpTo(KickOffset, FVector::ZeroVector, DeltaSeconds, 14.f);
	KickRotation = FMath::RInterpTo(KickRotation, FRotator::ZeroRotator, DeltaSeconds, 12.f);

	if (bReloading || bReloadVisualsActive)
	{
		UpdateReloadVisuals();
	}
}

// ---------------------------------------------------------------------------
// Model
// ---------------------------------------------------------------------------

void AShoterWeapon::BuildModel(USceneComponent* Parent, TArray<TObjectPtr<UStaticMeshComponent>>& OutParts, bool bFirstPerson)
{
	UShoterAssets* Assets = UShoterAssets::Get(this);
	if (!Assets || !Parent)
	{
		return;
	}

	if (PartMaterials.Num() == 0)
	{
		PartMaterials.SetNum(5);
		PartMaterials[static_cast<int32>(EShoterPartMaterial::Body)] = Assets->MakeSurfaceMaterial(this, Stats.BodyColor, 0.75f, 0.32f);
		PartMaterials[static_cast<int32>(EShoterPartMaterial::Dark)] = Assets->MakeSurfaceMaterial(this, FLinearColor(0.025f, 0.027f, 0.03f), 0.9f, 0.45f);
		PartMaterials[static_cast<int32>(EShoterPartMaterial::Accent)] = Assets->MakeSurfaceMaterial(this, Stats.AccentColor, 0.6f, 0.3f);
		PartMaterials[static_cast<int32>(EShoterPartMaterial::Glow)] = Assets->MakeSurfaceMaterial(this, Stats.EnergyColor * 0.2f, 0.f, 0.3f, Stats.EnergyColor, 12.f);
		PartMaterials[static_cast<int32>(EShoterPartMaterial::Reticle)] = Assets->MakeSurfaceMaterial(this, Stats.EnergyColor * 0.1f, 0.f, 0.3f, Stats.EnergyColor, 10.f);
	}
	if (FirstPersonPartMaterials.Num() == 0)
	{
		// Right next to the camera the light body and the neon parts would bloom to white, so tone them down.
		FirstPersonPartMaterials.SetNum(5);
		FirstPersonPartMaterials[static_cast<int32>(EShoterPartMaterial::Body)] = Assets->MakeSurfaceMaterial(this, Stats.BodyColor * 0.4f, 0.75f, 0.4f);
		FirstPersonPartMaterials[static_cast<int32>(EShoterPartMaterial::Dark)] = PartMaterials[static_cast<int32>(EShoterPartMaterial::Dark)];
		FirstPersonPartMaterials[static_cast<int32>(EShoterPartMaterial::Accent)] = Assets->MakeSurfaceMaterial(this, Stats.AccentColor * 0.8f, 0.6f, 0.35f);
		FirstPersonPartMaterials[static_cast<int32>(EShoterPartMaterial::Glow)] = Assets->MakeSurfaceMaterial(this, Stats.EnergyColor * 0.1f, 0.f, 0.3f, Stats.EnergyColor, 3.5f);
		FirstPersonPartMaterials[static_cast<int32>(EShoterPartMaterial::Reticle)] = Assets->MakeSurfaceMaterial(this, Stats.EnergyColor * 0.1f, 0.f, 0.3f, Stats.EnergyColor, 4.f);
	}
	const TArray<TObjectPtr<UMaterialInstanceDynamic>>& Materials = bFirstPerson ? FirstPersonPartMaterials : PartMaterials;

	auto MakePart = [&](const FShoterWeaponPart& Part, USceneComponent* Attach, UMaterialInterface* Material)
	{
		UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(this);
		Mesh->SetStaticMesh(Assets->GetShape(Part.Shape));
		Mesh->SetupAttachment(Attach);
		Mesh->SetRelativeLocation(Part.Location);
		Mesh->SetRelativeRotation(Part.Rotation);
		Mesh->SetRelativeScale3D(Part.Size / 100.f);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetGenerateOverlapEvents(false);
		Mesh->SetMaterial(0, Material);
		if (bFirstPerson)
		{
			Mesh->SetOnlyOwnerSee(true);
			Mesh->SetCastShadow(false);
			Mesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
		}
		else
		{
			Mesh->SetOwnerNoSee(true);
			Mesh->bCastHiddenShadow = true;
		}
		Mesh->SetVisibility(bIsEquipped);
		Mesh->RegisterComponent();
		OutParts.Add(Mesh);
	};

	TArray<FShoterWeaponPart> AllParts = Parts;
	AllParts.Append(SightParts);
	for (const FShoterWeaponPart& Part : AllParts)
	{
		MakePart(Part, Parent, Materials[static_cast<int32>(Part.Material)]);
	}

	// Magazine: its own root (moved by the reload choreography) and its own glow material (so a fresh cell can burn brighter).
	if (MagParts.Num() > 0)
	{
		USceneComponent* MagRoot = NewObject<USceneComponent>(this);
		MagRoot->SetupAttachment(Parent);
		MagRoot->RegisterComponent();
		UMaterialInstanceDynamic* MagGlow = Assets->MakeSurfaceMaterial(this, Stats.EnergyColor * 0.1f, 0.f, 0.3f, Stats.EnergyColor, bFirstPerson ? 3.5f : 12.f);
		(bFirstPerson ? FirstPersonMagRoot : ThirdPersonMagRoot) = MagRoot;
		(bFirstPerson ? FirstPersonMagGlow : ThirdPersonMagGlow) = MagGlow;
		for (const FShoterWeaponPart& Part : MagParts)
		{
			UMaterialInterface* Material = Part.Material == EShoterPartMaterial::Glow ? static_cast<UMaterialInterface*>(MagGlow) : Materials[static_cast<int32>(Part.Material)];
			MakePart(Part, MagRoot, Material);
		}
	}
}

void AShoterWeapon::UpdateAttachment()
{
	AShoterCharacter* Character = OwningCharacter;
	if (!Character)
	{
		return;
	}

	if (USceneComponent* Pivot = Character->GetWeaponPivot())
	{
		if (FirstPersonRoot->GetAttachParent() != Pivot)
		{
			FirstPersonRoot->AttachToComponent(Pivot, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		}
	}

	USkeletalMeshComponent* Body = Character->GetMesh();
	// The UE5 mannequin has a dedicated weapon grip socket; fall back to the hand bone otherwise.
	static const FName GripSocket(TEXT("HandGrip_R"));
	static const FName HandBone(TEXT("hand_r"));
	const FName AttachName = (Body && Body->DoesSocketExist(GripSocket)) ? GripSocket : HandBone;
	if (Body && Body->GetSkeletalMeshAsset() && Body->DoesSocketExist(AttachName))
	{
		ThirdPersonRoot->AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetNotIncludingScale, AttachName);
		ThirdPersonRoot->SetRelativeLocationAndRotation(Stats.ThirdPersonOffset, Stats.ThirdPersonRotation);
	}
	else
	{
		// Placeholder body: hold the gun in front of the chest.
		ThirdPersonRoot->AttachToComponent(Character->GetRootComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		ThirdPersonRoot->SetRelativeLocationAndRotation(FVector(30.f, 18.f, 38.f), FRotator::ZeroRotator);
	}
	ThirdPersonRoot->SetRelativeScale3D(FVector(Stats.ThirdPersonScale));
}

void AShoterWeapon::SetOwningCharacter(AShoterCharacter* NewOwner)
{
	if (OwningCharacter == NewOwner)
	{
		return;
	}
	OwningCharacter = NewOwner;
	SetOwner(NewOwner);
	SetInstigator(NewOwner);
	if (bModelBuilt)
	{
		UpdateAttachment();
	}
}

void AShoterWeapon::OnRep_OwningCharacter()
{
	SetOwner(OwningCharacter);
	if (bModelBuilt)
	{
		UpdateAttachment();
	}
}

bool AShoterWeapon::IsOwnerLocallyControlled() const
{
	return OwningCharacter && OwningCharacter->IsLocallyControlled();
}

// ---------------------------------------------------------------------------
// Equip
// ---------------------------------------------------------------------------

void AShoterWeapon::OnEquip(const AShoterWeapon* LastWeapon)
{
	bIsEquipped = true;
	EquipStartTime = GetWorld()->GetTimeSeconds();

	if (bModelBuilt)
	{
		UpdateAttachment();
	}
	FirstPersonRoot->SetVisibility(true, true);
	ThirdPersonRoot->SetVisibility(true, true);

	if (IsOwnerLocallyControlled() && OwningCharacter->IsPlayerControlled())
	{
		UShoterAssets::PlaySound2D(this, Stats.EquipSound, 1.2f);
	}
	else
	{
		UShoterAssets::PlaySoundAt(this, Stats.EquipSound, GetActorLocation(), EShoterSoundRange::Short, 0.6f);
	}

	// Keep firing if the trigger is still held (e.g. after an automatic reload swap).
	if (OwningCharacter && OwningCharacter->WantsToFire() && IsOwnerLocallyControlled())
	{
		bWantsToFire = true;
		GetWorldTimerManager().SetTimer(TimerHandle_Refire, this, &AShoterWeapon::HandleFiring, Stats.EquipTime, false);
	}
}

void AShoterWeapon::OnUnequip()
{
	bIsEquipped = false;
	StopFire();
	CancelReload();
	FirstPersonRoot->SetVisibility(false, true);
	ThirdPersonRoot->SetVisibility(false, true);
}

bool AShoterWeapon::IsEquipping() const
{
	return bIsEquipped && GetWorld()->GetTimeSeconds() - EquipStartTime < Stats.EquipTime;
}

float AShoterWeapon::GetEquipAlpha() const
{
	return FMath::Clamp((GetWorld()->GetTimeSeconds() - EquipStartTime) / FMath::Max(Stats.EquipTime, 0.01f), 0.f, 1.f);
}

// ---------------------------------------------------------------------------
// Firing
// ---------------------------------------------------------------------------

float AShoterWeapon::GetFireInterval() const
{
	return 60.f / FMath::Max(Stats.FireRate, 1.f);
}

bool AShoterWeapon::CanFire() const
{
	return bIsEquipped && !IsEquipping() && !bReloading && OwningCharacter && OwningCharacter->IsAlive() && !OwningCharacter->IsSprinting();
}

void AShoterWeapon::StartFire()
{
	bWantsToFire = true;
	if (!bIsEquipped)
	{
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	const float NextAllowedTime = LastFireTime + GetFireInterval();
	if (Now >= NextAllowedTime)
	{
		HandleFiring();
	}
	else if (!GetWorldTimerManager().IsTimerActive(TimerHandle_Refire))
	{
		GetWorldTimerManager().SetTimer(TimerHandle_Refire, this, &AShoterWeapon::HandleFiring, NextAllowedTime - Now, false);
	}
}

void AShoterWeapon::StopFire()
{
	bWantsToFire = false;
	// Semi-automatic weapons keep a buffered shot so fast taps are not lost.
	if (Stats.bAutomatic)
	{
		GetWorldTimerManager().ClearTimer(TimerHandle_Refire);
	}
}

void AShoterWeapon::HandleFiring()
{
	if (!OwningCharacter || !bIsEquipped)
	{
		return;
	}

	if (!CanFire())
	{
		// Equipping, reloading or sprinting: retry shortly while the trigger is held.
		if (bWantsToFire && OwningCharacter->IsAlive())
		{
			GetWorldTimerManager().SetTimer(TimerHandle_Refire, this, &AShoterWeapon::HandleFiring, 0.05f, false);
		}
		return;
	}

	if (AmmoInClip <= 0)
	{
		UShoterAssets::PlaySoundAt(this, Stats.DryFireSound, GetMuzzleLocation(), EShoterSoundRange::Short, 1.2f);
		bWantsToFire = false;
		if (ReserveAmmo > 0)
		{
			StartReload();
		}
		return;
	}

	const FVector Origin = OwningCharacter->GetPawnViewLocation();
	const FVector AimDirection = OwningCharacter->GetControlRotation().Vector();

	if (Stats.ProjectileClass)
	{
		FireProjectile(Origin, AimDirection);
	}
	else
	{
		FireHitscan(Origin, AimDirection);
	}

	ConsumeAmmo();
	LastFireTime = GetWorld()->GetTimeSeconds();
	ApplyLocalFireFeedback();

	if (AmmoInClip <= 0 && ReserveAmmo > 0)
	{
		// Automatic reload shortly after the last round.
		GetWorldTimerManager().SetTimer(TimerHandle_Reload, this, &AShoterWeapon::StartReload, 0.3f, false);
	}
	else if (Stats.bAutomatic && bWantsToFire)
	{
		GetWorldTimerManager().SetTimer(TimerHandle_Refire, this, &AShoterWeapon::HandleFiring, GetFireInterval(), false);
	}

	if (!Stats.bAutomatic)
	{
		bWantsToFire = false;
	}
}

bool AShoterWeapon::TraceShot(const FVector& Start, const FVector& Dir, FHitResult& OutHit) const
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ShoterWeaponTrace), true, this);
	if (OwningCharacter)
	{
		Params.AddIgnoredActor(OwningCharacter);
	}
	return GetWorld()->LineTraceSingleByChannel(OutHit, Start, Start + Dir * Stats.Range, SHOTER_TRACE_WEAPON, Params);
}

void AShoterWeapon::FireHitscan(const FVector& Origin, const FVector& AimDir)
{
	const float ConeRadians = FMath::DegreesToRadians(GetCurrentSpread());
	const int32 NumPellets = FMath::Clamp(Stats.Pellets, 1, MaxReportedPellets);

	TArray<FVector_NetQuantize> ImpactPoints;
	TArray<FShoterHitReport> Hits;
	uint32 FleshMask = 0;
	bool bAnyHit = false;
	bool bAnyHeadshot = false;

	for (int32 Pellet = 0; Pellet < NumPellets; ++Pellet)
	{
		const FVector Direction = ConeRadians > KINDA_SMALL_NUMBER ? FMath::VRandCone(AimDir, ConeRadians) : AimDir;
		FHitResult Hit;
		const bool bBlocked = TraceShot(Origin, Direction, Hit);
		ImpactPoints.Add(bBlocked ? Hit.ImpactPoint : Origin + Direction * Stats.Range);

		AActor* HitActor = bBlocked ? Hit.GetActor() : nullptr;
		if (HitActor && HitActor->IsA<APawn>())
		{
			FleshMask |= (1u << Pellet);

			const bool bHead = Hit.BoneName == SHOTER_HEAD_BONE || (Hit.GetComponent() && Hit.GetComponent()->ComponentHasTag(SHOTER_HEAD_BONE));
			FShoterHitReport& Report = Hits.AddDefaulted_GetRef();
			Report.HitActor = HitActor;
			Report.HitLocation = Hit.ImpactPoint;
			Report.HitNormal = Hit.ImpactNormal;
			Report.BoneName = bHead ? SHOTER_HEAD_BONE : Hit.BoneName;

			if (IsTargetAlive(HitActor) && IsLikelyEnemy(OwningCharacter, HitActor))
			{
				bAnyHit = true;
				bAnyHeadshot |= bHead;
			}
		}
	}

	const bool bLocalPlayer = OwningCharacter && OwningCharacter->IsPlayerControlled() && IsOwnerLocallyControlled();
	if (bLocalPlayer)
	{
		PlayShotEffects(Origin, ImpactPoints, FleshMask);
		if (bAnyHit)
		{
			if (AShoterPlayerController* PC = Cast<AShoterPlayerController>(OwningCharacter->GetController()))
			{
				PC->NotifyLocalHit(bAnyHeadshot);
			}
		}
	}

	if (HasAuthority())
	{
		LastServerFireTime = GetWorld()->GetTimeSeconds();
		ProcessHitsAuthority(Origin, Hits);
		MulticastFireEffects(Origin, ImpactPoints, FleshMask);
		if (OwningCharacter)
		{
			OwningCharacter->ClearSpawnProtection();
		}
	}
	else
	{
		ServerFireHitscan(Origin, AimDir, ImpactPoints, FleshMask, Hits);
	}
}

void AShoterWeapon::ServerFireHitscan_Implementation(FVector_NetQuantize Origin, FVector_NetQuantizeNormal AimDirection,
	const TArray<FVector_NetQuantize>& ImpactPoints, uint32 FleshMask, const TArray<FShoterHitReport>& Hits)
{
	if (!OwningCharacter || !OwningCharacter->IsAlive() || AmmoInClip <= 0)
	{
		return;
	}

	// Token bucket: tolerate network jitter, reject sustained fire faster than the weapon allows.
	const float Now = GetWorld()->GetTimeSeconds();
	ServerFireBudget = FMath::Min(3.f, ServerFireBudget + (Now - LastServerFireTime) / GetFireInterval());
	LastServerFireTime = Now;
	if (ServerFireBudget < 0.75f)
	{
		return;
	}
	ServerFireBudget -= 1.f;

	ConsumeAmmo();

	FVector SafeOrigin = Origin;
	const FVector Eye = OwningCharacter->GetPawnViewLocation();
	if (FVector::DistSquared(SafeOrigin, Eye) > FMath::Square(300.f))
	{
		SafeOrigin = Eye;
	}

	if (Hits.Num() <= MaxReportedPellets)
	{
		ProcessHitsAuthority(SafeOrigin, Hits);
	}

	TArray<FVector_NetQuantize> Impacts = ImpactPoints;
	if (Impacts.Num() > MaxReportedPellets)
	{
		Impacts.SetNum(MaxReportedPellets);
	}
	MulticastFireEffects(SafeOrigin, Impacts, FleshMask);
	OwningCharacter->ClearSpawnProtection();
}

bool AShoterWeapon::ValidateHit(const FVector& Origin, const FShoterHitReport& Hit) const
{
	AActor* Target = Hit.HitActor;
	if (!IsValid(Target) || Target == OwningCharacter || !IsTargetAlive(Target))
	{
		return false;
	}

	if (FVector::Dist(Origin, Hit.HitLocation) > Stats.Range * 1.05f)
	{
		return false;
	}

	// The reported point must be near the target. Generous on purpose: it covers latency.
	FVector BoundsOrigin;
	FVector BoundsExtent;
	Target->GetActorBounds(true, BoundsOrigin, BoundsExtent);
	if (!FBox::BuildAABB(BoundsOrigin, BoundsExtent).ExpandBy(250.f).IsInside(Hit.HitLocation))
	{
		return false;
	}

	// No static world geometry may stand between the shooter and the hit point.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ShoterValidateHit), false, OwningCharacter);
	Params.AddIgnoredActor(Target);
	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);
	const FVector Direction = (Hit.HitLocation - Origin).GetSafeNormal();
	FHitResult Blocking;
	return !GetWorld()->LineTraceSingleByObjectType(Blocking, Origin, Hit.HitLocation - Direction * 30.f, ObjectParams, Params);
}

float AShoterWeapon::ComputeDamage(float Distance, bool bHeadshot) const
{
	float Scale = 1.f;
	if (Stats.FalloffEnd > Stats.FalloffStart && Distance > Stats.FalloffStart)
	{
		const float Alpha = FMath::Clamp((Distance - Stats.FalloffStart) / (Stats.FalloffEnd - Stats.FalloffStart), 0.f, 1.f);
		Scale = FMath::Lerp(1.f, Stats.MinDamageScale, Alpha);
	}
	return Stats.Damage * Scale * (bHeadshot ? Stats.HeadshotMultiplier : 1.f);
}

void AShoterWeapon::ProcessHitsAuthority(const FVector& Origin, const TArray<FShoterHitReport>& Hits)
{
	// Pellets that hit the same target are summed into one damage event (one hit marker, clean kill credit).
	TMap<AActor*, FAggregatedHit> PerTarget;
	for (const FShoterHitReport& Hit : Hits)
	{
		if (!ValidateHit(Origin, Hit))
		{
			continue;
		}
		const bool bHead = Hit.BoneName == SHOTER_HEAD_BONE;
		FAggregatedHit& Aggregated = PerTarget.FindOrAdd(Hit.HitActor.Get());
		if (Aggregated.Damage <= 0.f)
		{
			Aggregated.Report = Hit;
		}
		Aggregated.Damage += ComputeDamage(FVector::Dist(Origin, Hit.HitLocation), bHead);
		Aggregated.bHeadshot |= bHead;
	}

	AController* InstigatorController = OwningCharacter ? OwningCharacter->GetController() : nullptr;
	for (const TPair<AActor*, FAggregatedHit>& Entry : PerTarget)
	{
		AActor* Target = Entry.Key;
		const FAggregatedHit& Aggregated = Entry.Value;

		FHitResult HitResult;
		HitResult.bBlockingHit = true;
		HitResult.Location = Aggregated.Report.HitLocation;
		HitResult.ImpactPoint = Aggregated.Report.HitLocation;
		HitResult.Normal = Aggregated.Report.HitNormal;
		HitResult.ImpactNormal = Aggregated.Report.HitNormal;
		HitResult.BoneName = Aggregated.bHeadshot ? SHOTER_HEAD_BONE : Aggregated.Report.BoneName;

		const FVector ShotDirection = (FVector(Aggregated.Report.HitLocation) - Origin).GetSafeNormal();
		const FPointDamageEvent DamageEvent(Aggregated.Damage, HitResult, ShotDirection, UDamageType::StaticClass());
		Target->TakeDamage(Aggregated.Damage, DamageEvent, InstigatorController, this);
	}
}

void AShoterWeapon::FireProjectile(const FVector& Origin, const FVector& AimDir)
{
	const float ConeRadians = FMath::DegreesToRadians(GetCurrentSpread());
	const FVector Direction = ConeRadians > KINDA_SMALL_NUMBER ? FMath::VRandCone(AimDir, ConeRadians) : AimDir;

	// Spawn a bit in front of the eyes, but never behind a wall we are hugging.
	FVector SpawnLocation = Origin + AimDir * 60.f;
	FHitResult Blocking;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ShoterProjectileSpawn), false, OwningCharacter);
	if (GetWorld()->LineTraceSingleByChannel(Blocking, Origin, SpawnLocation, ECC_Visibility, Params))
	{
		SpawnLocation = Blocking.Location - AimDir * 15.f;
	}

	const bool bLocalPlayer = OwningCharacter && OwningCharacter->IsPlayerControlled() && IsOwnerLocallyControlled();
	if (bLocalPlayer)
	{
		PlayShotEffects(Origin, TArray<FVector_NetQuantize>(), 0);
	}

	if (HasAuthority())
	{
		LastServerFireTime = GetWorld()->GetTimeSeconds();
		SpawnProjectile(SpawnLocation, Direction);
		MulticastFireEffects(Origin, TArray<FVector_NetQuantize>(), 0);
		if (OwningCharacter)
		{
			OwningCharacter->ClearSpawnProtection();
		}
	}
	else
	{
		ServerFireProjectile(SpawnLocation, Direction);
	}
}

void AShoterWeapon::ServerFireProjectile_Implementation(FVector_NetQuantize Origin, FVector_NetQuantizeNormal Direction)
{
	if (!OwningCharacter || !OwningCharacter->IsAlive() || AmmoInClip <= 0)
	{
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	ServerFireBudget = FMath::Min(3.f, ServerFireBudget + (Now - LastServerFireTime) / GetFireInterval());
	LastServerFireTime = Now;
	if (ServerFireBudget < 0.75f)
	{
		return;
	}
	ServerFireBudget -= 1.f;

	ConsumeAmmo();

	FVector SafeOrigin = Origin;
	const FVector Eye = OwningCharacter->GetPawnViewLocation();
	if (FVector::DistSquared(SafeOrigin, Eye) > FMath::Square(300.f))
	{
		SafeOrigin = Eye;
	}
	SpawnProjectile(SafeOrigin, Direction);
	MulticastFireEffects(Eye, TArray<FVector_NetQuantize>(), 0);
	OwningCharacter->ClearSpawnProtection();
}

void AShoterWeapon::SpawnProjectile(const FVector& Origin, const FVector& Direction)
{
	if (!Stats.ProjectileClass)
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.Owner = OwningCharacter;
	Params.Instigator = OwningCharacter;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AShoterProjectile* Projectile = GetWorld()->SpawnActor<AShoterProjectile>(Stats.ProjectileClass, Origin, Direction.Rotation(), Params))
	{
		Projectile->InitProjectile(Direction, Stats.ProjectileSpeed, OwningCharacter ? OwningCharacter->GetController() : nullptr, Stats.DisplayName.ToString());
	}
}

void AShoterWeapon::MulticastFireEffects_Implementation(FVector_NetQuantize Origin, const TArray<FVector_NetQuantize>& ImpactPoints, uint32 FleshMask)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	// The shooting player already played everything locally.
	if (OwningCharacter && OwningCharacter->IsPlayerControlled() && IsOwnerLocallyControlled())
	{
		return;
	}
	PlayShotEffects(Origin, ImpactPoints, FleshMask);
}

bool AShoterWeapon::IsFirstPersonView() const
{
	if (!OwningCharacter || !OwningCharacter->IsPlayerControlled() || !IsOwnerLocallyControlled())
	{
		return false;
	}
	const APlayerController* PC = Cast<APlayerController>(OwningCharacter->GetController());
	// Debug/mirror/death cameras look at the pawn from outside: there the third-person model is what is on screen.
	return PC && PC->GetViewTarget() == OwningCharacter && PC->PlayerCameraManager && PC->PlayerCameraManager->GetCameraCacheView().bUseFirstPersonParameters;
}

void AShoterWeapon::PlayShotEffects(const FVector& Origin, const TArray<FVector_NetQuantize>& ImpactPoints, uint32 FleshMask)
{
	UWorld* World = GetWorld();
	const bool bLocalView = OwningCharacter && OwningCharacter->IsPlayerControlled() && IsOwnerLocallyControlled();
	const bool bFirstPersonView = IsFirstPersonView();
	const FVector Muzzle = GetMuzzleLocation();
	const float Pitch = FMath::FRandRange(0.96f, 1.04f);

	if (bLocalView)
	{
		UShoterAssets::PlaySound2D(this, Stats.FireSound, 0.85f, Pitch);
	}
	else
	{
		UShoterAssets::PlaySoundAt(this, Stats.FireSound, Muzzle, Stats.FireSoundRange, 1.f, Pitch);
	}

	const float FlashScale = (Stats.Type == EShoterWeaponType::Shotgun || Stats.Type == EShoterWeaponType::Launcher) ? 1.6f
		: (Stats.Type == EShoterWeaponType::Sniper ? 1.8f : 1.f);
	FShoterFX::SpawnMuzzleFlash(bLocalView ? FirstPersonRoot.Get() : ThirdPersonRoot.Get(), Stats.MuzzleOffset, Stats.EnergyColor, FlashScale, bLocalView, !bLocalView);

	// Bolts leave from where the muzzle is actually drawn: for the local first-person model that is the raw muzzle pushed through the
	// viewmodel FOV transform (the model is rendered with its own FOV and scale, so its raw position is not where it appears).
	FVector VisualMuzzle = Muzzle;
	if (bFirstPersonView)
	{
		VisualMuzzle = UShoterFXPool::ToFirstPersonSpace(this, Muzzle);
	}
	else if (bLocalView && ThirdPersonRoot)
	{
		VisualMuzzle = ThirdPersonRoot->GetComponentTransform().TransformPosition(Stats.MuzzleOffset);
	}
	const float AimAlpha = (bFirstPersonView && OwningCharacter) ? OwningCharacter->GetAimAlpha() : 0.f;
	UShoterFXPool* Pool = UShoterFXPool::Get(this);

	for (int32 Index = 0; Index < ImpactPoints.Num(); ++Index)
	{
		const FVector End = ImpactPoints[Index];
		const bool bFlesh = (FleshMask & (1u << Index)) != 0;
		// Shots that reached max range hit nothing.
		const bool bHit = FVector::DistSquared(Origin, End) < FMath::Square(Stats.Range * 0.98f);
		const float Distance = FVector::Dist(VisualMuzzle, End);
		const FVector ImpactNormal = (Origin - End).GetSafeNormal();

		// The rail beam is instant; very close hits have no visible flight. Everything else lands when the bolt arrives.
		const bool bImpactNow = !Pool || Stats.bBeamTracer || Distance < 300.f;
		if (bHit && bImpactNow)
		{
			FShoterFX::SpawnImpact(World, End, ImpactNormal, Stats.EnergyColor, bFlesh);
		}
		if (!Pool)
		{
			continue;
		}

		if (Stats.bBeamTracer)
		{
			FShoterBeamParams Beam;
			Beam.Start = VisualMuzzle;
			Beam.End = End;
			Beam.Color = Stats.EnergyColor;
			Beam.Width = Stats.TracerWidth * 2.2f * (bFirstPersonView ? FMath::Lerp(0.6f, 0.4f, AimAlpha) : 1.f);
			Beam.Intensity = bFirstPersonView ? 7.f : 14.f;
			Beam.Lifetime = 0.9f;
			Pool->SpawnBeam(Beam);
		}
		else if (Distance >= 250.f)
		{
			const bool bPellet = ImpactPoints.Num() > 1;
			FShoterBoltParams Bolt;
			Bolt.Start = VisualMuzzle;
			Bolt.End = End;
			Bolt.Color = Stats.EnergyColor;
			Bolt.Speed = Stats.BoltSpeed * FMath::FRandRange(0.94f, 1.06f);
			Bolt.Length = Stats.BoltLength * (bPellet ? 0.55f : 1.f);
			Bolt.Width = Stats.TracerWidth * (bPellet ? 1.2f : 1.7f) * (bFirstPersonView ? FMath::Lerp(1.f, 0.6f, AimAlpha) : 1.f);
			Bolt.Intensity = bFirstPersonView ? 5.f : 14.f;
			Bolt.bImpact = bHit && !bImpactNow;
			Bolt.ImpactNormal = ImpactNormal;
			Bolt.bImpactFlesh = bFlesh;
			Pool->SpawnBolt(Bolt);
		}
		else if (bHit && !bImpactNow)
		{
			FShoterFX::SpawnImpact(World, End, ImpactNormal, Stats.EnergyColor, bFlesh);
		}
	}

	EjectCasing(bFirstPersonView);
}

void AShoterWeapon::EjectCasing(bool bFirstPersonView)
{
	UShoterFXPool* Pool = UShoterFXPool::Get(this);
	const USceneComponent* Root = bFirstPersonView ? FirstPersonRoot.Get() : ThirdPersonRoot.Get();
	if (!Pool || !Root || !OwningCharacter)
	{
		return;
	}
	const float Aim = bFirstPersonView ? OwningCharacter->GetAimAlpha() : 0.f;
	if (bFirstPersonView && Stats.bHasScope && Aim > 0.5f)
	{
		return;		// looking through the scope: the model is hidden
	}

	const FTransform ModelTransform = Root->GetComponentTransform();
	FVector Port = ModelTransform.TransformPosition(Stats.EjectOffset);
	if (bFirstPersonView)
	{
		Port = UShoterFXPool::ToFirstPersonSpace(this, Port);
	}
	else if (const APlayerController* Viewer = GetWorld()->GetFirstPlayerController())
	{
		// Too small to see from afar.
		if (Viewer->PlayerCameraManager && FVector::DistSquared(Viewer->PlayerCameraManager->GetCameraLocation(), Port) > FMath::Square(3000.f))
		{
			return;
		}
	}

	// Out to the right, up and a little back; while aiming it stays low and close so it never crosses the sight picture.
	const FVector LocalVelocity(FMath::FRandRange(-90.f, -15.f) , FMath::FRandRange(150.f, 240.f) * FMath::Lerp(1.f, 0.8f, Aim), FMath::FRandRange(70.f, 150.f) * FMath::Lerp(1.f, 0.4f, Aim));
	FVector Velocity = ModelTransform.TransformVectorNoScale(LocalVelocity) + OwningCharacter->GetVelocity().GetClampedToMaxSize(600.f) * 0.5f;

	FShoterDebrisParams Casing;
	Casing.Shape = EShoterShape::Cylinder;
	Casing.Size = FVector(Stats.CasingDiameter, Stats.CasingDiameter, Stats.CasingLength);
	Casing.BaseColor = FLinearColor(0.1f, 0.1f, 0.11f);
	Casing.Metallic = 0.8f;
	Casing.Roughness = 0.35f;
	Casing.GlowColor = Stats.EnergyColor;
	Casing.GlowStrength = bFirstPersonView ? 5.f : 9.f;
	Casing.GlowTime = 0.6f;
	Casing.Location = Port;
	Casing.Rotation = FRotator(FMath::FRandRange(-180.f, 180.f), FMath::FRandRange(-180.f, 180.f), FMath::FRandRange(-180.f, 180.f));
	Casing.Velocity = Velocity;
	const float Spin = FMath::FRandRange(500.f, 1100.f);
	Casing.AngularVelocity = FMath::VRand() * Spin;
	Casing.Lifetime = 2.5f;
	Casing.FadeTime = 0.5f;
	Casing.MaxBounces = 1;
	Casing.BounceSound = TEXT("Grenade_Bounce");
	Pool->SpawnDebris(Casing);
}

FVector AShoterWeapon::GetMuzzleLocation() const
{
	const bool bLocalView = OwningCharacter && OwningCharacter->IsPlayerControlled() && IsOwnerLocallyControlled();
	const USceneComponent* ModelRoot = bLocalView ? FirstPersonRoot.Get() : ThirdPersonRoot.Get();
	return ModelRoot ? ModelRoot->GetComponentTransform().TransformPosition(Stats.MuzzleOffset) : GetActorLocation();
}

void AShoterWeapon::ConsumeAmmo()
{
	AmmoInClip = FMath::Max(0, AmmoInClip - 1);
}

void AShoterWeapon::ApplyLocalFireFeedback()
{
	if (!OwningCharacter)
	{
		return;
	}

	SpreadBloom = FMath::Min(Stats.MaxBloom, SpreadBloom + Stats.SpreadPerShot);

	if (!OwningCharacter->IsPlayerControlled() || !IsOwnerLocallyControlled())
	{
		return;
	}

	const float AimScale = FMath::Lerp(1.f, Stats.AimRecoilScale, OwningCharacter->GetAimAlpha());
	OwningCharacter->AddRecoil(Stats.RecoilPitch * AimScale * FMath::FRandRange(0.85f, 1.15f), Stats.RecoilYaw * 0.6f * AimScale * FMath::FRandRange(-1.f, 1.f));

	// The viewmodel barely rotates while aiming so the gun never swings over the target.
	const float ViewmodelKickScale = FMath::Lerp(1.f, 0.2f, OwningCharacter->GetAimAlpha());
	KickOffset.X -= Stats.KickBack * AimScale;
	KickRotation.Pitch += Stats.RecoilPitch * 1.5f * AimScale * ViewmodelKickScale;

	if (const AController* Controller = OwningCharacter->GetController())
	{
		if (const APlayerController* PC = Cast<APlayerController>(Controller))
		{
			if (AShoterPlayerCameraManager* CameraManager = Cast<AShoterPlayerCameraManager>(PC->PlayerCameraManager))
			{
				const bool bHeavy = Stats.Type == EShoterWeaponType::Shotgun || Stats.Type == EShoterWeaponType::Sniper || Stats.Type == EShoterWeaponType::Launcher;
				CameraManager->AddTrauma(bHeavy ? 0.25f : 0.05f);
			}
		}
	}
}

float AShoterWeapon::GetCurrentSpread() const
{
	const AShoterCharacter* Character = OwningCharacter;
	const float Aim = Character ? Character->GetAimAlpha() : 0.f;

	float Spread = FMath::Lerp(Stats.HipSpread, Stats.AimSpread, Aim) + SpreadBloom * FMath::Lerp(1.f, 0.5f, Aim);
	if (Character)
	{
		const float SpeedAlpha = FMath::Clamp(Character->GetVelocity().Size2D() / 520.f, 0.f, 1.f);
		Spread += Stats.MovingSpread * 0.4f * SpeedAlpha * FMath::Lerp(1.f, 0.4f, Aim);
		if (const UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			if (Movement->IsFalling())
			{
				Spread += Stats.AirSpread * 0.5f;
			}
		}
		if (Character->bIsCrouched)
		{
			Spread *= 0.8f;
		}
	}
	return FMath::Max(Spread, 0.f);
}

FVector AShoterWeapon::GetAimViewOffset(float ForwardOffset) const
{
	if (bAimViewOffsetCached)
	{
		return CachedAimViewOffset;
	}

	// Weapons with iron sights: the top centre of the front post goes exactly on the view axis (Y = Z = 0) and the
	// rear sight sits a fixed distance from the eye. Pushed further out only if needed to keep the gun narrow.
	if (!Stats.SightOffset.IsNearlyZero())
	{
		constexpr float RearSightDistance = 40.f;
		constexpr float MaxHalfWidthTan = 0.17f;
		constexpr float MinEyeDistance = 14.f;
		float RearX = TNumericLimits<float>::Max();
		for (const FShoterWeaponPart& Part : SightParts)
		{
			RearX = FMath::Min(RearX, Part.Location.X - Part.Size.X * 0.5f);
		}
		if (RearX == TNumericLimits<float>::Max())
		{
			RearX = Stats.SightOffset.X;
		}
		float ViewX = RearSightDistance - RearX;
		for (const FShoterWeaponPart& Part : Parts)
		{
			const FVector Extent = Part.Rotation.Quaternion().RotateVector(Part.Size * 0.5f).GetAbs();
			ViewX = FMath::Max(ViewX, (FMath::Abs(Part.Location.Y) + Extent.Y) / MaxHalfWidthTan - (Part.Location.X - Extent.X));
			ViewX = FMath::Max(ViewX, MinEyeDistance - (Part.Location.X - Extent.X));
		}
		CachedAimViewOffset = FVector(ViewX, -Stats.SightOffset.Y, -Stats.SightOffset.Z);
		bAimViewOffsetCached = true;
		return CachedAimViewOffset;
	}

	// Collect the extreme points of every part in weapon space (cylinders sampled around their rim, the rest as boxes).
	TArray<FVector> Points;
	for (const FShoterWeaponPart& Part : Parts)
	{
		const FQuat Rotation = Part.Rotation.Quaternion();
		const FVector Half = Part.Size * 0.5f;
		if (Part.Shape == EShoterShape::Cylinder)
		{
			for (int32 Step = 0; Step < 8; ++Step)
			{
				const float Angle = 2.f * PI * static_cast<float>(Step) / 8.f;
				for (const float Sign : { -1.f, 1.f })
				{
					Points.Add(Part.Location + Rotation.RotateVector(FVector(Half.X * FMath::Cos(Angle), Half.Y * FMath::Sin(Angle), Half.Z * Sign)));
				}
			}
		}
		else
		{
			for (int32 Corner = 0; Corner < 8; ++Corner)
			{
				const FVector Local((Corner & 1) ? Half.X : -Half.X, (Corner & 2) ? Half.Y : -Half.Y, (Corner & 4) ? Half.Z : -Half.Z);
				Points.Add(Part.Location + Rotation.RotateVector(Local));
			}
		}
	}
	if (Points.Num() == 0)
	{
		CachedAimViewOffset = FVector(Stats.AimOffset.X + ForwardOffset, 0.f, Stats.AimOffset.Z);
		bAimViewOffsetCached = true;
		return CachedAimViewOffset;
	}

	// Angular limits measured against the 16:9 viewmodel FOV (vertical half-angle tangent; Hor+ keeps it for wider screens).
	const float TanHalfHorizontal = FMath::Tan(FMath::DegreesToRadians(AShoterCharacter::ViewmodelFOV * 0.5f));
	const float TanHalfVertical = TanHalfHorizontal / (16.f / 9.f);
	constexpr float ClearanceNdc = 0.15f;		// highest solid point stays at least this far below the eye line (NDC, 1 = half the screen height)
	constexpr float MaxHalfWidthNdc = 0.115f;	// no point further from the centre line than this fraction of the half-width (x2 = ~12% of screen width)
	constexpr float MinEyeDistance = 14.f;
	constexpr float MinAimX = 24.f;				// never closer than this before the constant forward offset

	const float ClearanceTan = ClearanceNdc * TanHalfVertical;
	const float MaxHalfWidthTan = MaxHalfWidthNdc * TanHalfHorizontal;

	float ViewX = FMath::Max(Stats.AimOffset.X, MinAimX) + ForwardOffset;
	for (const FVector& Point : Points)
	{
		ViewX = FMath::Max(ViewX, FMath::Abs(Point.Y) / MaxHalfWidthTan - Point.X);
		ViewX = FMath::Max(ViewX, MinEyeDistance - Point.X);
	}

	float ViewZ = TNumericLimits<float>::Max();
	for (const FVector& Point : Points)
	{
		ViewZ = FMath::Min(ViewZ, -ClearanceTan * (Point.X + ViewX) - Point.Z);
	}

	CachedAimViewOffset = FVector(ViewX, 0.f, ViewZ);
	bAimViewOffsetCached = true;
	return CachedAimViewOffset;
}

float AShoterWeapon::GetTimeSinceLastFire() const
{
	return GetWorld()->GetTimeSeconds() - LastFireTime;
}

// ---------------------------------------------------------------------------
// Reload & ammo
// ---------------------------------------------------------------------------

bool AShoterWeapon::CanReload() const
{
	return bIsEquipped && !bReloading && AmmoInClip < Stats.MagSize && ReserveAmmo > 0 && OwningCharacter && OwningCharacter->IsAlive();
}

void AShoterWeapon::StartReload()
{
	if (!CanReload())
	{
		return;
	}
	if (!HasAuthority())
	{
		ServerStartReload();
	}

	bReloading = true;
	ReloadStartTime = GetWorld()->GetTimeSeconds();
	GetWorldTimerManager().ClearTimer(TimerHandle_Refire);
	GetWorldTimerManager().SetTimer(TimerHandle_Reload, this, &AShoterWeapon::FinishReload, Stats.ReloadTime, false);

	BeginReloadVisuals(0.f);
}

void AShoterWeapon::ServerStartReload_Implementation()
{
	StartReload();
}

void AShoterWeapon::FinishReload()
{
	if (!bReloading)
	{
		return;
	}
	bReloading = false;
	ResetReloadVisuals();

	// Same math on client and server; the server value replicates to the owner anyway.
	const int32 Needed = Stats.MagSize - AmmoInClip;
	const int32 Taken = FMath::Min(Needed, ReserveAmmo);
	AmmoInClip += Taken;
	ReserveAmmo -= Taken;

	if (bWantsToFire && Stats.bAutomatic && IsOwnerLocallyControlled())
	{
		HandleFiring();
	}
}

void AShoterWeapon::CancelReload()
{
	if (bReloading)
	{
		bReloading = false;
		GetWorldTimerManager().ClearTimer(TimerHandle_Reload);
	}
	ResetReloadVisuals();
}

void AShoterWeapon::OnRep_Reloading()
{
	// Simulated proxies learn about the reload from the replicated flag; the choreography is clock driven from here, so there is no
	// per-frame traffic. A flag that arrives late starts the cosmetic reload part way through.
	if (bReloading)
	{
		ReloadStartTime = GetWorld()->GetTimeSeconds();
		BeginReloadVisuals(0.f);
	}
	else
	{
		ResetReloadVisuals();
	}
}

float AShoterWeapon::GetReloadProgress() const
{
	if (!bReloading)
	{
		return 0.f;
	}
	return FMath::Clamp((GetWorld()->GetTimeSeconds() - ReloadStartTime) / FMath::Max(Stats.ReloadTime, 0.01f), 0.f, 1.f);
}

// ---------------------------------------------------------------------------
// Reload choreography (cosmetic, driven by the reload progress on every machine)
// ---------------------------------------------------------------------------

namespace
{
	float SmoothStep01(float X)
	{
		X = FMath::Clamp(X, 0.f, 1.f);
		return X * X * (3.f - 2.f * X);
	}

	float Segment(float P, float From, float To)
	{
		return To > From ? FMath::Clamp((P - From) / (To - From), 0.f, 1.f) : 1.f;
	}
}

void AShoterWeapon::GetReloadFrame(FShoterReloadFrame& Out) const
{
	const FShoterReloadSpec& R = Stats.Reload;
	Out = FShoterReloadFrame();
	Out.LeftLocation = Stats.LeftGrip.Location;
	Out.LeftRotation = Stats.LeftGrip.Rotation;
	Out.LeftCurl = Stats.LeftGrip.Curl;
	if (!bReloading)
	{
		return;
	}

	const float P = GetReloadProgress();
	const FVector Rest = Stats.LeftGrip.Location;
	const FVector MagDirection = R.MagOut.GetSafeNormal();
	const FVector Grab = R.MagGrip;
	const FVector Pulled = Grab + MagDirection * R.MagTravel;
	const FVector Drop = Pulled + R.HandDrop;
	const float TGrab = R.Grab;
	const float TPull = FMath::Max(R.Pull, TGrab + 0.01f);
	const float TAway = FMath::Max(R.Away, TPull + 0.01f);
	const float TBack = FMath::Max(R.Back, TAway + 0.02f);
	const float TSeat = FMath::Max(R.Seat, TBack + 0.01f);
	const float TCharge = R.bCharge ? FMath::Max(R.Charge, TSeat + 0.04f) : TSeat;
	const float TFresh = FMath::Lerp(TAway, TBack, 0.3f);
	const float MagCurl = 0.9f;

	FQuat RestRot = Stats.LeftGrip.Rotation.Quaternion();
	const FQuat MagRot = R.MagHandRot.Quaternion();
	auto SetRot = [&](const FQuat& A, const FQuat& B, float Alpha) { Out.LeftRotation = FQuat::Slerp(A, B, Alpha).Rotator(); };

	if (P < TGrab)
	{
		const float S = SmoothStep01(P / TGrab);
		Out.LeftLocation = FMath::Lerp(Rest, Grab, S);
		SetRot(RestRot, MagRot, S);
		Out.LeftCurl = FMath::Lerp(Stats.LeftGrip.Curl, MagCurl, S);
	}
	else if (P < TPull)
	{
		const float S = SmoothStep01(Segment(P, TGrab, TPull));
		Out.LeftLocation = FMath::Lerp(Grab, Pulled, S);
		Out.LeftRotation = R.MagHandRot;
		Out.LeftCurl = MagCurl;
		Out.MagOffset = MagDirection * R.MagTravel * S;
	}
	else if (P < TAway)
	{
		const float S = SmoothStep01(Segment(P, TPull, TAway));
		Out.LeftLocation = FMath::Lerp(Pulled, Drop, S);
		Out.LeftRotation = R.MagHandRot;
		Out.LeftCurl = FMath::Lerp(MagCurl, 0.55f, S);
		Out.bMagVisible = false;
	}
	else if (P < TBack)
	{
		// The hand waits below the screen, takes the fresh cell and comes back with it.
		const float S = SmoothStep01(Segment(P, TFresh, TBack));
		Out.LeftLocation = FMath::Lerp(Drop, Pulled, S);
		Out.LeftRotation = R.MagHandRot;
		Out.LeftCurl = MagCurl;
		Out.bMagVisible = P >= TFresh;
		Out.MagOffset = MagDirection * R.MagTravel + (Out.LeftLocation - Pulled);
		Out.MagGlow = 1.8f;
	}
	else if (P < TSeat)
	{
		const float S = SmoothStep01(Segment(P, TBack, TSeat));
		Out.LeftLocation = FMath::Lerp(Pulled, Grab, S);
		Out.LeftRotation = R.MagHandRot;
		Out.LeftCurl = MagCurl;
		Out.MagOffset = MagDirection * R.MagTravel * (1.f - S);
		Out.MagGlow = 1.8f;
	}
	else if (R.bCharge && P < TCharge)
	{
		// Rack the action: to the handle, pull back, let go.
		const float U = Segment(P, TSeat, TCharge);
		const FVector Handle = R.ChargeLocation;
		const FVector Pull = Handle + FVector(-R.ChargeStroke, 0.f, 0.f);
		const FQuat ChargeRot = R.ChargeRotation.Quaternion();
		if (U < 0.3f)
		{
			const float S = SmoothStep01(U / 0.3f);
			Out.LeftLocation = FMath::Lerp(Grab, Handle, S);
			SetRot(MagRot, ChargeRot, S);
			Out.LeftCurl = FMath::Lerp(MagCurl, 0.7f, S);
		}
		else if (U < 0.58f)
		{
			Out.LeftLocation = FMath::Lerp(Handle, Pull, SmoothStep01((U - 0.3f) / 0.28f));
			Out.LeftRotation = R.ChargeRotation;
			Out.LeftCurl = 0.8f;
		}
		else if (U < 0.72f)
		{
			Out.LeftLocation = FMath::Lerp(Pull, Handle, FMath::Clamp((U - 0.58f) / 0.14f, 0.f, 1.f));
			Out.LeftRotation = R.ChargeRotation;
			Out.LeftCurl = 0.8f;
		}
		else
		{
			const float S = SmoothStep01((U - 0.72f) / 0.28f);
			Out.LeftLocation = FMath::Lerp(Handle, Rest, S);
			SetRot(ChargeRot, RestRot, S);
			Out.LeftCurl = FMath::Lerp(0.8f, Stats.LeftGrip.Curl, S);
		}
		Out.MagGlow = 1.3f;
	}
	else
	{
		const FVector From = R.bCharge ? R.ChargeLocation : Grab;
		const FQuat FromRot = R.bCharge ? R.ChargeRotation.Quaternion() : MagRot;
		const float S = SmoothStep01(Segment(P, TCharge, 1.f));
		Out.LeftLocation = FMath::Lerp(From, Rest, S);
		SetRot(FromRot, RestRot, S);
		Out.LeftCurl = FMath::Lerp(R.bCharge ? 0.8f : MagCurl, Stats.LeftGrip.Curl, S);
		Out.MagGlow = FMath::Lerp(1.3f, 1.f, S);
	}

	// A short flare right after the cell clicks home.
	if (P >= TSeat)
	{
		const float Since = (P - TSeat) * Stats.ReloadTime;
		Out.MagGlow *= 1.f + 2.2f * FMath::Exp(-Since * 7.f);
	}

	// Whole-gun tilt: in over the grab, held until the cell is seated, out while the hand goes back.
	Out.Tilt = SmoothStep01(P / FMath::Max(TGrab, 0.05f)) * (1.f - SmoothStep01(Segment(P, TSeat, 1.f))) * R.TiltScale;
	Out.LeftWeight = SmoothStep01(P / 0.12f) * (1.f - SmoothStep01((P - 0.9f) / 0.1f));
}

void AShoterWeapon::AddViewKick(const FVector& Offset, const FRotator& Rotation)
{
	KickOffset += Offset;
	KickRotation += Rotation;
}

bool AShoterWeapon::GetThirdPersonModelTransform(FTransform& OutTransform) const
{
	const AShoterCharacter* Character = OwningCharacter;
	const USkeletalMeshComponent* Body = Character ? Character->GetMesh() : nullptr;
	static const FName GripSocket(TEXT("HandGrip_R"));
	if (!Body || !Body->GetSkeletalMeshAsset() || !Body->DoesSocketExist(GripSocket))
	{
		return false;
	}
	const FTransform Socket = Body->GetSocketTransform(GripSocket);
	FShoterReloadFrame Frame;
	GetReloadFrame(Frame);
	const FQuat Tilt = (FRotator(-9.f, 5.f, 14.f) * Frame.Tilt).Quaternion();
	OutTransform = FTransform(Stats.ThirdPersonRotation.Quaternion() * Tilt, Stats.ThirdPersonOffset, FVector(Stats.ThirdPersonScale)) * Socket;
	return true;
}

void AShoterWeapon::PlayReloadSound(FName Sound, float Volume)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (IsFirstPersonView())
	{
		UShoterAssets::PlaySound2D(this, Sound, Volume);
	}
	else
	{
		// Heard by players within earshot (a pistol-sized sound, ~25 m).
		FVector Location = GetActorLocation();
		if (const USceneComponent* Root = ThirdPersonRoot.Get())
		{
			Location = Root->GetComponentLocation();
		}
		UShoterAssets::PlaySoundAt(this, Sound, Location, EShoterSoundRange::Short, Volume * 0.9f);
	}
}

void AShoterWeapon::BeginReloadVisuals(float StartProgress)
{
	bReleaseDone = false;
	bSeatDone = false;
	bRackDone = false;
	bReloadVisualsActive = true;
	// Events behind the starting progress are skipped, not played in a burst.
	if (StartProgress > Stats.Reload.Pull)
	{
		bReleaseDone = true;
	}
	if (StartProgress > Stats.Reload.Seat)
	{
		bSeatDone = true;
	}
}

void AShoterWeapon::UpdateReloadVisuals()
{
	if (!bModelBuilt)
	{
		return;
	}
	if (!bReloading)
	{
		ResetReloadVisuals();
		return;
	}

	const FShoterReloadSpec& R = Stats.Reload;
	const float P = GetReloadProgress();
	if (!bReleaseDone && P >= R.Pull)
	{
		bReleaseDone = true;
		OnMagazineReleased();
	}
	if (!bSeatDone && P >= R.Seat)
	{
		bSeatDone = true;
		OnMagazineSeated();
	}
	const float RackTime = R.Seat + 0.72f * (R.Charge - R.Seat);
	if (R.bCharge && !bRackDone && P >= RackTime)
	{
		bRackDone = true;
		OnActionRacked();
	}

	FShoterReloadFrame Frame;
	GetReloadFrame(Frame);
	if (ThirdPersonRoot && ThirdPersonRoot->GetAttachParent() && ThirdPersonRoot->GetAttachSocketName() != NAME_None)
	{
		ThirdPersonRoot->SetRelativeRotation(Stats.ThirdPersonRotation.Quaternion() * (FRotator(-9.f, 5.f, 14.f) * Frame.Tilt).Quaternion());
	}
	const FVector Scale = Frame.bMagVisible ? FVector::OneVector : FVector(0.0001f);
	for (USceneComponent* MagRoot : { FirstPersonMagRoot.Get(), ThirdPersonMagRoot.Get() })
	{
		if (MagRoot)
		{
			MagRoot->SetRelativeLocation(Frame.MagOffset);
			MagRoot->SetRelativeScale3D(Scale);
		}
	}
	if (FirstPersonMagGlow)
	{
		FirstPersonMagGlow->SetScalarParameterValue(TEXT("EmissiveStrength"), 3.5f * Frame.MagGlow);
	}
	if (ThirdPersonMagGlow)
	{
		ThirdPersonMagGlow->SetScalarParameterValue(TEXT("EmissiveStrength"), 12.f * Frame.MagGlow);
	}
}

void AShoterWeapon::ResetReloadVisuals()
{
	if (!bReloadVisualsActive)
	{
		return;
	}
	bReloadVisualsActive = false;
	if (ThirdPersonRoot && ThirdPersonRoot->GetAttachSocketName() != NAME_None)
	{
		ThirdPersonRoot->SetRelativeRotation(Stats.ThirdPersonRotation);
	}
	for (USceneComponent* MagRoot : { FirstPersonMagRoot.Get(), ThirdPersonMagRoot.Get() })
	{
		if (MagRoot)
		{
			MagRoot->SetRelativeLocation(FVector::ZeroVector);
			MagRoot->SetRelativeScale3D(FVector::OneVector);
		}
	}
	if (FirstPersonMagGlow)
	{
		FirstPersonMagGlow->SetScalarParameterValue(TEXT("EmissiveStrength"), 3.5f);
	}
	if (ThirdPersonMagGlow)
	{
		ThirdPersonMagGlow->SetScalarParameterValue(TEXT("EmissiveStrength"), 12.f);
	}
}

void AShoterWeapon::OnMagazineReleased()
{
	PlayReloadSound(Stats.MagOutSound, 0.9f);

	UShoterFXPool* Pool = UShoterFXPool::Get(this);
	if (!Pool || !OwningCharacter)
	{
		return;
	}

	const FShoterReloadSpec& R = Stats.Reload;
	const bool bFirstPerson = IsFirstPersonView();
	const USceneComponent* Root = bFirstPerson ? FirstPersonRoot.Get() : ThirdPersonRoot.Get();
	if (!Root)
	{
		return;
	}
	const FTransform ModelTransform = Root->GetComponentTransform();
	const FVector MagDirection = R.MagOut.GetSafeNormal();
	const FVector LocalPosition = R.MagCenter + MagDirection * R.MagTravel;

	FShoterDebrisParams Debris;
	Debris.Shape = EShoterShape::Cube;
	Debris.Size = R.MagSize * FMath::Max(ModelTransform.GetScale3D().X, 0.1f);
	Debris.BaseColor = Stats.AccentColor.GetLuminance() > 0.2f ? Stats.AccentColor * 0.6f : FLinearColor(0.06f, 0.065f, 0.075f);
	Debris.Metallic = 0.8f;
	Debris.Roughness = 0.4f;
	Debris.GlowColor = Stats.EnergyColor;
	Debris.GlowStrength = bFirstPerson ? 1.6f : 8.f;
	Debris.GlowTime = bFirstPerson ? 0.5f : 0.55f;
	Debris.Location = ModelTransform.TransformPosition(LocalPosition);
	Debris.Rotation = ModelTransform.TransformRotation(R.MagRotation.Quaternion()).Rotator();
	const FVector Velocity = ModelTransform.TransformVectorNoScale(MagDirection * 35.f + FVector(-15.f, -40.f, -20.f))
		+ OwningCharacter->GetVelocity().GetClampedToMaxSize(500.f) * 0.5f;
	Debris.Velocity = Velocity;
	Debris.AngularVelocity = FVector(FMath::FRandRange(-260.f, 260.f), FMath::FRandRange(-200.f, 200.f), FMath::FRandRange(-180.f, 180.f));
	Debris.bFirstPerson = bFirstPerson;
	if (bFirstPerson)
	{
		// Falls out of the picture within a fraction of a second; no world collision needed.
		Debris.GravityZ = -260.f;
		Debris.Lifetime = 1.3f;
		Debris.FadeTime = 0.3f;
		Debris.bCollide = false;
	}
	else
	{
		Debris.Lifetime = 3.f;
		Debris.FadeTime = 0.6f;
		Debris.bCollide = true;
		Debris.MaxBounces = 2;
		Debris.BounceSound = TEXT("Grenade_Bounce");
		Debris.BounceSoundPitch = 1.3f;
	}
	Pool->SpawnDebris(Debris);
}

void AShoterWeapon::OnMagazineSeated()
{
	PlayReloadSound(Stats.MagInSound, 1.f);
	if (IsFirstPersonView())
	{
		// A small bump of the whole gun as the cell locks in.
		AddViewKick(FVector(-1.1f, 0.f, -0.9f), FRotator(2.6f, 0.f, 0.f));
	}
}

void AShoterWeapon::OnActionRacked()
{
	PlayReloadSound(Stats.ChargeSound, 0.9f);
	if (IsFirstPersonView())
	{
		AddViewKick(FVector(-0.8f, 0.f, 0.f), FRotator(1.2f, 0.f, 0.f));
	}
}

void AShoterWeapon::AddReserveAmmo(int32 Amount)
{
	if (HasAuthority())
	{
		ReserveAmmo = FMath::Clamp(ReserveAmmo + Amount, 0, Stats.MaxReserve);
	}
}

void AShoterWeapon::RefillAmmo()
{
	if (HasAuthority())
	{
		AmmoInClip = Stats.MagSize;
		ReserveAmmo = Stats.MaxReserve;
	}
}

bool AShoterWeapon::IsAmmoFull() const
{
	return AmmoInClip >= Stats.MagSize && ReserveAmmo >= Stats.MaxReserve;
}
