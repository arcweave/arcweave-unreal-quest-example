#if WITH_DEV_AUTOMATION_TESTS

#include "QuestCharacter.h"
#include "QuestBindings.h"
#include "QuestDirector.h"
#include "QuestWorldActor.h"

#include "ArcweaveVariable.h"
#include "ArcscriptTranspilerOutput.h"
#include "ArcweaveSubsystem.h"
#include "Camera/CameraComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/PointLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

namespace
{
/** Exercises the spawned world and the same focus/interaction path used by the E key. */
class FQuestWorldCommand : public IAutomationLatentCommand
{
public:
    explicit FQuestWorldCommand(FAutomationTestBase& InTest)
        : Test(InTest), Deadline(FPlatformTime::Seconds() + 15.0)
    {
    }

    virtual bool Update() override
    {
        if (FPlatformTime::Seconds() > Deadline)
        {
            Test.AddError(TEXT("Timed out waiting for the running quest world or its gate animation. Run with -game /Game/Maps/PowerStation -NullRHI."));
            return true;
        }

        switch (Step)
        {
        case 0:
            for (const FWorldContext& Context : GEngine->GetWorldContexts())
            {
                UWorld* Candidate = Context.World();
                if ((Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE)
                    && Candidate && Candidate->HasBegunPlay() && Candidate->GetFirstPlayerController())
                {
                    Character = Cast<AQuestCharacter>(Candidate->GetFirstPlayerController()->GetPawn());
                    if (Character.IsValid())
                    {
                        World = Candidate;
                        Director = Candidate->GetGameInstance()->GetSubsystem<UQuestDirector>();
                        EventEntryId = GEngine->GetEngineSubsystem<UArcweaveSubsystem>()->GetArcweaveProjectData().StartingElementId;
                        ++Step;
                        break;
                    }
                }
            }
            return false;

        case 1:
        {
            Test.TestFalse(TEXT("GameMode starts the world with an unaccepted quest"), Director->IsQuestStarted());
            Test.TestEqual(TEXT("World starts with zero power cells"), Director->GetPowerCellCount(), 0);
            Test.TestEqual(TEXT("World uses the authored two-cell requirement"), Director->GetRequiredPowerCellCount(), 2);
            Test.TestFalse(TEXT("World starts without power"), Director->IsPowerRestored());
            Test.TestFalse(TEXT("World starts without completion"), Director->IsQuestCompleted());
            Test.TestEqual(TEXT("World startup does not simulate an interaction"), Visits(EventEntryId), 0);
            Test.TestTrue(TEXT("World startup has no gameplay cursor"), Director->GetCurrentElementId().IsEmpty());
            Test.TestTrue(TEXT("World startup has no interaction feedback"), Director->GetStatus().IsEmpty());
            Test.TestEqual(TEXT("World startup computes the authored initial objective"), Director->GetObjective(), FString(TEXT("Use the terminal to begin.")));
            Test.TestTrue(TEXT("World startup leaves the event type empty"), Variable(QuestBindings::EventTypeAttribute).IsEmpty());
            Test.TestTrue(TEXT("World startup leaves the pickup identity empty"), Variable(QuestBindings::CellIdAttribute).IsEmpty());
            Test.TestEqual(TEXT("World startup leaves shared cell A available"), Variable(QuestBindings::CellACollectedAttribute), FString(TEXT("false")));
            Test.TestEqual(TEXT("World startup leaves shared cell B available"), Variable(QuestBindings::CellBCollectedAttribute), FString(TEXT("false")));
            InitialVisits = GEngine->GetEngineSubsystem<UArcweaveSubsystem>()->GetArcweaveProjectData().Visits;
            FHitResult Hit;
            Test.TestTrue(TEXT("Closed gate blocks the actual doorway collision trace"), TraceGate(Hit));
            Gate = Cast<AQuestWorldActor>(Hit.GetActor());
            GatePanel = Hit.GetComponent();
            if (!Test.TestNotNull(TEXT("Doorway blocker is the native gate actor"), Gate.Get())
                || !Test.TestNotNull(TEXT("Closed gate has a collision component"), GatePanel.Get()))
            {
                return true;
            }
            ClosedPanelHeight = GatePanel->GetComponentLocation().Z;
            InitialGateLabelTransform = Gate->FindComponentByClass<UTextRenderComponent>()->GetComponentTransform();
            Test.TestTrue(TEXT("Gate sign faces into the station like the fixed wall signs"),
                InitialGateLabelTransform.GetRotation().GetForwardVector().Equals(FVector(-1, 0, 0), 0.001));
            for (TActorIterator<APointLight> It(World.Get()); It; ++It)
            {
                UPointLightComponent* Light = Cast<UPointLightComponent>(It->GetLightComponent());
                StationLights.Emplace(Light, Light->Intensity);
            }
            Test.TestTrue(TEXT("Native station fixtures exist in the running world"), StationLights.Num() > 0);
            Generator = InteractAt(TEXT("generator"));
            if (!Generator.IsValid()) return true;
            Test.TestEqual(TEXT("The world generator label reads authored offline text"),
                Generator->FindComponentByClass<UTextRenderComponent>()->Text.ToString(),
                Director->GetUIText(TEXT("quest_ui.generator_label")));
            Test.TestFalse(TEXT("Actual generator interaction before the terminal cannot start the quest"), Director->IsQuestStarted());
            Test.TestFalse(TEXT("Authored prerequisite response leaves world power off"), Director->IsPowerRestored());
            Test.TestFalse(TEXT("Authored prerequisite response leaves the gate closed"), Director->IsGateOpen());
            Test.TestEqual(TEXT("Actual generator interaction enters the authored terminal-required node"),
                Director->GetCurrentElementId(), FString(QuestBindings::TerminalRequiredElement));
            Test.TestEqual(TEXT("Actual preterminal interaction visits the authored guidance node"), Visits(QuestBindings::TerminalRequiredElement), 1);
            Test.TestTrue(TEXT("World feedback includes the guidance node's authored explanation"),
                Director->GetStatus().Contains(TEXT("The generator is waiting for authorization.")));
            ++Step;
            return false;
        }

        case 2:
            CellA = InteractAt(TEXT("cell_a"));
            if (!CellA.IsValid()) return true;
            Test.TestEqual(TEXT("Real pickup before acceptance uses the authored prerequisite"),
                Director->GetCurrentElementId(), FString(QuestBindings::PickupTerminalRequiredElement));
            Test.TestEqual(TEXT("Denied world pickup leaves the narrative count at zero"), Director->GetPowerCellCount(), 0);
            Test.TestEqual(TEXT("Denied world pickup leaves the shared collected flag false"), Variable(QuestBindings::CellACollectedAttribute), FString(TEXT("false")));
            Test.TestFalse(TEXT("Denied world pickup remains visible"), CellA->IsHidden());
            Test.TestTrue(TEXT("Denied world pickup retains collision"), CellA->GetActorEnableCollision());
            Test.TestEqual(TEXT("Physical cell label comes from the authored catalog"),
                CellA->FindComponentByClass<UTextRenderComponent>()->Text.ToString(),
                Director->GetUIText(TEXT("world_text.cell_a_label")));
            EnterExit();
            ++Step;
            return false;

        case 3:
            Test.TestTrue(TEXT("Moving the camera around the station leaves the gate sign fixed"),
                Gate->FindComponentByClass<UTextRenderComponent>()->GetComponentTransform().Equals(InitialGateLabelTransform, 0.001));
            // Teleporting uses the character capsule's normal overlap path; never call ReachExit here.
            if (World->GetTimeSeconds() - PhaseStart < 0.1) return false;
            Test.TestEqual(TEXT("Real exit overlap before power executes authored denial"),
                Director->GetCurrentElementId(), FString(QuestBindings::ExitDeniedElement));
            CheckExitEvent();
            Test.TestFalse(TEXT("An early physical exit overlap cannot complete the task"), Director->IsQuestCompleted());
            Test.TestFalse(TEXT("An early exit overlap cannot restore power"), Director->IsPowerRestored());
            if (!InteractAt(TEXT("terminal"))) return true;
            if (!Test.TestTrue(TEXT("Tracing and interacting with the actual terminal starts the quest"), Director->IsQuestStarted())) return true;
            ++Step;
            return false;

        case 4:
            if (!InteractAt(TEXT("generator"))) return true;
            Test.TestFalse(TEXT("World generator takes the insufficient-cell branch"), Director->IsPowerRestored());
            Test.TestFalse(TEXT("Insufficient-cell branch leaves the actual gate closed"), Director->IsGateOpen());
            Test.TestEqual(TEXT("Insufficient-cell interaction reaches the authored response"),
                Director->GetCurrentElementId(), FString(QuestBindings::MissingCellsElement));
            Test.TestFalse(TEXT("Insufficient-cell response is shown to the player"), Director->GetStatus().IsEmpty());
            ++Step;
            return false;

        case 5:
            CellA = InteractAt(TEXT("cell_a"));
            if (!CellA.IsValid()) return true;
            Test.TestEqual(TEXT("Actual cell A pickup updates the narrative variable"), Director->GetPowerCellCount(), 1);
            Test.TestEqual(TEXT("Actual cell A pickup updates its shared collected flag"), Variable(QuestBindings::CellACollectedAttribute), FString(TEXT("true")));
            Test.TestEqual(TEXT("Actual cell A pickup leaves cell B available in the narrative"), Variable(QuestBindings::CellBCollectedAttribute), FString(TEXT("false")));
            Test.TestEqual(TEXT("World pickup displays feedback after the count update"),
                Director->GetStatus(), FString(TEXT("Collected a power cell (1/2).")));
            Test.TestTrue(TEXT("Quest notification hides the actual collected cell A actor"), CellA->IsHidden());
            Test.TestFalse(TEXT("Collected cell A no longer blocks collision"), CellA->GetActorEnableCollision());
            {
                const int32 BeforeHiddenInteraction = Visits(EventEntryId);
                Character->QuestInteract();
                Test.TestEqual(TEXT("Pressing interact again at the collected pickup cannot duplicate it"), Director->GetPowerCellCount(), 1);
                Test.TestEqual(TEXT("A hidden pickup no longer generates physical interaction events"), Visits(EventEntryId), BeforeHiddenInteraction);
            }
            ++Step;
            return false;

        case 6:
            CellB = InteractAt(TEXT("cell_b"));
            if (!CellB.IsValid()) return true;
            Test.TestEqual(TEXT("Actual cell B pickup updates the narrative variable"), Director->GetPowerCellCount(), 2);
            Test.TestEqual(TEXT("Actual cell B pickup updates its shared collected flag"), Variable(QuestBindings::CellBCollectedAttribute), FString(TEXT("true")));
            Test.TestTrue(TEXT("Quest notification hides the actual collected cell B actor"), CellB->IsHidden());
            Test.TestFalse(TEXT("Collected cell B no longer blocks collision"), CellB->GetActorEnableCollision());
            ++Step;
            return false;

        case 7:
            Generator = InteractAt(TEXT("generator"));
            if (!Generator.IsValid()) return true;
            if (!Test.TestTrue(TEXT("Actual generator interaction now restores power"), Director->IsPowerRestored())) return true;
            Test.TestTrue(TEXT("Authored command opens the world gate"), Director->IsGateOpen());
            Test.TestFalse(TEXT("World power restoration alone does not complete the task"), Director->IsQuestCompleted());
            Test.TestEqual(TEXT("Power restoration refreshes the authored world generator label"),
                Generator->FindComponentByClass<UTextRenderComponent>()->Text.ToString(),
                Director->GetUIText(TEXT("quest_ui.generator_label")));
            Test.TestEqual(TEXT("Gate label reads the authored access text"),
                Gate->FindComponentByClass<UTextRenderComponent>()->Text.ToString(),
                Director->GetUIText(TEXT("quest_ui.gate_label")));
            Test.TestEqual(TEXT("World power restoration selects the exit objective"),
                Director->GetPresentationElementId(), FString(QuestBindings::PresentationPoweredElement));
            for (const auto& Light : StationLights)
            {
                Test.TestTrue(TEXT("World notification increases each native station light's intensity"), Light.Key->Intensity > Light.Value);
            }
            AnimationStart = World->GetTimeSeconds();
            Deadline = FPlatformTime::Seconds() + 15.0;
            ++Step;
            return false;

        case 8:
        {
            // Advance through normal game ticks; do not call actor Tick or move the panel from the test.
            if (World->GetTimeSeconds() - AnimationStart < 3.0) return false;
            Character->QuestView(TEXT("gate"));
            FHitResult Hit;
            Test.TestFalse(TEXT("Opened gate leaves the actual doorway collision trace clear"), TraceGate(Hit));
            Test.TestTrue(TEXT("The collision panel itself moved upward through its normal animation"),
                GatePanel->GetComponentLocation().Z > ClosedPanelHeight + 400.0);
            Test.TestFalse(TEXT("Finishing the gate animation still waits for the physical exit"), Director->IsQuestCompleted());
            Test.TestTrue(TEXT("Opening the moving panel leaves the gate sign fixed on its frame"),
                Gate->FindComponentByClass<UTextRenderComponent>()->GetComponentTransform().Equals(InitialGateLabelTransform, 0.001));
            const FArcweaveProjectData Before = GEngine->GetEngineSubsystem<UArcweaveSubsystem>()->GetArcweaveProjectData();
            for (int32 Read = 0; Read < 32; ++Read)
            {
                Test.TestEqual(TEXT("Native interaction text reads the cached authored generator prompt"),
                    Generator->GetInteractionText(*Director), Director->GetUIText(TEXT("quest_ui.generator_prompt")));
                Gate->GetInteractionText(*Director);
                Director->GetObjective();
            }
            Test.TestTrue(TEXT("Repeated native prompt reads leave every narrative visit unchanged"),
                Before.Visits.OrderIndependentCompareEqual(GEngine->GetEngineSubsystem<UArcweaveSubsystem>()->GetArcweaveProjectData().Visits));
            EnterExit();
            ++Step;
            return false;
        }

        case 9:
            if (World->GetTimeSeconds() - PhaseStart < 0.1) return false;
            Test.TestTrue(TEXT("Entering the real exit volume completes the powered task"), Director->IsQuestCompleted());
            Test.TestEqual(TEXT("The physical exit overlap executes the authored completion node"),
                Director->GetCurrentElementId(), FString(QuestBindings::CompletedElement));
            Test.TestEqual(TEXT("Physical completion selects the completed presentation"),
                Director->GetPresentationElementId(), FString(QuestBindings::PresentationCompletedElement));
            Test.TestEqual(TEXT("Physical exit entry completes exactly once"), Visits(QuestBindings::CompletedElement), 1);
            CheckExitEvent();
            Character->QuestView(TEXT("gate"));
            PhaseStart = World->GetTimeSeconds();
            ++Step;
            return false;

        case 10:
            if (World->GetTimeSeconds() - PhaseStart < 0.1) return false;
            EnterExit();
            ++Step;
            return false;

        case 11:
        {
            if (World->GetTimeSeconds() - PhaseStart < 0.1) return false;
            Test.TestEqual(TEXT("Physical exit reentry executes authored repeated-entry feedback"),
                Director->GetCurrentElementId(), FString(QuestBindings::ExitAlreadyCompletedElement));
            Test.TestEqual(TEXT("Physical reentry does not execute completion twice"), Visits(QuestBindings::CompletedElement), 1);
            Test.TestTrue(TEXT("Repeated physical exit entry preserves completion"), Director->IsQuestCompleted());
            CheckExitEvent();
            // Leave the trigger before restarting so the next session begins outside it.
            Character->QuestView(TEXT("gate"));
            FString Error;
            if (!Test.TestTrue(TEXT("Restart reloads the narrative in the same running world"), Director->StartNewGame(Error)))
            {
                Test.AddError(Error);
                return true;
            }
            ++Step;
            return false;
        }

        case 12:
        {
            Test.TestFalse(TEXT("World restart clears the quest flag"), Director->IsQuestStarted());
            Test.TestFalse(TEXT("World restart clears completion"), Director->IsQuestCompleted());
            Test.TestEqual(TEXT("World restart restores the narrative cell count"), Director->GetPowerCellCount(), 0);
            Test.TestFalse(TEXT("World restart turns power off"), Director->IsPowerRestored());
            Test.TestFalse(TEXT("World restart closes the gate"), Director->IsGateOpen());
            Test.TestFalse(TEXT("World restart makes cell A visible again"), CellA->IsHidden());
            Test.TestFalse(TEXT("World restart makes cell B visible again"), CellB->IsHidden());
            Test.TestTrue(TEXT("World restart restores cell A collision"), CellA->GetActorEnableCollision());
            Test.TestTrue(TEXT("World restart restores cell B collision"), CellB->GetActorEnableCollision());
            Test.TestEqual(TEXT("World restart does not simulate an interaction"), Visits(EventEntryId), 0);
            Test.TestTrue(TEXT("World restart clears the gameplay cursor"), Director->GetCurrentElementId().IsEmpty());
            Test.TestTrue(TEXT("World restart clears gameplay feedback"), Director->GetStatus().IsEmpty());
            Test.TestTrue(TEXT("World restart clears the last event type"), Variable(QuestBindings::EventTypeAttribute).IsEmpty());
            Test.TestTrue(TEXT("World restart clears the pickup identity"), Variable(QuestBindings::CellIdAttribute).IsEmpty());
            Test.TestEqual(TEXT("World restart resets shared cell A"), Variable(QuestBindings::CellACollectedAttribute), FString(TEXT("false")));
            Test.TestEqual(TEXT("World restart resets shared cell B"), Variable(QuestBindings::CellBCollectedAttribute), FString(TEXT("false")));
            Test.TestEqual(TEXT("World restart clears the previous completion visit"), Visits(QuestBindings::CompletedElement), 0);
            Test.TestEqual(TEXT("World restart selects the unaccepted presentation"),
                Director->GetPresentationElementId(), FString(QuestBindings::PresentationUnacceptedElement));
            Test.TestTrue(TEXT("World restart restores the visits of a freshly initialized session"),
                InitialVisits.OrderIndependentCompareEqual(GEngine->GetEngineSubsystem<UArcweaveSubsystem>()->GetArcweaveProjectData().Visits));
            FHitResult Hit;
            Test.TestTrue(TEXT("Reset gate once again blocks the real doorway"), TraceGate(Hit));
            Test.TestTrue(TEXT("Reset doorway obstruction belongs to the same gate actor"), Hit.GetActor() == Gate.Get());
            Test.TestTrue(TEXT("Reset returns the panel to its closed position"),
                FMath::IsNearlyEqual(GatePanel->GetComponentLocation().Z, ClosedPanelHeight, 0.1));
            Test.TestTrue(TEXT("Reset preserves the fixed gate sign transform"),
                Gate->FindComponentByClass<UTextRenderComponent>()->GetComponentTransform().Equals(InitialGateLabelTransform, 0.001));
            for (const auto& Light : StationLights)
            {
                Test.TestEqual(TEXT("World restart restores each fixture's original intensity"), Light.Key->Intensity, Light.Value);
            }
            // A state update drives the station through its normal notification path.
            // Opening the gate remains a separately authored engine action.
            UArcweaveSubsystem* Arcweave = GEngine->GetEngineSubsystem<UArcweaveSubsystem>();
            Arcweave->SetVariable(QuestBindings::PowerRestoredAttribute, TEXT("true"));
            Test.TestTrue(TEXT("The world power getter reads the changed quest attribute immediately"), Director->IsPowerRestored());
            if (!InteractAt(TEXT("terminal"))) return true;
            Test.TestEqual(TEXT("A normal world interaction reads the external power milestone"),
                Director->GetCurrentElementId(), FString(QuestBindings::TerminalPoweredElement));
            Test.TestEqual(TEXT("External power refresh selects the powered presentation"),
                Director->GetPresentationElementId(), FString(QuestBindings::PresentationPoweredElement));
            Test.TestEqual(TEXT("External power refresh never executes the generator success element"), Visits(QuestBindings::SuccessElement), 0);
            Test.TestFalse(TEXT("Power state alone does not issue the gate-opening action"), Director->IsGateOpen());
            Test.TestTrue(TEXT("The separately controlled gate still blocks the doorway"), TraceGate(Hit));
            for (const auto& Light : StationLights)
            {
                Test.TestTrue(TEXT("Normal world refresh lights the station from the scoped power value"), Light.Key->Intensity > Light.Value);
            }
            Arcweave->SetVariable(QuestBindings::PowerRestoredAttribute, TEXT("false"));
            Test.TestFalse(TEXT("Clearing scoped power immediately clears the world power getter"), Director->IsPowerRestored());
            if (!InteractAt(TEXT("terminal"))) return true;
            Test.TestEqual(TEXT("World presentation follows power back to the collecting state"),
                Director->GetPresentationElementId(), FString(QuestBindings::PresentationCollectingElement));
            Test.TestFalse(TEXT("Normal world refresh does not restore a stale native power flag"), Director->IsPowerRestored());
            for (const auto& Light : StationLights)
            {
                Test.TestEqual(TEXT("World fixtures return to their unpowered intensity from the scoped value"), Light.Key->Intensity, Light.Value);
            }
            FString Error;
            if (!Test.TestTrue(TEXT("The externally changed state also resets through normal restart"), Director->StartNewGame(Error)))
            {
                Test.AddError(Error);
                return true;
            }
            Character->QuestView(TEXT("overview"));
            return true;
        }
        }
        return true;
    }

private:
    int32 Visits(const FString& Id) const
    {
        return GEngine->GetEngineSubsystem<UArcweaveSubsystem>()->GetArcweaveProjectData().Visits.FindChecked(Id);
    }

    FString Variable(const TCHAR* Id) const
    {
        return GEngine->GetEngineSubsystem<UArcweaveSubsystem>()->GetArcweaveProjectData().CurrentVars.FindChecked(Id).Value;
    }

    void EnterExit()
    {
        BeforeExitVisits = ExitResponseVisits();
        BeforeExitEventVisits = Visits(EventEntryId);
        Character->QuestView(TEXT("exit"));
        PhaseStart = World->GetTimeSeconds();
    }

    int32 ExitResponseVisits() const
    {
        return Visits(QuestBindings::ExitDeniedElement) + Visits(QuestBindings::CompletedElement)
            + Visits(QuestBindings::ExitAlreadyCompletedElement);
    }

    void CheckExitEvent()
    {
        Test.TestEqual(TEXT("Physical exit overlap enters the shared event graph once"), Visits(EventEntryId), BeforeExitEventVisits + 1);
        Test.TestEqual(TEXT("Physical exit overlap executes exactly one authored exit response"), ExitResponseVisits(), BeforeExitVisits + 1);
        Test.TestEqual(TEXT("Physical exit overlap supplies the exit event type"), Variable(QuestBindings::EventTypeAttribute), FString(TEXT("enter_exit")));
        Test.TestTrue(TEXT("Physical exit overlap clears the pickup identity"), Variable(QuestBindings::CellIdAttribute).IsEmpty());
    }

    AQuestWorldActor* InteractAt(const TCHAR* View)
    {
        Character->QuestView(View);
        FVector Eyes;
        FRotator Rotation;
        Character->GetActorEyesViewPoint(Eyes, Rotation);
        const UCameraComponent* Camera = Character->FindComponentByClass<UCameraComponent>();
        FHitResult Hit;
        const FCollisionQueryParams Query(SCENE_QUERY_STAT(QuestWorldTestInteraction), false, Character.Get());
        World->LineTraceSingleByChannel(Hit, Camera->GetComponentLocation(),
            Camera->GetComponentLocation() + Rotation.Vector() * 340.0f, ECC_Visibility, Query);
        AQuestWorldActor* Target = Cast<AQuestWorldActor>(Hit.GetActor());
        if (Test.TestNotNull(FString::Printf(TEXT("%s viewpoint traces a real interactive actor"), View), Target))
        {
            const int32 BeforeEventVisits = Visits(EventEntryId);
            Character->QuestInteract();
            const FString ExpectedEvent = FString(View) == TEXT("terminal") ? TEXT("use_terminal")
                : FString(View) == TEXT("generator") ? TEXT("check_generator") : TEXT("collect_cell");
            Test.TestEqual(FString(View) + TEXT(" interaction enters the shared event graph exactly once"),
                Visits(EventEntryId), BeforeEventVisits + 1);
            Test.TestEqual(FString(View) + TEXT(" interaction supplies its event type"), Variable(QuestBindings::EventTypeAttribute), ExpectedEvent);
            const FString ExpectedCellId = ExpectedEvent == TEXT("collect_cell") ? FString(View) : FString();
            Test.TestEqual(FString(View) + TEXT(" interaction supplies its physical cell identity or clears it"),
                Variable(QuestBindings::CellIdAttribute), ExpectedCellId);
        }
        return Target;
    }

    bool TraceGate(FHitResult& Hit) const
    {
        const FCollisionQueryParams Query(SCENE_QUERY_STAT(QuestWorldTestGate), false, Character.Get());
        return World->LineTraceSingleByChannel(Hit, FVector(2050, 0, 150), FVector(2380, 0, 150), ECC_Visibility, Query);
    }

    FAutomationTestBase& Test;
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<AQuestCharacter> Character;
    TWeakObjectPtr<UQuestDirector> Director;
    TWeakObjectPtr<AQuestWorldActor> CellA;
    TWeakObjectPtr<AQuestWorldActor> CellB;
    TWeakObjectPtr<AQuestWorldActor> Gate;
    TWeakObjectPtr<AQuestWorldActor> Generator;
    TWeakObjectPtr<UPrimitiveComponent> GatePanel;
    TArray<TPair<TWeakObjectPtr<UPointLightComponent>, float>> StationLights;
    TMap<FString, int32> InitialVisits;
    FString EventEntryId;
    int32 Step = 0;
    int32 BeforeExitVisits = 0;
    int32 BeforeExitEventVisits = 0;
    double Deadline;
    double AnimationStart = 0.0;
    double PhaseStart = 0.0;
    double ClosedPanelHeight = 0.0;
    FTransform InitialGateLabelTransform;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FArcweaveQuestWorldTest,
    "ArcweaveQuest.World",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArcweaveQuestWorldTest::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FQuestWorldCommand(*this));
    return true;
}

#endif
