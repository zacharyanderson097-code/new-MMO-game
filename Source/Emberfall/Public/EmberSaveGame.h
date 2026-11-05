#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "EmberSaveGame.generated.h"

// Convenience for standalone iteration only. Networked sessions never trust this file.
UCLASS()
class EMBERFALL_API UEmberSaveGame : public USaveGame
{
    GENERATED_BODY()
public:
    UPROPERTY(SaveGame) int32 Version = 1;
    UPROPERTY(SaveGame) int32 GearTier = 0;
    UPROPERTY(SaveGame) int32 Sigils = 0;
    UPROPERTY(SaveGame) int32 Victories = 0;
    UPROPERTY(SaveGame) int32 DungeonClears = 0;
    UPROPERTY(SaveGame) int32 Emberstone = 0;
    UPROPERTY(SaveGame) bool bChapterStarted = false;
};
