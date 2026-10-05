#pragma once

#include "CoreMinimal.h"
#include "ArcweaveRuntimeState.h"
#include "GameFramework/SaveGame.h"
#include "QuestSaveGame.generated.h"

/** A completed interaction: Arcweave state plus the game's applied world and presentation state. */
UCLASS()
class ARCWEAVEQUEST_API UQuestSaveGame : public USaveGame
{
    GENERATED_BODY()

public:
    // Unreal omits default-valued properties; keep 1 so legacy saves without this field stay version 1.
    UPROPERTY(SaveGame)
    int32 FormatVersion = 1;

    UPROPERTY(SaveGame)
    FArcweaveRuntimeState ArcweaveState;

    UPROPERTY(SaveGame)
    bool bGateOpen = false;

    UPROPERTY(SaveGame)
    FString CurrentElementId;

    UPROPERTY(SaveGame)
    FString PresentationElementId;

    UPROPERTY(SaveGame)
    FString Objective;

    UPROPERTY(SaveGame)
    FString Status;

    UPROPERTY(SaveGame)
    FTransform PlayerTransform = FTransform::Identity;

    UPROPERTY(SaveGame)
    FRotator ControlRotation = FRotator::ZeroRotator;
};
