#include "EmberHUD.h"

#include "EmberBoss.h"
#include "EmberCharacter.h"
#include "EmberGameState.h"
#include "EmberWorldEvent.h"
#include "EngineUtils.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

namespace EmberHUDColors
{
    const FLinearColor Ink(0.025f, 0.035f, 0.045f, 0.90f);
    const FLinearColor Border(0.32f, 0.34f, 0.32f, 0.72f);
    const FLinearColor Ivory(0.93f, 0.90f, 0.81f, 1.f);
    const FLinearColor Muted(0.65f, 0.69f, 0.69f, 1.f);
    const FLinearColor Gold(0.93f, 0.69f, 0.30f, 1.f);
    const FLinearColor Health(0.72f, 0.22f, 0.20f, 1.f);
    const FLinearColor Stamina(0.24f, 0.68f, 0.61f, 1.f);
}

void AEmberHUD::Panel(float X, float Y, float Width, float Height, float Scale)
{
    DrawRect(EmberHUDColors::Border, X, Y, Width, Height);
    DrawRect(EmberHUDColors::Ink, X + Scale, Y + Scale,
        FMath::Max(0.f, Width - 2.f * Scale), FMath::Max(0.f, Height - 2.f * Scale));
}

void AEmberHUD::Label(const FString& Text, float X, float Y, float Scale,
    FLinearColor Color, float FontScale, bool bLarge)
{
    if (!GEngine)
    {
        return;
    }
    UFont* Font = bLarge ? GEngine->GetMediumFont() : GEngine->GetSmallFont();
    DrawText(Text, Color, X, Y, Font, Scale * FontScale);
}

void AEmberHUD::WrappedLabel(const FString& Text, float X, float Y, float Width,
    float Scale, FLinearColor Color, int32 MaxLines)
{
    if (!GEngine || Text.IsEmpty())
    {
        return;
    }

    TArray<FString> Words;
    Text.ParseIntoArrayWS(Words);
    FString Line;
    int32 LineIndex = 0;
    for (const FString& Word : Words)
    {
        const FString Candidate = Line.IsEmpty() ? Word : Line + TEXT(" ") + Word;
        float TextWidth = 0.f;
        float TextHeight = 0.f;
        GetTextSize(Candidate, TextWidth, TextHeight, GEngine->GetSmallFont(), Scale);
        if (TextWidth > Width && !Line.IsEmpty())
        {
            Label(Line, X, Y + LineIndex * 19.f * Scale, Scale, Color);
            ++LineIndex;
            if (LineIndex >= MaxLines)
            {
                return;
            }
            Line = Word;
        }
        else
        {
            Line = Candidate;
        }
    }
    if (!Line.IsEmpty() && LineIndex < MaxLines)
    {
        Label(Line, X, Y + LineIndex * 19.f * Scale, Scale, Color);
    }
}

void AEmberHUD::Meter(float X, float Y, float Width, float Fraction, float Scale,
    FLinearColor Color, const FString& Caption)
{
    Label(Caption, X, Y, Scale, EmberHUDColors::Ivory);
    DrawRect(FLinearColor(0.10f, 0.12f, 0.13f, 1.f), X, Y + 20.f * Scale,
        Width, 7.f * Scale);
    DrawRect(Color, X, Y + 20.f * Scale, Width * FMath::Clamp(Fraction, 0.f, 1.f),
        7.f * Scale);
}

void AEmberHUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas || !GEngine || !GetOwningPlayerController())
    {
        return;
    }

    AEmberCharacter* Player = Cast<AEmberCharacter>(GetOwningPlayerController()->GetPawn());
    if (!Player)
    {
        return;
    }
    AEmberGameState* State = GetWorld() ? GetWorld()->GetGameState<AEmberGameState>() : nullptr;
    AEmberBoss* Boss = State ? State->GetBoss() : nullptr;
    const bool bInDungeon = !Player->GetDungeonInstance().IsEmpty();
    if (bInDungeon)
    {
        Boss = nullptr;
        for (TActorIterator<AEmberBoss> It(GetWorld()); It; ++It)
            if (It->GetDungeonInstance() == Player->GetDungeonInstance()) { Boss = *It; break; }
    }

    const float ViewWidth = Canvas->ClipX;
    const float ViewHeight = Canvas->ClipY;
    const float Scale = FMath::Min(ViewWidth / 1440.f, ViewHeight / 900.f);
    const float Margin = 24.f * Scale;
    const float LeftWidth = 312.f * Scale;
    const float RightWidth = 280.f * Scale;

    Panel(Margin, Margin, LeftWidth, 174.f * Scale, Scale);
    DrawRect(EmberHUDColors::Gold, Margin, Margin, 3.f * Scale, 174.f * Scale);
    Label(TEXT("E M B E R F A L L"), Margin + 17.f * Scale, Margin + 13.f * Scale,
        Scale, EmberHUDColors::Gold, 1.25f, true);
    Label(TEXT("DEVELOPMENT BUILD / SHARED WORLD"), Margin + 17.f * Scale,
        Margin + 47.f * Scale, Scale, EmberHUDColors::Muted, 0.82f);
    Meter(Margin + 17.f * Scale, Margin + 78.f * Scale, LeftWidth - 34.f * Scale,
        Player->GetHealthFraction(), Scale, EmberHUDColors::Health, TEXT("VITALITY"));
    Meter(Margin + 17.f * Scale, Margin + 120.f * Scale, LeftWidth - 34.f * Scale,
        Player->GetStaminaFraction(), Scale, EmberHUDColors::Stamina, TEXT("STAMINA"));

    const float ObjectiveY = Margin + 190.f * Scale;
    Panel(Margin, ObjectiveY, LeftWidth, 120.f * Scale, Scale);
    Label(TEXT("THE FIRST EMBER"), Margin + 17.f * Scale, ObjectiveY + 13.f * Scale,
        Scale, EmberHUDColors::Gold);
    const FString Objective = !Player->IsProgressReady()
        ? TEXT("Sign in through the realm client. Open the console and use Login with your one-use join ticket.")
        : bInDungeon
        ? TEXT("Drowned Archive: defeat Keeper Serael with your party. Return to the entrance circle [E] to leave.")
        : !Player->HasChapterStarted()
        ? TEXT("E at the Wayfire beacon: discover why the valley has fallen silent.")
        : (!Boss || !Boss->IsDefeated()
            ? TEXT("Defeat the Cinderwarden. Read its attacks and preserve your stamina.")
            : TEXT("E at the forge to upgrade. The world boss returns in 30 seconds; E near it replays sooner."));
    WrappedLabel(Objective,
        Margin + 17.f * Scale, ObjectiveY + 39.f * Scale, LeftWidth - 34.f * Scale,
        Scale, EmberHUDColors::Ivory, 3);

    const float RightX = ViewWidth - Margin - RightWidth;
    Panel(RightX, Margin, RightWidth, 215.f * Scale, Scale);
    Label(TEXT("EARNED, NEVER BOUGHT"), RightX + 17.f * Scale, Margin + 15.f * Scale,
        Scale, EmberHUDColors::Gold);
    WrappedLabel(Player->GetGearName(), RightX + 17.f * Scale, Margin + 46.f * Scale,
        RightWidth - 34.f * Scale, Scale, EmberHUDColors::Ivory, 2);
    Label(FString::Printf(TEXT("Ember sigils    %d"), Player->GetSigils()),
        RightX + 17.f * Scale, Margin + 98.f * Scale, Scale, EmberHUDColors::Muted);
    Label(FString::Printf(TEXT("Boss victories  %d"), Player->GetVictories()),
        RightX + 17.f * Scale, Margin + 124.f * Scale, Scale, EmberHUDColors::Muted);
    Label(FString::Printf(TEXT("Archive clears  %d"), Player->GetDungeonClears()),
        RightX + 17.f * Scale, Margin + 150.f * Scale, Scale, EmberHUDColors::Muted);
    Label(FString::Printf(TEXT("Emberstone      %d"), Player->GetEmberstone()),
        RightX + 17.f * Scale, Margin + 176.f * Scale, Scale, EmberHUDColors::Muted);

    const FString ZoneStatus = bInDungeon ? TEXT("DROWNED ARCHIVE / PARTY PVE") : Player->IsInsidePvpZone()
        ? (Player->IsPvpOptedIn() ? TEXT("PVP ZONE / CONSENT ON")
                                : TEXT("PVP ZONE / CONSENT OFF"))
        : TEXT("COOPERATIVE PVE / NO PLAYER COMBAT");
    Label(ZoneStatus, RightX + 5.f * Scale, Margin + 229.f * Scale,
        Scale, Player->IsInsidePvpZone() ? EmberHUDColors::Gold : EmberHUDColors::Stamina, 0.78f);
    Label(TEXT("Replicated foundation / not a production MMO"), RightX + 5.f * Scale,
        Margin + 249.f * Scale, Scale, EmberHUDColors::Muted, 0.72f);
    if (!bInDungeon && State && State->GetWorldEvent() && State->GetWorldEvent()->GetSecondsUntilSpawn() > 0.f)
        Label(FString::Printf(TEXT("World boss returns in %.0fs"), State->GetWorldEvent()->GetSecondsUntilSpawn()),
            Margin + 17.f * Scale, ObjectiveY + 135.f * Scale, Scale, EmberHUDColors::Gold);
    Label(TEXT("Archive portal: south | Emberstone: marked seams"), Margin + 17.f * Scale,
        ObjectiveY + 160.f * Scale, Scale, EmberHUDColors::Muted, .78f);

    if (Boss && Boss->IsEngaged() && !Boss->IsDefeated())
    {
        const float BossWidth = 524.f * Scale;
        const float BossX = (ViewWidth - BossWidth) * 0.5f;
        Panel(BossX, Margin, BossWidth, 116.f * Scale, Scale);
        Label(FString::Printf(TEXT("%s  /  PHASE %d"), bInDungeon ? TEXT("KEEPER SERAEL") : TEXT("THE CINDERWARDEN"), Boss->GetPhase()),
            BossX + 18.f * Scale, Margin + 14.f * Scale, Scale, EmberHUDColors::Ivory);
        DrawRect(FLinearColor(0.12f, 0.10f, 0.09f, 1.f), BossX + 18.f * Scale,
            Margin + 43.f * Scale, BossWidth - 36.f * Scale, 10.f * Scale);
        DrawRect(EmberHUDColors::Health, BossX + 18.f * Scale, Margin + 43.f * Scale,
            (BossWidth - 36.f * Scale) * FMath::Clamp(Boss->GetHealthFraction(), 0.f, 1.f),
            10.f * Scale);
        WrappedLabel(Boss->GetAttackLabel(), BossX + 18.f * Scale, Margin + 66.f * Scale,
            BossWidth - 36.f * Scale, Scale, EmberHUDColors::Gold, 2);
    }

    const float SkillWidth = 132.f * Scale;
    const float SkillGap = 10.f * Scale;
    const float SkillsX = (ViewWidth - (4.f * SkillWidth + 3.f * SkillGap)) * 0.5f;
    const float SkillsY = ViewHeight - 130.f * Scale;
    const TCHAR* Keys[] = { TEXT("LMB"), TEXT("Q"), TEXT("R"), TEXT("SPACE") };
    const TCHAR* Names[] = { TEXT("STRIKE"), TEXT("HEAVY"), TEXT("MEND"), TEXT("DODGE") };
    for (int32 Index = 0; Index < 4; ++Index)
    {
        const float X = SkillsX + Index * (SkillWidth + SkillGap);
        const float Cooldown = FMath::Max(0.f, Player->GetAbilityCooldown(Index));
        Panel(X, SkillsY, SkillWidth, 73.f * Scale, Scale);
        Label(Keys[Index], X + 12.f * Scale, SkillsY + 9.f * Scale,
            Scale, EmberHUDColors::Gold);
        Label(Names[Index], X + 12.f * Scale, SkillsY + 30.f * Scale,
            Scale, EmberHUDColors::Ivory, 0.88f);
        Label(Cooldown > 0.f ? FString::Printf(TEXT("%.1fs"), Cooldown) : TEXT("READY"),
            X + 12.f * Scale, SkillsY + 51.f * Scale, Scale,
            Cooldown > 0.f ? EmberHUDColors::Muted : EmberHUDColors::Stamina, 0.80f);
    }

    const FString Notification = Player->GetNotification();
    if (!Notification.IsEmpty())
    {
        const float NoticeWidth = 760.f * Scale;
        const float NoticeX = (ViewWidth - NoticeWidth) * 0.5f;
        const float NoticeY = SkillsY - 70.f * Scale;
        Panel(NoticeX, NoticeY, NoticeWidth, 57.f * Scale, Scale);
        WrappedLabel(Notification, NoticeX + 16.f * Scale, NoticeY + 11.f * Scale,
            NoticeWidth - 32.f * Scale, Scale, EmberHUDColors::Ivory, 2);
    }

    Label(TEXT("WASD move | Mouse look | LMB strike | RMB guard | Q heavy | Space dodge | R mend | E interact | P PvP | Esc mouse"),
        Margin, ViewHeight - 28.f * Scale, Scale, EmberHUDColors::Muted, 0.83f);

    if (!Player->IsAlive())
    {
        const float DefeatWidth = 500.f * Scale;
        const float DefeatX = (ViewWidth - DefeatWidth) * 0.5f;
        const float DefeatY = ViewHeight * 0.45f;
        Panel(DefeatX, DefeatY, DefeatWidth, 85.f * Scale, Scale);
        Label(TEXT("THE EMBER ENDURES"), DefeatX + 24.f * Scale,
            DefeatY + 15.f * Scale, Scale, EmberHUDColors::Gold, 1.15f, true);
        Label(TEXT("Returning to the Wayfire in 5 seconds. Gear is safe."), DefeatX + 24.f * Scale,
            DefeatY + 52.f * Scale, Scale, EmberHUDColors::Ivory);
    }
}
