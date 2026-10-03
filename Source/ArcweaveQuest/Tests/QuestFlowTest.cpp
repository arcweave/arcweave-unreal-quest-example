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

    const TSet<FName> RequiredUIFields = {
        TEXT("hud.brand"), TEXT("hud.station_name"), TEXT("hud.mission_tagline"), TEXT("hud.cells_label"),
        TEXT("hud.station_footer"), TEXT("world_text.terminal_label"), TEXT("world_text.cell_a_label"),
        TEXT("world_text.cell_b_label"), TEXT("world_text.sign_station"), TEXT("world_text.sign_distribution"),
        TEXT("world_text.sign_gate"), TEXT("world_text.sign_exit"), TEXT("quest_ui.mission_heading"),
        TEXT("quest_ui.grid_status"), TEXT("quest_ui.terminal_prompt"), TEXT("quest_ui.cell_prompt"),
        TEXT("quest_ui.generator_prompt"), TEXT("quest_ui.generator_label"), TEXT("quest_ui.gate_label")
    };
    struct FUIComponentScope
    {
        const TCHAR* Id;
        const TCHAR* Scope;
        int32 Fields;
    };
    const FUIComponentScope UIComponents[] = {
        {QuestBindings::HUDTextComponent, TEXT("hud"), 5},
        {QuestBindings::WorldTextComponent, TEXT("world_text"), 7},
        {QuestBindings::QuestUIComponent, TEXT("quest_ui"), 7}
    };
    TMap<FName, FString> AuthoredUI;
    TMap<FName, FString> UIVariableIds;
    for (const FUIComponentScope& Expected : UIComponents)
    {
        const FArcweaveComponentData* Component = InitialState.Components.FindByPredicate(
            [&Expected](const FArcweaveComponentData& Candidate) { return Candidate.Id == Expected.Id; });
        if (!TestNotNull(TEXT("The UI component is imported: ") + FString(Expected.Scope), Component)) return false;
        TestEqual(TEXT("The component has its authored scope: ") + FString(Expected.Scope), Component->CustomId, FString(Expected.Scope));
        TestEqual(TEXT("The component contains its required fields: ") + FString(Expected.Scope), Component->Attributes.Num(), Expected.Fields);
        for (const FArcweaveAttributeData& Attribute : Component->Attributes)
        {
            const FName Field(*(Component->CustomId + TEXT(".") + Attribute.CustomId));
            const FArcweaveVariable* RuntimeVariable = InitialState.CurrentVars.Find(Attribute.Id);
            if (!TestTrue(TEXT("UI attribute has a required qualified field: ") + Field.ToString(), RequiredUIFields.Contains(Field))
                || !TestFalse(TEXT("UI attribute field is unique: ") + Field.ToString(), UIVariableIds.Contains(Field))
                || !TestFalse(TEXT("Authored UI text is nonempty: ") + Field.ToString(), Attribute.Value.Data.IsEmpty())
                || !TestNotNull(TEXT("UI attribute imports a runtime variable: ") + Field.ToString(), RuntimeVariable))
            {
                return false;
            }
            UIVariableIds.Add(Field, Attribute.Id);
            AuthoredUI.Add(Field, Attribute.Value.Data);
            TestEqual(TEXT("UI attribute owner is the component: ") + Field.ToString(), Attribute.cId, Component->Id);
            TestEqual(TEXT("UI attribute owner type is components: ") + Field.ToString(), Attribute.cType, FString(TEXT("components")));
            TestEqual(TEXT("UI variable uses the attribute custom ID: ") + Field.ToString(), RuntimeVariable->Name, Attribute.CustomId);
            TestEqual(TEXT("UI variable has string type: ") + Field.ToString(), RuntimeVariable->Type, FString(TEXT("string")));
            TestEqual(TEXT("UI variable belongs to components: ") + Field.ToString(), RuntimeVariable->cType, FString(TEXT("components")));
            TestEqual(TEXT("UI variable has its component scope: ") + Field.ToString(), RuntimeVariable->Scope, Component->CustomId);
            TestTrue(TEXT("UI variable retains its authored default: ") + Field.ToString(), RuntimeVariable->bHasDefaultValue);
            TestEqual(TEXT("UI variable default matches authored text: ") + Field.ToString(), RuntimeVariable->DefaultValue, Attribute.Value.Data);
            if (Component->CustomId != TEXT("quest_ui"))
            {
                TestEqual(TEXT("Static UI starts with its authored value: ") + Field.ToString(), RuntimeVariable->Value, Attribute.Value.Data);
            }
        }
    }
    if (!TestEqual(TEXT("Every UI field has its own runtime variable"), UIVariableIds.Num(), RequiredUIFields.Num())) return false;
    int32 GlobalVariableCount = 0;
    for (const auto& Pair : InitialState.CurrentVars)
    {
        GlobalVariableCount += Pair.Value.cType == TEXT("global") && Pair.Value.Scope.IsEmpty() ? 1 : 0;
    }
    TestEqual(TEXT("The five quest variables retain global scope"), GlobalVariableCount, 5);
    for (const FUIComponentScope& Expected : UIComponents)
    {
        int32 ScopedVariableCount = 0;
        for (const auto& Pair : InitialState.CurrentVars)
        {
            ScopedVariableCount += Pair.Value.cType == TEXT("components") && Pair.Value.Scope == Expected.Scope ? 1 : 0;
        }
        TestEqual(TEXT("Runtime UI variable count for ") + FString(Expected.Scope), ScopedVariableCount, Expected.Fields);
    }
    const FArcweaveComponentData* EventComponent = InitialState.Components.FindByPredicate(
        [](const FArcweaveComponentData& Component) { return Component.Id == QuestBindings::GameEventComponent; });
    if (!TestNotNull(TEXT("The Game event component is imported"), EventComponent)) return false;
    TestEqual(TEXT("Event inputs have the game_event scope"), EventComponent->CustomId, FString(TEXT("game_event")));
    TestEqual(TEXT("The event component has exactly two inputs"), EventComponent->Attributes.Num(), 2);
    struct FEventInput
    {
        const TCHAR* Id;
        const TCHAR* Name;
        const TCHAR* Type;
        const TCHAR* Default;
    };
    const FEventInput EventInputs[] = {
        {QuestBindings::EventTypeAttribute, TEXT("type"), TEXT("string"), TEXT("")},
        {QuestBindings::CellAlreadyCollectedAttribute, TEXT("cell_already_collected"), TEXT("boolean"), TEXT("false")}
    };
    for (const FEventInput& Input : EventInputs)
    {
        const FString Label = TEXT("game_event.") + FString(Input.Name);
        const FArcweaveAttributeData* Attribute = EventComponent->Attributes.FindByPredicate(
            [&Input](const FArcweaveAttributeData& Candidate) { return Candidate.Id == Input.Id; });
        const FArcweaveVariable* RuntimeVariable = InitialState.CurrentVars.Find(Input.Id);
        if (!TestNotNull(Label + TEXT(" is an authored event attribute"), Attribute)
            || !TestNotNull(Label + TEXT(" imports as a runtime variable"), RuntimeVariable)) return false;
        TestEqual(Label + TEXT(" has its authored name"), Attribute->CustomId, FString(Input.Name));
        TestEqual(Label + TEXT(" belongs to the event component"), Attribute->cId, EventComponent->Id);
        TestEqual(Label + TEXT(" has component ownership"), Attribute->cType, FString(TEXT("components")));
        TestEqual(Label + TEXT(" has the required runtime name"), RuntimeVariable->Name, FString(Input.Name));
        TestEqual(Label + TEXT(" has the required type"), RuntimeVariable->Type, FString(Input.Type));
        TestEqual(Label + TEXT(" has component runtime ownership"), RuntimeVariable->cType, FString(TEXT("components")));
        TestEqual(Label + TEXT(" has the required runtime scope"), RuntimeVariable->Scope, EventComponent->CustomId);
        TestTrue(Label + TEXT(" retains an authored default"), RuntimeVariable->bHasDefaultValue);
        TestEqual(Label + TEXT(" has the required default"), RuntimeVariable->DefaultValue, FString(Input.Default));
        TestEqual(Label + TEXT(" starts at its default"), RuntimeVariable->Value, FString(Input.Default));
    }
    const auto CheckUIValues = [this, Director, Arcweave, &UIVariableIds](const TCHAR* Stage)
    {
        const FArcweaveProjectData State = Arcweave->GetArcweaveProjectData();
        for (const auto& Pair : UIVariableIds)
        {
            TestEqual(FString(Stage) + TEXT(": cached UI matches ") + Pair.Key.ToString(),
                Director->GetUIText(Pair.Key), State.CurrentVars.FindChecked(Pair.Value).Value);
        }
    };
    const auto CheckPresentation = [this, Director, &AuthoredUI, &CheckUIValues](const TCHAR* ElementId)
    {
        TMap<FName, FString> ExpectedFields;
        for (const auto& Pair : AuthoredUI)
        {
            if (Pair.Key.ToString().StartsWith(TEXT("quest_ui."))) ExpectedFields.Add(Pair.Key, Pair.Value);
        }
        const FString Stage(ElementId);
        if (Stage == QuestBindings::PresentationUnacceptedElement)
        {
            ExpectedFields.Add(TEXT("quest_ui.terminal_prompt"), TEXT("Read station terminal"));
            ExpectedFields.Add(TEXT("quest_ui.cell_prompt"), TEXT("Read the station terminal first"));
        }
        else if (Stage == QuestBindings::PresentationReadyElement)
        {
            ExpectedFields.Add(TEXT("quest_ui.generator_prompt"), TEXT("Install cells and restore power"));
        }
        else if (Stage == QuestBindings::PresentationPoweredElement || Stage == QuestBindings::PresentationCompletedElement)
        {
            ExpectedFields.Add(TEXT("quest_ui.grid_status"), TEXT("GRID ONLINE"));
            ExpectedFields.Add(TEXT("quest_ui.generator_prompt"), TEXT("Review running generator"));
            ExpectedFields.Add(TEXT("quest_ui.generator_label"), TEXT("GENERATOR / ONLINE"));
            ExpectedFields.Add(TEXT("quest_ui.gate_label"), TEXT("GATE 01 / ACCESS GRANTED"));
            if (Stage == QuestBindings::PresentationCompletedElement)
            {
                ExpectedFields.Add(TEXT("quest_ui.mission_heading"), TEXT("MISSION COMPLETE"));
            }
        }
        TestEqual(TEXT("Presentation selects the expected authored leaf"), Director->GetPresentationElementId(), Stage);
        for (const auto& Pair : ExpectedFields)
        {
            TestEqual(TEXT("Presentation renders the expected value for ") + Pair.Key.ToString(), Director->GetUIText(Pair.Key), Pair.Value);
        }
        CheckUIValues(*Stage);
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
    const auto CheckEvent = [this, Arcweave, &Variable](const TCHAR* EventType,
        const TFunction<bool()>& Action, bool bExpectedDuplicate = false)
    {
        const FArcweaveProjectData Before = Arcweave->GetArcweaveProjectData();
        const bool bResult = Action();
        const FArcweaveProjectData After = Arcweave->GetArcweaveProjectData();
        const FString Prefix = FString(EventType) + TEXT(": ");
        TestEqual(Prefix + TEXT("executes the shared world-event entry exactly once"),
            After.Visits.FindChecked(QuestBindings::EventEntryElement), Before.Visits.FindChecked(QuestBindings::EventEntryElement) + 1);
        TestEqual(Prefix + TEXT("sets the event type before routing"), Variable(QuestBindings::EventTypeAttribute), FString(EventType));
        TestEqual(Prefix + TEXT("replaces pickup context before routing"), Variable(QuestBindings::CellAlreadyCollectedAttribute),
            FString(bExpectedDuplicate ? TEXT("true") : TEXT("false")));
        const TPair<const TCHAR*, const TCHAR*> Lanes[] = {
            {TEXT("use_terminal"), QuestBindings::TerminalEntryElement},
            {TEXT("collect_cell"), QuestBindings::PickupEntryElement},
            {TEXT("check_generator"), QuestBindings::GeneratorElement},
            {TEXT("enter_exit"), QuestBindings::ExitEntryElement}
        };
        for (const auto& Lane : Lanes)
        {
            const int32 ExpectedIncrement = FString(EventType) == Lane.Key ? 1 : 0;
            TestEqual(Prefix + TEXT("routes only to the selected lane: ") + Lane.Key,
                After.Visits.FindChecked(Lane.Value), Before.Visits.FindChecked(Lane.Value) + ExpectedIncrement);
        }
        return bResult;
    };
    const auto StartQuest = [Director, &Error, &CheckEvent]
    {
        return CheckEvent(TEXT("use_terminal"), [Director, &Error] { return Director->StartQuest(Error); });
    };
    const auto AttemptGenerator = [Director, &Error, &CheckEvent]
    {
        return CheckEvent(TEXT("check_generator"), [Director, &Error] { return Director->TryRestorePower(Error); });
    };
    const auto AttemptPickup = [Director, &Error, &CheckEvent](FName CellId, bool bExpectedDuplicate = false)
    {
        return CheckEvent(TEXT("collect_cell"), [Director, &Error, CellId] { return Director->CollectCell(CellId, Error); }, bExpectedDuplicate);
    };
    const auto ReachExit = [Director, &Error, &CheckEvent]
    {
        return CheckEvent(TEXT("enter_exit"), [Director, &Error] { return Director->ReachExit(Error); });
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
            Catalog.Add(Pair.Key, Director->GetUIText(Pair.Key));
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
                Director->GetUIText(Pair.Key);
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
            TestEqual(Prefix + TEXT("cached catalog is unchanged: ") + Pair.Key.ToString(), Director->GetUIText(Pair.Key), Pair.Value);
        }
        TestEqual(Prefix + TEXT("objective is unchanged"), Director->GetObjective(), Objective);
        TestEqual(Prefix + TEXT("status is unchanged"), Director->GetStatus(), Status);
    };
    const auto CheckRestart = [this, Director, Arcweave, &InitialState, &Visits, &Error, &UIVariableIds, &CheckUIValues]()
    {
        if (!TestTrue(TEXT("Restart reloads defaults and computes the initial presentation"), Director->StartNewGame(Error)))
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
        TestEqual(TEXT("Restart does not simulate a world event"), Visits(QuestBindings::EventEntryElement), 0);
        TestTrue(TEXT("Restart leaves the gameplay cursor empty"), Director->GetCurrentElementId().IsEmpty());
        TestTrue(TEXT("Restart clears previous gameplay feedback"), Director->GetStatus().IsEmpty());
        TestEqual(TEXT("Restart clears the last event type"), Restarted.CurrentVars.FindChecked(QuestBindings::EventTypeAttribute).Value, FString());
        TestEqual(TEXT("Restart clears duplicate pickup context"), Restarted.CurrentVars.FindChecked(QuestBindings::CellAlreadyCollectedAttribute).Value, FString(TEXT("false")));
        TestEqual(TEXT("Restart selects the unaccepted presentation"), Director->GetPresentationElementId(), FString(QuestBindings::PresentationUnacceptedElement));
        TestTrue(TEXT("Restart visits match a newly initialized session, not an all-zero map"),
            Restarted.Visits.OrderIndependentCompareEqual(InitialState.Visits));
        TestEqual(TEXT("Restart preserves quest, UI, and event input definitions"), Restarted.CurrentVars.Num(), InitialState.CurrentVars.Num());
        for (const auto& Pair : InitialState.CurrentVars)
        {
            TestEqual(TEXT("Restart restores authored value ") + Pair.Key, Restarted.CurrentVars.FindChecked(Pair.Key).Value, Pair.Value.Value);
        }
        for (const auto& Pair : UIVariableIds)
        {
            TestEqual(TEXT("Restart restores initialized UI text: ") + Pair.Key.ToString(),
                Director->GetUIText(Pair.Key), InitialState.CurrentVars.FindChecked(Pair.Value).Value);
        }
        CheckUIValues(TEXT("Restarted state"));
        return true;
    };

    TestEqual(TEXT("Five globals, nineteen UI strings, and two event inputs are imported"), InitialState.CurrentVars.Num(), 26);
    TestFalse(TEXT("Initialization does not accept the task"), Director->IsQuestStarted());
    TestFalse(TEXT("Initialization does not complete the task"), Director->IsQuestCompleted());
    TestEqual(TEXT("Initial required cell count comes from the export"), Director->GetRequiredPowerCellCount(), 2);
    TestEqual(TEXT("Startup does not enter the world-event graph"), Visits(QuestBindings::EventEntryElement), 0);
    TestTrue(TEXT("Startup has no gameplay cursor"), Director->GetCurrentElementId().IsEmpty());
    TestTrue(TEXT("Startup has no interaction feedback"), Director->GetStatus().IsEmpty());
    TestEqual(TEXT("Unaccepted presentation is selected by the graph"), Director->GetPresentationElementId(), FString(QuestBindings::PresentationUnacceptedElement));
    TestEqual(TEXT("Authored initial objective is cached"), Director->GetObjective(), FString(TEXT("Use the terminal to begin.")));
    CheckUIValues(TEXT("Initialized state"));
    TestFalse(TEXT("Initial terminal prompt is available"), Director->GetUIText(TEXT("quest_ui.terminal_prompt")).IsEmpty());
    CheckCachedReads(TEXT("Initial state"));
    CheckPresentation(QuestBindings::PresentationUnacceptedElement);

    TestTrue(TEXT("A generator attempt before acceptance executes authored guidance"), AttemptGenerator());
    TestTrue(TEXT("Narrative denial is not an integration error"), Error.IsEmpty());
    TestEqual(TEXT("Generator enters the terminal-required response"), Director->GetCurrentElementId(), FString(QuestBindings::TerminalRequiredElement));
    TestTrue(TEXT("Generator guidance is the authored authorization text"), Director->GetStatus().Contains(TEXT("The generator is waiting for authorization.")));
    TestEqual(TEXT("Preterminal attempt visits the generator"), Visits(QuestBindings::GeneratorElement), 1);
    TestFalse(TEXT("Generator guidance leaves acceptance false"), Director->IsQuestStarted());
    TestFalse(TEXT("Generator guidance leaves power off"), Director->IsPowerRestored());

    Arcweave->SetVariable(QuestBindings::PowerCellsVariable, TEXT("2"));
    TestTrue(TEXT("Authored acceptance requirement still applies with enough cells"), AttemptGenerator());
    TestEqual(TEXT("Acceptance takes precedence over the required count"), Director->GetCurrentElementId(), FString(QuestBindings::TerminalRequiredElement));
    TestEqual(TEXT("Premature two-cell attempt does not execute success"), Visits(QuestBindings::SuccessElement), 0);
    TestEqual(TEXT("Premature attempts execute no world commands"), PowerCommands + GateCommands + PickupCommands, 0);
    Arcweave->SetVariable(QuestBindings::PowerCellsVariable, TEXT("0"));

    TestTrue(TEXT("Pickup before acceptance executes its authored denial"), AttemptPickup(TEXT("cell_a")));
    TestEqual(TEXT("Pickup uses its dedicated prerequisite response"), Director->GetCurrentElementId(), FString(QuestBindings::PickupTerminalRequiredElement));
    TestTrue(TEXT("Denied pickup is a valid narrative interaction"), Error.IsEmpty());
    TestEqual(TEXT("Denied pickup cannot increase the count"), Director->GetPowerCellCount(), 0);
    TestFalse(TEXT("Denied pickup leaves the physical cell available"), Director->HasCollectedCell(TEXT("cell_a")));
    TestEqual(TEXT("Denied pickup does not dispatch collect_cell"), PickupCommands, 0);
    TestTrue(TEXT("Exit before power executes the authored denial"), ReachExit());
    TestEqual(TEXT("Early exit reaches the denial element"), Director->GetCurrentElementId(), FString(QuestBindings::ExitDeniedElement));
    TestFalse(TEXT("Early exit does not complete the task"), Director->IsQuestCompleted());

    TestTrue(TEXT("Terminal traverses the acceptance graph"), StartQuest());
    TestTrue(TEXT("Acceptance script changes questStarted"), Director->IsQuestStarted());
    TestEqual(TEXT("Accepting the task ends at Start"), Director->GetCurrentElementId(), FString(QuestBindings::StartElement));
    TestEqual(TEXT("Acceptance does not automatically attempt the generator"), Visits(QuestBindings::GeneratorElement), 2);
    TestEqual(TEXT("Collecting objective renders current and required counts"), Director->GetObjective(), FString(TEXT("Collect power cells (0/2), then use the generator.")));
    TestTrue(TEXT("Repeated terminal interaction follows an authored response"), StartQuest());
    TestEqual(TEXT("Repeat acceptance reaches its own response"), Director->GetCurrentElementId(), FString(QuestBindings::TerminalAcceptedElement));
    TestEqual(TEXT("Repeat acceptance does not replay Start"), Visits(QuestBindings::StartElement), 1);

    TestTrue(TEXT("Zero-cell generator attempt executes the missing-cell response"), AttemptGenerator());
    TestEqual(TEXT("Zero cells select MissingCells"), Director->GetCurrentElementId(), FString(QuestBindings::MissingCellsElement));
    TestFalse(TEXT("Missing-cell response leaves world power off"), Director->IsPowerRestored());
    TestFalse(TEXT("Missing-cell response leaves the gate closed"), Director->IsGateOpen());

    TestTrue(TEXT("First accepted pickup traverses the action and feedback nodes"), AttemptPickup(TEXT("cell_a")));
    TestEqual(TEXT("Pickup command updates the narrative count"), Variable(QuestBindings::PowerCellsVariable), FString(TEXT("1")));
    TestTrue(TEXT("Pickup command records the physical cell"), Director->HasCollectedCell(TEXT("cell_a")));
    TestEqual(TEXT("Pickup feedback is rendered after the count update"), Director->GetStatus(), FString(TEXT("Collected a power cell (1/2).")));
    TestEqual(TEXT("Pickup ends at its feedback node"), Director->GetCurrentElementId(), FString(QuestBindings::PickupCollectedElement));
    TestEqual(TEXT("One cell keeps the collecting presentation"), Director->GetPresentationElementId(), FString(QuestBindings::PresentationCollectingElement));
    TestEqual(TEXT("Collecting objective updates with the current count"), Director->GetObjective(), FString(TEXT("Collect power cells (1/2), then use the generator.")));
    TestEqual(TEXT("Exactly one physical pickup command was issued"), PickupCommands, 1);
    CheckCachedReads(TEXT("Collecting state"));
    CheckPresentation(QuestBindings::PresentationCollectingElement);

    TestTrue(TEXT("Duplicate pickup executes authored feedback"), AttemptPickup(TEXT("cell_a"), true));
    TestEqual(TEXT("Duplicate uses the duplicate response"), Director->GetCurrentElementId(), FString(QuestBindings::DuplicatePickupElement));
    TestTrue(TEXT("Duplicate is not an integration error"), Error.IsEmpty());
    TestEqual(TEXT("Duplicate cannot increase the count"), Director->GetPowerCellCount(), 1);
    TestEqual(TEXT("Duplicate cannot reissue collect_cell"), PickupCommands, 1);
    TestEqual(TEXT("Duplicate does not enter the collected feedback node"), Visits(QuestBindings::PickupCollectedElement), 1);
    Arcweave->SetVariable(QuestBindings::QuestStartedVariable, TEXT("false"));
    TestTrue(TEXT("Duplicate detection takes precedence over quest acceptance"), AttemptPickup(TEXT("cell_a"), true));
    TestEqual(TEXT("The pickup branch checks duplicate context before acceptance"),
        Director->GetCurrentElementId(), FString(QuestBindings::DuplicatePickupElement));
    TestEqual(TEXT("Duplicate routing never repeats the pickup action"), PickupCommands, 1);
    Arcweave->SetVariable(QuestBindings::QuestStartedVariable, TEXT("true"));
    TestTrue(TEXT("One-cell generator attempt reevaluates the condition"), AttemptGenerator());
    TestEqual(TEXT("One cell still selects MissingCells"), Director->GetCurrentElementId(), FString(QuestBindings::MissingCellsElement));

    TestTrue(TEXT("Another duplicate request restores true pickup context"), AttemptPickup(TEXT("cell_a"), true));
    TestTrue(TEXT("Second unique pickup reaches the requirement"), AttemptPickup(TEXT("cell_b")));
    TestEqual(TEXT("Both pickups are reflected in narrative state"), Director->GetPowerCellCount(), 2);
    TestEqual(TEXT("Second pickup feedback reads the updated count"), Director->GetStatus(), FString(TEXT("Collected a power cell (2/2).")));
    TestEqual(TEXT("Required count selects the authored ready presentation"), Director->GetPresentationElementId(), FString(QuestBindings::PresentationReadyElement));
    TestEqual(TEXT("Ready objective is authored text"), Director->GetObjective(), FString(TEXT("Return to the generator and restore power.")));
    const FString ReadyPrompt = Director->GetUIText(TEXT("quest_ui.generator_prompt"));
    TestFalse(TEXT("Ready generator prompt is supplied by the graph"), ReadyPrompt.IsEmpty());
    CheckPresentation(QuestBindings::PresentationReadyElement);

    TestTrue(TEXT("Generator succeeds after the required pickups"), AttemptGenerator());
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
    CheckPresentation(QuestBindings::PresentationPoweredElement);

    TestTrue(TEXT("Repeated generator use executes an authored already-online response"), AttemptGenerator());
    TestEqual(TEXT("Repeated generator reaches AlreadyOnline"), Director->GetCurrentElementId(), FString(QuestBindings::AlreadyOnlineElement));
    TestEqual(TEXT("Repeated generator does not reenter success"), Visits(QuestBindings::SuccessElement), 1);
    TestEqual(TEXT("Repeated generator does not reissue power"), PowerCommands, 1);
    TestEqual(TEXT("Repeated generator does not reissue gate opening"), GateCommands, 1);
    TestTrue(TEXT("Terminal can describe the powered state"), StartQuest());
    TestEqual(TEXT("Powered terminal uses its authored response"), Director->GetCurrentElementId(), FString(QuestBindings::TerminalPoweredElement));

    TestTrue(TEXT("Reaching the exit executes the completion graph"), ReachExit());
    TestTrue(TEXT("Exit completion updates questCompleted"), Director->IsQuestCompleted());
    TestEqual(TEXT("Only the completion node ends the quest"), Director->GetCurrentElementId(), FString(QuestBindings::CompletedElement));
    TestEqual(TEXT("Completed presentation is distinct from powered"), Director->GetPresentationElementId(), FString(QuestBindings::PresentationCompletedElement));
    TestEqual(TEXT("Completed objective comes from the presentation node"), Director->GetObjective(), FString(TEXT("Task complete. You reached the exit.")));
    TestEqual(TEXT("Completion node is visited once"), Visits(QuestBindings::CompletedElement), 1);
    TestTrue(TEXT("Repeated exit entry executes authored feedback"), ReachExit());
    TestEqual(TEXT("Repeated exit reaches its own response"), Director->GetCurrentElementId(), FString(QuestBindings::ExitAlreadyCompletedElement));
    TestEqual(TEXT("Repeated exit does not replay the completion node"), Visits(QuestBindings::CompletedElement), 1);
    TestTrue(TEXT("Terminal can describe the completed state"), StartQuest());
    TestEqual(TEXT("Completed terminal uses its authored response"), Director->GetCurrentElementId(), FString(QuestBindings::TerminalCompletedElement));
    CheckCachedReads(TEXT("Completed state"));
    CheckPresentation(QuestBindings::PresentationCompletedElement);
    CheckUIValues(TEXT("Completed state"));

    const FArcweaveProjectData BeforeUnknown = Arcweave->GetArcweaveProjectData();
    const int32 CommandsBeforeUnknown = PowerCommands + GateCommands + PickupCommands;
    TestTrue(TEXT("An unknown event is handled by the authored router fallback"),
        CheckEvent(TEXT("unknown_test_event"), [Director, &Error] { return Director->RunEvent(TEXT("unknown_test_event"), Error); }));
    TestTrue(TEXT("The fallback is narrative feedback rather than an integration error"), Error.IsEmpty());
    TestEqual(TEXT("Unknown input selects the dedicated fallback leaf"), Director->GetCurrentElementId(), FString(QuestBindings::UnknownEventElement));
    TestEqual(TEXT("The unknown-event response executes once"), Visits(QuestBindings::UnknownEventElement), 1);
    TestFalse(TEXT("The fallback supplies authored feedback"), Director->GetStatus().IsEmpty());
    TestEqual(TEXT("Unknown events dispatch no world commands"), PowerCommands + GateCommands + PickupCommands, CommandsBeforeUnknown);
    TestTrue(TEXT("Unknown input preserves native power"), Director->IsPowerRestored());
    TestTrue(TEXT("Unknown input preserves the open gate"), Director->IsGateOpen());
    TestTrue(TEXT("Unknown input preserves physical cell A"), Director->HasCollectedCell(TEXT("cell_a")));
    TestTrue(TEXT("Unknown input preserves physical cell B"), Director->HasCollectedCell(TEXT("cell_b")));
    const FArcweaveProjectData AfterUnknown = Arcweave->GetArcweaveProjectData();
    for (const auto& Pair : BeforeUnknown.CurrentVars)
    {
        if (Pair.Value.Scope != TEXT("game_event"))
        {
            TestEqual(TEXT("Unknown events preserve quest and UI state: ") + Pair.Value.Name,
                AfterUnknown.CurrentVars.FindChecked(Pair.Key).Value, Pair.Value.Value);
        }
    }
    CheckPresentation(QuestBindings::PresentationCompletedElement);

    // Runtime UI changes become visible at the same event boundary as the presentation graph.
    const FString StationNameVariableId = UIVariableIds.FindChecked(TEXT("hud.station_name"));
    const FString UpdatedStationName(TEXT("RELAY 08 / TEST"));
    Arcweave->SetVariable(StationNameVariableId, UpdatedStationName);
    TestEqual(TEXT("SetVariable updates the scoped UI runtime value"),
        Arcweave->GetArcweaveProjectData().CurrentVars.FindChecked(StationNameVariableId).Value, UpdatedStationName);
    TestEqual(TEXT("Reading a catalog getter alone preserves the previously cached UI text"),
        Director->GetUIText(TEXT("hud.station_name")), AuthoredUI.FindChecked(TEXT("hud.station_name")));
    CheckCachedReads(TEXT("UI change waiting for an event"));
    TestTrue(TEXT("A normal repeated terminal event refreshes the UI cache"), StartQuest());
    TestEqual(TEXT("The event refresh reads the current UI variable rather than the authored attribute"),
        Director->GetUIText(TEXT("hud.station_name")), UpdatedStationName);
    const FArcweaveProjectData UpdatedUIState = Arcweave->GetArcweaveProjectData();
    TestEqual(TEXT("A runtime UI change preserves the authored variable default"),
        UpdatedUIState.CurrentVars.FindChecked(StationNameVariableId).DefaultValue, AuthoredUI.FindChecked(TEXT("hud.station_name")));
    const FArcweaveComponentData* UpdatedUIComponent = UpdatedUIState.Components.FindByPredicate(
        [](const FArcweaveComponentData& Component) { return Component.Id == QuestBindings::HUDTextComponent; });
    if (!TestNotNull(TEXT("The UI component remains available after a runtime change"), UpdatedUIComponent)) return false;
    const FArcweaveAttributeData* AuthoredStationAttribute = UpdatedUIComponent->Attributes.FindByPredicate(
        [&StationNameVariableId](const FArcweaveAttributeData& Attribute) { return Attribute.Id == StationNameVariableId; });
    if (!TestNotNull(TEXT("The authored station-name attribute remains available"), AuthoredStationAttribute)) return false;
    TestEqual(TEXT("Runtime UI changes do not rewrite the authored component attribute"),
        AuthoredStationAttribute->Value.Data, AuthoredUI.FindChecked(TEXT("hud.station_name")));
    CheckUIValues(TEXT("UI change refreshed"));
    CheckCachedReads(TEXT("UI change refreshed"));

    if (!CheckRestart()) return false;
    TestEqual(TEXT("Restart cannot replay pickup commands"), PickupCommands, 2);
    TestEqual(TEXT("Restart cannot replay completion commands"), PowerCommands + GateCommands, 2);

    // Changing the authored requirement changes both the branch result and its displayed count.
    Arcweave->SetVariable(QuestBindings::RequiredPowerCellsVariable, TEXT("1"));
    TestTrue(TEXT("One-cell variant still begins through the terminal graph"), StartQuest());
    TestEqual(TEXT("Required count getter reads the authored variable"), Director->GetRequiredPowerCellCount(), 1);
    TestEqual(TEXT("Objective renders the changed requirement"), Director->GetObjective(), FString(TEXT("Collect power cells (0/1), then use the generator.")));
    TestTrue(TEXT("One-cell variant allows either physical cell"), AttemptPickup(TEXT("cell_b")));
    TestEqual(TEXT("Pickup feedback renders the changed requirement"), Director->GetStatus(), FString(TEXT("Collected a power cell (1/1).")));
    TestEqual(TEXT("One pickup now selects Ready"), Director->GetPresentationElementId(), FString(QuestBindings::PresentationReadyElement));
    TestEqual(TEXT("One-cell readiness selects the same authored interaction prompt"), Director->GetUIText(TEXT("quest_ui.generator_prompt")), ReadyPrompt);
    TestTrue(TEXT("One-cell variant still handles duplicate feedback"), AttemptPickup(TEXT("cell_b"), true));
    TestEqual(TEXT("Duplicate preserves the one-cell requirement state"), Director->GetPowerCellCount(), 1);
    TestEqual(TEXT("Only one additional pickup command was dispatched"), PickupCommands, 3);
    TestTrue(TEXT("One-cell requirement permits power restoration"), AttemptGenerator());
    TestTrue(TEXT("One-cell variant opens the gate"), Director->IsGateOpen());
    TestFalse(TEXT("One-cell variant also waits for the exit to complete"), Director->IsQuestCompleted());
    TestTrue(TEXT("One-cell variant completes at the exit"), ReachExit());
    TestTrue(TEXT("One-cell exit marks completion"), Director->IsQuestCompleted());
    TestEqual(TEXT("Each session issues exactly one power command"), PowerCommands, 2);
    TestEqual(TEXT("Each session issues exactly one gate command"), GateCommands, 2);
    CheckCachedReads(TEXT("One-cell completed state"));
    CheckPresentation(QuestBindings::PresentationCompletedElement);

    TestTrue(TEXT("A completed quest still routes duplicate pickup feedback through its pickup branch"), AttemptPickup(TEXT("cell_b"), true));
    TestEqual(TEXT("The completed duplicate leaves pickup commands unchanged"), PickupCommands, 3);

    // Presentation can be refreshed independently when external gameplay changes the quest state.
    // Static HUD/world overrides survive; quest_ui is rebuilt from its authored defaults each time.
    const FString WorldLabelVariableId = UIVariableIds.FindChecked(TEXT("world_text.cell_a_label"));
    const FString UpdatedWorldLabel(TEXT("Runtime cell label"));
    Arcweave->SetVariable(StationNameVariableId, UpdatedStationName);
    Arcweave->SetVariable(WorldLabelVariableId, UpdatedWorldLabel);
    for (const auto& Pair : UIVariableIds)
    {
        if (Pair.Key.ToString().StartsWith(TEXT("quest_ui.")))
        {
            Arcweave->SetVariable(Pair.Value, TEXT("Stale presentation text"));
        }
    }
    const auto CheckPresentationRefresh = [this, Director, Arcweave, &Error, &CheckPresentation,
        &CheckCachedReads, &PowerCommands, &GateCommands, &PickupCommands, &PresentationChanges,
        &UpdatedStationName, &UpdatedWorldLabel](const TCHAR* ExpectedLeaf, bool bStarted, bool bPowered,
        bool bCompleted, int32 Cells, int32 Required)
    {
        Arcweave->SetVariable(QuestBindings::QuestStartedVariable, bStarted ? TEXT("true") : TEXT("false"));
        Arcweave->SetVariable(QuestBindings::PowerRestoredVariable, bPowered ? TEXT("true") : TEXT("false"));
        Arcweave->SetVariable(QuestBindings::QuestCompletedVariable, bCompleted ? TEXT("true") : TEXT("false"));
        Arcweave->SetVariable(QuestBindings::PowerCellsVariable, FString::FromInt(Cells));
        Arcweave->SetVariable(QuestBindings::RequiredPowerCellsVariable, FString::FromInt(Required));
        const FArcweaveProjectData Before = Arcweave->GetArcweaveProjectData();
        const FString Cursor = Director->GetCurrentElementId();
        const FString Status = Director->GetStatus();
        const bool bWorldPowered = Director->IsPowerRestored();
        const bool bWorldGateOpen = Director->IsGateOpen();
        const int32 Commands = PowerCommands + GateCommands + PickupCommands;
        const int32 Notifications = PresentationChanges;
        if (!TestTrue(TEXT("Arcscript refreshes the presentation graph"), Director->RefreshPresentation(Error)))
        {
            AddError(Error);
            return false;
        }
        const FArcweaveProjectData After = Arcweave->GetArcweaveProjectData();
        TestEqual(TEXT("Presentation refresh preserves the variable collection"), After.CurrentVars.Num(), Before.CurrentVars.Num());
        for (const auto& Pair : Before.CurrentVars)
        {
            const FArcweaveVariable& Actual = After.CurrentVars.FindChecked(Pair.Key);
            if (Pair.Value.Scope != TEXT("quest_ui"))
            {
                TestEqual(TEXT("Presentation preserves quest globals, event inputs, and static UI: ") + Pair.Value.Scope + TEXT(".") + Pair.Value.Name,
                    Actual.Value, Pair.Value.Value);
            }
            TestEqual(TEXT("Presentation preserves each variable's authored default: ") + Pair.Value.Name,
                Actual.DefaultValue, Pair.Value.DefaultValue);
        }
        TestEqual(TEXT("Presentation never dispatches gameplay commands"), PowerCommands + GateCommands + PickupCommands, Commands);
        TestEqual(TEXT("Presentation refresh alone does not publish gameplay changes"), PresentationChanges, Notifications);
        TestEqual(TEXT("Presentation preserves the gameplay cursor"), Director->GetCurrentElementId(), Cursor);
        TestEqual(TEXT("Presentation preserves gameplay feedback"), Director->GetStatus(), Status);
        TestEqual(TEXT("Presentation preserves native power state"), Director->IsPowerRestored(), bWorldPowered);
        TestEqual(TEXT("Presentation preserves native gate state"), Director->IsGateOpen(), bWorldGateOpen);
        TestEqual(TEXT("Presentation reset preserves a current HUD text override"), Director->GetUIText(TEXT("hud.station_name")), UpdatedStationName);
        TestEqual(TEXT("Presentation reset preserves a current world text override"), Director->GetUIText(TEXT("world_text.cell_a_label")), UpdatedWorldLabel);
        CheckPresentation(ExpectedLeaf);
        CheckCachedReads(TEXT("Presentation backtracking"));
        return true;
    };
    if (!CheckPresentationRefresh(QuestBindings::PresentationCompletedElement, true, true, true, 2, 2)) return false;
    if (!CheckPresentationRefresh(QuestBindings::PresentationPoweredElement, true, true, false, 2, 2)) return false;
    if (!CheckPresentationRefresh(QuestBindings::PresentationUnacceptedElement, false, false, false, 0, 2)) return false;
    if (!CheckPresentationRefresh(QuestBindings::PresentationReadyElement, true, false, false, 2, 2)) return false;
    if (!CheckPresentationRefresh(QuestBindings::PresentationCollectingElement, true, false, false, 1, 2)) return false;
    if (!CheckPresentationRefresh(QuestBindings::PresentationReadyElement, true, false, false, 1, 1)) return false;
    if (!CheckRestart()) return false;
    return true;
}

#endif
