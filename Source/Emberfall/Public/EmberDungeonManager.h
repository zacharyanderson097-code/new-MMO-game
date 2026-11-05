#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EmberDungeonManager.generated.h"

class AEmberBoss;
class AEmberCharacter;

/** Fixed geometry belongs to a replicated actor, so every client gets the same collision. */
UCLASS()
class EMBERFALL_API AEmberDungeonRoom : public AActor
{
    GENERATED_BODY()
public:
    AEmberDungeonRoom();
};

/** Server-owned party snapshot and reward rules; four isolated development rooms, five players each. */
UCLASS()
class EMBERFALL_API AEmberDungeonManager : public AActor
{
    GENERATED_BODY()
public:
    AEmberDungeonManager();
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    bool EnterDungeon(const TArray<AEmberCharacter*>& Members, AEmberCharacter* Leader, const FString& PartyId);
    bool ExitDungeon(AEmberCharacter* Player);
    bool IsDungeonMember(AEmberCharacter* Player) const;
    FString GetInstanceId(AEmberCharacter* Player) const;
    FVector GetRespawnLocation(AEmberCharacter* Player) const;
    void OnDungeonBossDefeated(AEmberBoss* DefeatedBoss);

private:
    struct FRoom
    {
        FString InstanceId;
        FString PartyId;
        int32 Slot = INDEX_NONE;
        FVector Origin = FVector::ZeroVector;
        TArray<TWeakObjectPtr<AEmberCharacter>> Members;
        TWeakObjectPtr<AEmberDungeonRoom> Geometry;
        TWeakObjectPtr<AEmberBoss> Boss;
        float EmptySince = -1.f;
        bool bCompleted = false;
    };

    FRoom* FindRoom(AEmberCharacter* Player);
    const FRoom* FindRoom(AEmberCharacter* Player) const;
    void DestroyRoom(FRoom& Room);
    static FVector LobbyLocation(const FRoom& Room, int32 MemberIndex = 0);
    TArray<FRoom> Rooms;
};
