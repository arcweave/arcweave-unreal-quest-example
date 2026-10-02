#!/usr/bin/env python3
"""Offline regression checks: python3 Scripts/test-sync-narrative.py."""

import copy
import importlib.util
import json
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

    def catalog_attribute(self):
        return self.project["attributes"][self.element("TextCatalogElement")["attributes"][0]]

    def generator_connection(self):
        return self.project["connections"][self.element("GeneratorElement")["outputs"][0]]

    def localized_content(self, binding):
        locale = next(item["iso"] for item in self.project["locales"] if item["base"] is None)
        return self.project["contents"][self.bindings[binding]]["content"][locale]

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

    def test_metadata_catalog_does_not_need_executable_content(self):
        self.element("TextCatalogElement")["content"] = None
        self.validate()

    def test_missing_bound_entry_is_rejected(self):
        del self.project["elements"][self.bindings["TerminalEntryElement"]]
        self.assert_invalid("missing the TerminalEntryElement binding")

    def test_display_assignment_is_rejected(self):
        self.element("PresentationCollectingElement")["content"] = "<pre><code>powerCells = 99</code></pre>"
        self.assert_invalid("Display content cannot assign state")

    def test_nested_display_function_is_rejected(self):
        self.element("PresentationCollectingElement")["content"] = "<pre><code>show(random(10))</code></pre>"
        self.assert_invalid("Display content cannot assign state")

    def test_display_command_is_rejected(self):
        self.element("PresentationCollectingElement")["components"] = [self.bindings["CollectCellComponent"]]
        self.assert_invalid("Only PickupAction")

    def test_display_condition_function_is_rejected(self):
        branch = self.project["branches"][self.bindings["PresentationBranch"]]
        self.project["conditions"][branch["conditions"]["ifCondition"]]["script"] = "resetVisits()"
        self.assert_invalid("Presentation conditions cannot call functions")

    def test_missing_display_metadata_is_rejected(self):
        self.element("PresentationReadyElement")["attributes"] = []
        self.assert_invalid("missing its required named metadata")

    def test_rich_text_catalog_attribute_is_rejected(self):
        self.catalog_attribute()["value"]["plain"] = False
        self.assert_invalid("metadata must be nonempty plain strings")

    def test_html_in_plain_catalog_attribute_is_rejected(self):
        self.catalog_attribute()["value"]["data"] = "<p>Station title.</p>"
        self.assert_invalid("metadata must be nonempty plain strings")

    def test_metadata_custom_id_is_rejected(self):
        self.catalog_attribute()["customId"] = "extraState"
        self.assert_invalid("metadata must be nonempty plain strings")

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
        self.assert_invalid("Display content cannot assign state")


if __name__ == "__main__":
    unittest.main()
