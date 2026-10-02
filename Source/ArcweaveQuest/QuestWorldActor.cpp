#include "QuestWorldActor.h"

#include "QuestDirector.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
const FLinearColor Steel(0.085f, 0.12f, 0.17f);
const FLinearColor Edge(0.22f, 0.29f, 0.35f);
const FLinearColor Cyan(0.025f, 0.7f, 0.9f);
const FLinearColor Amber(1.0f, 0.35f, 0.055f);
}

AQuestWorldActor::AQuestWorldActor()
{
    PrimaryActorTick.bCanEverTick = true;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
    VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Visuals"));
    VisualRoot->SetupAttachment(RootComponent);
    Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
    Label->SetupAttachment(RootComponent);
    Label->SetHorizontalAlignment(EHTA_Center);
    Label->SetVerticalAlignment(EVRTA_TextCenter);
    Label->SetWorldSize(23.0f);
    Label->SetTextRenderColor(FColor(155, 224, 240));
    Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    StatusLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("StatusLight"));
    StatusLight->SetupAttachment(RootComponent);
    StatusLight->SetIntensity(4500.0f);
    StatusLight->SetAttenuationRadius(340.0f);
    StatusLight->SetCastShadows(false);
}

UStaticMeshComponent* AQuestWorldActor::AddPart(const TCHAR* Shape, FVector Location,
    FVector Scale, FLinearColor Color, bool bAccent, USceneComponent* Parent)
{
    UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
    Part->SetupAttachment(Parent ? Parent : VisualRoot.Get());
    const FString MeshPath = FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Shape, Shape);
    Part->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, *MeshPath));
    Part->SetRelativeLocation(Location);
    Part->SetRelativeScale3D(Scale);
    Part->SetCollisionProfileName(TEXT("BlockAll"));
    UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Material, this);
    Instance->SetVectorParameterValue(TEXT("Color"), Color);
    Instance->SetScalarParameterValue(TEXT("Roughness"), 0.58f);
    Part->SetMaterial(0, Instance);
    if (bAccent)
    {
        AccentMaterials.Add(Instance);
    }
    Part->RegisterComponent();
    return Part;
}

void AQuestWorldActor::Configure(EQuestStationObject InKind, FName InCellId)
{
    Kind = InKind;
    CellId = InCellId;
    switch (Kind)
    {
    case EQuestStationObject::Terminal:
        AddPart(TEXT("Cube"), FVector(0, 0, 12), FVector(1.35f, 1.4f, 0.24f), Edge);
        AddPart(TEXT("Cube"), FVector(12, 0, 68), FVector(0.72f, 0.85f, 1.1f), Steel);
        AddPart(TEXT("Cube"), FVector(0, 0, 131), FVector(0.4f, 1.28f, 0.94f), Edge);
        AddPart(TEXT("Cube"), FVector(-22, 0, 135), FVector(0.055f, 1.02f, 0.65f), Cyan, true);
        AddPart(TEXT("Cube"), FVector(-34, 0, 96), FVector(0.42f, 1.1f, 0.08f), Steel);
        for (int32 Index = 0; Index < 4; ++Index)
        {
            AddPart(TEXT("Cube"), FVector(-26, -32 + Index * 21, 150), FVector(0.035f, 0.12f, 0.035f), Edge);
        }
        Label->SetRelativeLocation(FVector(0, 0, 207));
        StatusLight->SetRelativeLocation(FVector(-65, 0, 155));
        break;
    case EQuestStationObject::PowerCell:
        AddPart(TEXT("Cube"), FVector(0, 0, 15), FVector(1.15f, 1.15f, 0.3f), Steel, false, RootComponent);
        AddPart(TEXT("Cube"), FVector(0, 0, 42), FVector(0.72f, 0.72f, 0.24f), Edge, false, RootComponent);
        AddPart(TEXT("Cylinder"), FVector(0, 0, 104), FVector(0.46f, 0.46f, 0.9f), Cyan, true);
        AddPart(TEXT("Cylinder"), FVector(0, 0, 61), FVector(0.58f, 0.58f, 0.15f), Edge);
        AddPart(TEXT("Cylinder"), FVector(0, 0, 147), FVector(0.58f, 0.58f, 0.15f), Edge);
        AddPart(TEXT("Cylinder"), FVector(0, 0, 157), FVector(0.25f, 0.25f, 0.07f), Cyan, true);
        Label->SetRelativeLocation(FVector(0, 0, 211));
        StatusLight->SetRelativeLocation(FVector(0, 0, 170));
        break;
    case EQuestStationObject::Generator:
        AddPart(TEXT("Cube"), FVector(0, 0, 14), FVector(2.75f, 2.55f, 0.28f), Edge);
        AddPart(TEXT("Cylinder"), FVector(0, 0, 132), FVector(1.9f, 1.9f, 2.3f), Steel);
        for (int32 Index = 0; Index < 5; ++Index)
        {
            AddPart(TEXT("Cylinder"), FVector(0, 0, 53 + Index * 42), FVector(2.02f, 2.02f, 0.085f), Amber, true);
        }
        AddPart(TEXT("Cylinder"), FVector(0, 0, 260), FVector(1.36f, 1.36f, 0.3f), Edge);
        AddPart(TEXT("Cube"), FVector(-111, 0, 117), FVector(0.38f, 1.45f, 0.84f), Edge);
        AddPart(TEXT("Cube"), FVector(-133, -38, 120), FVector(0.06f, 0.36f, 0.6f), Amber, true);
        AddPart(TEXT("Cube"), FVector(-133, 38, 120), FVector(0.06f, 0.36f, 0.6f), Amber, true);
        Label->SetRelativeLocation(FVector(0, 0, 317));
        StatusLight->SetRelativeLocation(FVector(0, 0, 303));
        StatusLight->SetAttenuationRadius(580.0f);
        break;
    case EQuestStationObject::Gate:
        AddPart(TEXT("Cube"), FVector(0, -325, 213), FVector(1.05f, 0.75f, 4.26f), Edge, false, RootComponent);
        AddPart(TEXT("Cube"), FVector(0, 325, 213), FVector(1.05f, 0.75f, 4.26f), Edge, false, RootComponent);
        AddPart(TEXT("Cube"), FVector(0, 0, 430), FVector(1.25f, 7.25f, 0.54f), Steel, false, RootComponent);
        AddPart(TEXT("Cube"), FVector(0, 0, 670), FVector(1.3f, 7.25f, 4.25f), Steel, false, RootComponent);
        AddPart(TEXT("Cube"), FVector(0, 0, 196), FVector(0.48f, 5.76f, 3.92f), Steel);
        AddPart(TEXT("Cube"), FVector(-27, 0, 196), FVector(0.07f, 0.055f, 3.8f), Edge);
        for (float Side : {-1.0f, 1.0f})
        {
            AddPart(TEXT("Cube"), FVector(-28, Side * 236, 196), FVector(0.07f, 0.065f, 3.3f), Amber, true);
            AddPart(TEXT("Cube"), FVector(-56, Side * 325, 260), FVector(0.08f, 0.16f, 2.35f), Amber, true, RootComponent);
        }
        Label->SetRelativeLocation(FVector(-70, 0, 486));
        Label->SetWorldSize(30.0f);
        StatusLight->SetRelativeLocation(FVector(-140, 0, 385));
        StatusLight->SetAttenuationRadius(680.0f);
        break;
    }
}

void AQuestWorldActor::ApplyQuestState(const UQuestDirector& Director, bool bAnimate)
{
    const bool bCollected = Kind == EQuestStationObject::PowerCell && Director.HasCollectedCell(CellId);
    SetActorHiddenInGame(bCollected);
    SetActorEnableCollision(!bCollected);
    const bool bPowered = Director.IsPowerRestored();
    const FLinearColor Color = (Kind == EQuestStationObject::Generator || Kind == EQuestStationObject::Gate)
        ? (bPowered ? Cyan : Amber) : Cyan;
    for (UMaterialInstanceDynamic* Material : AccentMaterials)
    {
        Material->SetVectorParameterValue(TEXT("Color"), Color);
    }
    StatusLight->SetLightColor(Color);
    StatusLight->SetVisibility(!bCollected);
    if (Kind == EQuestStationObject::Terminal)
    {
        Label->SetText(FText::FromString(Director.GetUIText(TEXT("world_text.terminal_label"))));
    }
    if (Kind == EQuestStationObject::PowerCell)
    {
        Label->SetText(FText::FromString(Director.GetUIText(
            CellId == TEXT("cell_a") ? TEXT("world_text.cell_a_label") : TEXT("world_text.cell_b_label"))));
    }
    if (Kind == EQuestStationObject::Generator)
    {
        Label->SetText(FText::FromString(Director.GetUIText(TEXT("quest_ui.generator_label"))));
    }
    if (Kind == EQuestStationObject::Gate)
    {
        GateTarget = Director.IsGateOpen() ? 1.0f : 0.0f;
        if (!bAnimate)
        {
            GateOpenAmount = GateTarget;
            VisualRoot->SetRelativeLocation(FVector(0, 0, GateOpenAmount * 440.0f));
        }
        Label->SetText(FText::FromString(Director.GetUIText(TEXT("quest_ui.gate_label"))));
    }
}

FString AQuestWorldActor::GetInteractionText(const UQuestDirector& Director) const
{
    switch (Kind)
    {
    case EQuestStationObject::Terminal:
        return Director.GetUIText(TEXT("quest_ui.terminal_prompt"));
    case EQuestStationObject::PowerCell:
        return Director.GetUIText(TEXT("quest_ui.cell_prompt"));
    case EQuestStationObject::Generator:
        return Director.GetUIText(TEXT("quest_ui.generator_prompt"));
    default:
        return FString();
    }
}

void AQuestWorldActor::Interact(UQuestDirector& Director)
{
    FString Error;
    switch (Kind)
    {
    case EQuestStationObject::Terminal:
        Director.StartQuest(Error);
        break;
    case EQuestStationObject::PowerCell:
        Director.CollectCell(CellId, Error);
        break;
    case EQuestStationObject::Generator:
        Director.TryRestorePower(Error);
        break;
    default:
        break;
    }
}

void AQuestWorldActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    AnimationTime += DeltaSeconds;
    if (Kind == EQuestStationObject::Gate)
    {
        GateOpenAmount = FMath::FInterpConstantTo(GateOpenAmount, GateTarget, DeltaSeconds, 0.42f);
        VisualRoot->SetRelativeLocation(FVector(0, 0, GateOpenAmount * 440.0f));
    }
    else if (Kind == EQuestStationObject::PowerCell)
    {
        VisualRoot->SetRelativeLocation(FVector(0, 0, FMath::Sin(AnimationTime * 1.6f) * 5.0f));
        VisualRoot->AddLocalRotation(FRotator(0, DeltaSeconds * 18.0f, 0));
    }
    if (APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0))
    {
        Label->SetWorldRotation((Camera->GetCameraLocation() - Label->GetComponentLocation()).Rotation());
    }
}
