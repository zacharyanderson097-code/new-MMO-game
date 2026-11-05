#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EmberWorldScene.generated.h"

class UStaticMesh;
class UMaterialInterface;

/** Replicated scene marker; each peer builds the same local primitive scenery and collision. */
UCLASS()
class EMBERFALL_API AEmberWorldScene : public AActor
{
    GENERATED_BODY()
public:
    AEmberWorldScene();
    virtual void BeginPlay() override;
private:
    void BuildWorld();
    AActor* MakeShape(UStaticMesh* Mesh, const FVector& Location, const FVector& Scale,
        const FLinearColor& Color, bool bCollision = true,
        const FRotator& Rotation = FRotator::ZeroRotator);
    void MakeLabel(const FString& Text, const FVector& Location, const FColor& Color, float Size = 75.f);
    UPROPERTY()
    TObjectPtr<UStaticMesh> CubeMesh;
    UPROPERTY()
    TObjectPtr<UStaticMesh> CylinderMesh;
    UPROPERTY()
    TObjectPtr<UStaticMesh> SphereMesh;
    UPROPERTY()
    TObjectPtr<UStaticMesh> ConeMesh;
    UPROPERTY()
    TObjectPtr<UMaterialInterface> ShapeMaterial;
};
