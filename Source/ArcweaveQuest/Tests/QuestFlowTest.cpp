#if WITH_DEV_AUTOMATION_TESTS

#include "QuestDirector.h"

#include "QuestBindings.h"
#include "ArcweaveVariable.h"
#include "ArcscriptTranspilerOutput.h"
#include "ArcweaveSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FArcweaveQuestFlowTest,
    "ArcweaveQuest.Flow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FArcweaveQuestFlowTest::RunTest(const FString& Parameters)
{
    UGameInstance* GameInstance = NewObject<UGameInstance>();
    UQuestDirector* Director = NewObject<UQuestDirector>(GameInstance);
    UArcweaveSubsystem* Arcweave = GEngine->GetEngineSubsystem<UArcweaveSubsystem>();
    FString Error;
    if (!TestTrue(TEXT("The released plugin loads the local narrative export"), Director->StartNewGame(Error)))
    {
        AddError(Error);
        return false;
    }

    int32 PowerCommands = 0;
    int32 GateCommands = 0;
    int32 PresentationChanges = 0;
    const TFunction<void()> RestorePower = Director->CommandHandlers.FindChecked(TEXT("restore_power"));
    const TFunction<void()> OpenGate = Director->CommandHandlers.FindChecked(TEXT("open_gate"));
    Director->CommandHandlers.Add(TEXT("restore_power"), [&PowerCommands, RestorePower]
    {
        ++PowerCommands;
        RestorePower();
    });
    Director->CommandHandlers.Add(TEXT("open_gate"), [&GateCommands, OpenGate]
    {
        ++GateCommands;
        OpenGate();
    });
    Director->OnQuestChanged.AddLambda([this, Director, &PresentationChanges]
    {
        ++PresentationChanges;
        if (Director->IsPowerRestored())
        {
            TestTrue(TEXT("Presentation observes both completed world commands"), Director->IsGateOpen());
        }
    });

    const auto Visits = [Arcweave](const TCHAR* Id)
    {
        return Arcweave->GetArcweaveProjectData().Visits.FindChecked(Id);
    };
    const auto Variable = [Arcweave](const TCHAR* Id)
    {
        return Arcweave->GetArcweaveProjectData().CurrentVars.FindChecked(Id).Value;
    };
    TestFalse(TEXT("New game awaits the terminal interaction"), Director->IsQuestStarted());
    TestEqual(TEXT("New game has no collected cells"), Director->GetPowerCellCount(), 0);
    TestTrue(TEXT("Generator before terminal runs a successful narrative interaction"), Director->TryRestorePower(Error));
    TestTrue(TEXT("Authored prerequisite guidance is not reported as an integration error"), Error.IsEmpty());
    TestEqual(TEXT("Generator enters the authored terminal-required element"), Director->GetCurrentElementId(), FString(QuestBindings::TerminalRequiredElement));
    TestEqual(TEXT("Preterminal interaction visits the generator"), Visits(QuestBindings::GeneratorElement), 1);
    TestEqual(TEXT("Preterminal interaction visits the authored guidance"), Visits(QuestBindings::TerminalRequiredElement), 1);
    TestTrue(TEXT("Authored guidance tells the player to accept the task"),
        Director->GetStatus().Contains(TEXT("Use the terminal to accept the task first.")));
    TestTrue(TEXT("Feedback includes the explanation authored in the guidance node"),
        Director->GetStatus().Contains(TEXT("The generator is waiting for authorization.")));
    TestFalse(TEXT("Guidance does not accept the quest"), Director->IsQuestStarted());
    TestFalse(TEXT("Guidance does not restore power"), Director->IsPowerRestored());
    TestFalse(TEXT("Guidance does not open the gate"), Director->IsGateOpen());

    Arcweave->SetVariable(QuestBindings::PowerCellsVariable, TEXT("2"));
    TestTrue(TEXT("Generator reevaluates the authored prerequisite even with enough cells"), Director->TryRestorePower(Error));
    TestEqual(TEXT("Quest prerequisite takes precedence over the two-cell condition"), Director->GetCurrentElementId(), FString(QuestBindings::TerminalRequiredElement));
    TestEqual(TEXT("Two-cell attempt visits the authored guidance again"), Visits(QuestBindings::TerminalRequiredElement), 2);
    TestEqual(TEXT("Unaccepted quest never enters success despite having two cells"), Visits(QuestBindings::SuccessElement), 0);
    TestFalse(TEXT("Two-cell prerequisite response leaves the quest unaccepted"), Director->IsQuestStarted());
    TestEqual(TEXT("Two-cell prerequisite response leaves the authored power flag false"), Variable(QuestBindings::PowerRestoredVariable), FString(TEXT("false")));
    TestFalse(TEXT("Two-cell prerequisite response leaves world power off"), Director->IsPowerRestored());
    TestFalse(TEXT("Two-cell prerequisite response leaves the gate closed"), Director->IsGateOpen());
    TestEqual(TEXT("Prerequisite guidance dispatches no completion commands"), PowerCommands + GateCommands, 0);
    Arcweave->SetVariable(QuestBindings::PowerCellsVariable, TEXT("0"));
    TestFalse(TEXT("Cells cannot be collected before accepting the task"), Director->CollectCell(TEXT("cell_a"), Error));
    TestEqual(TEXT("Rejected pickup does not add a cell"), Director->GetPowerCellCount(), 0);

    TestTrue(TEXT("Terminal starts the authored quest"), Director->StartQuest(Error));
    TestTrue(TEXT("Start script updates the questStarted variable"), Director->IsQuestStarted());
    TestEqual(TEXT("Current narrative cursor is Start"), Director->GetCurrentElementId(), FString(QuestBindings::StartElement));
    TestEqual(TEXT("Start element is visited once"), Visits(QuestBindings::StartElement), 1);
    TestTrue(TEXT("Repeated terminal interaction is harmless"), Director->StartQuest(Error));
    TestEqual(TEXT("Repeated terminal does not replay its script"), Visits(QuestBindings::StartElement), 1);

    TestTrue(TEXT("Generator evaluates the authored condition with zero cells"), Director->TryRestorePower(Error));
    TestEqual(TEXT("False branch enters the authored missing-cells element"), Director->GetCurrentElementId(), FString(QuestBindings::MissingCellsElement));
    TestFalse(TEXT("False branch leaves power off"), Director->IsPowerRestored());
    TestFalse(TEXT("False branch leaves the gate closed"), Director->IsGateOpen());
    TestEqual(TEXT("False branch executes no world command"), PowerCommands + GateCommands, 0);
    TestFalse(TEXT("Authored missing-cells text is rendered"), Director->GetStatus().IsEmpty());

    TestTrue(TEXT("First world pickup updates narrative state"), Director->CollectCell(TEXT("cell_a"), Error));
    TestTrue(TEXT("World remembers the first pickup"), Director->HasCollectedCell(TEXT("cell_a")));
    TestEqual(TEXT("SetVariable passes one collected cell into Arcweave"), Variable(QuestBindings::PowerCellsVariable), FString(TEXT("1")));
    TestFalse(TEXT("Repeated pickup cannot award another cell"), Director->CollectCell(TEXT("cell_a"), Error));
    TestEqual(TEXT("Duplicate pickup preserves the narrative count"), Director->GetPowerCellCount(), 1);
    TestTrue(TEXT("Generator reevaluates with one cell"), Director->TryRestorePower(Error));
    TestEqual(TEXT("One cell still takes the authored false branch"), Director->GetCurrentElementId(), FString(QuestBindings::MissingCellsElement));
    TestEqual(TEXT("Each valid attempt visits the missing-cells response"), Visits(QuestBindings::MissingCellsElement), 2);

    TestTrue(TEXT("Second pickup updates narrative state"), Director->CollectCell(TEXT("cell_b"), Error));
    TestEqual(TEXT("Both cells are present in the Arcweave variable"), Variable(QuestBindings::PowerCellsVariable), FString(TEXT("2")));
    TestTrue(TEXT("Generator reevaluates the same branch after the second pickup"), Director->TryRestorePower(Error));
    TestEqual(TEXT("True branch enters the actual success connection target"), Director->GetCurrentElementId(), FString(QuestBindings::SuccessElement));
    TestEqual(TEXT("Success script updates the narrative completion flag"), Variable(QuestBindings::PowerRestoredVariable), FString(TEXT("true")));
    TestTrue(TEXT("restore_power component dispatches the C++ world handler"), Director->IsPowerRestored());
    TestTrue(TEXT("open_gate component dispatches the C++ world handler"), Director->IsGateOpen());
    TestEqual(TEXT("Power handler executes once"), PowerCommands, 1);
    TestEqual(TEXT("Gate handler executes once"), GateCommands, 1);
    TestEqual(TEXT("Success element is entered once"), Visits(QuestBindings::SuccessElement), 1);
    TestEqual(TEXT("Generator ran once per deliberate interaction, including prerequisite attempts"), Visits(QuestBindings::GeneratorElement), 5);
    TestTrue(TEXT("World changes notify presentation"), PresentationChanges > 0);

    TestTrue(TEXT("Completed generator accepts a repeated interaction"), Director->TryRestorePower(Error));
    TestEqual(TEXT("Repeated completion does not execute power again"), PowerCommands, 1);
    TestEqual(TEXT("Repeated completion does not execute gate again"), GateCommands, 1);
    TestEqual(TEXT("Repeated completion does not replay the success script"), Visits(QuestBindings::SuccessElement), 1);
    TestEqual(TEXT("Repeated completion does not advance generator visits"), Visits(QuestBindings::GeneratorElement), 5);

    TestTrue(TEXT("Restart reloads authored defaults"), Director->StartNewGame(Error));
    TestFalse(TEXT("Restart clears the quest flag"), Director->IsQuestStarted());
    TestFalse(TEXT("Restart resets world power"), Director->IsPowerRestored());
    TestFalse(TEXT("Restart closes the gate"), Director->IsGateOpen());
    TestFalse(TEXT("Restart respawns the first cell"), Director->HasCollectedCell(TEXT("cell_a")));
    TestFalse(TEXT("Restart respawns the second cell"), Director->HasCollectedCell(TEXT("cell_b")));
    TestEqual(TEXT("Restart restores the authored cell count"), Director->GetPowerCellCount(), 0);
    TestEqual(TEXT("Restart restores the authored completion flag"), Variable(QuestBindings::PowerRestoredVariable), FString(TEXT("false")));
    TestTrue(TEXT("Restart clears the narrative cursor"), Director->GetCurrentElementId().IsEmpty());
    for (const auto& Pair : Arcweave->GetArcweaveProjectData().Visits)
    {
        TestEqual(TEXT("Restart clears visit counter ") + Pair.Key, Pair.Value, 0);
    }
    TestEqual(TEXT("Restart applies world state without running completion commands"), PowerCommands + GateCommands, 2);
    return true;
}

#endif
