# Local verification

Verified on 2026-10-04 with Unreal Engine 5.6.1, Visual Studio 2022 / MSVC 14.38.33130, Windows SDK 10.0.26100.0, and Arcweave plugin main commit `7513d9e113f4b8bca56e662fdb736fb41cb933ba`.

| Check | Result |
| --- | --- |
| Win64 Development Editor build | Passed |
| `ArcweaveQuest.Flow` automation | Passed |
| `ArcweaveQuest.World` in the editor's game runtime | Passed |
| Win64 Development build, cook, stage, and archive to a custom directory | Passed |
| `ArcweaveQuest.World` in that custom-directory packaged executable | Passed |
| PowerShell build-script routing and argument checks | 3 scenarios passed |
| Online Unreal/JSON exports and 45 runtime/test UUID bindings | Passed |
| Five scoped state attributes, nineteen UI strings, and two event inputs | Passed |
| Graph validation, negative fixtures, and import integrity | Passed |
| Offline sync-validator regression tests | 171 passed |

The flow test checks immediate getters for all five state values after direct `SetVariable` changes, followed by presentation refresh and restart. Arcscript and pickup-command changes exercise the same variable-change subscription. Scalar reads use the cache and do not copy the entire imported project.

The flow test covers initialization without acceptance, authored prerequisites and repeat responses, pickup command/feedback ordering, configurable requirements of one and two cells, duplicate pickup identity, power restoration, exit completion, and restart. It checks that restoring power does not complete the mission, and that repeated getter calls preserve all variables, visits, commands, and notifications.

The Player and Restore power quest components import five typed state values; the three UI components add nineteen strings and Game event adds two typed inputs, for 26 component variables and no globals. Native checks verify each state attribute's owner, name, scope, type, and default. State, Inputs, Actions, and UI folders organize the authoring project. World-event tests read `StartingElementId` from the imported project. Each interaction must visit that shared entry exactly once and enter only its selected lane. Duplicate pickups use that same route and reach the authored duplicate condition before acceptance; a later interaction replaces both inputs. The four router conditions connect directly to their flow branches. Unknown events return the existing integration error without changing quest or world state, executing a response, or refreshing presentation. Startup and restart run presentation only, leaving world-event visits at zero and gameplay feedback empty.

Tests cover all five presentation states and transitions back to earlier states, proving the graph resets stale `quest_ui` values and preserves player/quest state, event inputs, and shared HUD/world strings. Cached reads remain side-effect free, and restart restores the initial presentation and authored shared text. Restart also matches the visits of a newly initialized session, which runs only the presentation nodes before any interaction.

Both native tests also change `quest.power_restored` through `SetVariable`, then refresh through a normal terminal interaction. The director’s variable-change subscription immediately updates its read cache, station lights follow the normal world refresh, and clearing the value restores unpowered lighting. These checks never execute generator success or open the gate, proving there is no duplicate native power flag or restore-power command. Gate opening remains a separate engine action.

The world test drives the character's real focus raycasts and interaction path, checks authored actor labels, verifies that collected actors become hidden and non-colliding, checks station light intensities, lets the gate animate through normal game ticks, and traces the doorway before/after opening and reset. It also verifies that the gate sign faces into the station like the wall signs and keeps its world position and orientation as the camera moves, the panel opens, and the mission resets. It enters the actual capsule overlap at the exit before power, after power, and a second time after completion; it never calls the completion method directly.

Sync validation checks the authored `startingElement`, the unique `quest_presentation` board and plain-string element marker named `entry_point` with value `objectives_ui`, and bound branch/leaf IDs, five typed state attributes with valid new-game defaults and no globals, nineteen strings scoped to the three UI components, single-output acyclic event paths, four direct branch routes without an else fallback, duplicate-before-acceptance pickup checks, all world nodes reachable from the shared entry, separate action flows, the two action components and their placement, standalone data components, nonempty executable content, and presentation scripts restricted to the seven derived `quest_ui` fields. A live API audit confirms all 24 condition rows have exactly one outgoing connection, and the validator rejects additional outputs. It also checks that all seven `quest_ui` defaults reset at entry, each code block contains one statement supported by the bundled interpreter, only supported UI assignments and scoped state/input/UI reads are used, and presentation cannot mutate gameplay or shared text. Negative fixtures also reject loops, extra state, misplaced commands, empty executable nodes, and invalid display metadata. Obsolete global identifiers in executable Arcscript are rejected; narrative string literals are preserved. World events may still update shared HUD/world text. Additional designer notes and internal graph objects remain allowed. The reproducible import also passes Arcweave's Laravel normalization and integrity checks.

Automation runs with `-NullRHI -RenderOffscreen` and normally `-unattended`, without opening a game window. These checks exercise gameplay and collision with rendering disabled. The runner reads each JSON report and fails if tests failed or no tests ran; it also waits for the packaged GUI executable to exit before checking the report. Packaged testing copies Unreal's HTML report template from the installed engine into the test build.

The Editor target was built and Flow and World tested in the isolated project. The package was built in an isolated copy through the committed `Scripts/build.ps1`, using a relative `-OutputDirectory` with spaces. Packaged World used the same directory, and the tested package was installed in the original project; executable and export hashes match the tested files. The script supplies `-NoHotReloadFromIDE` to the Unreal Build Tool arguments and `-NullRHI -RenderOffscreen` to the cooker arguments. Close the editor before rebuilding the project it has loaded.

The bundled plugin logs warnings when an export has no cover and while evaluating conditions. Unreal therefore records the successful tests as `succeededWithWarnings`; there are no failed assertions. The sample leaves these upstream warnings visible and does not modify the plugin.

Run the Unreal commands in the [README](../README.md) and `python Scripts/test-sync-narrative.py` for the offline validator tests. Logs and JSON/HTML reports for this revision are in `Saved/Validation/ReviewFixes-20261004`, which is excluded from Git. Packaged game output is in `Builds/Windows`, also excluded from Git.

The component migration preserves existing graph layout, titles, notes, UI defaults, and narrative wording. Arcscript references now use `player.*` and `quest.*`; generator success retains only the Open gate action reference. The graph still has 24 conditions with one output each.

Build-script fixtures execute the actual PowerShell script against fake UAT/game executables, checking default, absolute, and relative output paths with spaces, the selected executable, copied report template, and package flags. The previous/default runner deliberately fails in later scenarios so a test cannot silently use a stale package. Run `powershell -ExecutionPolicy Bypass -File Scripts/test-build.ps1` without Unreal for these checks.

World-script regressions reject two assignments or acceptance plus `show()` in one block across all three export formats, while preserving separate blocks, multiline calls, quoted separators, native conditional fragments, and rich-text element references.
