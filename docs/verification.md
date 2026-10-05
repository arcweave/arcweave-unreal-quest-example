# Local verification

Verified on 2026-10-05 with Unreal Engine 5.6.1, Visual Studio 2022 / MSVC 14.38.33130, and Windows SDK 10.0.26100.0. The sample pins [plugin v2.2.0](https://github.com/arcweave/arcweave-unreal-plugin/releases/tag/v2.2.0) at `1eb60cdcd0ebbefe14d92f996481f603397e13a4`.

The isolated project's Development Editor build and Win64 Development package passed after removing the duplicate pickup command/state. The tested package is installed in `Builds/Windows`. Its loose narrative export was subsequently refreshed for the connection-color change below. The actual project's editor DLL was also rebuilt after the editor closed. Existing saves and local project settings were preserved.

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

## Narrative and import checks

- **256 Python checks** pass for narrative structure, defaults, command placement, query restrictions, import consistency, and synchronized exports.
- Comparing the live exports before and after the change confirms that the Arcscript, conditions, connections, starting element, and existing variable attributes are unchanged.
- The only action is now **Open gate**, with a rich-text **Description** that adds no runtime variable. Pickup visibility follows `cell_a.collected` and `cell_b.collected`.
- Runtime, authoring, and uploadable import exports are generated together. The project retains one board, 22 elements, six branches, 23 conditions with one output each, 49 connections, twelve jumpers, and **36 runtime variables**.

After native validation, all 49 connection themes were set to `default`. An export comparison confirms this follow-up changes only connection themes; the 256 Python checks pass again.

The current Unreal export SHA-256 is `7623a0a6b99bbe187928fa1ca585a3ede29bd02c2a9e0e974b5c237fb0dba4b9`. All export checksums are recorded in `Narrative/project.json`. The sample now explicitly writes save format 2; earlier checkpoints require a fresh save.

The full frontend `JsonImportHandler.formatData()` and backend integrity checks passed during publication preparation with zero errors. This update rechecks the bundled import through the Python suite. The three unchanged PowerShell build-script fixtures also passed during publication preparation and run in GitHub Actions.

## Screenshots and browser coverage

The [gameplay screenshot](images/gameplay.png) and [board overview](images/arcweave-board.png) are from publication validation. Gameplay appearance and board paths are unchanged; the current board removes the pickup action reference and uses default connection colors throughout. A fresh browser screenshot could not be captured because the collaborative preview snapshot failed.

Publication validation confirmed anonymous access to the public design view and Play Mode, generator guidance before acceptance, and terminal acceptance. This update verifies the changed component metadata through the API and the quest behavior through native automation; it does not claim a new browser playthrough.

## Repeating the checks

See [development commands](development.md#native-automation). Automation and cooking use `-NullRHI -RenderOffscreen`, so no game window opens. These results cover the Windows single-player sample.

Current logs, reports, graph comparison, and installed hashes are under `Saved/Validation/PickupState-20261005`, excluded from Git. Earlier plugin, import, screenshot, and license evidence remains under `Saved/Validation/Publication-20261005`.
