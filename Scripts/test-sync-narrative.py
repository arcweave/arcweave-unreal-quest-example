#!/usr/bin/env python3
"""Offline regression checks: python3 Scripts/test-sync-narrative.py."""

import copy
import importlib.util
import json
from html import escape
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("sync_narrative", ROOT / "Scripts/sync-narrative.py")
SYNC = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SYNC)


class NarrativeValidationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.bindings = json.loads((ROOT / "Narrative/bindings.json").read_text(encoding="utf-8"))
        cls.unreal = json.loads((ROOT / "Content/ArcweaveExport/quest.json").read_text(encoding="utf-8"))["project"]
        cls.authoring = json.loads((ROOT / "Narrative/authoring.json").read_text(encoding="utf-8"))
        cls.localized = json.loads((ROOT / "Narrative/import.json").read_text(encoding="utf-8"))["project"]

    def setUp(self):
        self.project = copy.deepcopy(self.unreal)

    def validate(self):
        SYNC.validate_bindings(self.project, self.bindings)

    def assert_invalid(self, message):
        with self.assertRaisesRegex(ValueError, message):
            self.validate()

    def element(self, binding):
        return self.project["elements"][self.bindings[binding]]

    def variable(self, binding):
        return self.project["variables"][self.bindings[binding]]

    def ui_attribute(self, binding="HUDTextComponent"):
        component = self.project["components"][self.bindings[binding]]
        return self.project["attributes"][component["attributes"][0]]

    def generator_connection(self):
        return self.project["connections"][self.element("GeneratorElement")["outputs"][0]]

    def localized_content(self, binding):
        locale = next(item["iso"] for item in self.project["locales"] if item["base"] is None)
        return self.project["contents"][self.bindings[binding]]["content"][locale]

    def code_block(self, content):
        return "<pre><code>" + escape(content) + "</code></pre>"

    def script(self, binding, content):
        self.element(binding)["content"] = self.code_block(content)

    def entry_resets(self):
        component = self.project["components"][self.bindings["QuestUIComponent"]]
        return [self.code_block(f'reset(quest_ui.{self.project["attributes"][key]["customId"]})')
                for key in component["attributes"]]

    def insert_presentation_element(self, content):
        element = "a6f37e73-273c-48c8-b7d7-06585d5e28f2"
        connection = "747e4254-a0c0-46ba-8ed7-5199d36ec366"
        original_id = self.element("PresentationEntryElement")["outputs"][0]
        original = self.project["connections"][original_id]
        self.project["elements"][element] = {
            "content": content, "components": [], "outputs": [connection],
        }
        self.project["connections"][connection] = dict(original, sourceid=element, sourceType="elements")
        original.update(targetid=element, targetType="elements")
        board = next(board for board in self.project["boards"].values()
                     if original_id in (board.get("connections") or []))
        board["elements"].append(element)
        board["connections"].append(connection)
        return self.project["elements"][element]

    def ui_folder(self):
        ui_ids = {self.bindings[name] for name in SYNC.UI_COMPONENTS}
        return next(item for item in self.project["components"].values()
                    if set(item.get("children", [])) == ui_ids)

    def test_bundled_unreal_export(self):
        self.validate()

    def test_bundled_authoring_export(self):
        self.project = copy.deepcopy(self.authoring)
        self.validate()

    def test_valid_all_locales_import(self):
        self.project = copy.deepcopy(self.localized)
        self.assertNotIn("content", self.element("TerminalEntryElement"))
        self.assertTrue(self.localized_content("TerminalEntryElement")["text"])
        self.validate()

    def test_required_cell_count_can_be_tuned(self):
        self.variable("RequiredPowerCellsVariable")["value"] = 1
        self.validate()

    def test_designer_note_needs_no_cpp_binding(self):
        note = "7ae1f39d-3536-411d-a38a-867961d460f4"
        self.project["notes"][note] = {"content": "<p>Designer note.</p>", "x": 0, "y": 0}
        self.project["boards"][self.bindings["Board"]]["notes"].append(note)
        self.validate()

    def test_internal_element_and_connection_need_no_cpp_bindings(self):
        element = "ed6601c3-99e8-4a19-91ad-f073f0be3daf"
        connection = "6d4e7fdb-16d7-42ba-9eec-947e8383b033"
        original = self.generator_connection()
        self.project["elements"][element] = {
            "content": "<p>Checking station link.</p>", "components": [], "outputs": [connection],
        }
        self.project["connections"][connection] = dict(original, sourceid=element, sourceType="elements")
        original.update(targetid=element, targetType="elements")
        board = self.project["boards"][self.bindings["Board"]]
        board["elements"].append(element)
        board["connections"].append(connection)
        self.validate()

    def test_ui_attribute_display_name_can_change(self):
        self.ui_attribute()["name"] = "Shared station branding"
        self.validate()

    def test_all_ui_defaults_can_be_edited_without_changing_the_schema(self):
        for binding in SYNC.UI_COMPONENTS:
            component = self.project["components"][self.bindings[binding]]
            for ident in component["attributes"]:
                self.project["attributes"][ident]["value"]["data"] += " revised"
        self.validate()

    def test_flat_runtime_export_does_not_require_folders(self):
        self.project["components"] = {key: item for key, item in self.project["components"].items()
                                      if "children" not in item}
        self.validate()

    def test_ui_folder_cannot_introduce_a_runtime_scope(self):
        self.project = copy.deepcopy(self.localized)
        self.ui_folder()["customId"] = "ui"
        self.assert_invalid("UI folder is organizational")

    def test_ui_folder_cannot_introduce_attributes(self):
        self.project = copy.deepcopy(self.localized)
        self.ui_folder()["attributes"] = ["unexpected-folder-attribute"]
        self.assert_invalid("UI folder is organizational")

    def test_data_components_must_stay_together_in_the_ui_folder(self):
        self.project = copy.deepcopy(self.localized)
        self.ui_folder()["children"].remove(self.bindings["WorldTextComponent"])
        self.assert_invalid("UI folder must contain exactly the three data components")

    def test_ui_scope_cannot_be_duplicated_by_another_component(self):
        self.project["components"][self.bindings["RestorePowerComponent"]]["customId"] = "quest_ui"
        self.assert_invalid("UI data component scopes must be unique")

    def test_missing_bound_entry_is_rejected(self):
        del self.project["elements"][self.bindings["TerminalEntryElement"]]
        self.assert_invalid("missing the TerminalEntryElement binding")

    def test_display_assignment_is_rejected(self):
        self.element("PresentationCollectingElement")["content"] = "<pre><code>powerCells = 99</code></pre>"
        self.assert_invalid("Presentation may only assign quest_ui fields")

    def test_nested_display_function_is_rejected(self):
        self.element("PresentationCollectingElement")["content"] = "<pre><code>show(random(10))</code></pre>"
        self.assert_invalid("Presentation may only assign quest_ui fields")

    def test_display_command_is_rejected(self):
        self.element("PresentationCollectingElement")["components"] = [self.bindings["CollectCellComponent"]]
        self.assert_invalid("Only PickupAction")

    def test_display_condition_function_is_rejected(self):
        branch = self.project["branches"][self.bindings["PresentationBranch"]]
        self.project["conditions"][branch["conditions"]["ifCondition"]]["script"] = "resetVisits()"
        self.assert_invalid("Presentation conditions cannot call functions")

    def test_nested_completion_condition_is_read_only(self):
        branch = self.project["branches"][self.bindings["PresentationCompletionBranch"]]
        self.project["conditions"][branch["conditions"]["ifCondition"]]["script"] = "reset(quest_ui.mission_heading)"
        self.assert_invalid("Presentation conditions cannot call functions")

    def test_shared_power_setup_cannot_modify_gameplay(self):
        self.script("PresentationPoweredSetupElement", "questCompleted = true")
        self.assert_invalid("Presentation may only assign quest_ui fields")

    def test_presentation_internal_element_needs_no_cpp_binding(self):
        self.insert_presentation_element("<pre><code>show(hud.station_name)</code></pre>")
        self.validate()

    def test_unbound_presentation_element_is_still_validated(self):
        self.insert_presentation_element("<pre><code>resetAll()</code></pre>")
        self.assert_invalid("Presentation may only assign quest_ui fields")

    def test_unbound_presentation_element_cannot_keep_stale_metadata(self):
        self.insert_presentation_element("<p>Display helper.</p>")["attributes"] = ["legacy-field"]
        self.assert_invalid("Presentation elements must not retain metadata")

    def test_presentation_requires_all_five_terminal_leaves(self):
        connection = next(item for item in self.project["connections"].values()
                          if item["targetid"] == self.bindings["PresentationReadyElement"])
        connection["targetid"] = self.bindings["PresentationCollectingElement"]
        self.assert_invalid("Presentation must terminate at exactly its five bound display leaves")

    def test_events_cannot_enter_a_presentation_leaf_even_on_the_same_board(self):
        source = self.project["boards"][self.bindings["PresentationBoard"]]
        destination = self.project["boards"][self.bindings["Board"]]
        for collection in ("elements", "branches", "connections"):
            destination[collection].extend(source[collection])
            source[collection] = []
        self.generator_connection().update(
            targetid=self.bindings["PresentationReadyElement"], targetType="elements")
        self.assert_invalid("World events must not automatically enter the presentation graph")

    def test_stale_element_display_metadata_is_rejected(self):
        self.element("PresentationReadyElement")["attributes"] = ["legacy-display-attribute"]
        self.assert_invalid("Presentation elements must not retain metadata")

    def test_rich_text_ui_attribute_is_rejected(self):
        self.ui_attribute()["value"]["plain"] = False
        self.assert_invalid("UI attributes must be nonempty plain strings")

    def test_html_in_plain_ui_attribute_is_rejected(self):
        self.ui_attribute()["value"]["data"] = "<p>Station title.</p>"
        self.assert_invalid("UI attributes must be nonempty plain strings")


    def test_wrong_ui_component_custom_id_is_rejected(self):
        self.project["components"][self.bindings["HUDTextComponent"]]["customId"] = "other_ui"
        self.assert_invalid("hud data component must have its required custom ID")

    def test_missing_ui_attribute_custom_id_is_rejected(self):
        del self.ui_attribute()["customId"]
        self.assert_invalid("exactly its required string attributes and custom IDs")

    def test_unknown_ui_attribute_custom_id_is_rejected(self):
        self.ui_attribute()["customId"] = "unknown_field"
        self.assert_invalid("exactly its required string attributes and custom IDs")

    def test_non_string_ui_attribute_is_rejected(self):
        self.ui_attribute()["value"] = {"type": "integer", "data": 1}
        self.assert_invalid("UI attributes must be nonempty plain strings")

    def test_ui_attribute_wrong_owner_is_rejected(self):
        self.ui_attribute()["cId"] = self.bindings["RestorePowerComponent"]
        self.assert_invalid("UI attributes must be nonempty plain strings")

    def test_additional_ui_attribute_is_rejected(self):
        extra = "c916868d-e2ab-4d42-8f92-d4152a29a9e6"
        self.project["attributes"][extra] = dict(self.ui_attribute(), customId="debug_label")
        self.project["components"][self.bindings["HUDTextComponent"]]["attributes"].append(extra)
        self.assert_invalid("exactly its required string attributes and custom IDs")

    def test_additional_command_component_variable_is_rejected(self):
        extra = "ed23bc46-83b3-4937-ab43-91c2c8b85d08"
        owner = self.bindings["RestorePowerComponent"]
        self.project["attributes"][extra] = dict(self.ui_attribute(), cId=owner, customId="debug_label")
        self.project["components"][owner]["attributes"] = [extra]
        self.assert_invalid("Only the nineteen UI string attributes may add scoped variables")

    def test_additional_board_variable_is_rejected(self):
        extra = "5aa30329-45b8-42df-b65b-1e94f0a76a84"
        owner = self.bindings["Board"]
        self.project["attributes"][extra] = dict(self.ui_attribute(), cType="boards", cId=owner, customId="debug_label")
        self.project["boards"][owner]["attributes"] = [extra]
        self.assert_invalid("Only the nineteen UI string attributes may add scoped variables")

    def test_ui_component_cannot_be_attached_as_a_command(self):
        for binding in SYNC.UI_COMPONENTS:
            with self.subTest(component=binding):
                self.element("InitializationElement")["components"] = [self.bindings[binding]]
                self.assert_invalid("UI components must remain standalone data")

    def test_presentation_can_show_known_ui_field(self):
        self.script("PresentationCollectingElement",
                    "show(hud.station_name, world_text.sign_exit, quest_ui.mission_heading, powerCells)")
        self.validate()

    def test_presentation_condition_can_read_known_ui_field(self):
        branch = self.project["branches"][self.bindings["PresentationBranch"]]
        self.project["conditions"][branch["conditions"]["ifCondition"]]["script"] = (
            'hud.station_name != "" && world_text.sign_exit != "" && quest_ui.grid_status != ""')
        self.validate()

    def test_presentation_assignments_can_read_known_scopes(self):
        self.script("PresentationCollectingElement", "quest_ui.mission_heading = hud.station_name")
        self.validate()

    def test_presentation_cannot_assign_world_text(self):
        self.script("PresentationCollectingElement", 'world_text.sign_exit = "Changed"')
        self.assert_invalid("Presentation may only assign quest_ui fields")

    def test_presentation_cannot_assign_an_unknown_quest_ui_field(self):
        self.script("PresentationCollectingElement", 'quest_ui.unknown = "Changed"')
        self.assert_invalid("Presentation may only assign quest_ui fields")

    def test_presentation_cannot_use_compound_assignment(self):
        self.script("PresentationCollectingElement", 'quest_ui.mission_heading += "Changed"')
        self.assert_invalid("Presentation may only assign quest_ui fields")

    def test_presentation_cannot_call_a_function_from_an_assignment(self):
        self.script("PresentationCollectingElement", "quest_ui.mission_heading = random(10)")
        self.assert_invalid("Presentation may only assign quest_ui fields")

    def test_missing_entry_default_reset_is_rejected(self):
        self.element("PresentationEntryElement")["content"] = "".join(self.entry_resets()[:-1])
        self.assert_invalid("reset each of the seven quest_ui fields exactly once")

    def test_duplicate_entry_default_reset_is_rejected(self):
        resets = self.entry_resets()
        self.element("PresentationEntryElement")["content"] = "".join(resets[:-1] + resets[:1])
        self.assert_invalid("reset each of the seven quest_ui fields exactly once")

    def test_entry_reset_order_does_not_depend_on_attribute_order(self):
        self.element("PresentationEntryElement")["content"] = "".join(reversed(self.entry_resets()))
        self.validate()

    def test_entry_resets_must_precede_other_statements(self):
        self.element("PresentationEntryElement")["content"] = (
            self.code_block('quest_ui.mission_heading = "Changed"') + "".join(self.entry_resets()))
        self.assert_invalid("reset all seven fields before any other statements")

    def test_entry_resets_cannot_be_conditional(self):
        self.script("PresentationEntryElement", "if questStarted:\n"
                    "    reset(quest_ui.mission_heading)")
        self.assert_invalid("Presentation may only assign quest_ui fields")

    def test_reset_all_cannot_replace_entry_defaults(self):
        self.script("PresentationEntryElement", "resetAll()")
        self.assert_invalid("Presentation may only assign quest_ui fields")

    def test_entry_cannot_reset_gameplay_or_static_ui(self):
        for target in ("questStarted", "hud.station_name", "world_text.sign_exit", "quest_ui.unknown"):
            with self.subTest(target=target):
                self.element("PresentationEntryElement")["content"] = (
                    "".join(self.entry_resets()) + self.code_block(f"reset({target})"))
                self.assert_invalid("Only the presentation entry may reset known quest_ui fields")

    def test_later_presentation_elements_cannot_reset_defaults(self):
        self.element("PresentationPoweredSetupElement")["content"] = self.entry_resets()[0]
        self.assert_invalid("Only the presentation entry may reset known quest_ui fields")

    def test_multiple_statements_in_one_code_block_are_rejected(self):
        for separator in ("\n", "; "):
            with self.subTest(separator=separator):
                self.script("PresentationPoweredSetupElement", separator.join((
                    "quest_ui.mission_heading = hud.station_name",
                    "quest_ui.grid_status = world_text.sign_gate",
                )))
                self.assert_invalid("Each presentation code block must contain exactly one simple statement")

    def test_multiple_assignments_in_separate_code_blocks_are_allowed(self):
        self.element("PresentationPoweredSetupElement")["content"] = (
            self.code_block("quest_ui.mission_heading = hud.station_name")
            + self.code_block("quest_ui.grid_status = world_text.sign_gate"))
        self.validate()

    def test_localized_entry_requires_all_default_resets(self):
        self.project = copy.deepcopy(self.localized)
        self.localized_content("PresentationEntryElement")["text"] = "<p>Missing resets.</p>"
        self.assert_invalid("reset each of the seven quest_ui fields exactly once")

    def test_presentation_unknown_ui_field_is_rejected(self):
        self.element("PresentationCollectingElement")["content"] = "<pre><code>show(hud.unknown_field)</code></pre>"
        self.assert_invalid("Presentation may only assign quest_ui fields")

    def test_presentation_ui_method_call_is_rejected(self):
        self.element("PresentationCollectingElement")["content"] = "<pre><code>show(hud.station_name.upper())</code></pre>"
        self.assert_invalid("Presentation may only assign quest_ui fields")

    def test_presentation_ui_assignment_is_rejected(self):
        self.element("PresentationCollectingElement")["content"] = '<pre><code>hud.station_name = "Changed"</code></pre>'
        self.assert_invalid("Presentation may only assign quest_ui fields")

    def test_presentation_cannot_show_bare_ui_object(self):
        self.element("PresentationCollectingElement")["content"] = "<pre><code>show(hud)</code></pre>"
        self.assert_invalid("Presentation may only assign quest_ui fields")

    def test_presentation_cannot_read_unknown_component(self):
        self.element("PresentationCollectingElement")["content"] = "<pre><code>show(other.station_name)</code></pre>"
        self.assert_invalid("Presentation may only assign quest_ui fields")

    def test_extra_global_variable_is_rejected(self):
        self.project["variables"]["extra"] = {
            "name": "extra", "type": "integer", "cType": "global", "value": 0,
        }
        self.assert_invalid("exactly five global variables")

    def test_integer_is_not_accepted_as_boolean_default(self):
        self.variable("QuestCompletedVariable")["value"] = 0
        self.assert_invalid("initial value has the wrong type")

    def test_already_accepted_new_game_is_rejected(self):
        self.variable("QuestStartedVariable")["value"] = True
        self.assert_invalid("not a valid new-game default")

    def test_zero_required_cells_is_rejected(self):
        self.variable("RequiredPowerCellsVariable")["value"] = 0
        self.assert_invalid("not a valid new-game default")

    def test_multiple_element_outputs_are_rejected(self):
        outputs = self.element("GeneratorElement")["outputs"]
        outputs.append(outputs[0])
        self.assert_invalid("at most one automatic output")

    def test_multiple_condition_outputs_are_rejected(self):
        original_id, original = next(
            (ident, item) for ident, item in self.project["connections"].items()
            if item["sourceType"] == "conditions"
        )
        extra = "b1f10946-7b6c-4b51-85a2-090fd3e03f9d"
        target = self.bindings["MissingCellsElement"]
        if original["targetid"] == target:
            target = self.bindings["SuccessElement"]
        self.project["connections"][extra] = dict(original, targetid=target, targetType="elements")
        board = next(board for board in self.project["boards"].values()
                     if original_id in (board.get("connections") or []))
        board["connections"].append(extra)
        self.assert_invalid("Each condition row must have exactly one outgoing connection")

    def test_condition_output_bookkeeping_must_match_connection(self):
        condition = next(iter(self.project["conditions"].values()))
        condition["output"] = "incorrect-output"
        self.assert_invalid("Each condition row must have exactly one outgoing connection")

    def test_automatic_cycle_is_rejected(self):
        self.generator_connection().update(targetid=self.bindings["GeneratorElement"], targetType="elements")
        self.assert_invalid("automatic event path contains a cycle")

    def test_automatic_cross_event_entry_is_rejected(self):
        self.generator_connection().update(targetid=self.bindings["ExitEntryElement"], targetType="elements")
        self.assert_invalid("world-event entry points must not execute one another")

    def test_collection_requires_pickup_event_context(self):
        self.generator_connection().update(targetid=self.bindings["PickupActionElement"], targetType="elements")
        self.assert_invalid("Only the pickup event supplies the physical identity")

    def test_changed_command_custom_id_is_rejected(self):
        self.project["components"][self.bindings["CollectCellComponent"]]["customId"] = "collect_other"
        self.assert_invalid("custom ID no longer matches")

    def test_collection_command_on_terminal_is_rejected(self):
        self.element("TerminalEntryElement")["components"] = [self.bindings["CollectCellComponent"]]
        self.assert_invalid("Only PickupAction")

    def test_power_command_on_initialization_is_rejected(self):
        self.element("InitializationElement")["components"] = [self.bindings["RestorePowerComponent"]]
        self.assert_invalid("only Success may restore power")

    def test_gate_command_on_exit_is_rejected(self):
        self.element("CompletedElement")["components"] = [self.bindings["OpenGateComponent"]]
        self.assert_invalid("only Success may restore power")

    def test_missing_pickup_command_is_rejected(self):
        self.element("PickupActionElement")["components"] = []
        self.assert_invalid("Only PickupAction")

    def test_empty_executable_entry_is_rejected(self):
        self.element("TerminalEntryElement")["content"] = None
        self.assert_invalid("Executable elements need nonempty content")

    def test_empty_executable_html_is_rejected(self):
        self.element("PickupActionElement")["content"] = "<p> </p>"
        self.assert_invalid("Executable elements need nonempty content")

    def test_empty_localized_executable_content_is_rejected(self):
        self.project = copy.deepcopy(self.localized)
        self.localized_content("TerminalEntryElement")["text"] = None
        self.assert_invalid("Executable elements need nonempty content")

    def test_localized_display_assignment_is_rejected(self):
        self.project = copy.deepcopy(self.localized)
        self.localized_content("PresentationCollectingElement")["text"] = "<pre><code>powerCells = 99</code></pre>"
        self.assert_invalid("Presentation may only assign quest_ui fields")


if __name__ == "__main__":
    unittest.main()
