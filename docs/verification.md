# Local verification

Verified on 2026-10-01 with Unreal Engine 5.6.1, Visual Studio 2022 / MSVC 14.38.33130, Windows SDK 10.0.26100.0, and the pinned Arcweave v2.1.0 plugin.

| Check | Result |
| --- | --- |
| Win64 Development Editor build | Passed |
| `ArcweaveQuest.Flow` automation | Passed |
| `ArcweaveQuest.World` in the editor's game runtime | Passed |
| Win64 Development build, cook, stage, and archive | Passed |
| `ArcweaveQuest.World` in the packaged executable | Passed |
| Online Unreal/JSON exports and all 17 C++ UUID bindings | Matched |

The flow test covers terminal gating, zero/one/two-cell conditions, duplicate pickups, command dispatch exactly once, repeated completion, and restart defaults/visits. The world test drives the character's real focus raycasts and interaction path, verifies that collected actors become hidden and non-colliding, checks station light intensities, lets the gate animate through normal game ticks, and traces the doorway before/after opening and after reset.

Tests run with `-NullRHI -RenderOffscreen -unattended` and do not open a game window. These checks exercise gameplay and collision with rendering disabled. The runner reads each JSON report and fails if tests failed or no tests ran; it also waits for the packaged GUI executable to exit before checking the report. Packaged testing copies Unreal's HTML report template from the installed engine into the test build.

The released plugin logs warnings when an export has no cover and while evaluating conditions. Unreal therefore records the successful tests as `succeededWithWarnings`; there are no failed assertions. The sample leaves these upstream warnings visible and does not modify the plugin.

Run the commands in the [README](../README.md) to reproduce these checks. Full local logs and JSON/HTML reports are in `Saved/Validation`, which is excluded from Git. Packaged game output is in `Builds/Windows`, also excluded from Git.
