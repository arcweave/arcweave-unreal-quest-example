#if WITH_DEV_AUTOMATION_TESTS

#include "QuestCharacter.h"
#include "QuestDirector.h"
#include "QuestWorldActor.h"

#include "Camera/CameraComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PrimitiveComponent.h"
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
            Test.TestFalse(TEXT("World starts without power"), Director->IsPowerRestored());
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
            for (TActorIterator<APointLight> It(World.Get()); It; ++It)
            {
                UPointLightComponent* Light = Cast<UPointLightComponent>(It->GetLightComponent());
                StationLights.Emplace(Light, Light->Intensity);
            }
            Test.TestTrue(TEXT("Native station fixtures exist in the running world"), StationLights.Num() > 0);
            if (!InteractAt(TEXT("generator"))) return true;
            Test.TestFalse(TEXT("Actual generator interaction before the terminal cannot start the quest"), Director->IsQuestStarted());
            Test.TestTrue(TEXT("Premature world interaction displays terminal guidance"), Director->GetStatus().Contains(TEXT("terminal")));
            Test.TestTrue(TEXT("Premature interaction does not enter a narrative node"), Director->GetCurrentElementId().IsEmpty());
            ++Step;
            return false;
        }

        case 2:
            if (!InteractAt(TEXT("terminal"))) return true;
            if (!Test.TestTrue(TEXT("Tracing and interacting with the actual terminal starts the quest"), Director->IsQuestStarted())) return true;
            ++Step;
            return false;

        case 3:
            if (!InteractAt(TEXT("generator"))) return true;
            Test.TestFalse(TEXT("World generator takes the insufficient-cell branch"), Director->IsPowerRestored());
            Test.TestFalse(TEXT("Insufficient-cell branch leaves the actual gate closed"), Director->IsGateOpen());
            Test.TestTrue(TEXT("Insufficient-cell response is shown to the player"), Director->GetStatus().Contains(TEXT("two power cells")));
            ++Step;
            return false;

        case 4:
            CellA = InteractAt(TEXT("cell_a"));
            if (!CellA.IsValid()) return true;
            Test.TestEqual(TEXT("Actual cell A pickup updates the narrative variable"), Director->GetPowerCellCount(), 1);
            Test.TestTrue(TEXT("Quest notification hides the actual collected cell A actor"), CellA->IsHidden());
            Test.TestFalse(TEXT("Collected cell A no longer blocks collision"), CellA->GetActorEnableCollision());
            Character->QuestInteract();
            Test.TestEqual(TEXT("Pressing interact again at the collected pickup cannot duplicate it"), Director->GetPowerCellCount(), 1);
            ++Step;
            return false;

        case 5:
            CellB = InteractAt(TEXT("cell_b"));
            if (!CellB.IsValid()) return true;
            Test.TestEqual(TEXT("Actual cell B pickup updates the narrative variable"), Director->GetPowerCellCount(), 2);
            Test.TestTrue(TEXT("Quest notification hides the actual collected cell B actor"), CellB->IsHidden());
            Test.TestFalse(TEXT("Collected cell B no longer blocks collision"), CellB->GetActorEnableCollision());
            ++Step;
            return false;

        case 6:
            if (!InteractAt(TEXT("generator"))) return true;
            if (!Test.TestTrue(TEXT("Actual generator interaction now restores power"), Director->IsPowerRestored())) return true;
            Test.TestTrue(TEXT("Authored command opens the world gate"), Director->IsGateOpen());
            for (const auto& Light : StationLights)
            {
                Test.TestTrue(TEXT("World notification increases each native station light's intensity"), Light.Key->Intensity > Light.Value);
            }
            AnimationStart = World->GetTimeSeconds();
            Deadline = FPlatformTime::Seconds() + 15.0;
            ++Step;
            return false;

        case 7:
        {
            // Advance through normal game ticks; do not call actor Tick or move the panel from the test.
            if (World->GetTimeSeconds() - AnimationStart < 3.0) return false;
            Character->QuestView(TEXT("gate"));
            FHitResult Hit;
            Test.TestFalse(TEXT("Opened gate leaves the actual doorway collision trace clear"), TraceGate(Hit));
            Test.TestTrue(TEXT("The collision panel itself moved upward through its normal animation"),
                GatePanel->GetComponentLocation().Z > ClosedPanelHeight + 400.0);
            FString Error;
            if (!Test.TestTrue(TEXT("Restart reloads the narrative in the same running world"), Director->StartNewGame(Error)))
            {
                Test.AddError(Error);
                return true;
            }
            ++Step;
            return false;
        }

        case 8:
        {
            Test.TestFalse(TEXT("World restart clears the quest flag"), Director->IsQuestStarted());
            Test.TestEqual(TEXT("World restart restores the narrative cell count"), Director->GetPowerCellCount(), 0);
            Test.TestFalse(TEXT("World restart turns power off"), Director->IsPowerRestored());
            Test.TestFalse(TEXT("World restart closes the gate"), Director->IsGateOpen());
            Test.TestFalse(TEXT("World restart makes cell A visible again"), CellA->IsHidden());
            Test.TestFalse(TEXT("World restart makes cell B visible again"), CellB->IsHidden());
            Test.TestTrue(TEXT("World restart restores cell A collision"), CellA->GetActorEnableCollision());
            Test.TestTrue(TEXT("World restart restores cell B collision"), CellB->GetActorEnableCollision());
            FHitResult Hit;
            Test.TestTrue(TEXT("Reset gate once again blocks the real doorway"), TraceGate(Hit));
            Test.TestTrue(TEXT("Reset doorway obstruction belongs to the same gate actor"), Hit.GetActor() == Gate.Get());
            Test.TestTrue(TEXT("Reset returns the panel to its closed position"),
                FMath::IsNearlyEqual(GatePanel->GetComponentLocation().Z, ClosedPanelHeight, 0.1));
            for (const auto& Light : StationLights)
            {
                Test.TestEqual(TEXT("World restart restores each fixture's original intensity"), Light.Key->Intensity, Light.Value);
            }
            Character->QuestView(TEXT("overview"));
            return true;
        }
        }
        return true;
    }

private:
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
            Character->QuestInteract();
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
    TWeakObjectPtr<UPrimitiveComponent> GatePanel;
    TArray<TPair<TWeakObjectPtr<UPointLightComponent>, float>> StationLights;
    int32 Step = 0;
    double Deadline;
    double AnimationStart = 0.0;
    double ClosedPanelHeight = 0.0;
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
