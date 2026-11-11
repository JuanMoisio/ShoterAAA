#include "AI/ShoterZombie.h"
#include "AI/ShoterZombieController.h"
#include "Core/ShoterAssets.h"
#include "Game/ShoterGameMode_Zombies.h"
#include "Player/ShoterCharacter.h"
#include "ShoterAAA.h"
#include "Animation/AnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

namespace
{
	constexpr float ZombieCapsuleRadius = 34.f;
	constexpr float ZombieCapsuleHalfHeight = 96.f;

	/** Half speed for a short time after being hit. */
	constexpr float ZombieStaggerDuration = 0.2f;
	constexpr float ZombieStaggerSpeedScale = 0.5f;

	/** bAttacking stays up this long after the damage check so the swing animation can finish. */
	constexpr float ZombieAttackRecoveryTime = 0.3f;

	constexpr float ZombieCorpseLifeSpan = 6.f;

	/** Per-type appearance. Skin is the dark base color, Rim the emissive glow. */
	struct FZombieLook
	{
		FLinearColor Skin;
		FLinearColor Rim;
		float RimStrength;
		float Scale;
		float AnimRate;
	};

	FZombieLook GetZombieLook(EShoterZombieType Type)
	{
		switch (Type)
		{
		case EShoterZombieType::Runner:
			return { FLinearColor(0.16f, 0.10f, 0.05f), FLinearColor(1.0f, 0.42f, 0.04f), 0.5f, 0.92f, 1.25f };
		case EShoterZombieType::Brute:
			return { FLinearColor(0.15f, 0.05f, 0.05f), FLinearColor(1.0f, 0.08f, 0.04f), 0.6f, 1.35f, 1.0f };
		case EShoterZombieType::Walker:
		default:
			return { FLinearColor(0.09f, 0.14f, 0.07f), FLinearColor(0.25f, 1.0f, 0.2f), 0.45f, 1.0f, 1.0f };
		}
	}
}

AShoterZombie::AShoterZombie(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

	AIControllerClass = AShoterZombieController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Capsule: movement only. Weapon traces must go through it and hit the mesh instead (headshots).
	UCapsuleComponent* Capsule = GetCapsuleComponent();
	Capsule->InitCapsuleSize(ZombieCapsuleRadius, ZombieCapsuleHalfHeight);
	Capsule->SetCollisionResponseToChannel(SHOTER_TRACE_WEAPON, ECR_Ignore);

	// Mesh: query only, blocks weapon traces. Offset for the UE5 mannequin.
	USkeletalMeshComponent* MeshComp = GetMesh();
	MeshComp->SetRelativeLocation(FVector(0.f, 0.f, -ZombieCapsuleHalfHeight));
	MeshComp->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
	MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	MeshComp->SetCollisionResponseToAllChannels(ECR_Ignore);
	MeshComp->SetCollisionResponseToChannel(SHOTER_TRACE_WEAPON, ECR_Block);
	MeshComp->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->bOrientRotationToMovement = true;
	Move->bUseControllerDesiredRotation = false;
	Move->RotationRate = FRotator(0.f, 540.f, 0.f);
	Move->bUseRVOAvoidance = true;
	Move->AvoidanceConsiderationRadius = 250.f;
	Move->MaxWalkSpeed = BaseWalkSpeed;
}

void AShoterZombie::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AShoterZombie, ZombieType);
	DOREPLIFETIME(AShoterZombie, Health);
	DOREPLIFETIME(AShoterZombie, MaxHealth);
	DOREPLIFETIME(AShoterZombie, bDead);
	DOREPLIFETIME(AShoterZombie, bAttacking);
}

void AShoterZombie::BeginPlay()
{
	Super::BeginPlay();

	// Replicated state (type, dead) may already have arrived before the body existed, so (re)apply it here.
	SetupBody();
	ApplyLook();

	if (bDead)
	{
		StartDeathPresentation(-GetActorForwardVector());
	}
	else
	{
		ScheduleGrowl();
	}

	// Only the placeholder body is animated in Tick.
	if (!PlaceholderRoot)
	{
		SetActorTickEnabled(false);
	}
}

void AShoterZombie::InitZombie(EShoterZombieType InType, int32 InWave)
{
	if (!HasAuthority())
	{
		return;
	}

	const int32 Wave = FMath::Max(1, InWave);
	const float WaveHealthScale = 1.f + 0.12f * static_cast<float>(Wave - 1);

	float BaseHealth = 100.f;
	float Speed = 180.f;

	switch (InType)
	{
	case EShoterZombieType::Runner:
		BaseHealth = 70.f;
		Speed = FMath::Min(420.f + 10.f * static_cast<float>(Wave), 560.f);
		AttackDamage = 22.f;
		break;

	case EShoterZombieType::Brute:
		BaseHealth = 420.f;
		Speed = FMath::Min(150.f + 5.f * static_cast<float>(Wave), 240.f);
		AttackDamage = 55.f;
		AttackRange = 190.f;
		break;

	case EShoterZombieType::Walker:
	default:
		BaseHealth = 100.f;
		Speed = FMath::Min(180.f + 8.f * static_cast<float>(Wave), 300.f);
		AttackDamage = 30.f;
		break;
	}

	ZombieType = InType;
	MaxHealth = BaseHealth * WaveHealthScale;
	Health = MaxHealth;

	BaseWalkSpeed = Speed;
	GetCharacterMovement()->MaxWalkSpeed = Speed;

	// Replicates through OnRep_ZombieType on the clients; the server applies it here.
	ApplyLook();
}

void AShoterZombie::SetupBody()
{
	bool bUsingSkeletalMesh = false;

	if (!BodyMeshPath.IsNull())
	{
		if (USkeletalMesh* LoadedMesh = Cast<USkeletalMesh>(BodyMeshPath.TryLoad()))
		{
			USkeletalMeshComponent* MeshComp = GetMesh();
			MeshComp->SetSkeletalMesh(LoadedMesh);

			if (!BodyAnimClassPath.IsNull())
			{
				if (UClass* AnimClass = BodyAnimClassPath.TryLoadClass<UAnimInstance>())
				{
					MeshComp->SetAnimationMode(EAnimationMode::AnimationBlueprint);
					MeshComp->SetAnimInstanceClass(AnimClass);
				}
			}

			bUsingSkeletalMesh = true;
		}
	}

	if (!bUsingSkeletalMesh)
	{
		BuildPlaceholderBody();
	}
}

void AShoterZombie::BuildPlaceholderBody()
{
	UShoterAssets* Assets = UShoterAssets::Get(this);
	if (!Assets || PlaceholderRoot)
	{
		return;
	}

	// Pivot at the feet: the capsule center is ZombieCapsuleHalfHeight above them.
	PlaceholderRoot = NewObject<USceneComponent>(this, TEXT("PlaceholderRoot"));
	PlaceholderRoot->SetupAttachment(GetCapsuleComponent());
	PlaceholderRoot->SetRelativeLocation(FVector(0.f, 0.f, -ZombieCapsuleHalfHeight));
	PlaceholderRoot->RegisterComponent();

	// Shoulder pivot: the arms swing around it.
	PlaceholderArms = NewObject<USceneComponent>(this, TEXT("PlaceholderArms"));
	PlaceholderArms->SetupAttachment(PlaceholderRoot);
	PlaceholderArms->SetRelativeLocation(FVector(0.f, 0.f, 142.f));
	PlaceholderArms->RegisterComponent();

	auto AddPart = [this, Assets](USceneComponent* Parent, EShoterShape Shape, const FVector& Location, const FRotator& Rotation, const FVector& SizeCm, bool bShootable) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
		Part->SetMobility(EComponentMobility::Movable);
		Part->SetStaticMesh(Assets->GetShape(Shape));
		Part->SetupAttachment(Parent);
		Part->SetRelativeLocation(Location);
		Part->SetRelativeRotation(Rotation);
		Part->SetRelativeScale3D(SizeCm / 100.f);
		Part->SetGenerateOverlapEvents(false);
		Part->SetCanEverAffectNavigation(false);

		if (bShootable)
		{
			// Weapon traces must hit the body, everything else passes through (the capsule does the blocking).
			Part->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Part->SetCollisionResponseToAllChannels(ECR_Ignore);
			Part->SetCollisionResponseToChannel(SHOTER_TRACE_WEAPON, ECR_Block);
		}
		else
		{
			Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}

		Part->RegisterComponent();
		return Part;
	};

	// Legs
	PlaceholderParts.Add(AddPart(PlaceholderRoot, EShoterShape::Cube, FVector(0.f, -11.f, 42.f), FRotator::ZeroRotator, FVector(18.f, 16.f, 84.f), true));
	PlaceholderParts.Add(AddPart(PlaceholderRoot, EShoterShape::Cube, FVector(0.f, 11.f, 42.f), FRotator::ZeroRotator, FVector(18.f, 16.f, 84.f), true));
	// Torso, leaning forward
	PlaceholderParts.Add(AddPart(PlaceholderRoot, EShoterShape::Cube, FVector(2.f, 0.f, 122.f), FRotator(8.f, 0.f, 0.f), FVector(26.f, 44.f, 72.f), true));
	// Head: tagged so the weapon traces count a hit on it as a headshot (there are no bones on a static mesh).
	UStaticMeshComponent* HeadPart = AddPart(PlaceholderRoot, EShoterShape::Sphere, FVector(8.f, 0.f, 172.f), FRotator::ZeroRotator, FVector(30.f, 28.f, 30.f), true);
	HeadPart->ComponentTags.Add(SHOTER_HEAD_BONE);
	PlaceholderParts.Add(HeadPart);
	// Arms stretched forward
	PlaceholderParts.Add(AddPart(PlaceholderArms, EShoterShape::Cube, FVector(34.f, -28.f, 0.f), FRotator::ZeroRotator, FVector(64.f, 12.f, 12.f), true));
	PlaceholderParts.Add(AddPart(PlaceholderArms, EShoterShape::Cube, FVector(34.f, 28.f, 0.f), FRotator::ZeroRotator, FVector(64.f, 12.f, 12.f), true));
	// Glowing eyes
	PlaceholderEyes.Add(AddPart(PlaceholderRoot, EShoterShape::Sphere, FVector(21.f, -6.f, 175.f), FRotator::ZeroRotator, FVector(5.f, 5.f, 5.f), false));
	PlaceholderEyes.Add(AddPart(PlaceholderRoot, EShoterShape::Sphere, FVector(21.f, 6.f, 175.f), FRotator::ZeroRotator, FVector(5.f, 5.f, 5.f), false));
}

void AShoterZombie::ApplyLook()
{
	const FZombieLook Look = GetZombieLook(ZombieType);

	// Scale (the capsule scales with the actor). On the server, lift the actor so a bigger capsule does not sink into the floor.
	const FVector NewScale(Look.Scale);
	if (!GetActorScale3D().Equals(NewScale, KINDA_SMALL_NUMBER))
	{
		UCapsuleComponent* Capsule = GetCapsuleComponent();
		const float OldHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
		SetActorScale3D(NewScale);
		const float Growth = Capsule->GetScaledCapsuleHalfHeight() - OldHalfHeight;
		if (HasAuthority() && Growth > 0.f)
		{
			AddActorWorldOffset(FVector(0.f, 0.f, Growth), false, nullptr, ETeleportType::TeleportPhysics);
		}
	}

	USkeletalMeshComponent* MeshComp = GetMesh();
	MeshComp->GlobalAnimRateScale = Look.AnimRate;

	UShoterAssets* Assets = UShoterAssets::Get(this);
	if (!Assets)
	{
		return;
	}

	BodyMaterial = Assets->MakeSurfaceMaterial(this, Look.Skin, 0.05f, 0.8f, Look.Rim, Look.RimStrength);
	if (BodyMaterial)
	{
		const int32 NumSlots = MeshComp->GetNumMaterials();
		for (int32 SlotIndex = 0; SlotIndex < NumSlots; ++SlotIndex)
		{
			MeshComp->SetMaterial(SlotIndex, BodyMaterial);
		}

		for (UStaticMeshComponent* Part : PlaceholderParts)
		{
			if (Part)
			{
				Part->SetMaterial(0, BodyMaterial);
			}
		}
	}

	if (PlaceholderEyes.Num() > 0)
	{
		EyeMaterial = Assets->MakeGlowMaterial(this, Look.Rim, 30.f);
		if (EyeMaterial)
		{
			for (UStaticMeshComponent* Eye : PlaceholderEyes)
			{
				if (Eye)
				{
					Eye->SetMaterial(0, EyeMaterial);
				}
			}
		}
	}
}

void AShoterZombie::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Only the placeholder body is animated here; the skeletal mesh uses its anim blueprint.
	if (!PlaceholderRoot || bDead)
	{
		return;
	}

	const float SpeedAlpha = FMath::Clamp(static_cast<float>(GetVelocity().Size2D()) / 300.f, 0.f, 1.f);
	PlaceholderAnimTime += DeltaSeconds * (3.f + 9.f * SpeedAlpha);

	const float Sway = FMath::Sin(PlaceholderAnimTime) * 5.f * SpeedAlpha;
	const float Bob = FMath::Abs(FMath::Sin(PlaceholderAnimTime)) * 5.f * SpeedAlpha;
	PlaceholderRoot->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -ZombieCapsuleHalfHeight + Bob), FRotator(0.f, 0.f, Sway));

	// Arms: raised while walking, chopping down while attacking.
	const float TargetArmPitch = bAttacking ? -60.f : 10.f;
	PlaceholderArmPitch = FMath::FInterpTo(PlaceholderArmPitch, TargetArmPitch, DeltaSeconds, bAttacking ? 16.f : 6.f);
	if (PlaceholderArms)
	{
		PlaceholderArms->SetRelativeRotation(FRotator(PlaceholderArmPitch, 0.f, 0.f));
	}
}

bool AShoterZombie::IsTargetInAttackRange(const AActor* Target, float RangeScale) const
{
	if (!Target)
	{
		return false;
	}

	const FVector Delta = Target->GetActorLocation() - GetActorLocation();
	const float VerticalTolerance = 180.f * FMath::Max(static_cast<float>(GetActorScale3D().Z), 1.f);
	if (FMath::Abs(Delta.Z) > VerticalTolerance)
	{
		return false;
	}

	return Delta.Size2D() <= AttackRange * RangeScale;
}

// --- Damage and death (server) ---

float AShoterZombie::TakeDamage(float Damage, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (!HasAuthority() || !IsAlive() || Damage <= 0.f)
	{
		return 0.f;
	}

	// Zombies never hurt each other (splash from their own attacks, etc.).
	if (Cast<AShoterZombie>(DamageCauser) || (EventInstigator && Cast<AShoterZombie>(EventInstigator->GetPawn())))
	{
		return 0.f;
	}

	const float IncomingDamage = Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);
	if (IncomingDamage <= 0.f)
	{
		return 0.f;
	}

	bool bHeadshot = false;
	FVector HitDirection = FVector::ZeroVector;

	if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		const FPointDamageEvent& PointEvent = static_cast<const FPointDamageEvent&>(DamageEvent);
		bHeadshot = (PointEvent.HitInfo.BoneName == SHOTER_HEAD_BONE);
		HitDirection = PointEvent.ShotDirection;
	}
	else if (DamageEvent.IsOfType(FRadialDamageEvent::ClassID))
	{
		const FRadialDamageEvent& RadialEvent = static_cast<const FRadialDamageEvent&>(DamageEvent);
		HitDirection = GetActorLocation() - RadialEvent.Origin;
	}
	else if (DamageCauser)
	{
		HitDirection = GetActorLocation() - DamageCauser->GetActorLocation();
	}

	HitDirection = HitDirection.GetSafeNormal();
	if (HitDirection.IsNearlyZero())
	{
		HitDirection = -GetActorForwardVector();
	}

	const float AppliedDamage = FMath::Min(IncomingDamage, Health);
	Health = FMath::Max(0.f, Health - AppliedDamage);

	UWorld* World = GetWorld();
	if (AShoterGameMode_Zombies* GameMode = World ? World->GetAuthGameMode<AShoterGameMode_Zombies>() : nullptr)
	{
		GameMode->OnZombieDamaged(this, EventInstigator, AppliedDamage, bHeadshot);
	}

	if (Health <= 0.f)
	{
		HandleDeath(EventInstigator, bHeadshot, HitDirection);
	}
	else
	{
		// Brief stagger: half speed for a moment.
		GetCharacterMovement()->MaxWalkSpeed = BaseWalkSpeed * ZombieStaggerSpeedScale;
		GetWorldTimerManager().SetTimer(TimerHandle_Stagger, this, &AShoterZombie::EndStagger, ZombieStaggerDuration, false);
	}

	return AppliedDamage;
}

void AShoterZombie::EndStagger()
{
	if (IsAlive())
	{
		GetCharacterMovement()->MaxWalkSpeed = BaseWalkSpeed;
	}
}

void AShoterZombie::HandleDeath(AController* Killer, bool bHeadshot, const FVector& HitDirection)
{
	bDead = true;
	Health = 0.f;
	bAttacking = false;
	AttackTarget.Reset();

	// Score first, while the zombie and its controller are still intact.
	UWorld* World = GetWorld();
	if (AShoterGameMode_Zombies* GameMode = World ? World->GetAuthGameMode<AShoterGameMode_Zombies>() : nullptr)
	{
		GameMode->OnZombieKilled(this, Killer, bHeadshot);
	}

	// Stop the AI (the controller has no player state, so it destroys itself).
	DetachFromControllerPendingDestroy();

	// Capsule off, ragdoll and sound on this machine; clients do the same from OnRep_Dead.
	StartDeathPresentation(HitDirection);

	ForceNetUpdate();
	SetLifeSpan(ZombieCorpseLifeSpan);
}

void AShoterZombie::FellOutOfWorld(const UDamageType& DmgType)
{
	if (HasAuthority() && IsAlive())
	{
		bDead = true;
		Health = 0.f;

		UWorld* World = GetWorld();
		if (AShoterGameMode_Zombies* GameMode = World ? World->GetAuthGameMode<AShoterGameMode_Zombies>() : nullptr)
		{
			// No killer: the wave still has to count it.
			GameMode->OnZombieKilled(this, nullptr, false);
		}
	}

	DetachFromControllerPendingDestroy();
	Destroy();
}

void AShoterZombie::StartDeathPresentation(const FVector& ImpulseDirection)
{
	if (bDeathPresented)
	{
		return;
	}
	bDeathPresented = true;

	FTimerManager& TimerManager = GetWorldTimerManager();
	TimerManager.ClearTimer(TimerHandle_Growl);
	TimerManager.ClearTimer(TimerHandle_Attack);
	TimerManager.ClearTimer(TimerHandle_Stagger);

	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->StopMovementImmediately();
	Move->DisableMovement();

	// Nothing to see on a dedicated server.
	if (IsNetMode(NM_DedicatedServer))
	{
		return;
	}

	UShoterAssets::PlaySoundAt(this, TEXT("Zombie_Death"), GetActorLocation(), EShoterSoundRange::Medium);

	USkeletalMeshComponent* MeshComp = GetMesh();
	if (MeshComp && MeshComp->GetSkeletalMeshAsset())
	{
		MeshComp->SetCollisionProfileName(TEXT("Ragdoll"));
		// Corpses must not soak up bullets meant for the zombies behind them.
		MeshComp->SetCollisionResponseToChannel(SHOTER_TRACE_WEAPON, ECR_Ignore);
		MeshComp->SetAllBodiesSimulatePhysics(true);
		MeshComp->SetSimulatePhysics(true);
		MeshComp->WakeAllRigidBodies();
		MeshComp->bBlendPhysics = true;
		MeshComp->AddImpulse(ImpulseDirection * 300.f + FVector(0.f, 0.f, 150.f), NAME_None, true);
	}
	else if (PlaceholderRoot)
	{
		// No physics asset on the placeholder: just topple it over onto its side.
		for (UStaticMeshComponent* Part : PlaceholderParts)
		{
			if (Part)
			{
				Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
		}

		PlaceholderRoot->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -ZombieCapsuleHalfHeight + 14.f), FRotator(0.f, 0.f, 85.f));
	}
}

// --- Melee attack (server) ---

void AShoterZombie::StartAttack(AActor* Target)
{
	if (!HasAuthority() || !Target || !IsAlive() || bAttacking)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float Now = static_cast<float>(World->GetTimeSeconds());
	if (Now - LastAttackTime < AttackCooldown)
	{
		return;
	}

	LastAttackTime = Now;
	AttackTarget = Target;
	bAttacking = true;
	ForceNetUpdate();

	// Clients play it from OnRep_Attacking; the server (listen host) plays it here.
	PlayAttackSound();

	// Face the target (flat); the movement component does not turn a standing zombie.
	FVector ToTarget = Target->GetActorLocation() - GetActorLocation();
	ToTarget.Z = 0.f;
	if (!ToTarget.IsNearlyZero())
	{
		SetActorRotation(FRotator(0.f, ToTarget.Rotation().Yaw, 0.f));
	}

	GetWorldTimerManager().SetTimer(TimerHandle_Attack, this, &AShoterZombie::ResolveAttack, FMath::Max(0.05f, AttackWindup), false);
}

void AShoterZombie::ResolveAttack()
{
	AActor* Target = AttackTarget.Get();

	if (IsAlive() && Target && IsTargetInAttackRange(Target, 1.25f))
	{
		const AShoterCharacter* TargetCharacter = Cast<AShoterCharacter>(Target);
		if (!TargetCharacter || TargetCharacter->IsAlive())
		{
			UGameplayStatics::ApplyDamage(Target, AttackDamage, GetController(), this, UDamageType::StaticClass());
		}
	}

	GetWorldTimerManager().SetTimer(TimerHandle_Attack, this, &AShoterZombie::FinishAttack, ZombieAttackRecoveryTime, false);
}

void AShoterZombie::FinishAttack()
{
	bAttacking = false;
	AttackTarget.Reset();
	ForceNetUpdate();
}

void AShoterZombie::PlayAttackSound()
{
	UShoterAssets::PlaySoundAt(this, TEXT("Zombie_Attack"), GetActorLocation(), EShoterSoundRange::Medium);
}

// --- Cosmetic growls ---

void AShoterZombie::ScheduleGrowl()
{
	if (IsNetMode(NM_DedicatedServer) || bDead)
	{
		return;
	}

	GetWorldTimerManager().SetTimer(TimerHandle_Growl, this, &AShoterZombie::PlayGrowl, FMath::FRandRange(3.f, 7.f), false);
}

void AShoterZombie::PlayGrowl()
{
	if (bDead)
	{
		return;
	}

	static const FName GrowlSounds[3] = { FName(TEXT("Zombie_Growl_01")), FName(TEXT("Zombie_Growl_02")), FName(TEXT("Zombie_Growl_03")) };
	UShoterAssets::PlaySoundAt(this, GrowlSounds[FMath::RandRange(0, 2)], GetActorLocation(), EShoterSoundRange::Medium);

	ScheduleGrowl();
}

// --- Replication callbacks ---

void AShoterZombie::OnRep_ZombieType()
{
	ApplyLook();
}

void AShoterZombie::OnRep_Dead()
{
	// Before BeginPlay the body does not exist yet; BeginPlay presents the death in that case.
	if (bDead && HasActorBegunPlay())
	{
		StartDeathPresentation(-GetActorForwardVector());
	}
}

void AShoterZombie::OnRep_Attacking()
{
	if (bAttacking && !bDead)
	{
		PlayAttackSound();
	}
}
