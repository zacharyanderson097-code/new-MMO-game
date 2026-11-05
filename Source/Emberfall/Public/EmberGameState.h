#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "EmberGameState.generated.h"

class AEmberBoss;
class AEmberWorldEvent;

/** Shared encounter state is visible to clients; authority stays in GameMode. */
UCLASS()
class EMBERFALL_API AEmberGameState : public AGameStateBase
{
    GENERATED_BODY()
public:
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    AEmberBoss* GetBoss() const { return Boss; }
    void SetBoss(AEmberBoss* InBoss);
    void SetWorldEvent(AEmberWorldEvent* InEvent);
    AEmberWorldEvent* GetWorldEvent() const { return WorldEvent; }
    FString GetObjectiveText() const;
private:
    UPROPERTY(Replicated)
    TObjectPtr<AEmberBoss> Boss;
    UPROPERTY(Replicated) TObjectPtr<AEmberWorldEvent> WorldEvent;
};
