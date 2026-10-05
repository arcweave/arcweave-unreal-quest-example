#include "QuestHUD.h"

#include "QuestCharacter.h"
#include "QuestDirector.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"

namespace
{
constexpr float TextScale = 1.5f;
const FLinearColor White(0.89f, 0.94f, 0.97f);
const FLinearColor Muted(0.55f, 0.66f, 0.74f);
const FLinearColor Cyan(0.10f, 0.83f, 0.93f);
const FLinearColor Panel(0.016f, 0.028f, 0.042f, 0.90f);
const FLinearColor Amber(1.0f, 0.57f, 0.23f);
}

void AQuestHUD::Text(const FString& Value, float X, float Y, float Scale, FLinearColor Color)
{
    DrawText(Value, Color, X, Y, GEngine->GetMediumFont(), Scale * TextScale);
}

float AQuestHUD::WrappedText(const FString& Value, float X, float Y, float Width, float Scale,
    FLinearColor Color, bool bDraw)
{
    const float StartY = Y;
    float TextWidth = 0, TextHeight = 0;
    TArray<FString> Words;
    Value.ParseIntoArrayWS(Words);
    FString Line;
    for (const FString& Word : Words)
    {
        const FString Candidate = Line.IsEmpty() ? Word : Line + TEXT(" ") + Word;
        GetTextSize(Candidate, TextWidth, TextHeight, GEngine->GetMediumFont(), Scale * TextScale);
        if (TextWidth > Width && !Line.IsEmpty())
        {
            if (bDraw)
            {
                Text(Line, X, Y, Scale, Color);
            }
            Y += TextHeight + 7.0f * Scale;
            Line = Word;
        }
        else
        {
            Line = Candidate;
        }
    }
    if (bDraw)
    {
        Text(Line, X, Y, Scale, Color);
    }
    return Y - StartY + TextHeight;
}

void AQuestHUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas)
    {
        return;
    }
    const UQuestDirector* Director = GetGameInstance()->GetSubsystem<UQuestDirector>();
    const float S = FMath::Clamp(Canvas->ClipX / 1600.0f, 0.9f, 1.5f);
    const float Margin = 34.0f * S;
    const float Left = Margin;
    const float Right = Canvas->ClipX - Margin;
    const float CenterX = Canvas->ClipX * 0.5f;
    const float CenterY = Canvas->ClipY * 0.5f;
    const bool bOnline = Director->IsPowerRestored();
    const bool bCompleted = Director->IsQuestCompleted();
    const int32 RequiredCells = Director->GetRequiredPowerCellCount();
    const int32 CollectedCells = Director->GetPowerCellCount();

    DrawRect(Panel, Left, Margin, 442 * S, 83 * S);
    DrawRect(Cyan, Left, Margin, 3 * S, 83 * S);
    Text(Director->GetUIText(TEXT("hud.brand")), Left + 19 * S, Margin + 13 * S, 0.78f * S, Muted);
    Text(Director->GetUIText(TEXT("hud.station_name")), Left + 18 * S, Margin + 35 * S, 1.52f * S, White);

    const float ObjectiveY = Margin + 95 * S;
    DrawRect(Panel, Left, ObjectiveY, 442 * S, 143 * S);
    Text(Director->GetUIText(TEXT("quest_ui.mission_heading")), Left + 19 * S, ObjectiveY + 15 * S,
        0.79f * S, bCompleted ? Cyan : Amber);
    WrappedText(Director->GetObjective(), Left + 19 * S, ObjectiveY + 43 * S, 402 * S, 1.04f * S, White);
    Text(Director->GetUIText(TEXT("hud.mission_tagline")), Left + 19 * S, ObjectiveY + 117 * S, 0.68f * S, Muted);

    DrawRect(Panel, Right - 242 * S, Margin, 242 * S, 112 * S);
    DrawRect(bOnline ? Cyan : Amber, Right - 242 * S, Margin, 242 * S, 2 * S);
    Text(Director->GetUIText(TEXT("quest_ui.grid_status")), Right - 223 * S, Margin + 15 * S,
        0.81f * S, bOnline ? Cyan : Amber);
    Text(Director->GetUIText(TEXT("hud.cells_label")), Right - 223 * S, Margin + 46 * S, 0.76f * S, Muted);
    Text(FString::Printf(TEXT("%d / %d"), CollectedCells, RequiredCells), Right - 87 * S, Margin + 42 * S, 1.04f * S, White);
    const float Progress = RequiredCells > 0
        ? FMath::Clamp(static_cast<float>(CollectedCells) / RequiredCells, 0.0f, 1.0f)
        : 0.0f;
    DrawRect(FLinearColor(0.13f, 0.20f, 0.25f), Right - 223 * S, Margin + 82 * S, 200 * S, 8 * S);
    DrawRect(Cyan, Right - 223 * S, Margin + 82 * S, 200 * S * Progress, 8 * S);

    const FString PersistenceStatus = Director->GetPersistenceStatus();
    const float SaveY = Margin + 124 * S;
    const float SaveHeight = PersistenceStatus.IsEmpty() ? 36 * S
        : 54 * S + WrappedText(PersistenceStatus, 0, 0, 204 * S, 0.73f * S, White, false);
    DrawRect(Panel, Right - 242 * S, SaveY, 242 * S, SaveHeight);
    Text(Director->GetUIText(TEXT("save_ui.controls")), Right - 223 * S, SaveY + 10 * S,
        0.73f * S, Muted);
    if (!PersistenceStatus.IsEmpty())
    {
        WrappedText(PersistenceStatus, Right - 223 * S, SaveY + 36 * S, 204 * S,
            0.73f * S, White);
    }

    const FString Status = Director->GetStatus();
    const float StatusWidth = FMath::Min(860.0f * S, Canvas->ClipX - Margin * 2);
    const float StatusHeight = WrappedText(Status, 0, 0, StatusWidth - 36 * S, 0.84f * S, White, false) + 26 * S;
    const float StatusTop = Canvas->ClipY - 64 * S - StatusHeight;
    const AQuestCharacter* Character = Cast<AQuestCharacter>(GetOwningPawn());
    const FString Prompt = Character ? Character->GetInteractionPrompt() : FString();
    const FLinearColor Reticle = Prompt.IsEmpty() ? FLinearColor(0.65f, 0.77f, 0.82f, 0.8f) : Cyan;
    DrawLine(CenterX - 7 * S, CenterY, CenterX - 3 * S, CenterY, Reticle, S);
    DrawLine(CenterX + 3 * S, CenterY, CenterX + 7 * S, CenterY, Reticle, S);
    DrawLine(CenterX, CenterY - 7 * S, CenterX, CenterY - 3 * S, Reticle, S);
    DrawLine(CenterX, CenterY + 3 * S, CenterX, CenterY + 7 * S, Reticle, S);
    if (!Prompt.IsEmpty())
    {
        float Width = 0, Height = 0;
        GetTextSize(Prompt, Width, Height, GEngine->GetMediumFont(), S * TextScale);
        const float PromptX = CenterX - (Width + 63 * S) * 0.5f;
        const float PromptY = StatusTop - 62 * S;
        DrawRect(Panel, PromptX - 17 * S, PromptY - 10 * S, Width + 96 * S, 49 * S);
        DrawRect(Cyan, PromptX, PromptY, 28 * S, 27 * S);
        float KeyWidth = 0, KeyHeight = 0;
        GetTextSize(TEXT("E"), KeyWidth, KeyHeight, GEngine->GetMediumFont(), S * TextScale);
        Text(TEXT("E"), PromptX + (28 * S - KeyWidth) * 0.5f, PromptY + (27 * S - KeyHeight) * 0.5f,
            S, FLinearColor(0.02f, 0.10f, 0.13f));
        Text(Prompt, PromptX + 44 * S, PromptY + (27 * S - Height) * 0.5f, S, White);
    }

    if (!Status.IsEmpty())
    {
        const float StatusLeft = CenterX - StatusWidth * 0.5f;
        DrawRect(Panel, StatusLeft, StatusTop, StatusWidth, StatusHeight);
        DrawRect(bOnline ? Cyan : Muted, StatusLeft, StatusTop, 2 * S, StatusHeight);
        WrappedText(Status, StatusLeft + 18 * S, StatusTop + 13 * S, StatusWidth - 36 * S,
            0.84f * S, bOnline ? Cyan : White);
    }
    DrawRect(Panel, Left, Canvas->ClipY - 48 * S, 588 * S, 28 * S);
    Text(Director->GetUIText(TEXT("hud.controls")),
        Left + 12 * S, Canvas->ClipY - 42 * S, 0.73f * S, Muted);
    Text(Director->GetUIText(TEXT("hud.station_footer")), Right - 297 * S, Canvas->ClipY - 42 * S, 0.68f * S, Muted);
}
