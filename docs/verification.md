# Local verification

Verified on 2026-10-05 with Unreal Engine 5.6.1, Visual Studio 2022 / MSVC 14.38.33130, Windows SDK 10.0.26100.0, and Arcweave plugin main commit `7513d9e113f4b8bca56e662fdb736fb41cb933ba`.

| Check | Result |
| --- | --- |
| Actual project's Win64 Development Editor build | Passed |
| `ArcweaveQuest.Flow` against the rebuilt project | Passed; `Report-Test-20261005-093148` |
| `ArcweaveQuest.World` against the rebuilt project | Passed; `Report-WorldTest-20261005-093233` |
| Win64 Development build, cook, stage, and archive | Passed; isolated custom package directory |
| `ArcweaveQuest.World` in the packaged executable | Passed; `Report-WorldTest-20261005-093248`, one passed, zero failed |
| Actual Arcweave browser Play Mode | Passed; two completed playthroughs, optional queries, and restart |
| Offline sync-validator regression tests | 245 passed |
| Laravel normalization and import integrity | Passed; zero errors |

The actual project's DLL was rebuilt while its editor was closed, then both Flow and World passed there. The package was built separately in an isolated project and tested before copying it to `Builds/Windows`; installed executable and narrative-export hashes match the tested build.

## Shared Play Mode and Unreal flow

The export contains **one board, 22 elements, six branches, 23 conditions with one output each, 49 connections, twelve jumpers, and 28 component runtime variables**. Player, Restore power quest, Cell A, and Cell B contribute seven state attributes; the three UI components add nineteen strings, and Game event supplies `type` and `cell_id`. There are no globals or simulation-only variables. The two query-entry markers are metadata, not runtime state, and the existing 45 UUID bindings are unchanged.

Browser testing used the actual online Play Mode, without manually changing event inputs in the Debugger. The first playthrough checked denied generator, pickup, and exit attempts; task acceptance; cell B collection; duplicate B rejection; cell A collection; power restoration; and exit completion. Optional inventory checks showed 0, 1, and 2 cells. Objective checks showed unaccepted, collecting, ready, and powered guidance. Both query flows returned through jumpers without changing gameplay state or event inputs. Ordinary responses and the denied exit returned directly to Station; successful exit had no continuation. Restart from completion restored quest and cell flags, count, and empty event inputs. A second full playthrough completed the mission without selecting either query. Native tests additionally cover the completed objective and all UI reset defaults.

Native Flow checks that the imported `StartingElementId` identifies the seven-choice station menu. Its first two outputs are query jumpers; Unreal finds the branch destination among the five gameplay choices instead of following the first output. It supplies the event and cell identity without executing menu labels. Each event executes only its selected lane, stops before returning to Station, then refreshes presentation exactly once, including terminal exit responses. The objectives/UI entry is found by the unique plain-string `entry_point = objectives_ui` marker on the starting element's board. The inventory query is identified by `entry_point = inventory`. Four event-lane jumpers, five objective returns, and one inventory return target Station; two menu jumpers reach the queries. There is no separate presentation board or fixed UUID binding for any entry.

Both optional queries are executed at all five objective stages in native Flow. Inventory preserves every variable; objective queries can update only `quest_ui`. Both preserve quest/event state, engine actions, physical pickups, the gameplay cursor, interaction feedback, and cached HUD presentation. They cannot enter another query or replay a world response. The inventory result uses the authored label and current count, while the objective result matches the automatically refreshed HUD. World verifies immediate objectives after acceptance and pickups, and the completed HUD after exit, without visiting the inventory query.

Pickup assertions verify that the same element sets the selected cell's `collected` flag, increments the cached inventory, and renders the new count before the physical `collect_cell` handler runs. The handler records the physical ID without rewriting narrative variables. Duplicate and denied pickups preserve the count and cell state. Tests also cover configurable requirements of one and two cells, repeated interactions, power restoration, exit completion, and restart. Restoring power does not complete the mission. Startup and restart execute only presentation, leaving world-event visits at zero and gameplay feedback empty. Unsupported events report the existing integration error without running a quest action or refreshing presentation.

## Presentation and world coverage

Flow checks immediate cached getters for all five player/quest values after direct `SetVariable` changes, plus updates from Arcscript. Repeated getters preserve variables, visits, commands, and notifications. All five presentation states, the completed-without-power case, and transitions back to earlier states verify that `quest_ui` defaults reset while player, quest, cell, event, and shared HUD/world values survive a presentation refresh. Restart restores authored defaults and the initial presentation.

Both native tests change `quest.power_restored` directly, then refresh through a normal terminal interaction. The read cache updates immediately, station lights follow the normal world refresh, and clearing the value restores unpowered lighting. These checks do not execute generator success or open the gate; opening remains a separate authored engine action.

World drives the character's real focus raycasts and interactions, verifies authored actor labels, confirms collected actors become hidden and non-colliding, checks light intensities, and lets the gate animate through normal game ticks. It traces the doorway before opening, after opening, and after reset. The gate sign remains fixed in world position and orientation as the camera and gate move. Completion is exercised through the real capsule overlap before power, after power, and after completion, without directly calling the completion method.

## Narrative validation

The validator checks the single-board layout, current starting element, five complete menu input assignments, common router destination, two read-only query choices, and four direct event routes without an else fallback. It accepts reordered menu choices and rekeyed query entries/jumpers without adding UUID bindings. It checks shared cell state, duplicate-before-acceptance pickup conditions, action placement, standalone data components, variable types/defaults, and nonempty executable content. Every branch condition has exactly one outgoing connection.

World paths must return to Station through their own lane's shared jumper; only the successful and already-completed exit outcomes may end. World lanes cannot enter optional query flows. Inventory content is read-only and returns through its own jumper. Presentation resets all seven `quest_ui` fields before any conditional overrides, then selects one of five objectives and returns through local jumpers targeting the current starting menu. Conditional script fragments must be balanced and read-only, with one statement per code block. Cycles within automatic paths are rejected while deliberate returns to Station are allowed. Presentation conditions are read-only, presentation writes are restricted to `quest_ui`, and presentation cannot dispatch world commands or mutate gameplay and shared text.

Regression fixtures reject invalid menu assignments, malformed return targets, extra state, misplaced commands, empty nodes, invalid display metadata, and multiple statements in a code block. Separate code blocks, multiline calls, quoted separators, native conditional fragments, and rich-text references remain supported. Obsolete global names in executable Arcscript are rejected without changing narrative string literals. The reproducible import passes Arcweave's Laravel normalization and integrity checks with zero errors.

## Running the checks

Automation and cooking use `-NullRHI -RenderOffscreen`, with unattended automation, so native verification opens no game window. These tests exercise gameplay and collision with rendering disabled. The runner checks each JSON report and fails if no tests ran or any test failed. It waits for the packaged executable to exit before reading that report. Package builds disable hot reload and support custom output directories.

The unchanged PowerShell build-script fixtures previously passed three scenarios covering default, absolute, and relative paths with spaces, executable selection, report-template copying, and cooker/build flags. A stale default runner deliberately fails, preventing a custom-output test from silently using the wrong package.

The bundled plugin logs existing cover/condition warnings, so successful native reports can show `succeededWithWarnings`; there are no failed assertions in the completed runs. The plugin source is unchanged. These checks do not constitute a rendered visual review, multiplayer test, or VR/mobile validation.

Run the commands in the [README](../README.md), `python Scripts/test-sync-narrative.py`, and `powershell -ExecutionPolicy Bypass -File Scripts/test-build.ps1` to repeat the relevant checks. Logs, JSON/HTML reports, browser screenshots, and playthrough state evidence are in `Saved/Validation/StationQueries-20261005`. Local verification artifacts and `Builds/Windows` are excluded from Git.
