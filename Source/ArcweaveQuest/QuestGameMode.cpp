#include "QuestGameMode.h"

#include "QuestCharacter.h"
#include "QuestDirector.h"
#include "QuestHUD.h"
#include "QuestWorldActor.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/GameInstance.h"
#include "Engine/PointLight.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

AQuestGameMode::AQuestGameMode()
{
    DefaultPawnClass = AQuestCharacter::StaticClass();
    HUDClass = AQuestHUD::StaticClass();
}

void AQuestGameMode::BeginPlay()
{
    Super::BeginPlay();
    BuildStation();
    Director = GetGameInstance()->GetSubsystem<UQuestDirector>();
    QuestChangedHandle = Director->OnQuestChanged.AddUObject(this, &AQuestGameMode::RefreshStation);
    FString Error;
    Director->StartNewGame(Error);
    RefreshStation();
}

void AQuestGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (Director)
    {
        Director->OnQuestChanged.Remove(QuestChangedHandle);
    }
    Super::EndPlay(EndPlayReason);
}

void AQuestGameMode::AddBlock(FVector Location, FVector Dimensions, FLinearColor Color, FRotator Rotation)
{
    AActor* Part = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), Location, Rotation);
    UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Part);
    Part->SetRootComponent(Mesh);
    Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
    Mesh->SetWorldLocationAndRotation(Location, Rotation);
    Mesh->SetWorldScale3D(Dimensions / 100.0f);
    Mesh->SetCollisionProfileName(TEXT("BlockAll"));
    UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Material, Part);
    Instance->SetVectorParameterValue(TEXT("Color"), Color);
    Instance->SetScalarParameterValue(TEXT("Roughness"), 0.7f);
    Mesh->SetMaterial(0, Instance);
    Mesh->RegisterComponent();
}

void AQuestGameMode::AddSign(const FString& Value, FVector Location, float Size, FColor Color)
{
    AActor* Sign = GetWorld()->SpawnActor<AActor>();
    UTextRenderComponent* Text = NewObject<UTextRenderComponent>(Sign);
    Sign->SetRootComponent(Text);
    Text->SetWorldLocationAndRotation(Location, FRotator(0, 180, 0));
    Text->SetHorizontalAlignment(EHTA_Center);
    Text->SetVerticalAlignment(EVRTA_TextCenter);
    Text->SetWorldSize(Size);
    Text->SetTextRenderColor(Color);
    Text->SetText(FText::FromString(Value));
    Text->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Text->RegisterComponent();
}

void AQuestGameMode::BuildStation()
{
    const FLinearColor Wall(0.075f, 0.105f, 0.145f);
    const FLinearColor Floor(0.13f, 0.17f, 0.21f);
    const FLinearColor Rib(0.20f, 0.25f, 0.30f);
    const FLinearColor Cyan(0.035f, 0.62f, 0.76f);
    const FLinearColor Amber(0.95f, 0.34f, 0.06f);

    AddBlock(FVector(1200, 0, -30), FVector(3400, 2380, 60), Wall);
    for (int32 X = 0; X < 9; ++X)
    {
        for (int32 Y = 0; Y < 6; ++Y)
        {
            AddBlock(FVector(-280 + X * 380, -975 + Y * 390, 2), FVector(375, 385, 4), Floor);
        }
    }
    for (float Side : {-1.0f, 1.0f})
    {
        AddBlock(FVector(1120, Side * 1170, 225), FVector(3360, 60, 450), Wall);
        AddBlock(FVector(1020, Side * 265, 5), FVector(2300, 7, 4), Cyan);
        AddBlock(FVector(2220, Side * 780, 225), FVector(70, 865, 450), Wall);
        AddBlock(FVector(2690, Side * 265, 5), FVector(280, 7, 4), Cyan);
        for (int32 Index = 0; Index < 6; ++Index)
        {
            const float X = -360 + Index * 560;
            AddBlock(FVector(X, Side * 1120, 230), FVector(62, 110, 460), Rib);
            AddBlock(FVector(X - 34, Side * 1110, 290), FVector(6, 88, 190), Cyan);
        }
    }
    AddBlock(FVector(-490, 0, 225), FVector(60, 2380, 450), Wall);
    AddBlock(FVector(2860, 0, 225), FVector(60, 2380, 450), Wall);
    for (float X : {200.0f, 1080.0f, 1950.0f})
    {
        AddBlock(FVector(X, 0, 495), FVector(55, 2320, 90), Rib);
        AddBlock(FVector(X - 29, 0, 490), FVector(4, 2050, 12), Cyan);
    }

    // Each device occupies a distinct bay around the central route to the gate.
    const auto AddDevice = [this](EQuestStationObject Kind, FVector Location, FName CellId = NAME_None)
    {
        AQuestWorldActor* Device = GetWorld()->SpawnActor<AQuestWorldActor>(Location, FRotator::ZeroRotator);
        Device->Configure(Kind, CellId);
        StationObjects.Add(Device);
    };
    AddDevice(EQuestStationObject::Terminal, FVector(350, -230, 4));
    AddDevice(EQuestStationObject::PowerCell, FVector(620, -735, 4), TEXT("cell_a"));
    AddDevice(EQuestStationObject::PowerCell, FVector(1630, 735, 4), TEXT("cell_b"));
    AddDevice(EQuestStationObject::Generator, FVector(1260, 0, 4));
    AddDevice(EQuestStationObject::Gate, FVector(2220, 0, 4));

    for (const FVector Bay : {FVector(620, -735, 5), FVector(1630, 735, 5)})
    {
        AddBlock(Bay + FVector(0, 0, 4), FVector(230, 230, 8), Wall);
        AddBlock(Bay + FVector(-122, 0, 4), FVector(7, 254, 8), Cyan);
        AddBlock(Bay + FVector(122, 0, 4), FVector(7, 254, 8), Cyan);
    }
    AddBlock(FVector(1260, 0, 7), FVector(385, 360, 10), Wall);
    for (float Y : {-185.0f, 185.0f})
    {
        AddBlock(FVector(1260, Y, 9), FVector(400, 9, 8), Amber);
    }
    // Utility trunks and storage crates give the primitive geometry an industrial rhythm.
    for (int32 Index = 0; Index < 3; ++Index)
    {
        const FVector Crate(940 + Index * 165, -945, 68);
        AddBlock(Crate, FVector(145, 145, 136), Rib);
        AddBlock(Crate + FVector(-74, 0, 0), FVector(5, 105, 92), Wall);
        AddBlock(Crate + FVector(-78, 0, 0), FVector(5, 14, 105), Amber);
    }
    AddBlock(FVector(1850, -920, 58), FVector(320, 165, 116), Wall);
    AddBlock(FVector(360, 950, 72), FVector(410, 160, 144), Rib);
    for (int32 Index = 0; Index < 6; ++Index)
    {
        AddBlock(FVector(190 + Index * 65, 865, 77), FVector(30, 5, 90), Wall);
    }

    AddSign(TEXT("RELAY / 07"), FVector(2175, -755, 324), 58, FColor(175, 205, 218));
    AddSign(TEXT("POWER DISTRIBUTION"), FVector(2173, -755, 250), 21, FColor(93, 153, 174));
    AddSign(TEXT("01"), FVector(2175, 785, 295), 124, FColor(65, 96, 115));
    AddSign(TEXT("AUTHORIZED EXIT"), FVector(2818, 0, 262), 28, FColor(91, 210, 225));

    ADirectionalLight* Sun = GetWorld()->SpawnActor<ADirectionalLight>(FVector(0, 0, 1000), FRotator(-50, -28, 0));
    Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable);
    Sun->GetLightComponent()->SetIntensity(3.0f);
    Sun->GetLightComponent()->SetLightColor(FLinearColor(0.79f, 0.86f, 1.0f));
    ASkyLight* Sky = GetWorld()->SpawnActor<ASkyLight>();
    Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
    Sky->GetLightComponent()->SetIntensity(0.65f);
    Sky->GetLightComponent()->SetLightColor(FLinearColor(0.35f, 0.48f, 0.65f));
    // Broad fixtures keep the station readable even before its local power is restored.
    for (float X : {200.0f, 1030.0f, 1850.0f, 2600.0f})
    {
        for (float Y : {-690.0f, 690.0f})
        {
            APointLight* Fixture = GetWorld()->SpawnActor<APointLight>(FVector(X, Y, 382), FRotator::ZeroRotator);
            UPointLightComponent* Light = Cast<UPointLightComponent>(Fixture->GetLightComponent());
            Light->SetMobility(EComponentMobility::Movable);
            Light->SetIntensity(18000.0f);
            Light->SetAttenuationRadius(890.0f);
            Light->SetCastShadows(false);
            StationLights.Add(Light);
            AddBlock(FVector(X, Y, 421), FVector(130, 45, 14), Rib);
            AddBlock(FVector(X, Y, 411), FVector(112, 28, 5), FLinearColor(0.85f, 0.9f, 1.0f));
        }
    }
}

void AQuestGameMode::RefreshStation()
{
    const bool bGateOpen = Director->IsGateOpen();
    for (AQuestWorldActor* Object : StationObjects)
    {
        Object->ApplyQuestState(*Director, bGateOpen);
    }
    for (UPointLightComponent* Light : StationLights)
    {
        Light->SetLightColor(Director->IsPowerRestored()
            ? FLinearColor(0.50f, 0.83f, 1.0f) : FLinearColor(1.0f, 0.76f, 0.49f));
        Light->SetIntensity(Director->IsPowerRestored() ? 24000.0f : 18000.0f);
    }
}
