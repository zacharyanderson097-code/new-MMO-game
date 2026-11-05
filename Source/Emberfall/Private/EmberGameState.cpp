#include "EmberGameState.h"
#include "EmberBoss.h"
#include "EmberWorldEvent.h"
#include "Net/UnrealNetwork.h"

void AEmberGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AEmberGameState, Boss);
    DOREPLIFETIME(AEmberGameState, WorldEvent);
}

void AEmberGameState::SetWorldEvent(AEmberWorldEvent* InEvent)
{
    if (HasAuthority()) { WorldEvent = InEvent; ForceNetUpdate(); }
}

void AEmberGameState::SetBoss(AEmberBoss* InBoss)
{
    if (HasAuthority())
    {
        Boss = InBoss;
        ForceNetUpdate();
    }
}

FString AEmberGameState::GetObjectiveText() const
{
    if (!Boss)
    {
        return TEXT("The eastern arena is awakening...");
    }
    if (!Boss->IsDefeated())
    {
        return TEXT("Defeat the Cinderwarden in the eastern arena | Read its telegraphs and dodge");
    }
    return TEXT("Return to Dawn's Rest [E] to forge with earned sigils | [E] near the fallen guardian to replay");
}
