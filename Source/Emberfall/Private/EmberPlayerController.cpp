#include "EmberPlayerController.h"

#include "EmberCharacter.h"
#include "EmberGameMode.h"
#include "EmberRealmSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

namespace
{
    UEmberRealmSubsystem* RealmFor(const APlayerController* Controller)
    {
        UGameInstance* Instance = Controller ? Controller->GetGameInstance() : nullptr;
        return Instance ? Instance->GetSubsystem<UEmberRealmSubsystem>() : nullptr;
    }

    bool IsLeaseRejection(const FString& Code)
    {
        return Code == TEXT("lease_invalid") || Code == TEXT("lease_expired")
            || Code == TEXT("invalid_lease") || Code == TEXT("expired_lease")
            || Code == TEXT("realm_unauthorized") || Code == TEXT("unauthorized");
    }
}

AEmberPlayerController::AEmberPlayerController()
{
    PrimaryActorTick.bCanEverTick = true;
}

void AEmberPlayerController::Login(const FString& Ticket)
{
    // A ticket is one use and short-lived. Never pass a password or reusable
    // account session to the in-game console or through Unreal travel URLs.
    ServerLogin(Ticket);
}

void AEmberPlayerController::ServerLogin_Implementation(const FString& Ticket)
{
    UWorld* World = GetWorld();
    AEmberCharacter* Character = Cast<AEmberCharacter>(GetPawn());
    if (!World || bEndingPlay || !Character || bAuthInFlight) return;
    if (Character->IsProgressReady())
    {
        ClientMessage(TEXT("This character is already ready. Standalone play uses a separate local save."));
        return;
    }
    const float Now = World->GetTimeSeconds();
    if (Now < NextAuthenticationAt) return;
    NextAuthenticationAt = Now + 2.f;
    bool bValidTicket = Ticket.Len() >= 32 && Ticket.Len() <= 256;
    for (TCHAR Letter : Ticket)
        bValidTicket &= FChar::IsAlnum(Letter) || Letter == '-' || Letter == '_';
    if (!bValidTicket)
    {
        ClientMessage(TEXT("Use Login followed by a one-use join ticket from the realm client."));
        return;
    }
    UEmberRealmSubsystem* Realm = RealmFor(this);
    if (!Realm || !Realm->GetIsConfigured())
    {
        ClientMessage(TEXT("The server's realm service is not configured. Ask the server operator to check its settings."));
        return;
    }
    bAuthInFlight = true;
    const TWeakObjectPtr<AEmberPlayerController> WeakController(this);
    const TWeakObjectPtr<AEmberCharacter> WeakCharacter(Character);
    const TWeakObjectPtr<UEmberRealmSubsystem> WeakRealm(Realm);
    Realm->Admit(Ticket,
        [WeakController, WeakCharacter, WeakRealm, Now](bool bOk, const FEmberRealmCharacter& Record,
            const FString& Lease, const FString& Error)
        {
            AEmberPlayerController* Controller = WeakController.Get();
            AEmberCharacter* Player = WeakCharacter.Get();
            if (!Controller || Controller->bEndingPlay || !Player || Controller->GetPawn() != Player)
            {
                if (bOk)
                    if (UEmberRealmSubsystem* Service = WeakRealm.Get()) Service->Release(Record.CharacterId, Lease);
                return;
            }
            Controller->bAuthInFlight = false;
            if (!bOk)
            {
                // Backend errors are bounded symbolic codes; never echo response
                // bodies, passwords, credentials, or the submitted ticket.
                Controller->ClientMessage(FString::Printf(TEXT("Character admission failed (%s). Request a new ticket to retry."), *Error));
                return;
            }
            // Boss contribution uses this pawn's identity. A different account
            // requires a new pawn/connection rather than inheriting old tags.
            if (!Player->GetRealmCharacterId().IsEmpty() && Player->GetRealmCharacterId() != Record.CharacterId)
            {
                if (UEmberRealmSubsystem* Service = WeakRealm.Get()) Service->Release(Record.CharacterId, Lease);
                Controller->ClientMessage(TEXT("Reconnect to switch realm characters."));
                return;
            }
            Controller->AdmittedCharacterId = Record.CharacterId;
            Controller->AdmittedLease = Lease;
            Controller->bHeartbeatInFlight = false;
            Controller->HeartbeatFailures = 0;
            // Bound from request dispatch, not delayed response receipt, so HTTP
            // transit cannot extend the backend's sixty-second ownership lease.
            Controller->LeaseValidUntil = Now + 55.f;
            Controller->NextHeartbeatAt = Controller->GetWorld()->GetTimeSeconds() + 20.f;
            Player->ApplyRealmIdentity(Record, Lease);
            Controller->ClientMessage(TEXT("Character loaded. Server-earned progress is stored by the realm service."));
        });
}

void AEmberPlayerController::PlayerTick(float DeltaTime)
{
    Super::PlayerTick(DeltaTime);
    if (!HasAuthority() || bEndingPlay || AdmittedLease.IsEmpty()) return;
    const float Now = GetWorld()->GetTimeSeconds();
    // This check precedes the in-flight guard: retries must not let an old pawn
    // keep playing once another process can acquire the expired backend lease.
    if (Now >= LeaseValidUntil)
    {
        InvalidateAdmission(TEXT("Realm lease refresh timed out. Obtain a new ticket after the service recovers."));
        return;
    }
    AEmberCharacter* Character = Cast<AEmberCharacter>(GetPawn());
    UEmberRealmSubsystem* Realm = RealmFor(this);
    if (!Character || !Realm) return;
    if (!Character->IsProgressReady())
    {
        InvalidateAdmission(TEXT("Character admission ended. Obtain a new join ticket to continue."));
        return;
    }
    if (bHeartbeatInFlight) return;
    if (Now < NextHeartbeatAt) return;
    NextHeartbeatAt = Now + 20.f;
    bHeartbeatInFlight = true;
    const FString Lease = AdmittedLease;
    const FString CharacterId = AdmittedCharacterId;
    const TWeakObjectPtr<AEmberPlayerController> WeakController(this);
    Realm->Heartbeat(CharacterId, Lease,
        [WeakController, Lease, Now](bool bOk, const FString& Error)
        {
            AEmberPlayerController* Controller = WeakController.Get();
            if (!Controller || Controller->bEndingPlay || Controller->AdmittedLease != Lease) return;
            Controller->bHeartbeatInFlight = false;
            if (bOk)
            {
                Controller->HeartbeatFailures = 0;
                Controller->LeaseValidUntil = Now + 55.f;
                return;
            }
            ++Controller->HeartbeatFailures;
            if (!IsLeaseRejection(Error) && Controller->HeartbeatFailures < 3) return;
            Controller->InvalidateAdmission(TEXT("Realm connection lost. Gameplay is paused until a new character admission succeeds."));
        });
}

void AEmberPlayerController::InvalidateAdmission(const FString& Message)
{
    if (!HasAuthority()) return;
    if (AEmberCharacter* Character = Cast<AEmberCharacter>(GetPawn())) Character->InvalidateRealmIdentity();
    if (UEmberRealmSubsystem* Realm = RealmFor(this)) Realm->Release(AdmittedCharacterId, AdmittedLease);
    AdmittedCharacterId.Empty();
    AdmittedLease.Empty();
    LeaseValidUntil = 0.f;
    bHeartbeatInFlight = false;
    HeartbeatFailures = 0;
    ClientMessage(Message);
}

void AEmberPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    bEndingPlay = true;
    if (HasAuthority() && !AdmittedLease.IsEmpty())
    {
        // The subsystem lets in-flight idempotent writes finish before releasing
        // the lease. Abrupt process loss still relies on lease expiry.
        if (UEmberRealmSubsystem* Realm = RealmFor(this)) Realm->Release(AdmittedCharacterId, AdmittedLease);
    }
    Super::EndPlay(EndPlayReason);
}

void AEmberPlayerController::RealmStatus() { ServerRealmStatus(); }
void AEmberPlayerController::ServerRealmStatus_Implementation()
{
    const AEmberCharacter* Character = Cast<AEmberCharacter>(GetPawn());
    ClientMessage(Character && Character->IsProgressReady()
        ? TEXT("Character ready. Network progress uses the realm service; standalone progress uses its local save.")
        : TEXT("Character not admitted. Obtain a one-use ticket with backend.client, then use Login <ticket>."));
}

bool AEmberPlayerController::CanRequestWorldAction()
{
    AEmberCharacter* Character = Cast<AEmberCharacter>(GetPawn());
    if (bEndingPlay || !GetWorld() || !Character || !Character->IsProgressReady() || !Character->IsAlive()) return false;
    const float Now = GetWorld()->GetTimeSeconds();
    if (Now < NextWorldActionAt) return false;
    NextWorldActionAt = Now + .5f;
    return true;
}

void AEmberPlayerController::Dungeon() { ServerEnterDungeon(); }
void AEmberPlayerController::ServerEnterDungeon_Implementation()
{
    if (!CanRequestWorldAction()) return;
    if (AEmberGameMode* Mode = GetWorld()->GetAuthGameMode<AEmberGameMode>())
        Mode->RequestDungeonEntry(Cast<AEmberCharacter>(GetPawn()));
}

void AEmberPlayerController::ExitDungeon() { ServerExitDungeon(); }
void AEmberPlayerController::ServerExitDungeon_Implementation()
{
    if (!CanRequestWorldAction()) return;
    if (AEmberGameMode* Mode = GetWorld()->GetAuthGameMode<AEmberGameMode>())
        Mode->ExitDungeon(Cast<AEmberCharacter>(GetPawn()));
}

void AEmberPlayerController::Gather() { ServerGather(); }
void AEmberPlayerController::ServerGather_Implementation()
{
    if (!CanRequestWorldAction()) return;
    if (AEmberGameMode* Mode = GetWorld()->GetAuthGameMode<AEmberGameMode>())
        Mode->Gather(Cast<AEmberCharacter>(GetPawn()));
}
