#include "EmberDungeonManager.h"

#include "EmberBoss.h"
#include "EmberCharacter.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace EmberDungeon
{
    constexpr int32 MaxRooms = 4;
    constexpr int32 MaxMembers = 5;
    constexpr float CleanupGrace = 30.f;
    const FVector Sanctuary(0.f, 0.f, 120.f);
}

AEmberDungeonRoom::AEmberDungeonRoom()
{
    bReplicates = true;
    bAlwaysRelevant = true; // Few development rooms: a newly teleported client must receive its floor.
    SetReplicateMovement(true);
    PrimaryActorTick.bCanEverTick = false;
    USceneComponent* Scene = CreateDefaultSubobject<USceneComponent>(TEXT("RoomRoot"));
    SetRootComponent(Scene);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

    auto MakeBlock = [this, Scene](const FName Name, const FVector& Location, const FVector& Scale)
    {
        UStaticMeshComponent* Mesh = CreateDefaultSubobject<UStaticMeshComponent>(Name);
        Mesh->SetupAttachment(Scene);
        Mesh->SetMobility(EComponentMobility::Movable);
        Mesh->SetStaticMesh(Cube.Object);
        Mesh->SetRelativeLocation(Location);
        Mesh->SetRelativeScale3D(Scale);
        Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Mesh->SetCollisionResponseToAllChannels(ECR_Block);
        return Mesh;
    };

    MakeBlock(TEXT("Floor"), FVector(0.f, 0.f, -65.f), FVector(36.f, 30.f, 1.f));
    MakeBlock(TEXT("NorthWall"), FVector(0.f, 1500.f, 235.f), FVector(36.f, 1.f, 5.f));
    MakeBlock(TEXT("SouthWall"), FVector(0.f, -1500.f, 235.f), FVector(36.f, 1.f, 5.f));
    MakeBlock(TEXT("EastWall"), FVector(1800.f, 0.f, 235.f), FVector(1.f, 30.f, 5.f));
    MakeBlock(TEXT("WestWall"), FVector(-1800.f, 0.f, 235.f), FVector(1.f, 30.f, 5.f));
    for (int32 Index = 0; Index < 4; ++Index)
    {
        const FVector Location((Index < 2) ? -1200.f : 1200.f, (Index % 2) ? 1000.f : -1000.f, 160.f);
        MakeBlock(FName(*FString::Printf(TEXT("ArchivePillar%d"), Index)), Location, FVector(1.1f, 1.1f, 3.5f));
    }

    UStaticMeshComponent* ExitRing = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ExitRing"));
    ExitRing->SetupAttachment(Scene);
    ExitRing->SetStaticMesh(Cylinder.Object);
    ExitRing->SetRelativeLocation(FVector(-700.f, 0.f, -8.f));
    ExitRing->SetRelativeScale3D(FVector(5.8f, 5.8f, .1f));
    ExitRing->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    UTextRenderComponent* Title = CreateDefaultSubobject<UTextRenderComponent>(TEXT("ArchiveTitle"));
    Title->SetupAttachment(Scene);
    Title->SetRelativeLocation(FVector(1450.f, 0.f, 450.f));
    Title->SetRelativeRotation(FRotator(0.f, 180.f, 0.f));
    Title->SetHorizontalAlignment(EHTA_Center);
    Title->SetWorldSize(80.f);
    Title->SetTextRenderColor(FColor(137, 214, 232));
    Title->SetText(FText::FromString(TEXT("DROWNED ARCHIVE\nKeeper Serael")));
    Title->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    UTextRenderComponent* Exit = CreateDefaultSubobject<UTextRenderComponent>(TEXT("ExitLabel"));
    Exit->SetupAttachment(Scene);
    Exit->SetRelativeLocation(FVector(-900.f, 0.f, 250.f));
    Exit->SetHorizontalAlignment(EHTA_Center);
    Exit->SetWorldSize(55.f);
    Exit->SetTextRenderColor(FColor(255, 215, 140));
    Exit->SetText(FText::FromString(TEXT("[E] Return to Dawn's Rest\nStand on the entrance circle")));
    Exit->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

AEmberDungeonManager::AEmberDungeonManager()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 1.f;
    bReplicates = false; // Clients receive room geometry and their server-set instance on the character.
}

FVector AEmberDungeonManager::LobbyLocation(const FRoom& Room, int32 MemberIndex)
{
    const int32 Index = FMath::Clamp(MemberIndex, 0, EmberDungeon::MaxMembers - 1);
    return Room.Origin + FVector(-700.f + (Index % 2) * 120.f, (Index / 2 - 1) * 140.f, 120.f);
}

const AEmberDungeonManager::FRoom* AEmberDungeonManager::FindRoom(AEmberCharacter* Player) const
{
    if (!Player)
    {
        return nullptr;
    }
    for (const FRoom& Room : Rooms)
    {
        if (Room.InstanceId == Player->GetDungeonInstance()
            && Room.Members.Contains(TWeakObjectPtr<AEmberCharacter>(Player)))
        {
            return &Room;
        }
    }
    return nullptr;
}

AEmberDungeonManager::FRoom* AEmberDungeonManager::FindRoom(AEmberCharacter* Player)
{
    return const_cast<FRoom*>(static_cast<const AEmberDungeonManager*>(this)->FindRoom(Player));
}

bool AEmberDungeonManager::IsDungeonMember(AEmberCharacter* Player) const
{
    return FindRoom(Player) != nullptr;
}

FString AEmberDungeonManager::GetInstanceId(AEmberCharacter* Player) const
{
    const FRoom* Room = FindRoom(Player);
    return Room ? Room->InstanceId : FString();
}

FVector AEmberDungeonManager::GetRespawnLocation(AEmberCharacter* Player) const
{
    const FRoom* Room = FindRoom(Player);
    return Room ? LobbyLocation(*Room, Room->Members.IndexOfByKey(TWeakObjectPtr<AEmberCharacter>(Player)))
        : EmberDungeon::Sanctuary;
}

bool AEmberDungeonManager::EnterDungeon(const TArray<AEmberCharacter*>& Members,
    AEmberCharacter* Leader, const FString& PartyId)
{
    if (!HasAuthority() || !Leader || Members.Num() < 1 || Members.Num() > EmberDungeon::MaxMembers
        || !Members.Contains(Leader) || PartyId.IsEmpty() || PartyId.Len() > 128)
    {
        return false;
    }
    for (const FRoom& Room : Rooms)
    {
        if (Room.PartyId == PartyId)
        {
            Leader->Notify(TEXT("Your party already has an archive room. Empty rooms close after 30 seconds."));
            return false;
        }
    }
    TSet<AEmberCharacter*> UniqueMembers;
    for (AEmberCharacter* Member : Members)
    {
        if (!IsValid(Member) || !Member->GetController() || !Member->IsAlive()
            || !Member->IsProgressReady() || !Member->GetDungeonInstance().IsEmpty()
            || UniqueMembers.Contains(Member))
        {
            Leader->Notify(TEXT("Every party member must be online, alive, loaded, and outside another dungeon."));
            return false;
        }
        for (TActorIterator<AEmberBoss> It(GetWorld()); It; ++It)
        {
            if (It->IsEngaged() && It->HasContributed(Member))
            {
                Leader->Notify(TEXT("Finish or leave the active boss encounter before entering the archive."));
                return false;
            }
        }
        UniqueMembers.Add(Member);
    }
    int32 Slot = INDEX_NONE;
    for (int32 Candidate = 0; Candidate < EmberDungeon::MaxRooms; ++Candidate)
    {
        if (!Rooms.ContainsByPredicate([Candidate](const FRoom& Room) { return Room.Slot == Candidate; }))
        {
            Slot = Candidate;
            break;
        }
    }
    if (Slot == INDEX_NONE)
    {
        Leader->Notify(TEXT("All four archive rooms are occupied. Please try again after a party leaves."));
        return false;
    }

    FRoom Room;
    Room.Slot = Slot;
    Room.Origin = FVector(12000.f + Slot * 8000.f, 0.f, 0.f);
    Room.PartyId = PartyId;
    Room.InstanceId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Room.Geometry = GetWorld()->SpawnActor<AEmberDungeonRoom>(
        AEmberDungeonRoom::StaticClass(), Room.Origin, FRotator::ZeroRotator, Spawn);
    if (!Room.Geometry.IsValid())
    {
        Leader->Notify(TEXT("The archive room could not be created. No party members were moved."));
        return false;
    }
    Room.Boss = GetWorld()->SpawnActor<AEmberBoss>(AEmberBoss::StaticClass(),
        Room.Origin + FVector(600.f, 0.f, 0.f), FRotator::ZeroRotator, Spawn);
    if (!Room.Boss.IsValid())
    {
        DestroyRoom(Room);
        Leader->Notify(TEXT("The archive guardian could not be created. No party members were moved."));
        return false;
    }
    Room.Boss->ConfigureDungeon(this, Room.InstanceId);
    for (AEmberCharacter* Member : Members)
    {
        Room.Members.Add(Member);
    }
    Rooms.Add(MoveTemp(Room));
    FRoom& ActiveRoom = Rooms.Last();
    for (int32 Index = 0; Index < Members.Num(); ++Index)
    {
        AEmberCharacter* Member = Members[Index];
        Member->SetDungeonInstance(ActiveRoom.InstanceId); // Identity changes before any out-of-bounds teleport.
        Member->GetCharacterMovement()->StopMovementImmediately();
        Member->SetActorLocationAndRotation(LobbyLocation(ActiveRoom, Index), FRotator::ZeroRotator,
            false, nullptr, ETeleportType::TeleportPhysics);
        Member->ForceNetUpdate();
        Member->Notify(TEXT("DROWNED ARCHIVE | Your party's private encounter. Defeat Keeper Serael; return to the entrance circle and press E to leave."));
    }
    return true;
}

bool AEmberDungeonManager::ExitDungeon(AEmberCharacter* Player)
{
    if (!HasAuthority() || !Player)
    {
        return false;
    }
    FRoom* Room = FindRoom(Player);
    if (!Room)
    {
        return false;
    }
    const FVector ExitCenter = Room->Origin + FVector(-700.f, 0.f, 0.f);
    if (!Player->IsAlive() || FVector::DistSquared2D(Player->GetActorLocation(), ExitCenter) > FMath::Square(300.f))
    {
        Player->Notify(TEXT("Return to the archive entrance circle and press E to leave."));
        return false;
    }
    Room->Members.Remove(TWeakObjectPtr<AEmberCharacter>(Player));
    Player->SetDungeonInstance(FString());
    Player->GetCharacterMovement()->StopMovementImmediately();
    Player->SetActorLocation(EmberDungeon::Sanctuary, false, nullptr, ETeleportType::TeleportPhysics);
    Player->ForceNetUpdate();
    Player->Notify(TEXT("Returned to Dawn's Rest. Your earned archive rewards remain yours."));
    if (Room->Members.IsEmpty())
    {
        Room->EmptySince = GetWorld()->GetTimeSeconds();
    }
    return true;
}

void AEmberDungeonManager::OnDungeonBossDefeated(AEmberBoss* DefeatedBoss)
{
    if (!HasAuthority() || !DefeatedBoss || !DefeatedBoss->IsDefeated())
    {
        return;
    }
    for (FRoom& Room : Rooms)
    {
        if (Room.Boss.Get() != DefeatedBoss || Room.InstanceId != DefeatedBoss->GetDungeonInstance() || Room.bCompleted)
        {
            continue;
        }
        Room.bCompleted = true; // Duplicate death callbacks cannot issue another room reward.
        for (const TWeakObjectPtr<AEmberCharacter>& WeakMember : Room.Members)
        {
            AEmberCharacter* Member = WeakMember.Get();
            if (Member && Member->GetController() && Member->GetDungeonInstance() == Room.InstanceId
                && DefeatedBoss->HasContributed(Member)
                && FVector::DistSquared2D(Member->GetActorLocation(), DefeatedBoss->GetActorLocation()) <= FMath::Square(3000.f))
            {
                Member->AwardDungeonLoot(DefeatedBoss->GetEncounterId());
            }
            else if (Member)
            {
                Member->Notify(TEXT("Keeper Serael is defeated. Only enrolled contributors in this room receive its reward. Return to the entrance [E]."));
            }
        }
        return;
    }
}

void AEmberDungeonManager::DestroyRoom(FRoom& Room)
{
    if (Room.Boss.IsValid())
    {
        Room.Boss->Destroy();
    }
    if (Room.Geometry.IsValid())
    {
        Room.Geometry->Destroy();
    }
}

void AEmberDungeonManager::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!HasAuthority())
    {
        return;
    }
    const float Now = GetWorld()->GetTimeSeconds();
    for (int32 Index = Rooms.Num() - 1; Index >= 0; --Index)
    {
        FRoom& Room = Rooms[Index];
        Room.Members.RemoveAll([&Room](const TWeakObjectPtr<AEmberCharacter>& WeakMember)
        {
            AEmberCharacter* Member = WeakMember.Get();
            if (!Member || Member->GetDungeonInstance() != Room.InstanceId)
            {
                return true;
            }
            if (!Member->GetController())
            {
                Member->SetDungeonInstance(FString());
                Member->SetActorLocation(EmberDungeon::Sanctuary, false, nullptr, ETeleportType::TeleportPhysics);
                return true;
            }
            return false;
        });
        for (int32 MemberIndex = 0; MemberIndex < Room.Members.Num(); ++MemberIndex)
        {
            AEmberCharacter* Member = Room.Members[MemberIndex].Get();
            if (!Member)
            {
                continue;
            }
            const FVector Relative = Member->GetActorLocation() - Room.Origin;
            if (FMath::Abs(Relative.X) > 1800.f || FMath::Abs(Relative.Y) > 1500.f
                || Relative.Z < -300.f || Relative.Z > 2000.f)
            {
                Member->GetCharacterMovement()->StopMovementImmediately();
                Member->SetActorLocation(LobbyLocation(Room, MemberIndex), false, nullptr, ETeleportType::TeleportPhysics);
                Member->ForceNetUpdate();
                Member->Notify(TEXT("Returned to your archive entrance after leaving the room boundary."));
            }
        }
        if (Room.Members.IsEmpty())
        {
            if (Room.EmptySince < 0.f)
            {
                Room.EmptySince = Now;
            }
            if (Now - Room.EmptySince >= EmberDungeon::CleanupGrace)
            {
                DestroyRoom(Room);
                Rooms.RemoveAtSwap(Index);
            }
        }
    }
}

void AEmberDungeonManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (HasAuthority())
    {
        for (FRoom& Room : Rooms)
        {
            DestroyRoom(Room);
        }
        Rooms.Empty();
    }
    Super::EndPlay(EndPlayReason);
}
