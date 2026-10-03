# Arcweave Unreal Quest Example

A small C++ Unreal project in which Arcweave drives a world objective: accept a task, collect the required power cells, restore a station generator, and reach the exit. Arcweave owns the quest decisions, feedback, objective text, prompts, and station labels.

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
- Walk through the gate into the exit corridor to complete the mission. Restoring power and completing the escape are separate authored states.
- Press **R** to restart the mission.

The authored `requiredPowerCells` value defaults to `2`. Setting it to `1` updates the requirement, feedback, prompts, and HUD together. The sample level has two physical pickups; a larger requirement also needs additional pickups in the level.

The game runs offline from `Content/ArcweaveExport/quest.json`. An API key is only needed to refresh that export; no credential belongs in Unreal settings or packaged builds.

## How it works

| Responsibility | Implementation |
| --- | --- |
| Quest decisions, feedback, objectives, prompts, and station text | Matching Arcweave project and bundled export; see [narrative guide](docs/narrative.md) |
| Execute elements, update variables, select branch, dispatch components | [QuestDirector.cpp](Source/ArcweaveQuest/QuestDirector.cpp) |
| Build the station and reflect quest state in the world | [QuestGameMode.cpp](Source/ArcweaveQuest/QuestGameMode.cpp) |
| Movement and interaction | [QuestCharacter.cpp](Source/ArcweaveQuest/QuestCharacter.cpp) |
| Objective and interaction display | [QuestHUD.cpp](Source/ArcweaveQuest/QuestHUD.cpp) |
| Stable narrative UUID bindings | [QuestBindings.h](Source/ArcweaveQuest/QuestBindings.h) and [bindings.json](Narrative/bindings.json) |

`UQuestDirector` is sample game code. It obtains the plugin with `GEngine->GetEngineSubsystem<UArcweaveSubsystem>()`. Every interaction enters the same **Handle world event** element. Unreal sets `game_event.type` to `use_terminal`, `collect_cell`, `check_generator`, or `enter_exit`; the router connects directly to the matching flow's condition branch. Each condition has one outgoing connection. The director follows consecutive branches and automatic connections until the flow ends, refreshes the objective/UI, and waits for another interaction. The router has no fallback: an unsupported event reaches the director's existing integration error without executing a quest action. Startup imports the project and computes the initial objective/UI without executing the world-event entry.

Referenced components are a convention implemented by this sample: `collect_cell`, `restore_power`, and `open_gate` dispatch to C++ handlers. Before a pickup attempt, Unreal supplies `game_event.cell_already_collected` for the selected physical item. The pickup branch checks that flag, then task acceptance, before requesting collection. Its handler records the unique item and updates `powerCells` through `SetVariable`; the next authored element renders the updated count. Authored branches handle rejected and repeated interactions. The plugin does not provide an arbitrary Arcscript command/event registration API.

The **UI** component folder separates shared HUD text (`hud`, five strings), static world labels (`world_text`, seven strings), and state-dependent quest display (`quest_ui`, seven strings). Folder names organize the editor; component custom IDs define Arcscript scopes. The director maps qualified keys such as `hud.station_name` to attribute UUIDs, then caches current values after each event. HUD drawing and looking at objects only call `GetUIText`; they never execute Arcscript or increment visits.

The Objectives and interface graph resets the seven `quest_ui` fields to their authored defaults before selecting a state. Individual elements override only the fields that differ. Powered and completed states share a setup element for the online grid, generator, and gate labels; completion changes the heading. The final leaf's content supplies the objective. This graph may update `quest_ui` but does not mutate quest globals, shared HUD/world text, or dispatch world commands. Its elements have no repeated display attributes. Use one statement per Arcscript code block, as required by the released interpreter.

The project has five global variables: `questStarted` means the terminal task was accepted, `powerCells` is Unreal's collected-item count, `requiredPowerCells` is the authored requirement, `powerRestored` is set by the generator success element, and `questCompleted` is set by the exit completion element. Together with the nineteen UI strings and two inputs on the **Game event** component, the plugin imports **26 runtime variables**. Event inputs describe the most recent request and are replaced on every interaction; restart restores an empty event type and a false duplicate flag. Editing authored wording and refreshing the export changes the response without a C++ edit. Runtime changes to shared HUD/world strings appear on the next event refresh and persist across presentation resets; restart restores authored values.

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
