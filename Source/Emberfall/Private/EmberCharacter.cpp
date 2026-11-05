#include "EmberCharacter.h"
#include "EmberBoss.h"
#include "EmberGameMode.h"
#include "EmberDungeonManager.h"
#include "EmberRealmSubsystem.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerState.h"
#include "EmberRules.h"
#include "EmberSaveGame.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
    constexpr uint8 StrikeAction = 0, HeavyAction = 1, MendAction = 2, DodgeAction = 3;
    constexpr uint8 InteractAction = 4, ConsentAction = 5;
    const FVector PvpCenter(0.f, 2500.f, 0.f);
    constexpr float PvpRadius = 650.f;
    const TCHAR* SaveSlot = TEXT("Emberfall_Standalone_v1");
}

AEmberCharacter::AEmberCharacter()
{
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true;
    SetReplicateMovement(true);
    GetCapsuleComponent()->InitCapsuleSize(38.f, 90.f);
    // Players cannot body-block cooperative event entrances or the forge.
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
    bUseControllerRotationYaw = true;
    GetCharacterMovement()->MaxWalkSpeed = 600.f;
    GetCharacterMovement()->BrakingDecelerationWalking = 2200.f;
    GetCharacterMovement()->bOrientRotationToMovement = false;

    CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
    CameraBoom->SetupAttachment(RootComponent);
    CameraBoom->TargetArmLength = 620.f;
    CameraBoom->SocketOffset = FVector(0.f, 55.f, 90.f);
    CameraBoom->bUsePawnControlRotation = true;
    CameraBoom->bEnableCameraLag = true;
    CameraBoom->CameraLagSpeed = 12.f;
    FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
    FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
    FollowCamera->FieldOfView = 85.f;

    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    Armor = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Armor"));
    Armor->SetupAttachment(RootComponent);
    Armor->SetStaticMesh(Cube.Object);
    Armor->SetRelativeScale3D(FVector(.52f, .7f, .9f));
    Armor->SetRelativeLocation(FVector(0.f, 0.f, -18.f));
    Armor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Helmet = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Helmet"));
    Helmet->SetupAttachment(RootComponent);
    Helmet->SetStaticMesh(Sphere.Object);
    Helmet->SetRelativeScale3D(FVector(.42f));
    Helmet->SetRelativeLocation(FVector(0.f, 0.f, 57.f));
    Helmet->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Blade = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Blade"));
    Blade->SetupAttachment(RootComponent);
    Blade->SetStaticMesh(Cube.Object);
    Blade->SetRelativeLocation(FVector(60.f, 42.f, -5.f));
    Blade->SetRelativeRotation(FRotator(-35.f, 0.f, 0.f));
    Blade->SetRelativeScale3D(FVector(.12f, .08f, 1.35f));
    Blade->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AEmberCharacter::BeginPlay()
{
    Super::BeginPlay();
    if (HasAuthority() && GetNetMode() == NM_Standalone)
    {
        bProgressReady = true;
        if (const UEmberSaveGame* Save = Cast<UEmberSaveGame>(UGameplayStatics::LoadGameFromSlot(SaveSlot, 0)))
        {
            if (Save->Version == 1)
            {
                GearTier = FMath::Clamp(Save->GearTier, 0, EmberRules::MaxTier);
                Sigils = FMath::Clamp(Save->Sigils, 0, 1000000);
                Victories = FMath::Clamp(Save->Victories, 0, 1000000);
                DungeonClears = FMath::Clamp(Save->DungeonClears, 0, 1000000);
                Emberstone = FMath::Clamp(Save->Emberstone, 0, 1000000);
                bChapterStarted = Save->bChapterStarted;
            }
        }
    }
    if (HasAuthority() && !bProgressReady) GetCharacterMovement()->DisableMovement();
    InitializeLocalView();
}

void AEmberCharacter::PawnClientRestart()
{
    Super::PawnClientRestart();
    // Late-joining pawns may BeginPlay before their owning controller is replicated.
    InitializeLocalView();
}

void AEmberCharacter::InitializeLocalView()
{
    if (IsLocallyControlled() && !bLocalViewInitialized)
    {
        if (APlayerController* PC = Cast<APlayerController>(Controller))
        {
            bLocalViewInitialized = true;
            PC->bShowMouseCursor = false;
            PC->SetInputMode(FInputModeGameOnly());
            PC->SetControlRotation(FRotator(-18.f, 0.f, 0.f));
        }
        Notification = TEXT("The gate is ash. The Wayfire still remembers. Find the beacon ahead [E].");
        NotificationUntil = GetWorld()->GetTimeSeconds() + 18.f;
    }
}

void AEmberCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!HasAuthority()) return;
    const float Now = GetWorld()->GetTimeSeconds();
    if (!bProgressReady) return;
    const FVector Position = GetActorLocation();
    AEmberGameMode* Mode = GetWorld()->GetAuthGameMode<AEmberGameMode>();
    AEmberDungeonManager* Dungeons = Mode ? Mode->GetDungeonManager() : nullptr;
    const bool bInDungeon = !DungeonInstance.IsEmpty();
    const FVector Recovery = bInDungeon && Dungeons
        ? Dungeons->GetRespawnLocation(this) : FVector(0.f, 0.f, 130.f);
    const bool bOutsideRegion = !bInDungeon && (Position.X < -2900.f
        || Position.X > 6900.f || Position.Y < -4400.f || Position.Y > 5400.f);
    if (Position.Z < -500.f || Position.Z > 5000.f || bOutsideRegion
        || (bInDungeon && FVector::DistSquared2D(Position, Recovery) > FMath::Square(4500.f)))
    {
        GetCharacterMovement()->StopMovementImmediately();
        TeleportTo(Recovery, FRotator::ZeroRotator);
        Notify(TEXT("The Wayfire guides you back from the edge of the test region."));
    }
    if (!IsAlive())
    {
        if (Now >= RespawnAt)
        {
            const FVector Spawn = Recovery + FVector(0.f, FMath::RandRange(-100.f, 100.f), 0.f);
            GetCharacterMovement()->StopMovementImmediately();
            TeleportTo(Spawn, FRotator::ZeroRotator);
            Health = EmberRules::MaxHealth;
            Stamina = EmberRules::MaxStamina;
            InvulnerableUntil = Now + 2.f;
            Notify(TEXT("The Wayfire calls you back. Your earned gear is safe."));
        }
        return;
    }
    Stamina = FMath::Min(EmberRules::MaxStamina, Stamina + DeltaSeconds * (bGuarding ? 4.f : 19.f));
    if (Now - LastDamageAt > 12.f)
        Health = FMath::Min(EmberRules::MaxHealth, Health + DeltaSeconds * 3.f);
}

void AEmberCharacter::SetupPlayerInputComponent(UInputComponent* Input)
{
    Super::SetupPlayerInputComponent(Input);
    Input->BindAxis(TEXT("MoveForward"), this, &AEmberCharacter::MoveForward);
    Input->BindAxis(TEXT("MoveRight"), this, &AEmberCharacter::MoveRight);
    Input->BindAxis(TEXT("Turn"), this, &AEmberCharacter::Turn);
    Input->BindAxis(TEXT("LookUp"), this, &AEmberCharacter::LookUp);
    Input->BindAction(TEXT("Strike"), IE_Pressed, this, &AEmberCharacter::Strike);
    Input->BindAction(TEXT("Heavy"), IE_Pressed, this, &AEmberCharacter::Heavy);
    Input->BindAction(TEXT("Mend"), IE_Pressed, this, &AEmberCharacter::Mend);
    Input->BindAction(TEXT("Dodge"), IE_Pressed, this, &AEmberCharacter::Dodge);
    Input->BindAction(TEXT("Interact"), IE_Pressed, this, &AEmberCharacter::Interact);
    Input->BindAction(TEXT("PvpConsent"), IE_Pressed, this, &AEmberCharacter::TogglePvp);
    Input->BindAction(TEXT("Guard"), IE_Pressed, this, &AEmberCharacter::GuardPressed);
    Input->BindAction(TEXT("Guard"), IE_Released, this, &AEmberCharacter::GuardReleased);
    Input->BindAction(TEXT("ReleaseMouse"), IE_Pressed, this, &AEmberCharacter::ReleaseMouse);
}

void AEmberCharacter::MoveForward(float Value)
{
    if (Controller && bProgressReady && IsAlive() && !bMouseReleased)
        AddMovementInput(FRotationMatrix(FRotator(0, Controller->GetControlRotation().Yaw, 0)).GetUnitAxis(EAxis::X), Value);
}
void AEmberCharacter::MoveRight(float Value)
{
    if (Controller && bProgressReady && IsAlive() && !bMouseReleased)
        AddMovementInput(FRotationMatrix(FRotator(0, Controller->GetControlRotation().Yaw, 0)).GetUnitAxis(EAxis::Y), Value);
}
void AEmberCharacter::Turn(float Value) { if (!bMouseReleased) AddControllerYawInput(Value); }
void AEmberCharacter::LookUp(float Value) { if (!bMouseReleased) AddControllerPitchInput(Value); }
void AEmberCharacter::Strike() { if (!bMouseReleased) ServerUseAction(StrikeAction); }
void AEmberCharacter::Heavy() { if (!bMouseReleased) ServerUseAction(HeavyAction); }
void AEmberCharacter::Mend() { if (!bMouseReleased) ServerUseAction(MendAction); }
void AEmberCharacter::Dodge() { if (!bMouseReleased) ServerUseAction(DodgeAction); }
void AEmberCharacter::Interact() { if (!bMouseReleased) ServerUseAction(InteractAction); }
void AEmberCharacter::TogglePvp() { if (!bMouseReleased) ServerUseAction(ConsentAction); }
void AEmberCharacter::GuardPressed() { if (!bMouseReleased) ServerSetGuard(true); }
void AEmberCharacter::GuardReleased() { ServerSetGuard(false); }

void AEmberCharacter::ReleaseMouse()
{
    if (APlayerController* PC = Cast<APlayerController>(Controller))
    {
        bMouseReleased = !bMouseReleased;
        PC->bShowMouseCursor = bMouseReleased;
        ServerSetGuard(false);
        if (bMouseReleased) PC->SetInputMode(FInputModeGameAndUI());
        else PC->SetInputMode(FInputModeGameOnly());
    }
}

void AEmberCharacter::ServerUseAction_Implementation(uint8 Action)
{
    if (!bProgressReady || !IsAlive()) return;
    const float Now = GetWorld()->GetTimeSeconds();
    switch (Action)
    {
    case StrikeAction:
    case HeavyAction:
        ExecuteStrike(Action == HeavyAction);
        break;
    case MendAction:
        if (Now < NextMendAt || Stamina < EmberRules::HealCost || Health >= EmberRules::MaxHealth) return;
        Stamina -= EmberRules::HealCost;
        NextMendAt = Now + 12.f;
        Health = FMath::Min(EmberRules::MaxHealth, Health + 35.f);
        Notify(TEXT("Wayfire mend: +35 health."));
        break;
    case DodgeAction:
        if (Now < NextDodgeAt || Stamina < EmberRules::DodgeCost) return;
        Stamina -= EmberRules::DodgeCost;
        NextDodgeAt = Now + .85f;
        InvulnerableUntil = Now + .35f;
        bGuarding = false;
        {
            FVector Direction = GetVelocity().GetSafeNormal2D();
            if (Direction.IsNearlyZero()) Direction = GetActorForwardVector();
            LaunchCharacter(Direction * 1000.f + FVector(0.f, 0.f, 100.f), true, true);
        }
        break;
    case InteractAction:
        if (Now < NextInteractionAt) return;
        NextInteractionAt = Now + .4f;
        if (AEmberGameMode* Mode = GetWorld()->GetAuthGameMode<AEmberGameMode>()) Mode->Interact(this);
        break;
    case ConsentAction:
        if (Now < NextConsentAt || !DungeonInstance.IsEmpty()) return;
        NextConsentAt = Now + 1.f;
        // Leaving consent mid-fight cannot be used as invulnerability.
        if (Now - LastDamageAt < 10.f)
        {
            Notify(TEXT("PvP consent cannot change for 10 seconds after taking damage."));
            return;
        }
        bPvpOptedIn = !bPvpOptedIn;
        Notify(bPvpOptedIn ? TEXT("PvP consent ON. Combat requires both players inside the marked grounds.")
                         : TEXT("PvP consent OFF. You cannot damage or be damaged by players."));
        break;
    default: break;
    }
}

void AEmberCharacter::ServerSetGuard_Implementation(bool bGuard)
{
    const float Now = GetWorld()->GetTimeSeconds();
    // Always allow releasing guard. Repeated requests cannot replenish any resource.
    if (!bGuard) { bGuarding = false; return; }
    if (Now < NextGuardToggleAt) return;
    NextGuardToggleAt = Now + .1f;
    bGuarding = bProgressReady && IsAlive() && Stamina >= 10.f;
}

void AEmberCharacter::ExecuteStrike(bool bHeavy)
{
    const float Now = GetWorld()->GetTimeSeconds();
    if (Now < NextStrikeAt || (bHeavy && (Now < NextHeavyAt || Stamina < EmberRules::HeavyCost))) return;
    NextStrikeAt = Now + (bHeavy ? .9f : .55f);
    if (bHeavy) { Stamina -= EmberRules::HeavyCost; NextHeavyAt = Now + 6.f; }
    bGuarding = false;
    MulticastStrikeEffect(bHeavy);
    const float Damage = EmberRules::WeaponDamage(GearTier) * (bHeavy ? 2.2f : 1.f);
    auto InReach = [this](const AActor* Target, float Reach)
    {
        const FVector Delta = Target->GetActorLocation() - GetActorLocation();
        if (Delta.SizeSquared2D() > FMath::Square(Reach) || FMath::Abs(Delta.Z) >= 350.f
            || FVector::DotProduct(GetActorForwardVector(), Delta.GetSafeNormal2D()) < .15f) return false;
        FHitResult Hit;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(EmberMelee), false, this);
        Query.AddIgnoredActor(Target);
        return !GetWorld()->LineTraceSingleByChannel(Hit, GetActorLocation(),
            Target->GetActorLocation() + FVector(0.f, 0.f, 50.f), ECC_Visibility, Query);
    };
    for (TActorIterator<AEmberBoss> It(GetWorld()); It; ++It)
    {
        if (It->GetDungeonInstance() == DungeonInstance && !It->IsDefeated() && InReach(*It, 450.f))
        {
            if (bChapterStarted)
            {
                LastDamageAt = Now;
                It->ReceiveStrike(Damage, this);
            }
            else Notify(TEXT("Rekindle the Wayfire beacon [E] before challenging the Warden."));
        }
    }
    for (TActorIterator<AEmberCharacter> It(GetWorld()); It; ++It)
    {
        AEmberCharacter* Target = *It;
        if (Target == this || !Target->bProgressReady || !Target->IsAlive()
            || Target->DungeonInstance != DungeonInstance || !InReach(Target, 230.f)) continue;
        if (EmberRules::CanDamagePlayer(bPvpOptedIn, Target->bPvpOptedIn, IsInsidePvpZone(), Target->IsInsidePvpZone()))
        {
            // PvP uses equalized weapon power; overworld grinding doesn't buy a win here.
            LastDamageAt = Now;
            Target->ReceiveCombatDamage(EmberRules::WeaponDamage(0) * (bHeavy ? 1.6f : 1.f));
        }
    }
}

void AEmberCharacter::ReceiveCombatDamage(float Amount)
{
    if (!HasAuthority() || !bProgressReady || !IsAlive() || !FMath::IsFinite(Amount) || Amount <= 0.f) return;
    const float Now = GetWorld()->GetTimeSeconds();
    if (Now < InvulnerableUntil) return;
    LastDamageAt = Now;
    if (bGuarding && Stamina >= 18.f)
    {
        Stamina -= 18.f;
        Amount *= .3f;
        if (Stamina < 18.f) bGuarding = false;
    }
    Health = FMath::Max(0.f, Health - Amount);
    if (!IsAlive())
    {
        RespawnAt = Now + 5.f;
        bGuarding = false;
        GetCharacterMovement()->StopMovementImmediately();
        Notify(TEXT("You have fallen. Returning to the Wayfire in 5 seconds. No gear is lost."));
    }
}

bool AEmberCharacter::IsInsidePvpZone() const
{
    if (!DungeonInstance.IsEmpty() || !bProgressReady) return false;
    const FVector Delta = GetActorLocation() - PvpCenter;
    return Delta.SizeSquared2D() <= FMath::Square(PvpRadius) && FMath::Abs(Delta.Z) < 400.f;
}

bool AEmberCharacter::IsInCombat() const
{
    return GetWorld()->GetTimeSeconds() - LastDamageAt < 10.f;
}

void AEmberCharacter::SetDungeonInstance(const FString& InstanceId)
{
    if (!HasAuthority()) return;
    DungeonInstance = InstanceId;
    bPvpOptedIn = false;
    bGuarding = false;
    GetCharacterMovement()->StopMovementImmediately();
    ForceNetUpdate();
}

void AEmberCharacter::ApplyRealmState(const FEmberRealmCharacter& State)
{
    if (!HasAuthority() || State.CharacterId != RealmCharacterId || State.Revision < RealmRevision) return;
    RealmRevision = State.Revision;
    GearTier = FMath::Clamp(State.GearTier, 0, EmberRules::MaxTier);
    Sigils = FMath::Clamp(State.Sigils, 0, 1000000);
    Victories = FMath::Clamp(State.Victories, 0, 1000000);
    DungeonClears = FMath::Clamp(State.DungeonClears, 0, 1000000);
    Emberstone = FMath::Clamp(State.Emberstone, 0, 1000000);
    bChapterStarted = State.bChapterStarted;
    ForceNetUpdate();
}

void AEmberCharacter::ApplyRealmIdentity(const FEmberRealmCharacter& State, const FString& Lease)
{
    if (!HasAuthority() || bProgressReady || State.CharacterId.IsEmpty() || Lease.IsEmpty()) return;
    if (!RealmCharacterId.IsEmpty() && RealmCharacterId != State.CharacterId) return;
    RealmCharacterId = State.CharacterId;
    RealmLease = Lease;
    RealmRevision = -1;
    ApplyRealmState(State);
    Health = EmberRules::MaxHealth;
    Stamina = EmberRules::MaxStamina;
    if (APlayerState* PS = GetPlayerState()) PS->SetPlayerName(State.Username);
    bProgressReady = true;
    GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    Notify(TEXT("Realm character loaded. Your gear and progress are saved by the server."));
}

void AEmberCharacter::InvalidateRealmIdentity()
{
    if (!HasAuthority()) return;
    bProgressReady = false;
    RealmLease.Empty();
    bForgePending = false;
    bChapterPending = false;
    DungeonInstance.Empty();
    bGuarding = false;
    bPvpOptedIn = false;
    GetCharacterMovement()->DisableMovement();
    TeleportTo(FVector(0.f, 0.f, 130.f), FRotator::ZeroRotator);
    Notify(TEXT("Realm connection lost. Log in with a new one-use ticket to continue."));
}

void AEmberCharacter::SubmitMutation(const FString& Operation, const FString& RequestId, const FString& SuccessText)
{
    if (!HasAuthority() || !bProgressReady || GetNetMode() == NM_Standalone) return;
    UEmberRealmSubsystem* Realm = GetGameInstance()->GetSubsystem<UEmberRealmSubsystem>();
    if (!Realm || !Realm->GetIsConfigured())
    {
        bForgePending = false;
        bChapterPending = false;
        Notify(TEXT("Realm service is not configured; no unsaved reward or upgrade was granted."));
        return;
    }
    TWeakObjectPtr<AEmberCharacter> WeakThis(this);
    const FString SubmittedLease = RealmLease;
    Realm->Mutate(RealmCharacterId, RealmLease, Operation, RequestId,
        [WeakThis, SubmittedLease, Operation, SuccessText](bool bSuccess, const FEmberRealmCharacter& State, const FString& Error)
        {
            AEmberCharacter* Player = WeakThis.Get();
            if (!Player || Player->RealmLease != SubmittedLease) return;
            if (Operation == TEXT("forge")) Player->bForgePending = false;
            if (Operation == TEXT("chapter")) Player->bChapterPending = false;
            if (bSuccess)
            {
                Player->ApplyRealmState(State);
                Player->Notify(SuccessText);
            }
            else
            {
                Player->Notify(TEXT("The realm could not confirm this action. Reconnect to refresh saved progress."));
                if (Error.Contains(TEXT("lease"))) Player->InvalidateRealmIdentity();
            }
        });
}

void AEmberCharacter::SetChapterStarted(bool bStarted)
{
    if (!HasAuthority() || !bProgressReady || !bStarted || bChapterStarted || bChapterPending) return;
    if (GetNetMode() != NM_Standalone)
    {
        bChapterPending = true;
        SubmitMutation(TEXT("chapter"), FGuid::NewGuid().ToString(),
            TEXT("The beacon remembers. Your chapter is saved. Challenge the Cinderwarden or enter the Archive."));
        return;
    }
    bChapterStarted = true;
    SaveProgress();
}

void AEmberCharacter::AwardBossLoot(const FString& EncounterId)
{
    if (!HasAuthority() || !bProgressReady || EncounterId.IsEmpty()) return;
    if (GetNetMode() != NM_Standalone)
    {
        SubmitMutation(TEXT("boss_reward"), EncounterId + TEXT(":boss"),
            TEXT("Cinderwarden defeated! Your personal Ember Sigil is saved to the realm."));
        return;
    }
    const EmberRules::Reward Reward = EmberRules::GrantBossReward(Sigils, Victories);
    Sigils = Reward.Sigils;
    Victories = Reward.Victories;
    Notify(TEXT("Cinderwarden defeated! +1 personal Ember Sigil. Return to the sanctuary forge [E]."));
    SaveProgress();
}

void AEmberCharacter::AwardDungeonLoot(const FString& EncounterId)
{
    if (!HasAuthority() || !bProgressReady || EncounterId.IsEmpty()) return;
    if (GetNetMode() != NM_Standalone)
    {
        SubmitMutation(TEXT("dungeon_reward"), EncounterId + TEXT(":dungeon"),
            TEXT("Archive cleared! +1 personal Sigil and a saved dungeon clear. Return to the entrance [E]."));
        return;
    }
    const EmberRules::Reward Reward = EmberRules::GrantBossReward(Sigils, Victories);
    Sigils = Reward.Sigils;
    Victories = Reward.Victories;
    DungeonClears = FMath::Min(1000000, DungeonClears + 1);
    Notify(TEXT("Archive cleared! +1 personal Sigil. Return to the entrance [E]."));
    SaveProgress();
}

void AEmberCharacter::AwardGathering()
{
    if (!HasAuthority() || !bProgressReady) return;
    if (GetNetMode() != NM_Standalone)
    {
        SubmitMutation(TEXT("gather"), FGuid::NewGuid().ToString(), TEXT("Gathered +1 Emberstone. Stored in your realm inventory."));
        return;
    }
    Emberstone = FMath::Min(1000000, Emberstone + 1);
    Notify(TEXT("Gathered +1 Emberstone."));
    SaveProgress();
}

void AEmberCharacter::ForgeGear()
{
    if (!HasAuthority() || !bProgressReady || bForgePending) return;
    if (GearTier >= EmberRules::MaxTier)
    {
        Notify(TEXT("Dawnbound blade mastered. Its power was earned, never purchased."));
        return;
    }
    if (!EmberRules::CanForge(GearTier, Sigils))
    {
        Notify(FString::Printf(TEXT("Next forge requires %d earned Sigils. You have %d."), EmberRules::ForgeCost(GearTier), Sigils));
        return;
    }
    if (GetNetMode() != NM_Standalone)
    {
        bForgePending = true;
        SubmitMutation(TEXT("forge"), FGuid::NewGuid().ToString(), TEXT("Earned gear forged and saved to the realm."));
        return;
    }
    Sigils -= EmberRules::ForgeCost(GearTier);
    ++GearTier;
    Notify(FString::Printf(TEXT("Forged %s. Every upgrade comes from playing."), *GetGearName()));
    SaveProgress();
}

void AEmberCharacter::SaveProgress()
{
    // Network progression uses realm transactions; this file is standalone-only.
    if (GetNetMode() != NM_Standalone) return;
    UEmberSaveGame* Save = Cast<UEmberSaveGame>(UGameplayStatics::CreateSaveGameObject(UEmberSaveGame::StaticClass()));
    if (!Save) return;
    Save->GearTier = GearTier;
    Save->Sigils = Sigils;
    Save->Victories = Victories;
    Save->DungeonClears = DungeonClears;
    Save->Emberstone = Emberstone;
    Save->bChapterStarted = bChapterStarted;
    if (!UGameplayStatics::SaveGameToSlot(Save, SaveSlot, 0)) Notify(TEXT("Progress is in memory, but the local save failed."));
}

void AEmberCharacter::Notify(const FString& Message)
{
    if (HasAuthority()) ClientNotify(Message);
}

void AEmberCharacter::ClientNotify_Implementation(const FString& Message)
{
    Notification = Message;
    NotificationUntil = GetWorld()->GetTimeSeconds() + 9.f;
}

void AEmberCharacter::MulticastStrikeEffect_Implementation(bool bHeavy)
{
    if (GetNetMode() == NM_DedicatedServer) return;
    const FVector Start = GetActorLocation() + GetActorForwardVector() * 40.f;
    DrawDebugDirectionalArrow(GetWorld(), Start, Start + GetActorForwardVector() * 400.f, 65.f,
        bHeavy ? FColor(255, 170, 65) : FColor(100, 225, 255), false, .2f, 0, bHeavy ? 10.f : 5.f);
}

float AEmberCharacter::GetHealthFraction() const { return Health / EmberRules::MaxHealth; }
float AEmberCharacter::GetStaminaFraction() const { return Stamina / EmberRules::MaxStamina; }
FString AEmberCharacter::GetGearName() const
{
    switch (GearTier)
    {
    case 1: return TEXT("Tempered blade");
    case 2: return TEXT("Dawnbound blade");
    default: return TEXT("Wanderer's blade");
    }
}
FString AEmberCharacter::GetNotification() const
{
    return GetWorld()->GetTimeSeconds() <= NotificationUntil ? Notification : FString();
}
float AEmberCharacter::GetSynchronizedTime() const
{
    if (const AGameStateBase* GS = GetWorld()->GetGameState()) return GS->GetServerWorldTimeSeconds();
    return GetWorld()->GetTimeSeconds();
}
float AEmberCharacter::GetAbilityCooldown(int32 Slot) const
{
    float Until = 0.f;
    switch (Slot)
    {
    case 0: Until = NextStrikeAt; break;
    case 1: Until = NextHeavyAt; break;
    case 2: Until = NextMendAt; break;
    case 3: Until = NextDodgeAt; break;
    default: break;
    }
    return FMath::Max(0.f, Until - GetSynchronizedTime());
}

void AEmberCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AEmberCharacter, Health);
    DOREPLIFETIME(AEmberCharacter, bGuarding);
    DOREPLIFETIME(AEmberCharacter, bPvpOptedIn);
    DOREPLIFETIME_CONDITION(AEmberCharacter, Stamina, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AEmberCharacter, GearTier, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AEmberCharacter, Sigils, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AEmberCharacter, Victories, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AEmberCharacter, DungeonClears, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AEmberCharacter, Emberstone, COND_OwnerOnly);
    DOREPLIFETIME(AEmberCharacter, bProgressReady);
    DOREPLIFETIME_CONDITION(AEmberCharacter, RealmCharacterId, COND_OwnerOnly);
    DOREPLIFETIME(AEmberCharacter, DungeonInstance);
    DOREPLIFETIME_CONDITION(AEmberCharacter, bChapterStarted, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AEmberCharacter, NextStrikeAt, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AEmberCharacter, NextHeavyAt, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AEmberCharacter, NextMendAt, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AEmberCharacter, NextDodgeAt, COND_OwnerOnly);
}
