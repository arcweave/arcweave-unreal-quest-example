# Local verification

Verified on 2026-10-02 with Unreal Engine 5.6.1, Visual Studio 2022 / MSVC 14.38.33130, Windows SDK 10.0.26100.0, and the pinned Arcweave v2.1.0 plugin.

| Check | Result |
| --- | --- |
| Win64 Development Editor build | Passed |
| `ArcweaveQuest.Flow` automation | Passed |
| `ArcweaveQuest.World` in the editor's game runtime | Passed |
| Win64 Development build, cook, stage, and archive | Passed |
| `ArcweaveQuest.World` in the packaged executable | Passed |
| Online Unreal/JSON exports and all 38 runtime/test UUID bindings | Matched |
| Five gameplay variables and 47 plain text metadata fields | Verified |
| Graph validation, negative fixtures, and import integrity | Passed |
| Offline sync-validator regression tests | 35 passed |

The flow test covers initialization without acceptance, authored prerequisites and repeat responses, pickup command/feedback ordering, configurable requirements of one and two cells, duplicate pickup identity, power restoration, exit completion, and restart. It checks that restoring power does not complete the mission, and that repeated getter calls preserve all variables, visits, commands, and notifications. Restart matches the visits of a newly initialized session, including its initialization and presentation nodes.

The world test drives the character's real focus raycasts and interaction path, checks authored actor labels, verifies that collected actors become hidden and non-colliding, checks station light intensities, lets the gate animate through normal game ticks, and traces the doorway before/after opening and reset. It enters the actual capsule overlap at the exit before power, after power, and a second time after completion; it never calls the completion method directly.

Sync validation checks bound entry/leaf IDs, five typed variables with valid new-game defaults, single-output acyclic event paths, separate world-event entry points, command placement, nonempty executable content, and presentation text that only reads state. A live API audit confirms all 18 condition rows have exactly one outgoing connection, and the validator rejects additional outputs. Negative fixtures also reject loops, extra state, misplaced commands, empty executable nodes, and invalid display metadata. Additional designer notes and internal graph objects remain allowed. The reproducible import also passes Arcweave's Laravel normalization and integrity checks.

Tests run with `-NullRHI -RenderOffscreen -unattended` and do not open a game window. These checks exercise gameplay and collision with rendering disabled. The runner reads each JSON report and fails if tests failed or no tests ran; it also waits for the packaged GUI executable to exit before checking the report. Packaged testing copies Unreal's HTML report template from the installed engine into the test build.

The change was built and tested in a separate local copy while the original editor remained open. Packaging that isolated copy added `-NoHotReloadFromIDE` to the Unreal Build Tool arguments and `-NullRHI -RenderOffscreen` to the cooker arguments. Close the editor before rebuilding the project it has loaded.

The released plugin logs warnings when an export has no cover and while evaluating conditions. Unreal therefore records the successful tests as `succeededWithWarnings`; there are no failed assertions. The sample leaves these upstream warnings visible and does not modify the plugin.

Run the Unreal commands in the [README](../README.md) and `python Scripts/test-sync-narrative.py` for the offline validator tests. Full local logs and JSON/HTML reports are in `Saved/Validation`, which is excluded from Git. Packaged game output is in `Builds/Windows`, also excluded from Git.
