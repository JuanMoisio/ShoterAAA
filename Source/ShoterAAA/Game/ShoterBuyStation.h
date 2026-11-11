// Weapon/ammo station. Zombies: buy with points. PvP: free power-weapon locker with a cooldown.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/ShoterInteractable.h"
#include "ShoterBuyStation.generated.h"

class AShoterWeapon;
class UStaticMeshComponent;
class UTextRenderComponent;
class UPointLightComponent;
class UBoxComponent;
class UMaterialInstanceDynamic;

UCLASS()
class SHOTERAAA_API AShoterBuyStation : public AActor, public IShoterInteractable
{
	GENERATED_BODY()

public:
	AShoterBuyStation();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	//~ IShoterInteractable
	virtual FText GetInteractionPrompt(const AShoterCharacter* User) const override;
	virtual bool CanInteract(const AShoterCharacter* User) const override;
	virtual void Interact(AShoterCharacter* User) override;

	/** Weapon offered here. None = ammo station (refills every weapon and grenades). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shoter")
	TSubclassOf<AShoterWeapon> WeaponClass;

	/** Zombies: overrides the weapon's BuyCost (or the ammo station price). -1 = default. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shoter")
	int32 CostOverride = -1;

	/** PvP: seconds until the locker can be used again. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shoter")
	float PvPCooldown = 30.f;

	/** Zombies price for this user: weapon price, or ammo price if they already own the weapon. */
	int32 GetCostFor(const AShoterCharacter* User) const;
	bool IsAvailable() const { return bAvailable; }

	/** PvP: seconds left until the station is usable again (0 when available). Valid on every machine. */
	float GetRemainingCooldown() const;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Shoter")
	TObjectPtr<USceneComponent> SceneRoot;

	/** Blocks the Visibility channel so the player's interaction trace can find the station. */
	UPROPERTY(VisibleAnywhere, Category = "Shoter")
	TObjectPtr<UBoxComponent> InteractionBox;

	UPROPERTY(VisibleAnywhere, Category = "Shoter")
	TObjectPtr<UStaticMeshComponent> BaseMesh;

	/** Glowing band around the top of the pedestal. */
	UPROPERTY(VisibleAnywhere, Category = "Shoter")
	TObjectPtr<UStaticMeshComponent> StripMesh;

	/** Rotating hologram of the offered item. */
	UPROPERTY(VisibleAnywhere, Category = "Shoter")
	TObjectPtr<USceneComponent> HologramRoot;

	/** Upper cone of the hologram diamond. */
	UPROPERTY(VisibleAnywhere, Category = "Shoter")
	TObjectPtr<UStaticMeshComponent> HologramTop;

	/** Lower (flipped) cone of the hologram diamond. */
	UPROPERTY(VisibleAnywhere, Category = "Shoter")
	TObjectPtr<UStaticMeshComponent> HologramBottom;

	UPROPERTY(VisibleAnywhere, Category = "Shoter")
	TObjectPtr<UTextRenderComponent> Label;

	UPROPERTY(VisibleAnywhere, Category = "Shoter")
	TObjectPtr<UPointLightComponent> Light;

	UPROPERTY(ReplicatedUsing = OnRep_Available)
	bool bAvailable = true;

	/** Server world time (AGameStateBase::GetServerWorldTimeSeconds) when the PvP cooldown ends. */
	UPROPERTY(Replicated)
	float CooldownEndTime = 0.f;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PedestalMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> StripMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HologramMaterial;

	UFUNCTION()
	void OnRep_Available();

	/** Cosmetic feedback for everyone near the station (sound + materialize flash). */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPurchaseFeedback();

	void SetAvailable(bool bNewAvailable);
	void RefreshVisuals();
	bool IsZombiesMode() const;

	// --- Server ---
	void StartCooldown();
	void EndCooldown();

	/** Gives the weapon (or refills the owned one / everything for the ammo station). */
	void GrantTo(AShoterCharacter* User);

	/** False when buying would give the user nothing (everything already full). */
	bool WouldBenefit(const AShoterCharacter* User) const;

	// --- Presentation (clients and listen server) ---
	void EnsureMaterials();
	void UpdateLabel(bool bForce);
	void FaceLabelToCamera();
	FText BuildLabelText() const;
	FLinearColor GetEnergyColor() const;
	FText GetOfferedName() const;
	int32 GetBaseCost() const;
	float GetServerNow() const;

	FTimerHandle TimerHandle_Cooldown;

	float HologramTime = 0.f;
	float HologramYaw = 0.f;

	/** Last value the label was built for (seconds left, 0 = ready, -1 = zombies), so the text only changes when needed. */
	int32 LastLabelKey = MIN_int32;
};
