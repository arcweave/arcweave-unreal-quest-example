#pragma once

#include "ArcweaveProjectData.h"
#include "Misc/AutomationTest.h"

/** Test expectations use authored titles and custom IDs, independently of the route being tested. */
struct FQuestTestNarrative
{
    FString TerminalAcceptElement;
    FString SuccessElement;
    FString MissingCellsElement;
    FString TerminalRequiredElement;
    FString DuplicatePickupElement;
    FString CompletedElement;
    FString TerminalAcceptedElement;
    FString TerminalPoweredElement;
    FString TerminalCompletedElement;
    FString AlreadyOnlineElement;
    FString PickupTerminalRequiredElement;
    FString PickupActionElement;
    FString ExitDeniedElement;
    FString ExitAlreadyCompletedElement;
    FString PresentationCompletedElement;
    FString PresentationPoweredElement;
    FString PresentationUnacceptedElement;
    FString PresentationCollectingElement;
    FString PresentationReadyElement;
    FString OpenGateComponent;
    FString HUDTextComponent;
    FString WorldTextComponent;
    FString QuestUIComponent;
    FString GameEventComponent;
    FString PlayerComponent;
    FString QuestStateComponent;
    FString CellAComponent;
    FString CellBComponent;
    FString SaveUIComponent;
    FString EventTypeAttribute;
    FString PowerCellsAttribute;
    FString QuestStartedAttribute;
    FString PowerRestoredAttribute;
    FString QuestCompletedAttribute;
    FString RequiredPowerCellsAttribute;
    FString CellACollectedAttribute;
    FString CellBCollectedAttribute;
    FString CellIdAttribute;
    FString EventRouterBranch;

    bool Resolve(const FArcweaveProjectData& Project, FAutomationTestBase& Test)
    {
        bool bValid = true;
        const auto CheckUnique = [&Test, &bValid](const TCHAR* Name, int32 Matches)
        {
            bValid = Test.TestEqual(FString(TEXT("One narrative object matches ")) + Name, Matches, 1) && bValid;
        };
        struct FExpected
        {
            FString* Id;
            const TCHAR* Name;
        };
        const FExpected Elements[] = {
            {&TerminalAcceptElement, TEXT("Terminal · accept task")},
            {&SuccessElement, TEXT("Generator · restore power")},
            {&MissingCellsElement, TEXT("Generator · missing cells")},
            {&TerminalRequiredElement, TEXT("Generator · terminal required")},
            {&DuplicatePickupElement, TEXT("Pickup denied · already collected")},
            {&CompletedElement, TEXT("Exit reached · task complete")},
            {&TerminalAcceptedElement, TEXT("Terminal · task already accepted")},
            {&TerminalPoweredElement, TEXT("Terminal · exit is ready")},
            {&TerminalCompletedElement, TEXT("Terminal · task already complete")},
            {&AlreadyOnlineElement, TEXT("Generator · already online")},
            {&PickupTerminalRequiredElement, TEXT("Pickup denied · accept the task")},
            {&PickupActionElement, TEXT("Pickup accepted · collect the cell")},
            {&ExitDeniedElement, TEXT("Exit denied · restore power")},
            {&ExitAlreadyCompletedElement, TEXT("Exit · already complete")},
            {&PresentationCompletedElement, TEXT("Display · completed")},
            {&PresentationPoweredElement, TEXT("Display · powered")},
            {&PresentationUnacceptedElement, TEXT("Display · unaccepted")},
            {&PresentationCollectingElement, TEXT("Display · collecting")},
            {&PresentationReadyElement, TEXT("Display · ready")},
        };
        for (const FExpected& Expected : Elements)
        {
            Expected.Id->Empty();
            int32 Matches = 0;
            for (const FArcweaveBoardData& Board : Project.Boards)
            {
                for (const FArcweaveElementData& Element : Board.Elements)
                {
                    if (Element.Title == Expected.Name)
                    {
                        *Expected.Id = Element.Id;
                        ++Matches;
                    }
                }
            }
            CheckUnique(Expected.Name, Matches);
        }
        const FExpected Components[] = {
            {&OpenGateComponent, TEXT("open_gate")},
            {&HUDTextComponent, TEXT("hud")},
            {&WorldTextComponent, TEXT("world_text")},
            {&QuestUIComponent, TEXT("quest_ui")},
            {&GameEventComponent, TEXT("game_event")},
            {&PlayerComponent, TEXT("player")},
            {&QuestStateComponent, TEXT("quest")},
            {&CellAComponent, TEXT("cell_a")},
            {&CellBComponent, TEXT("cell_b")},
            {&SaveUIComponent, TEXT("save_ui")},
        };
        for (const FExpected& Expected : Components)
        {
            Expected.Id->Empty();
            int32 Matches = 0;
            for (const FArcweaveComponentData& Component : Project.Components)
            {
                if (Component.CustomId == Expected.Name)
                {
                    *Expected.Id = Component.Id;
                    ++Matches;
                }
            }
            CheckUnique(Expected.Name, Matches);
        }
        const FExpected Variables[] = {
            {&EventTypeAttribute, TEXT("game_event.type")},
            {&PowerCellsAttribute, TEXT("player.power_cells")},
            {&QuestStartedAttribute, TEXT("quest.started")},
            {&PowerRestoredAttribute, TEXT("quest.power_restored")},
            {&QuestCompletedAttribute, TEXT("quest.completed")},
            {&RequiredPowerCellsAttribute, TEXT("quest.required_power_cells")},
            {&CellACollectedAttribute, TEXT("cell_a.collected")},
            {&CellBCollectedAttribute, TEXT("cell_b.collected")},
            {&CellIdAttribute, TEXT("game_event.cell_id")},
        };
        for (const FExpected& Expected : Variables)
        {
            Expected.Id->Empty();
            int32 Matches = 0;
            for (const auto& Pair : Project.CurrentVars)
            {
                const FArcweaveVariable& Variable = Pair.Value;
                if (Variable.Scope + TEXT(".") + Variable.Name == Expected.Name)
                {
                    *Expected.Id = Variable.Id;
                    ++Matches;
                }
            }
            CheckUnique(Expected.Name, Matches);
        }

        TSet<FString> RouterTargets;
        for (const FArcweaveBoardData& Board : Project.Boards)
        {
            for (const FArcweaveElementData& Element : Board.Elements)
            {
                if (Element.Id != Project.StartingElementId) continue;
                for (const FArcweaveConnectionsData& Output : Element.Outputs)
                {
                    if (Output.TargetType == TEXT("branches")) RouterTargets.Add(Output.Targetid);
                }
            }
        }
        CheckUnique(TEXT("the station event router"), RouterTargets.Num());
        EventRouterBranch.Empty();
        for (const FString& Target : RouterTargets) EventRouterBranch = Target;
        return bValid;
    }
};
