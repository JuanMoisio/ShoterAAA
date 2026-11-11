// Non-ASCII characters in the Spanish texts are written as \uXXXX escapes so the file compiles identically
// whatever the source file encoding is (\u00D3 = O acute, \u00F3 = o acute, \u2014 = em dash).

#include "Game/ShoterBuyStation.h"
#include "Core/ShoterAssets.h"
#include "Core/ShoterFX.h"
#include "Game/ShoterGameState.h"
#include "Player/ShoterCharacter.h"
#include "Player/ShoterPlayerController.h"
#include "Player/ShoterPlayerState.h"
#include "Weapons/ShoterWeapon.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Internationalization/Text.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "ShoterBuyStation"

namespace
{
	constexpr float BuyStationPedestalHeight = 100.f;
	constexpr float BuyStationHologramHeight = 148.f;
	constexpr float BuyStationLabelHeight = 212.f;

	/** Zombies: price of the generic ammo station when CostOverride is not set. */
	constexpr int32 BuyStationDefaultAmmoCost = 400;

	/** PvP: cooldown of the ammo stations (weapon lockers use PvPCooldown). */
	constexpr float BuyStationAmmoCooldown = 20.f;

	constexpr float BuyStationLightOn = 2500.f;
	constexpr float BuyStationLightOff = 120.f;

	FLinearColor GetBuyStationAmmoColor()
	{
		return FLinearColor(1.0f, 0.72f, 0.1f);
	}

	FLinearColor GetBuyStationDeniedColor()
	{
		return FLinearColor(1.0f, 0.15f, 0.1f);
	}

	FLinearColor GetBuyStationSuccessColor()
	{
		return FLinearColor(0.2f, 1.0f, 0.4f);
	}
}

AShoterBuyStation::AShoterBuyStation()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeFinder(TEXT("/Engine/BasicShapes/Cone.Cone"));
	UStaticMesh* CubeMesh = CubeFinder.Succeeded() ? CubeFinder.Object : nullptr;
	UStaticMesh* ConeMesh = ConeFinder.Succeeded() ? ConeFinder.Object : nullptr;

	// Only the player's Visibility trace may find the station; everything else passes through the box.
	InteractionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionBox"));
	InteractionBox->SetupAttachment(SceneRoot);
	InteractionBox->SetBoxExtent(FVector(70.f, 50.f, 90.f));
	InteractionBox->SetRelativeLocation(FVector(0.f, 0.f, 90.f));
	InteractionBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	InteractionBox->SetGenerateOverlapEvents(false);
	InteractionBox->SetCanEverAffectNavigation(false);
	InteractionBox->CanCharacterStepUpOn = ECB_No;

	// Pedestal: 1.2 x 0.8 x 1.0 m, blocks pawns.
	BaseMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BaseMesh"));
	BaseMesh->SetupAttachment(SceneRoot);
	if (CubeMesh)
	{
		BaseMesh->SetStaticMesh(CubeMesh);
	}
	BaseMesh->SetRelativeLocation(FVector(0.f, 0.f, BuyStationPedestalHeight * 0.5f));
	BaseMesh->SetRelativeScale3D(FVector(1.2f, 0.8f, 1.0f));
	BaseMesh->SetCollisionProfileName(TEXT("BlockAll"));

	// Glowing band around the top of the pedestal.
	StripMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StripMesh"));
	StripMesh->SetupAttachment(SceneRoot);
	if (CubeMesh)
	{
		StripMesh->SetStaticMesh(CubeMesh);
	}
	StripMesh->SetRelativeLocation(FVector(0.f, 0.f, BuyStationPedestalHeight - 12.f));
	StripMesh->SetRelativeScale3D(FVector(1.22f, 0.82f, 0.04f));
	StripMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	StripMesh->SetCastShadow(false);
	StripMesh->SetCanEverAffectNavigation(false);

	// Hologram: a diamond made of two cones, spinning above the pedestal.
	HologramRoot = CreateDefaultSubobject<USceneComponent>(TEXT("HologramRoot"));
	HologramRoot->SetupAttachment(SceneRoot);
	HologramRoot->SetRelativeLocation(FVector(0.f, 0.f, BuyStationHologramHeight));

	HologramTop = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HologramTop"));
	HologramTop->SetupAttachment(HologramRoot);
	if (ConeMesh)
	{
		HologramTop->SetStaticMesh(ConeMesh);
	}
	HologramTop->SetMobility(EComponentMobility::Movable);
	HologramTop->SetRelativeLocation(FVector(0.f, 0.f, 13.f));
	HologramTop->SetRelativeScale3D(FVector(0.32f, 0.32f, 0.26f));
	HologramTop->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HologramTop->SetCastShadow(false);
	HologramTop->SetCanEverAffectNavigation(false);

	HologramBottom = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HologramBottom"));
	HologramBottom->SetupAttachment(HologramRoot);
	if (ConeMesh)
	{
		HologramBottom->SetStaticMesh(ConeMesh);
	}
	HologramBottom->SetMobility(EComponentMobility::Movable);
	HologramBottom->SetRelativeLocation(FVector(0.f, 0.f, -13.f));
	HologramBottom->SetRelativeRotation(FRotator(180.f, 0.f, 0.f));
	HologramBottom->SetRelativeScale3D(FVector(0.32f, 0.32f, 0.26f));
	HologramBottom->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HologramBottom->SetCastShadow(false);
	HologramBottom->SetCanEverAffectNavigation(false);

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(SceneRoot);
	Label->SetMobility(EComponentMobility::Movable);
	Label->SetRelativeLocation(FVector(0.f, 0.f, BuyStationLabelHeight));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextCenter);
	Label->SetWorldSize(28.f);
	Label->SetTextRenderColor(FColor::White);
	Label->SetCastShadow(false);

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(SceneRoot);
	Light->SetMobility(EComponentMobility::Movable);
	Light->SetRelativeLocation(FVector(0.f, 0.f, BuyStationHologramHeight + 20.f));
	Light->SetIntensity(BuyStationLightOn);
	Light->SetAttenuationRadius(450.f);
	Light->SetCastShadows(false);
}

void AShoterBuyStation::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AShoterBuyStation, bAvailable);
	DOREPLIFETIME(AShoterBuyStation, CooldownEndTime);
}

void AShoterBuyStation::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// Also runs in the editor: RefreshVisuals only touches game-world-only things (materials) when it is safe.
	RefreshVisuals();
}

void AShoterBuyStation::BeginPlay()
{
	Super::BeginPlay();

	// Everything below the server logic is cosmetic.
	if (IsNetMode(NM_DedicatedServer))
	{
		SetActorTickEnabled(false);
		return;
	}

	RefreshVisuals();
}

void AShoterBuyStation::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (IsNetMode(NM_DedicatedServer))
	{
		return;
	}

	// Spin and bob the hologram (slower and lower while the station is unavailable).
	HologramTime += DeltaSeconds;
	HologramYaw = FMath::Fmod(HologramYaw + DeltaSeconds * (bAvailable ? 70.f : 12.f), 360.f);
	if (HologramRoot)
	{
		const float Bob = FMath::Sin(HologramTime * 2.f) * (bAvailable ? 6.f : 2.f);
		HologramRoot->SetRelativeLocationAndRotation(FVector(0.f, 0.f, BuyStationHologramHeight + Bob), FRotator(0.f, HologramYaw, 0.f));
	}

	UpdateLabel(false);
	FaceLabelToCamera();
}

// --- Presentation ---

bool AShoterBuyStation::IsZombiesMode() const
{
	const UWorld* World = GetWorld();
	const AShoterGameState* ShoterGameState = World ? World->GetGameState<AShoterGameState>() : nullptr;
	return ShoterGameState && ShoterGameState->IsZombiesMode();
}

FLinearColor AShoterBuyStation::GetEnergyColor() const
{
	if (WeaponClass)
	{
		if (const AShoterWeapon* WeaponDefaults = WeaponClass.GetDefaultObject())
		{
			return WeaponDefaults->GetStats().EnergyColor;
		}
	}
	return GetBuyStationAmmoColor();
}

FText AShoterBuyStation::GetOfferedName() const
{
	if (WeaponClass)
	{
		if (const AShoterWeapon* WeaponDefaults = WeaponClass.GetDefaultObject())
		{
			return WeaponDefaults->GetDisplayName().ToUpper();
		}
	}
	return LOCTEXT("OfferedAmmo", "MUNICI\u00D3N");
}

int32 AShoterBuyStation::GetBaseCost() const
{
	if (CostOverride >= 0)
	{
		return CostOverride;
	}

	if (WeaponClass)
	{
		if (const AShoterWeapon* WeaponDefaults = WeaponClass.GetDefaultObject())
		{
			return WeaponDefaults->GetBuyCost();
		}
	}
	return BuyStationDefaultAmmoCost;
}

float AShoterBuyStation::GetServerNow() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.f;
	}

	const AGameStateBase* GameState = World->GetGameState();
	return static_cast<float>(GameState ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds());
}

float AShoterBuyStation::GetRemainingCooldown() const
{
	if (bAvailable)
	{
		return 0.f;
	}
	return FMath::Max(0.f, CooldownEndTime - GetServerNow());
}

FText AShoterBuyStation::BuildLabelText() const
{
	const FText Name = GetOfferedName();

	if (IsZombiesMode())
	{
		return FText::Format(LOCTEXT("LabelZombies", "{0}\n{1}"), Name, FText::AsNumber(GetBaseCost(), &FNumberFormattingOptions::DefaultNoGrouping()));
	}

	if (!bAvailable)
	{
		const int32 Seconds = FMath::Max(1, FMath::CeilToInt(GetRemainingCooldown()));
		return FText::Format(LOCTEXT("LabelCooldown", "{0}\n{1} s"), Name, FText::AsNumber(Seconds));
	}

	return FText::Format(LOCTEXT("LabelReady", "{0}\nLISTO"), Name);
}

void AShoterBuyStation::UpdateLabel(bool bForce)
{
	if (!Label)
	{
		return;
	}

	int32 Key = 0;
	if (IsZombiesMode())
	{
		Key = -1;
	}
	else if (!bAvailable)
	{
		Key = FMath::Max(1, FMath::CeilToInt(GetRemainingCooldown()));
	}

	if (!bForce && Key == LastLabelKey)
	{
		return;
	}

	LastLabelKey = Key;
	Label->SetText(BuildLabelText());
}

void AShoterBuyStation::FaceLabelToCamera()
{
	if (!Label || !GEngine)
	{
		return;
	}

	const APlayerController* LocalController = GEngine->GetFirstLocalPlayerController(GetWorld());
	if (!LocalController || !LocalController->PlayerCameraManager)
	{
		return;
	}

	// Text faces its local +X axis: turn it toward the camera, staying upright.
	const FVector ToCamera = LocalController->PlayerCameraManager->GetCameraLocation() - Label->GetComponentLocation();
	FRotator LookRotation = ToCamera.Rotation();
	LookRotation.Pitch = 0.f;
	LookRotation.Roll = 0.f;
	Label->SetWorldRotation(LookRotation);
}

void AShoterBuyStation::EnsureMaterials()
{
	if (PedestalMaterial && StripMaterial && HologramMaterial)
	{
		return;
	}

	// The asset subsystem lives in the game instance: nothing to do in the editor viewport.
	const UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}

	UShoterAssets* Assets = UShoterAssets::Get(this);
	if (!Assets)
	{
		return;
	}

	const FLinearColor Energy = GetEnergyColor();

	// Dark metal pedestal with an emissive strip, glowing hologram.
	PedestalMaterial = Assets->MakeSurfaceMaterial(this, FLinearColor(0.025f, 0.03f, 0.04f), 0.9f, 0.3f);
	StripMaterial = Assets->MakeSurfaceMaterial(this, FLinearColor(0.01f, 0.01f, 0.012f), 0.2f, 0.4f, Energy, 8.f);
	HologramMaterial = Assets->MakeGlowMaterial(this, Energy, 12.f);

	if (PedestalMaterial && BaseMesh)
	{
		BaseMesh->SetMaterial(0, PedestalMaterial);
	}
	if (StripMaterial && StripMesh)
	{
		StripMesh->SetMaterial(0, StripMaterial);
	}
	if (HologramMaterial)
	{
		if (HologramTop)
		{
			HologramTop->SetMaterial(0, HologramMaterial);
		}
		if (HologramBottom)
		{
			HologramBottom->SetMaterial(0, HologramMaterial);
		}
	}
}

void AShoterBuyStation::RefreshVisuals()
{
	if (IsNetMode(NM_DedicatedServer))
	{
		return;
	}

	EnsureMaterials();

	const FLinearColor Energy = GetEnergyColor();
	const bool bOn = bAvailable;

	// Dim everything while the station is unavailable.
	const FLinearColor Tint = bOn ? Energy : FLinearColor(0.12f, 0.12f, 0.14f);

	if (HologramMaterial)
	{
		HologramMaterial->SetVectorParameterValue(TEXT("Color"), Tint);
		HologramMaterial->SetScalarParameterValue(TEXT("Intensity"), bOn ? 12.f : 1.5f);
	}

	if (StripMaterial)
	{
		StripMaterial->SetVectorParameterValue(TEXT("EmissiveColor"), Tint);
		StripMaterial->SetScalarParameterValue(TEXT("EmissiveStrength"), bOn ? 8.f : 0.5f);
	}

	if (Light)
	{
		Light->SetLightColor(Energy);
		Light->SetIntensity(bOn ? BuyStationLightOn : BuyStationLightOff);
	}

	if (Label)
	{
		// Weapon color pushed toward white so dark colors stay readable.
		const FLinearColor LabelColor = Energy + (FLinearColor::White - Energy) * 0.55f;
		Label->SetTextRenderColor(bOn ? LabelColor.ToFColor(true) : FColor(120, 120, 130));
	}

	UpdateLabel(true);
}

void AShoterBuyStation::OnRep_Available()
{
	RefreshVisuals();
}

// --- IShoterInteractable ---

int32 AShoterBuyStation::GetCostFor(const AShoterCharacter* User) const
{
	// Ammo station: one flat price.
	if (!WeaponClass)
	{
		return CostOverride >= 0 ? CostOverride : BuyStationDefaultAmmoCost;
	}

	// Weapon station: owning the weapon already turns the purchase into an ammo refill.
	const AShoterWeapon* WeaponDefaults = WeaponClass.GetDefaultObject();
	if (User && User->FindWeapon(WeaponClass))
	{
		return WeaponDefaults ? WeaponDefaults->GetAmmoCost() : BuyStationDefaultAmmoCost;
	}

	if (CostOverride >= 0)
	{
		return CostOverride;
	}
	return WeaponDefaults ? WeaponDefaults->GetBuyCost() : 0;
}

FText AShoterBuyStation::GetInteractionPrompt(const AShoterCharacter* User) const
{
	if (!bAvailable)
	{
		const int32 Seconds = FMath::Max(1, FMath::CeilToInt(GetRemainingCooldown()));
		return FText::Format(LOCTEXT("PromptUnavailable", "Disponible en {0} s"), FText::AsNumber(Seconds));
	}

	if (IsZombiesMode())
	{
		const FText Cost = FText::AsNumber(GetCostFor(User), &FNumberFormattingOptions::DefaultNoGrouping());

		if (!WeaponClass)
		{
			return FText::Format(LOCTEXT("PromptAmmoStationZombies", "[E] Recargar munici\u00F3n \u2014 {0} pts"), Cost);
		}

		const bool bOwned = User && User->FindWeapon(WeaponClass) != nullptr;
		const FText Format = bOwned
			? LOCTEXT("PromptAmmoZombies", "[E] Munici\u00F3n {0} \u2014 {1} pts")
			: LOCTEXT("PromptBuyZombies", "[E] Comprar {0} \u2014 {1} pts");
		return FText::Format(Format, GetOfferedName(), Cost);
	}

	if (!WeaponClass)
	{
		return LOCTEXT("PromptAmmoPvP", "[E] Recargar munici\u00F3n");
	}
	return FText::Format(LOCTEXT("PromptTakePvP", "[E] Tomar {0}"), GetOfferedName());
}

bool AShoterBuyStation::CanInteract(const AShoterCharacter* User) const
{
	// The prompt still shows when the player cannot afford it: the server answers with a "not enough points" notice.
	return User && User->IsAlive() && bAvailable;
}

// --- Server ---

bool AShoterBuyStation::WouldBenefit(const AShoterCharacter* User) const
{
	if (!User)
	{
		return false;
	}

	if (WeaponClass)
	{
		const AShoterWeapon* Owned = User->FindWeapon(WeaponClass);
		return !Owned || !Owned->IsAmmoFull();
	}

	if (User->GetGrenades() < User->GetMaxGrenades())
	{
		return true;
	}

	for (const TObjectPtr<AShoterWeapon>& Weapon : User->GetInventory())
	{
		if (Weapon && !Weapon->IsAmmoFull())
		{
			return true;
		}
	}
	return false;
}

void AShoterBuyStation::GrantTo(AShoterCharacter* User)
{
	if (!WeaponClass)
	{
		User->RefillAllAmmo();
		return;
	}

	if (AShoterWeapon* Owned = User->FindWeapon(WeaponClass))
	{
		Owned->RefillAmmo();
	}
	else
	{
		User->GiveWeapon(WeaponClass, true);
	}
}

void AShoterBuyStation::Interact(AShoterCharacter* User)
{
	if (!HasAuthority() || !User || !User->IsAlive())
	{
		return;
	}

	AShoterPlayerController* PlayerController = Cast<AShoterPlayerController>(User->GetController());

	if (!bAvailable)
	{
		if (PlayerController)
		{
			PlayerController->ClientNotify(LOCTEXT("StationUnavailable", "NO DISPONIBLE"), GetBuyStationDeniedColor(), TEXT("Denied"));
		}
		return;
	}

	// The focus trace runs on the client: double-check the distance here, with some slack for latency.
	if (FVector::Dist(User->GetActorLocation(), GetActorLocation()) > GetInteractionRange() + 400.f)
	{
		return;
	}

	// Never charge (or burn the cooldown) for something that would change nothing.
	if (!WouldBenefit(User))
	{
		if (PlayerController)
		{
			PlayerController->ClientNotify(LOCTEXT("AmmoAlreadyFull", "MUNICI\u00D3N LLENA"), GetBuyStationDeniedColor(), TEXT("Denied"));
		}
		return;
	}

	const bool bZombies = IsZombiesMode();
	if (bZombies)
	{
		AShoterPlayerState* PlayerState = User->GetShoterPlayerState();
		const int32 Cost = GetCostFor(User);

		if (!PlayerState || !PlayerState->SpendZombiePoints(Cost))
		{
			// Only the buyer hears about it.
			if (PlayerController)
			{
				PlayerController->ClientNotify(LOCTEXT("NotEnoughPoints", "PUNTOS INSUFICIENTES"), GetBuyStationDeniedColor(), TEXT("Denied"));
			}
			return;
		}
	}

	const bool bGotNewWeapon = WeaponClass && User->FindWeapon(WeaponClass) == nullptr;

	GrantTo(User);
	MulticastPurchaseFeedback();

	if (PlayerController)
	{
		FText Message = LOCTEXT("AmmoRefilled", "MUNICI\u00D3N RECARGADA");
		if (bGotNewWeapon)
		{
			Message = FText::Format(bZombies ? LOCTEXT("WeaponBought", "COMPRADO: {0}") : LOCTEXT("WeaponTaken", "ARMA OBTENIDA: {0}"), GetOfferedName());
		}
		// The purchase sound is played in the world for everyone, so no extra 2D sound here.
		PlayerController->ClientNotify(Message, GetBuyStationSuccessColor(), NAME_None);
	}

	// PvP lockers are free but go on cooldown for everybody.
	if (!bZombies)
	{
		StartCooldown();
	}
}

void AShoterBuyStation::StartCooldown()
{
	const float Cooldown = WeaponClass ? PvPCooldown : BuyStationAmmoCooldown;
	if (Cooldown <= 0.f)
	{
		return;
	}

	// Set the end time first so it replicates together with the availability flag.
	CooldownEndTime = GetServerNow() + Cooldown;
	SetAvailable(false);

	GetWorldTimerManager().SetTimer(TimerHandle_Cooldown, this, &AShoterBuyStation::EndCooldown, Cooldown, false);
}

void AShoterBuyStation::EndCooldown()
{
	SetAvailable(true);
}

void AShoterBuyStation::SetAvailable(bool bNewAvailable)
{
	if (bAvailable == bNewAvailable)
	{
		return;
	}

	bAvailable = bNewAvailable;

	// Replication notifies only the clients: the server (listen host) refreshes here.
	RefreshVisuals();
	ForceNetUpdate();
}

void AShoterBuyStation::MulticastPurchaseFeedback_Implementation()
{
	if (IsNetMode(NM_DedicatedServer))
	{
		return;
	}

	const FVector FxLocation = HologramRoot ? HologramRoot->GetComponentLocation() : GetActorLocation() + FVector(0.f, 0.f, BuyStationHologramHeight);

	UShoterAssets::PlaySoundAt(this, TEXT("Purchase"), FxLocation, EShoterSoundRange::Medium);
	FShoterFX::SpawnTeleportFlash(GetWorld(), FxLocation, GetEnergyColor());
}

#undef LOCTEXT_NAMESPACE
