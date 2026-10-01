#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "QuestHUD.generated.h"

UCLASS()
class ARCWEAVEQUEST_API AQuestHUD : public AHUD
{
    GENERATED_BODY()

public:
    virtual void DrawHUD() override;

private:
    void Text(const FString& Value, float X, float Y, float Scale, FLinearColor Color);
    float WrappedText(const FString& Value, float X, float Y, float Width, float Scale,
        FLinearColor Color, bool bDraw = true);
};
