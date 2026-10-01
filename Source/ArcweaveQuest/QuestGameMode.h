#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "QuestGameMode.generated.h"

class AQuestWorldActor;
class UPointLightComponent;
class UQuestDirector;

UCLASS()
class ARCWEAVEQUEST_API AQuestGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    AQuestGameMode();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    void BuildStation();
    void RefreshStation();
    void AddBlock(FVector Location, FVector Dimensions, FLinearColor Color, FRotator Rotation = FRotator::ZeroRotator);
    void AddSign(const FString& Value, FVector Location, float Size, FColor Color);

    UPROPERTY()
    TObjectPtr<UQuestDirector> Director;
    UPROPERTY()
    TArray<TObjectPtr<AQuestWorldActor>> StationObjects;
    UPROPERTY()
    TArray<TObjectPtr<UPointLightComponent>> StationLights;
    FDelegateHandle QuestChangedHandle;
};
