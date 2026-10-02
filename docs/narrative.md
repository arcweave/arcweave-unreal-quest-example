# Restore Power narrative

Open [Restore Power — Unreal C++ Quest Sample](https://arcweave.com/app/project/MWEZgMb62g) in [workspace Z7gAR6XY](https://arcweave.com/app/workspace/Z7gAR6XY/projects). Access requires permission to the workspace or project. The bundled export runs locally without an API key.

Arcweave owns quest decisions, feedback, objectives, interaction prompts, and station labels. Unreal supplies physical events and implements three authored world commands. The project has two boards: **01 · World event flows** has separate, labeled event lanes; **02 · Objectives and interface** selects the current display and stores the text catalog.

## World events

Each entry runs its connected path until an element has no output. A connection means “continue this event now.” Moving between the terminal, pickups, generator, and exit requires another Unreal event, so those lanes are not connected to one another.

| Unreal event | Arcweave entry | Authored behavior |
| --- | --- | --- |
| Start/restart | `InitializationElement` | Shows the opening instruction without accepting the task. |
| Use terminal | `TerminalEntryElement` | Checks completed, powered, and accepted states in order. Otherwise, `StartElement` sets `questStarted = true`. Repeated interactions have their own feedback. |
| Collect a new cell | `PickupEntryElement` | Denies collection before acceptance. Otherwise, `PickupActionElement` requests `collect_cell`, then `PickupCollectedElement` renders the updated count. |
| Use an already collected cell | `DuplicatePickupElement` | Supplies the duplicate-pickup response. Unreal tracks physical item identity. |
| Use generator | `GeneratorElement` | Checks already powered, task not accepted, and sufficient cells in order. Success sets `powerRestored = true` and requests `restore_power` and `open_gate`; otherwise it gives guidance. |
| Enter the exit volume | `ExitEntryElement` | Gives repeat feedback if completed; sets `questCompleted = true` only when power is restored; otherwise denies exit. |

The generator condition is `powerCells >= requiredPowerCells`. Before acceptance, the authored prerequisite wins even if enough cells are present. After power is restored, interacting again reaches an authored review response. Opening the gate does not complete the task: the player must enter the exit.

`UQuestDirector::RunGraph` calls `TranspileObject` for each element, dispatches attached command components, then uses `GetIsTargetBranch` to resolve the next connection against current variables. The `collect_cell` handler records the physical pickup and calls `SetVariable` **before** the following feedback element runs. That element uses:

```arcscript
show("Collected a power cell (", powerCells, "/", requiredPowerCells, ").")
```

Every executed element has nonempty content because the released plugin cannot parse empty elements. Entry and action nodes use short progress text; their final feedback replaces it before the event is published to the HUD.

Command names are component **custom IDs** understood by this sample's C++ registry. They are not built-in Arcscript functions or Unreal Gameplay Tags. `collect_cell` belongs only on `PickupActionElement`, where Unreal has supplied a pending pickup identity. `restore_power` and `open_gate` belong only on `SuccessElement`.

## Five variables

| Variable | New-game value | Meaning and owner |
| --- | --- | --- |
| `questStarted` | `false` | Arcweave marks terminal acceptance. |
| `powerCells` | `0` | Unreal writes the number of unique physical pickups collected. |
| `requiredPowerCells` | `2` | Authored configuration shared by generator conditions, objectives, and count feedback. |
| `powerRestored` | `false` | Arcweave records generator success. |
| `questCompleted` | `false` | Arcweave records arrival at the powered exit. |

The scene contains two pickups. Change `requiredPowerCells` to `1` to try a shorter task; a target above `2` needs additional Unreal pickups. There are no variables for UI strings. Arcweave Play Mode can exercise the lanes by starting at their entries and changing these variables in the Debugger. Physical collection, lighting, and gate commands execute in Unreal.

## Objectives and interface

After every event, C++ runs `PresentationEntryElement` and caches the chosen leaf's content and named attributes. The presentation branch checks these states in order:

| Condition | Objective |
| --- | --- |
| `questCompleted` | Task complete. You reached the exit. |
| `powerRestored` | Power restored. Walk through the open gate. |
| `!questStarted` | Use the terminal to begin. |
| `powerCells < requiredPowerCells` | Collect power cells (0/2), then use the generator. |
| Otherwise | Return to the generator and restore power. |

The collecting objective renders both numbers with `show()`, using the same variables as the generator condition. Presentation content contains only text and `show()` expressions, with no assignments or command components. Refreshing the cache records presentation visits once per event; HUD drawing and focus queries only read the cache.

Each display leaf has seven named plain-string attributes: `mission_heading`, `grid_status`, `terminal_prompt`, `cell_prompt`, `generator_prompt`, `generator_label`, and `gate_label`. The powered and completed displays retain the **Review running generator** prompt so the authored repeat response remains accessible.

`TextCatalogElement` contains twelve static fields: `brand`, `station_name`, `mission_tagline`, `cells_label`, `station_footer`, `terminal_label`, `cell_a_label`, `cell_b_label`, `sign_station`, `sign_distribution`, `sign_gate`, and `sign_exit`.

These attributes are element metadata with `value.type = "string"`, `value.plain = true`, and no custom ID. Their values are literal display strings, without HTML or Arcscript. Keep dynamic text in element content; the plugin does not transpile attribute values.

## Files and editing

- `Narrative/bindings.json` and `Source/ArcweaveQuest/QuestBindings.h` identify the entry points, feedback/display leaves, variables, and command components used by C++ or its tests. Conditions, connections, notes, and attributes retain their IDs in the graph without becoming C++ bindings.
- `Narrative/project.json` records the online project/workspace, export URLs, export time, and SHA-256 checksums. It contains no credential.
- `Narrative/import.json` reproduces this graph and its board layout using the all-locales authoring export plus coordinates from the Unreal export. Importing its `project` creates a separate project; it does not update the linked one.
- `Narrative/authoring.json` is the actual Arcweave JSON API export. That endpoint omits coordinates.
- `Content/ArcweaveExport/quest.json` is the actual Unreal API response, including its `project` envelope and layout. It is the only JSON file in that import directory.

Keep the bound IDs, command custom IDs, variable meanings, and named metadata fields when editing. Text, conditions, notes, node positions, and internal automatic paths can change without adding C++ bindings. Automatic paths must remain acyclic, stay within one board, and have at most one output per element. Each condition row has exactly one outgoing connection; use separate condition rows for separate outcomes. Changing the event interface, supported commands, or presentation contract requires corresponding C++ changes.

## Refresh the bundled narrative

Use Python 3.9 or later and an API key with **Read projects** access to the sample. Keep its token file outside the repository:

```bash
python Scripts/sync-narrative.py --token-file "/path/outside/repository/arcweave-token.txt"
```

The script downloads both exports from `https://arcweave.com` and validates them before replacing either file. It checks required bindings, the five variables and new-game defaults, graph connections and cycles, separate world-event entries, nonempty executable content, command placement, complete plain metadata, and presentation content that only reads state. New designer notes and internal graph objects do not require bindings. It updates export checksums, does not edit online projects, and never stores or prints the key. Two requests count against the workspace's import/export rate limit; on HTTP 429, wait for the reported interval and rerun. The layout-preserving `import.json` is maintained separately as a reproducible snapshot.

Run `python Scripts/test-sync-narrative.py` to check the bundled exports and validator regressions offline, without an API key.

For an editor run, press **R** to restart the mission after syncing, or start a new Play session. This reloads the local export. For an existing packaged build, also copy the refreshed `quest.json` into `Builds/Windows/ArcweaveQuest/Content/ArcweaveExport/` before restarting; packaging again includes it automatically. Text and condition edits do not require recompiling C++. This sample uses the released Unreal plugin v2.1.0.
