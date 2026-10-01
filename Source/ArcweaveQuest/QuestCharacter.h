#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "QuestCharacter.generated.h"

class AQuestWorldActor;
class UCameraComponent;

UCLASS()
class ARCWEAVEQUEST_API AQuestCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    AQuestCharacter();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
    FString GetInteractionPrompt() const;

    /** Named viewpoints for checking the sample through the Unreal console. */
    UFUNCTION(Exec)
    void QuestView(const FString& View);
    UFUNCTION(Exec)
    void QuestInteract();

private:
    void MoveForward(float Value);
    void MoveRight(float Value);
    void Interact();
    void RestartQuest();
    void RefreshFocus();

    UPROPERTY()
    TObjectPtr<UCameraComponent> Camera;
    TWeakObjectPtr<AQuestWorldActor> FocusedObject;
};
