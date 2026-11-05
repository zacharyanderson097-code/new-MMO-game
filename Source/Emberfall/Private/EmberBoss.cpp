#include "EmberBoss.h"

#include "EmberCharacter.h"
#include "EmberDungeonManager.h"
#include "EmberGameMode.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Guid.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
    constexpr float MaximumHealth = 600.f;
    constexpr float InnerSafeRadius = 230.f;
    constexpr float OuterRingRadius = 750.f;
    constexpr float LeashDistance = 1600.f;

    // Engine cubes form real rendered outlines, including in packaged builds.
    // They never collide and contain no authoritative damage logic.
    void AddOutline(UInstancedStaticMeshComponent* Component, const FVector& Center, float Radius)
    {
        constexpr int32 Segments = 64;
        const float SegmentLength = 2.f * Radius * FMath::Sin(PI / Segments);
        for (int32 Index = 0; Index < Segments; ++Index)
        {
            const float Angle = 2.f * PI * Index / Segments;
            const FVector Location = Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * Radius;
            const FRotator Rotation(0.f, FMath::RadiansToDegrees(Angle) + 90.f, 0.f);
            Component->AddInstance(FTransform(Rotation, Location, FVector(SegmentLength / 100.f + .02f, .1f, .05f)), true);
        }
    }
}

AEmberBoss::AEmberBoss()
{
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true;
    bAlwaysRelevant = true;
    SetReplicateMovement(true);
    NetUpdateFrequency = 20.f;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("EncounterRoot"));

    Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SentinelBody"));
    Body->SetupAttachment(RootComponent);
    Body->SetRelativeLocation(FVector(0.f, 0.f, 155.f));
    Body->SetRelativeScale3D(FVector(3.f, 3.f, 3.f));
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    Crown = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SentinelCrown"));
    Crown->SetupAttachment(RootComponent);
    Crown->SetRelativeLocation(FVector(0.f, 0.f, 345.f));
    Crown->SetRelativeScale3D(FVector(1.5f, 1.5f, 1.5f));
    Crown->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    DangerOutline = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("DangerOutline"));
    DangerOutline->SetupAttachment(RootComponent);
    DangerOutline->SetMobility(EComponentMobility::Movable);
    DangerOutline->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    DangerOutline->SetCastShadow(false);
    DangerOutline->SetVisibility(false);

    SafeOutline = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("SafeOutline"));
    SafeOutline->SetupAttachment(RootComponent);
    SafeOutline->SetMobility(EComponentMobility::Movable);
    SafeOutline->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SafeOutline->SetCastShadow(false);
    SafeOutline->SetVisibility(false);

    static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (SphereMesh.Succeeded()) Body->SetStaticMesh(SphereMesh.Object);
    if (CubeMesh.Succeeded())
    {
        Crown->SetStaticMesh(CubeMesh.Object);
        DangerOutline->SetStaticMesh(CubeMesh.Object);
        SafeOutline->SetStaticMesh(CubeMesh.Object);
    }
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(
        TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if (ShapeMaterial.Succeeded())
    {
        DangerOutline->SetMaterial(0, ShapeMaterial.Object);
        SafeOutline->SetMaterial(0, ShapeMaterial.Object);
    }
}

void AEmberBoss::BeginPlay()
{
    Super::BeginPlay();
    if (HasAuthority()) EncounterId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
    if (GetNetMode() == NM_DedicatedServer) return;
    if (UMaterialInstanceDynamic* DangerMaterial = DangerOutline->CreateDynamicMaterialInstance(0))
    {
        DangerMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, .08f, .015f));
    }
    if (UMaterialInstanceDynamic* SafeMaterial = SafeOutline->CreateDynamicMaterialInstance(0))
    {
        SafeMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(.04f, 1.f, .2f));
    }
}

void AEmberBoss::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AEmberBoss, Health);
    DOREPLIFETIME(AEmberBoss, WindupRemaining);
    DOREPLIFETIME(AEmberBoss, WindupDuration);
    DOREPLIFETIME(AEmberBoss, ResetDelay);
    DOREPLIFETIME(AEmberBoss, AttackRadius);
    DOREPLIFETIME(AEmberBoss, bEngaged);
    DOREPLIFETIME(AEmberBoss, bWindingUp);
    DOREPLIFETIME(AEmberBoss, bRingAttack);
    DOREPLIFETIME(AEmberBoss, TelegraphCenter);
    DOREPLIFETIME(AEmberBoss, DungeonInstance);
    DOREPLIFETIME(AEmberBoss, EncounterId);
}

float AEmberBoss::GetHealthFraction() const
{
    return FMath::Clamp(Health / MaximumHealth, 0.f, 1.f);
}

int32 AEmberBoss::GetPhase() const
{
    const float Fraction = GetHealthFraction();
    return Fraction > .65f ? 1 : Fraction > .30f ? 2 : 3;
}

FString AEmberBoss::GetAttackLabel() const
{
    const FString Name = DungeonInstance.IsEmpty() ? TEXT("Cinderwarden") : TEXT("Keeper Serael");
    if (IsDefeated()) return Name + TEXT(" defeated - contributors earn personal rewards");
    if (!bEngaged) return Name + TEXT(" - approach and strike to begin");
    if (ResetDelay > 0.f) return TEXT("Encounter resetting");
    if (!bWindingUp) return TEXT("Recovering - attack now");
    return bRingAttack
        ? FString::Printf(TEXT("CINDER HALO %.1fs - move inside the green circle"), WindupRemaining)
        : FString::Printf(TEXT("ASHFALL %.1fs - move outside the red circle"), WindupRemaining);
}

void AEmberBoss::ReceiveStrike(float Damage, AEmberCharacter* Attacker)
{
    if (!HasAuthority() || !FMath::IsFinite(Damage) || Damage <= 0.f || IsDefeated()) return;
    UWorld* World = GetWorld();
    if (!World || !Attacker || !Attacker->IsProgressReady() || !Attacker->IsAlive()
        || Attacker->GetDungeonInstance() != DungeonInstance
        || FVector::Dist2D(Attacker->GetActorLocation(), GetActorLocation()) > LeashDistance) return;

    Contributors.Add(TWeakObjectPtr<AEmberCharacter>(Attacker));
    bEngaged = true;
    Health = FMath::Max(0.f, Health - Damage);
    if (IsDefeated())
    {
        bEngaged = false;
        bWindingUp = false;
        OnRep_Health();
        ForceNetUpdate();
        if (IsValid(DungeonManager) && !DungeonInstance.IsEmpty())
        {
            DungeonManager->OnDungeonBossDefeated(this);
        }
        else if (DungeonInstance.IsEmpty())
        {
            if (AEmberGameMode* Mode = World->GetAuthGameMode<AEmberGameMode>())
            {
                Mode->OnBossDefeated();
            }
        }
    }
}

void AEmberBoss::ConfigureDungeon(AEmberDungeonManager* Manager, const FString& InstanceId)
{
    if (!HasAuthority() || !IsValid(Manager) || InstanceId.IsEmpty()) return;
    DungeonManager = Manager;
    DungeonInstance = InstanceId;
    ForceNetUpdate();
}

bool AEmberBoss::HasContributed(AEmberCharacter* Player) const
{
    return Player && Contributors.Contains(TWeakObjectPtr<AEmberCharacter>(Player));
}

void AEmberBoss::OnRep_Health()
{
    Body->SetRelativeScale3D(IsDefeated() ? FVector(3.f, 3.f, .6f) : FVector(3.f, 3.f, 3.f));
    Body->SetRelativeLocation(FVector(0.f, 0.f, IsDefeated() ? 30.f : 155.f));
    Crown->SetVisibility(!IsDefeated());
    if (IsDefeated()) HideTelegraphs();
}

void AEmberBoss::ResetEncounter()
{
    if (!HasAuthority()) return;
    // Completed dungeon instances never regenerate their reward encounter.
    // Wipes may reset a living boss; a fresh completion needs a new instance.
    if (!DungeonInstance.IsEmpty() && IsDefeated()) return;
    EncounterId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
    Health = MaximumHealth;
    CooldownRemaining = 2.f;
    WindupRemaining = 0.f;
    ResetDelay = 0.f;
    AttackNumber = 0;
    bEngaged = false;
    bWindingUp = false;
    bRingAttack = false;
    Contributors.Reset();
    HideTelegraphs();
    OnRep_Health();
    ForceNetUpdate();
}

AEmberCharacter* AEmberBoss::FindNearestTarget() const
{
    AEmberCharacter* Nearest = nullptr;
    float NearestDistance = LeashDistance;
    for (TActorIterator<AEmberCharacter> It(GetWorld()); It; ++It)
    {
        AEmberCharacter* Player = *It;
        // A bystander at the sanctuary cannot hold a wiped encounter open or
        // become its ranged target. New participants join by striking the boss.
        if (!Player->IsProgressReady() || !Player->IsAlive() || Player->GetDungeonInstance() != DungeonInstance
            || Player->IsInsidePvpZone() || !HasContributed(Player)) continue;
        const float Distance = FVector::Dist2D(Player->GetActorLocation(), GetActorLocation());
        if (Distance <= NearestDistance)
        {
            Nearest = Player;
            NearestDistance = Distance;
        }
    }
    return Nearest;
}

void AEmberBoss::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (IsDefeated() || !bEngaged || !bWindingUp) HideTelegraphs();
    if (IsDefeated() || !bEngaged) return;

    if (!HasAuthority())
    {
        if (bWindingUp) DrawTelegraph();
        return;
    }

    AEmberCharacter* Player = FindNearestTarget();
    if (!Player)
    {
        bWindingUp = false;
        HideTelegraphs();
        ResetDelay += DeltaSeconds;
        if (ResetDelay >= 4.f) ResetEncounter();
        return;
    }
    ResetDelay = 0.f;

    if (bWindingUp)
    {
        WindupRemaining = FMath::Max(0.f, WindupRemaining - DeltaSeconds);
        DrawTelegraph();
        if (WindupRemaining <= 0.f)
        {
            ResolveAttack();
            bWindingUp = false;
            HideTelegraphs();
            const int32 Phase = GetPhase();
            CooldownRemaining = Phase == 1 ? 4.f : Phase == 2 ? 3.2f : 2.6f;
        }
    }
    else
    {
        CooldownRemaining -= DeltaSeconds;
        if (CooldownRemaining <= 0.f) BeginAttack(Player);
    }
}

void AEmberBoss::BeginAttack(AEmberCharacter* Player)
{
    const int32 Phase = GetPhase();
    ++AttackNumber;
    bRingAttack = Phase >= 2 && AttackNumber % 3 == 0;
    AttackRadius = Phase == 1 ? 320.f : Phase == 2 ? 400.f : 480.f;
    AttackDamage = Phase == 1 ? 22.f : Phase == 2 ? 28.f : 34.f;
    WindupDuration = Phase == 1 ? 1.8f : Phase == 2 ? 1.55f : 1.3f;
    WindupRemaining = WindupDuration;
    TelegraphCenter = bRingAttack ? GetActorLocation() : Player->GetActorLocation();
    if (!bRingAttack) TelegraphCenter.Z -= Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    TelegraphCenter.Z += 8.f;
    bWindingUp = true;
}

void AEmberBoss::ResolveAttack()
{
    if (!HasAuthority()) return;
    for (TActorIterator<AEmberCharacter> It(GetWorld()); It; ++It)
    {
        AEmberCharacter* Player = *It;
        if (!Player->IsProgressReady() || !Player->IsAlive() || Player->GetDungeonInstance() != DungeonInstance) continue;
        const FVector PlayerLocation = Player->GetActorLocation();
        const float Distance = FVector::Dist2D(PlayerLocation, TelegraphCenter);
        const bool bInDanger = bRingAttack
            ? Distance >= InnerSafeRadius && Distance <= OuterRingRadius
            : Distance <= AttackRadius;
        if (bInDanger && FMath::Abs(PlayerLocation.Z - TelegraphCenter.Z) <= 350.f)
        {
            Player->ReceiveCombatDamage(AttackDamage);
        }
    }
}

void AEmberBoss::DrawTelegraph() const
{
    if (GetNetMode() == NM_DedicatedServer) return;
    DangerOutline->ClearInstances();
    SafeOutline->ClearInstances();
    const float Radius = bRingAttack ? OuterRingRadius : AttackRadius;
    AddOutline(DangerOutline, TelegraphCenter, Radius);
    DangerOutline->SetVisibility(true);
    SafeOutline->SetVisibility(bRingAttack);
    if (bRingAttack)
    {
        AddOutline(SafeOutline, TelegraphCenter + FVector(0.f, 0.f, 2.f), InnerSafeRadius);
    }
}

void AEmberBoss::HideTelegraphs() const
{
    if (GetNetMode() == NM_DedicatedServer) return;
    if (DangerOutline->GetInstanceCount() > 0) DangerOutline->ClearInstances();
    if (SafeOutline->GetInstanceCount() > 0) SafeOutline->ClearInstances();
    DangerOutline->SetVisibility(false);
    SafeOutline->SetVisibility(false);
}
