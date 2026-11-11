#include "Weapons/ShoterWeapon.h"
#include "ShoterAAA.h"
#include "Weapons/ShoterProjectile.h"
#include "Player/ShoterCharacter.h"
#include "Player/ShoterPlayerController.h"
#include "Player/ShoterPlayerState.h"
#include "Player/ShoterPlayerCameraManager.h"
#include "AI/ShoterZombie.h"
#include "Core/ShoterFX.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
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
	Super::Destroyed();
}

void AShoterWeapon::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	SpreadBloom = FMath::FInterpConstantTo(SpreadBloom, 0.f, DeltaSeconds, Stats.BloomRecovery);
	KickOffset = FMath::VInterpTo(KickOffset, FVector::ZeroVector, DeltaSeconds, 14.f);
	KickRotation = FMath::RInterpTo(KickRotation, FRotator::ZeroRotator, DeltaSeconds, 12.f);
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
		PartMaterials.SetNum(4);
		PartMaterials[static_cast<int32>(EShoterPartMaterial::Body)] = Assets->MakeSurfaceMaterial(this, Stats.BodyColor, 0.75f, 0.32f);
		PartMaterials[static_cast<int32>(EShoterPartMaterial::Dark)] = Assets->MakeSurfaceMaterial(this, FLinearColor(0.025f, 0.027f, 0.03f), 0.9f, 0.45f);
		PartMaterials[static_cast<int32>(EShoterPartMaterial::Accent)] = Assets->MakeSurfaceMaterial(this, Stats.AccentColor, 0.6f, 0.3f);
		PartMaterials[static_cast<int32>(EShoterPartMaterial::Glow)] = Assets->MakeSurfaceMaterial(this, Stats.EnergyColor * 0.2f, 0.f, 0.3f, Stats.EnergyColor, 12.f);
	}

	for (const FShoterWeaponPart& Part : Parts)
	{
		UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(this);
		Mesh->SetStaticMesh(Assets->GetShape(Part.Shape));
		Mesh->SetupAttachment(Parent);
		Mesh->SetRelativeLocation(Part.Location);
		Mesh->SetRelativeRotation(Part.Rotation);
		Mesh->SetRelativeScale3D(Part.Size / 100.f);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetGenerateOverlapEvents(false);
		Mesh->SetMaterial(0, PartMaterials[static_cast<int32>(Part.Material)]);
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

void AShoterWeapon::PlayShotEffects(const FVector& Origin, const TArray<FVector_NetQuantize>& ImpactPoints, uint32 FleshMask)
{
	UWorld* World = GetWorld();
	const bool bLocalView = OwningCharacter && OwningCharacter->IsPlayerControlled() && IsOwnerLocallyControlled();
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

	for (int32 Index = 0; Index < ImpactPoints.Num(); ++Index)
	{
		const FVector End = ImpactPoints[Index];
		const bool bFlesh = (FleshMask & (1u << Index)) != 0;

		if (Stats.bBeamTracer)
		{
			FShoterFX::SpawnBeam(World, Muzzle, End, Stats.EnergyColor, Stats.TracerWidth * 2.f, 0.7f);
		}
		else if (Index == 0 || FMath::FRand() < 0.5f)
		{
			FShoterFX::SpawnTracer(World, Muzzle, End, Stats.EnergyColor, Stats.TracerWidth);
		}

		// Shots that reached max range hit nothing.
		if (FVector::DistSquared(Origin, End) < FMath::Square(Stats.Range * 0.98f))
		{
			FShoterFX::SpawnImpact(World, End, (Origin - End).GetSafeNormal(), Stats.EnergyColor, bFlesh);
		}
	}
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
	OwningCharacter->AddRecoil(Stats.RecoilPitch * AimScale * FMath::FRandRange(0.85f, 1.15f), Stats.RecoilYaw * AimScale * FMath::FRandRange(-1.f, 1.f));

	KickOffset.X -= Stats.KickBack * AimScale;
	KickRotation.Pitch += Stats.RecoilPitch * 1.5f * AimScale;

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
		Spread += Stats.MovingSpread * SpeedAlpha * FMath::Lerp(1.f, 0.4f, Aim);
		if (const UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			if (Movement->IsFalling())
			{
				Spread += Stats.AirSpread;
			}
		}
		if (Character->bIsCrouched)
		{
			Spread *= 0.8f;
		}
	}
	return FMath::Max(Spread, 0.f);
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

	if (IsOwnerLocallyControlled() && OwningCharacter->IsPlayerControlled())
	{
		UShoterAssets::PlaySound2D(this, Stats.ReloadSound, 0.8f);
	}
	else
	{
		UShoterAssets::PlaySoundAt(this, Stats.ReloadSound, GetActorLocation(), EShoterSoundRange::Short, 0.8f);
	}
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
}

void AShoterWeapon::OnRep_Reloading()
{
	if (bReloading)
	{
		ReloadStartTime = GetWorld()->GetTimeSeconds();
		UShoterAssets::PlaySoundAt(this, Stats.ReloadSound, GetActorLocation(), EShoterSoundRange::Short, 0.7f);
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
