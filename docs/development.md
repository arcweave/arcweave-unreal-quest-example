# Build, test, and package

Follow the [README setup](../README.md#run) first. The sample pins the Arcweave plugin to the **v2.2.0** release commit; initialize it with `git submodule update --init --recursive` if it was not cloned with the project. Git LFS supplies the committed Unreal map.

## Build options

`Scripts/build.ps1` defaults to Unreal Engine at `C:\Program Files\Epic Games\UE_5.6` and MSVC `14.38.33130`. Override either path/version when needed:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts/build.ps1 -Task Editor -EngineRoot "D:\Epic Games\UE_5.6" -CompilerVersion "14.38.33130"
```

Close the project's Unreal Editor before rebuilding its DLLs. The targets disable adaptive unity compilation because the bundled plugin relies on unity include order. Build output and automation reports go to `Saved/Validation`.

The scene uses Unreal primitive meshes and C++ actors. Regenerate the committed map after compiling the editor target with:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts/build.ps1 -Task Map
```

## Source map

| Responsibility | Source |
| --- | --- |
| Execute Arcweave flows, refresh presentation, and dispatch action components | [QuestDirector.cpp](../Source/ArcweaveQuest/QuestDirector.cpp) |
| Build the station and apply quest state to actors | [QuestGameMode.cpp](../Source/ArcweaveQuest/QuestGameMode.cpp) |
| Movement, interaction, and checkpoint keys | [QuestCharacter.cpp](../Source/ArcweaveQuest/QuestCharacter.cpp) |
| Objective, inventory, and save/load display | [QuestHUD.cpp](../Source/ArcweaveQuest/QuestHUD.cpp) |
| Game-owned snapshot data | [QuestSaveGame.h](../Source/ArcweaveQuest/QuestSaveGame.h) |
| Bound narrative IDs | [QuestBindings.h](../Source/ArcweaveQuest/QuestBindings.h) and [bindings.json](../Narrative/bindings.json) |

`UQuestDirector` is sample code. It accesses the plugin with `GEngine->GetEngineSubsystem<UArcweaveSubsystem>()`. The plugin imports the project and evaluates Arcscript; the sample interprets referenced `collect_cell` and `open_gate` components as requests for native effects. See the [narrative guide](narrative.md) for the shared Play Mode/game flow, state ownership, entry discovery, and persistence contract.

Input mappings live in `Config/DefaultInput.ini`. It removes Unreal's inherited F5 shader-complexity and F9 screenshot debug bindings so they do not conflict with checkpoints. Restart an open editor after changing input configuration.

## Checks without Unreal

Requires Python 3.9 or later and PowerShell. These checks also run in GitHub Actions:

```powershell
python Scripts/test-sync-narrative.py
powershell -ExecutionPolicy Bypass -File Scripts/test-build.ps1
```

The Python suite validates the bundled exports, graph contracts, import copy, and sync behavior. The PowerShell fixtures check build-script routing and automation-report failures without invoking Unreal.

## Native automation

Compile the editor target before running these commands. Tests use `-NullRHI -RenderOffscreen`, so no game window opens.

```powershell
powershell -ExecutionPolicy Bypass -File Scripts/build.ps1 -Task Test
powershell -ExecutionPolicy Bypass -File Scripts/build.ps1 -Task Test -TestFilter ArcweaveQuest.Persistence
powershell -ExecutionPolicy Bypass -File Scripts/build.ps1 -Task WorldTest
powershell -ExecutionPolicy Bypass -File Scripts/build.ps1 -Task WorldTest -TestFilter ArcweaveQuest.SaveLoadWorld
powershell -ExecutionPolicy Bypass -File Scripts/build.ps1 -Task Test -TestFilter ArcweaveQuest.SaveSession -SavePhase Write
powershell -ExecutionPolicy Bypass -File Scripts/build.ps1 -Task Test -TestFilter ArcweaveQuest.SaveSession -SavePhase Read
powershell -ExecutionPolicy Bypass -File Scripts/build.ps1 -Task Test -TestFilter Arcweave.Project.RuntimeState
```

| Test | Coverage |
| --- | --- |
| `ArcweaveQuest.Flow` (default `Test`) | Quest branches, state ownership, action dispatch, UI queries, and restart |
| `ArcweaveQuest.Persistence` | Disk snapshots, exact restoration, and failed loads without changing the mission |
| `ArcweaveQuest.World` (default `WorldTest`) | Actual interactions, actor visibility, lighting, gate animation/collision, and exit overlap |
| `ArcweaveQuest.SaveLoadWorld` | F5/F9 input dispatch and restoration of physical state, player pose, and HUD |
| `ArcweaveQuest.SaveSession` | A checkpoint written in one process and restored in a second |
| `Arcweave.Project.RuntimeState` | The plugin's snapshot API |

Run the two `SaveSession` phases in order. They use an automation-only slot, which the read phase removes. The runner checks Unreal's JSON report and fails if no tests run, any test fails, or tests remain unrun. See [recorded local results](verification.md).

## Package

```powershell
powershell -ExecutionPolicy Bypass -File Scripts/build.ps1 -Task Package
powershell -ExecutionPolicy Bypass -File Scripts/build.ps1 -Task WorldTest -Packaged
powershell -ExecutionPolicy Bypass -File Scripts/build.ps1 -Task WorldTest -Packaged -TestFilter ArcweaveQuest.SaveLoadWorld
```

Packaging builds a Win64 Development game in `Builds/Windows`, with windowless cooking and hot reload disabled. Use `-OutputDirectory` to choose another archive location, and pass the same value to subsequent packaged tests. Run builds sequentially to avoid UnrealBuildTool conflicts.

The package includes `Content/ArcweaveExport/quest.json` as a loose non-asset file. Syncing the source project does not update an existing package: copy the refreshed export into `Builds/Windows/ArcweaveQuest/Content/ArcweaveExport/`, or package again. Changing narrative content invalidates older checkpoints.
