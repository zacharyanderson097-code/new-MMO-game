#include "EmberWorldScene.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "Engine/SkyAtmosphere.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextRenderActor.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

AEmberWorldScene::AEmberWorldScene()
{
    bReplicates = true;
    bAlwaysRelevant = true;
    SetReplicateMovement(false);
    PrimaryActorTick.bCanEverTick = false;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(
        TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    CubeMesh = Cube.Object;
    CylinderMesh = Cylinder.Object;
    SphereMesh = Sphere.Object;
    ConeMesh = Cone.Object;
    ShapeMaterial = Material.Object;
}

void AEmberWorldScene::BeginPlay()
{
    Super::BeginPlay();
    BuildWorld();
}

AActor* AEmberWorldScene::MakeShape(UStaticMesh* Mesh, const FVector& Location,
    const FVector& Scale, const FLinearColor& Color, bool bCollision, const FRotator& Rotation)
{
    if (!Mesh || !GetWorld())
    {
        return nullptr;
    }
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AStaticMeshActor* Actor = GetWorld()->SpawnActor<AStaticMeshActor>(
        AStaticMeshActor::StaticClass(), Location, Rotation, Params);
    if (!Actor)
    {
        return nullptr;
    }
    UStaticMeshComponent* Component = Actor->GetStaticMeshComponent();
    Component->SetMobility(EComponentMobility::Movable);
    Component->SetStaticMesh(Mesh);
    Component->SetCollisionEnabled(bCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
    Component->SetCollisionResponseToAllChannels(ECR_Block);
    Actor->SetActorScale3D(Scale);
    if (ShapeMaterial)
    {
        UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(ShapeMaterial, Component);
        Material->SetVectorParameterValue(TEXT("Color"), Color);
        Component->SetMaterial(0, Material);
    }
    return Actor;
}

void AEmberWorldScene::MakeLabel(const FString& Text, const FVector& Location,
    const FColor& Color, float Size)
{
    ATextRenderActor* Label = GetWorld()->SpawnActor<ATextRenderActor>(
        ATextRenderActor::StaticClass(), Location, FRotator(0.f, 180.f, 0.f));
    if (Label)
    {
        UTextRenderComponent* Render = Label->GetTextRender();
        Render->SetText(FText::FromString(Text));
        Render->SetTextRenderColor(Color);
        Render->SetHorizontalAlignment(EHTA_Center);
        Render->SetWorldSize(Size);
        Render->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
}

void AEmberWorldScene::BuildWorld()
{
    // Only engine primitives are used: no marketplace assets, templates, or binary maps required.
    MakeShape(CubeMesh, FVector(2000.f, 500.f, -65.f), FVector(100.f, 100.f, 1.f),
        FLinearColor(.11f, .15f, .12f));

    ADirectionalLight* Sun = GetWorld()->SpawnActor<ADirectionalLight>(
        ADirectionalLight::StaticClass(), FVector(0.f, 0.f, 2000.f), FRotator(-45.f, -35.f, 0.f));
    if (Sun)
    {
        Sun->GetLightComponent()->SetIntensity(5.f);
        Sun->GetLightComponent()->SetLightColor(FLinearColor(1.f, .78f, .52f));
    }
    GetWorld()->SpawnActor<ASkyAtmosphere>();
    ASkyLight* Sky = GetWorld()->SpawnActor<ASkyLight>();
    if (Sky)
    {
        Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
        Sky->GetLightComponent()->SetIntensity(1.f);
        Sky->GetLightComponent()->SetLightColor(FLinearColor(.35f, .5f, .7f));
    }
    AExponentialHeightFog* Fog = GetWorld()->SpawnActor<AExponentialHeightFog>();
    if (Fog)
    {
        Fog->GetComponent()->SetFogDensity(.012f);
        Fog->GetComponent()->SetFogInscatteringColor(FLinearColor(.12f, .19f, .24f));
        Fog->GetComponent()->SetStartDistance(1800.f);
    }

    const FLinearColor Stone(.21f, .23f, .24f);
    const FLinearColor Gold(.65f, .32f, .06f);
    const FLinearColor Warm(.95f, .35f, .04f);
    // Sanctuary forge and the first story beacon.
    MakeShape(CylinderMesh, FVector(0.f, 0.f, -4.f), FVector(7.f, 7.f, .2f), Stone);
    MakeShape(CubeMesh, FVector(-180.f, -180.f, 45.f), FVector(1.6f, .85f, .8f), Stone);
    MakeShape(CubeMesh, FVector(-180.f, -180.f, 95.f), FVector(2.f, 1.1f, .2f), Gold);
    MakeLabel(TEXT("DAWN'S REST\n[E] Forge earned gear"), FVector(-250.f, 0.f, 300.f), FColor(240, 196, 112), 58.f);

    MakeShape(CylinderMesh, FVector(700.f, 0.f, 30.f), FVector(1.7f, 1.7f, .6f), Stone);
    MakeShape(CylinderMesh, FVector(700.f, 0.f, 125.f), FVector(.6f, .6f, 1.4f), Gold);
    MakeShape(SphereMesh, FVector(700.f, 0.f, 220.f), FVector(.8f),
        FLinearColor(.95f, .35f, .04f), false);
    MakeLabel(TEXT("THE LAST BEACON\n[E] Begin the chapter"), FVector(700.f, 0.f, 380.f), FColor(255, 211, 136), 54.f);

    // The party portal stays in the shared world; the manager creates private rooms.
    MakeShape(CylinderMesh, FVector(0.f, -1500.f, -5.f), FVector(6.f, 6.f, .15f), Stone, false);
    MakeShape(CubeMesh, FVector(0.f, -1770.f, 170.f), FVector(.7f, .7f, 3.5f), Gold);
    MakeShape(CubeMesh, FVector(0.f, -1230.f, 170.f), FVector(.7f, .7f, 3.5f), Gold);
    MakeLabel(TEXT("THE DROWNED ARCHIVE\n[E] Party leader: enter dungeon\nGather all party members at this circle"),
        FVector(0.f, -1500.f, 450.f), FColor(137, 214, 232), 55.f);
    for (const FVector& Node : {FVector(-500.f, 1000.f, 30.f), FVector(800.f, -900.f, 30.f)})
    {
        MakeShape(SphereMesh, Node, FVector(1.2f, 1.2f, .8f), FLinearColor(.15f, .55f, .65f));
        MakeLabel(TEXT("EMBERSTONE SEAM\n[E] Gather"), Node + FVector(0.f, 0.f, 180.f), FColor(137, 214, 232), 45.f);
    }

    // A ring of flat markers remains visible in packaged builds (not debug-only geometry).
    for (int32 Index = 0; Index < 48; ++Index)
    {
        const float Angle = 2.f * PI * Index / 48.f;
        const FVector Offset(FMath::Cos(Angle), FMath::Sin(Angle), 0.f);
        MakeShape(CubeMesh, FVector(2500.f, 0.f, 0.f) + Offset * 1000.f,
            FVector(.6f, .6f, .15f), Warm, false);
        MakeShape(CubeMesh, FVector(0.f, 2500.f, 0.f) + Offset * 650.f,
            FVector(.5f, .5f, .15f), FLinearColor(.58f, .08f, .08f), false);
    }
    MakeLabel(TEXT("CINDERWARDEN\nCooperative PvE encounter"), FVector(2800.f, 0.f, 620.f), FColor(255, 174, 90));
    MakeLabel(TEXT("THE RED MARCH\nOpt-in PvP training boundary\nSeparate from the cooperative boss arena"),
        FVector(0.f, 2800.f, 400.f), FColor(236, 112, 112), 58.f);

    // Broken columns frame the boss without blocking the arena's readable movement space.
    for (int32 Index = 0; Index < 10; ++Index)
    {
        const float Angle = 2.f * PI * Index / 10.f;
        const FVector Place = FVector(2500.f, 0.f, 190.f) + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * 1300.f;
        MakeShape(CylinderMesh, Place, FVector(1.2f, 1.2f, 4.f + (Index % 3)), Stone);
    }
    // Deterministic silhouettes around the playable area's outer edges.
    for (int32 Index = 0; Index < 24; ++Index)
    {
        const float X = -2400.f + Index * 370.f;
        const float Y = (Index % 2 == 0) ? -2300.f : 4400.f;
        const float Height = 650.f + (Index % 5) * 180.f;
        MakeShape(ConeMesh, FVector(X, Y, Height * .5f - 10.f),
            FVector(10.f, 10.f, Height / 100.f), FLinearColor(.08f, .12f, .14f));
    }
}
