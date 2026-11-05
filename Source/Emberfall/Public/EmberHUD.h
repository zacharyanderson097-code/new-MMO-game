#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "EmberHUD.generated.h"

/** Content-independent HUD for the local playable slice. */
UCLASS()
class EMBERFALL_API AEmberHUD : public AHUD
{
    GENERATED_BODY()

public:
    virtual void DrawHUD() override;

private:
    void Panel(float X, float Y, float Width, float Height, float Scale);
    void Label(const FString& Text, float X, float Y, float Scale,
        FLinearColor Color, float FontScale = 1.f, bool bLarge = false);
    void WrappedLabel(const FString& Text, float X, float Y, float Width,
        float Scale, FLinearColor Color, int32 MaxLines = 3);
    void Meter(float X, float Y, float Width, float Fraction, float Scale,
        FLinearColor Color, const FString& Caption);
};
