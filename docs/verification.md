# Local verification

Verified on 2026-10-05 with Unreal Engine 5.6.1, Visual Studio 2022 / MSVC 14.38.33130, and Windows SDK 10.0.26100.0. The sample pins [plugin v2.2.0](https://github.com/arcweave/arcweave-unreal-plugin/releases/tag/v2.2.0) at `1eb60cdcd0ebbefe14d92f996481f603397e13a4`. Its release tree matches the tested preparation commit exactly.

The isolated project's Development Editor build and Win64 Development package passed. The actual project's DLLs were rebuilt after its editor closed. The tested package is installed in `Builds/Windows`; all 57 installed files match their tested hashes, and existing saves and local project settings were preserved.

## Native checks

| Check | Result / report |
| --- | --- |
| `Arcweave.Project` | All four tests passed without warnings; `Report-Test-20261005-111739` |
| `ArcweaveQuest.Flow` | Passed; `Report-Test-20261005-111823` |
| `ArcweaveQuest.Persistence` | Passed; `Report-Test-20261005-111914` |
| Packaged `ArcweaveQuest.SaveLoadWorld` | Passed; `Report-WorldTest-20261005-112229` |

The plugin checks cover starting-element discovery, runtime snapshots, component/board variables, and import failures. The sample checks cover the shared narrative flow and 36-variable UI/state model, disk saves and rejected loads, F5/F9 input routing, actual pickup visibility/collision, gate and lighting restoration, player pose, and exit overlaps without replaying quest events. The sample retains the plugin's existing cover/condition warnings; there are no test failures.

Both sample notices and all four plugin license/notice files are present as loose packaged files and match their source bytes. No credentials are required by the game or these native tests.

## Narrative and import checks

- **256 Python checks** pass for narrative structure, defaults, query restrictions, import consistency, project selection, and synchronized exports.
- **Three PowerShell fixtures** pass for build-script routing and automation-report handling without Unreal. GitHub Actions runs these offline checks on Windows.
- The directly uploadable `Narrative/import.json` passes the actual frontend `JsonImportHandler.formatData()` with zero content errors or normalization logs.
- Both browser-normalized and API-normalized imports pass the backend integrity checker with zero errors. Starting element, graph IDs, component custom IDs, locales, and layout are preserved.

The live graph is unchanged: one board, 22 elements, six branches, 23 conditions with one output each, 49 connections, and twelve jumpers. Adding `hud.controls` brings the runtime to seven state variables, 27 UI strings, and two event inputs. `TerminalAcceptElement` names the terminal-acceptance binding; the real starting element remains the station menu.

The refreshed Unreal export SHA-256 is `28b25533d5e98a112cf68bcd06d8e99bb80556cc3c745913be8e5f705f82e26b`. Runtime, authoring, and uploadable import exports are generated together and recorded in `Narrative/project.json`. Changed narrative content invalidates older checkpoints.

## Screenshots and public access

The [gameplay screenshot](images/gameplay.png) was captured from the running game after accepting the task, collecting both cells, and restoring power. It uses offscreen rendering at 1920×1080 and shows the actual HUD, authored controls, lighting, and open gate state.

The [board screenshot](images/arcweave-board.png) comes from the live Arcweave project. Its public design view was verified with no signed-in user. Public Play Mode also loaded anonymously; generator guidance before acceptance and terminal acceptance were checked. The browser host disconnected before the remainder of that browser playthrough, so this pass does not claim a new complete browser run. Native Flow coverage checks the unchanged quest paths.

## Repeating the checks

See [development commands](development.md#native-automation). Automation and cooking use `-NullRHI -RenderOffscreen`; the gameplay screenshot uses rendering enabled with `-RenderOffscreen`. These results cover the Windows single-player sample, not multiplayer or VR/mobile targets.

Logs, JSON/HTML reports, import evidence, packaged-license checks, and installed hashes are under `Saved/Validation/Publication-20261005`, excluded from Git. Earlier save/load and keyboard-regression reports remain under `Saved/Validation/SaveLoad-20261005` and `Saved/Validation/CheckpointInput-20261005`.
