#include "Weapons/ShoterProjectile.h"
#include "ShoterAAA.h"
#include "Core/ShoterAssets.h"
#include "Core/ShoterFX.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

AShoterProjectile::AShoterProjectile()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);
	InitialLifeSpan = 10.f;

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(9.f);
	Collision->SetCollisionObjectType(SHOTER_OBJECT_PROJECTILE);
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Collision->SetCollisionResponseToAllChannels(ECR_Block);
	Collision->SetCollisionResponseToChannel(SHOTER_TRACE_WEAPON, ECR_Ignore);
	Collision->SetCollisionResponseToChannel(SHOTER_OBJECT_PROJECTILE, ECR_Ignore);
	Collision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	Collision->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Collision->SetGenerateOverlapEvents(false);
	RootComponent = Collision;

	Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
	Movement->UpdatedComponent = Collision;
	Movement->InitialSpeed = 3000.f;
	Movement->MaxSpeed = 8000.f;
	Movement->bRotationFollowsVelocity = true;
	Movement->ProjectileGravityScale = 0.f;
	Movement->bShouldBounce = false;

	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visual"));
	Visual->SetupAttachment(Collision);
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Visual->SetCastShadow(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		Visual->SetStaticMesh(SphereMesh.Object);
	}

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Collision);
	Light->SetIntensityUnits(ELightUnits::Unitless);
	Light->SetIntensity(2500.f);
	Light->SetAttenuationRadius(320.f);
	Light->SetCastShadows(false);
}

void AShoterProjectile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AShoterProjectile, bExploded);
	DOREPLIFETIME(AShoterProjectile, ExplosionLocation);
}

void AShoterProjectile::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	Movement->OnProjectileStop.AddDynamic(this, &AShoterProjectile::OnProjectileStop);
	Movement->OnProjectileBounce.AddDynamic(this, &AShoterProjectile::OnProjectileBounce);
	if (APawn* InstigatorPawn = GetInstigator())
	{
		Collision->IgnoreActorWhenMoving(InstigatorPawn, true);
	}
}

void AShoterProjectile::BeginPlay()
{
	Super::BeginPlay();

	Visual->SetRelativeScale3D(FVector(VisualDiameter / 100.f));
	if (UShoterAssets* Assets = UShoterAssets::Get(this))
	{
		Visual->SetMaterial(0, Assets->MakeGlowMaterial(this, Color, 30.f));
	}
	Light->SetLightColor(Color);

	// Glow tail (cone with its tip pointing backwards) and a soft halo: cosmetic, no collision, no gameplay change.
	UShoterAssets* Assets = UShoterAssets::Get(this);
	if (Assets && Movement->bRotationFollowsVelocity && GetNetMode() != NM_DedicatedServer)
	{
		const float Length = VisualDiameter * 6.f;
		Trail = NewObject<UStaticMeshComponent>(this, TEXT("Trail"));
		Trail->SetStaticMesh(Assets->GetShape(EShoterShape::Cone));
		Trail->SetupAttachment(Collision);
		Trail->SetRelativeLocationAndRotation(FVector(-(Length * 0.5f + VisualDiameter * 0.25f), 0.f, 0.f), FRotator(90.f, 0.f, 0.f));
		Trail->SetRelativeScale3D(FVector(VisualDiameter * 0.9f / 100.f, VisualDiameter * 0.9f / 100.f, Length / 100.f));
		Trail->SetMaterial(0, Assets->MakeGlowMaterial(this, Color, 7.f));
		Trail->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Trail->SetCastShadow(false);
		Trail->RegisterComponent();

		Halo = NewObject<UStaticMeshComponent>(this, TEXT("Halo"));
		Halo->SetStaticMesh(Assets->GetShape(EShoterShape::Sphere));
		Halo->SetupAttachment(Collision);
		Halo->SetRelativeScale3D(FVector(VisualDiameter * 2.1f / 100.f));
		Halo->SetMaterial(0, Assets->MakeGlowMaterial(this, Color, 2.5f));
		Halo->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Halo->SetCastShadow(false);
		Halo->RegisterComponent();
	}

	if (HasAuthority() && FuseTime > 0.f)
	{
		GetWorldTimerManager().SetTimer(TimerHandle_Fuse, this, &AShoterProjectile::OnFuseExpired, FuseTime, false);
	}
}

void AShoterProjectile::InitProjectile(const FVector& Direction, float Speed, AController* InInstigatorController, const FString& InWeaponName)
{
	Movement->Velocity = Direction.GetSafeNormal() * Speed;
	InstigatorController = InInstigatorController;
	WeaponName = InWeaponName;
}

void AShoterProjectile::OnProjectileStop(const FHitResult& ImpactResult)
{
	if (HasAuthority() && bExplodeOnImpact)
	{
		Explode(ImpactResult);
	}
}

void AShoterProjectile::OnProjectileBounce(const FHitResult& ImpactResult, const FVector& ImpactVelocity)
{
	if (!BounceSound.IsNone() && ImpactVelocity.SizeSquared() > FMath::Square(150.f))
	{
		UShoterAssets::PlaySoundAt(this, BounceSound, ImpactResult.ImpactPoint, EShoterSoundRange::Short, FMath::Clamp(ImpactVelocity.Size() / 900.f, 0.2f, 1.f));
	}
	if (HasAuthority() && bExplodeOnImpact)
	{
		Explode(ImpactResult);
	}
}

void AShoterProjectile::OnFuseExpired()
{
	FHitResult NoHit;
	NoHit.ImpactPoint = GetActorLocation();
	NoHit.Location = GetActorLocation();
	Explode(NoHit);
}

void AShoterProjectile::Explode(const FHitResult& Impact)
{
	if (bExploded || !HasAuthority())
	{
		return;
	}
	bExploded = true;
	GetWorldTimerManager().ClearTimer(TimerHandle_Fuse);

	const FVector Location = Impact.bBlockingHit ? FVector(Impact.ImpactPoint + Impact.ImpactNormal * 10.f) : GetActorLocation();
	ExplosionLocation = Location;
	AController* DamageInstigator = InstigatorController.Get();

	// Direct hit bonus on whoever we touched.
	AActor* DirectTarget = Impact.GetActor();
	if (DirectHitDamage > 0.f && DirectTarget && DirectTarget->IsA<APawn>())
	{
		const FPointDamageEvent DirectEvent(DirectHitDamage, Impact, GetVelocity().GetSafeNormal(), UDamageType::StaticClass());
		DirectTarget->TakeDamage(DirectHitDamage, DirectEvent, DamageInstigator, this);
	}

	TArray<AActor*> IgnoreActors;
	UGameplayStatics::ApplyRadialDamageWithFalloff(this, ExplosionDamage, ExplosionMinDamage, Location, ExplosionInnerRadius, ExplosionRadius, 1.f,
		UDamageType::StaticClass(), IgnoreActors, this, DamageInstigator, ECC_Visibility);

	PlayExplosionEffects();

	// Stay around briefly so clients receive bExploded, then go away.
	Movement->StopMovementImmediately();
	Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Visual->SetHiddenInGame(true);
	Light->SetVisibility(false);
	if (Trail) { Trail->SetHiddenInGame(true); }
	if (Halo) { Halo->SetHiddenInGame(true); }
	SetLifeSpan(1.f);
}

void AShoterProjectile::OnRep_Exploded()
{
	if (bExploded)
	{
		Visual->SetHiddenInGame(true);
		Light->SetVisibility(false);
		if (Trail) { Trail->SetHiddenInGame(true); }
		if (Halo) { Halo->SetHiddenInGame(true); }
		PlayExplosionEffects();
	}
}

void AShoterProjectile::PlayExplosionEffects()
{
	const FVector Location = ExplosionLocation.IsNearlyZero() ? GetActorLocation() : FVector(ExplosionLocation);
	FShoterFX::SpawnExplosion(GetWorld(), Location, ExplosionRadius, Color);
}

// ---------------------------------------------------------------------------

AShoterPlasmaOrb::AShoterPlasmaOrb()
{
	DirectHitDamage = 40.f;
	ExplosionDamage = 100.f;
	ExplosionMinDamage = 15.f;
	ExplosionInnerRadius = 100.f;
	ExplosionRadius = 380.f;
	bExplodeOnImpact = true;
	Color = FLinearColor(0.4f, 1.f, 0.2f);
	VisualDiameter = 18.f;
	Collision->InitSphereRadius(10.f);
	Movement->ProjectileGravityScale = 0.f;
}

AShoterGrenade::AShoterGrenade()
{
	DirectHitDamage = 0.f;
	ExplosionDamage = 140.f;
	ExplosionMinDamage = 20.f;
	ExplosionInnerRadius = 150.f;
	ExplosionRadius = 520.f;
	FuseTime = 2.2f;
	bExplodeOnImpact = false;
	Color = FLinearColor(1.f, 0.45f, 0.1f);
	VisualDiameter = 11.f;
	BounceSound = TEXT("Grenade_Bounce");

	Collision->InitSphereRadius(7.f);
	Movement->ProjectileGravityScale = 1.f;
	Movement->bShouldBounce = true;
	Movement->Bounciness = 0.35f;
	Movement->Friction = 0.4f;
	Movement->bRotationFollowsVelocity = false;
	Light->SetIntensity(1200.f);
	Light->SetAttenuationRadius(200.f);
}
