"""Rebuild the minimal map. The station itself is spawned by C++ at BeginPlay."""
import unreal

world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
world.get_world_settings().set_editor_property("force_no_precomputed_lighting", True)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0, 0, 110), unreal.Rotator(0, 0, 0))
if not unreal.EditorLoadingAndSavingUtils.save_map(world, "/Game/Maps/PowerStation"):
    raise RuntimeError("Could not save PowerStation map")
unreal.log("Created PowerStation map")
