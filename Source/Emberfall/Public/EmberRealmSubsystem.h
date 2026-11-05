#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "EmberRealmSubsystem.generated.h"

class FJsonObject;

/** A validated backend record. It never contains passwords or account session tokens. */
struct EMBERFALL_API FEmberRealmCharacter
{
    FString CharacterId;
    FString Username;
    int32 GearTier = 0;
    int32 Sigils = 0;
    int32 Victories = 0;
    int32 DungeonClears = 0;
    int32 Emberstone = 0;
    int32 Revision = 0;
    bool bChapterStarted = false;
};

using FEmberAdmitCallback = TFunction<void(bool, const FEmberRealmCharacter&, const FString&, const FString&)>;
using FEmberMutationCallback = TFunction<void(bool, const FEmberRealmCharacter&, const FString&)>;
using FEmberRealmCallback = TFunction<void(bool, const FString&)>;
using FEmberPartyCallback = TFunction<void(bool, FString, FString, TArray<FString>, FString)>;

/** Server-side HTTP boundary. Client processes never receive the game-server credential. */
UCLASS()
class EMBERFALL_API UEmberRealmSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual UWorld* GetWorld() const override;
    bool GetIsConfigured() const { return bConfigured; }

    void Admit(const FString& Ticket, FEmberAdmitCallback Callback);
    void Mutate(const FString& CharacterId, const FString& Lease, const FString& Operation,
        const FString& RequestId, FEmberMutationCallback Callback);
    void Heartbeat(const FString& CharacterId, const FString& Lease, FEmberRealmCallback Callback);
    void Release(const FString& CharacterId, const FString& Lease);
    void FetchParty(const FString& CharacterId, const FString& Lease, FEmberPartyCallback Callback);

private:
    using FJsonCallback = TFunction<void(bool, const TSharedPtr<FJsonObject>&, const FString&)>;
    void Request(const FString& Path, const TSharedRef<FJsonObject>& Body, FJsonCallback Callback,
        bool bRetrySafe = false, int32 Attempt = 0);
    void RetryRequest(const FString& Path, const TSharedRef<FJsonObject>& Body,
        FJsonCallback Callback, int32 NextAttempt);
    void CompleteMutation(const FString& Lease);
    void SendRelease(const FString& CharacterId, const FString& Lease);

    FString BaseUrl;
    FString ServerToken;
    bool bConfigured = false;
    bool bShuttingDown = false;
    TMap<FString, int32> PendingMutations;
    TMap<FString, FString> DeferredReleases;
};
