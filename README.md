# Arcweave Unreal Quest Example

A small C++ Unreal project in which Arcweave drives a world objective: collect two power cells, restore a station generator, and open the exit gate.

[Open the matching Arcweave project](https://arcweave.com/app/project/MWEZgMb62g) · [Narrative and C++ guide](docs/narrative.md)

This example uses the released Arcweave **v2.1.0** plugin, pinned to commit `89eb76f46bff2ef3ad7382c67b35ac3bd86c45c7` as a Git submodule. It does not include save/load. The build targets disable adaptive unity compilation because this plugin release relies on unity include order; its source is unchanged.

## Run locally

Requires Windows, Unreal Engine **5.6.1**, Visual Studio 2022 with **Game development with C++**, MSVC **14.38**, Windows SDK, Git, and Git LFS. The checked-in project uses engine association `5.6`.

```powershell
git clone --recurse-submodules https://github.com/arcweave/arcweave-unreal-quest-example.git
cd arcweave-unreal-quest-example
git lfs pull
powershell -ExecutionPolicy Bypass -File Scripts/build.ps1 -Task Editor
```

Open `ArcweaveQuest.uproject` and press Play, or run:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts/build.ps1 -Task Play
```

Use `-EngineRoot` and `-CompilerVersion` to override the script defaults. If cloning without `--recurse-submodules`, run `git submodule update --init --recursive` first. Run **one game/PIE instance at a time**: the plugin's interpreter is an engine subsystem shared by game instances in that engine process.

## Play the mission

- Move with **WASD**, look with the mouse, and press **E** while looking at a nearby interactable.
- Activate the terminal to start the objective.
- Try the generator before accepting the task to see Arcweave's terminal guidance, or before collecting both cells to see its missing-cell response.
- Find both power cells, then activate the generator again. Arcweave selects the success branch; its referenced components turn on station power and open the gate.
- Press **R** to restart the mission.

The game runs offline from `Content/ArcweaveExport/quest.json`. An API key is only needed to refresh that export; no credential belongs in Unreal settings or packaged builds.

## How it works

| Responsibility | Implementation |
| --- | --- |
| Quest text, variables, branch conditions, component references | Matching Arcweave project and bundled export; see [narrative guide](docs/narrative.md) |
| Execute elements, update variables, select branch, dispatch components | [QuestDirector.cpp](Source/ArcweaveQuest/QuestDirector.cpp) |
| Build the station and reflect quest state in the world | [QuestGameMode.cpp](Source/ArcweaveQuest/QuestGameMode.cpp) |
| Movement and interaction | [QuestCharacter.cpp](Source/ArcweaveQuest/QuestCharacter.cpp) |
| Objective and interaction display | [QuestHUD.cpp](Source/ArcweaveQuest/QuestHUD.cpp) |
| Stable narrative UUID bindings | [QuestBindings.h](Source/ArcweaveQuest/QuestBindings.h) and [bindings.json](Narrative/bindings.json) |

`UQuestDirector` is sample game code. It obtains the plugin with `GEngine->GetEngineSubsystem<UArcweaveSubsystem>()`. The game chooses when to execute an element with `TranspileObject`, updates the cell count with `SetVariable`, and re-evaluates the authored generator branch on each attempt. That branch checks `questStarted` first, then `powerCells`; Unreal displays the selected element's rendered content. Editing the guidance in Arcweave and refreshing the export changes the response without a C++ edit.

Referenced components are a convention implemented by this sample: when an element executes, the director reads its components and dispatches `restore_power` and `open_gate` to C++ handlers. The plugin does not provide an arbitrary Arcscript command/event registration API. Repeated interaction after completion does not re-execute the success node or its commands.

The scene uses Unreal primitive meshes and C++ actors. There are no quest Blueprints or external model dependencies. The committed map can be regenerated with `Scripts/build.ps1 -Task Map` after compiling the editor target.

## Verification and packaging

```powershell
powershell -ExecutionPolicy Bypass -File Scripts/build.ps1 -Task Test
powershell -ExecutionPolicy Bypass -File Scripts/build.ps1 -Task WorldTest
powershell -ExecutionPolicy Bypass -File Scripts/build.ps1 -Task Package
powershell -ExecutionPolicy Bypass -File Scripts/build.ps1 -Task WorldTest -Packaged
```

Tests run without a window using `-NullRHI -RenderOffscreen`. `Test` checks the narrative flow; `WorldTest` exercises the running world, pickups, lighting, and gate collision. `-Packaged` runs it in the packaged Development executable. The script checks Unreal's JSON automation report, including that tests actually ran. Reports and build logs are under `Saved/Validation`; packaged output defaults to `Builds/Windows`. See [local verification results](docs/verification.md).

The package includes the bundled JSON as a loose non-asset file so the plugin's local import can find it. The sample targets a single-player Windows desktop game; it is not a multiplayer or VR/mobile integration example.
