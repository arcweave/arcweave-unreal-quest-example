#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "QuestWorldActor.generated.h"

class UMaterialInstanceDynamic;
class UPointLightComponent;
class UQuestDirector;
class UStaticMeshComponent;
class UTextRenderComponent;

UENUM()
enum class EQuestStationObject : uint8
{
    Terminal,
    PowerCell,
    Generator,
    Gate
};

UCLASS()
class ARCWEAVEQUEST_API AQuestWorldActor : public AActor
{
    GENERATED_BODY()

public:
    AQuestWorldActor();
    virtual void Tick(float DeltaSeconds) override;
    void Configure(EQuestStationObject InKind, FName InCellId = NAME_None);
    void ApplyQuestState(const UQuestDirector& Director, bool bAnimate);
    FString GetInteractionText(const UQuestDirector& Director) const;
    void Interact(UQuestDirector& Director);

private:
    UStaticMeshComponent* AddPart(const TCHAR* Shape, FVector Location, FVector Scale,
        FLinearColor Color, bool bAccent = false, USceneComponent* Parent = nullptr);

    UPROPERTY()
    TObjectPtr<USceneComponent> VisualRoot;
    UPROPERTY()
    TObjectPtr<UTextRenderComponent> Label;
    UPROPERTY()
    TObjectPtr<UPointLightComponent> StatusLight;
    UPROPERTY()
    TArray<TObjectPtr<UMaterialInstanceDynamic>> AccentMaterials;

    EQuestStationObject Kind = EQuestStationObject::Terminal;
    FName CellId;
    float GateOpenAmount = 0.0f;
    float GateTarget = 0.0f;
    float AnimationTime = 0.0f;
};
