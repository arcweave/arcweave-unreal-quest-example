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
    if (!TestTrue(TEXT("The pinned plugin loads and initializes the local narrative"), Director->StartNewGame(Error)))
    {
        AddError(Error);
        return false;
    }
    const FArcweaveProjectData InitialState = Arcweave->GetArcweaveProjectData();
    const FString EventEntryId = InitialState.StartingElementId;
    if (!TestFalse(TEXT("The imported project supplies its world-event starting element"), EventEntryId.IsEmpty())) return false;
    if (!TestEqual(TEXT("Play Mode and Unreal share one quest board"), InitialState.Boards.Num(), 1)) return false;
    const FArcweaveBoardData& QuestBoard = InitialState.Boards[0];
    const FArcweaveElementData* Menu = QuestBoard.Elements.FindByPredicate(
        [&EventEntryId](const FArcweaveElementData& Element) { return Element.Id == EventEntryId; });
    if (!TestNotNull(TEXT("The starting menu belongs to the shared board"), Menu)) return false;
    if (!TestEqual(TEXT("The starting menu offers five physical interactions and two optional queries"), Menu->Outputs.Num(), 7)) return false;
    const FArcweaveElementData* InventoryEntry = QuestBoard.Elements.FindByPredicate(
        [](const FArcweaveElementData& Element)
        {
            return Element.Attributes.ContainsByPredicate([](const FArcweaveAttributeData& Attribute)
            {
                return Attribute.Name == TEXT("entry_point") && Attribute.Value.Data == TEXT("inventory");
            });
        });
    if (!TestNotNull(TEXT("The inventory query is discoverable from its authored entry-point marker"), InventoryEntry)) return false;
    const FString InventoryEntryId = InventoryEntry->Id;
    TSet<FString> QueryDestinations;
    TSet<FString> QueryJumperIds;
    int32 PhysicalChoices = 0;
    for (const FArcweaveConnectionsData& Choice : Menu->Outputs)
    {
        TestFalse(TEXT("Every Play Mode choice has an authored label"), Choice.Label.IsEmpty());
        if (Choice.TargetType == TEXT("branches"))
        {
            ++PhysicalChoices;
            TestEqual(TEXT("Every physical Play Mode choice enters the same event router"), Choice.Targetid, FString(QuestBindings::EventRouterBranch));
        }
        else
        {
            TestEqual(TEXT("Optional queries are reached through jumpers"), Choice.TargetType, FString(TEXT("jumpers")));
            const FArcweaveJumpersData* QueryJumper = QuestBoard.Jumpers.FindByPredicate(
                [&Choice](const FArcweaveJumpersData& Jumper) { return Jumper.Id == Choice.Targetid; });
            if (!TestNotNull(TEXT("Each optional query has an imported jumper"), QueryJumper)) return false;
            QueryJumperIds.Add(QueryJumper->Id);
            QueryDestinations.Add(QueryJumper->ElementData.Id);
        }
    }
    TestEqual(TEXT("The menu retains all five physical interactions"), PhysicalChoices, 5);
    TestEqual(TEXT("The two query choices use distinct jumpers"), QueryJumperIds.Num(), 2);
    TestEqual(TEXT("Inventory and objective choices lead to separate query flows"), QueryDestinations.Num(), 2);
    TestTrue(TEXT("The menu reaches the marked inventory query"), QueryDestinations.Contains(InventoryEntryId));
    TestTrue(TEXT("The menu reaches the marked objectives query"), QueryDestinations.Contains(Director->PresentationEntryId));
    TestEqual(TEXT("A query precedes gameplay choices, so native events must select the router rather than the first output"),
        Menu->Outputs[0].TargetType, FString(TEXT("jumpers")));
    TSet<FString> ReturnJumperIds;
    for (const TCHAR* LeafId : {QuestBindings::PresentationUnacceptedElement, QuestBindings::PresentationCollectingElement,
        QuestBindings::PresentationReadyElement, QuestBindings::PresentationPoweredElement, QuestBindings::PresentationCompletedElement})
    {
        const FArcweaveElementData* Leaf = QuestBoard.Elements.FindByPredicate(
            [LeafId](const FArcweaveElementData& Element) { return Element.Id == LeafId; });
        if (!TestNotNull(TEXT("The shared board contains each objective leaf"), Leaf)
            || !TestEqual(TEXT("Each objective leaf has one return connection"), Leaf->Outputs.Num(), 1)) return false;
        const FArcweaveJumpersData* ReturnJumper = QuestBoard.Jumpers.FindByPredicate(
            [Leaf](const FArcweaveJumpersData& Jumper) { return Jumper.Id == Leaf->Outputs[0].Targetid; });
        if (!TestNotNull(TEXT("Each objective returns through an imported jumper"), ReturnJumper)) return false;
        TestEqual(TEXT("Each return jumper resolves to the runtime starting menu"), ReturnJumper->ElementData.Id, EventEntryId);
        ReturnJumperIds.Add(ReturnJumper->Id);
    }
    TestEqual(TEXT("The five objective leaves have separate return jumpers"), ReturnJumperIds.Num(), 5);
    if (!TestEqual(TEXT("Inventory has one return connection"), InventoryEntry->Outputs.Num(), 1)) return false;
    const FArcweaveJumpersData* InventoryReturn = QuestBoard.Jumpers.FindByPredicate(
        [InventoryEntry](const FArcweaveJumpersData& Jumper) { return Jumper.Id == InventoryEntry->Outputs[0].Targetid; });
    if (!TestNotNull(TEXT("Inventory returns through an imported jumper"), InventoryReturn)) return false;
    TestEqual(TEXT("Inventory returns directly to the station menu"), InventoryReturn->ElementData.Id, EventEntryId);
    ReturnJumperIds.Add(InventoryReturn->Id);
    TestEqual(TEXT("Inventory and the five objective leaves have separate return jumpers"), ReturnJumperIds.Num(), 6);
    struct FEventLane
    {
        const TCHAR* EventType;
        TArray<const TCHAR*> Responses;
    };
    const FEventLane Lanes[] = {
        {TEXT("use_terminal"), {QuestBindings::StartElement, QuestBindings::TerminalAcceptedElement,
            QuestBindings::TerminalPoweredElement, QuestBindings::TerminalCompletedElement}},
        {TEXT("collect_cell"), {QuestBindings::DuplicatePickupElement, QuestBindings::PickupTerminalRequiredElement,
            QuestBindings::PickupActionElement}},
        {TEXT("check_generator"), {QuestBindings::TerminalRequiredElement, QuestBindings::MissingCellsElement,
            QuestBindings::SuccessElement, QuestBindings::AlreadyOnlineElement}},
        {TEXT("enter_exit"), {QuestBindings::ExitDeniedElement, QuestBindings::CompletedElement,
            QuestBindings::ExitAlreadyCompletedElement}}
    };
    TSet<FString> WorldReturnJumperIds;
    for (const FEventLane& Lane : Lanes)
    {
        FString LaneJumperId;
        for (const TCHAR* ResponseId : Lane.Responses)
        {
            const FArcweaveElementData* Response = QuestBoard.Elements.FindByPredicate(
                [ResponseId](const FArcweaveElementData& Element) { return Element.Id == ResponseId; });
            if (!TestNotNull(TEXT("The shared board contains each event response"), Response)) return false;
            if (Response->Id == QuestBindings::CompletedElement || Response->Id == QuestBindings::ExitAlreadyCompletedElement)
            {
                TestTrue(TEXT("Successful and repeated exits end the Play Mode playthrough"), Response->Outputs.IsEmpty());
                continue;
            }
            if (!TestEqual(TEXT("Each continuing event response has one return connection"), Response->Outputs.Num(), 1)) return false;
            const FArcweaveJumpersData* ReturnJumper = QuestBoard.Jumpers.FindByPredicate(
                [Response](const FArcweaveJumpersData& Jumper) { return Jumper.Id == Response->Outputs[0].Targetid; });
            if (!TestNotNull(TEXT("Each continuing event returns through an imported jumper"), ReturnJumper)) return false;
            TestEqual(TEXT("World responses return directly to the station without requiring a query"),
                ReturnJumper->ElementData.Id, EventEntryId);
            if (LaneJumperId.IsEmpty()) LaneJumperId = ReturnJumper->Id;
            TestEqual(TEXT("Outcomes in one event lane share their station return jumper"), ReturnJumper->Id, LaneJumperId);
            WorldReturnJumperIds.Add(ReturnJumper->Id);
        }
    }
    TestEqual(TEXT("The four event lanes have separate station return jumpers"), WorldReturnJumperIds.Num(), 4);
    TSet<FString> AllJumperIds = ReturnJumperIds;
    AllJumperIds.Append(WorldReturnJumperIds);
    AllJumperIds.Append(QueryJumperIds);
    TestEqual(TEXT("The query choices and world/query returns use twelve distinct jumpers"), AllJumperIds.Num(), 12);
    TestEqual(TEXT("The imported board has no unused jumpers"), QuestBoard.Jumpers.Num(), AllJumperIds.Num());

    const TSet<FName> RequiredUIFields = {
        TEXT("hud.brand"), TEXT("hud.station_name"), TEXT("hud.mission_tagline"), TEXT("hud.cells_label"),
        TEXT("hud.station_footer"), TEXT("world_text.terminal_label"), TEXT("world_text.cell_a_label"),
        TEXT("world_text.cell_b_label"), TEXT("world_text.sign_station"), TEXT("world_text.sign_distribution"),
        TEXT("world_text.sign_gate"), TEXT("world_text.sign_exit"), TEXT("quest_ui.mission_heading"),
        TEXT("quest_ui.grid_status"), TEXT("quest_ui.terminal_prompt"), TEXT("quest_ui.cell_prompt"),
        TEXT("quest_ui.generator_prompt"), TEXT("quest_ui.generator_label"), TEXT("quest_ui.gate_label"),
        TEXT("save_ui.controls"), TEXT("save_ui.saved"), TEXT("save_ui.loaded"), TEXT("save_ui.no_save"),
        TEXT("save_ui.save_failed"), TEXT("save_ui.load_failed"), TEXT("save_ui.incompatible_save")
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
        {QuestBindings::QuestUIComponent, TEXT("quest_ui"), 7},
        {QuestBindings::SaveUIComponent, TEXT("save_ui"), 7}
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
    TestEqual(TEXT("All runtime state uses component scopes, with no remaining globals"), GlobalVariableCount, 0);
    struct FStateField
    {
        const TCHAR* Id;
        const TCHAR* Name;
        const TCHAR* Type;
        const TCHAR* Default;
    };
    struct FStateComponentScope
    {
        const TCHAR* Id;
        const TCHAR* Scope;
        TArray<FStateField> Fields;
    };
    const FStateComponentScope StateComponents[] = {
        {QuestBindings::PlayerComponent, TEXT("player"), {
            {QuestBindings::PowerCellsAttribute, TEXT("power_cells"), TEXT("integer"), TEXT("0")}}},
        {QuestBindings::QuestStateComponent, TEXT("quest"), {
            {QuestBindings::QuestStartedAttribute, TEXT("started"), TEXT("boolean"), TEXT("false")},
            {QuestBindings::PowerRestoredAttribute, TEXT("power_restored"), TEXT("boolean"), TEXT("false")},
            {QuestBindings::QuestCompletedAttribute, TEXT("completed"), TEXT("boolean"), TEXT("false")},
            {QuestBindings::RequiredPowerCellsAttribute, TEXT("required_power_cells"), TEXT("integer"), TEXT("2")}}},
        {QuestBindings::CellAComponent, TEXT("cell_a"), {
            {QuestBindings::CellACollectedAttribute, TEXT("collected"), TEXT("boolean"), TEXT("false")}}},
        {QuestBindings::CellBComponent, TEXT("cell_b"), {
            {QuestBindings::CellBCollectedAttribute, TEXT("collected"), TEXT("boolean"), TEXT("false")}}}
    };
    for (const FStateComponentScope& Expected : StateComponents)
    {
        const FArcweaveComponentData* Component = InitialState.Components.FindByPredicate(
            [&Expected](const FArcweaveComponentData& Candidate) { return Candidate.Id == Expected.Id; });
        if (!TestNotNull(TEXT("The state component is imported: ") + FString(Expected.Scope), Component)) return false;
        TestEqual(TEXT("The state component has its authored scope: ") + FString(Expected.Scope), Component->CustomId, FString(Expected.Scope));
        TestEqual(TEXT("The state component has only its required fields: ") + FString(Expected.Scope), Component->Attributes.Num(), Expected.Fields.Num());
        for (const FStateField& Field : Expected.Fields)
        {
            const FString Label = FString(Expected.Scope) + TEXT(".") + Field.Name;
            const FArcweaveAttributeData* Attribute = Component->Attributes.FindByPredicate(
                [&Field](const FArcweaveAttributeData& Candidate) { return Candidate.Id == Field.Id; });
            const FArcweaveVariable* RuntimeVariable = InitialState.CurrentVars.Find(Field.Id);
            if (!TestNotNull(Label + TEXT(" is an authored state attribute"), Attribute)
                || !TestNotNull(Label + TEXT(" imports as a runtime variable"), RuntimeVariable)) return false;
            TestEqual(Label + TEXT(" has its authored name"), Attribute->CustomId, FString(Field.Name));
            TestEqual(Label + TEXT(" belongs to its state component"), Attribute->cId, Component->Id);
            TestEqual(Label + TEXT(" has component ownership"), Attribute->cType, FString(TEXT("components")));
            TestEqual(Label + TEXT(" has the required runtime name"), RuntimeVariable->Name, FString(Field.Name));
            TestEqual(Label + TEXT(" has the required type"), RuntimeVariable->Type, FString(Field.Type));
            TestEqual(Label + TEXT(" has component runtime ownership"), RuntimeVariable->cType, FString(TEXT("components")));
            TestEqual(Label + TEXT(" has the required runtime scope"), RuntimeVariable->Scope, Component->CustomId);
            TestTrue(Label + TEXT(" retains an authored default"), RuntimeVariable->bHasDefaultValue);
            TestEqual(Label + TEXT(" has the required default"), RuntimeVariable->DefaultValue, FString(Field.Default));
            TestEqual(Label + TEXT(" starts at its default"), RuntimeVariable->Value, FString(Field.Default));
        }
    }
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
        {QuestBindings::CellIdAttribute, TEXT("cell_id"), TEXT("string"), TEXT("")}
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

    int32 GateCommands = 0;
    int32 PickupCommands = 0;
    int32 PresentationChanges = 0;
    TestEqual(TEXT("Only collection and gate opening require engine command handlers"), Director->CommandHandlers.Num(), 2);
    const TFunction<void()> OpenGate = Director->CommandHandlers.FindChecked(TEXT("open_gate"));
    const TFunction<void()> CollectCell = Director->CommandHandlers.FindChecked(TEXT("collect_cell"));
    Director->CommandHandlers.Add(TEXT("open_gate"), [this, Director, &GateCommands, OpenGate]
    {
        TestTrue(TEXT("The gate command observes the authored power milestone already set"), Director->IsPowerRestored());
        ++GateCommands;
        OpenGate();
    });
    Director->CommandHandlers.Add(TEXT("collect_cell"), [this, Director, Arcweave, &PickupCommands, CollectCell]
    {
        const FArcweaveProjectData BeforeCommand = Arcweave->GetArcweaveProjectData();
        const FString CellId = BeforeCommand.CurrentVars.FindChecked(QuestBindings::CellIdAttribute).Value;
        const TCHAR* CollectedAttribute = CellId == TEXT("cell_a")
            ? QuestBindings::CellACollectedAttribute : QuestBindings::CellBCollectedAttribute;
        TestEqual(TEXT("The pickup command observes the shared collected flag already set by Arcscript"),
            BeforeCommand.CurrentVars.FindChecked(CollectedAttribute).Value, FString(TEXT("true")));
        TestEqual(TEXT("Arcscript increments the cached inventory exactly once before the physical command"),
            Director->GetPowerCellCount(), Director->CollectedCells.Num() + 1);
        TestEqual(TEXT("The pickup command runs from the same element that updated narrative state"),
            Director->GetCurrentElementId(), FString(QuestBindings::PickupActionElement));
        TestEqual(TEXT("The merged pickup element renders its updated count before the physical command"),
            Director->GetStatus(), FString::Printf(TEXT("Collected a power cell (%d/%d)."),
                Director->GetPowerCellCount(), Director->GetRequiredPowerCellCount()));
        TestFalse(TEXT("The narrative pickup runs before hiding its physical cell"), Director->HasCollectedCell(FName(*CellId)));
        ++PickupCommands;
        CollectCell();
        TestTrue(TEXT("The pickup command records the cell selected by the injected event"), Director->HasCollectedCell(FName(*CellId)));
        const FArcweaveProjectData AfterCommand = Arcweave->GetArcweaveProjectData();
        for (const auto& Pair : BeforeCommand.CurrentVars)
        {
            TestEqual(TEXT("The physical pickup command does not rewrite shared narrative state: ") + Pair.Key,
                AfterCommand.CurrentVars.FindChecked(Pair.Key).Value, Pair.Value.Value);
        }
    });
    Director->OnQuestChanged.AddLambda([this, Director, &PresentationChanges]
    {
        ++PresentationChanges;
        if (Director->IsQuestCompleted())
        {
            TestTrue(TEXT("Completed presentation also observes restored power"), Director->IsPowerRestored());
        }
    });

    const auto Visits = [Arcweave](const FString& Id)
    {
        return Arcweave->GetArcweaveProjectData().Visits.FindChecked(Id);
    };
    const auto Variable = [Arcweave](const TCHAR* Id)
    {
        return Arcweave->GetArcweaveProjectData().CurrentVars.FindChecked(Id).Value;
    };
    const auto CheckEvent = [this, Director, Arcweave, &Variable, &EventEntryId, &InventoryEntryId, &Lanes](const TCHAR* EventType,
        const TFunction<bool()>& Action, FName CellId = NAME_None)
    {
        const FArcweaveProjectData Before = Arcweave->GetArcweaveProjectData();
        const bool bResult = Action();
        const FArcweaveProjectData After = Arcweave->GetArcweaveProjectData();
        const FString Prefix = FString(EventType) + TEXT(": ");
        TestEqual(Prefix + TEXT("executes the shared world-event entry exactly once"),
            After.Visits.FindChecked(EventEntryId), Before.Visits.FindChecked(EventEntryId) + 1);
        TestEqual(Prefix + TEXT("menu label assignments cannot overwrite the injected event type"),
            Variable(QuestBindings::EventTypeAttribute), FString(EventType));
        TestEqual(Prefix + TEXT("menu label assignments cannot overwrite the injected cell identity"),
            Variable(QuestBindings::CellIdAttribute), CellId.IsNone() ? FString() : CellId.ToString());
        TestEqual(Prefix + TEXT("automatically refreshes presentation exactly once without selecting a menu query"),
            After.Visits.FindChecked(Director->PresentationEntryId),
            Before.Visits.FindChecked(Director->PresentationEntryId) + (bResult ? 1 : 0));
        TestEqual(Prefix + TEXT("never executes the optional inventory query"),
            After.Visits.FindChecked(InventoryEntryId), Before.Visits.FindChecked(InventoryEntryId));
        // The plugin counts element visits, not branch evaluations. Each selected lane
        // must execute exactly one response, with no response from another event's lane.
        for (const FEventLane& Lane : Lanes)
        {
            int32 ResponseVisits = 0;
            for (const TCHAR* Response : Lane.Responses)
            {
                ResponseVisits += After.Visits.FindChecked(Response) - Before.Visits.FindChecked(Response);
            }
            const int32 ExpectedIncrement = FString(EventType) == Lane.EventType ? 1 : 0;
            TestEqual(Prefix + TEXT("executes only the selected lane's response: ") + Lane.EventType,
                ResponseVisits, ExpectedIncrement);
            if (bResult && ExpectedIncrement == 1)
            {
                TestTrue(Prefix + TEXT("keeps the gameplay cursor at its response while the HUD refreshes independently"),
                    Lane.Responses.ContainsByPredicate([Director](const TCHAR* Response)
                    {
                        return Director->GetCurrentElementId() == Response;
                    }));
            }
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
    const auto AttemptPickup = [Director, &Error, &CheckEvent](FName CellId)
    {
        return CheckEvent(TEXT("collect_cell"), [Director, &Error, CellId] { return Director->CollectCell(CellId, Error); }, CellId);
    };
    const auto ReachExit = [Director, &Error, &CheckEvent]
    {
        return CheckEvent(TEXT("enter_exit"), [Director, &Error] { return Director->ReachExit(Error); });
    };
    const auto CheckCachedReads = [this, Director, Arcweave, &GateCommands,
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
        const int32 Commands = GateCommands + PickupCommands;
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
        TestEqual(Prefix + TEXT("cached getters do not dispatch commands"), GateCommands + PickupCommands, Commands);
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
    const auto CheckOptionalQueries = [this, Director, Arcweave, &Error, &EventEntryId, &InventoryEntryId,
        &Lanes, &GateCommands, &PickupCommands, &PresentationChanges](const TCHAR* Stage)
    {
        for (const FString& QueryId : {InventoryEntryId, Director->PresentationEntryId})
        {
            const bool bInventory = QueryId == InventoryEntryId;
            const FString Prefix = FString(Stage) + (bInventory ? TEXT(" inventory query: ") : TEXT(" objective query: "));
            const FArcweaveProjectData Before = Arcweave->GetArcweaveProjectData();
            const FString Cursor = Director->GetCurrentElementId();
            const FString Status = Director->GetStatus();
            const FString Objective = Director->GetObjective();
            const FString Presentation = Director->GetPresentationElementId();
            const bool bGateWasOpen = Director->IsGateOpen();
            const TSet<FName> PhysicalCells = Director->CollectedCells;
            const int32 Commands = GateCommands + PickupCommands;
            const int32 Notifications = PresentationChanges;
            FArcweaveElementData Result;
            if (!TestTrue(Prefix + TEXT("executes its separate authored flow"), Director->RunGraph(QueryId, false, Result, Error)))
            {
                AddError(Error);
                return false;
            }
            const FArcweaveProjectData After = Arcweave->GetArcweaveProjectData();
            TestEqual(Prefix + TEXT("stops at its return jumper without entering the station again"),
                After.Visits.FindChecked(EventEntryId), Before.Visits.FindChecked(EventEntryId));
            TestEqual(Prefix + TEXT("executes its entry exactly once"),
                After.Visits.FindChecked(QueryId), Before.Visits.FindChecked(QueryId) + 1);
            const FString OtherQueryId = bInventory ? Director->PresentationEntryId : InventoryEntryId;
            TestEqual(Prefix + TEXT("does not enter the other query flow"),
                After.Visits.FindChecked(OtherQueryId), Before.Visits.FindChecked(OtherQueryId));
            for (const FEventLane& Lane : Lanes)
            {
                for (const TCHAR* Response : Lane.Responses)
                {
                    TestEqual(Prefix + TEXT("does not replay a gameplay response: ") + Response,
                        After.Visits.FindChecked(Response), Before.Visits.FindChecked(Response));
                }
            }
            TestEqual(Prefix + TEXT("preserves the variable collection"), After.CurrentVars.Num(), Before.CurrentVars.Num());
            for (const auto& Pair : Before.CurrentVars)
            {
                const FArcweaveVariable& Actual = After.CurrentVars.FindChecked(Pair.Key);
                if (bInventory || Pair.Value.Scope != TEXT("quest_ui"))
                {
                    TestEqual(Prefix + TEXT("preserves shared state and inputs: ") + Pair.Value.Scope + TEXT(".") + Pair.Value.Name,
                        Actual.Value, Pair.Value.Value);
                }
                TestEqual(Prefix + TEXT("preserves the authored default: ") + Pair.Value.Name,
                    Actual.DefaultValue, Pair.Value.DefaultValue);
            }
            TestEqual(Prefix + TEXT("does not dispatch physical commands"), GateCommands + PickupCommands, Commands);
            TestEqual(Prefix + TEXT("does not publish a gameplay change"), PresentationChanges, Notifications);
            TestEqual(Prefix + TEXT("preserves the physical gate"), Director->IsGateOpen(), bGateWasOpen);
            TestTrue(Prefix + TEXT("preserves the physical pickups"), Director->CollectedCells.Includes(PhysicalCells)
                && PhysicalCells.Includes(Director->CollectedCells));
            TestEqual(Prefix + TEXT("preserves the gameplay cursor"), Director->GetCurrentElementId(), Cursor);
            TestEqual(Prefix + TEXT("preserves interaction feedback"), Director->GetStatus(), Status);
            TestEqual(Prefix + TEXT("preserves the cached HUD objective"), Director->GetObjective(), Objective);
            TestEqual(Prefix + TEXT("preserves the cached HUD cursor"), Director->GetPresentationElementId(), Presentation);
            if (bInventory)
            {
                TestEqual(Prefix + TEXT("finishes at the inventory element"), Result.Id, InventoryEntryId);
                TestEqual(Prefix + TEXT("renders the current authored label and collected count"), Result.Content,
                    Director->GetUIText(TEXT("hud.cells_label")) + TEXT(": ") + FString::FromInt(Director->GetPowerCellCount()));
            }
            else
            {
                TestEqual(Prefix + TEXT("selects the same current objective as the automatic HUD refresh"), Result.Id, Presentation);
                TestEqual(Prefix + TEXT("renders the same current objective as the HUD"), Result.Content, Objective);
            }
        }
        return true;
    };
    const auto CheckRestart = [this, Director, Arcweave, &InitialState, &EventEntryId, &Visits, &Error, &UIVariableIds, &CheckUIValues]()
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
        TestEqual(TEXT("Restart does not simulate a world event"), Visits(EventEntryId), 0);
        TestTrue(TEXT("Restart leaves the gameplay cursor empty"), Director->GetCurrentElementId().IsEmpty());
        TestTrue(TEXT("Restart clears previous gameplay feedback"), Director->GetStatus().IsEmpty());
        TestEqual(TEXT("Restart clears the last event type"), Restarted.CurrentVars.FindChecked(QuestBindings::EventTypeAttribute).Value, FString());
        TestEqual(TEXT("Restart clears the last pickup identity"), Restarted.CurrentVars.FindChecked(QuestBindings::CellIdAttribute).Value, FString());
        TestEqual(TEXT("Restart resets the shared cell A flag"), Restarted.CurrentVars.FindChecked(QuestBindings::CellACollectedAttribute).Value, FString(TEXT("false")));
        TestEqual(TEXT("Restart resets the shared cell B flag"), Restarted.CurrentVars.FindChecked(QuestBindings::CellBCollectedAttribute).Value, FString(TEXT("false")));
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

    TestEqual(TEXT("Seven state values, twenty-six UI strings, and two event inputs are imported"), InitialState.CurrentVars.Num(), 35);
    TestFalse(TEXT("Initialization does not accept the task"), Director->IsQuestStarted());
    TestFalse(TEXT("Initialization does not complete the task"), Director->IsQuestCompleted());
    TestEqual(TEXT("Initial required cell count comes from the export"), Director->GetRequiredPowerCellCount(), 2);
    TestEqual(TEXT("Startup does not enter the world-event graph"), Visits(EventEntryId), 0);
    TestTrue(TEXT("Startup has no gameplay cursor"), Director->GetCurrentElementId().IsEmpty());
    TestTrue(TEXT("Startup has no interaction feedback"), Director->GetStatus().IsEmpty());
    TestEqual(TEXT("Unaccepted presentation is selected by the graph"), Director->GetPresentationElementId(), FString(QuestBindings::PresentationUnacceptedElement));
    TestEqual(TEXT("Authored initial objective is cached"), Director->GetObjective(), FString(TEXT("Use the terminal to begin.")));
    CheckUIValues(TEXT("Initialized state"));
    TestFalse(TEXT("Initial terminal prompt is available"), Director->GetUIText(TEXT("quest_ui.terminal_prompt")).IsEmpty());
    CheckCachedReads(TEXT("Initial state"));
    CheckPresentation(QuestBindings::PresentationUnacceptedElement);
    if (!CheckOptionalQueries(TEXT("Initial state"))) return false;

    TestTrue(TEXT("A generator attempt before acceptance executes authored guidance"), AttemptGenerator());
    TestTrue(TEXT("Narrative denial is not an integration error"), Error.IsEmpty());
    TestEqual(TEXT("Generator enters the terminal-required response"), Director->GetCurrentElementId(), FString(QuestBindings::TerminalRequiredElement));
    TestTrue(TEXT("Generator guidance is the authored authorization text"), Director->GetStatus().Contains(TEXT("The generator is waiting for authorization.")));
    TestEqual(TEXT("Preterminal attempt visits the authored guidance once"), Visits(QuestBindings::TerminalRequiredElement), 1);
    TestFalse(TEXT("Generator guidance leaves acceptance false"), Director->IsQuestStarted());
    TestFalse(TEXT("Generator guidance leaves power off"), Director->IsPowerRestored());

    Arcweave->SetVariable(QuestBindings::PowerCellsAttribute, TEXT("2"));
    TestTrue(TEXT("Authored acceptance requirement still applies with enough cells"), AttemptGenerator());
    TestEqual(TEXT("Acceptance takes precedence over the required count"), Director->GetCurrentElementId(), FString(QuestBindings::TerminalRequiredElement));
    TestEqual(TEXT("Premature two-cell attempt does not execute success"), Visits(QuestBindings::SuccessElement), 0);
    TestEqual(TEXT("Premature attempts execute no world commands"), GateCommands + PickupCommands, 0);
    Arcweave->SetVariable(QuestBindings::PowerCellsAttribute, TEXT("0"));

    TestTrue(TEXT("Pickup before acceptance executes its authored denial"), AttemptPickup(TEXT("cell_a")));
    TestEqual(TEXT("Pickup uses its dedicated prerequisite response"), Director->GetCurrentElementId(), FString(QuestBindings::PickupTerminalRequiredElement));
    TestTrue(TEXT("Denied pickup is a valid narrative interaction"), Error.IsEmpty());
    TestEqual(TEXT("Denied pickup cannot increase the count"), Director->GetPowerCellCount(), 0);
    TestEqual(TEXT("Denied pickup leaves the shared cell A available"), Variable(QuestBindings::CellACollectedAttribute), FString(TEXT("false")));
    TestEqual(TEXT("Denied pickup leaves the shared cell B available"), Variable(QuestBindings::CellBCollectedAttribute), FString(TEXT("false")));
    TestFalse(TEXT("Denied pickup leaves the physical cell available"), Director->HasCollectedCell(TEXT("cell_a")));
    TestEqual(TEXT("Denied pickup does not dispatch collect_cell"), PickupCommands, 0);
    TestTrue(TEXT("Exit before power executes the authored denial"), ReachExit());
    TestEqual(TEXT("Early exit reaches the denial element"), Director->GetCurrentElementId(), FString(QuestBindings::ExitDeniedElement));
    TestFalse(TEXT("Early exit does not complete the task"), Director->IsQuestCompleted());

    TestTrue(TEXT("Terminal traverses the acceptance graph"), StartQuest());
    TestTrue(TEXT("Acceptance script changes quest.started"), Director->IsQuestStarted());
    TestEqual(TEXT("Accepting the task ends at Start"), Director->GetCurrentElementId(), FString(QuestBindings::StartElement));
    TestEqual(TEXT("Acceptance does not repeat the generator guidance"), Visits(QuestBindings::TerminalRequiredElement), 2);
    TestEqual(TEXT("Collecting objective renders current and required counts"), Director->GetObjective(), FString(TEXT("Collect power cells (0/2), then use the generator.")));
    TestTrue(TEXT("Repeated terminal interaction follows an authored response"), StartQuest());
    TestEqual(TEXT("Repeat acceptance reaches its own response"), Director->GetCurrentElementId(), FString(QuestBindings::TerminalAcceptedElement));
    TestEqual(TEXT("Repeat acceptance does not replay Start"), Visits(QuestBindings::StartElement), 1);

    TestTrue(TEXT("Zero-cell generator attempt executes the missing-cell response"), AttemptGenerator());
    TestEqual(TEXT("Zero cells select MissingCells"), Director->GetCurrentElementId(), FString(QuestBindings::MissingCellsElement));
    TestFalse(TEXT("Missing-cell response leaves world power off"), Director->IsPowerRestored());
    TestFalse(TEXT("Missing-cell response leaves the gate closed"), Director->IsGateOpen());

    TestTrue(TEXT("First accepted pickup executes the shared state, feedback, and command element"), AttemptPickup(TEXT("cell_a")));
    TestEqual(TEXT("Pickup Arcscript updates the shared narrative count"), Variable(QuestBindings::PowerCellsAttribute), FString(TEXT("1")));
    TestEqual(TEXT("The first pickup marks shared cell A collected"), Variable(QuestBindings::CellACollectedAttribute), FString(TEXT("true")));
    TestEqual(TEXT("Collecting cell A leaves shared cell B available"), Variable(QuestBindings::CellBCollectedAttribute), FString(TEXT("false")));
    TestTrue(TEXT("Pickup command records the physical cell"), Director->HasCollectedCell(TEXT("cell_a")));
    TestEqual(TEXT("Pickup feedback is rendered after the count update"), Director->GetStatus(), FString(TEXT("Collected a power cell (1/2).")));
    TestEqual(TEXT("Pickup ends at its combined action and feedback node"), Director->GetCurrentElementId(), FString(QuestBindings::PickupActionElement));
    TestEqual(TEXT("One cell keeps the collecting presentation"), Director->GetPresentationElementId(), FString(QuestBindings::PresentationCollectingElement));
    TestEqual(TEXT("Collecting objective updates with the current count"), Director->GetObjective(), FString(TEXT("Collect power cells (1/2), then use the generator.")));
    TestEqual(TEXT("Exactly one physical pickup command was issued"), PickupCommands, 1);
    CheckCachedReads(TEXT("Collecting state"));
    CheckPresentation(QuestBindings::PresentationCollectingElement);
    if (!CheckOptionalQueries(TEXT("Collecting state"))) return false;

    TestTrue(TEXT("Duplicate pickup executes authored feedback"), AttemptPickup(TEXT("cell_a")));
    TestEqual(TEXT("Duplicate uses the duplicate response"), Director->GetCurrentElementId(), FString(QuestBindings::DuplicatePickupElement));
    TestTrue(TEXT("Duplicate is not an integration error"), Error.IsEmpty());
    TestEqual(TEXT("Duplicate cannot increase the count"), Director->GetPowerCellCount(), 1);
    TestEqual(TEXT("Duplicate preserves the shared cell A flag"), Variable(QuestBindings::CellACollectedAttribute), FString(TEXT("true")));
    TestEqual(TEXT("Duplicate cannot collect shared cell B"), Variable(QuestBindings::CellBCollectedAttribute), FString(TEXT("false")));
    TestEqual(TEXT("Duplicate cannot reissue collect_cell"), PickupCommands, 1);
    TestEqual(TEXT("Duplicate does not reenter the combined pickup node"), Visits(QuestBindings::PickupActionElement), 1);
    Arcweave->SetVariable(QuestBindings::QuestStartedAttribute, TEXT("false"));
    TestTrue(TEXT("Duplicate detection takes precedence over quest acceptance"), AttemptPickup(TEXT("cell_a")));
    TestEqual(TEXT("The pickup branch checks shared collected state before acceptance"),
        Director->GetCurrentElementId(), FString(QuestBindings::DuplicatePickupElement));
    TestEqual(TEXT("Duplicate routing never repeats the pickup action"), PickupCommands, 1);
    Arcweave->SetVariable(QuestBindings::QuestStartedAttribute, TEXT("true"));
    TestTrue(TEXT("One-cell generator attempt reevaluates the condition"), AttemptGenerator());
    TestEqual(TEXT("One cell still selects MissingCells"), Director->GetCurrentElementId(), FString(QuestBindings::MissingCellsElement));

    TestTrue(TEXT("Another duplicate request reads shared cell A state"), AttemptPickup(TEXT("cell_a")));
    TestTrue(TEXT("Second unique pickup reaches the requirement"), AttemptPickup(TEXT("cell_b")));
    TestEqual(TEXT("Both pickups are reflected in narrative state"), Director->GetPowerCellCount(), 2);
    TestEqual(TEXT("The second pickup preserves shared cell A"), Variable(QuestBindings::CellACollectedAttribute), FString(TEXT("true")));
    TestEqual(TEXT("The second pickup marks shared cell B collected"), Variable(QuestBindings::CellBCollectedAttribute), FString(TEXT("true")));
    TestEqual(TEXT("Second pickup feedback reads the updated count"), Director->GetStatus(), FString(TEXT("Collected a power cell (2/2).")));
    TestEqual(TEXT("Required count selects the authored ready presentation"), Director->GetPresentationElementId(), FString(QuestBindings::PresentationReadyElement));
    TestEqual(TEXT("Ready objective is authored text"), Director->GetObjective(), FString(TEXT("Return to the generator and restore power.")));
    const FString ReadyPrompt = Director->GetUIText(TEXT("quest_ui.generator_prompt"));
    TestFalse(TEXT("Ready generator prompt is supplied by the graph"), ReadyPrompt.IsEmpty());
    CheckPresentation(QuestBindings::PresentationReadyElement);
    if (!CheckOptionalQueries(TEXT("Ready state"))) return false;

    TestTrue(TEXT("Generator succeeds after the required pickups"), AttemptGenerator());
    TestEqual(TEXT("Success follows the authored connection"), Director->GetCurrentElementId(), FString(QuestBindings::SuccessElement));
    TestEqual(TEXT("Success updates the narrative power flag"), Variable(QuestBindings::PowerRestoredAttribute), FString(TEXT("true")));
    TestTrue(TEXT("The authored quest.power_restored value turns world power on"), Director->IsPowerRestored());
    TestTrue(TEXT("open_gate opens the world gate"), Director->IsGateOpen());
    TestFalse(TEXT("Restoring power does not complete the task before reaching the exit"), Director->IsQuestCompleted());
    TestEqual(TEXT("Completion variable remains false while the exit is available"), Variable(QuestBindings::QuestCompletedAttribute), FString(TEXT("false")));
    TestEqual(TEXT("Power and completion select distinct presentation stages"), Director->GetPresentationElementId(), FString(QuestBindings::PresentationPoweredElement));
    TestEqual(TEXT("Powered objective directs the player to the exit"), Director->GetObjective(), FString(TEXT("Power restored. Walk through the open gate.")));
    TestEqual(TEXT("Gate command runs once"), GateCommands, 1);
    CheckCachedReads(TEXT("Powered state"));
    CheckPresentation(QuestBindings::PresentationPoweredElement);
    if (!CheckOptionalQueries(TEXT("Powered state"))) return false;

    TestTrue(TEXT("Repeated generator use executes an authored already-online response"), AttemptGenerator());
    TestEqual(TEXT("Repeated generator reaches AlreadyOnline"), Director->GetCurrentElementId(), FString(QuestBindings::AlreadyOnlineElement));
    TestEqual(TEXT("Repeated generator does not reenter success"), Visits(QuestBindings::SuccessElement), 1);
    TestEqual(TEXT("Repeated generator does not reissue gate opening"), GateCommands, 1);
    TestTrue(TEXT("Terminal can describe the powered state"), StartQuest());
    TestEqual(TEXT("Powered terminal uses its authored response"), Director->GetCurrentElementId(), FString(QuestBindings::TerminalPoweredElement));

    TestTrue(TEXT("Reaching the exit executes the completion graph"), ReachExit());
    TestTrue(TEXT("Exit completion updates quest.completed"), Director->IsQuestCompleted());
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
    if (!CheckOptionalQueries(TEXT("Completed state"))) return false;
    const FArcweaveProjectData ForwardCompletedState = Arcweave->GetArcweaveProjectData();

    const FArcweaveProjectData BeforeUnknown = Arcweave->GetArcweaveProjectData();
    const int32 CommandsBeforeUnknown = GateCommands + PickupCommands;
    const FString ObjectiveBeforeUnknown = Director->GetObjective();
    const FString PresentationBeforeUnknown = Director->GetPresentationElementId();
    TestFalse(TEXT("An unknown event reports an integration error when no router condition matches"),
        CheckEvent(TEXT("unknown_test_event"), [Director, &Error] { return Director->RunEvent(TEXT("unknown_test_event"), Error); }));
    TestEqual(TEXT("An unmatched router uses the existing missing-destination error"), Error, FString(TEXT("The authored branch has no destination.")));
    TestEqual(TEXT("Unknown input stops at the shared event entry"), Director->GetCurrentElementId(), EventEntryId);
    TestEqual(TEXT("The integration error is surfaced in the interaction status"), Director->GetStatus(), Error);
    TestEqual(TEXT("Unknown input preserves the cached objective"), Director->GetObjective(), ObjectiveBeforeUnknown);
    TestEqual(TEXT("Unknown input preserves the presentation cursor"), Director->GetPresentationElementId(), PresentationBeforeUnknown);
    TestEqual(TEXT("Unknown events dispatch no world commands"), GateCommands + PickupCommands, CommandsBeforeUnknown);
    TestTrue(TEXT("Unknown input preserves quest acceptance"), Director->IsQuestStarted());
    TestTrue(TEXT("Unknown input preserves quest completion"), Director->IsQuestCompleted());
    TestTrue(TEXT("Unknown input preserves the authored power state"), Director->IsPowerRestored());
    TestTrue(TEXT("Unknown input preserves the open gate"), Director->IsGateOpen());
    TestTrue(TEXT("Unknown input preserves physical cell A"), Director->HasCollectedCell(TEXT("cell_a")));
    TestTrue(TEXT("Unknown input preserves physical cell B"), Director->HasCollectedCell(TEXT("cell_b")));
    const FArcweaveProjectData AfterUnknown = Arcweave->GetArcweaveProjectData();
    TestEqual(TEXT("Unknown input preserves the visit collection"), AfterUnknown.Visits.Num(), BeforeUnknown.Visits.Num());
    for (const auto& Pair : BeforeUnknown.Visits)
    {
        const int32 ExpectedIncrement = Pair.Key == EventEntryId ? 1 : 0;
        TestEqual(TEXT("Unknown input executes no gameplay or presentation node beyond the event entry: ") + Pair.Key,
            AfterUnknown.Visits.FindChecked(Pair.Key), Pair.Value + ExpectedIncrement);
    }
    TestEqual(TEXT("Unknown input preserves the variable collection"), AfterUnknown.CurrentVars.Num(), BeforeUnknown.CurrentVars.Num());
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
    TestEqual(TEXT("Restart cannot replay the gate command"), GateCommands, 1);

    // Changing the authored requirement changes both the branch result and its displayed count.
    Arcweave->SetVariable(QuestBindings::RequiredPowerCellsAttribute, TEXT("1"));
    TestTrue(TEXT("One-cell variant still begins through the terminal graph"), StartQuest());
    TestEqual(TEXT("Required count getter reads the authored variable"), Director->GetRequiredPowerCellCount(), 1);
    TestEqual(TEXT("Objective renders the changed requirement"), Director->GetObjective(), FString(TEXT("Collect power cells (0/1), then use the generator.")));
    TestTrue(TEXT("One-cell variant allows either physical cell"), AttemptPickup(TEXT("cell_b")));
    TestEqual(TEXT("Pickup feedback renders the changed requirement"), Director->GetStatus(), FString(TEXT("Collected a power cell (1/1).")));
    TestEqual(TEXT("One pickup now selects Ready"), Director->GetPresentationElementId(), FString(QuestBindings::PresentationReadyElement));
    TestEqual(TEXT("One-cell readiness selects the same authored interaction prompt"), Director->GetUIText(TEXT("quest_ui.generator_prompt")), ReadyPrompt);
    TestTrue(TEXT("One-cell variant still handles duplicate feedback"), AttemptPickup(TEXT("cell_b")));
    TestEqual(TEXT("Duplicate preserves the one-cell requirement state"), Director->GetPowerCellCount(), 1);
    TestEqual(TEXT("Only one additional pickup command was dispatched"), PickupCommands, 3);
    TestTrue(TEXT("One-cell requirement permits power restoration"), AttemptGenerator());
    TestTrue(TEXT("One-cell variant opens the gate"), Director->IsGateOpen());
    TestFalse(TEXT("One-cell variant also waits for the exit to complete"), Director->IsQuestCompleted());
    TestTrue(TEXT("One-cell variant completes at the exit"), ReachExit());
    TestTrue(TEXT("One-cell exit marks completion"), Director->IsQuestCompleted());
    TestEqual(TEXT("Each session issues exactly one gate command"), GateCommands, 2);
    CheckCachedReads(TEXT("One-cell completed state"));
    CheckPresentation(QuestBindings::PresentationCompletedElement);

    TestTrue(TEXT("A completed quest still routes duplicate pickup feedback through its pickup branch"), AttemptPickup(TEXT("cell_b")));
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
        &CheckCachedReads, &GateCommands, &PickupCommands, &PresentationChanges,
        &UpdatedStationName, &UpdatedWorldLabel, &EventEntryId](const TCHAR* ExpectedLeaf, bool bStarted, bool bPowered,
        bool bCompleted, int32 Cells, int32 Required)
    {
        Arcweave->SetVariable(QuestBindings::QuestStartedAttribute, bStarted ? TEXT("true") : TEXT("false"));
        Arcweave->SetVariable(QuestBindings::PowerRestoredAttribute, bPowered ? TEXT("true") : TEXT("false"));
        Arcweave->SetVariable(QuestBindings::QuestCompletedAttribute, bCompleted ? TEXT("true") : TEXT("false"));
        Arcweave->SetVariable(QuestBindings::PowerCellsAttribute, FString::FromInt(Cells));
        Arcweave->SetVariable(QuestBindings::RequiredPowerCellsAttribute, FString::FromInt(Required));
        const FArcweaveProjectData Before = Arcweave->GetArcweaveProjectData();
        const FString Cursor = Director->GetCurrentElementId();
        const FString Status = Director->GetStatus();
        TestEqual(TEXT("The acceptance getter immediately reads the changed quest attribute"), Director->IsQuestStarted(), bStarted);
        TestEqual(TEXT("The power getter immediately reads the changed quest attribute"), Director->IsPowerRestored(), bPowered);
        TestEqual(TEXT("The completion getter immediately reads the changed quest attribute"), Director->IsQuestCompleted(), bCompleted);
        TestEqual(TEXT("The inventory getter immediately reads the changed player attribute"), Director->GetPowerCellCount(), Cells);
        TestEqual(TEXT("The requirement getter immediately reads the changed quest attribute"), Director->GetRequiredPowerCellCount(), Required);
        const bool bWorldGateOpen = Director->IsGateOpen();
        const int32 Commands = GateCommands + PickupCommands;
        const int32 Notifications = PresentationChanges;
        if (!TestTrue(TEXT("Arcscript refreshes the presentation graph"), Director->RefreshPresentation(Error)))
        {
            AddError(Error);
            return false;
        }
        const FArcweaveProjectData After = Arcweave->GetArcweaveProjectData();
        TestEqual(TEXT("Presentation refresh stops before the shared interaction menu"),
            After.Visits.FindChecked(EventEntryId), Before.Visits.FindChecked(EventEntryId));
        TestEqual(TEXT("Presentation refresh enters its marked section exactly once"),
            After.Visits.FindChecked(Director->PresentationEntryId), Before.Visits.FindChecked(Director->PresentationEntryId) + 1);
        TestEqual(TEXT("Presentation refresh preserves the variable collection"), After.CurrentVars.Num(), Before.CurrentVars.Num());
        for (const auto& Pair : Before.CurrentVars)
        {
            const FArcweaveVariable& Actual = After.CurrentVars.FindChecked(Pair.Key);
            if (Pair.Value.Scope != TEXT("quest_ui"))
            {
                TestEqual(TEXT("Presentation preserves state attributes, event inputs, and static UI: ") + Pair.Value.Scope + TEXT(".") + Pair.Value.Name,
                    Actual.Value, Pair.Value.Value);
            }
            TestEqual(TEXT("Presentation preserves each variable's authored default: ") + Pair.Value.Name,
                Actual.DefaultValue, Pair.Value.DefaultValue);
        }
        TestEqual(TEXT("Presentation never dispatches gameplay commands"), GateCommands + PickupCommands, Commands);
        TestEqual(TEXT("Presentation refresh alone does not publish gameplay changes"), PresentationChanges, Notifications);
        TestEqual(TEXT("Presentation preserves the gameplay cursor"), Director->GetCurrentElementId(), Cursor);
        TestEqual(TEXT("Presentation preserves gameplay feedback"), Director->GetStatus(), Status);
        TestEqual(TEXT("Presentation keeps power consistent with the quest attribute"), Director->IsPowerRestored(), bPowered);
        TestEqual(TEXT("Presentation preserves native gate state"), Director->IsGateOpen(), bWorldGateOpen);
        TestEqual(TEXT("Presentation reset preserves a current HUD text override"), Director->GetUIText(TEXT("hud.station_name")), UpdatedStationName);
        TestEqual(TEXT("Presentation reset preserves a current world text override"), Director->GetUIText(TEXT("world_text.cell_a_label")), UpdatedWorldLabel);
        CheckPresentation(ExpectedLeaf);
        CheckCachedReads(TEXT("Presentation backtracking"));
        return true;
    };
    if (!CheckPresentationRefresh(QuestBindings::PresentationCompletedElement, true, true, true, 2, 2)) return false;
    if (!CheckPresentationRefresh(QuestBindings::PresentationCompletedElement, true, false, true, 2, 2)) return false;
    if (!CheckPresentationRefresh(QuestBindings::PresentationPoweredElement, true, true, false, 2, 2)) return false;
    if (!CheckPresentationRefresh(QuestBindings::PresentationUnacceptedElement, false, false, false, 0, 2)) return false;
    if (!CheckPresentationRefresh(QuestBindings::PresentationReadyElement, true, false, false, 2, 2)) return false;
    if (!CheckPresentationRefresh(QuestBindings::PresentationCollectingElement, true, false, false, 1, 2)) return false;
    if (!CheckPresentationRefresh(QuestBindings::PresentationReadyElement, true, false, false, 1, 1)) return false;
    if (!CheckRestart()) return false;

    // Power is narrative state; changing it does not imply the separate gate-opening action.
    const int32 CommandsBeforePowerChange = GateCommands + PickupCommands;
    Arcweave->SetVariable(QuestBindings::PowerRestoredAttribute, TEXT("true"));
    TestTrue(TEXT("SetVariable immediately updates the power getter without an engine command"), Director->IsPowerRestored());
    TestFalse(TEXT("Changing the power attribute alone leaves the native gate closed"), Director->IsGateOpen());
    TestTrue(TEXT("A normal terminal interaction refreshes the changed power state"), StartQuest());
    TestEqual(TEXT("The terminal reads the scoped power state"), Director->GetCurrentElementId(), FString(QuestBindings::TerminalPoweredElement));
    CheckPresentation(QuestBindings::PresentationPoweredElement);
    TestEqual(TEXT("An external power change never runs generator success"), Visits(QuestBindings::SuccessElement), 0);
    TestEqual(TEXT("Refreshing external power changes dispatches no engine commands"), GateCommands + PickupCommands, CommandsBeforePowerChange);
    TestFalse(TEXT("The gate still requires its own authored action"), Director->IsGateOpen());
    Arcweave->SetVariable(QuestBindings::PowerRestoredAttribute, TEXT("false"));
    TestFalse(TEXT("Clearing the power attribute immediately clears the power getter"), Director->IsPowerRestored());
    TestTrue(TEXT("The next terminal interaction refreshes the unpowered state"), StartQuest());
    CheckPresentation(QuestBindings::PresentationCollectingElement);
    TestFalse(TEXT("A normal refresh does not resurrect stale native power"), Director->IsPowerRestored());
    TestEqual(TEXT("Clearing external power also dispatches no engine commands"), GateCommands + PickupCommands, CommandsBeforePowerChange);
    if (!CheckRestart()) return false;

    // Both physical item identities exercise the same authored pickup rule in either order.
    const int32 CommandsBeforeReversePickup = PickupCommands;
    const int32 CommandsBeforeReverseGate = GateCommands;
    TestTrue(TEXT("Cell B before acceptance uses the same authored denial"), AttemptPickup(TEXT("cell_b")));
    TestEqual(TEXT("Denied cell B leaves its shared flag available"), Variable(QuestBindings::CellBCollectedAttribute), FString(TEXT("false")));
    TestEqual(TEXT("Denied cell B cannot issue a pickup command"), PickupCommands, CommandsBeforeReversePickup);
    TestTrue(TEXT("The reverse-order session begins through the terminal"), StartQuest());
    TestTrue(TEXT("The reverse-order session collects cell B first"), AttemptPickup(TEXT("cell_b")));
    TestEqual(TEXT("Cell B first advances the shared inventory once"), Director->GetPowerCellCount(), 1);
    TestEqual(TEXT("Cell B first leaves cell A available"), Variable(QuestBindings::CellACollectedAttribute), FString(TEXT("false")));
    TestEqual(TEXT("Cell B first sets its shared flag"), Variable(QuestBindings::CellBCollectedAttribute), FString(TEXT("true")));
    TestTrue(TEXT("Cell B duplicate uses the shared-state branch"), AttemptPickup(TEXT("cell_b")));
    TestEqual(TEXT("Cell B duplicate cannot increment inventory"), Director->GetPowerCellCount(), 1);
    TestEqual(TEXT("Cell B duplicate cannot issue another command"), PickupCommands, CommandsBeforeReversePickup + 1);
    TestTrue(TEXT("The reverse-order session then collects cell A"), AttemptPickup(TEXT("cell_a")));
    TestEqual(TEXT("Reverse-order pickups reach the same inventory total"), Director->GetPowerCellCount(), 2);
    TestEqual(TEXT("Reverse-order pickups use exactly two physical commands"), PickupCommands, CommandsBeforeReversePickup + 2);
    CheckPresentation(QuestBindings::PresentationReadyElement);
    TestTrue(TEXT("The reverse-order session restores power"), AttemptGenerator());
    TestTrue(TEXT("The reverse-order session completes at the exit"), ReachExit());
    TestTrue(TEXT("The reverse-order terminal reports completion"), StartQuest());
    TestEqual(TEXT("The reverse-order session opens the gate exactly once"), GateCommands, CommandsBeforeReverseGate + 1);
    const FArcweaveProjectData ReverseCompletedState = Arcweave->GetArcweaveProjectData();
    for (const auto& Pair : ForwardCompletedState.CurrentVars)
    {
        TestEqual(TEXT("Either pickup order reaches the same authored state: ") + Pair.Value.Scope + TEXT(".") + Pair.Value.Name,
            ReverseCompletedState.CurrentVars.FindChecked(Pair.Key).Value, Pair.Value.Value);
    }
    CheckCachedReads(TEXT("Reverse-order completed state"));
    if (!CheckRestart()) return false;
    return true;
}

#endif
