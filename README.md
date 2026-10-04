# Arcweave Unreal Quest Example

A small C++ Unreal project in which Arcweave drives a world objective: accept a task, collect the required power cells, restore a station generator, and reach the exit. One Arcweave board runs the mission both in browser Play Mode and in Unreal. Arcweave owns quest progression, feedback, objective text, prompts, and station labels.

[Open the matching Arcweave project](https://arcweave.com/app/project/MWEZgMb62g) · [Narrative and C++ guide](docs/narrative.md)

This example pins the Arcweave plugin to main commit `7513d9e113f4b8bca56e662fdb736fb41cb933ba` as a Git submodule, including [starting-element support](https://github.com/arcweave/arcweave-unreal-plugin/pull/39). It does not include save/load. The build targets disable adaptive unity compilation because the bundled plugin relies on unity include order; its source is unchanged.

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

In [Arcweave Play Mode](https://arcweave.com/app/project/MWEZgMb62g/play), choose an interaction from **Station · choose an interaction**: use the terminal, collect cell A or B, check the generator, or enter the exit. Follow each response and objective back to the menu. You can complete the mission, try interactions out of order, and attempt duplicate pickups without editing variables in the Debugger. Play Mode uses the same quest conditions and state changes as Unreal; physical collection, lighting, and the gate are represented by their authored feedback. Use Play Mode's restart control to reset the mission.

In Unreal:

- Move with **WASD**, look with the mouse, and press **E** while looking at a nearby interactable.
- Activate the terminal to start the objective.
- Try the generator before accepting the task to see Arcweave's terminal guidance, or before collecting both cells to see its missing-cell response.
- Find both power cells, then activate the generator again. Arcweave sets the quest's power state, which lights the station, and references the Open gate component to request the gate action.
- Walk through the gate into the exit corridor to complete the mission. Restoring power and completing the escape are separate authored states.
- Press **R** to restart the mission.

The authored `quest.required_power_cells` value defaults to `2`. Setting it to `1` updates the requirement, feedback, prompts, and HUD together. The sample has two cells; a larger requirement needs more authored cell state and menu choices, as well as physical pickups in the level.

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

`UQuestDirector` is sample game code. It obtains the plugin with `GEngine->GetEngineSubsystem<UArcweaveSubsystem>()`. The imported `StartingElementId` identifies the station interaction menu; its UUID is not hardcoded in C++. In Play Mode, its five connection labels set `game_event.type` and `game_event.cell_id` before entering a common event router. Unreal supplies those inputs from the physical interaction and follows the same router destination without executing the menu labels. The router selects the terminal, pickup, generator, or exit flow. Each condition has one outgoing connection; the starting menu is the only element with multiple outputs.

The **Restore power · playable quest** board connects every response to the objectives/UI path. Each of its five objective outcomes returns to the station menu through a nearby jumper, keeping long return lines out of the layout. The native director stops before the UI entry, refreshes presentation once, then resolves the return jumper and stops before executing its menu target. It then waits for another physical interaction. Startup computes the initial objective/UI directly without executing a world event. The director finds the UI entry by its plain-string element attribute named `entry_point`, with value `objectives_ui`, on the same board as the starting element and caches its ID. Neither entry needs a UUID binding. An unsupported event reaches the existing integration error without executing a quest action.

Referenced action components are a convention implemented by this sample: `collect_cell` and `open_gate` dispatch to C++ handlers. A pickup request identifies `cell_a` or `cell_b` through `game_event.cell_id`. The pickup branch checks that cell's shared `collected` flag, then task acceptance. On the accepted path, Arcscript marks the cell as collected and increments `player.power_cells` before the `collect_cell` handler records the physical pickup. C++ does not write the narrative count. This gives Play Mode the same collection rules and progression as Unreal. The plugin does not provide an arbitrary Arcscript command/event registration API.

The **State** folder contains Player (`player`), Restore power quest (`quest`), Cell A (`cell_a`), and Cell B (`cell_b`). **Inputs** contains Game event, **Actions** contains Collect cell and Open gate, and **UI** contains the three text components. Component variables hold values that Arcscript and Unreal can read; references to action components request effects in the engine. The director keeps a read cache of the five player/quest values, seeded at import and updated immediately by `OnArcweaveVariableChanged` when Arcscript or `SetVariable` changes them. HUD and world state getters read this cache without copying the entire imported project. Arcweave remains authoritative; there is no separate Restore power action or independently managed native power flag.

The **UI** component folder separates shared HUD text (`hud`, five strings), static world labels (`world_text`, seven strings), and state-dependent quest display (`quest_ui`, seven strings). Folder names organize the editor; component custom IDs define Arcscript scopes. The director maps qualified keys such as `hud.station_name` to attribute UUIDs, then caches current values after each event. HUD drawing and looking at objects only call `GetUIText`; they never execute Arcscript or increment visits.

The objectives/UI path resets the seven `quest_ui` fields to their authored defaults before selecting a state. Individual elements override only the fields that differ. Powered and completed states share a setup element for the online grid, generator, and gate labels; completion changes the heading. The final leaf's content supplies the objective before returning to the menu in Play Mode. This path may update `quest_ui` but does not mutate player, quest, or cell state, shared HUD/world text, or dispatch world commands. Its entry has one identifying metadata attribute; display text remains in the UI components. Use one statement per Arcscript code block, as required by the bundled interpreter.

The seven state fields are `player.power_cells` (the authored collected-item count), `quest.started` (terminal task accepted), `quest.required_power_cells` (authored requirement), `quest.power_restored` (set by generator success), `quest.completed` (set by exit completion), and each cell's `collected` flag. Together with the nineteen UI strings and two inputs on **Game event**, the plugin imports **28 component variables and no globals**. Event inputs describe the most recent request and are replaced on every interaction; restart restores empty event inputs and uncollected cells. There is no simulator board, simulation flag, or separate preview state. Editing authored wording and refreshing the export changes the response without a C++ edit. Runtime changes to shared HUD/world strings appear on the next event refresh and persist across presentation resets; restart restores authored values.

The scene uses Unreal primitive meshes and C++ actors. There are no quest Blueprints or external model dependencies. The committed map can be regenerated with `Scripts/build.ps1 -Task Map` after compiling the editor target.

## Verification and packaging

```powershell
powershell -ExecutionPolicy Bypass -File Scripts/test-build.ps1
python Scripts/test-sync-narrative.py
powershell -ExecutionPolicy Bypass -File Scripts/build.ps1 -Task Test
powershell -ExecutionPolicy Bypass -File Scripts/build.ps1 -Task WorldTest
powershell -ExecutionPolicy Bypass -File Scripts/build.ps1 -Task Package
powershell -ExecutionPolicy Bypass -File Scripts/build.ps1 -Task WorldTest -Packaged
```

The first two commands check build-script routing and narrative validation without starting Unreal; the Python check requires Python 3.9 or later. Unreal tests and cooking run without a window using `-NullRHI -RenderOffscreen`. Packaging also disables hot reload. `Test` checks the narrative flow; `WorldTest` exercises the running world, pickups, lighting, and gate collision. `-Packaged` runs it in the packaged Development executable. The script checks Unreal's JSON automation report, including that tests actually ran. Reports and build logs are under `Saved/Validation`; packaged output defaults to `Builds/Windows`. Use the same `-OutputDirectory` argument for `Package` and `WorldTest -Packaged` when choosing a different archive directory. See [local verification results](docs/verification.md).

The package includes the bundled JSON as a loose non-asset file so the plugin's local import can find it. The sample targets a single-player Windows desktop game; it is not a multiplayer or VR/mobile integration example.
