#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "EmberPlayerController.generated.h"

/** Development console entry points. Credentials are exchanged only with authority. */
UCLASS()
class EMBERFALL_API AEmberPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    AEmberPlayerController();
    virtual void PlayerTick(float DeltaTime) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    /** Obtain a short-lived one-use ticket from backend.client; never use a password here. */
    UFUNCTION(Exec) void Login(const FString& Ticket);
    UFUNCTION(Exec) void RealmStatus();
    UFUNCTION(Exec) void Dungeon();
    UFUNCTION(Exec) void ExitDungeon();
    UFUNCTION(Exec) void Gather();

protected:
    UFUNCTION(Server, Reliable) void ServerLogin(const FString& Ticket);
    UFUNCTION(Server, Reliable) void ServerRealmStatus();
    UFUNCTION(Server, Reliable) void ServerEnterDungeon();
    UFUNCTION(Server, Reliable) void ServerExitDungeon();
    UFUNCTION(Server, Reliable) void ServerGather();

private:
    bool CanRequestWorldAction();
    void InvalidateAdmission(const FString& Message);
    bool bAuthInFlight = false;
    bool bHeartbeatInFlight = false;
    bool bEndingPlay = false;
    int32 HeartbeatFailures = 0;
    float NextAuthenticationAt = 0.f;
    float NextWorldActionAt = 0.f;
    float NextHeartbeatAt = 0.f;
    float LeaseValidUntil = 0.f;
    FString AdmittedCharacterId;
    FString AdmittedLease;
};
