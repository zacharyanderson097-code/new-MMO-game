#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EmberBoss.generated.h"

class AEmberCharacter;
class AEmberDungeonManager;
class UInstancedStaticMeshComponent;
class UStaticMeshComponent;

/** Server-owned cooperative encounter. Engine primitives are temporary encounter art. */
UCLASS()
class EMBERFALL_API AEmberBoss : public AActor
{
    GENERATED_BODY()

public:
    AEmberBoss();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    float GetHealthFraction() const;
    int32 GetPhase() const;
    FString GetAttackLabel() const;
    bool IsDefeated() const { return Health <= 0.f; }
    bool IsEngaged() const { return bEngaged; }
    void ReceiveStrike(float Damage, AEmberCharacter* Attacker);
    bool HasContributed(AEmberCharacter* Player) const;
    void ResetEncounter();
    void ConfigureDungeon(AEmberDungeonManager* Manager, const FString& InstanceId);
    FString GetDungeonInstance() const { return DungeonInstance; }
    FString GetEncounterId() const { return EncounterId; }

private:
    void BeginAttack(AEmberCharacter* Player);
    void ResolveAttack();
    void DrawTelegraph() const;
    void HideTelegraphs() const;
    AEmberCharacter* FindNearestTarget() const;

    UFUNCTION()
    void OnRep_Health();

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> Body;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> Crown;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UInstancedStaticMeshComponent> DangerOutline;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UInstancedStaticMeshComponent> SafeOutline;

    UPROPERTY(ReplicatedUsing=OnRep_Health)
    float Health = 600.f;
    float CooldownRemaining = 2.f;

    UPROPERTY(Replicated)
    float WindupRemaining = 0.f;

    UPROPERTY(Replicated)
    float WindupDuration = 0.f;

    UPROPERTY(Replicated)
    float ResetDelay = 0.f;

    UPROPERTY(Replicated)
    float AttackRadius = 320.f;
    float AttackDamage = 22.f;
    int32 AttackNumber = 0;

    UPROPERTY(Replicated)
    bool bEngaged = false;

    UPROPERTY(Replicated)
    bool bWindingUp = false;

    UPROPERTY(Replicated)
    bool bRingAttack = false;

    UPROPERTY(Replicated)
    FVector TelegraphCenter = FVector::ZeroVector;

    UPROPERTY(Replicated)
    FString DungeonInstance;

    UPROPERTY(Replicated)
    FString EncounterId;

    UPROPERTY()
    TObjectPtr<AEmberDungeonManager> DungeonManager;

    TSet<TWeakObjectPtr<AEmberCharacter>> Contributors;
};
