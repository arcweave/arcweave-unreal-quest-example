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

    FArcweaveElementData Catalog;
    FArcweaveBoardData* Board = nullptr;
    if (!Arcweave->GetBoardForObject(QuestBindings::TextCatalogElement, Catalog, Board))
    {
        return RejectInteraction(TEXT("The narrative export is missing its text catalog."), Error);
    }
    CatalogText.Reset();
    for (const FArcweaveAttributeData& Attribute : Catalog.Attributes)
    {
        CatalogText.Add(FName(*Attribute.Name), Attribute.Value.Data);
    }
    bProjectLoaded = true;

    // Initialization supplies guidance without accepting the terminal's task.
    return RunEvent(QuestBindings::InitializationElement, Error);
}

bool UQuestDirector::StartQuest(FString& Error)
{
    return RunEvent(QuestBindings::TerminalEntryElement, Error);
}

bool UQuestDirector::CollectCell(FName CellId, FString& Error)
{
    // Physical item identity belongs to Unreal. Arcweave owns permission and feedback.
    PendingCellId = CellId;
    const bool bResult = RunEvent(CollectedCells.Contains(CellId)
        ? QuestBindings::DuplicatePickupElement : QuestBindings::PickupEntryElement, Error);
    PendingCellId = NAME_None;
    return bResult;
}

bool UQuestDirector::TryRestorePower(FString& Error)
{
    return RunEvent(QuestBindings::GeneratorElement, Error);
}

bool UQuestDirector::ReachExit(FString& Error)
{
    return RunEvent(QuestBindings::ExitEntryElement, Error);
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

bool UQuestDirector::RunEvent(const FString& EntryElementId, FString& Error)
{
    if (!bProjectLoaded)
    {
        return RejectInteraction(TEXT("The local narrative export has not been loaded. Restart after checking the export file."), Error);
    }

    FArcweaveElementData LastElement;
    if (!RunGraph(EntryElementId, true, LastElement, Error) || !RefreshPresentation(Error))
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
    // Presentation nodes contain text/show() only and have no command components.
    // Rendering records their visits once per event; HUD and focus getters never execute nodes.
    FArcweaveElementData Presentation;
    if (!RunGraph(QuestBindings::PresentationEntryElement, false, Presentation, Error))
    {
        return false;
    }
    PresentationElementId = Presentation.Id;
    Objective = Presentation.Content;
    PresentationText.Reset();
    for (const FArcweaveAttributeData& Attribute : Presentation.Attributes)
    {
        PresentationText.Add(FName(*Attribute.Name), Attribute.Value.Data);
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
