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
    CommandHandlers.Add(TEXT("restore_power"), [this] { bPowerRestored = true; });
    CommandHandlers.Add(TEXT("open_gate"), [this] { bGateOpen = true; });
    CommandHandlers.Add(TEXT("collect_cell"), [this]
    {
        CollectedCells.Add(PendingCellId);
        Arcweave->SetVariable(QuestBindings::PowerCellsVariable, FString::FromInt(CollectedCells.Num()));
    });
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
    PresentationElementId.Empty();
    Status.Empty();
    bPowerRestored = false;
    bGateOpen = false;

    const FArcweaveProjectData Project = Arcweave->GetArcweaveProjectData();
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
    // Physical item identity belongs to Unreal. Arcweave owns permission and feedback.
    PendingCellId = CellId;
    const bool bResult = RunEvent(TEXT("collect_cell"), Error, CollectedCells.Contains(CellId));
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
    return bProjectLoaded && Arcweave->GetArcweaveProjectData().CurrentVars.FindChecked(
        QuestBindings::QuestStartedVariable).Value.Equals(TEXT("true"), ESearchCase::CaseSensitive);
}

bool UQuestDirector::IsQuestCompleted() const
{
    return bProjectLoaded && Arcweave->GetArcweaveProjectData().CurrentVars.FindChecked(
        QuestBindings::QuestCompletedVariable).Value.Equals(TEXT("true"), ESearchCase::CaseSensitive);
}

int32 UQuestDirector::GetPowerCellCount() const
{
    return bProjectLoaded ? FCString::Atoi(*Arcweave->GetArcweaveProjectData().CurrentVars.FindChecked(
        QuestBindings::PowerCellsVariable).Value) : 0;
}

int32 UQuestDirector::GetRequiredPowerCellCount() const
{
    return bProjectLoaded ? FCString::Atoi(*Arcweave->GetArcweaveProjectData().CurrentVars.FindChecked(
        QuestBindings::RequiredPowerCellsVariable).Value) : 0;
}

bool UQuestDirector::RunEvent(const FString& EventType, FString& Error, bool bCellAlreadyCollected)
{
    if (!bProjectLoaded)
    {
        return RejectInteraction(TEXT("The local narrative export has not been loaded. Restart after checking the export file."), Error);
    }

    // Replace the complete event input each time; pickup context cannot leak into later events.
    Arcweave->SetVariable(QuestBindings::EventTypeAttribute, EventType);
    Arcweave->SetVariable(QuestBindings::CellAlreadyCollectedAttribute, bCellAlreadyCollected ? TEXT("true") : TEXT("false"));

    FArcweaveElementData LastElement;
    if (!RunGraph(QuestBindings::EventEntryElement, true, LastElement, Error) || !RefreshPresentation(Error))
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
    // Each authored event is an acyclic sequence with one automatic output per element.
    // Branches select the next connection; an element without outputs waits for a new event.
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
        FArcweaveConnectionsData Connection = LastElement.Outputs[0];
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
        ElementId = Connection.Targetid;
    }
}

bool UQuestDirector::RefreshPresentation(FString& Error)
{
    // The graph resets quest_ui defaults and applies state-specific overrides without
    // world commands. It runs once per event; HUD and focus getters only read the cache.
    FArcweaveElementData Presentation;
    if (!RunGraph(QuestBindings::PresentationEntryElement, false, Presentation, Error))
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
