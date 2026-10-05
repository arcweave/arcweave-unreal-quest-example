#pragma once

#include "CoreMinimal.h"
#include "ArcweaveVariable.h"
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
    virtual void Deinitialize() override;

    bool StartNewGame(FString& Error);
    bool StartQuest(FString& Error);
    bool CollectCell(FName CellId, FString& Error);
    bool TryRestorePower(FString& Error);
    bool ReachExit(FString& Error);
    bool SaveCheckpoint(const FString& SlotName, const FTransform& PlayerTransform,
        const FRotator& ControlRotation, FString& Error);
    bool LoadCheckpoint(const FString& SlotName, FTransform& OutPlayerTransform,
        FRotator& OutControlRotation, FString& Error);

    bool IsQuestStarted() const;
    bool IsQuestCompleted() const;
    bool IsPowerRestored() const;
    bool IsGateOpen() const { return bGateOpen; }
    bool HasCollectedCell(FName CellId) const { return CollectedCells.Contains(CellId); }
    int32 GetPowerCellCount() const;
    int32 GetRequiredPowerCellCount() const;
    FString GetObjective() const { return Objective; }
    FString GetStatus() const { return Status; }
    FString GetPersistenceStatus() const { return PersistenceStatus; }
    FString GetCurrentElementId() const { return CurrentElementId; }
    FString GetPresentationElementId() const { return PresentationElementId; }
    FString GetUIText(FName QualifiedField) const { return UIText.FindRef(QualifiedField); }

    FSimpleMulticastDelegate OnQuestChanged;

private:
    friend class FArcweaveQuestFlowTest;
    friend class FArcweaveQuestSaveLoadTest;

    bool RunEvent(const FString& EventType, FString& Error, FName CellId = NAME_None);
    bool RunGraph(const FString& EntryElementId, bool bDispatchCommands,
        FArcweaveElementData& LastElement, FString& Error);
    bool RefreshPresentation(FString& Error);
    bool RejectInteraction(const FString& Message, FString& Error);
    bool RejectPersistence(FName FeedbackField, const FString& Message, FString& Error);
    void RefreshRuntimeCaches();
    void PublishChange();

    UFUNCTION()
    void HandleVariablesChanged(const TArray<FArcweaveVariable>& Variables);

    UFUNCTION()
    void HandleStateRestored();

    UPROPERTY()
    UArcweaveSubsystem* Arcweave = nullptr;

    TMap<FName, TFunction<void()>> CommandHandlers;
    // Applied physical pickup effects; narrative collection state lives in Arcweave.
    TSet<FName> CollectedCells;
    FName PendingCellId;
    FString CurrentElementId;
    FString PresentationEntryId;
    FString PresentationElementId;
    TMap<FName, FString> UIVariableIds;
    TMap<FName, FString> UIText;
    // Read cache only: Arcweave owns these values and reports every runtime change.
    TMap<FString, FString> StateValues;
    FString Objective;
    FString Status;
    FString PersistenceStatus;
    bool bProjectLoaded = false;
    bool bGateOpen = false;
    bool bInteractionInProgress = false;
};
