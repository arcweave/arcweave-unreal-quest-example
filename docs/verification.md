# Local verification

The results below record the 2026-10-05 baseline, before the integration switched to named lookups. The current C++ refactor still requires a Windows/Unreal build and native automation run; these historical results do not validate it.

The baseline used Unreal Engine 5.6.1, Visual Studio 2022 / MSVC 14.38.33130, and Windows SDK 10.0.26100.0. The sample pins [plugin v2.2.0](https://github.com/arcweave/arcweave-unreal-plugin/releases/tag/v2.2.0) at `1eb60cdcd0ebbefe14d92f996481f603397e13a4`. Its Development Editor build and Win64 Development package passed after removing the duplicate pickup command/state.

## Native checks

| Check | Result / report |
| --- | --- |
| `ArcweaveQuest.Flow` | Passed; `Report-Test-20261005-132122` |
| `ArcweaveQuest.Persistence` | Passed; `Report-Test-20261005-132152` |
| Packaged `ArcweaveQuest.World` | Passed; `Report-WorldTest-20261005-132300` |
| Packaged `ArcweaveQuest.SaveLoadWorld` | Passed; `Report-WorldTest-20261005-132313` |
| `ArcweaveQuest.SaveSession` — write process | Passed; `Report-Test-20261005-132322` |
| `ArcweaveQuest.SaveSession` — read process | Passed; `Report-Test-20261005-132336` |

These checks cover accepted and denied pickups, duplicates, reverse pickup order, restart, actual actor visibility/collision, and external changes to each collected flag. Persistence checks restore those flags without running pickup scripts, reject legacy saves whose default version field is omitted by Unreal, and preserve the separately authored gate action. The packaged save/load check exercises F5/F9 input, player pose, HUD, lighting, gate state, and exit overlaps. All six runs passed with warnings and no failures.

The plugin itself is unchanged. Its four `Arcweave.Project` tests passed during release preparation (`Report-Test-20261005-111739`), alongside verification of the packaged sample and dependency license notices.

## Narrative and import baseline

The baseline narrative has one board, 22 elements, six branches, 23 conditions with one output each, 49 connections, twelve jumpers, and 36 runtime variables. Its only action is **Open gate**; pickup visibility follows `cell_a.collected` and `cell_b.collected`. After native validation, all connection themes were set to `default`; the recorded export comparison found no other changes in that follow-up. The sample writes save format 2; earlier checkpoints require a fresh save.

The frontend `JsonImportHandler.formatData()` and backend integrity checks passed during publication preparation with zero errors. The starter import and runtime export remain bundled in the repository. For your own changes, [replace the runtime export](narrative.md#refresh-the-bundled-narrative) and run the native checks above.

GitHub Actions runs the three PowerShell build-script routing fixtures without Unreal. These check build tooling, not narrative behavior.

## Screenshots and browser coverage

The [gameplay screenshot](images/gameplay.png) and [board overview](images/arcweave-board.png) are from publication validation. Gameplay appearance and board paths are unchanged; the bundled board removes the pickup action reference and uses default connection colors throughout.

Publication validation confirmed anonymous access to the public design view and Play Mode, generator guidance before acceptance, and terminal acceptance. No new browser playthrough is recorded for the named-lookup refactor.

## Repeating the checks

See [development commands](development.md#native-automation). Automation and cooking use `-NullRHI -RenderOffscreen`, so no game window opens. These results cover the Windows single-player sample.

The baseline logs, reports, graph comparison, and installed hashes were recorded under `Saved/Validation/PickupState-20261005`, excluded from Git. Earlier plugin, import, screenshot, and license evidence was recorded under `Saved/Validation/Publication-20261005`.
