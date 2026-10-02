#pragma once

// Stable runtime and test UUIDs from Narrative/bindings.json.
// Internal conditions, connections, notes, and attributes stay in the authored graph.
namespace QuestBindings
{
    inline constexpr const TCHAR* Board = TEXT("b031a811-813e-45a6-8d7f-30c0e4e46eac");
    inline constexpr const TCHAR* StartElement = TEXT("e60800d3-a5aa-49c4-9b9c-fb5f8c1c09e7");
    inline constexpr const TCHAR* GeneratorElement = TEXT("53a8e27d-542d-4222-85fc-36073d0e37df");
    inline constexpr const TCHAR* SuccessElement = TEXT("bfc100c0-7b99-4404-a71c-c1b42b102c02");
    inline constexpr const TCHAR* MissingCellsElement = TEXT("10be7825-974e-4306-b8e4-d2bbc63486dd");
    inline constexpr const TCHAR* PowerCellsVariable = TEXT("6546037a-fcdf-4b42-9fe3-d697bc156d79");
    inline constexpr const TCHAR* QuestStartedVariable = TEXT("882765ae-0eef-4aae-b430-84006b4a0a9e");
    inline constexpr const TCHAR* PowerRestoredVariable = TEXT("ae62d51d-cd04-4022-a643-580921115260");
    inline constexpr const TCHAR* RestorePowerComponent = TEXT("defba3c3-cdce-4b80-91cc-b75118f1f512");
    inline constexpr const TCHAR* OpenGateComponent = TEXT("80c7c42c-bb19-4065-8afa-7cd5b54776fc");
    inline constexpr const TCHAR* TerminalRequiredElement = TEXT("05d919db-a991-45b7-b20f-db02dbd6f307");
    inline constexpr const TCHAR* RequiredPowerCellsVariable = TEXT("97c232d5-287e-43f2-a25c-b32cf7e0a076");
    inline constexpr const TCHAR* QuestCompletedVariable = TEXT("902e425d-c870-4206-aca6-f0e288d925f5");
    inline constexpr const TCHAR* PresentationBoard = TEXT("a9ad17ae-15e4-49b7-aff7-7df07bc1f5a2");
    inline constexpr const TCHAR* CollectCellComponent = TEXT("06f4de3e-a022-472e-a2f9-bd73ee5875ee");
    inline constexpr const TCHAR* InitializationElement = TEXT("76724a0b-9935-4c27-9941-90c63f3a795c");
    inline constexpr const TCHAR* TerminalEntryElement = TEXT("ebdc3d2e-5680-4b07-831c-fa919ef56da2");
    inline constexpr const TCHAR* PickupEntryElement = TEXT("df460871-e5db-4974-87f8-1b071d5039e6");
    inline constexpr const TCHAR* DuplicatePickupElement = TEXT("35f2414d-8156-474e-98d3-eaf74211d45b");
    inline constexpr const TCHAR* ExitEntryElement = TEXT("802dcbdf-3cf6-4f75-8cbc-f8f03053f85e");
    inline constexpr const TCHAR* PresentationEntryElement = TEXT("7be79d97-39d8-4179-ad71-4b60b4ee62c8");
    inline constexpr const TCHAR* HUDTextComponent = TEXT("06e4e920-e844-4509-9891-18e05d39f48b");
    inline constexpr const TCHAR* WorldTextComponent = TEXT("db3ac52b-3cc2-4a1b-86c8-2d2cd5903573");
    inline constexpr const TCHAR* QuestUIComponent = TEXT("e170ad75-b550-436b-9877-32a16bcaffbe");
    inline constexpr const TCHAR* PickupCollectedElement = TEXT("6af8cc14-6034-4e97-ab3f-6ebbdc207161");
    inline constexpr const TCHAR* CompletedElement = TEXT("f2d75b83-f348-4b58-8190-b68d119db86d");
    inline constexpr const TCHAR* TerminalAcceptedElement = TEXT("2689f5bc-f257-4b93-8d03-16760617ef62");
    inline constexpr const TCHAR* TerminalPoweredElement = TEXT("41df4d86-d079-4785-a22a-013597633640");
    inline constexpr const TCHAR* TerminalCompletedElement = TEXT("c80115f9-8323-42c8-b514-6d7895ad6852");
    inline constexpr const TCHAR* AlreadyOnlineElement = TEXT("f0fd0f64-ef53-446f-a43b-63746394bba0");
    inline constexpr const TCHAR* PickupTerminalRequiredElement = TEXT("469d524b-9a3e-41b4-9f94-67a17246b608");
    inline constexpr const TCHAR* PickupActionElement = TEXT("988b8226-23eb-4990-b481-19cd7cd3145c");
    inline constexpr const TCHAR* ExitDeniedElement = TEXT("12c92fd1-be9a-40c9-9bc3-1d6f5573ee9a");
    inline constexpr const TCHAR* ExitAlreadyCompletedElement = TEXT("961efa28-f033-44aa-ae8f-25bcb9a8e3e1");
    inline constexpr const TCHAR* PresentationCompletedElement = TEXT("13031eea-0fa8-4721-ac16-8a583906a6ae");
    inline constexpr const TCHAR* PresentationPoweredElement = TEXT("09438fc8-be1d-4f8a-adad-30170999ca60");
    inline constexpr const TCHAR* PresentationUnacceptedElement = TEXT("172fb20d-be4b-40f7-acec-fb22e76afe50");
    inline constexpr const TCHAR* PresentationCollectingElement = TEXT("825244f4-6345-4314-b731-1f378ae010a1");
    inline constexpr const TCHAR* PresentationReadyElement = TEXT("b87fd479-2ae1-4201-aaae-a738b8b53f99");
    inline constexpr const TCHAR* PresentationBranch = TEXT("3cd93c4b-0dbb-47b0-9335-f36e3a27af08");
    inline constexpr const TCHAR* PresentationPoweredSetupElement = TEXT("a21dc333-37fa-4de2-9213-0acfbfff4811");
    inline constexpr const TCHAR* PresentationCompletionBranch = TEXT("6562a23b-ba75-455b-9cba-119f77f7dd80");
}
