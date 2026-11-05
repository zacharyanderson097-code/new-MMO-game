#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "EmberGameMode.generated.h"

class AEmberBoss;
class AEmberCharacter;
class APlayerStart;
class AEmberDungeonManager;
class AEmberWorldEvent;

/** Server-only encounter rules. No client-provided gear or reward values are accepted. */
UCLASS()
class EMBERFALL_API AEmberGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AEmberGameMode();
    virtual void BeginPlay() override;
    virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
    void Interact(AEmberCharacter* Player);
    void OnBossDefeated();
    void ResetWorldBoss();
    void RequestDungeonEntry(AEmberCharacter* Player);
    void ExitDungeon(AEmberCharacter* Player);
    void Gather(AEmberCharacter* Player);
    AEmberDungeonManager* GetDungeonManager() const { return DungeonManager; }
    FString GetObjectiveText() const;
    bool IsBeaconLit() const { return bBeaconLit; }
    FVector GetSanctuaryLocation() const { return FVector(0.f, 0.f, 120.f); }
    FVector GetBeaconLocation() const { return FVector(700.f, 0.f, 0.f); }
    AEmberBoss* GetBoss() const { return Boss; }
private:
    UPROPERTY()
    TObjectPtr<AEmberBoss> Boss;
    UPROPERTY()
    TObjectPtr<APlayerStart> SanctuaryStart;
    bool bBeaconLit = false;
    bool bBossRewardsDistributed = false;
    UPROPERTY() TObjectPtr<AEmberDungeonManager> DungeonManager;
    UPROPERTY() TObjectPtr<AEmberWorldEvent> WorldEvent;
    TSet<TWeakObjectPtr<AEmberCharacter>> PendingDungeonEntries;
    TMap<FString, float> GatheringCooldowns;
};
