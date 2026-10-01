#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "QuestDirector.generated.h"

class UArcweaveSubsystem;

/** Owns gameplay interaction timing; Arcweave owns the authored narrative decisions. */
UCLASS()
class ARCWEAVEQUEST_API UQuestDirector : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    UQuestDirector();

    bool StartNewGame(FString& Error);
    bool StartQuest(FString& Error);
    bool CollectCell(FName CellId, FString& Error);
    bool TryRestorePower(FString& Error);

    bool IsQuestStarted() const;
    bool IsPowerRestored() const { return bPowerRestored; }
    bool IsGateOpen() const { return bGateOpen; }
    bool HasCollectedCell(FName CellId) const { return CollectedCells.Contains(CellId); }
    int32 GetPowerCellCount() const;
    FString GetObjective() const { return Objective; }
    FString GetStatus() const { return Status; }
    FString GetCurrentElementId() const { return CurrentElementId; }

    FSimpleMulticastDelegate OnQuestChanged;

private:
    friend class FArcweaveQuestFlowTest;

    bool EnterElement(const FString& ElementId, FString& Error);
    bool RejectInteraction(const FString& Message, FString& Error);
    void PublishChange();

    UPROPERTY()
    UArcweaveSubsystem* Arcweave = nullptr;

    TMap<FName, TFunction<void()>> CommandHandlers;
    TSet<FName> CollectedCells;
    FString CurrentElementId;
    FString Objective = TEXT("Use the terminal to begin.");
    FString Status = TEXT("The gate has no power.");
    bool bProjectLoaded = false;
    bool bPowerRestored = false;
    bool bGateOpen = false;
};
