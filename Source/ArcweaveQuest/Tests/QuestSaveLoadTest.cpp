#if WITH_DEV_AUTOMATION_TESTS

#include "QuestDirector.h"
#include "QuestTestNarrative.h"
#include "QuestSaveGame.h"

#include "ArcweaveSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
struct FQuestCheckpointObservation
{
    FArcweaveProjectData Narrative;
    TMap<FName, FString> UI;
    FString CurrentElement;
    FString PresentationElement;
    FString Objective;
    FString Status;
    bool bGateOpen;
};

FQuestCheckpointObservation ObserveCheckpoint(const UQuestDirector& Director)
{
    FQuestCheckpointObservation Result;
    Result.Narrative = GEngine->GetEngineSubsystem<UArcweaveSubsystem>()->GetArcweaveProjectData();
    for (const auto& Pair : Result.Narrative.CurrentVars)
    {
        const FArcweaveVariable& Variable = Pair.Value;
        if (Variable.Scope == TEXT("hud") || Variable.Scope == TEXT("world_text")
            || Variable.Scope == TEXT("quest_ui") || Variable.Scope == TEXT("save_ui"))
        {
            const FName Field(*(Variable.Scope + TEXT(".") + Variable.Name));
            Result.UI.Add(Field, Director.GetUIText(Field));
        }
    }
    Result.CurrentElement = Director.GetCurrentElementId();
    Result.PresentationElement = Director.GetPresentationElementId();
    Result.Objective = Director.GetObjective();
    Result.Status = Director.GetStatus();
    Result.bGateOpen = Director.IsGateOpen();
    return Result;
}

void CheckCheckpoint(FAutomationTestBase& Test, const FString& Stage,
    const UQuestDirector& Director, const FQuestCheckpointObservation& Expected, const FQuestTestNarrative& NarrativeIds)
{
    const FString Prefix = Stage + TEXT(": ");
    const FArcweaveProjectData Actual = GEngine->GetEngineSubsystem<UArcweaveSubsystem>()->GetArcweaveProjectData();
    Test.TestTrue(Prefix + TEXT("all visit counters are restored without executing narrative nodes"),
        Actual.Visits.OrderIndependentCompareEqual(Expected.Narrative.Visits));
    Test.TestEqual(Prefix + TEXT("variable count is unchanged"), Actual.CurrentVars.Num(), Expected.Narrative.CurrentVars.Num());
    for (const auto& Pair : Expected.Narrative.CurrentVars)
    {
        const FArcweaveVariable* Variable = Actual.CurrentVars.Find(Pair.Key);
        if (Test.TestNotNull(Prefix + TEXT("saved variable exists: ") + Pair.Key, Variable))
        {
            Test.TestEqual(Prefix + TEXT("saved value: ") + Pair.Value.Scope + TEXT(".") + Pair.Value.Name, Variable->Value, Pair.Value.Value);
            Test.TestEqual(Prefix + TEXT("authored defaults survive restoration: ") + Pair.Key, Variable->DefaultValue, Pair.Value.DefaultValue);
        }
    }
    for (const auto& Pair : Expected.UI)
    {
        Test.TestEqual(Prefix + TEXT("cached UI restored: ") + Pair.Key.ToString(), Director.GetUIText(Pair.Key), Pair.Value);
    }
    Test.TestEqual(Prefix + TEXT("acceptance read cache"), Director.IsQuestStarted(),
        Expected.Narrative.CurrentVars.FindChecked(*NarrativeIds.QuestStartedAttribute).Value == TEXT("true"));
    Test.TestEqual(Prefix + TEXT("power read cache"), Director.IsPowerRestored(),
        Expected.Narrative.CurrentVars.FindChecked(*NarrativeIds.PowerRestoredAttribute).Value == TEXT("true"));
    Test.TestEqual(Prefix + TEXT("completion read cache"), Director.IsQuestCompleted(),
        Expected.Narrative.CurrentVars.FindChecked(*NarrativeIds.QuestCompletedAttribute).Value == TEXT("true"));
    Test.TestEqual(Prefix + TEXT("inventory read cache"), Director.GetPowerCellCount(),
        FCString::Atoi(*Expected.Narrative.CurrentVars.FindChecked(*NarrativeIds.PowerCellsAttribute).Value));
    Test.TestEqual(Prefix + TEXT("gameplay cursor"), Director.GetCurrentElementId(), Expected.CurrentElement);
    Test.TestEqual(Prefix + TEXT("objective cursor"), Director.GetPresentationElementId(), Expected.PresentationElement);
    Test.TestEqual(Prefix + TEXT("resolved objective"), Director.GetObjective(), Expected.Objective);
    Test.TestEqual(Prefix + TEXT("resolved interaction feedback"), Director.GetStatus(), Expected.Status);
    Test.TestEqual(Prefix + TEXT("applied gate effect"), Director.IsGateOpen(), Expected.bGateOpen);
    Test.TestEqual(Prefix + TEXT("cell A availability follows its saved Arcweave flag"), Director.HasCollectedCell(TEXT("cell_a")),
        Expected.Narrative.CurrentVars.FindChecked(*NarrativeIds.CellACollectedAttribute).Value == TEXT("true"));
    Test.TestEqual(Prefix + TEXT("cell B availability follows its saved Arcweave flag"), Director.HasCollectedCell(TEXT("cell_b")),
        Expected.Narrative.CurrentVars.FindChecked(*NarrativeIds.CellBCollectedAttribute).Value == TEXT("true"));
}

/** Only removes slots created by this test, including on an assertion failure. */
struct FScopedQuestTestSlots
{
    const FString Prefix = TEXT("ArcweaveQuest_Automation_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    TArray<FString> Slots;

    FString Add(const TCHAR* Suffix)
    {
        const FString Slot = Prefix + TEXT("_") + Suffix;
        Slots.Add(Slot);
        return Slot;
    }

    ~FScopedQuestTestSlots()
    {
        for (const FString& Slot : Slots) UGameplayStatics::DeleteGameInSlot(Slot, 0);
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FArcweaveQuestSaveLoadTest,
    "ArcweaveQuest.Persistence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FArcweaveQuestSaveLoadTest::RunTest(const FString& Parameters)
{
    FScopedQuestTestSlots Slots;
    const FString OneCellSlot = Slots.Add(TEXT("OneCell"));
    const FString PoweredSlot = Slots.Add(TEXT("Powered"));
    const FString CompletedSlot = Slots.Add(TEXT("Completed"));
    const FString InvalidSlot = Slots.Add(TEXT("Invalid"));
    const FString MissingSlot = Slots.Add(TEXT("Missing"));
    UGameInstance* GameInstance = NewObject<UGameInstance>();
    UQuestDirector* Director = NewObject<UQuestDirector>(GameInstance);
    UArcweaveSubsystem* Arcweave = GEngine->GetEngineSubsystem<UArcweaveSubsystem>();
    FString Error;
    if (!TestTrue(TEXT("Persistence test initializes the current narrative"), Director->StartNewGame(Error)))
    {
        AddError(Error);
        return false;
    }

    FQuestTestNarrative NarrativeIds;
    if (!NarrativeIds.Resolve(Arcweave->GetArcweaveProjectData(), *this)) return false;

    int32 GameplayCommands = 0;
    const auto ObserveCommands = [&GameplayCommands](UQuestDirector* Target)
    {
        const TFunction<void()> Original = Target->CommandHandlers.FindChecked(TEXT("open_gate"));
        Target->CommandHandlers.Add(TEXT("open_gate"), [&GameplayCommands, Original]
        {
            ++GameplayCommands;
            Original();
        });
    };
    ObserveCommands(Director);

    TestTrue(TEXT("Accept the quest before saving"), Director->StartQuest(Error));
    // Restore must rebuild the UI cache from runtime variables, not authored defaults.
    const FArcweaveProjectData Project = Arcweave->GetArcweaveProjectData();
    for (const auto& Pair : Project.CurrentVars)
    {
        if (Pair.Value.Scope == TEXT("hud") && Pair.Value.Name == TEXT("station_name"))
        {
            Arcweave->SetVariable(Pair.Key, TEXT("SAVED STATION NAME"));
        }
    }
    if (!TestTrue(TEXT("Collect cell A before the first checkpoint"), Director->CollectCell(TEXT("cell_a"), Error))) return false;
    const FTransform SavedTransform(FRotator(0, 35, 0), FVector(410, -735, 110));
    const FRotator SavedControl(-12, 35, 0);
    const FQuestCheckpointObservation OneCell = ObserveCheckpoint(*Director);
    TestEqual(TEXT("The one-cell checkpoint stores a resolved inventory objective"), OneCell.Objective,
        FString(TEXT("Collect power cells (1/2), then use the generator.")));
    TestEqual(TEXT("The one-cell checkpoint stores resolved feedback"), OneCell.Status, FString(TEXT("Collected a power cell (1/2).")));
    if (!TestTrue(TEXT("Save writes a real one-cell checkpoint"), Director->SaveCheckpoint(OneCellSlot, SavedTransform, SavedControl, Error))) return false;
    TestTrue(TEXT("The checkpoint exists in Unreal's save system"), UGameplayStatics::DoesSaveGameExist(OneCellSlot, 0));
    TestEqual(TEXT("Saving uses the authored confirmation"), Director->GetPersistenceStatus(), Director->GetUIText(TEXT("save_ui.saved")));
    CheckCheckpoint(*this, TEXT("Saving does not advance the game"), *Director, OneCell, NarrativeIds);

    TestTrue(TEXT("Continue to the second pickup"), Director->CollectCell(TEXT("cell_b"), Error));
    if (!TestTrue(TEXT("Restore power after the one-cell checkpoint"), Director->TryRestorePower(Error))) return false;
    const FQuestCheckpointObservation Powered = ObserveCheckpoint(*Director);
    TestTrue(TEXT("Powered checkpoint has an applied open gate"), Powered.bGateOpen);
    if (!TestTrue(TEXT("Save a powered checkpoint"), Director->SaveCheckpoint(PoweredSlot, SavedTransform, SavedControl, Error))) return false;
    if (!TestTrue(TEXT("Complete the mission after the powered checkpoint"), Director->ReachExit(Error))) return false;
    const FQuestCheckpointObservation Completed = ObserveCheckpoint(*Director);
    if (!TestTrue(TEXT("Save the completed mission"), Director->SaveCheckpoint(CompletedSlot, SavedTransform, SavedControl, Error))) return false;

    const auto RestoreAndCheck = [this, &Director, &Error, &GameplayCommands, &SavedTransform, &SavedControl, &NarrativeIds]
        (const FString& Slot, const FQuestCheckpointObservation& Expected, const TCHAR* Stage)
    {
        int32 Notifications = 0;
        const FDelegateHandle Changed = Director->OnQuestChanged.AddLambda([&Notifications] { ++Notifications; });
        const int32 CommandsBefore = GameplayCommands;
        FTransform RestoredTransform = FTransform::Identity;
        FRotator RestoredControl = FRotator::ZeroRotator;
        const bool bLoaded = Director->LoadCheckpoint(Slot, RestoredTransform, RestoredControl, Error);
        Director->OnQuestChanged.Remove(Changed);
        if (!TestTrue(FString(Stage) + TEXT(": disk load succeeds"), bLoaded))
        {
            AddError(Error);
            return false;
        }
        CheckCheckpoint(*this, Stage, *Director, Expected, NarrativeIds);
        TestTrue(FString(Stage) + TEXT(": player transform round-trips"), RestoredTransform.Equals(SavedTransform));
        TestTrue(FString(Stage) + TEXT(": camera rotation round-trips"), RestoredControl.Equals(SavedControl));
        TestEqual(FString(Stage) + TEXT(": no gameplay action is replayed"), GameplayCommands, CommandsBefore);
        TestEqual(FString(Stage) + TEXT(": exactly one completed state is published"), Notifications, 1);
        TestEqual(FString(Stage) + TEXT(": authored load confirmation"), Director->GetPersistenceStatus(), Director->GetUIText(TEXT("save_ui.loaded")));
        return true;
    };

    if (!RestoreAndCheck(OneCellSlot, OneCell, TEXT("Completed to one cell"))) return false;
    if (!RestoreAndCheck(PoweredSlot, Powered, TEXT("One cell to powered"))) return false;
    if (!RestoreAndCheck(CompletedSlot, Completed, TEXT("Powered to completed"))) return false;
    if (!RestoreAndCheck(CompletedSlot, Completed, TEXT("Repeated completed load"))) return false;

    const auto CheckRejectedLoad = [this, Director, &Error, &GameplayCommands, &NarrativeIds]
        (const FString& Slot, const TCHAR* ExpectedMessageKey, const TCHAR* Stage)
    {
        const FQuestCheckpointObservation Before = ObserveCheckpoint(*Director);
        const int32 CommandsBefore = GameplayCommands;
        const FTransform OutputSentinel(FRotator(0, 170, 0), FVector(100, 200, 300));
        const FRotator RotationSentinel(-23, 170, 0);
        FTransform Output = OutputSentinel;
        FRotator Control = RotationSentinel;
        TestFalse(FString(Stage) + TEXT(": rejected"), Director->LoadCheckpoint(Slot, Output, Control, Error));
        TestFalse(FString(Stage) + TEXT(": diagnostic explains the failure"), Error.IsEmpty());
        TestEqual(FString(Stage) + TEXT(": authored player-facing feedback"),
            Director->GetPersistenceStatus(), Director->GetUIText(FName(ExpectedMessageKey)));
        CheckCheckpoint(*this, Stage, *Director, Before, NarrativeIds);
        TestTrue(FString(Stage) + TEXT(": transform output is untouched"), Output.Equals(OutputSentinel));
        TestTrue(FString(Stage) + TEXT(": rotation output is untouched"), Control.Equals(RotationSentinel));
        TestEqual(FString(Stage) + TEXT(": no engine commands"), GameplayCommands, CommandsBefore);
    };
    CheckRejectedLoad(MissingSlot, TEXT("save_ui.no_save"), TEXT("Missing save"));
    // An empty legacy class name is unreadable without suggesting an unbounded string length.
    const TArray<uint8> InvalidBytes = {0x00, 0x00, 0x00, 0x00};
    if (!TestTrue(TEXT("Write malformed save bytes for the corruption case"), UGameplayStatics::SaveDataToSlot(InvalidBytes, InvalidSlot, 0))) return false;
    CheckRejectedLoad(InvalidSlot, TEXT("save_ui.load_failed"), TEXT("Corrupt save"));

    UQuestSaveGame* Incompatible = Cast<UQuestSaveGame>(UGameplayStatics::LoadGameFromSlot(OneCellSlot, 0));
    if (!TestNotNull(TEXT("The checkpoint uses the sample's USaveGame class"), Incompatible)) return false;
    Incompatible->ArcweaveState.ProjectFingerprint = TEXT("different-narrative-content");
    if (!TestTrue(TEXT("Write a checkpoint from different narrative content"), UGameplayStatics::SaveGameToSlot(Incompatible, InvalidSlot, 0))) return false;
    CheckRejectedLoad(InvalidSlot, TEXT("save_ui.incompatible_save"), TEXT("Changed narrative content"));

    UQuestSaveGame* InvalidVersion = Cast<UQuestSaveGame>(UGameplayStatics::LoadGameFromSlot(OneCellSlot, 0));
    if (!TestNotNull(TEXT("Reload the unmodified checkpoint for the format case"), InvalidVersion)) return false;
    TestEqual(TEXT("The current sample save format has no duplicated pickup state"), InvalidVersion->FormatVersion, 2);
    // Version 1 matches the class default and may be omitted by Unreal's delta serialization.
    InvalidVersion->FormatVersion = 1;
    if (!TestTrue(TEXT("Write a checkpoint using the old duplicated-pickup save format"), UGameplayStatics::SaveGameToSlot(InvalidVersion, InvalidSlot, 0))) return false;
    CheckRejectedLoad(InvalidSlot, TEXT("save_ui.incompatible_save"), TEXT("Legacy pickup save format"));
    InvalidVersion->FormatVersion = 99;
    if (!TestTrue(TEXT("Write an unsupported sample save version"), UGameplayStatics::SaveGameToSlot(InvalidVersion, InvalidSlot, 0))) return false;
    CheckRejectedLoad(InvalidSlot, TEXT("save_ui.incompatible_save"), TEXT("Unsupported save format"));

    UQuestSaveGame* InvalidWorld = Cast<UQuestSaveGame>(UGameplayStatics::LoadGameFromSlot(OneCellSlot, 0));
    if (!TestNotNull(TEXT("Reload the checkpoint for invalid world data"), InvalidWorld)) return false;
    InvalidWorld->CurrentElementId = TEXT("unknown_element");
    if (!TestTrue(TEXT("Write a checkpoint with an unknown gameplay cursor"), UGameplayStatics::SaveGameToSlot(InvalidWorld, InvalidSlot, 0))) return false;
    CheckRejectedLoad(InvalidSlot, TEXT("save_ui.load_failed"), TEXT("Invalid game-owned state"));

    UQuestSaveGame* InvalidNarrative = Cast<UQuestSaveGame>(UGameplayStatics::LoadGameFromSlot(OneCellSlot, 0));
    if (!TestNotNull(TEXT("Reload the checkpoint for invalid narrative data"), InvalidNarrative)) return false;
    InvalidNarrative->ArcweaveState.Variables.Remove(*NarrativeIds.PowerCellsAttribute);
    if (!TestTrue(TEXT("Write an incomplete narrative snapshot"), UGameplayStatics::SaveGameToSlot(InvalidNarrative, InvalidSlot, 0))) return false;
    CheckRejectedLoad(InvalidSlot, TEXT("save_ui.load_failed"), TEXT("Invalid plugin-owned state"));

    // A new game resets the live interpreter but deliberately keeps the disk checkpoint.
    if (!TestTrue(TEXT("StartNewGame resets the completed live game"), Director->StartNewGame(Error))) return false;
    TestTrue(TEXT("Starting over preserves the disk save"), UGameplayStatics::DoesSaveGameExist(OneCellSlot, 0));
    TestTrue(TEXT("Starting over clears old persistence feedback"), Director->GetPersistenceStatus().IsEmpty());
    if (!RestoreAndCheck(OneCellSlot, OneCell, TEXT("Load after restart"))) return false;

    Director->Deinitialize();
    Director = NewObject<UQuestDirector>(GameInstance);
    if (!TestTrue(TEXT("A replacement director imports a fresh narrative"), Director->StartNewGame(Error))) return false;
    ObserveCommands(Director);
    if (!RestoreAndCheck(OneCellSlot, OneCell, TEXT("Load in a replacement director"))) return false;
    TestTrue(TEXT("Gameplay continues from the restored checkpoint"), Director->CollectCell(TEXT("cell_b"), Error));
    TestEqual(TEXT("The next pickup increments restored inventory exactly once"), Director->GetPowerCellCount(), 2);
    TestEqual(TEXT("The next pickup executes the saved pickup counter plus one"),
        Arcweave->GetArcweaveProjectData().Visits.FindChecked(*NarrativeIds.PickupActionElement),
        OneCell.Narrative.Visits.FindChecked(*NarrativeIds.PickupActionElement) + 1);

    // Gate effects are saved independently, while pickups derive from Arcweave flags.
    // External state updates must not cause LoadCheckpoint to invent gameplay commands.
    if (!TestTrue(TEXT("Start a session with externally provided power"), Director->StartNewGame(Error))) return false;
    Arcweave->SetVariable(*NarrativeIds.PowerRestoredAttribute, TEXT("true"));
    Arcweave->SetVariable(*NarrativeIds.CellACollectedAttribute, TEXT("true"));
    if (!TestTrue(TEXT("Refresh the powered presentation without the generator action"), Director->StartQuest(Error))) return false;
    const FQuestCheckpointObservation ExternalPower = ObserveCheckpoint(*Director);
    TestTrue(TEXT("The external milestone supplies power"), Director->IsPowerRestored());
    TestFalse(TEXT("The separate gate action has not occurred"), ExternalPower.bGateOpen);
    TestTrue(TEXT("The external collected flag controls cell A availability"), Director->HasCollectedCell(TEXT("cell_a")));
    TestFalse(TEXT("The external flag leaves cell B available"), Director->HasCollectedCell(TEXT("cell_b")));
    TestEqual(TEXT("The external collected flag requires no pickup element replay"),
        ExternalPower.Narrative.Visits.FindChecked(*NarrativeIds.PickupActionElement), 0);
    if (!TestTrue(TEXT("Save narrative power and its separately closed world gate"),
        Director->SaveCheckpoint(PoweredSlot, SavedTransform, SavedControl, Error))) return false;
    if (!RestoreAndCheck(CompletedSlot, Completed, TEXT("Move from external power to a completed checkpoint"))) return false;
    if (!RestoreAndCheck(PoweredSlot, ExternalPower, TEXT("Restore separately authored state and applied effects"))) return false;
    Director->Deinitialize();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FArcweaveQuestSaveSessionTest,
    "ArcweaveQuest.SaveSession",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FArcweaveQuestSaveSessionTest::RunTest(const FString& Parameters)
{
    FString Phase;
    if (!FParse::Value(FCommandLine::Get(), TEXT("QuestSavePhase="), Phase))
    {
        AddError(TEXT("Cross-process test requires -QuestSavePhase=Write followed by -QuestSavePhase=Read in separate processes."));
        return false;
    }
    const FString Slot(TEXT("ArcweaveQuest_Automation_CrossProcess"));
    const FTransform ExpectedTransform(FRotator(0, 42, 0), FVector(410, -735, 110));
    const FRotator ExpectedControl(-18, 42, 0);
    UQuestDirector* Director = NewObject<UQuestDirector>(NewObject<UGameInstance>());
    UArcweaveSubsystem* Arcweave = GEngine->GetEngineSubsystem<UArcweaveSubsystem>();
    FString Error;
    if (!TestTrue(TEXT("New process initializes its interpreter"), Director->StartNewGame(Error))) return false;
    FQuestTestNarrative NarrativeIds;
    if (!NarrativeIds.Resolve(Arcweave->GetArcweaveProjectData(), *this)) return false;
    if (Phase == TEXT("Write"))
    {
        UGameplayStatics::DeleteGameInSlot(Slot, 0);
        if (!TestTrue(TEXT("Writer accepts the quest"), Director->StartQuest(Error))
            || !TestTrue(TEXT("Writer collects cell B"), Director->CollectCell(TEXT("cell_b"), Error))
            || !TestTrue(TEXT("Writer persists a one-cell checkpoint"), Director->SaveCheckpoint(Slot, ExpectedTransform, ExpectedControl, Error))) return false;
        TestEqual(TEXT("Writer has visited the pickup response once"),
            Arcweave->GetArcweaveProjectData().Visits.FindChecked(*NarrativeIds.PickupActionElement), 1);
    }
    else if (Phase == TEXT("Read"))
    {
        TestEqual(TEXT("Reader starts with an empty inventory"), Director->GetPowerCellCount(), 0);
        TestEqual(TEXT("Reader starts without prior pickup visits"),
            Arcweave->GetArcweaveProjectData().Visits.FindChecked(*NarrativeIds.PickupActionElement), 0);
        UQuestSaveGame* Saved = Cast<UQuestSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
        if (!TestNotNull(TEXT("The previous process left its USaveGame on disk"), Saved)) return false;
        FTransform PlayerTransform;
        FRotator ControlRotation;
        const bool bLoaded = Director->LoadCheckpoint(Slot, PlayerTransform, ControlRotation, Error);
        UGameplayStatics::DeleteGameInSlot(Slot, 0);
        if (!TestTrue(TEXT("The new process restores the previous process's save"), bLoaded)) return false;
        TestEqual(TEXT("Reader restores one cell"), Director->GetPowerCellCount(), 1);
        TestTrue(TEXT("Reader restores acceptance"), Director->IsQuestStarted());
        TestTrue(TEXT("Reader derives cell B availability from the saved flag"), Director->HasCollectedCell(TEXT("cell_b")));
        TestFalse(TEXT("Reader keeps cell A available"), Director->HasCollectedCell(TEXT("cell_a")));
        TestFalse(TEXT("Reader keeps the gate closed"), Director->IsGateOpen());
        TestEqual(TEXT("Reader restores the resolved objective"), Director->GetObjective(), Saved->Objective);
        TestEqual(TEXT("Reader restores the resolved interaction feedback"), Director->GetStatus(), Saved->Status);
        TestTrue(TEXT("Reader restores player position and rotation"), PlayerTransform.Equals(ExpectedTransform));
        TestTrue(TEXT("Reader restores camera rotation"), ControlRotation.Equals(ExpectedControl));
        FArcweaveRuntimeState Restored;
        if (!TestTrue(TEXT("Reader can capture the restored interpreter"), Arcweave->CaptureState(Restored, Error))) return false;
        TestTrue(TEXT("All visits survive the process boundary without replay"), Restored.Visits.OrderIndependentCompareEqual(Saved->ArcweaveState.Visits));
        TestEqual(TEXT("All variables survive the process boundary"), Restored.Variables.Num(), Saved->ArcweaveState.Variables.Num());
        for (const auto& Pair : Saved->ArcweaveState.Variables)
        {
            TestEqual(TEXT("Cross-process variable: ") + Pair.Key, Restored.Variables.FindChecked(Pair.Key).Value, Pair.Value.Value);
        }
        TestTrue(TEXT("The new process can continue the quest"), Director->CollectCell(TEXT("cell_a"), Error));
        TestEqual(TEXT("Continued play reaches two cells"), Director->GetPowerCellCount(), 2);
    }
    else
    {
        AddError(TEXT("QuestSavePhase must be Write or Read."));
    }
    Director->Deinitialize();
    return true;
}

#endif
