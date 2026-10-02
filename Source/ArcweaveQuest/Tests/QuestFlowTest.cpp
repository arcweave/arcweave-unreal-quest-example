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
    if (!TestTrue(TEXT("The released plugin loads and initializes the local narrative"), Director->StartNewGame(Error)))
    {
        AddError(Error);
        return false;
    }
    const FArcweaveProjectData InitialState = Arcweave->GetArcweaveProjectData();

    const TSet<FName> RequiredCatalogFields = {
        TEXT("brand"), TEXT("station_name"), TEXT("mission_tagline"), TEXT("cells_label"),
        TEXT("station_footer"), TEXT("terminal_label"), TEXT("cell_a_label"), TEXT("cell_b_label"),
        TEXT("sign_station"), TEXT("sign_distribution"), TEXT("sign_gate"), TEXT("sign_exit")
    };
    TMap<FName, FString> AuthoredCatalog;
    const FArcweaveComponentData* UIComponent = InitialState.Components.FindByPredicate(
        [](const FArcweaveComponentData& Component) { return Component.Id == QuestBindings::UIComponent; });
    if (!TestNotNull(TEXT("The UI text component is imported"), UIComponent)) return false;
    TestEqual(TEXT("The UI component exposes the authored ui scope"), UIComponent->CustomId, FString(TEXT("ui")));
    TestEqual(TEXT("The UI component contains twelve string attributes"), UIComponent->Attributes.Num(), 12);
    TMap<FName, FString> UIVariableIds;
    for (const FArcweaveAttributeData& Attribute : UIComponent->Attributes)
    {
        const FName Field(*Attribute.CustomId);
        const FArcweaveVariable* RuntimeVariable = InitialState.CurrentVars.Find(Attribute.Id);
        if (!TestTrue(TEXT("UI attribute has a required field: ") + Attribute.CustomId, RequiredCatalogFields.Contains(Field))
            || !TestFalse(TEXT("UI attribute field is unique: ") + Attribute.CustomId, UIVariableIds.Contains(Field))
            || !TestFalse(TEXT("Authored UI text is nonempty: ") + Attribute.CustomId, Attribute.Value.Data.IsEmpty())
            || !TestNotNull(TEXT("UI attribute imports a runtime variable: ") + Attribute.CustomId, RuntimeVariable))
        {
            return false;
        }
        UIVariableIds.Add(Field, Attribute.Id);
        AuthoredCatalog.Add(Field, Attribute.Value.Data);
        TestEqual(TEXT("UI attribute owner is the component: ") + Attribute.CustomId, Attribute.cId, UIComponent->Id);
        TestEqual(TEXT("UI attribute owner type is components: ") + Attribute.CustomId, Attribute.cType, FString(TEXT("components")));
        TestEqual(TEXT("UI variable uses the attribute custom ID: ") + Attribute.CustomId, RuntimeVariable->Name, Attribute.CustomId);
        TestEqual(TEXT("UI variable has string type: ") + Attribute.CustomId, RuntimeVariable->Type, FString(TEXT("string")));
        TestEqual(TEXT("UI variable belongs to components: ") + Attribute.CustomId, RuntimeVariable->cType, FString(TEXT("components")));
        TestEqual(TEXT("UI variable is scoped to ui: ") + Attribute.CustomId, RuntimeVariable->Scope, FString(TEXT("ui")));
        TestTrue(TEXT("UI variable retains its authored default: ") + Attribute.CustomId, RuntimeVariable->bHasDefaultValue);
        TestEqual(TEXT("UI variable default matches authored text: ") + Attribute.CustomId, RuntimeVariable->DefaultValue, Attribute.Value.Data);
        TestEqual(TEXT("Initial UI variable value matches authored text: ") + Attribute.CustomId, RuntimeVariable->Value, Attribute.Value.Data);
    }
    if (!TestEqual(TEXT("Every catalog field has its own runtime UI variable"), UIVariableIds.Num(), RequiredCatalogFields.Num())) return false;
    int32 GlobalVariableCount = 0;
    int32 UIVariableCount = 0;
    for (const auto& Pair : InitialState.CurrentVars)
    {
        GlobalVariableCount += Pair.Value.cType == TEXT("global") && Pair.Value.Scope.IsEmpty() ? 1 : 0;
        UIVariableCount += Pair.Value.cType == TEXT("components") && Pair.Value.Scope == TEXT("ui") ? 1 : 0;
    }
    TestEqual(TEXT("The five quest variables retain global scope"), GlobalVariableCount, 5);
    TestEqual(TEXT("All twelve UI strings use component scope"), UIVariableCount, 12);
    const auto CheckCatalogValues = [this, Director, Arcweave, &UIVariableIds](const TCHAR* Stage)
    {
        const FArcweaveProjectData State = Arcweave->GetArcweaveProjectData();
        for (const auto& Pair : UIVariableIds)
        {
            TestEqual(FString(Stage) + TEXT(": cached catalog matches ui.") + Pair.Key.ToString(),
                Director->GetCatalogText(Pair.Key), State.CurrentVars.FindChecked(Pair.Value).Value);
        }
    };

    int32 PowerCommands = 0;
    int32 GateCommands = 0;
    int32 PickupCommands = 0;
    int32 PresentationChanges = 0;
    const TFunction<void()> RestorePower = Director->CommandHandlers.FindChecked(TEXT("restore_power"));
    const TFunction<void()> OpenGate = Director->CommandHandlers.FindChecked(TEXT("open_gate"));
    const TFunction<void()> CollectCell = Director->CommandHandlers.FindChecked(TEXT("collect_cell"));
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
    Director->CommandHandlers.Add(TEXT("collect_cell"), [&PickupCommands, CollectCell]
    {
        ++PickupCommands;
        CollectCell();
    });
    Director->OnQuestChanged.AddLambda([this, Director, &PresentationChanges]
    {
        ++PresentationChanges;
        if (Director->IsPowerRestored())
        {
            TestTrue(TEXT("Presentation observes both completed world commands"), Director->IsGateOpen());
        }
        if (Director->IsQuestCompleted())
        {
            TestTrue(TEXT("Completed presentation also observes restored power"), Director->IsPowerRestored());
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
    const auto CheckCachedReads = [this, Director, Arcweave, &PowerCommands, &GateCommands,
        &PickupCommands, &PresentationChanges, &UIVariableIds](const TCHAR* Stage)
    {
        const FArcweaveProjectData Before = Arcweave->GetArcweaveProjectData();
        const FString Cursor = Director->GetCurrentElementId();
        const FString Presentation = Director->GetPresentationElementId();
        const FString Objective = Director->GetObjective();
        const FString Status = Director->GetStatus();
        TMap<FName, FString> Catalog;
        for (const auto& Pair : UIVariableIds)
        {
            Catalog.Add(Pair.Key, Director->GetCatalogText(Pair.Key));
        }
        const int32 Commands = PowerCommands + GateCommands + PickupCommands;
        const int32 Notifications = PresentationChanges;
        for (int32 Read = 0; Read < 32; ++Read)
        {
            Director->GetObjective();
            Director->GetStatus();
            Director->GetPowerCellCount();
            Director->GetRequiredPowerCellCount();
            Director->IsQuestStarted();
            Director->IsPowerRestored();
            Director->IsGateOpen();
            Director->IsQuestCompleted();
            Director->HasCollectedCell(TEXT("cell_a"));
            for (const auto& Pair : UIVariableIds)
            {
                Director->GetCatalogText(Pair.Key);
            }
            for (const TCHAR* Key : {TEXT("mission_heading"), TEXT("grid_status"), TEXT("terminal_prompt"),
                TEXT("cell_prompt"), TEXT("generator_prompt"), TEXT("generator_label"), TEXT("gate_label")})
            {
                Director->GetPresentationText(Key);
            }
        }
        const FArcweaveProjectData After = Arcweave->GetArcweaveProjectData();
        const FString Prefix = FString(Stage) + TEXT(": ");
        TestTrue(Prefix + TEXT("cached getters preserve every visit counter"), Before.Visits.OrderIndependentCompareEqual(After.Visits));
        TestEqual(Prefix + TEXT("cached getters preserve the variable collection"), After.CurrentVars.Num(), Before.CurrentVars.Num());
        for (const auto& Pair : Before.CurrentVars)
        {
            const FArcweaveVariable* Actual = After.CurrentVars.Find(Pair.Key);
            if (TestNotNull(Prefix + TEXT("variable remains present: ") + Pair.Key, Actual))
            {
                TestEqual(Prefix + TEXT("cached getters preserve variable ") + Pair.Key, Actual->Value, Pair.Value.Value);
            }
        }
        TestEqual(Prefix + TEXT("cached getters do not dispatch commands"), PowerCommands + GateCommands + PickupCommands, Commands);
        TestEqual(Prefix + TEXT("cached getters do not notify presentation"), PresentationChanges, Notifications);
        TestEqual(Prefix + TEXT("gameplay cursor is unchanged"), Director->GetCurrentElementId(), Cursor);
        TestEqual(Prefix + TEXT("presentation cursor is unchanged"), Director->GetPresentationElementId(), Presentation);
        for (const auto& Pair : Catalog)
        {
            TestEqual(Prefix + TEXT("cached catalog is unchanged: ") + Pair.Key.ToString(), Director->GetCatalogText(Pair.Key), Pair.Value);
        }
        TestEqual(Prefix + TEXT("objective is unchanged"), Director->GetObjective(), Objective);
        TestEqual(Prefix + TEXT("status is unchanged"), Director->GetStatus(), Status);
    };
    const auto CheckRestart = [this, Director, Arcweave, &InitialState, &Visits, &Error, &AuthoredCatalog, &CheckCatalogValues]()
    {
        if (!TestTrue(TEXT("Restart executes the authored initialization again"), Director->StartNewGame(Error)))
        {
            AddError(Error);
            return false;
        }
        const FArcweaveProjectData Restarted = Arcweave->GetArcweaveProjectData();
        TestFalse(TEXT("Restart clears quest acceptance"), Director->IsQuestStarted());
        TestFalse(TEXT("Restart clears completion"), Director->IsQuestCompleted());
        TestFalse(TEXT("Restart turns power off"), Director->IsPowerRestored());
        TestFalse(TEXT("Restart closes the gate"), Director->IsGateOpen());
        TestFalse(TEXT("Restart respawns cell A"), Director->HasCollectedCell(TEXT("cell_a")));
        TestFalse(TEXT("Restart respawns cell B"), Director->HasCollectedCell(TEXT("cell_b")));
        TestEqual(TEXT("Restart restores authored required count"), Director->GetRequiredPowerCellCount(), 2);
        TestEqual(TEXT("Restart restores collected count"), Director->GetPowerCellCount(), 0);
        TestEqual(TEXT("Initialization is visited once in the new session"), Visits(QuestBindings::InitializationElement), 1);
        TestEqual(TEXT("Restart retains the initialization gameplay cursor"), Director->GetCurrentElementId(), FString(QuestBindings::InitializationElement));
        TestEqual(TEXT("Restart selects the unaccepted presentation"), Director->GetPresentationElementId(), FString(QuestBindings::PresentationUnacceptedElement));
        TestTrue(TEXT("Restart visits match a newly initialized session, not an all-zero map"),
            Restarted.Visits.OrderIndependentCompareEqual(InitialState.Visits));
        TestEqual(TEXT("Restart preserves global quest and component UI variable definitions"), Restarted.CurrentVars.Num(), InitialState.CurrentVars.Num());
        for (const auto& Pair : InitialState.CurrentVars)
        {
            TestEqual(TEXT("Restart restores authored value ") + Pair.Key, Restarted.CurrentVars.FindChecked(Pair.Key).Value, Pair.Value.Value);
        }
        for (const auto& Pair : AuthoredCatalog)
        {
            TestEqual(TEXT("Restart restores authored catalog text: ") + Pair.Key.ToString(), Director->GetCatalogText(Pair.Key), Pair.Value);
        }
        CheckCatalogValues(TEXT("Restarted state"));
        return true;
    };

    TestEqual(TEXT("Five global quest variables and twelve UI strings are imported"), InitialState.CurrentVars.Num(), 17);
    TestFalse(TEXT("Initialization does not accept the task"), Director->IsQuestStarted());
    TestFalse(TEXT("Initialization does not complete the task"), Director->IsQuestCompleted());
    TestEqual(TEXT("Initial required cell count comes from the export"), Director->GetRequiredPowerCellCount(), 2);
    TestEqual(TEXT("Initialization executes once"), Visits(QuestBindings::InitializationElement), 1);
    TestEqual(TEXT("Presentation does not replace the initialization cursor"), Director->GetCurrentElementId(), FString(QuestBindings::InitializationElement));
    TestEqual(TEXT("Unaccepted presentation is selected by the graph"), Director->GetPresentationElementId(), FString(QuestBindings::PresentationUnacceptedElement));
    TestEqual(TEXT("Authored initial objective is cached"), Director->GetObjective(), FString(TEXT("Use the terminal to begin.")));
    CheckCatalogValues(TEXT("Initialized state"));
    TestFalse(TEXT("Initial terminal prompt is available"), Director->GetPresentationText(TEXT("terminal_prompt")).IsEmpty());
    CheckCachedReads(TEXT("Initial state"));

    TestTrue(TEXT("A generator attempt before acceptance executes authored guidance"), Director->TryRestorePower(Error));
    TestTrue(TEXT("Narrative denial is not an integration error"), Error.IsEmpty());
    TestEqual(TEXT("Generator enters the terminal-required response"), Director->GetCurrentElementId(), FString(QuestBindings::TerminalRequiredElement));
    TestTrue(TEXT("Generator guidance is the authored authorization text"), Director->GetStatus().Contains(TEXT("The generator is waiting for authorization.")));
    TestEqual(TEXT("Preterminal attempt visits the generator"), Visits(QuestBindings::GeneratorElement), 1);
    TestFalse(TEXT("Generator guidance leaves acceptance false"), Director->IsQuestStarted());
    TestFalse(TEXT("Generator guidance leaves power off"), Director->IsPowerRestored());

    Arcweave->SetVariable(QuestBindings::PowerCellsVariable, TEXT("2"));
    TestTrue(TEXT("Authored acceptance requirement still applies with enough cells"), Director->TryRestorePower(Error));
    TestEqual(TEXT("Acceptance takes precedence over the required count"), Director->GetCurrentElementId(), FString(QuestBindings::TerminalRequiredElement));
    TestEqual(TEXT("Premature two-cell attempt does not execute success"), Visits(QuestBindings::SuccessElement), 0);
    TestEqual(TEXT("Premature attempts execute no world commands"), PowerCommands + GateCommands + PickupCommands, 0);
    Arcweave->SetVariable(QuestBindings::PowerCellsVariable, TEXT("0"));

    TestTrue(TEXT("Pickup before acceptance executes its authored denial"), Director->CollectCell(TEXT("cell_a"), Error));
    TestEqual(TEXT("Pickup uses its dedicated prerequisite response"), Director->GetCurrentElementId(), FString(QuestBindings::PickupTerminalRequiredElement));
    TestTrue(TEXT("Denied pickup is a valid narrative interaction"), Error.IsEmpty());
    TestEqual(TEXT("Denied pickup cannot increase the count"), Director->GetPowerCellCount(), 0);
    TestFalse(TEXT("Denied pickup leaves the physical cell available"), Director->HasCollectedCell(TEXT("cell_a")));
    TestEqual(TEXT("Denied pickup does not dispatch collect_cell"), PickupCommands, 0);
    TestTrue(TEXT("Exit before power executes the authored denial"), Director->ReachExit(Error));
    TestEqual(TEXT("Early exit reaches the denial element"), Director->GetCurrentElementId(), FString(QuestBindings::ExitDeniedElement));
    TestFalse(TEXT("Early exit does not complete the task"), Director->IsQuestCompleted());

    TestTrue(TEXT("Terminal traverses the acceptance graph"), Director->StartQuest(Error));
    TestTrue(TEXT("Acceptance script changes questStarted"), Director->IsQuestStarted());
    TestEqual(TEXT("Accepting the task ends at Start"), Director->GetCurrentElementId(), FString(QuestBindings::StartElement));
    TestEqual(TEXT("Acceptance does not automatically attempt the generator"), Visits(QuestBindings::GeneratorElement), 2);
    TestEqual(TEXT("Collecting objective renders current and required counts"), Director->GetObjective(), FString(TEXT("Collect power cells (0/2), then use the generator.")));
    TestTrue(TEXT("Repeated terminal interaction follows an authored response"), Director->StartQuest(Error));
    TestEqual(TEXT("Repeat acceptance reaches its own response"), Director->GetCurrentElementId(), FString(QuestBindings::TerminalAcceptedElement));
    TestEqual(TEXT("Repeat acceptance does not replay Start"), Visits(QuestBindings::StartElement), 1);

    TestTrue(TEXT("Zero-cell generator attempt executes the missing-cell response"), Director->TryRestorePower(Error));
    TestEqual(TEXT("Zero cells select MissingCells"), Director->GetCurrentElementId(), FString(QuestBindings::MissingCellsElement));
    TestFalse(TEXT("Missing-cell response leaves world power off"), Director->IsPowerRestored());
    TestFalse(TEXT("Missing-cell response leaves the gate closed"), Director->IsGateOpen());

    TestTrue(TEXT("First accepted pickup traverses the action and feedback nodes"), Director->CollectCell(TEXT("cell_a"), Error));
    TestEqual(TEXT("Pickup command updates the narrative count"), Variable(QuestBindings::PowerCellsVariable), FString(TEXT("1")));
    TestTrue(TEXT("Pickup command records the physical cell"), Director->HasCollectedCell(TEXT("cell_a")));
    TestEqual(TEXT("Pickup feedback is rendered after the count update"), Director->GetStatus(), FString(TEXT("Collected a power cell (1/2).")));
    TestEqual(TEXT("Pickup ends at its feedback node"), Director->GetCurrentElementId(), FString(QuestBindings::PickupCollectedElement));
    TestEqual(TEXT("One cell keeps the collecting presentation"), Director->GetPresentationElementId(), FString(QuestBindings::PresentationCollectingElement));
    TestEqual(TEXT("Collecting objective updates with the current count"), Director->GetObjective(), FString(TEXT("Collect power cells (1/2), then use the generator.")));
    TestEqual(TEXT("Exactly one physical pickup command was issued"), PickupCommands, 1);
    CheckCachedReads(TEXT("Collecting state"));

    TestTrue(TEXT("Duplicate pickup executes authored feedback"), Director->CollectCell(TEXT("cell_a"), Error));
    TestEqual(TEXT("Duplicate uses the duplicate response"), Director->GetCurrentElementId(), FString(QuestBindings::DuplicatePickupElement));
    TestTrue(TEXT("Duplicate is not an integration error"), Error.IsEmpty());
    TestEqual(TEXT("Duplicate cannot increase the count"), Director->GetPowerCellCount(), 1);
    TestEqual(TEXT("Duplicate cannot reissue collect_cell"), PickupCommands, 1);
    TestEqual(TEXT("Duplicate does not enter the collected feedback node"), Visits(QuestBindings::PickupCollectedElement), 1);
    TestTrue(TEXT("One-cell generator attempt reevaluates the condition"), Director->TryRestorePower(Error));
    TestEqual(TEXT("One cell still selects MissingCells"), Director->GetCurrentElementId(), FString(QuestBindings::MissingCellsElement));

    TestTrue(TEXT("Second unique pickup reaches the requirement"), Director->CollectCell(TEXT("cell_b"), Error));
    TestEqual(TEXT("Both pickups are reflected in narrative state"), Director->GetPowerCellCount(), 2);
    TestEqual(TEXT("Second pickup feedback reads the updated count"), Director->GetStatus(), FString(TEXT("Collected a power cell (2/2).")));
    TestEqual(TEXT("Required count selects the authored ready presentation"), Director->GetPresentationElementId(), FString(QuestBindings::PresentationReadyElement));
    TestEqual(TEXT("Ready objective is authored text"), Director->GetObjective(), FString(TEXT("Return to the generator and restore power.")));
    const FString ReadyPrompt = Director->GetPresentationText(TEXT("generator_prompt"));
    TestFalse(TEXT("Ready generator prompt is supplied by the graph"), ReadyPrompt.IsEmpty());

    TestTrue(TEXT("Generator succeeds after the required pickups"), Director->TryRestorePower(Error));
    TestEqual(TEXT("Success follows the authored connection"), Director->GetCurrentElementId(), FString(QuestBindings::SuccessElement));
    TestEqual(TEXT("Success updates the narrative power flag"), Variable(QuestBindings::PowerRestoredVariable), FString(TEXT("true")));
    TestTrue(TEXT("restore_power turns world power on"), Director->IsPowerRestored());
    TestTrue(TEXT("open_gate opens the world gate"), Director->IsGateOpen());
    TestFalse(TEXT("Restoring power does not complete the task before reaching the exit"), Director->IsQuestCompleted());
    TestEqual(TEXT("Completion variable remains false while the exit is available"), Variable(QuestBindings::QuestCompletedVariable), FString(TEXT("false")));
    TestEqual(TEXT("Power and completion select distinct presentation stages"), Director->GetPresentationElementId(), FString(QuestBindings::PresentationPoweredElement));
    TestEqual(TEXT("Powered objective directs the player to the exit"), Director->GetObjective(), FString(TEXT("Power restored. Walk through the open gate.")));
    TestEqual(TEXT("Power command runs once"), PowerCommands, 1);
    TestEqual(TEXT("Gate command runs once"), GateCommands, 1);
    CheckCachedReads(TEXT("Powered state"));

    TestTrue(TEXT("Repeated generator use executes an authored already-online response"), Director->TryRestorePower(Error));
    TestEqual(TEXT("Repeated generator reaches AlreadyOnline"), Director->GetCurrentElementId(), FString(QuestBindings::AlreadyOnlineElement));
    TestEqual(TEXT("Repeated generator does not reenter success"), Visits(QuestBindings::SuccessElement), 1);
    TestEqual(TEXT("Repeated generator does not reissue power"), PowerCommands, 1);
    TestEqual(TEXT("Repeated generator does not reissue gate opening"), GateCommands, 1);
    TestTrue(TEXT("Terminal can describe the powered state"), Director->StartQuest(Error));
    TestEqual(TEXT("Powered terminal uses its authored response"), Director->GetCurrentElementId(), FString(QuestBindings::TerminalPoweredElement));

    TestTrue(TEXT("Reaching the exit executes the completion graph"), Director->ReachExit(Error));
    TestTrue(TEXT("Exit completion updates questCompleted"), Director->IsQuestCompleted());
    TestEqual(TEXT("Only the completion node ends the quest"), Director->GetCurrentElementId(), FString(QuestBindings::CompletedElement));
    TestEqual(TEXT("Completed presentation is distinct from powered"), Director->GetPresentationElementId(), FString(QuestBindings::PresentationCompletedElement));
    TestEqual(TEXT("Completed objective comes from the presentation node"), Director->GetObjective(), FString(TEXT("Task complete. You reached the exit.")));
    TestEqual(TEXT("Completion node is visited once"), Visits(QuestBindings::CompletedElement), 1);
    TestTrue(TEXT("Repeated exit entry executes authored feedback"), Director->ReachExit(Error));
    TestEqual(TEXT("Repeated exit reaches its own response"), Director->GetCurrentElementId(), FString(QuestBindings::ExitAlreadyCompletedElement));
    TestEqual(TEXT("Repeated exit does not replay the completion node"), Visits(QuestBindings::CompletedElement), 1);
    TestTrue(TEXT("Terminal can describe the completed state"), Director->StartQuest(Error));
    TestEqual(TEXT("Completed terminal uses its authored response"), Director->GetCurrentElementId(), FString(QuestBindings::TerminalCompletedElement));
    CheckCachedReads(TEXT("Completed state"));
    CheckCatalogValues(TEXT("Completed state"));

    // Runtime UI changes become visible at the same event boundary as the presentation graph.
    const FString StationNameVariableId = UIVariableIds.FindChecked(TEXT("station_name"));
    const FString UpdatedStationName(TEXT("RELAY 08 / TEST"));
    Arcweave->SetVariable(StationNameVariableId, UpdatedStationName);
    TestEqual(TEXT("SetVariable updates the scoped UI runtime value"),
        Arcweave->GetArcweaveProjectData().CurrentVars.FindChecked(StationNameVariableId).Value, UpdatedStationName);
    TestEqual(TEXT("Reading a catalog getter alone preserves the previously cached UI text"),
        Director->GetCatalogText(TEXT("station_name")), AuthoredCatalog.FindChecked(TEXT("station_name")));
    CheckCachedReads(TEXT("UI change waiting for an event"));
    TestTrue(TEXT("A normal repeated terminal event refreshes the UI cache"), Director->StartQuest(Error));
    TestEqual(TEXT("The event refresh reads the current UI variable rather than the authored attribute"),
        Director->GetCatalogText(TEXT("station_name")), UpdatedStationName);
    const FArcweaveProjectData UpdatedUIState = Arcweave->GetArcweaveProjectData();
    TestEqual(TEXT("A runtime UI change preserves the authored variable default"),
        UpdatedUIState.CurrentVars.FindChecked(StationNameVariableId).DefaultValue, AuthoredCatalog.FindChecked(TEXT("station_name")));
    const FArcweaveComponentData* UpdatedUIComponent = UpdatedUIState.Components.FindByPredicate(
        [](const FArcweaveComponentData& Component) { return Component.Id == QuestBindings::UIComponent; });
    if (!TestNotNull(TEXT("The UI component remains available after a runtime change"), UpdatedUIComponent)) return false;
    const FArcweaveAttributeData* AuthoredStationAttribute = UpdatedUIComponent->Attributes.FindByPredicate(
        [&StationNameVariableId](const FArcweaveAttributeData& Attribute) { return Attribute.Id == StationNameVariableId; });
    if (!TestNotNull(TEXT("The authored station-name attribute remains available"), AuthoredStationAttribute)) return false;
    TestEqual(TEXT("Runtime UI changes do not rewrite the authored component attribute"),
        AuthoredStationAttribute->Value.Data, AuthoredCatalog.FindChecked(TEXT("station_name")));
    CheckCatalogValues(TEXT("UI change refreshed"));
    CheckCachedReads(TEXT("UI change refreshed"));

    if (!CheckRestart()) return false;
    TestEqual(TEXT("Restart cannot replay pickup commands"), PickupCommands, 2);
    TestEqual(TEXT("Restart cannot replay completion commands"), PowerCommands + GateCommands, 2);

    // Changing the authored requirement changes both the branch result and its displayed count.
    Arcweave->SetVariable(QuestBindings::RequiredPowerCellsVariable, TEXT("1"));
    TestTrue(TEXT("One-cell variant still begins through the terminal graph"), Director->StartQuest(Error));
    TestEqual(TEXT("Required count getter reads the authored variable"), Director->GetRequiredPowerCellCount(), 1);
    TestEqual(TEXT("Objective renders the changed requirement"), Director->GetObjective(), FString(TEXT("Collect power cells (0/1), then use the generator.")));
    TestTrue(TEXT("One-cell variant allows either physical cell"), Director->CollectCell(TEXT("cell_b"), Error));
    TestEqual(TEXT("Pickup feedback renders the changed requirement"), Director->GetStatus(), FString(TEXT("Collected a power cell (1/1).")));
    TestEqual(TEXT("One pickup now selects Ready"), Director->GetPresentationElementId(), FString(QuestBindings::PresentationReadyElement));
    TestEqual(TEXT("One-cell readiness selects the same authored interaction prompt"), Director->GetPresentationText(TEXT("generator_prompt")), ReadyPrompt);
    TestTrue(TEXT("One-cell variant still handles duplicate feedback"), Director->CollectCell(TEXT("cell_b"), Error));
    TestEqual(TEXT("Duplicate preserves the one-cell requirement state"), Director->GetPowerCellCount(), 1);
    TestEqual(TEXT("Only one additional pickup command was dispatched"), PickupCommands, 3);
    TestTrue(TEXT("One-cell requirement permits power restoration"), Director->TryRestorePower(Error));
    TestTrue(TEXT("One-cell variant opens the gate"), Director->IsGateOpen());
    TestFalse(TEXT("One-cell variant also waits for the exit to complete"), Director->IsQuestCompleted());
    TestTrue(TEXT("One-cell variant completes at the exit"), Director->ReachExit(Error));
    TestTrue(TEXT("One-cell exit marks completion"), Director->IsQuestCompleted());
    TestEqual(TEXT("Each session issues exactly one power command"), PowerCommands, 2);
    TestEqual(TEXT("Each session issues exactly one gate command"), GateCommands, 2);
    CheckCachedReads(TEXT("One-cell completed state"));
    if (!CheckRestart()) return false;
    return true;
}

#endif
