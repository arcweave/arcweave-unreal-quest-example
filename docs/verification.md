# Local verification

Verified on 2026-10-05 with Unreal Engine 5.6.1, Visual Studio 2022 / MSVC 14.38.33130, Windows SDK 10.0.26100.0, and Arcweave plugin main commit `3b235a2a16cdff2f7bcdee5022bb1f16db466270`.

The actual project's editor and plugin DLLs rebuilt successfully while its editor was closed. The Win64 Development package built, cooked, staged, and archived successfully in an isolated project. After testing, it was copied to `Builds/Windows`; the installed executable and narrative-export hashes match the tested package.

| Automation check | Result |
| --- | --- |
| `ArcweaveQuest.Flow` | Passed; `Report-Test-20261005-102117` |
| `ArcweaveQuest.Persistence` | Passed; `Report-Test-20261005-102631` |
| `ArcweaveQuest.World` | Passed; `Report-WorldTest-20261005-103027` |
| `ArcweaveQuest.SaveLoadWorld` | Passed; `Report-WorldTest-20261005-102216` |
| `ArcweaveQuest.SaveSession — Write` | Passed; `Report-Test-20261005-102732` |
| `ArcweaveQuest.SaveSession — Read (new process)` | Passed; `Report-Test-20261005-102850` |
| `Arcweave.Project.RuntimeState` | Passed; `Report-Test-20261005-102935` |
| `Packaged ArcweaveQuest.World` | Passed; `Report-WorldTest-20261005-103110` |
| `Packaged ArcweaveQuest.SaveLoadWorld` | Passed; `Report-WorldTest-20261005-103027` |

All nine native automation runs completed with one passed test and zero failures each. The sample reports retain existing plugin cover/condition warnings; the plugin runtime-state test completed without warnings. The 249 offline narrative-validator tests pass, the three PowerShell build-script fixtures pass, and Laravel normalization/import integrity reports zero errors.

## Save and restore coverage

`Persistence` writes real `USaveGame` files after one cell, after power restoration, and after completion. It advances the live mission, loads each earlier checkpoint, and compares every variable and visit counter, authored defaults, cached UI text, resolved objective/feedback, narrative cursors, applied pickups, gate state, player transform, and view rotation. Loading publishes the restored director state once and dispatches no collection or gate-opening command. Repeated loads preserve the snapshot exactly.

The test also verifies missing and unreadable slots, incompatible narrative fingerprints, unsupported save formats, unknown pickup IDs, and incomplete plugin snapshots. Failed loads preserve narrative/world state and output transforms while providing authored operation feedback. Restart preserves the disk save, and a replacement director can restore it and continue collecting. A separate case saves narrative power with the native gate still closed, proving loading does not infer an unexecuted gate command from the power variable.

`SaveLoadWorld` uses actual terminal, pickup, generator, and exit interactions. It saves with one cell, while the gate is opening, and inside the completed exit. Loading restores actor visibility/collision, lighting, world labels, player pose, camera rotation, and the HUD. Gate restoration snaps immediately to its saved logical state. The test waits through subsequent ticks to confirm loading inside the exit does not synthesize another event or increase visits, then restores the earlier checkpoint and collects the remaining cell normally. It passes against both the rebuilt project and the packaged executable.

`SaveSession` runs in two separate Unreal processes. The writer accepts the task, collects cell B, and writes an automation-only slot. The reader starts with fresh variables and visits, loads the file, checks all saved variables and visits plus resolved UI/world state and player pose, then collects cell A. The read phase removes the test slot. These tests never write or delete the player's `ArcweaveQuestCheckpoint` slot.

The plugin's own `Arcweave.Project.RuntimeState` test passes against the pinned plugin. Its snapshot stores mutable values and visits; the sample supplies the file, cursor/presentation, applied world effects, and player pose. Matching imported narrative content is required, including text and layout. This sample does not migrate checkpoints between content versions.

## Quest and narrative regressions

The export contains one board, 22 elements, six branches, 23 conditions with one output each, 49 connections, and twelve jumpers. The four State components contribute seven variables; four UI components contribute 26 strings; Game event contributes two inputs. There are 35 component runtime variables, no globals, and 46 UUID bindings.

Adding `save_ui` left the graph, starting element, jumpers, notes, and layout unchanged. The previously verified browser playthrough remains the same: inventory and objectives are optional, both return with **Back to station**, ordinary interactions return to Station, and successful exit ends the run. File save/load is implemented by Unreal.

`Flow` covers query-first routing, all five objective states, read-only inventory and objective query restrictions, configurable cell requirements, either pickup order, denied and duplicate interactions, command timing, cached reads, exact presentation refresh counts, and restart. `World` covers real focus raycasts, collected-actor collision, lighting, gate animation and doorway collision, the fixed gate sign, and completion through actual capsule overlaps. Both pass with the new plugin and UI component; `World` also passes in the package.

The validator checks the four UI schemas and prevents query paths from writing save controls or feedback. It retains the single-board graph, one-output condition, command placement, default/type, entry discovery, jumper ownership, and automatic-path cycle checks. The reproducible authoring import passes Laravel's normalization and integrity checks without changing database content.

## Repeating the checks

Commands are listed in the [README](../README.md#verification-and-packaging). Run the `SaveSession` writer before the reader, using `-SavePhase Write` and `-SavePhase Read`; omitting the phase reports an error. The runner validates each JSON report and fails if no tests run or any test fails. Package tests use the same custom output directory as packaging.

Native automation and cooking use `-NullRHI -RenderOffscreen`, so no game window opens. These checks exercise gameplay with rendering disabled; they are not a rendered HUD review, multiplayer test, or VR/mobile validation. Logs, actual/packaged JSON and HTML reports, and installed hashes are under `Saved/Validation/SaveLoad-20261005`, excluded from Git.
