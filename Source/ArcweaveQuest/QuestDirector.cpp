#include "QuestDirector.h"

#include "QuestBindings.h"
#include "ArcweaveVariable.h"
#include "ArcscriptTranspilerOutput.h"
#include "ArcweaveSubsystem.h"
#include "Engine/Engine.h"
#include "GetIsTargetBranchOutput.h"

UQuestDirector::UQuestDirector()
{
    // Designers attach these component custom IDs to an element. Game code defines their effects.
    CommandHandlers.Add(TEXT("restore_power"), [this] { bPowerRestored = true; });
    CommandHandlers.Add(TEXT("open_gate"), [this] { bGateOpen = true; });
}

bool UQuestDirector::StartNewGame(FString& Error)
{
    Arcweave = GEngine->GetEngineSubsystem<UArcweaveSubsystem>();
    // The packaged project contains a local export. No API token or network import is required.
    if (!Arcweave->LoadJsonFile())
    {
        return RejectInteraction(TEXT("Could not load Content/ArcweaveExport/quest.json."), Error);
    }

    CollectedCells.Reset();
    CurrentElementId.Empty();
    bPowerRestored = false;
    bGateOpen = false;
    bProjectLoaded = true;
    Status = TEXT("The gate has no power. Use the terminal to begin.");
    Error.Empty();
    PublishChange();
    return true;
}

bool UQuestDirector::StartQuest(FString& Error)
{
    if (!bProjectLoaded)
    {
        return RejectInteraction(TEXT("The local narrative export has not been loaded. Restart after checking the export file."), Error);
    }
    if (IsQuestStarted())
    {
        Status = TEXT("The terminal's task has already been accepted.");
        Error.Empty();
        PublishChange();
        return true;
    }
    if (!EnterElement(QuestBindings::StartElement, Error))
    {
        return false;
    }
    PublishChange();
    return true;
}

bool UQuestDirector::CollectCell(FName CellId, FString& Error)
{
    if (!IsQuestStarted())
    {
        return RejectInteraction(TEXT("Use the terminal to accept the task first."), Error);
    }
    if (CollectedCells.Contains(CellId))
    {
        return RejectInteraction(TEXT("This power cell has already been collected."), Error);
    }

    CollectedCells.Add(CellId);
    Arcweave->SetVariable(QuestBindings::PowerCellsVariable, FString::FromInt(CollectedCells.Num()));
    Status = FString::Printf(TEXT("Collected a power cell (%d/2)."), GetPowerCellCount());
    Error.Empty();
    PublishChange();
    return true;
}

bool UQuestDirector::TryRestorePower(FString& Error)
{
    if (!bProjectLoaded)
    {
        return RejectInteraction(TEXT("The local narrative export has not been loaded. Restart after checking the export file."), Error);
    }
    if (bPowerRestored)
    {
        Status = TEXT("The generator is already running and the gate is open.");
        Error.Empty();
        PublishChange();
        return true;
    }

    if (!EnterElement(QuestBindings::GeneratorElement, Error))
    {
        return false;
    }
    FArcweaveElementData Generator;
    FArcweaveBoardData* Board = nullptr;
    Arcweave->GetBoardForObject(QuestBindings::GeneratorElement, Generator, Board);

    // Arcweave selects terminal guidance, missing-cell guidance, or success from the
    // latest questStarted and powerCells values. Prerequisite feedback is authored text.
    const FGetIsTargetBranchOutput Branch = Arcweave->GetIsTargetBranch(*Board, Generator.Outputs[0]);
    if (!Branch.IsBranch || Branch.BranchConnections.IsEmpty())
    {
        return RejectInteraction(TEXT("The generator's authored branch has no destination."), Error);
    }
    if (!EnterElement(Branch.BranchConnections[0].Targetid, Error))
    {
        return false;
    }

    // A failed narrative condition is a valid interaction. The player can collect cells
    // and try again; the next interaction evaluates the condition afresh.
    PublishChange();
    return true;
}

bool UQuestDirector::IsQuestStarted() const
{
    return bProjectLoaded && Arcweave->GetArcweaveProjectData().CurrentVars.FindChecked(
        QuestBindings::QuestStartedVariable).Value.Equals(TEXT("true"), ESearchCase::CaseSensitive);
}

int32 UQuestDirector::GetPowerCellCount() const
{
    return bProjectLoaded ? FCString::Atoi(*Arcweave->GetArcweaveProjectData().CurrentVars.FindChecked(
        QuestBindings::PowerCellsVariable).Value) : 0;
}

bool UQuestDirector::EnterElement(const FString& ElementId, FString& Error)
{
    bool bSuccess = false;
    const FArcweaveElementData Element = Arcweave->TranspileObject(ElementId, bSuccess);
    if (!bSuccess)
    {
        return RejectInteraction(TEXT("Could not execute the requested narrative element."), Error);
    }

    CurrentElementId = Element.Id;
    Status = Element.Content;
    for (const FArcweaveComponentData& Component : Element.Components)
    {
        const FName Command(*Component.CustomId);
        const TFunction<void()>* Handler = CommandHandlers.Find(Command);
        if (!Handler)
        {
            return RejectInteraction(FString::Printf(TEXT("No C++ handler is registered for '%s'."), *Component.CustomId), Error);
        }
        (*Handler)();
    }
    Error.Empty();
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
    if (bPowerRestored)
    {
        Objective = TEXT("Power restored. Walk through the open gate.");
    }
    else if (!IsQuestStarted())
    {
        Objective = TEXT("Use the terminal to begin.");
    }
    else if (GetPowerCellCount() < 2)
    {
        Objective = FString::Printf(TEXT("Collect power cells (%d/2), then use the generator."), GetPowerCellCount());
    }
    else
    {
        Objective = TEXT("Return to the generator and restore power.");
    }
    OnQuestChanged.Broadcast();
}
