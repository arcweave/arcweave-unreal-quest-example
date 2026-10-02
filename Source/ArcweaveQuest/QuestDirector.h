#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "QuestDirector.generated.h"

class UArcweaveSubsystem;
struct FArcweaveElementData;

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
    bool ReachExit(FString& Error);

    bool IsQuestStarted() const;
    bool IsQuestCompleted() const;
    bool IsPowerRestored() const { return bPowerRestored; }
    bool IsGateOpen() const { return bGateOpen; }
    bool HasCollectedCell(FName CellId) const { return CollectedCells.Contains(CellId); }
    int32 GetPowerCellCount() const;
    int32 GetRequiredPowerCellCount() const;
    FString GetObjective() const { return Objective; }
    FString GetStatus() const { return Status; }
    FString GetCurrentElementId() const { return CurrentElementId; }
    FString GetPresentationElementId() const { return PresentationElementId; }
    FString GetCatalogText(FName Field) const { return CatalogText.FindRef(Field); }
    FString GetPresentationText(FName Field) const { return PresentationText.FindRef(Field); }

    FSimpleMulticastDelegate OnQuestChanged;

private:
    friend class FArcweaveQuestFlowTest;

    bool RunEvent(const FString& EntryElementId, FString& Error);
    bool RunGraph(const FString& EntryElementId, bool bDispatchCommands,
        FArcweaveElementData& LastElement, FString& Error);
    bool RefreshPresentation(FString& Error);
    bool RejectInteraction(const FString& Message, FString& Error);
    void PublishChange();

    UPROPERTY()
    UArcweaveSubsystem* Arcweave = nullptr;

    TMap<FName, TFunction<void()>> CommandHandlers;
    TSet<FName> CollectedCells;
    FName PendingCellId;
    FString CurrentElementId;
    FString PresentationElementId;
    TMap<FName, FString> CatalogText;
    TMap<FName, FString> PresentationText;
    FString Objective;
    FString Status;
    bool bProjectLoaded = false;
    bool bPowerRestored = false;
    bool bGateOpen = false;
};
