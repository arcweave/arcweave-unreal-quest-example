#include "QuestDirector.h"

#include "QuestBindings.h"
#include "ArcweaveVariable.h"
#include "ArcscriptTranspilerOutput.h"
#include "ArcweaveSubsystem.h"
#include "Engine/Engine.h"
#include "GetIsTargetBranchOutput.h"

UQuestDirector::UQuestDirector()
{
    // Arcweave decides when to request an action; these handlers implement its world effect.
    CommandHandlers.Add(TEXT("open_gate"), [this] { bGateOpen = true; });
    CommandHandlers.Add(TEXT("collect_cell"), [this]
    {
        // Arcscript already updated inventory. This hides the collected world actor.
        CollectedCells.Add(PendingCellId);
    });
}

void UQuestDirector::Deinitialize()
{
    if (Arcweave)
    {
        Arcweave->OnArcweaveVariableChanged.RemoveDynamic(this, &UQuestDirector::HandleVariablesChanged);
    }
    StateValues.Reset();
    bProjectLoaded = false;
    Super::Deinitialize();
}

bool UQuestDirector::StartNewGame(FString& Error)
{
    bProjectLoaded = false;
    Arcweave = GEngine->GetEngineSubsystem<UArcweaveSubsystem>();
    if (!Arcweave->LoadJsonFile())
    {
        return RejectInteraction(TEXT("Could not load Content/ArcweaveExport/quest.json."), Error);
    }

    CollectedCells.Reset();
    PendingCellId = NAME_None;
    CurrentElementId.Empty();
    PresentationEntryId.Empty();
    PresentationElementId.Empty();
    Status.Empty();
    bGateOpen = false;

    const FArcweaveProjectData Project = Arcweave->GetArcweaveProjectData();
    const FArcweaveBoardData* QuestBoard = Project.Boards.FindByPredicate(
        [&Project](const FArcweaveBoardData& Board)
        {
            return Board.Elements.ContainsByPredicate([&Project](const FArcweaveElementData& Element)
            {
                return Element.Id == Project.StartingElementId;
            });
        });
    if (!QuestBoard)
    {
        return RejectInteraction(TEXT("The narrative export is missing its starting element's board."), Error);
    }
    const FArcweaveElementData* PresentationEntry = QuestBoard->Elements.FindByPredicate(
        [](const FArcweaveElementData& Element)
        {
            return Element.Attributes.ContainsByPredicate([](const FArcweaveAttributeData& Attribute)
            {
                return Attribute.Name == TEXT("entry_point") && Attribute.Value.Data == TEXT("objectives_ui");
            });
        });
    if (!PresentationEntry)
    {
        return RejectInteraction(TEXT("The quest board is missing its objectives_ui entry point."), Error);
    }
    PresentationEntryId = PresentationEntry->Id;

    UIVariableIds.Reset();
    for (const TCHAR* ComponentId : {QuestBindings::HUDTextComponent,
        QuestBindings::WorldTextComponent, QuestBindings::QuestUIComponent})
    {
        const FArcweaveComponentData* UI = Project.Components.FindByPredicate(
            [ComponentId](const FArcweaveComponentData& Component) { return Component.Id == ComponentId; });
        if (!UI)
        {
            return RejectInteraction(TEXT("The narrative export is missing a required UI component."), Error);
        }
        for (const FArcweaveAttributeData& Attribute : UI->Attributes)
        {
            UIVariableIds.Add(FName(*(UI->CustomId + TEXT(".") + Attribute.CustomId)), Attribute.Id);
        }
    }
    StateValues.Reset();
    for (const TCHAR* Id : {QuestBindings::QuestStartedAttribute, QuestBindings::QuestCompletedAttribute,
        QuestBindings::PowerRestoredAttribute, QuestBindings::PowerCellsAttribute, QuestBindings::RequiredPowerCellsAttribute})
    {
        StateValues.Add(Id, Project.CurrentVars.FindChecked(Id).Value);
    }
    Arcweave->OnArcweaveVariableChanged.AddUniqueDynamic(this, &UQuestDirector::HandleVariablesChanged);
    bProjectLoaded = true;

    // Startup computes the authored objective/UI without simulating a world interaction.
    if (!RefreshPresentation(Error))
    {
        return false;
    }
    Error.Empty();
    PublishChange();
    return true;
}

bool UQuestDirector::StartQuest(FString& Error)
{
    return RunEvent(TEXT("use_terminal"), Error);
}

bool UQuestDirector::CollectCell(FName CellId, FString& Error)
{
    // Physical interactions and Play Mode choices supply the same authored cell identity.
    PendingCellId = CellId;
    const bool bResult = RunEvent(TEXT("collect_cell"), Error, CellId);
    PendingCellId = NAME_None;
    return bResult;
}

bool UQuestDirector::TryRestorePower(FString& Error)
{
    return RunEvent(TEXT("check_generator"), Error);
}

bool UQuestDirector::ReachExit(FString& Error)
{
    return RunEvent(TEXT("enter_exit"), Error);
}

bool UQuestDirector::IsQuestStarted() const
{
    return bProjectLoaded && StateValues.FindChecked(QuestBindings::QuestStartedAttribute)
        .Equals(TEXT("true"), ESearchCase::CaseSensitive);
}

bool UQuestDirector::IsQuestCompleted() const
{
    return bProjectLoaded && StateValues.FindChecked(QuestBindings::QuestCompletedAttribute)
        .Equals(TEXT("true"), ESearchCase::CaseSensitive);
}

bool UQuestDirector::IsPowerRestored() const
{
    return bProjectLoaded && StateValues.FindChecked(QuestBindings::PowerRestoredAttribute)
        .Equals(TEXT("true"), ESearchCase::CaseSensitive);
}

int32 UQuestDirector::GetPowerCellCount() const
{
    return bProjectLoaded ? FCString::Atoi(*StateValues.FindChecked(QuestBindings::PowerCellsAttribute)) : 0;
}

int32 UQuestDirector::GetRequiredPowerCellCount() const
{
    return bProjectLoaded ? FCString::Atoi(*StateValues.FindChecked(QuestBindings::RequiredPowerCellsAttribute)) : 0;
}

void UQuestDirector::HandleVariablesChanged(const TArray<FArcweaveVariable>& Variables)
{
    for (const FArcweaveVariable& Variable : Variables)
    {
        if (FString* Value = StateValues.Find(Variable.Id))
        {
            *Value = Variable.Value;
        }
    }
}

bool UQuestDirector::RunEvent(const FString& EventType, FString& Error, FName CellId)
{
    if (!bProjectLoaded)
    {
        return RejectInteraction(TEXT("The local narrative export has not been loaded. Restart after checking the export file."), Error);
    }

    // Replace the complete event input each time; pickup context cannot leak into later events.
    Arcweave->SetVariable(QuestBindings::EventTypeAttribute, EventType);
    Arcweave->SetVariable(QuestBindings::CellIdAttribute, CellId.IsNone() ? FString() : CellId.ToString());

    FArcweaveElementData LastElement;
    if (!RunGraph(Arcweave->GetArcweaveProjectData().StartingElementId, true, LastElement, Error) || !RefreshPresentation(Error))
    {
        return false;
    }
    Error.Empty();
    PublishChange();
    return true;
}

bool UQuestDirector::RunGraph(const FString& EntryElementId, bool bDispatchCommands,
    FArcweaveElementData& LastElement, FString& Error)
{
    FString ElementId = EntryElementId;
    // Each interaction or optional query returns to Station. Native execution stops
    // before the menu; RunEvent refreshes the HUD independently after every event.
    const FString StopElementId = Arcweave->GetArcweaveProjectData().StartingElementId;
    while (true)
    {
        bool bSuccess = false;
        LastElement = Arcweave->TranspileObject(ElementId, bSuccess);
        if (!bSuccess)
        {
            return RejectInteraction(TEXT("Could not execute the requested narrative element."), Error);
        }

        if (bDispatchCommands)
        {
            CurrentElementId = LastElement.Id;
            if (!LastElement.Content.IsEmpty())
            {
                Status = LastElement.Content;
            }
            for (const FArcweaveComponentData& Component : LastElement.Components)
            {
                const TFunction<void()>* Handler = CommandHandlers.Find(FName(*Component.CustomId));
                if (!Handler)
                {
                    return RejectInteraction(FString::Printf(TEXT("No C++ handler is registered for '%s'."), *Component.CustomId), Error);
                }
                (*Handler)();
            }
        }

        if (LastElement.Outputs.IsEmpty())
        {
            return true;
        }

        FArcweaveElementData Source;
        FArcweaveBoardData* Board = nullptr;
        Arcweave->GetBoardForObject(LastElement.Id, Source, Board);
        const FArcweaveConnectionsData* Output = &LastElement.Outputs[0];
        if (LastElement.Id == StopElementId)
        {
            // Gameplay choices share one branch destination. The inventory/objective
            // choices use jumpers and must not intercept a physical Unreal event.
            Output = LastElement.Outputs.FindByPredicate([](const FArcweaveConnectionsData& Candidate)
            {
                return Candidate.TargetType == TEXT("branches");
            });
            if (!Output)
            {
                return RejectInteraction(TEXT("The station menu is missing its gameplay route."), Error);
            }
        }
        // Menu label assignments are Play Mode inputs; Unreal has already supplied them.
        FArcweaveConnectionsData Connection = *Output;
        FGetIsTargetBranchOutput Branch = Arcweave->GetIsTargetBranch(*Board, Connection);
        while (Branch.IsBranch)
        {
            if (Branch.BranchConnections.IsEmpty())
            {
                return RejectInteraction(TEXT("The authored branch has no destination."), Error);
            }
            Connection = Branch.BranchConnections[0];
            Branch = Arcweave->GetIsTargetBranch(*Board, Connection);
        }
        FString NextElementId = Connection.Targetid;
        if (Connection.TargetType == TEXT("jumpers"))
        {
            const FArcweaveJumpersData* Jumper = Board->Jumpers.FindByPredicate(
                [&Connection](const FArcweaveJumpersData& Candidate) { return Candidate.Id == Connection.Targetid; });
            if (!Jumper)
            {
                return RejectInteraction(TEXT("The authored return jumper is missing."), Error);
            }
            NextElementId = Jumper->ElementData.Id;
        }
        if (NextElementId == StopElementId)
        {
            return true;
        }
        ElementId = NextElementId;
    }
}

bool UQuestDirector::RefreshPresentation(FString& Error)
{
    // The graph resets quest_ui defaults and applies state-specific overrides without
    // world commands. It runs once per event; HUD and focus getters only read the cache.
    FArcweaveElementData Presentation;
    if (!RunGraph(PresentationEntryId, false, Presentation, Error))
    {
        return false;
    }
    PresentationElementId = Presentation.Id;
    Objective = Presentation.Content;
    // Read current values after Arcscript has updated quest_ui. Shared hud and world_text
    // variables also retain runtime changes until the game explicitly changes or resets them.
    const FArcweaveProjectData Project = Arcweave->GetArcweaveProjectData();
    UIText.Reset();
    for (const auto& Variable : UIVariableIds)
    {
        UIText.Add(Variable.Key, Project.CurrentVars.FindChecked(Variable.Value).Value);
    }
    return true;
}

bool UQuestDirector::RejectInteraction(const FString& Message, FString& Error)
{
    Error = Message;
    Status = Message;
    OnQuestChanged.Broadcast();
    return false;
}

void UQuestDirector::PublishChange()
{
    OnQuestChanged.Broadcast();
}
