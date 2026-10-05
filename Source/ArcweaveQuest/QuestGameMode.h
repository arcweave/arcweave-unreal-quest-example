#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "QuestGameMode.generated.h"

class AQuestWorldActor;
class UBoxComponent;
class UPointLightComponent;
class UPrimitiveComponent;
class UQuestDirector;
class UTextRenderComponent;

UCLASS()
class ARCWEAVEQUEST_API AQuestGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    AQuestGameMode();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    bool SaveCheckpoint(const FString& SlotName, FString& Error);
    bool LoadCheckpoint(const FString& SlotName, FString& Error);

private:
    void BuildStation();
    void RefreshStation();
    void AddBlock(FVector Location, FVector Dimensions, FLinearColor Color, FRotator Rotation = FRotator::ZeroRotator);
    void AddSign(FName TextKey, FVector Location, float Size, FColor Color);

    UFUNCTION()
    void HandleExitBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
        UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

    UPROPERTY()
    TObjectPtr<UQuestDirector> Director;
    UPROPERTY()
    TArray<TObjectPtr<AQuestWorldActor>> StationObjects;
    UPROPERTY()
    TArray<TObjectPtr<UPointLightComponent>> StationLights;
    UPROPERTY()
    TMap<FName, TObjectPtr<UTextRenderComponent>> StationSigns;
    UPROPERTY()
    TObjectPtr<UBoxComponent> ExitTrigger;
    FDelegateHandle QuestChangedHandle;
    bool bApplyingCheckpoint = false;
};
