# Arcweave Unreal Quest Example

A small C++ Unreal project in which Arcweave drives a world objective: accept a task, collect the required power cells, restore a station generator, and reach the exit. Arcweave owns the quest decisions, feedback, objective text, prompts, and station labels.

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

- Move with **WASD**, look with the mouse, and press **E** while looking at a nearby interactable.
- Activate the terminal to start the objective.
- Try the generator before accepting the task to see Arcweave's terminal guidance, or before collecting both cells to see its missing-cell response.
- Find both power cells, then activate the generator again. Arcweave sets the quest's power state, which lights the station, and references the Open gate component to request the gate action.
- Walk through the gate into the exit corridor to complete the mission. Restoring power and completing the escape are separate authored states.
- Press **R** to restart the mission.

The authored `quest.required_power_cells` value defaults to `2`. Setting it to `1` updates the requirement, feedback, prompts, and HUD together. The sample level has two physical pickups; a larger requirement also needs additional pickups in the level.

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

`UQuestDirector` is sample game code. It obtains the plugin with `GEngine->GetEngineSubsystem<UArcweaveSubsystem>()`. Every interaction reads `StartingElementId` from the imported project and enters **Handle world event**, which is selected as the starting element in Arcweave. Its UUID is not hardcoded in C++. Unreal sets `game_event.type` to `use_terminal`, `collect_cell`, `check_generator`, or `enter_exit`; the router connects directly to the matching flow's condition branch. Each condition has one outgoing connection. The director follows consecutive branches and automatic connections until the flow ends, refreshes the objective/UI, and waits for another interaction. The router has no fallback: an unsupported event reaches the director's existing integration error without executing a quest action. Startup imports the project and computes the initial objective/UI without executing the world-event entry. At import, the director finds the objectives/UI entry on the board with custom ID `quest_presentation` by its plain-string element attribute named `entry_point`, with value `objectives_ui`, and caches its ID. Neither graph entry needs a UUID binding.

Referenced action components are a convention implemented by this sample: `collect_cell` and `open_gate` dispatch to C++ handlers. Before a pickup attempt, Unreal supplies `game_event.cell_already_collected` for the selected physical item. The pickup branch checks that flag, then task acceptance, before requesting collection. Its handler records the unique item and updates `player.power_cells` through `SetVariable`; the next authored element renders the updated count. Authored branches handle rejected and repeated interactions. The plugin does not provide an arbitrary Arcscript command/event registration API.

The **State** folder contains Player (`player`) and Restore power quest (`quest`). **Inputs** contains Game event, **Actions** contains Collect cell and Open gate, and **UI** contains the three text components. Component variables hold values that Arcscript and Unreal can read; references to action components request effects in the engine. The director reads `quest.power_restored` directly, and the normal world refresh uses it to update station lighting. There is no separate Restore power action or duplicate native power flag.

The **UI** component folder separates shared HUD text (`hud`, five strings), static world labels (`world_text`, seven strings), and state-dependent quest display (`quest_ui`, seven strings). Folder names organize the editor; component custom IDs define Arcscript scopes. The director maps qualified keys such as `hud.station_name` to attribute UUIDs, then caches current values after each event. HUD drawing and looking at objects only call `GetUIText`; they never execute Arcscript or increment visits.

The Objectives and interface graph resets the seven `quest_ui` fields to their authored defaults before selecting a state. Individual elements override only the fields that differ. Powered and completed states share a setup element for the online grid, generator, and gate labels; completion changes the heading. The final leaf's content supplies the objective. This graph may update `quest_ui` but does not mutate player/quest state, shared HUD/world text, or dispatch world commands. Its entry has one identifying metadata attribute; display text remains in the UI components. Use one statement per Arcscript code block, as required by the bundled interpreter.

The five state fields are `player.power_cells` (Unreal's collected-item count), `quest.started` (terminal task accepted), `quest.required_power_cells` (authored requirement), `quest.power_restored` (set by generator success), and `quest.completed` (set by exit completion). Together with the nineteen UI strings and two inputs on **Game event**, the plugin imports **26 component variables and no globals**. Event inputs describe the most recent request and are replaced on every interaction; restart restores an empty event type and a false duplicate flag. Editing authored wording and refreshing the export changes the response without a C++ edit. Runtime changes to shared HUD/world strings appear on the next event refresh and persist across presentation resets; restart restores authored values.

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
