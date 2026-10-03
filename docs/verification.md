# Local verification

Verified on 2026-10-03 with Unreal Engine 5.6.1, Visual Studio 2022 / MSVC 14.38.33130, Windows SDK 10.0.26100.0, and the pinned Arcweave v2.1.0 plugin.

| Check | Result |
| --- | --- |
| Win64 Development Editor build | Passed |
| `ArcweaveQuest.Flow` automation | Passed |
| `ArcweaveQuest.World` in the editor's game runtime | Passed |
| Win64 Development build, cook, stage, and archive | Passed |
| `ArcweaveQuest.World` in the packaged executable | Passed |
| Online Unreal/JSON exports and 47 runtime/test UUID bindings | Passed |
| Five scoped state attributes, nineteen UI strings, and two event inputs | Passed |
| Graph validation, negative fixtures, and import integrity | Passed |
| Offline sync-validator regression tests | 145 passed |

The flow test covers initialization without acceptance, authored prerequisites and repeat responses, pickup command/feedback ordering, configurable requirements of one and two cells, duplicate pickup identity, power restoration, exit completion, and restart. It checks that restoring power does not complete the mission, and that repeated getter calls preserve all variables, visits, commands, and notifications.

The Player and Restore power quest components import five typed state values; the three UI components add nineteen strings and Game event adds two typed inputs, for 26 component variables and no globals. Native checks verify each state attribute's owner, name, scope, type, and default. State, Inputs, Actions, and UI folders organize the authoring project. Each interaction must visit the shared entry exactly once and enter only its selected lane. Duplicate pickups use that same route and reach the authored duplicate condition before acceptance; a later interaction replaces both inputs. The four router conditions connect directly to their flow branches. Unknown events return the existing integration error without changing quest or world state, executing a response, or refreshing presentation. Startup and restart run presentation only, leaving world-event visits at zero and gameplay feedback empty.

Tests cover all five presentation states and transitions back to earlier states, proving the graph resets stale `quest_ui` values and preserves player/quest state, event inputs, and shared HUD/world strings. Cached reads remain side-effect free, and restart restores the initial presentation and authored shared text. Restart also matches the visits of a newly initialized session, which runs only the presentation nodes before any interaction.

Both native tests also change `quest.power_restored` through `SetVariable`, then refresh through a normal terminal interaction. The director immediately reads the changed attribute, station lights follow the normal world refresh, and clearing the value restores unpowered lighting. These checks never execute generator success or open the gate, proving there is no duplicate native power flag or restore-power command. Gate opening remains a separate engine action.

The world test drives the character's real focus raycasts and interaction path, checks authored actor labels, verifies that collected actors become hidden and non-colliding, checks station light intensities, lets the gate animate through normal game ticks, and traces the doorway before/after opening and reset. It also verifies that the gate sign faces into the station like the wall signs and keeps its world position and orientation as the camera moves, the panel opens, and the mission resets. It enters the actual capsule overlap at the exit before power, after power, and a second time after completion; it never calls the completion method directly.

Sync validation checks bound entry/branch/leaf IDs, five typed state attributes with valid new-game defaults and no globals, nineteen strings scoped to the three UI components, single-output acyclic event paths, four direct branch routes without an else fallback, duplicate-before-acceptance pickup checks, all world nodes reachable from the shared entry, separate action flows, the two action components and their placement, standalone data components, nonempty executable content, and presentation scripts restricted to the seven derived `quest_ui` fields. A live API audit confirms all 24 condition rows have exactly one outgoing connection, and the validator rejects additional outputs. It also checks that all seven `quest_ui` defaults reset at entry, each code block contains one statement supported by the released interpreter, only supported UI assignments and scoped state/input/UI reads are used, and presentation cannot mutate gameplay or shared text. Negative fixtures also reject loops, extra state, misplaced commands, empty executable nodes, and invalid display metadata. Obsolete global identifiers in executable Arcscript are rejected; narrative string literals are preserved. World events may still update shared HUD/world text. Additional designer notes and internal graph objects remain allowed. The reproducible import also passes Arcweave's Laravel normalization and integrity checks.

Automation runs with `-NullRHI -RenderOffscreen` and normally `-unattended`, without opening a game window. The original project also passed Flow through normal editor startup without `-unattended`, exercising the usual module compatibility check. These checks exercise gameplay and collision with rendering disabled. The runner reads each JSON report and fails if tests failed or no tests ran; it also waits for the packaged GUI executable to exit before checking the report. Packaged testing copies Unreal's HTML report template from the installed engine into the test build.

The Editor target was rebuilt and Flow and World tested in the original project directory with the editor closed, updating the DLL used when opening `ArcweaveQuest.uproject`. The package was built and tested in an isolated copy, then installed in the original project; executable and export hashes match the tested files. Packaging added `-NoHotReloadFromIDE` to the Unreal Build Tool arguments and `-NullRHI -RenderOffscreen` to the cooker arguments. Close the editor before rebuilding the project it has loaded.

The released plugin logs warnings when an export has no cover and while evaluating conditions. Unreal therefore records the successful tests as `succeededWithWarnings`; there are no failed assertions. The sample leaves these upstream warnings visible and does not modify the plugin.

Run the Unreal commands in the [README](../README.md) and `python Scripts/test-sync-narrative.py` for the offline validator tests. Logs and JSON/HTML reports for this revision are in `Saved/Validation/StateComponents-20261003`, which is excluded from Git. Packaged game output is in `Builds/Windows`, also excluded from Git.

The component migration preserves existing graph layout, titles, notes, UI defaults, and narrative wording. Arcscript references now use `player.*` and `quest.*`; generator success retains only the Open gate action reference. The graph still has 24 conditions with one output each.
