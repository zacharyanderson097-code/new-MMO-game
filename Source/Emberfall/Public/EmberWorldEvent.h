#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EmberWorldEvent.generated.h"

class AEmberBoss;

/** Shared world-boss schedule. No ownership, PvP flag, or exclusive guild claim. */
UCLASS()
class EMBERFALL_API AEmberWorldEvent : public AActor
{
    GENERATED_BODY()

public:
    AEmberWorldEvent();
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    void SetBoss(AEmberBoss* InBoss);
    float GetSecondsUntilSpawn() const;

    UPROPERTY(Replicated)
    float NextSpawnTime = 0.f;

private:
    float GetServerTime() const;

    UPROPERTY(Replicated)
    TObjectPtr<AEmberBoss> Boss;
};
