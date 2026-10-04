#include "QuestCharacter.h"

#include "QuestDirector.h"
#include "QuestWorldActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"

AQuestCharacter::AQuestCharacter()
{
    PrimaryActorTick.bCanEverTick = true;
    GetCapsuleComponent()->InitCapsuleSize(34.0f, 92.0f);
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
    Camera->SetupAttachment(GetCapsuleComponent());
    Camera->SetRelativeLocation(FVector(0, 0, 64));
    Camera->bUsePawnControlRotation = true;
    Camera->FieldOfView = 90.0f;
    bUseControllerRotationYaw = true;
    GetCharacterMovement()->MaxWalkSpeed = 430.0f;
    GetCharacterMovement()->BrakingDecelerationWalking = 1800.0f;
}

void AQuestCharacter::BeginPlay()
{
    Super::BeginPlay();
    if (APlayerController* Player = Cast<APlayerController>(GetController()))
    {
        Player->SetInputMode(FInputModeGameOnly());
        Player->bShowMouseCursor = false;
        Player->PlayerCameraManager->ViewPitchMin = -75.0f;
        Player->PlayerCameraManager->ViewPitchMax = 75.0f;
    }
}

void AQuestCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);
    PlayerInputComponent->BindAxis(TEXT("MoveForward"), this, &AQuestCharacter::MoveForward);
    PlayerInputComponent->BindAxis(TEXT("MoveRight"), this, &AQuestCharacter::MoveRight);
    PlayerInputComponent->BindAxis(TEXT("Turn"), this, &ACharacter::AddControllerYawInput);
    PlayerInputComponent->BindAxis(TEXT("LookUp"), this, &ACharacter::AddControllerPitchInput);
    PlayerInputComponent->BindAction(TEXT("Interact"), IE_Pressed, this, &AQuestCharacter::Interact);
    PlayerInputComponent->BindAction(TEXT("Restart"), IE_Pressed, this, &AQuestCharacter::RestartQuest);
}

void AQuestCharacter::MoveForward(float Value)
{
    AddMovementInput(GetActorForwardVector(), Value);
}

void AQuestCharacter::MoveRight(float Value)
{
    AddMovementInput(GetActorRightVector(), Value);
}

void AQuestCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    RefreshFocus();
}

void AQuestCharacter::RefreshFocus()
{
    FVector ViewLocation;
    FRotator ViewRotation;
    GetActorEyesViewPoint(ViewLocation, ViewRotation);
    FHitResult Hit;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(QuestInteraction), false, this);
    GetWorld()->LineTraceSingleByChannel(Hit, Camera->GetComponentLocation(),
        Camera->GetComponentLocation() + ViewRotation.Vector() * 340.0f, ECC_Visibility, Query);
    FocusedObject = Cast<AQuestWorldActor>(Hit.GetActor());
}

FString AQuestCharacter::GetInteractionPrompt() const
{
    return FocusedObject.IsValid()
        ? FocusedObject->GetInteractionText(*GetGameInstance()->GetSubsystem<UQuestDirector>()) : FString();
}

void AQuestCharacter::Interact()
{
    if (FocusedObject.IsValid())
    {
        FocusedObject->Interact(*GetGameInstance()->GetSubsystem<UQuestDirector>());
    }
}

void AQuestCharacter::RestartQuest()
{
    FString Error;
    GetGameInstance()->GetSubsystem<UQuestDirector>()->StartNewGame(Error);
    SetActorLocation(FVector(0, 0, 110));
    GetCharacterMovement()->StopMovementImmediately();
    if (Controller)
    {
        Controller->SetControlRotation(FRotator::ZeroRotator);
    }
}

void AQuestCharacter::QuestView(const FString& View)
{
    FVector Location(0, 0, 110);
    FVector Target(1300, 0, 165);
    if (View == TEXT("terminal"))
    {
        Location = FVector(100, -230, 110);
        Target = FVector(350, -230, 135);
    }
    else if (View == TEXT("cell_a"))
    {
        Location = FVector(410, -735, 110);
        Target = FVector(620, -735, 110);
    }
    else if (View == TEXT("cell_b"))
    {
        Location = FVector(1420, 735, 110);
        Target = FVector(1630, 735, 110);
    }
    else if (View == TEXT("generator"))
    {
        Location = FVector(970, 0, 110);
        Target = FVector(1125, 0, 127);
    }
    else if (View == TEXT("gate"))
    {
        Location = FVector(1780, 0, 110);
        Target = FVector(2220, 0, 190);
    }
    else if (View == TEXT("exit"))
    {
        Location = FVector(2630, 0, 110);
        Target = FVector(2818, 0, 220);
    }
    else if (View == TEXT("overview"))
    {
        Location = FVector(-200, 410, 110);
        Target = FVector(1500, -100, 150);
    }
    SetActorLocation(Location);
    GetCharacterMovement()->StopMovementImmediately();
    if (Controller)
    {
        Controller->SetControlRotation((Target - (Location + Camera->GetRelativeLocation())).Rotation());
    }
}

void AQuestCharacter::QuestInteract()
{
    // Refresh the trace when console commands run back-to-back in the same frame.
    RefreshFocus();
    Interact();
}
