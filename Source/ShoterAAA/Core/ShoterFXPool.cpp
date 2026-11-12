#include "Core/ShoterFXPool.h"
#include "Core/ShoterAssets.h"
#include "Core/ShoterFX.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "Stats/Stats.h"

namespace
{
	/** Basic cylinders and cones point up (Z). Rotating by this turns local Z into local X (forward). */
	const FQuat ZToForward = FRotator(-90.f, 0.f, 0.f).Quaternion();
	/** Same for a cone whose tip should point backwards (-X). */
	const FQuat ZToBackward = FRotator(90.f, 0.f, 0.f).Quaternion();

	/** Fraction of the bolt that is the tapering tail. */
	constexpr float TailFraction = 0.62f;
	constexpr float TailIntensityScale = 0.4f;

	bool GetViewerLocation(const UWorld* World, FVector& OutLocation)
	{
		const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		if (PC && PC->PlayerCameraManager)
		{
			OutLocation = PC->PlayerCameraManager->GetCameraLocation();
			return true;
		}
		return false;
	}
}

AShoterFXPoolHost::AShoterFXPoolHost()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetCanBeDamaged(false);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

UShoterFXPool* UShoterFXPool::Get(const UObject* WorldContextObject)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UShoterFXPool>() : nullptr;
}

bool UShoterFXPool::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && !IsRunningDedicatedServer() && World->GetNetMode() != NM_DedicatedServer;
}

void UShoterFXPool::Deinitialize()
{
	Bolts.Reset();
	Beams.Reset();
	Debris.Reset();
	BoltMaterials.Reset();
	bPoolsBuilt = false;
	Super::Deinitialize();
}

TStatId UShoterFXPool::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UShoterFXPool, STATGROUP_Tickables);
}

UStaticMeshComponent* UShoterFXPool::MakeComponent(UStaticMesh* Mesh, UMaterialInterface* Material)
{
	UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Host);
	Component->SetStaticMesh(Mesh);
	Component->SetMobility(EComponentMobility::Movable);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetGenerateOverlapEvents(false);
	Component->SetCastShadow(false);
	Component->SetReceivesDecals(false);
	Component->SetCanEverAffectNavigation(false);
	Component->bAffectDynamicIndirectLighting = false;
	Component->bAffectDistanceFieldLighting = false;
	if (Material)
	{
		Component->SetMaterial(0, Material);
	}
	Component->SetupAttachment(Host->GetRootComponent());
	Component->RegisterComponent();
	Component->SetVisibility(false);
	return Component;
}

void UShoterFXPool::EnsurePools()
{
	if (bPoolsBuilt)
	{
		return;
	}
	UWorld* World = GetWorld();
	UShoterAssets* Assets = UShoterAssets::Get(World);
	if (!World || !Assets)
	{
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.ObjectFlags |= RF_Transient;
	Host = World->SpawnActor<AShoterFXPoolHost>(AShoterFXPoolHost::StaticClass(), FTransform::Identity, SpawnParams);
	if (!Host)
	{
		return;
	}
	bPoolsBuilt = true;

	UStaticMesh* Cylinder = Assets->GetShape(EShoterShape::Cylinder);
	UStaticMesh* Cone = Assets->GetShape(EShoterShape::Cone);

	Bolts.SetNum(MaxBolts);
	for (FBolt& Bolt : Bolts)
	{
		Bolt.Head = MakeComponent(Cylinder, nullptr);
		Bolt.Tail = MakeComponent(Cone, nullptr);
	}

	Beams.SetNum(MaxBeams);
	for (FBeam& Beam : Beams)
	{
		Beam.CoreMaterial = Assets->MakeGlowMaterial(this, FLinearColor::White, 10.f);
		Beam.HaloMaterial = Assets->MakeGlowMaterial(this, FLinearColor::White, 10.f);
		KeepAlive.Add(Beam.CoreMaterial);
		KeepAlive.Add(Beam.HaloMaterial);
		Beam.Core = MakeComponent(Cylinder, Beam.CoreMaterial);
		Beam.Halo = MakeComponent(Cylinder, Beam.HaloMaterial);
	}

	Streaks.SetNum(MaxStreaks);
	for (FStreak& Streak : Streaks)
	{
		Streak.Material = Assets->MakeGlowMaterial(this, FLinearColor::White, 3.f);
		KeepAlive.Add(Streak.Material);
		Streak.Mesh = MakeComponent(Cylinder, Streak.Material);
	}

	Debris.SetNum(MaxDebris);
	for (FDebris& Piece : Debris)
	{
		Piece.Material = Assets->MakeSurfaceMaterial(this, FLinearColor(0.03f, 0.03f, 0.035f), 0.9f, 0.4f);
		KeepAlive.Add(Piece.Material);
		Piece.Mesh = MakeComponent(Assets->GetShape(EShoterShape::Cube), Piece.Material);
	}
}

UMaterialInstanceDynamic* UShoterFXPool::GetBoltMaterial(const FLinearColor& Color, float Intensity)
{
	const FColor Quantized = Color.ToFColor(false);
	const uint32 Key = HashCombine(GetTypeHash(Quantized.DWColor()), GetTypeHash(FMath::RoundToInt(Intensity * 10.f)));
	if (UMaterialInstanceDynamic** Found = BoltMaterials.Find(Key))
	{
		return *Found;
	}
	UShoterAssets* Assets = UShoterAssets::Get(this);
	UMaterialInstanceDynamic* Material = Assets ? Assets->MakeGlowMaterial(this, Color, Intensity) : nullptr;
	if (Material)
	{
		KeepAlive.Add(Material);
		BoltMaterials.Add(Key, Material);
	}
	return Material;
}

FVector UShoterFXPool::ToFirstPersonSpace(const UObject* WorldContextObject, const FVector& RawWorldPosition)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (PC && PC->PlayerCameraManager)
	{
		const FMinimalViewInfo& View = PC->PlayerCameraManager->GetCameraCacheView();
		if (View.bUseFirstPersonParameters)
		{
			return View.TransformWorldToFirstPerson(RawWorldPosition, true);
		}
	}
	return RawWorldPosition;
}

// ---------------------------------------------------------------------------
// Bolts
// ---------------------------------------------------------------------------

void UShoterFXPool::SpawnBolt(const FShoterBoltParams& Params)
{
	EnsurePools();
	if (!bPoolsBuilt)
	{
		return;
	}
	const FVector Delta = Params.End - Params.Start;
	const float Distance = Delta.Size();
	if (Distance < 20.f)
	{
		if (Params.bImpact)
		{
			FShoterFX::SpawnImpact(GetWorld(), Params.End, Params.ImpactNormal, Params.Color, Params.bImpactFlesh);
		}
		return;
	}

	// Free slot, otherwise recycle the oldest bolt (its impact fires right away so it is never lost).
	FBolt* Slot = nullptr;
	for (FBolt& Bolt : Bolts)
	{
		if (!Bolt.bActive)
		{
			Slot = &Bolt;
			break;
		}
		if (!Slot || Bolt.SpawnTime < Slot->SpawnTime)
		{
			Slot = &Bolt;
		}
	}
	if (Slot->bActive)
	{
		FireBoltImpact(*Slot);
		ReleaseBolt(*Slot);
	}

	Slot->bActive = true;
	Slot->bImpactFired = false;
	Slot->Params = Params;
	Slot->Start = Params.Start;
	Slot->Dir = Delta / Distance;
	Slot->Rotation = Slot->Dir.ToOrientationQuat();
	Slot->Distance = Distance;
	Slot->HeadPos = 0.f;
	Slot->Speed = FMath::Max(Params.Speed, 100.f);
	Slot->Length = FMath::Max(Params.Length, 10.f);
	Slot->Width = Params.Width;
	Slot->SpawnTime = GetWorld()->GetTimeSeconds();
	++NumLiveBolts;

	Slot->Head->SetMaterial(0, GetBoltMaterial(Params.Color, Params.Intensity));
	Slot->Tail->SetMaterial(0, GetBoltMaterial(Params.Color, Params.Intensity * TailIntensityScale));
	Slot->Head->SetVisibility(true);
	Slot->Tail->SetVisibility(true);
	// Placed correctly by the first tick (same frame, before rendering).
	TickBolts(0.f);
	SpawnStreak(Params, Slot->Dir, Distance);
}

void UShoterFXPool::FireBoltImpact(FBolt& Bolt)
{
	if (Bolt.bImpactFired)
	{
		return;
	}
	Bolt.bImpactFired = true;
	if (Bolt.Params.bImpact)
	{
		FShoterFX::SpawnImpact(GetWorld(), Bolt.Params.End, Bolt.Params.ImpactNormal, Bolt.Params.Color, Bolt.Params.bImpactFlesh);
	}
}

void UShoterFXPool::ReleaseBolt(FBolt& Bolt)
{
	if (Bolt.bActive)
	{
		--NumLiveBolts;
	}
	Bolt.bActive = false;
	Bolt.Head->SetVisibility(false);
	Bolt.Tail->SetVisibility(false);
}

void UShoterFXPool::TickBolts(float DeltaTime)
{
	for (FBolt& Bolt : Bolts)
	{
		if (!Bolt.bActive)
		{
			continue;
		}
		Bolt.HeadPos += Bolt.Speed * DeltaTime;
		if (Bolt.HeadPos >= Bolt.Distance)
		{
			FireBoltImpact(Bolt);
		}

		// The visible segment slides from the start point to the end point; at both ends it is clipped, so the bolt grows out of the
		// muzzle and is swallowed by the impact point.
		const float TailPos = FMath::Max(Bolt.HeadPos - Bolt.Length, 0.f);
		const float FrontPos = FMath::Min(Bolt.HeadPos, Bolt.Distance);
		const float Segment = FrontPos - TailPos;
		if (TailPos >= Bolt.Distance - 1.f || (Bolt.HeadPos > 0.f && Segment < 1.f))
		{
			ReleaseBolt(Bolt);
			continue;
		}

		const float TailLength = Segment * TailFraction;
		const float HeadLength = Segment - TailLength;
		// Slightly thicker with distance so far bolts stay readable.
		const float W = Bolt.Width * FMath::Min(1.f + FrontPos / 1200.f, 6.f) / 100.f;
		Bolt.Tail->SetWorldTransform(FTransform(Bolt.Rotation * ZToBackward, Bolt.Start + Bolt.Dir * (TailPos + TailLength * 0.5f), FVector(W, W, TailLength / 100.f)));
		Bolt.Head->SetWorldTransform(FTransform(Bolt.Rotation * ZToForward, Bolt.Start + Bolt.Dir * (TailPos + TailLength + HeadLength * 0.5f), FVector(W, W, HeadLength / 100.f)));
	}
}

// ---------------------------------------------------------------------------
// Streaks
// ---------------------------------------------------------------------------

void UShoterFXPool::SpawnStreak(const FShoterBoltParams& Params, const FVector& Dir, float Distance)
{
	FStreak* Slot = nullptr;
	for (FStreak& Streak : Streaks)
	{
		if (!Streak.bActive)
		{
			Slot = &Streak;
			break;
		}
		if (!Slot || Streak.SpawnTime < Slot->SpawnTime)
		{
			Slot = &Streak;
		}
	}
	if (!Slot)
	{
		return;
	}
	Slot->bActive = true;
	Slot->Age = 0.f;
	Slot->Life = 0.13f;
	Slot->Start = Params.Start;
	Slot->Dir = Dir;
	Slot->Rotation = Dir.ToOrientationQuat() * ZToForward;
	// Up to ~13 m of the path (the bolt itself covers that in about a tenth of a second).
	Slot->Length = FMath::Min(Distance, 1300.f);
	Slot->Width = FMath::Max(Params.Width * 1.3f, 0.8f);
	Slot->Intensity = FMath::Min(Params.Intensity * 1.3f, 8.f);
	Slot->SpawnTime = GetWorld()->GetTimeSeconds();
	// Tinted a bit towards the energy colour's own hue so it never blooms to white.
	Slot->Material->SetVectorParameterValue(TEXT("Color"), Params.Color);
	Slot->Mesh->SetVisibility(true);
	TickStreaks(0.f);
}

void UShoterFXPool::TickStreaks(float DeltaTime)
{
	for (FStreak& Streak : Streaks)
	{
		if (!Streak.bActive)
		{
			continue;
		}
		Streak.Age += DeltaTime;
		const float Alpha = Streak.Age / Streak.Life;
		if (Alpha >= 1.f)
		{
			Streak.bActive = false;
			Streak.Mesh->SetVisibility(false);
			continue;
		}
		const float Fade = FMath::Square(1.f - Alpha);
		const float W = Streak.Width * (0.35f + 0.65f * (1.f - Alpha)) / 100.f;
		Streak.Mesh->SetWorldTransform(FTransform(Streak.Rotation, Streak.Start + Streak.Dir * (Streak.Length * 0.5f), FVector(W, W, Streak.Length / 100.f)));
		Streak.Material->SetScalarParameterValue(TEXT("Intensity"), Streak.Intensity * Fade);
	}
}

// ---------------------------------------------------------------------------
// Beams
// ---------------------------------------------------------------------------

void UShoterFXPool::SpawnBeam(const FShoterBeamParams& Params)
{
	EnsurePools();
	if (!bPoolsBuilt || (Params.End - Params.Start).SizeSquared() < 100.f)
	{
		return;
	}
	FBeam* Slot = nullptr;
	for (FBeam& Beam : Beams)
	{
		if (!Beam.bActive)
		{
			Slot = &Beam;
			break;
		}
		if (!Slot || Beam.SpawnTime < Slot->SpawnTime)
		{
			Slot = &Beam;
		}
	}
	Slot->bActive = true;
	Slot->Age = 0.f;
	Slot->Params = Params;
	Slot->SpawnTime = GetWorld()->GetTimeSeconds();
	if (Slot->CoreMaterial)
	{
		// Tinted core instead of pure white so it does not bloom into a white bar.
		Slot->CoreMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor::LerpUsingHSV(Params.Color, FLinearColor::White, 0.35f));
		Slot->HaloMaterial->SetVectorParameterValue(TEXT("Color"), Params.Color);
	}
	Slot->Core->SetVisibility(true);
	Slot->Halo->SetVisibility(true);
	TickBeams(0.f);
}

void UShoterFXPool::TickBeams(float DeltaTime)
{
	for (FBeam& Beam : Beams)
	{
		if (!Beam.bActive)
		{
			continue;
		}
		Beam.Age += DeltaTime;
		const float Alpha = Beam.Age / FMath::Max(Beam.Params.Lifetime, 0.05f);
		if (Alpha >= 1.f)
		{
			Beam.bActive = false;
			Beam.Core->SetVisibility(false);
			Beam.Halo->SetVisibility(false);
			continue;
		}

		const FVector Delta = Beam.Params.End - Beam.Params.Start;
		const float Distance = Delta.Size();
		const FQuat Rotation = (Delta / Distance).ToOrientationQuat() * ZToForward;
		const FVector Center = Beam.Params.Start + Delta * 0.5f;

		// The thin core vanishes fast (a flash), the wide halo lingers and thins out.
		const float CoreFade = FMath::Clamp(1.f - Alpha * 2.2f, 0.f, 1.f);
		const float HaloFade = FMath::Square(1.f - Alpha);
		const float CoreWidth = Beam.Params.Width * 0.3f * FMath::Max(CoreFade, 0.02f) / 100.f;
		const float HaloWidth = Beam.Params.Width * (0.35f + 0.65f * HaloFade) / 100.f;
		Beam.Core->SetWorldTransform(FTransform(Rotation, Center, FVector(CoreWidth, CoreWidth, Distance / 100.f)));
		Beam.Halo->SetWorldTransform(FTransform(Rotation, Center, FVector(HaloWidth, HaloWidth, Distance / 100.f)));
		if (Beam.CoreMaterial)
		{
			Beam.CoreMaterial->SetScalarParameterValue(TEXT("Intensity"), Beam.Params.Intensity * 1.4f * CoreFade);
			Beam.HaloMaterial->SetScalarParameterValue(TEXT("Intensity"), Beam.Params.Intensity * 0.6f * HaloFade * HaloFade);
		}
	}
}

// ---------------------------------------------------------------------------
// Debris
// ---------------------------------------------------------------------------

bool UShoterFXPool::SpawnDebris(const FShoterDebrisParams& Params)
{
	EnsurePools();
	if (!bPoolsBuilt)
	{
		return false;
	}
	UShoterAssets* Assets = UShoterAssets::Get(this);
	UStaticMesh* Mesh = Assets ? Assets->GetShape(Params.Shape) : nullptr;
	if (!Mesh)
	{
		return false;
	}

	FDebris* Slot = nullptr;
	for (FDebris& Piece : Debris)
	{
		if (!Piece.bActive)
		{
			Slot = &Piece;
			break;
		}
		if (!Slot || Piece.SpawnTime < Slot->SpawnTime)
		{
			Slot = &Piece;
		}
	}
	if (Slot->bActive)
	{
		ReleaseDebris(*Slot);
	}

	Slot->bActive = true;
	Slot->bResting = false;
	Slot->bGlowing = Params.GlowStrength > 0.f;
	Slot->Bounces = 0;
	Slot->Age = 0.f;
	Slot->Params = Params;
	Slot->Location = Params.Location;
	Slot->Velocity = Params.Velocity;
	Slot->Rotation = Params.Rotation.Quaternion();
	Slot->SpawnTime = GetWorld()->GetTimeSeconds();
	++NumLiveDebris;

	if (Slot->Mesh->GetStaticMesh() != Mesh)
	{
		Slot->Mesh->SetStaticMesh(Mesh);
	}
	Slot->Mesh->SetFirstPersonPrimitiveType(Params.bFirstPerson ? EFirstPersonPrimitiveType::FirstPerson : EFirstPersonPrimitiveType::None);
	if (Slot->Material)
	{
		Slot->Material->SetVectorParameterValue(TEXT("BaseColor"), Params.BaseColor);
		Slot->Material->SetScalarParameterValue(TEXT("Metallic"), Params.Metallic);
		Slot->Material->SetScalarParameterValue(TEXT("Roughness"), Params.Roughness);
		Slot->Material->SetVectorParameterValue(TEXT("EmissiveColor"), Params.GlowColor);
		Slot->Material->SetScalarParameterValue(TEXT("EmissiveStrength"), Params.GlowStrength);
	}
	Slot->Mesh->SetWorldTransform(FTransform(Slot->Rotation, Slot->Location, Params.Size / 100.f));
	Slot->Mesh->SetVisibility(true);
	return true;
}

void UShoterFXPool::ReleaseDebris(FDebris& Piece)
{
	if (Piece.bActive)
	{
		--NumLiveDebris;
	}
	Piece.bActive = false;
	Piece.Mesh->SetVisibility(false);
}

void UShoterFXPool::PlayBounceSound(const FShoterDebrisParams& Params, const FVector& Location)
{
	UWorld* World = GetWorld();
	if (Params.BounceSound.IsNone() || !World)
	{
		return;
	}
	// Rate limited (a fast SMG would otherwise rattle) and only audible near the listener anyway.
	const double Now = World->GetTimeSeconds();
	if (Now - LastBounceSoundTime > 1.0)
	{
		LastBounceSoundTime = Now;
		BounceSoundsThisSecond = 0;
	}
	if (BounceSoundsThisSecond >= 5)
	{
		return;
	}
	FVector Viewer;
	if (GetViewerLocation(World, Viewer) && FVector::DistSquared(Viewer, Location) > FMath::Square(1800.f))
	{
		return;
	}
	++BounceSoundsThisSecond;
	UShoterAssets::PlaySoundAt(World, Params.BounceSound, Location, EShoterSoundRange::Short, Params.BounceSoundVolume, Params.BounceSoundPitch * FMath::FRandRange(0.9f, 1.1f));
}

void UShoterFXPool::TickDebris(float DeltaTime)
{
	UWorld* World = GetWorld();
	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);
	const FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ShoterDebris), false);

	for (FDebris& Piece : Debris)
	{
		if (!Piece.bActive)
		{
			continue;
		}
		const FShoterDebrisParams& P = Piece.Params;
		Piece.Age += DeltaTime;
		if (Piece.Age >= P.Lifetime)
		{
			ReleaseDebris(Piece);
			continue;
		}

		bool bMoved = false;
		if (!Piece.bResting)
		{
			Piece.Velocity.Z += P.GravityZ * DeltaTime;
			const FVector NewLocation = Piece.Location + Piece.Velocity * DeltaTime;

			FHitResult Hit;
			if (P.bCollide && Piece.Age > 0.03f && DeltaTime > 0.f
				&& World->LineTraceSingleByObjectType(Hit, Piece.Location, NewLocation, ObjectParams, QueryParams))
			{
				const float Clearance = FMath::Max(P.Size.GetMin() * 0.5f, 0.3f);
				++Piece.Bounces;
				const FVector Normal = Hit.ImpactNormal;
				const float NormalSpeed = FVector::DotProduct(Piece.Velocity, Normal);
				if (Piece.Bounces > P.MaxBounces || FMath::Abs(NormalSpeed) < 40.f)
				{
					Piece.bResting = true;
					Piece.Velocity = FVector::ZeroVector;
					Piece.Location = Hit.ImpactPoint + Normal * Clearance;
				}
				else
				{
					const FVector Tangent = Piece.Velocity - Normal * NormalSpeed;
					Piece.Velocity = Tangent * 0.6f - Normal * NormalSpeed * P.Restitution;
					Piece.Location = Hit.ImpactPoint + Normal * Clearance;
					PlayBounceSound(P, Piece.Location);
				}
			}
			else
			{
				Piece.Location = NewLocation;
			}

			if (!Piece.bResting)
			{
				// Spin; slows right after the first bounce.
				const float SpinScale = Piece.Bounces > 0 ? 0.4f : 1.f;
				const FQuat Spin = (FRotator(P.AngularVelocity.Y, P.AngularVelocity.Z, P.AngularVelocity.X) * (SpinScale * DeltaTime)).Quaternion();
				Piece.Rotation = (Spin * Piece.Rotation).GetNormalized();
			}
			bMoved = true;
		}

		if (Piece.bGlowing && Piece.Material)
		{
			const float HeatAlpha = 1.f - Piece.Age / FMath::Max(P.GlowTime, 0.05f);
			if (HeatAlpha > 0.f)
			{
				Piece.Material->SetScalarParameterValue(TEXT("EmissiveStrength"), P.GlowStrength * HeatAlpha * HeatAlpha);
			}
			else
			{
				Piece.Material->SetScalarParameterValue(TEXT("EmissiveStrength"), 0.f);
				Piece.bGlowing = false;
			}
		}

		const float Remaining = P.Lifetime - Piece.Age;
		const bool bFading = Remaining < P.FadeTime;
		if (bMoved || bFading)
		{
			const float ScaleAlpha = bFading ? FMath::Clamp(Remaining / FMath::Max(P.FadeTime, 0.01f), 0.f, 1.f) : 1.f;
			Piece.Mesh->SetWorldTransform(FTransform(Piece.Rotation, Piece.Location, P.Size / 100.f * ScaleAlpha));
		}
	}
}

// ---------------------------------------------------------------------------

void UShoterFXPool::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bPoolsBuilt)
	{
		return;
	}
	if (NumLiveBolts > 0)
	{
		TickBolts(DeltaTime);
	}
	TickBeams(DeltaTime);
	TickStreaks(DeltaTime);
	if (NumLiveDebris > 0)
	{
		TickDebris(DeltaTime);
	}
}
