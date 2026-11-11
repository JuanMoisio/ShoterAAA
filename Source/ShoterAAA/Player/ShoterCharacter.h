// First-person sci-fi soldier: movement, health/shield, inventory, interaction, death.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Engine/NetSerialization.h"
#include "ShoterCharacter.generated.h"

class UCameraComponent;
class UShoterCharacterMovement;
class AShoterWeapon;
class AShoterPlayerController;
class AShoterPlayerState;
class UInputComponent;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;
struct FInputActionValue;

DECLARE_MULTICAST_DELEGATE_OneParam(FShoterCharacterDiedSignature, class AShoterCharacter* /*Character*/);

UCLASS(Config = Game)
class SHOTERAAA_API AShoterCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AShoterCharacter(const FObjectInitializer& ObjectInitializer);

	//~ AActor / APawn / ACharacter
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void Destroyed() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;
	virtual void PawnClientRestart() override;
	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	virtual void FellOutOfWorld(const UDamageType& DmgType) override;
	virtual void Landed(const FHitResult& Hit) override;
	virtual void OnJumped_Implementation() override;
	virtual bool CanJumpInternal_Implementation() const override;
	virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	virtual FVector GetPawnViewLocation() const override;

	// --- Components ---
	UCameraComponent* GetFirstPersonCamera() const { return FirstPersonCamera; }
	/** First-person weapons attach here; it is animated procedurally (sway, bob, aim). */
	USceneComponent* GetWeaponPivot() const { return WeaponPivot; }
	UShoterCharacterMovement* GetShoterMovement() const;

	// --- Health & shield (shield regenerates first, then health) ---
	UFUNCTION(BlueprintPure, Category = "Shoter")
	float GetHealth() const { return Health; }

	UFUNCTION(BlueprintPure, Category = "Shoter")
	float GetMaxHealth() const { return MaxHealth; }

	UFUNCTION(BlueprintPure, Category = "Shoter")
	float GetShield() const { return Shield; }

	UFUNCTION(BlueprintPure, Category = "Shoter")
	float GetMaxShield() const { return MaxShield; }

	UFUNCTION(BlueprintPure, Category = "Shoter")
	bool IsAlive() const { return !bIsDying && Health > 0.f; }

	bool IsDying() const { return bIsDying; }
	bool IsSpawnProtected() const { return bSpawnProtected; }

	/** Server: ends spawn protection (on timeout or when this character fires). */
	void ClearSpawnProtection();

	/** Server: full health and shield. */
	void RestoreHealth();

	/** World time of the last damage taken (valid on server and owning client). */
	float GetLastDamageTime() const { return LastDamageTime; }

	/** Team from the player state (ShoterTeam::None when unknown or in free-for-all). */
	uint8 GetTeam() const;
	AShoterPlayerState* GetShoterPlayerState() const;
	FString GetPlayerDisplayName() const;

	// --- Weapons ---
	AShoterWeapon* GetCurrentWeapon() const { return CurrentWeapon; }
	const TArray<TObjectPtr<AShoterWeapon>>& GetInventory() const { return Inventory; }
	int32 GetMaxWeapons() const { return MaxWeapons; }

	/** Server: spawns and adds a weapon. If already owned, refills it instead. If the inventory is full, replaces the current weapon. */
	AShoterWeapon* GiveWeapon(TSubclassOf<AShoterWeapon> WeaponClass, bool bEquip = true);
	AShoterWeapon* FindWeapon(TSubclassOf<AShoterWeapon> WeaponClass) const;

	/** Server: destroys every weapon (death / respawn). */
	void DestroyInventory();

	/** Server: refills every weapon and grenades. */
	void RefillAllAmmo();

	/** Local or server: switch weapons. */
	void EquipWeapon(AShoterWeapon* Weapon);
	void EquipWeaponSlot(int32 SlotIndex);
	void CycleWeapon(int32 Direction);

	int32 GetGrenades() const { return Grenades; }
	int32 GetMaxGrenades() const { return MaxGrenades; }

	/** Server */
	void AddGrenades(int32 Amount);

	// --- Actions: used by player input and by AI controllers (on the server) ---
	void StartFire();
	void StopFire();
	void Reload();
	void ThrowGrenade();
	void SetAiming(bool bNewAiming);
	void SetSprinting(bool bNewSprinting);
	void Interact();

	bool WantsToFire() const { return bWantsToFire; }
	bool IsAiming() const { return bIsAiming; }
	bool IsSprinting() const;
	bool IsSliding() const;

	/** 0 = hip fire, 1 = fully aimed. Interpolated locally; drives FOV, weapon position and HUD. */
	float GetAimAlpha() const { return AimAlpha; }

	/** Local: camera recoil requested by the weapon (degrees), applied smoothly over a few frames. */
	void AddRecoil(float PitchDegrees, float YawDegrees);

	// --- Interaction (local) ---
	AActor* GetFocusedInteractable() const { return FocusedInteractable.Get(); }
	FText GetInteractionPrompt() const;

	// --- Death ---
	/** Server: kill this character. */
	virtual void Die(AController* Killer, AActor* DamageCauser, bool bHeadshot, const FVector& HitDirection);

	/** Broadcast on every machine when the character dies. */
	FShoterCharacterDiedSignature OnDied;

	/** Re-applies the team color to the third-person body. */
	void RefreshTeamColor();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shoter")
	TObjectPtr<UCameraComponent> FirstPersonCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shoter")
	TObjectPtr<USceneComponent> WeaponPivot;

	// --- Tunables ---
	UPROPERTY(EditDefaultsOnly, Category = "Shoter|Health")
	float MaxHealth = 100.f;

	UPROPERTY(EditDefaultsOnly, Category = "Shoter|Health")
	float MaxShield = 100.f;

	UPROPERTY(EditDefaultsOnly, Category = "Shoter|Health")
	float ShieldRegenDelay = 4.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Shoter|Health")
	float ShieldRegenRate = 45.f;

	UPROPERTY(EditDefaultsOnly, Category = "Shoter|Health")
	float HealthRegenDelay = 7.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Shoter|Health")
	float HealthRegenRate = 12.f;

	UPROPERTY(EditDefaultsOnly, Category = "Shoter|Health")
	float SpawnProtectionTime = 2.5f;

	UPROPERTY(EditDefaultsOnly, Category = "Shoter|Weapons")
	int32 MaxWeapons = 3;

	UPROPERTY(EditDefaultsOnly, Category = "Shoter|Weapons")
	int32 MaxGrenades = 4;

	/** Third-person body mesh and anim class (soft paths, configurable in DefaultGame.ini). */
	UPROPERTY(Config)
	FSoftObjectPath BodyMeshPath;

	UPROPERTY(Config)
	FSoftClassPath BodyAnimClassPath;

	// --- Replicated state ---
	UPROPERTY(ReplicatedUsing = OnRep_Health)
	float Health = 100.f;

	UPROPERTY(ReplicatedUsing = OnRep_Shield)
	float Shield = 100.f;

	UPROPERTY(ReplicatedUsing = OnRep_CurrentWeapon)
	TObjectPtr<AShoterWeapon> CurrentWeapon;

	UPROPERTY(Replicated)
	TArray<TObjectPtr<AShoterWeapon>> Inventory;

	UPROPERTY(Replicated)
	int32 Grenades = 2;

	UPROPERTY(Replicated)
	bool bIsAiming = false;

	UPROPERTY(ReplicatedUsing = OnRep_SpawnProtected)
	bool bSpawnProtected = false;

	UPROPERTY(Replicated)
	bool bIsDying = false;

	UFUNCTION()
	void OnRep_Health(float OldHealth);

	UFUNCTION()
	void OnRep_Shield(float OldShield);

	UFUNCTION()
	void OnRep_CurrentWeapon(AShoterWeapon* LastWeapon);

	UFUNCTION()
	void OnRep_SpawnProtected();

	// --- RPCs ---
	UFUNCTION(Server, Reliable)
	void ServerEquipWeapon(AShoterWeapon* Weapon);

	UFUNCTION(Server, Reliable)
	void ServerSetAiming(bool bNewAiming);

	UFUNCTION(Server, Reliable)
	void ServerThrowGrenade(FVector_NetQuantize Origin, FVector_NetQuantizeNormal Direction);

	UFUNCTION(Server, Reliable)
	void ServerInteract(AActor* Target);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastDeath(FVector_NetQuantizeNormal HitDirection, bool bHeadshot);

	// --- Enhanced Input handlers ---
	void Input_Move(const FInputActionValue& Value);
	void Input_Look(const FInputActionValue& Value);
	void Input_LookGamepad(const FInputActionValue& Value);
	void Input_JumpPressed();
	void Input_JumpReleased();
	void Input_FirePressed();
	void Input_FireReleased();
	void Input_AimPressed();
	void Input_AimReleased();
	void Input_SprintPressed();
	void Input_SprintReleased();
	void Input_CrouchPressed();
	void Input_CrouchReleased();
	void Input_Reload();
	void Input_Interact();
	void Input_Grenade();
	void Input_NextWeapon();
	void Input_PrevWeapon();
	void Input_Weapon1();
	void Input_Weapon2();
	void Input_Weapon3();

	// --- Internals ---
	void SetCurrentWeapon(AShoterWeapon* NewWeapon, AShoterWeapon* LastWeapon);
	void SetupBodyMesh();
	void UpdateFirstPersonView(float DeltaSeconds);
	void UpdateInteractionFocus();
	void UpdateRegeneration(float DeltaSeconds);
	void UpdateFootsteps(float DeltaSeconds);
	void ApplyPendingRecoil(float DeltaSeconds);
	void StartRagdoll(const FVector& HitDirection, bool bHeadshot);
	void NotifyDamageFeedback(float Damage, AController* EventInstigator, bool bShieldBroken, bool bHeadshot, bool bKilled, const FVector& SourceLocation);
	/** Simple armored body made of basic shapes, used when no skeletal mesh is configured. */
	void BuildPlaceholderBody();
	void BindToPlayerState();

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> PlaceholderParts;

	TWeakObjectPtr<AShoterPlayerState> BoundPlayerState;
	FDelegateHandle TeamChangedHandle;

	float CameraBaseHeight = 64.f;
	float CrouchCameraOffset = 0.f;
	float SprintBlend = 0.f;
	float LastGrenadeTime = -100.f;
	bool bWasSliding = false;
	float AimAlpha = 0.f;
	float LastDamageTime = -1000.f;
	float FootstepDistance = 0.f;
	float BobTime = 0.f;
	float LandingDip = 0.f;
	bool bWantsToFire = false;
	bool bSprintHeld = false;

	FRotator PendingRecoil = FRotator::ZeroRotator;
	FRotator LastControlRotation = FRotator::ZeroRotator;
	FVector SwayOffset = FVector::ZeroVector;
	FRotator SwayRotation = FRotator::ZeroRotator;

	TWeakObjectPtr<AActor> FocusedInteractable;

	/** Server: who damaged us recently (for assists). */
	TMap<TWeakObjectPtr<AController>, float> DamageContributors;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BodyMaterial;

	FTimerHandle TimerHandle_SpawnProtection;

	friend class AShoterGameMode;
};
