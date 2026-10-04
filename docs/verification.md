# Local verification

Verified on 2026-10-04 with Unreal Engine 5.6.1, Visual Studio 2022 / MSVC 14.38.33130, Windows SDK 10.0.26100.0, and Arcweave plugin main commit `7513d9e113f4b8bca56e662fdb736fb41cb933ba`.

| Check | Result |
| --- | --- |
| Isolated Win64 Development Editor build | Passed |
| `ArcweaveQuest.Flow` automation | Passed; `Report-Test-20261004-182715` |
| `ArcweaveQuest.World` in the isolated editor's game runtime | Passed; `Report-WorldTest-20261004-182837` |
| Actual project's Win64 Development Editor build | Passed after closing the editor |
| Flow and World against the rebuilt actual project | Passed; `Report-Test-20261004-183522` and `Report-WorldTest-20261004-183612` |
| Win64 Development build, cook, stage, and archive | Passed; custom package directory |
| `ArcweaveQuest.World` in the packaged executable | Passed; `Report-WorldTest-20261004-183310`, one passed, zero failed |
| Actual Arcweave browser Play Mode | Passed; 42 navigation steps and restart checks |
| Offline sync-validator regression tests | 206 passed |
| Laravel normalization and import integrity | Passed; zero errors |

The native build and editor tests first passed in an isolated project. After the original editor closed, the actual project's DLL was rebuilt and both Flow and World passed there as well. The tested package was copied to `Builds/Windows`; its executable hashes match the tested build. Final menu layout adjustments were synced into the bundled and packaged exports after verifying that only coordinates, sizing, label positions, and connection attachment faces changed.

## Shared Play Mode and Unreal flow

The export contains **one board, 23 elements, seven branches, 24 conditions with one output each, 51 connections, five return jumpers, and 28 component runtime variables**. Player, Restore power quest, Cell A, and Cell B contribute seven state attributes; the three UI components add nineteen strings, and Game event supplies `type` and `cell_id`. There are no globals or simulation-only variables.

Browser testing used the actual online Play Mode, without manually changing event inputs in the Debugger. The playthrough checked denied generator, pickup, and exit attempts; task acceptance; missing-cell guidance; cell A collection; duplicate A rejection; cell B collection; power restoration; and exit completion. It visited all five objective states and followed their jumpers back to the interaction menu. Restart restored the quest and cell flags, count, and empty event inputs.

Native Flow checks that the imported `StartingElementId` identifies the five-choice station menu and that all choices target the same event router. Unreal supplies the event and cell identity without executing the menu label assignments. Each event executes only its selected lane, then refreshes presentation exactly once. The objectives/UI entry is found by the unique plain-string `entry_point = objectives_ui` marker on the starting element's board. Each objective has a separate imported jumper targeting the menu; native traversal resolves the target and stops before executing it. There is no separate presentation board or fixed UUID binding for either entry.

Pickup assertions verify that Arcscript sets the selected cell's `collected` flag and increments the cached inventory before the physical `collect_cell` handler runs. The handler records the physical ID without rewriting narrative variables. Duplicate and denied pickups preserve the count and cell state. Tests also cover configurable requirements of one and two cells, repeated interactions, power restoration, exit completion, and restart. Restoring power does not complete the mission. Startup and restart execute only presentation, leaving world-event visits at zero and gameplay feedback empty. Unsupported events report the existing integration error without running a quest action or refreshing presentation.

## Presentation and world coverage

Flow checks immediate cached getters for all five player/quest values after direct `SetVariable` changes, plus updates from Arcscript. Repeated getters preserve variables, visits, commands, and notifications. All five presentation states and transitions back to earlier states verify that `quest_ui` defaults reset while player, quest, cell, event, and shared HUD/world values survive a presentation refresh. Restart restores authored defaults and the initial presentation.

Both native tests change `quest.power_restored` directly, then refresh through a normal terminal interaction. The read cache updates immediately, station lights follow the normal world refresh, and clearing the value restores unpowered lighting. These checks do not execute generator success or open the gate; opening remains a separate authored engine action.

World drives the character's real focus raycasts and interactions, verifies authored actor labels, confirms collected actors become hidden and non-colliding, checks light intensities, and lets the gate animate through normal game ticks. It traces the doorway before opening, after opening, and after reset. The gate sign remains fixed in world position and orientation as the camera and gate move. Completion is exercised through the real capsule overlap before power, after power, and after completion, without directly calling the completion method.

## Narrative validation

The validator checks the single-board layout, current starting element, five complete menu input assignments, common router destination, and four direct event routes without an else fallback. It checks shared cell state, duplicate-before-acceptance pickup conditions, action placement, standalone data components, variable types/defaults, and nonempty executable content. Every branch condition has exactly one outgoing connection.

World paths must reach the objectives/UI boundary; presentation must reset all seven `quest_ui` fields and return through local jumpers targeting the current starting menu. Cycles within automatic paths are rejected while the deliberate menu/event/presentation loop is allowed. Presentation conditions are read-only, presentation writes are restricted to `quest_ui`, and presentation cannot dispatch world commands or mutate gameplay and shared text.

Regression fixtures reject invalid menu assignments, malformed return targets, extra state, misplaced commands, empty nodes, invalid display metadata, and multiple statements in a code block. Separate code blocks, multiline calls, quoted separators, native conditional fragments, and rich-text references remain supported. Obsolete global names in executable Arcscript are rejected without changing narrative string literals. The reproducible import passes Arcweave's Laravel normalization and integrity checks with zero errors.

## Running the checks

Automation and cooking use `-NullRHI -RenderOffscreen`, with unattended automation, so native verification opens no game window. These tests exercise gameplay and collision with rendering disabled. The runner checks each JSON report and fails if no tests ran or any test failed. It waits for the packaged executable to exit before reading that report. Package builds disable hot reload and support custom output directories.

The unchanged PowerShell build-script fixtures previously passed three scenarios covering default, absolute, and relative paths with spaces, executable selection, report-template copying, and cooker/build flags. A stale default runner deliberately fails, preventing a custom-output test from silently using the wrong package.

The bundled plugin logs existing cover/condition warnings, so successful native reports can show `succeededWithWarnings`; there are no failed assertions in the completed runs. The plugin source is unchanged. These checks do not constitute a rendered visual review, multiplayer test, or VR/mobile validation.

Run the commands in the [README](../README.md), `python Scripts/test-sync-narrative.py`, and `powershell -ExecutionPolicy Bypass -File Scripts/test-build.ps1` to repeat the relevant checks. Logs, JSON/HTML reports, browser screenshots, and playthrough state evidence are in `Saved/Validation/SharedPlayMode-20261004`. Local verification artifacts and `Builds/Windows` are excluded from Git.
