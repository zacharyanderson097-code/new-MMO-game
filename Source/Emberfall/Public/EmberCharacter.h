#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "EmberCharacter.generated.h"

class UCameraComponent;
class USpringArmComponent;
class UStaticMeshComponent;
struct FEmberRealmCharacter;

UCLASS()
class EMBERFALL_API AEmberCharacter : public ACharacter
{
    GENERATED_BODY()
public:
    AEmberCharacter();
    virtual void BeginPlay() override;
    virtual void PawnClientRestart() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* Input) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    float GetHealthFraction() const;
    float GetStaminaFraction() const;
    bool IsAlive() const { return Health > 0.f; }
    bool IsInsidePvpZone() const;
    bool IsPvpOptedIn() const { return bPvpOptedIn; }
    bool HasChapterStarted() const { return bChapterStarted; }
    int32 GetSigils() const { return Sigils; }
    int32 GetVictories() const { return Victories; }
    int32 GetDungeonClears() const { return DungeonClears; }
    int32 GetEmberstone() const { return Emberstone; }
    bool IsProgressReady() const { return bProgressReady; }
    const FString& GetRealmCharacterId() const { return RealmCharacterId; }
    const FString& GetRealmLease() const { return RealmLease; }
    const FString& GetDungeonInstance() const { return DungeonInstance; }
    void SetDungeonInstance(const FString& InstanceId);
    bool IsInCombat() const;
    void ApplyRealmIdentity(const FEmberRealmCharacter& State, const FString& Lease);
    void InvalidateRealmIdentity();
    FString GetGearName() const;
    FString GetNotification() const;
    float GetAbilityCooldown(int32 Slot) const;

    // Called by authority actors only. No client may supply damage or loot amounts.
    void ReceiveCombatDamage(float Amount);
    void SetChapterStarted(bool bStarted);
    void AwardBossLoot(const FString& EncounterId);
    void AwardDungeonLoot(const FString& EncounterId);
    void AwardGathering();
    void ForgeGear();
    void Notify(const FString& Message);

protected:
    UPROPERTY(VisibleAnywhere) TObjectPtr<USpringArmComponent> CameraBoom;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> FollowCamera;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Armor;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Helmet;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Blade;

    UPROPERTY(Replicated) float Health = 100.f;
    UPROPERTY(Replicated) float Stamina = 100.f;
    UPROPERTY(Replicated) int32 GearTier = 0;
    UPROPERTY(Replicated) int32 Sigils = 0;
    UPROPERTY(Replicated) int32 Victories = 0;
    UPROPERTY(Replicated) int32 DungeonClears = 0;
    UPROPERTY(Replicated) int32 Emberstone = 0;
    UPROPERTY(Replicated) bool bProgressReady = false;
    UPROPERTY(Replicated) FString RealmCharacterId;
    UPROPERTY(Replicated) FString DungeonInstance;
    UPROPERTY(Replicated) bool bPvpOptedIn = false;
    UPROPERTY(Replicated) bool bChapterStarted = false;
    UPROPERTY(Replicated) bool bGuarding = false;
    UPROPERTY(Replicated) float NextStrikeAt = 0.f;
    UPROPERTY(Replicated) float NextHeavyAt = 0.f;
    UPROPERTY(Replicated) float NextMendAt = 0.f;
    UPROPERTY(Replicated) float NextDodgeAt = 0.f;

    UFUNCTION(Server, Reliable) void ServerUseAction(uint8 Action);
    UFUNCTION(Server, Reliable) void ServerSetGuard(bool bGuard);
    UFUNCTION(Client, Reliable) void ClientNotify(const FString& Message);
    UFUNCTION(NetMulticast, Unreliable) void MulticastStrikeEffect(bool bHeavy);

private:
    void MoveForward(float Value);
    void MoveRight(float Value);
    void Turn(float Value);
    void LookUp(float Value);
    void Strike();
    void Heavy();
    void Dodge();
    void Mend();
    void Interact();
    void TogglePvp();
    void GuardPressed();
    void GuardReleased();
    void ReleaseMouse();
    void ExecuteStrike(bool bHeavy);
    void SaveProgress();
    void ApplyRealmState(const FEmberRealmCharacter& State);
    void SubmitMutation(const FString& Operation, const FString& RequestId, const FString& SuccessText);
    float GetSynchronizedTime() const;

    float InvulnerableUntil = 0.f;
    float RespawnAt = 0.f;
    float LastDamageAt = -100.f;
    float NextInteractionAt = 0.f;
    float NextConsentAt = 0.f;
    float NextGuardToggleAt = 0.f;
    FString RealmLease;
    int32 RealmRevision = -1;
    bool bForgePending = false;
    bool bChapterPending = false;
    bool bLocalViewInitialized = false;
    void InitializeLocalView();
    FString Notification;
    float NotificationUntil = 0.f;
    bool bMouseReleased = false;
};
