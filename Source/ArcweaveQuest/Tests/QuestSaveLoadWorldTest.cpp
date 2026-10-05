#if WITH_DEV_AUTOMATION_TESTS

#include "QuestCharacter.h"
#include "QuestTestNarrative.h"
#include "QuestDirector.h"
#include "QuestGameMode.h"
#include "QuestSaveGame.h"
#include "QuestWorldActor.h"

#include "ArcweaveSubsystem.h"
#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/PointLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "HAL/PlatformTime.h"
#include "InputKeyEventArgs.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "UnrealClient.h"

namespace
{
/** Exercises game-owned saves with real pickups, collision, lights, player and exit overlaps. */
class FQuestSaveLoadWorldCommand : public IAutomationLatentCommand
{
public:
    explicit FQuestSaveLoadWorldCommand(FAutomationTestBase& InTest)
        : Test(InTest), Deadline(FPlatformTime::Seconds() + 20.0)
    {
        const FString Prefix = TEXT("ArcweaveQuest_Automation_World_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
        OneCellSlot = Prefix + TEXT("_OneCell");
        PoweredSlot = Prefix + TEXT("_Powered");
        CompletedSlot = Prefix + TEXT("_Completed");
        MissingSlot = Prefix + TEXT("_Missing");
    }

    virtual ~FQuestSaveLoadWorldCommand() override
    {
        for (const FString& Slot : {OneCellSlot, PoweredSlot, CompletedSlot}) UGameplayStatics::DeleteGameInSlot(Slot, 0);
    }

    virtual bool Update() override
    {
        if (FPlatformTime::Seconds() > Deadline)
        {
            Test.AddError(TEXT("Timed out waiting for the running quest world or checkpoint overlap checks. Run with -game -NullRHI."));
            return true;
        }
        FString Error;
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
                    GameMode = Candidate->GetAuthGameMode<AQuestGameMode>();
                    if (Character.IsValid() && GameMode.IsValid())
                    {
                        World = Candidate;
                        Director = World->GetGameInstance()->GetSubsystem<UQuestDirector>();
                        Character->QuestView(TEXT("overview"));
                        if (!Test.TestTrue(TEXT("Checkpoint world test starts a fresh game"), Director->StartNewGame(Error))) return true;
                        ++Step;
                        break;
                    }
                }
            }
            return false;

        case 1:
        {
            if (!NarrativeIds.Resolve(Narrative(), Test)) return true;
            CheckCheckpointInput();
            FHitResult Hit;
            if (!Test.TestTrue(TEXT("The initial gate blocks the doorway"), TraceGate(Hit))) return true;
            Gate = Cast<AQuestWorldActor>(Hit.GetActor());
            GatePanel = Hit.GetComponent();
            if (!Test.TestNotNull(TEXT("The doorway has a gate panel"), GatePanel.Get())) return true;
            ClosedPanelHeight = GatePanel->GetComponentLocation().Z;
            for (TActorIterator<APointLight> It(World.Get()); It; ++It)
            {
                UPointLightComponent* Light = Cast<UPointLightComponent>(It->GetLightComponent());
                StationLights.Emplace(Light, Light->Intensity);
            }
            Test.TestTrue(TEXT("The station has native lighting to restore"), StationLights.Num() > 0);
            if (!InteractAt(TEXT("terminal"))) return true;
            CellA = InteractAt(TEXT("cell_a"));
            if (!CellA.IsValid()) return true;
            Test.TestEqual(TEXT("The first physical pickup creates the one-cell checkpoint state"), Director->GetPowerCellCount(), 1);
            World->GetFirstPlayerController()->SetControlRotation(FRotator(-17, 38, 0));
            if (!Save(OneCellSlot, TEXT("Save one cell from the real world"))) return true;
            ++Step;
            return false;
        }

        case 2:
            CellB = InteractAt(TEXT("cell_b"));
            if (!CellB.IsValid()) return true;
            Generator = InteractAt(TEXT("generator"));
            if (!Generator.IsValid()) return true;
            Test.TestTrue(TEXT("Physical generator interaction restores power before the second save"), Director->IsPowerRestored());
            Test.TestTrue(TEXT("Power restoration applied the native gate action"), Director->IsGateOpen());
            if (!Save(PoweredSlot, TEXT("Save while the gate animation is starting"))) return true;
            Character->QuestView(TEXT("exit"));
            PhaseStart = World->GetTimeSeconds();
            ++Step;
            return false;

        case 3:
            if (World->GetTimeSeconds() - PhaseStart < 0.1) return false;
            Test.TestTrue(TEXT("Real exit overlap completes the mission before saving"), Director->IsQuestCompleted());
            Test.TestEqual(TEXT("The completed checkpoint has one completion visit"),
                Narrative().Visits.FindChecked(*NarrativeIds.CompletedElement), 1);
            if (!Save(CompletedSlot, TEXT("Save the player inside the completed exit"))) return true;
            if (!LoadAndCheck(OneCellSlot, TEXT("Load one cell after completing the mission"))) return true;
            Test.TestFalse(TEXT("Loading the earlier save clears completion"), Director->IsQuestCompleted());
            Test.TestTrue(TEXT("The collected cell A stays hidden"), CellA->IsHidden());
            Test.TestFalse(TEXT("The restored cell A remains non-colliding"), CellA->GetActorEnableCollision());
            Test.TestFalse(TEXT("Loading before cell B collection makes it visible"), CellB->IsHidden());
            Test.TestTrue(TEXT("The restored cell B can be collected again"), CellB->GetActorEnableCollision());
            CheckGateAndLights(false);
            PhaseStart = World->GetTimeSeconds();
            ++Step;
            return false;

        case 4:
        {
            if (World->GetTimeSeconds() - PhaseStart < 0.1) return false;
            Test.TestTrue(TEXT("Leaving the exit during load did not generate another event"),
                LastLoadedVisits.OrderIndependentCompareEqual(Narrative().Visits));
            const FTransform BeforeMissingLoad = Character->GetActorTransform();
            const FRotator BeforeMissingControl = World->GetFirstPlayerController()->GetControlRotation();
            Test.TestFalse(TEXT("The world wrapper reports a missing save"), GameMode->LoadCheckpoint(MissingSlot, Error));
            Test.TestTrue(TEXT("A failed load preserves the player transform"), Character->GetActorTransform().Equals(BeforeMissingLoad));
            Test.TestTrue(TEXT("A failed load preserves camera rotation"), World->GetFirstPlayerController()->GetControlRotation().Equals(BeforeMissingControl));
            Test.TestTrue(TEXT("A failed world load preserves all visits"), LastLoadedVisits.OrderIndependentCompareEqual(Narrative().Visits));
            if (!LoadAndCheck(PoweredSlot, TEXT("Load a save made during the gate animation"))) return true;
            Test.TestTrue(TEXT("Loading power hides both collected actors"), CellA->IsHidden() && CellB->IsHidden());
            Test.TestFalse(TEXT("Both collected actors have collision disabled"), CellA->GetActorEnableCollision() || CellB->GetActorEnableCollision());
            CheckGateAndLights(true);
            if (!LoadAndCheck(CompletedSlot, TEXT("Restore the completed player inside the exit volume"))) return true;
            Test.TestTrue(TEXT("The completed save restores its quest flag"), Director->IsQuestCompleted());
            Test.TestEqual(TEXT("The completed save restores the authored completed HUD"),
                Director->GetUIText(TEXT("quest_ui.mission_heading")), FString(TEXT("MISSION COMPLETE")));
            PhaseStart = World->GetTimeSeconds();
            ++Step;
            return false;
        }

        case 5:
            if (World->GetTimeSeconds() - PhaseStart < 0.1) return false;
            Test.TestTrue(TEXT("Restoring inside the exit never synthesizes an overlap event, even on later ticks"),
                LastLoadedVisits.OrderIndependentCompareEqual(Narrative().Visits));
            Test.TestEqual(TEXT("Restoring at the exit does not run already-completed feedback"),
                Narrative().Visits.FindChecked(*NarrativeIds.ExitAlreadyCompletedElement), 0);
            Test.TestEqual(TEXT("Restoring at the exit keeps the saved gameplay cursor"),
                Director->GetCurrentElementId(), FString(*NarrativeIds.CompletedElement));
            if (!LoadAndCheck(OneCellSlot, TEXT("Restore the earlier game again"))) return true;
            CellB = InteractAt(TEXT("cell_b"));
            if (!CellB.IsValid()) return true;
            Test.TestEqual(TEXT("Continued physical play after load collects the remaining cell once"), Director->GetPowerCellCount(), 2);
            Test.TestTrue(TEXT("The remaining pickup hides through the ordinary event path"), CellB->IsHidden());
            Character->QuestView(TEXT("overview"));
            Test.TestTrue(TEXT("Checkpoint world test leaves a fresh game"), Director->StartNewGame(Error));
            return true;
        }
        return true;
    }

private:
    void CheckCheckpointInput()
    {
        APlayerController* Player = World->GetFirstPlayerController();
        UPlayerInput* Input = Player->PlayerInput;
        UGameViewportClient* Viewport = World->GetGameViewport();
        if (!Test.TestNotNull(TEXT("The game has a player input handler"), Input)
            || !Test.TestNotNull(TEXT("The game has a viewport"), Viewport)) return;

        const int32 ViewModeBefore = Viewport->ViewModeIndex;
        Test.TestFalse(TEXT("No screenshot is pending before checkpoint input"), FScreenshotRequest::IsScreenshotRequested());
        int32 SavePresses = 0;
        int32 LoadPresses = 0;
        // Exercise the real key mappings and debug-command path with isolated action
        // handlers, so this test never overwrites the player's checkpoint slot.
        UInputComponent* Actions = NewObject<UInputComponent>(Player);
        FInputActionBinding SaveAction(TEXT("SaveCheckpoint"), IE_Pressed);
        SaveAction.ActionDelegate.GetDelegateForManualSet().BindLambda([&SavePresses]() { ++SavePresses; });
        Actions->AddActionBinding(SaveAction);
        FInputActionBinding LoadAction(TEXT("LoadCheckpoint"), IE_Pressed);
        LoadAction.ActionDelegate.GetDelegateForManualSet().BindLambda([&LoadPresses]() { ++LoadPresses; });
        Actions->AddActionBinding(LoadAction);
        const TArray<UInputComponent*> InputStack{Actions};
        for (const FKey& Key : {EKeys::F5, EKeys::F9})
        {
            Test.TestTrue(Key.ToString() + TEXT(" has no competing debug command"), Input->GetBind(Key).IsEmpty());
            Player->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Pressed, 1.0f));
            Input->ProcessInputStack(InputStack, 1.0f / 60.0f, false);
            Test.TestEqual(Key.ToString() + TEXT(" preserves the rendering mode"), Viewport->ViewModeIndex, ViewModeBefore);
            Test.TestFalse(Key.ToString() + TEXT(" does not request a screenshot"), FScreenshotRequest::IsScreenshotRequested());
            Player->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Released, 0.0f));
            Input->ProcessInputStack(InputStack, 1.0f / 60.0f, false);
        }
        Test.TestEqual(TEXT("F5 dispatches the save action exactly once"), SavePresses, 1);
        Test.TestEqual(TEXT("F9 dispatches the load action exactly once"), LoadPresses, 1);
        // Leave the world usable even if a debug binding regresses.
        Viewport->SetViewMode(static_cast<EViewModeIndex>(ViewModeBefore));
        FScreenshotRequest::Reset();
    }

    FArcweaveProjectData Narrative() const
    {
        return GEngine->GetEngineSubsystem<UArcweaveSubsystem>()->GetArcweaveProjectData();
    }

    bool Save(const FString& Slot, const TCHAR* Label)
    {
        const FArcweaveProjectData Before = Narrative();
        FString Error;
        if (!Test.TestTrue(Label, GameMode->SaveCheckpoint(Slot, Error)))
        {
            Test.AddError(Error);
            return false;
        }
        Test.TestTrue(TEXT("Saving from the world does not run Arcscript"), Before.Visits.OrderIndependentCompareEqual(Narrative().Visits));
        return true;
    }

    bool LoadAndCheck(const FString& Slot, const TCHAR* Label)
    {
        const UQuestSaveGame* Saved = Cast<UQuestSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
        if (!Test.TestNotNull(TEXT("The world checkpoint is readable from disk"), Saved)) return false;
        FString Error;
        if (!Test.TestTrue(Label, GameMode->LoadCheckpoint(Slot, Error)))
        {
            Test.AddError(Error);
            return false;
        }
        const FArcweaveProjectData Restored = Narrative();
        LastLoadedVisits = Saved->ArcweaveState.Visits;
        const FString Prefix = FString(Label) + TEXT(": ");
        Test.TestTrue(Prefix + TEXT("all visits are restored without replay"), LastLoadedVisits.OrderIndependentCompareEqual(Restored.Visits));
        for (const auto& Pair : Saved->ArcweaveState.Variables)
        {
            const FArcweaveVariable& Variable = Restored.CurrentVars.FindChecked(Pair.Key);
            Test.TestEqual(Prefix + TEXT("variable ") + Variable.Scope + TEXT(".") + Variable.Name, Variable.Value, Pair.Value.Value);
        }
        Test.TestTrue(Prefix + TEXT("the actual player transform is restored immediately"),
            Character->GetActorTransform().Equals(Saved->PlayerTransform, 0.001));
        Test.TestTrue(Prefix + TEXT("the actual camera rotation is restored immediately"),
            World->GetFirstPlayerController()->GetControlRotation().Equals(Saved->ControlRotation, 0.001));
        Test.TestEqual(Prefix + TEXT("saved objective is restored"), Director->GetObjective(), Saved->Objective);
        Test.TestEqual(Prefix + TEXT("saved interaction feedback is restored"), Director->GetStatus(), Saved->Status);
        Test.TestEqual(Prefix + TEXT("saved gameplay cursor is restored"), Director->GetCurrentElementId(), Saved->CurrentElementId);
        Test.TestEqual(Prefix + TEXT("saved objective cursor is restored"), Director->GetPresentationElementId(), Saved->PresentationElementId);
        Test.TestEqual(Prefix + TEXT("gate label is refreshed from restored UI"),
            Gate->FindComponentByClass<UTextRenderComponent>()->Text.ToString(), Director->GetUIText(TEXT("quest_ui.gate_label")));
        Test.TestEqual(Prefix + TEXT("generator label is refreshed from restored UI"),
            Generator->FindComponentByClass<UTextRenderComponent>()->Text.ToString(), Director->GetUIText(TEXT("quest_ui.generator_label")));
        return true;
    }

    void CheckGateAndLights(bool bPowered)
    {
        FHitResult Hit;
        Test.TestEqual(TEXT("Loading snaps doorway collision to the saved gate state"), TraceGate(Hit), !bPowered);
        if (bPowered)
        {
            Test.TestTrue(TEXT("Loading an open gate snaps its panel above the doorway"),
                GatePanel->GetComponentLocation().Z > ClosedPanelHeight + 400.0);
        }
        else
        {
            Test.TestTrue(TEXT("Loading a closed gate snaps its panel to the closed position"),
                FMath::IsNearlyEqual(GatePanel->GetComponentLocation().Z, ClosedPanelHeight, 0.1));
        }
        for (const auto& Light : StationLights)
        {
            if (bPowered) Test.TestTrue(TEXT("Loaded power restores every station light"), Light.Key->Intensity > Light.Value);
            else Test.TestEqual(TEXT("Loading before power restores every unpowered light"), Light.Key->Intensity, Light.Value);
        }
    }

    bool TraceGate(FHitResult& Hit) const
    {
        const FCollisionQueryParams Query(SCENE_QUERY_STAT(QuestSaveLoadGate), false, Character.Get());
        return World->LineTraceSingleByChannel(Hit, FVector(2050, 0, 150), FVector(2380, 0, 150), ECC_Visibility, Query);
    }

    AQuestWorldActor* InteractAt(const TCHAR* View)
    {
        Character->QuestView(View);
        const UCameraComponent* Camera = Character->FindComponentByClass<UCameraComponent>();
        FHitResult Hit;
        const FCollisionQueryParams Query(SCENE_QUERY_STAT(QuestSaveLoadInteraction), false, Character.Get());
        const FRotator Rotation = World->GetFirstPlayerController()->GetControlRotation();
        World->LineTraceSingleByChannel(Hit, Camera->GetComponentLocation(),
            Camera->GetComponentLocation() + Rotation.Vector() * 340.0f, ECC_Visibility, Query);
        AQuestWorldActor* Target = Cast<AQuestWorldActor>(Hit.GetActor());
        if (!Test.TestNotNull(FString(View) + TEXT(" traces a real world interaction"), Target)) return nullptr;
        Character->QuestInteract();
        return Target;
    }

    FAutomationTestBase& Test;
    FQuestTestNarrative NarrativeIds;
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<AQuestCharacter> Character;
    TWeakObjectPtr<AQuestGameMode> GameMode;
    TWeakObjectPtr<UQuestDirector> Director;
    TWeakObjectPtr<AQuestWorldActor> CellA;
    TWeakObjectPtr<AQuestWorldActor> CellB;
    TWeakObjectPtr<AQuestWorldActor> Gate;
    TWeakObjectPtr<AQuestWorldActor> Generator;
    TWeakObjectPtr<UPrimitiveComponent> GatePanel;
    TArray<TPair<TWeakObjectPtr<UPointLightComponent>, float>> StationLights;
    TMap<FString, int32> LastLoadedVisits;
    FString OneCellSlot;
    FString PoweredSlot;
    FString CompletedSlot;
    FString MissingSlot;
    int32 Step = 0;
    double Deadline;
    double PhaseStart = 0.0;
    double ClosedPanelHeight = 0.0;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FArcweaveQuestSaveLoadWorldTest,
    "ArcweaveQuest.SaveLoadWorld",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArcweaveQuestSaveLoadWorldTest::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FQuestSaveLoadWorldCommand(*this));
    return true;
}

#endif
