#include "EmberRealmSubsystem.h"

#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformMisc.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "TimerManager.h"

namespace
{
    bool IsSafeOrigin(const FString& Url)
    {
        const bool bHttps = Url.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase);
        const bool bHttp = Url.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase);
        if (!bHttps && !bHttp) return false;
        const FString Authority = Url.Mid(bHttps ? 8 : 7);
        if (Authority.IsEmpty() || Authority.Contains(TEXT("/")) || Authority.Contains(TEXT("@"))
            || Authority.Contains(TEXT("?")) || Authority.Contains(TEXT("#"))) return false;
        for (TCHAR Character : Authority)
        {
            if (!FChar::IsAlnum(Character) && Character != '.' && Character != '-'
                && Character != ':' && Character != '[' && Character != ']') return false;
        }
        if (bHttps) return true;
        // Development HTTP is permitted only to explicitly named loopback origins.
        FString Host = Authority;
        FString Port;
        if (Authority.StartsWith(TEXT("[::1]")))
        {
            const FString Suffix = Authority.Mid(5);
            if (Suffix.IsEmpty()) return true;
            if (!Suffix.StartsWith(TEXT(":"))) return false;
            Port = Suffix.Mid(1);
            Host = TEXT("[::1]");
        }
        else
        {
            Authority.Split(TEXT(":"), &Host, &Port);
        }
        if (!Port.IsEmpty() && (!Port.IsNumeric() || FCString::Atoi(*Port) < 1 || FCString::Atoi(*Port) > 65535)) return false;
        return Host.Equals(TEXT("localhost"), ESearchCase::IgnoreCase)
            || Host == TEXT("127.0.0.1") || Host == TEXT("[::1]");
    }

    FString ErrorCode(const TSharedPtr<FJsonObject>& Root, int32 Status)
    {
        const TSharedPtr<FJsonObject>* Error = nullptr;
        FString Code;
        if (Root.IsValid() && Root->TryGetObjectField(TEXT("error"), Error) && Error && Error->IsValid()
            && (*Error)->TryGetStringField(TEXT("code"), Code) && !Code.IsEmpty() && Code.Len() <= 64)
        {
            bool bSafe = true;
            for (TCHAR Character : Code)
                bSafe &= (Character >= 'a' && Character <= 'z') || (Character >= '0' && Character <= '9') || Character == '_';
            if (bSafe) return Code;
        }
        if (Status == 401 || Status == 403) return TEXT("realm_unauthorized");
        if (Status == 409) return TEXT("realm_conflict");
        if (Status == 429) return TEXT("realm_rate_limited");
        return TEXT("realm_request_failed");
    }

    bool ReadBoundedInt(const TSharedPtr<FJsonObject>& Record, const TCHAR* Name, int32& Out, int32 Maximum = 1000000)
    {
        double Number = 0.;
        if (!Record->TryGetNumberField(Name, Number) || !FMath::IsFinite(Number)
            || Number < 0. || Number > Maximum || FMath::FloorToDouble(Number) != Number) return false;
        Out = static_cast<int32>(Number);
        return true;
    }

    bool ReadCharacter(const TSharedPtr<FJsonObject>& Root, FEmberRealmCharacter& Out)
    {
        const TSharedPtr<FJsonObject>* Record = nullptr;
        if (!Root.IsValid() || !Root->TryGetObjectField(TEXT("character"), Record) || !Record || !Record->IsValid()) return false;
        return (*Record)->TryGetStringField(TEXT("character_id"), Out.CharacterId)
            && !Out.CharacterId.IsEmpty() && Out.CharacterId.Len() <= 64
            && (*Record)->TryGetStringField(TEXT("username"), Out.Username)
            && !Out.Username.IsEmpty() && Out.Username.Len() <= 32
            && ReadBoundedInt(*Record, TEXT("gear_tier"), Out.GearTier, 2)
            && ReadBoundedInt(*Record, TEXT("sigils"), Out.Sigils)
            && ReadBoundedInt(*Record, TEXT("victories"), Out.Victories)
            && ReadBoundedInt(*Record, TEXT("dungeon_clears"), Out.DungeonClears)
            && ReadBoundedInt(*Record, TEXT("emberstone"), Out.Emberstone)
            && ReadBoundedInt(*Record, TEXT("revision"), Out.Revision, MAX_int32)
            && (*Record)->TryGetBoolField(TEXT("chapter_started"), Out.bChapterStarted);
    }

    TSharedRef<FJsonObject> IdentityBody(const FString& CharacterId, const FString& Lease)
    {
        TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
        Body->SetStringField(TEXT("character_id"), CharacterId);
        Body->SetStringField(TEXT("lease"), Lease);
        return Body;
    }
}

void UEmberRealmSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    BaseUrl = FPlatformMisc::GetEnvironmentVariable(TEXT("EMBER_BACKEND_URL")).TrimStartAndEnd();
    if (BaseUrl.IsEmpty()) BaseUrl = TEXT("http://127.0.0.1:8081");
    while (BaseUrl.RemoveFromEnd(TEXT("/"))) {}
    ServerToken = FPlatformMisc::GetEnvironmentVariable(TEXT("EMBER_SERVER_TOKEN"));
    bConfigured = IsSafeOrigin(BaseUrl) && ServerToken.Len() >= 32
        && !ServerToken.Contains(TEXT("\r")) && !ServerToken.Contains(TEXT("\n"));
}

void UEmberRealmSubsystem::Deinitialize()
{
    bShuttingDown = true;
    ServerToken.Empty();
    DeferredReleases.Empty();
    PendingMutations.Empty();
    Super::Deinitialize();
}

UWorld* UEmberRealmSubsystem::GetWorld() const
{
    const UGameInstance* Instance = GetGameInstance();
    return Instance ? Instance->GetWorld() : nullptr;
}

void UEmberRealmSubsystem::Request(const FString& Path, const TSharedRef<FJsonObject>& Body,
    FJsonCallback Callback, bool bRetrySafe, int32 Attempt)
{
    UWorld* World = GetWorld();
    if (bShuttingDown || !bConfigured || !World || World->GetNetMode() == NM_Client)
    {
        Callback(false, nullptr, TEXT("realm_not_configured"));
        return;
    }
    FString Payload;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Payload);
    if (!FJsonSerializer::Serialize(Body, Writer))
    {
        Callback(false, nullptr, TEXT("realm_invalid_request"));
        return;
    }
    const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Http = FHttpModule::Get().CreateRequest();
    Http->SetURL(BaseUrl + Path);
    Http->SetVerb(TEXT("POST"));
    Http->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Http->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + ServerToken);
    Http->SetTimeout(10.f);
    Http->SetContentAsString(Payload);
    const TWeakObjectPtr<UEmberRealmSubsystem> WeakThis(this);
    Http->OnProcessRequestComplete().BindLambda(
        [WeakThis, Path, Body, Callback, bRetrySafe, Attempt](FHttpRequestPtr, FHttpResponsePtr Response, bool bSucceeded) mutable
        {
            UEmberRealmSubsystem* Self = WeakThis.Get();
            if (!Self || Self->bShuttingDown) return;
            const int32 Status = Response.IsValid() ? Response->GetResponseCode() : 0;
            if (bRetrySafe && Attempt < 2 && (!bSucceeded || !Response.IsValid() || Status >= 500))
            {
                Self->RetryRequest(Path, Body, MoveTemp(Callback), Attempt + 1);
                return;
            }
            if (!bSucceeded || !Response.IsValid())
            {
                Callback(false, nullptr, TEXT("realm_transport_error"));
                return;
            }
            TSharedPtr<FJsonObject> Root;
            const FString ResponseText = Response->GetContentAsString();
            if (ResponseText.Len() <= 65536)
            {
                const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseText);
                FJsonSerializer::Deserialize(Reader, Root);
            }
            if (Status < 200 || Status >= 300)
            {
                Callback(false, nullptr, ErrorCode(Root, Status));
                return;
            }
            if (!Root.IsValid())
            {
                Callback(false, nullptr, TEXT("realm_invalid_response"));
                return;
            }
            Callback(true, Root, FString());
        });
    if (!Http->ProcessRequest())
    {
        Http->OnProcessRequestComplete().Unbind();
        if (bRetrySafe && Attempt < 2) RetryRequest(Path, Body, MoveTemp(Callback), Attempt + 1);
        else Callback(false, nullptr, TEXT("realm_transport_error"));
    }
}

void UEmberRealmSubsystem::RetryRequest(const FString& Path, const TSharedRef<FJsonObject>& Body,
    FJsonCallback Callback, int32 NextAttempt)
{
    UWorld* World = GetWorld();
    if (!World || bShuttingDown)
    {
        Callback(false, nullptr, TEXT("realm_transport_error"));
        return;
    }
    const TWeakObjectPtr<UEmberRealmSubsystem> WeakThis(this);
    FTimerHandle Handle;
    World->GetTimerManager().SetTimer(Handle,
        FTimerDelegate::CreateLambda([WeakThis, Path, Body, Callback, NextAttempt]() mutable
        {
            if (UEmberRealmSubsystem* Self = WeakThis.Get())
                Self->Request(Path, Body, MoveTemp(Callback), true, NextAttempt);
        }), static_cast<float>(NextAttempt), false);
}

void UEmberRealmSubsystem::Admit(const FString& Ticket, FEmberAdmitCallback Callback)
{
    TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
    Body->SetStringField(TEXT("ticket"), Ticket);
    // Ticket redemption is deliberately not retried: a lost successful response
    // leaves a short-lived lease, rather than redeeming a one-use ticket twice.
    Request(TEXT("/v1/server/admit"), Body,
        [Callback](bool bOk, const TSharedPtr<FJsonObject>& Root, const FString& Error)
        {
            FEmberRealmCharacter Character;
            FString Lease;
            if (bOk && (!ReadCharacter(Root, Character) || !Root->TryGetStringField(TEXT("lease"), Lease)
                || Lease.Len() < 16 || Lease.Len() > 256))
            {
                Callback(false, Character, FString(), TEXT("realm_invalid_response"));
                return;
            }
            Callback(bOk, Character, Lease, Error);
        });
}

void UEmberRealmSubsystem::Mutate(const FString& CharacterId, const FString& Lease,
    const FString& Operation, const FString& RequestId, FEmberMutationCallback Callback)
{
    TSharedRef<FJsonObject> Body = IdentityBody(CharacterId, Lease);
    Body->SetStringField(TEXT("operation"), Operation);
    Body->SetStringField(TEXT("request_id"), RequestId);
    ++PendingMutations.FindOrAdd(Lease);
    const TWeakObjectPtr<UEmberRealmSubsystem> WeakThis(this);
    Request(TEXT("/v1/server/mutate"), Body,
        [WeakThis, Lease, Callback](bool bOk, const TSharedPtr<FJsonObject>& Root, const FString& Error)
        {
            UEmberRealmSubsystem* Self = WeakThis.Get();
            if (!Self || Self->bShuttingDown) return;
            FEmberRealmCharacter Character;
            const bool bValid = bOk && ReadCharacter(Root, Character);
            Self->CompleteMutation(Lease);
            Callback(bValid, Character, bOk && !bValid ? TEXT("realm_invalid_response") : Error);
        }, true);
}

void UEmberRealmSubsystem::CompleteMutation(const FString& Lease)
{
    int32* Count = PendingMutations.Find(Lease);
    if (Count && --(*Count) > 0) return;
    PendingMutations.Remove(Lease);
    FString CharacterId;
    if (DeferredReleases.RemoveAndCopyValue(Lease, CharacterId)) SendRelease(CharacterId, Lease);
}

void UEmberRealmSubsystem::Heartbeat(const FString& CharacterId, const FString& Lease, FEmberRealmCallback Callback)
{
    Request(TEXT("/v1/server/heartbeat"), IdentityBody(CharacterId, Lease),
        [Callback](bool bOk, const TSharedPtr<FJsonObject>&, const FString& Error) { Callback(bOk, Error); }, true);
}

void UEmberRealmSubsystem::Release(const FString& CharacterId, const FString& Lease)
{
    if (CharacterId.IsEmpty() || Lease.IsEmpty()) return;
    if (PendingMutations.FindRef(Lease) > 0)
    {
        DeferredReleases.Add(Lease, CharacterId);
        return;
    }
    SendRelease(CharacterId, Lease);
}

void UEmberRealmSubsystem::SendRelease(const FString& CharacterId, const FString& Lease)
{
    Request(TEXT("/v1/server/release"), IdentityBody(CharacterId, Lease),
        [](bool, const TSharedPtr<FJsonObject>&, const FString&) {}, true);
}

void UEmberRealmSubsystem::FetchParty(const FString& CharacterId, const FString& Lease, FEmberPartyCallback Callback)
{
    Request(TEXT("/v1/server/party"), IdentityBody(CharacterId, Lease),
        [Callback](bool bOk, const TSharedPtr<FJsonObject>& Root, const FString& Error)
        {
            FString PartyId, LeaderId;
            TArray<FString> Members;
            const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
            bool bValid = bOk && Root->TryGetStringField(TEXT("party_id"), PartyId)
                && Root->TryGetStringField(TEXT("leader_id"), LeaderId)
                && Root->TryGetArrayField(TEXT("members"), Values) && Values && Values->Num() <= 5;
            if (bValid)
            {
                for (const TSharedPtr<FJsonValue>& Value : *Values)
                {
                    FString Member;
                    if (!Value.IsValid() || !Value->TryGetString(Member) || Member.IsEmpty() || Member.Len() > 64)
                    {
                        bValid = false;
                        break;
                    }
                    Members.Add(Member);
                }
            }
            Callback(bValid, PartyId, LeaderId, Members, bOk && !bValid ? TEXT("realm_invalid_response") : Error);
        }, true);
}
