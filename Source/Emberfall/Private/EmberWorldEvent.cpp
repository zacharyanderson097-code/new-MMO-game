#include "EmberWorldEvent.h"

#include "EmberBoss.h"
#include "EmberGameMode.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"

AEmberWorldEvent::AEmberWorldEvent()
{
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true;
    bAlwaysRelevant = true;
    NetUpdateFrequency = 2.f;
}

void AEmberWorldEvent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AEmberWorldEvent, Boss);
    DOREPLIFETIME(AEmberWorldEvent, NextSpawnTime);
}

void AEmberWorldEvent::SetBoss(AEmberBoss* InBoss)
{
    if (!HasAuthority() || !IsValid(InBoss) || !InBoss->GetDungeonInstance().IsEmpty()) return;
    Boss = InBoss;
    NextSpawnTime = 0.f;
    ForceNetUpdate();
}

float AEmberWorldEvent::GetServerTime() const
{
    const UWorld* World = GetWorld();
    if (!World) return 0.f;
    if (const AGameStateBase* State = World->GetGameState<AGameStateBase>())
    {
        return State->GetServerWorldTimeSeconds();
    }
    return World->GetTimeSeconds();
}

float AEmberWorldEvent::GetSecondsUntilSpawn() const
{
    return NextSpawnTime > 0.f ? FMath::Max(0.f, NextSpawnTime - GetServerTime()) : 0.f;
}

void AEmberWorldEvent::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!HasAuthority() || !IsValid(Boss)) return;

    if (!Boss->IsDefeated())
    {
        // A permitted manual reset supersedes a pending automatic spawn.
        if (NextSpawnTime > 0.f)
        {
            NextSpawnTime = 0.f;
            ForceNetUpdate();
        }
        return;
    }

    const float Now = GetServerTime();
    if (NextSpawnTime <= 0.f)
    {
        NextSpawnTime = Now + 30.f;
        ForceNetUpdate();
    }
    else if (Now >= NextSpawnTime)
    {
        if (AEmberGameMode* Mode = GetWorld()->GetAuthGameMode<AEmberGameMode>())
        {
            // Mode resets its reward ledger and boss together on the server.
            Mode->ResetWorldBoss();
            NextSpawnTime = 0.f;
            ForceNetUpdate();
        }
    }
}
