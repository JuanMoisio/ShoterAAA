#include "Core/ShoterFX.h"
#include "Core/ShoterAssets.h"
#include "Player/ShoterPlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/DecalComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

namespace
{
	AShoterFXActor* SpawnFXActor(UWorld* World, const FVector& Location, const FRotator& Rotation = FRotator::ZeroRotator)
	{
		if (!World || World->GetNetMode() == NM_DedicatedServer)
		{
			return nullptr;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient;
		return World->SpawnActor<AShoterFXActor>(AShoterFXActor::StaticClass(), Location, Rotation, Params);
	}

	/** Rotation that aligns a basic cylinder (axis = Z) with the actor's X axis. */
	const FRotator CylinderAlongX(-90.f, 0.f, 0.f);
}

// ---------------------------------------------------------------------------
// AShoterFXActor
// ---------------------------------------------------------------------------

AShoterFXActor::AShoterFXActor()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
	SetCanBeDamaged(false);

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

UStaticMeshComponent* AShoterFXActor::AddPart(UStaticMesh* Mesh, UMaterialInterface* Material, const FTransform& RelativeTransform, const FVector& PartVelocity)
{
	if (!Mesh)
	{
		return nullptr;
	}

	UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
	Part->SetStaticMesh(Mesh);
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Part->SetGenerateOverlapEvents(false);
	Part->SetCastShadow(false);
	Part->SetupAttachment(RootComponent);
	Part->SetRelativeTransform(RelativeTransform);
	if (Material)
	{
		Part->SetMaterial(0, Material);
	}
	Part->RegisterComponent();

	FPart& Entry = Parts.AddDefaulted_GetRef();
	Entry.Mesh = Part;
	Entry.Material = Cast<UMaterialInstanceDynamic>(Material);
	Entry.StartScale = RelativeTransform.GetScale3D();
	Entry.Velocity = PartVelocity;

	KeepAlive.Add(Part);
	if (Material)
	{
		KeepAlive.AddUnique(Material);
	}
	return Part;
}

UPointLightComponent* AShoterFXActor::AddLight(const FLinearColor& Color, float Intensity, float Radius)
{
	UPointLightComponent* NewLight = NewObject<UPointLightComponent>(this);
	NewLight->SetupAttachment(RootComponent);
	NewLight->SetIntensityUnits(ELightUnits::Unitless);
	NewLight->SetIntensity(Intensity);
	NewLight->SetLightColor(Color);
	NewLight->SetAttenuationRadius(Radius);
	NewLight->SetCastShadows(false);
	NewLight->RegisterComponent();

	Light = NewLight;
	StartLightIntensity = Intensity;
	return NewLight;
}

void AShoterFXActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	Age += DeltaSeconds;
	const float Alpha = FMath::Clamp(Age / FMath::Max(Lifetime, 0.01f), 0.f, 1.f);
	const float Fade = 1.f - Alpha;

	if (Behavior == EShoterFXBehavior::Travel)
	{
		const FVector Step = Velocity * DeltaSeconds;
		AddActorWorldOffset(Step);
		Travelled += Step.Size();
		if (TravelDistance > 0.f && Travelled >= TravelDistance)
		{
			Destroy();
			return;
		}
	}

	for (FPart& Part : Parts)
	{
		if (!Part.Mesh)
		{
			continue;
		}
		if (Behavior == EShoterFXBehavior::Burst)
		{
			Part.Velocity.Z += GravityZ * DeltaSeconds;
			Part.Mesh->AddWorldOffset(Part.Velocity * DeltaSeconds);
		}
		if (Behavior != EShoterFXBehavior::Travel)
		{
			const float ScaleMultiplier = FMath::Max(FMath::Lerp(1.f, EndScale, Alpha), 0.001f);
			Part.Mesh->SetRelativeScale3D(Part.StartScale * ScaleMultiplier);
		}
		if (Part.Material)
		{
			Part.Material->SetScalarParameterValue(TEXT("Intensity"), GlowIntensity * Fade);
		}
	}

	if (Light)
	{
		Light->SetIntensity(StartLightIntensity * Fade * Fade);
	}

	if (Age >= Lifetime)
	{
		Destroy();
	}
}

// ---------------------------------------------------------------------------
// FShoterFX
// ---------------------------------------------------------------------------

void FShoterFX::SpawnTracer(UWorld* World, const FVector& Start, const FVector& End, const FLinearColor& Color, float Width, float Speed)
{
	const FVector Delta = End - Start;
	const float Distance = Delta.Size();
	if (Distance < 100.f)
	{
		return;
	}

	UShoterAssets* Assets = UShoterAssets::Get(World);
	AShoterFXActor* FX = Assets ? SpawnFXActor(World, Start, Delta.Rotation()) : nullptr;
	if (!FX)
	{
		return;
	}

	const float StreakLength = FMath::Min(420.f, Distance * 0.5f);
	const FVector Scale(Width / 100.f, Width / 100.f, StreakLength / 100.f);
	FX->AddPart(Assets->GetShape(EShoterShape::Cylinder), Assets->MakeGlowMaterial(FX, Color, 40.f),
		FTransform(CylinderAlongX, FVector(StreakLength * 0.5f, 0.f, 0.f), Scale));

	FX->Behavior = EShoterFXBehavior::Travel;
	FX->Velocity = Delta / Distance * Speed;
	FX->TravelDistance = FMath::Max(10.f, Distance - StreakLength);
	FX->Lifetime = Distance / Speed + 0.05f;
	FX->GlowIntensity = 40.f;
}

void FShoterFX::SpawnBeam(UWorld* World, const FVector& Start, const FVector& End, const FLinearColor& Color, float Width, float Lifetime)
{
	const FVector Delta = End - Start;
	const float Distance = Delta.Size();
	if (Distance < 10.f)
	{
		return;
	}

	UShoterAssets* Assets = UShoterAssets::Get(World);
	AShoterFXActor* FX = Assets ? SpawnFXActor(World, Start, Delta.Rotation()) : nullptr;
	if (!FX)
	{
		return;
	}

	UStaticMesh* Cylinder = Assets->GetShape(EShoterShape::Cylinder);
	// Bright core + wide soft halo.
	FX->AddPart(Cylinder, Assets->MakeGlowMaterial(FX, FLinearColor::White, 60.f),
		FTransform(CylinderAlongX, FVector(Distance * 0.5f, 0.f, 0.f), FVector(Width * 0.4f / 100.f, Width * 0.4f / 100.f, Distance / 100.f)));
	FX->AddPart(Cylinder, Assets->MakeGlowMaterial(FX, Color, 25.f),
		FTransform(CylinderAlongX, FVector(Distance * 0.5f, 0.f, 0.f), FVector(Width / 100.f, Width / 100.f, Distance / 100.f)));
	FX->AddLight(Color, 8000.f, 400.f);

	FX->Behavior = EShoterFXBehavior::Fade;
	FX->Lifetime = Lifetime;
	FX->EndScale = 0.05f;
	FX->GlowIntensity = 40.f;
}

void FShoterFX::SpawnImpact(UWorld* World, const FVector& Location, const FVector& Normal, const FLinearColor& Color, bool bFlesh)
{
	UShoterAssets* Assets = UShoterAssets::Get(World);
	AShoterFXActor* FX = Assets ? SpawnFXActor(World, Location) : nullptr;
	if (!FX)
	{
		return;
	}

	const FLinearColor FlashColor = bFlesh ? FLinearColor(1.f, 0.12f, 0.05f) : Color;
	FX->AddPart(Assets->GetShape(EShoterShape::Sphere), Assets->MakeGlowMaterial(FX, FlashColor, 30.f),
		FTransform(FRotator::ZeroRotator, FVector::ZeroVector, FVector(bFlesh ? 0.18f : 0.12f)));

	UMaterialInstanceDynamic* SparkMaterial = Assets->MakeGlowMaterial(FX, bFlesh ? FLinearColor(0.6f, 0.02f, 0.02f) : Color, 45.f);
	UStaticMesh* Cube = Assets->GetShape(EShoterShape::Cube);
	const int32 NumSparks = bFlesh ? 5 : 9;
	for (int32 Index = 0; Index < NumSparks; ++Index)
	{
		const FVector Direction = FMath::VRandCone(Normal, FMath::DegreesToRadians(bFlesh ? 45.f : 65.f));
		const float Speed = FMath::FRandRange(250.f, bFlesh ? 500.f : 950.f);
		FX->AddPart(Cube, SparkMaterial, FTransform(Direction.Rotation(), FVector::ZeroVector, FVector(0.05f, 0.01f, 0.01f)), Direction * Speed);
	}

	FX->AddLight(FlashColor, bFlesh ? 1200.f : 2500.f, 220.f);
	FX->Behavior = EShoterFXBehavior::Burst;
	FX->Lifetime = 0.35f;
	FX->EndScale = 0.1f;
	FX->GravityZ = -1400.f;
	FX->GlowIntensity = 40.f;

	UShoterAssets::PlaySoundAt(World, bFlesh ? FName(TEXT("Impact_Flesh")) : FName(TEXT("Impact_Surface")), Location, EShoterSoundRange::Short,
		bFlesh ? 0.9f : 1.2f, FMath::FRandRange(0.9f, 1.1f));

	if (!bFlesh)
	{
		if (UMaterialInterface* DecalMaterial = Assets->GetMaterial(TEXT("M_Shoter_ImpactDecal")))
		{
			const FRotator DecalRotation = (-Normal).Rotation() + FRotator(0.f, 0.f, FMath::FRandRange(-180.f, 180.f));
			if (UDecalComponent* Decal = UGameplayStatics::SpawnDecalAtLocation(World, DecalMaterial, FVector(4.f, 7.f, 7.f), Location, DecalRotation, 12.f))
			{
				Decal->SetFadeOut(8.f, 4.f, false);
				Decal->SetFadeScreenSize(0.002f);
			}
		}
	}
}

void FShoterFX::SpawnMuzzleFlash(USceneComponent* AttachTo, const FVector& RelativeLocation, const FLinearColor& Color, float Scale, bool bOnlyOwnerSee, bool bOwnerNoSee)
{
	if (!AttachTo)
	{
		return;
	}
	UWorld* World = AttachTo->GetWorld();
	UShoterAssets* Assets = UShoterAssets::Get(World);
	AShoterFXActor* FX = Assets ? SpawnFXActor(World, AttachTo->GetComponentTransform().TransformPosition(RelativeLocation)) : nullptr;
	if (!FX)
	{
		return;
	}

	FX->SetOwner(AttachTo->GetOwner());
	FX->AttachToComponent(AttachTo, FAttachmentTransformRules::KeepWorldTransform);
	FX->SetActorRelativeLocation(RelativeLocation);
	FX->SetActorRelativeRotation(FRotator::ZeroRotator);

	UMaterialInstanceDynamic* FlashMaterial = Assets->MakeGlowMaterial(FX, Color, 35.f);
	UMaterialInstanceDynamic* CoreMaterial = Assets->MakeGlowMaterial(FX, FLinearColor::White, 45.f);
	TArray<UStaticMeshComponent*> Created;
	Created.Add(FX->AddPart(Assets->GetShape(EShoterShape::Sphere), CoreMaterial, FTransform(FRotator::ZeroRotator, FVector(1.f, 0.f, 0.f), FVector(0.05f * Scale))));
	Created.Add(FX->AddPart(Assets->GetShape(EShoterShape::Cone), FlashMaterial,
		FTransform(CylinderAlongX, FVector(5.f * Scale, 0.f, 0.f), FVector(0.07f * Scale, 0.07f * Scale, 0.12f * Scale))));
	for (int32 Petal = 0; Petal < 4; ++Petal)
	{
		const FRotator PetalRotation(0.f, 0.f, 45.f + 90.f * Petal);
		Created.Add(FX->AddPart(Assets->GetShape(EShoterShape::Cube), FlashMaterial,
			FTransform(PetalRotation, FVector(2.f * Scale, 0.f, 0.f), FVector(0.06f * Scale, 0.005f, 0.09f * Scale))));
	}
	for (UStaticMeshComponent* Part : Created)
	{
		if (Part)
		{
			Part->SetOnlyOwnerSee(bOnlyOwnerSee);
			Part->SetOwnerNoSee(bOwnerNoSee);
		}
	}

	FX->AddLight(Color, 3500.f * Scale, 350.f * Scale);
	FX->Behavior = EShoterFXBehavior::Fade;
	FX->Lifetime = 0.06f;
	FX->EndScale = 0.4f;
	FX->GlowIntensity = 40.f;
}

void FShoterFX::SpawnExplosion(UWorld* World, const FVector& Location, float Radius, const FLinearColor& Color)
{
	UShoterAssets* Assets = UShoterAssets::Get(World);
	AShoterFXActor* FX = Assets ? SpawnFXActor(World, Location) : nullptr;
	if (FX)
	{
		UStaticMesh* Sphere = Assets->GetShape(EShoterShape::Sphere);
		const float BallScale = Radius / 100.f * 0.35f;
		// Expanding glow shell (scales up over its lifetime) + white core.
		FX->AddPart(Sphere, Assets->MakeGlowMaterial(FX, Color, 20.f), FTransform(FRotator::ZeroRotator, FVector::ZeroVector, FVector(BallScale)));
		FX->AddPart(Sphere, Assets->MakeGlowMaterial(FX, FLinearColor::White, 35.f), FTransform(FRotator::ZeroRotator, FVector::ZeroVector, FVector(BallScale * 0.5f)));

		UMaterialInstanceDynamic* SparkMaterial = Assets->MakeGlowMaterial(FX, Color, 50.f);
		UStaticMesh* Cube = Assets->GetShape(EShoterShape::Cube);
		for (int32 Index = 0; Index < 16; ++Index)
		{
			const FVector Direction = FMath::VRandCone(FVector::UpVector, FMath::DegreesToRadians(80.f));
			FX->AddPart(Cube, SparkMaterial, FTransform(Direction.Rotation(), FVector::ZeroVector, FVector(0.09f, 0.015f, 0.015f)),
				Direction * FMath::FRandRange(600.f, 1500.f));
		}

		FX->AddLight(Color, 40000.f, Radius * 3.f);
		FX->Behavior = EShoterFXBehavior::Burst;
		FX->Lifetime = 0.6f;
		FX->EndScale = 2.6f;
		FX->GravityZ = -900.f;
		FX->GlowIntensity = 30.f;

		if (UMaterialInterface* Scorch = Assets->GetMaterial(TEXT("M_Shoter_ScorchDecal")))
		{
			if (UDecalComponent* Decal = UGameplayStatics::SpawnDecalAtLocation(World, Scorch, FVector(60.f, Radius * 0.5f, Radius * 0.5f), Location,
				FRotator(-90.f, 0.f, FMath::FRandRange(-180.f, 180.f)), 20.f))
			{
				Decal->SetFadeOut(12.f, 6.f, false);
			}
		}
	}

	UShoterAssets::PlaySoundAt(World, TEXT("Explosion"), Location, EShoterSoundRange::VeryLong, 1.f, FMath::FRandRange(0.92f, 1.05f));
	AShoterPlayerCameraManager::AddTraumaAtLocation(World, Location, Radius * 4.f, 0.85f);
}

void FShoterFX::SpawnTeleportFlash(UWorld* World, const FVector& Location, const FLinearColor& Color)
{
	UShoterAssets* Assets = UShoterAssets::Get(World);
	AShoterFXActor* FX = Assets ? SpawnFXActor(World, Location) : nullptr;
	if (!FX)
	{
		return;
	}

	UStaticMesh* Cylinder = Assets->GetShape(EShoterShape::Cylinder);
	FX->AddPart(Cylinder, Assets->MakeGlowMaterial(FX, Color, 20.f), FTransform(FRotator::ZeroRotator, FVector(0.f, 0.f, 100.f), FVector(0.7f, 0.7f, 2.2f)));
	FX->AddPart(Cylinder, Assets->MakeGlowMaterial(FX, FLinearColor::White, 30.f), FTransform(FRotator::ZeroRotator, FVector(0.f, 0.f, 100.f), FVector(0.15f, 0.15f, 2.4f)));
	FX->AddPart(Cylinder, Assets->MakeGlowMaterial(FX, Color, 25.f), FTransform(FRotator::ZeroRotator, FVector(0.f, 0.f, 2.f), FVector(1.4f, 1.4f, 0.03f)));
	FX->AddLight(Color, 12000.f, 500.f);

	FX->Behavior = EShoterFXBehavior::Fade;
	FX->Lifetime = 0.7f;
	FX->EndScale = 0.f;
	FX->GlowIntensity = 25.f;

	UShoterAssets::PlaySoundAt(World, TEXT("Respawn"), Location, EShoterSoundRange::Medium, 0.8f);
}
