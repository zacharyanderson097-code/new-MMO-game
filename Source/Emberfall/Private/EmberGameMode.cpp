#include "EmberGameMode.h"

#include "EmberBoss.h"
#include "EmberCharacter.h"
#include "EmberGameState.h"
#include "EmberHUD.h"
#include "EmberWorldScene.h"
#include "EmberWorldEvent.h"
#include "EmberDungeonManager.h"
#include "EmberRealmSubsystem.h"
#include "EmberPlayerController.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"

AEmberGameMode::AEmberGameMode()
{
    DefaultPawnClass = AEmberCharacter::StaticClass();
    HUDClass = AEmberHUD::StaticClass();
    GameStateClass = AEmberGameState::StaticClass();
    PlayerControllerClass = AEmberPlayerController::StaticClass();
}

AActor* AEmberGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
    if (!SanctuaryStart && GetWorld())
    {
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        SanctuaryStart = GetWorld()->SpawnActor<APlayerStart>(
            APlayerStart::StaticClass(), GetSanctuaryLocation(), FRotator::ZeroRotator, Params);
    }
    return SanctuaryStart ? SanctuaryStart.Get() : Super::ChoosePlayerStart_Implementation(Player);
}

void AEmberGameMode::BeginPlay()
{
    Super::BeginPlay();
    // GameMode exists only on the server. Scene geometry is built locally on every peer.
    GetWorld()->SpawnActor<AEmberWorldScene>();
    DungeonManager = GetWorld()->SpawnActor<AEmberDungeonManager>();
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Boss = GetWorld()->SpawnActor<AEmberBoss>(
        AEmberBoss::StaticClass(), FVector(2500.f, 0.f, 0.f), FRotator::ZeroRotator, Params);
    if (AEmberGameState* State = GetGameState<AEmberGameState>())
    {
        State->SetBoss(Boss);
        WorldEvent = GetWorld()->SpawnActor<AEmberWorldEvent>();
        if (WorldEvent) WorldEvent->SetBoss(Boss);
        State->SetWorldEvent(WorldEvent);
    }
}

void AEmberGameMode::Interact(AEmberCharacter* Player)
{
    if (!HasAuthority() || !Player || !Player->IsAlive() || !Player->IsProgressReady())
    {
        return;
    }
    if (FMath::Abs(Player->GetActorLocation().Z) > 400.f) return;
    if (!Player->GetDungeonInstance().IsEmpty())
    {
        ExitDungeon(Player);
        return;
    }
    if (FVector::DistSquared2D(Player->GetActorLocation(), FVector(0.f, -1500.f, 0.f)) <= FMath::Square(350.f))
    {
        RequestDungeonEntry(Player);
        return;
    }
    for (const FVector& Node : {FVector(-500.f, 1000.f, 0.f), FVector(800.f, -900.f, 0.f)})
    {
        if (FVector::DistSquared2D(Player->GetActorLocation(), Node) <= FMath::Square(250.f))
        {
            Gather(Player);
            return;
        }
    }
    if (Boss && Boss->IsDefeated()
        && FVector::DistSquared2D(Player->GetActorLocation(), Boss->GetActorLocation()) <= FMath::Square(500.f))
    {
        ResetWorldBoss();
        Player->Notify(TEXT("The Cinderwarden rises again. Every eligible contributor can earn another Sigil."));
        return;
    }
    if (FVector::DistSquared2D(Player->GetActorLocation(), GetBeaconLocation()) <= FMath::Square(300.f))
    {
        // Every player can activate the chapter; another player's interaction never locks it.
        if (!Player->HasChapterStarted())
        {
            bBeaconLit = true;
            Player->SetChapterStarted(true);
            Player->Notify(TEXT("The beacon remembers. Your chapter is being recorded. The Cinderwarden awaits in the eastern arena."));
        }
        else
        {
            Player->Notify(TEXT("The beacon is lit. Defeat the Cinderwarden to earn an Ember Sigil."));
        }
        return;
    }
    if (FVector::DistSquared2D(Player->GetActorLocation(), GetSanctuaryLocation()) <= FMath::Square(350.f))
    {
        Player->ForgeGear();
        return;
    }
    Player->Notify(TEXT("Press E near the beacon, sanctuary forge, or a defeated guardian to interact."));
}

void AEmberGameMode::OnBossDefeated()
{
    if (!HasAuthority() || !Boss || !Boss->IsDefeated() || bBossRewardsDistributed)
    {
        return;
    }
    bBossRewardsDistributed = true;
    // Personal eligibility: every contributor earns a separate reward, never just the last hit.
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        APlayerController* Controller = It->Get();
        AEmberCharacter* Player = Controller ? Cast<AEmberCharacter>(Controller->GetPawn()) : nullptr;
        if (!Player || !Boss->HasContributed(Player)
            || FVector::DistSquared2D(Player->GetActorLocation(), Boss->GetActorLocation()) > FMath::Square(3500.f))
        {
            continue;
        }
        if (Player->HasChapterStarted())
        {
            Player->AwardBossLoot(Boss->GetEncounterId());
        }
        else
        {
            Player->Notify(TEXT("The guardian falls, but your beacon is unlit. Begin the chapter at the beacon before earning its reward."));
        }
    }
}

void AEmberGameMode::ResetWorldBoss()
{
    if (!HasAuthority() || !Boss || !Boss->IsDefeated()) return;
    bBossRewardsDistributed = false;
    Boss->ResetEncounter();
}

void AEmberGameMode::ExitDungeon(AEmberCharacter* Player)
{
    if (HasAuthority() && Player && Player->IsProgressReady() && DungeonManager)
        DungeonManager->ExitDungeon(Player);
}

void AEmberGameMode::RequestDungeonEntry(AEmberCharacter* Player)
{
    const FVector Portal(0.f, -1500.f, 0.f);
    if (!HasAuthority() || !Player || !DungeonManager || !Player->IsProgressReady() || !Player->IsAlive()
        || !Player->HasChapterStarted() || !Player->GetDungeonInstance().IsEmpty()
        || Player->IsInCombat() || FMath::Abs(Player->GetActorLocation().Z) > 400.f
        || FVector::DistSquared2D(Player->GetActorLocation(), Portal) > FMath::Square(350.f))
    {
        if (Player) Player->Notify(TEXT("Light the beacon, leave combat, then gather your party at the southern Archive portal [E]."));
        return;
    }
    if (GetNetMode() == NM_Standalone)
    {
        TArray<AEmberCharacter*> Solo{Player};
        DungeonManager->EnterDungeon(Solo, Player, TEXT("standalone"));
        return;
    }
    if (PendingDungeonEntries.Contains(TWeakObjectPtr<AEmberCharacter>(Player))) return;
    UEmberRealmSubsystem* Realm = GetGameInstance()->GetSubsystem<UEmberRealmSubsystem>();
    if (!Realm || !Realm->GetIsConfigured())
    {
        Player->Notify(TEXT("The realm service is required to verify your party."));
        return;
    }
    PendingDungeonEntries.Add(Player);
    TWeakObjectPtr<AEmberGameMode> WeakMode(this);
    TWeakObjectPtr<AEmberCharacter> WeakPlayer(Player);
    const FString RequestedLease = Player->GetRealmLease();
    Realm->FetchParty(Player->GetRealmCharacterId(), RequestedLease,
        [WeakMode, WeakPlayer, RequestedLease, Portal](bool bSuccess, FString PartyId, FString LeaderId,
            TArray<FString> MemberIds, FString Error)
        {
            AEmberGameMode* Mode = WeakMode.Get();
            AEmberCharacter* Leader = WeakPlayer.Get();
            if (!Mode) return;
            Mode->PendingDungeonEntries.Remove(WeakPlayer);
            if (!Leader || Leader->GetRealmLease() != RequestedLease || !Leader->IsProgressReady()) return;
            if (!bSuccess)
            {
                Leader->Notify(TEXT("Could not verify party membership with the realm. Try again shortly."));
                return;
            }
            if (PartyId.IsEmpty())
            {
                PartyId = TEXT("solo:") + Leader->GetRealmCharacterId();
                LeaderId = Leader->GetRealmCharacterId();
                MemberIds = {LeaderId};
            }
            if (LeaderId != Leader->GetRealmCharacterId() || MemberIds.Num() < 1 || MemberIds.Num() > 5)
            {
                Leader->Notify(TEXT("Only the current party leader can enter the Archive for the group."));
                return;
            }
            TArray<AEmberCharacter*> Members;
            for (const FString& MemberId : MemberIds)
            {
                AEmberCharacter* Found = nullptr;
                for (TActorIterator<AEmberCharacter> It(Mode->GetWorld()); It; ++It)
                    if (It->GetRealmCharacterId() == MemberId && It->IsProgressReady() && It->GetController()) { Found = *It; break; }
                if (!Found || !Found->IsAlive() || !Found->HasChapterStarted() || Found->IsInCombat()
                    || !Found->GetDungeonInstance().IsEmpty() || Members.Contains(Found)
                    || FMath::Abs(Found->GetActorLocation().Z) > 400.f
                    || FVector::DistSquared2D(Found->GetActorLocation(), Portal) > FMath::Square(350.f))
                {
                    Leader->Notify(TEXT("All party members must be on this server, chapter-ready, out of combat, and beside the Archive portal."));
                    return;
                }
                Members.Add(Found);
            }
            if (Mode->DungeonManager) Mode->DungeonManager->EnterDungeon(Members, Leader, PartyId);
        });
}

void AEmberGameMode::Gather(AEmberCharacter* Player)
{
    if (!HasAuthority() || !Player || !Player->IsProgressReady() || !Player->IsAlive()
        || !Player->GetDungeonInstance().IsEmpty() || Player->IsInCombat()
        || FMath::Abs(Player->GetActorLocation().Z) > 350.f) return;
    const FVector Nodes[] = {FVector(-500.f, 1000.f, 0.f), FVector(800.f, -900.f, 0.f)};
    for (int32 Index = 0; Index < 2; ++Index)
    {
        if (FVector::DistSquared2D(Player->GetActorLocation(), Nodes[Index]) > FMath::Square(250.f)) continue;
        const FString Identity = Player->GetRealmCharacterId().IsEmpty()
            ? FString::FromInt(Player->GetUniqueID()) : Player->GetRealmCharacterId();
        const FString Key = Identity + TEXT(":") + FString::FromInt(Index);
        const float Now = GetWorld()->GetTimeSeconds();
        if (GatheringCooldowns.FindRef(Key) > Now)
        {
            Player->Notify(TEXT("This Emberstone seam is recovering. Try again in a few seconds."));
            return;
        }
        GatheringCooldowns.Add(Key, Now + 15.f);
        Player->AwardGathering();
        return;
    }
    Player->Notify(TEXT("Stand beside a marked Emberstone seam and press E to gather."));
}

FString AEmberGameMode::GetObjectiveText() const
{
    const AEmberGameState* State = GetGameState<AEmberGameState>();
    return State ? State->GetObjectiveText() : TEXT("Light the beacon east of Dawn's Rest [E]");
}
