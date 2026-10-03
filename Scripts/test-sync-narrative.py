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
        return self.route_connection("GeneratorBranch", 0)

    def conditions(self, binding):
        group = self.project["branches"][self.bindings[binding]]["conditions"]
        identifiers = [group["ifCondition"]] + (group.get("elseIfConditions", []) or [])
        if group.get("elseCondition"):
            identifiers.append(group["elseCondition"])
        return [self.project["conditions"][ident] for ident in identifiers]

    def route_connection(self, binding, index):
        return self.project["connections"][self.conditions(binding)[index]["output"]]

    def add_output(self, source_binding, target_binding):
        ident = "c4aa7afb-eb4e-40ce-8e2b-ac1cb6b98a5b"
        self.project["connections"][ident] = {
            "sourceid": self.bindings[source_binding], "sourceType": "elements",
            "targetid": self.bindings[target_binding], "targetType": "elements",
        }
        self.element(source_binding)["outputs"] = [ident]
        self.project["boards"][self.bindings["Board"]]["connections"].append(ident)

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
        self.assertNotIn("content", self.element("EventEntryElement"))
        self.assertTrue(self.localized_content("EventEntryElement")["text"])
        self.validate()

    def test_required_cell_count_can_be_tuned(self):
        self.variable("RequiredPowerCellsVariable")["value"] = 1
        self.validate()

    def test_input_display_names_and_feedback_wording_can_change(self):
        self.project["components"][self.bindings["GameEventComponent"]]["name"] = "Interaction inputs"
        self.project["attributes"][self.bindings["EventTypeAttribute"]]["name"] = "Requested interaction"
        self.element("DuplicatePickupElement")["content"] = "<p>You have already collected this cell.</p>"
        self.validate()

    def test_event_router_accepts_equivalent_whitespace_and_quotes(self):
        self.conditions("EventRouterBranch")[0]["script"] = "  (game_event.type == 'use_terminal')  "
        self.validate()

    def test_independent_event_routes_can_be_reordered(self):
        group = self.project["branches"][self.bindings["EventRouterBranch"]]["conditions"]
        group["ifCondition"], group["elseIfConditions"][1] = group["elseIfConditions"][1], group["ifCondition"]
        self.validate()

    def test_starting_element_must_be_shared_event_entry(self):
        self.project["startingElement"] = self.bindings["StartElement"]
        self.assert_invalid("starting element must be the shared world-event entry")

    def test_event_entry_cannot_bypass_router(self):
        output = self.element("EventEntryElement")["outputs"][0]
        self.project["connections"][output].update(targetid=self.bindings["TerminalBranch"], targetType="branches")
        self.assert_invalid("shared world-event entry must connect directly to its routing branch")

    def test_event_route_cannot_select_another_interaction(self):
        self.route_connection("EventRouterBranch", 0)["targetid"] = self.bindings["GeneratorBranch"]
        self.assert_invalid("event router must select terminal, collect_cell, generator, or exit")

    def test_event_router_has_no_startup_or_separate_duplicate_route(self):
        for event in ("start", "duplicate_cell"):
            with self.subTest(event=event):
                self.conditions("EventRouterBranch")[0]["script"] = f'game_event.type == "{event}"'
                self.assert_invalid("event router must select terminal, collect_cell, generator, or exit")

    def test_event_router_cannot_repeat_an_event_condition(self):
        self.conditions("EventRouterBranch")[1]["script"] = self.conditions("EventRouterBranch")[0]["script"]
        self.assert_invalid("event router must select terminal, collect_cell, generator, or exit")

    def test_event_router_requires_all_four_interaction_routes(self):
        group = self.project["branches"][self.bindings["EventRouterBranch"]]["conditions"]
        removed = group["elseIfConditions"].pop(0)
        output = self.project["conditions"].pop(removed)["output"]
        del self.project["connections"][output]
        self.project["boards"][self.bindings["Board"]]["connections"].remove(output)
        self.assert_invalid("event router must select terminal, collect_cell, generator, or exit")

    def test_event_router_cannot_add_an_else_fallback(self):
        group = self.project["branches"][self.bindings["EventRouterBranch"]]["conditions"]
        condition = "496e1844-c29b-40c3-91a8-93d22f790efd"
        output = "11834d53-50e3-4350-ae59-cec3161688ab"
        group["elseCondition"] = condition
        self.project["conditions"][condition] = {"output": output, "script": None}
        self.project["connections"][output] = {
            "sourceid": condition, "sourceType": "conditions",
            "targetid": self.bindings["ExitBranch"], "targetType": "branches",
        }
        self.project["boards"][self.bindings["Board"]]["connections"].append(output)
        self.assert_invalid("without an else fallback")

    def test_exit_requires_its_explicit_event_condition(self):
        group = self.project["branches"][self.bindings["EventRouterBranch"]]["conditions"]
        condition = group["elseIfConditions"].pop()
        group["elseCondition"] = condition
        self.project["conditions"][condition]["script"] = None
        self.assert_invalid("without an else fallback")

    def test_event_router_cannot_change_state_while_routing(self):
        self.conditions("EventRouterBranch")[0]["script"] = 'resetAll() || game_event.type == "use_terminal"'
        self.assert_invalid("event router must select terminal, collect_cell, generator, or exit")

    def test_pickup_route_cannot_bypass_duplicate_and_acceptance_checks(self):
        self.route_connection("EventRouterBranch", 1).update(
            targetid=self.bindings["PickupActionElement"], targetType="elements")
        self.assert_invalid("event router must select terminal, collect_cell, generator, or exit")

    def test_router_cannot_insert_an_intermediate_element_before_a_branch(self):
        element = "912c9af5-83db-419d-8ab2-cee0fcedd657"
        output = "8a820365-b29a-4397-9f85-ad55a9babfca"
        route = self.route_connection("EventRouterBranch", 0)
        self.project["elements"][element] = {
            "content": "<p>Checking terminal.</p>", "outputs": [output],
        }
        self.project["connections"][output] = dict(route, sourceid=element, sourceType="elements")
        route.update(targetid=element, targetType="elements")
        board = self.project["boards"][self.bindings["Board"]]
        board["elements"].append(element)
        board["connections"].append(output)
        self.assert_invalid("directly through its branch")

    def test_duplicate_check_runs_before_task_acceptance(self):
        group = self.project["branches"][self.bindings["PickupBranch"]]["conditions"]
        group["ifCondition"], group["elseIfConditions"][0] = group["elseIfConditions"][0], group["ifCondition"]
        self.assert_invalid("pickup branch must check duplicate identity first")

    def test_duplicate_route_cannot_collect_again(self):
        self.route_connection("PickupBranch", 0)["targetid"] = self.bindings["PickupActionElement"]
        self.assert_invalid("pickup branch must check duplicate identity first")

    def test_pickup_requires_the_supplied_physical_identity_flag(self):
        self.conditions("PickupBranch")[0]["script"] = "powerCells > 0"
        self.assert_invalid("pickup branch must check duplicate identity first")

    def test_pickup_acceptance_check_cannot_be_inverted(self):
        self.conditions("PickupBranch")[1]["script"] = "questStarted"
        self.assert_invalid("pickup branch must check duplicate identity first")

    def test_duplicate_feedback_cannot_fall_through_to_collection(self):
        self.add_output("DuplicatePickupElement", "PickupActionElement")
        self.assert_invalid("Duplicate-pickup feedback must end its flow")

    def test_task_required_feedback_cannot_fall_through_to_collection(self):
        self.add_output("PickupTerminalRequiredElement", "PickupActionElement")
        self.assert_invalid("Task-required pickup feedback must end its flow")

    def test_shared_entry_can_show_current_input_without_changing_it(self):
        self.script("EventEntryElement", "show(game_event.type)")
        self.validate()

    def test_duplicate_feedback_cannot_change_quest_or_input_state(self):
        for script in ('questStarted = true', 'game_event.type = "use_terminal"', 'resetAll()', 'show(random(10))'):
            with self.subTest(script=script):
                self.script("DuplicatePickupElement", script)
                self.assert_invalid("Duplicate-pickup feedback must be feedback only")

    def test_duplicate_feedback_cannot_emit_a_world_command(self):
        self.element("DuplicatePickupElement")["components"] = [self.bindings["OpenGateComponent"]]
        self.assert_invalid("only Success may restore power and open the gate")

    def test_duplicate_feedback_cannot_enter_another_lane(self):
        self.add_output("DuplicatePickupElement", "MissingCellsElement")
        self.assert_invalid("world-event lanes must not execute one another")

    def test_shared_entry_cannot_accept_the_task_during_routing(self):
        self.script("EventEntryElement", "questStarted = true")
        self.assert_invalid("shared event entry must be feedback only")

    def test_shared_entry_cannot_overwrite_the_identity_flag(self):
        self.script("EventEntryElement", "game_event.cell_already_collected = false")
        self.assert_invalid("shared event entry must be feedback only")

    def test_localized_duplicate_feedback_cannot_change_state(self):
        self.project = copy.deepcopy(self.localized)
        self.localized_content("DuplicatePickupElement")["text"] = self.code_block("questStarted = true")
        self.assert_invalid("Duplicate-pickup feedback must be feedback only")

    def test_cross_event_interior_node_is_rejected(self):
        self.generator_connection().update(targetid=self.bindings["TerminalAcceptedElement"], targetType="elements")
        self.assert_invalid("world-event lanes must not execute one another")

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
        self.assert_invalid("UI and game_event data component scopes must be unique")

    def test_event_scope_cannot_be_duplicated_by_another_component(self):
        self.project["components"][self.bindings["RestorePowerComponent"]]["customId"] = "game_event"
        self.assert_invalid("UI and game_event data component scopes must be unique")

    def test_event_component_must_keep_its_input_scope(self):
        self.project["components"][self.bindings["GameEventComponent"]]["customId"] = "interaction"
        self.assert_invalid("game_event data component must have its required custom ID")

    def test_event_component_cannot_be_placed_in_the_ui_folder(self):
        self.project = copy.deepcopy(self.localized)
        self.ui_folder()["children"].append(self.bindings["GameEventComponent"])
        self.assert_invalid("UI folder must contain exactly the three data components")

    def test_event_component_cannot_be_attached_as_a_command(self):
        self.element("EventEntryElement")["components"] = [self.bindings["GameEventComponent"]]
        self.assert_invalid("game_event components must remain standalone data")

    def test_event_inputs_require_their_exact_bound_attributes(self):
        self.project["components"][self.bindings["GameEventComponent"]]["attributes"].pop()
        self.assert_invalid("game_event component must contain exactly its two bound input attributes")

    def test_event_component_cannot_add_a_third_input(self):
        extra = "d9880807-af60-4c29-9456-8c64d833aa0e"
        self.project["attributes"][extra] = dict(
            self.project["attributes"][self.bindings["EventTypeAttribute"]], customId="cell_id")
        self.project["components"][self.bindings["GameEventComponent"]]["attributes"].append(extra)
        self.assert_invalid("game_event component must contain exactly its two bound input attributes")

    def test_event_input_names_and_ownership_are_part_of_the_contract(self):
        for binding in SYNC.EVENT_INPUTS:
            original = copy.deepcopy(self.project["attributes"][self.bindings[binding]])
            for key, value in (("customId", "other_input"), ("cType", "boards"), ("cId", self.bindings["HUDTextComponent"])):
                with self.subTest(binding=binding, key=key):
                    self.project["attributes"][self.bindings[binding]] = dict(original, **{key: value})
                    self.assert_invalid("input must keep its bound owner, type, and empty/false default")
            self.project["attributes"][self.bindings[binding]] = original

    def test_event_type_input_requires_an_empty_plain_string(self):
        attribute = self.project["attributes"][self.bindings["EventTypeAttribute"]]
        for value in (
            {"type": "string", "data": "start", "plain": True},
            {"type": "string", "data": "", "plain": False},
            {"type": "string", "data": False, "plain": True},
            {"type": "boolean", "data": False},
        ):
            with self.subTest(value=value):
                attribute["value"] = value
                self.assert_invalid("game_event.type input must keep its bound owner, type, and empty/false default")

    def test_exported_null_event_type_imports_as_an_empty_string(self):
        attribute = self.project["attributes"][self.bindings["EventTypeAttribute"]]
        for data in (None, ""):
            with self.subTest(data=data):
                attribute["value"]["data"] = data
                self.validate()

    def test_event_inputs_require_data_to_be_imported_by_the_plugin(self):
        for binding in SYNC.EVENT_INPUTS:
            with self.subTest(binding=binding):
                value = self.project["attributes"][self.bindings[binding]]["value"]
                original = value.pop("data")
                self.assert_invalid("input must keep its bound owner, type, and empty/false default")
                value["data"] = original

    def test_collected_input_requires_a_false_boolean(self):
        attribute = self.project["attributes"][self.bindings["CellAlreadyCollectedAttribute"]]
        for value in (
            {"type": "boolean", "data": True},
            {"type": "boolean", "data": 0},
            {"type": "string", "data": "false", "plain": True},
        ):
            with self.subTest(value=value):
                attribute["value"] = value
                self.assert_invalid("game_event.cell_already_collected input must keep its bound owner, type, and empty/false default")

    def test_missing_bound_branch_is_rejected(self):
        del self.project["branches"][self.bindings["TerminalBranch"]]
        self.assert_invalid("missing the TerminalBranch binding")

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
        self.assert_invalid("Only the nineteen UI strings and two game_event inputs may add scoped variables")

    def test_additional_board_variable_is_rejected(self):
        extra = "5aa30329-45b8-42df-b65b-1e94f0a76a84"
        owner = self.bindings["Board"]
        self.project["attributes"][extra] = dict(self.ui_attribute(), cType="boards", cId=owner, customId="debug_label")
        self.project["boards"][owner]["attributes"] = [extra]
        self.assert_invalid("Only the nineteen UI strings and two game_event inputs may add scoped variables")

    def test_ui_component_cannot_be_attached_as_a_command(self):
        for binding in SYNC.UI_COMPONENTS:
            with self.subTest(component=binding):
                self.element("EventEntryElement")["components"] = [self.bindings[binding]]
                self.assert_invalid("UI and game_event components must remain standalone data")

    def test_presentation_can_show_known_ui_field(self):
        self.script("PresentationCollectingElement",
                    "show(hud.station_name, world_text.sign_exit, quest_ui.mission_heading, powerCells)")
        self.validate()

    def test_presentation_can_read_event_inputs(self):
        self.script("PresentationCollectingElement", "show(game_event.type, game_event.cell_already_collected)")
        self.validate()

    def test_presentation_cannot_assign_event_inputs(self):
        for script in ('game_event.type = "use_terminal"', "game_event.cell_already_collected = false"):
            with self.subTest(script=script):
                self.script("PresentationCollectingElement", script)
                self.assert_invalid("Presentation may only assign quest_ui fields")

    def test_presentation_cannot_read_an_unknown_event_input(self):
        self.script("PresentationCollectingElement", "show(game_event.cell_id)")
        self.assert_invalid("Presentation may only assign quest_ui fields")

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
        for target in (
            "questStarted", "hud.station_name", "world_text.sign_exit", "quest_ui.unknown",
            "game_event.type", "game_event.cell_already_collected",
        ):
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
        outputs = self.element("EventEntryElement")["outputs"]
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
        self.generator_connection().update(targetid=self.bindings["GeneratorBranch"], targetType="branches")
        self.assert_invalid("automatic event path contains a cycle")

    def test_automatic_cross_event_branch_is_rejected(self):
        self.generator_connection().update(targetid=self.bindings["ExitBranch"], targetType="branches")
        self.assert_invalid("world-event lanes must not execute one another")

    def test_unconnected_world_event_element_is_rejected(self):
        ident = "6529841b-dc21-43f6-b73d-85e49e5663d5"
        self.project["elements"][ident] = {"content": "<p>Unused arrival message.</p>"}
        self.project["boards"][self.bindings["Board"]]["elements"].append(ident)
        self.assert_invalid("Every world-event node must be reachable from the shared event entry")

    def test_collection_requires_pickup_event_context(self):
        self.generator_connection().update(targetid=self.bindings["PickupActionElement"], targetType="elements")
        self.assert_invalid("Only the pickup event supplies the physical identity")

    def test_changed_command_custom_id_is_rejected(self):
        self.project["components"][self.bindings["CollectCellComponent"]]["customId"] = "collect_other"
        self.assert_invalid("custom ID no longer matches")

    def test_collection_command_on_terminal_is_rejected(self):
        self.element("StartElement")["components"] = [self.bindings["CollectCellComponent"]]
        self.assert_invalid("Only PickupAction")

    def test_power_command_on_shared_event_entry_is_rejected(self):
        self.element("EventEntryElement")["components"] = [self.bindings["RestorePowerComponent"]]
        self.assert_invalid("only Success may restore power")

    def test_gate_command_on_exit_is_rejected(self):
        self.element("CompletedElement")["components"] = [self.bindings["OpenGateComponent"]]
        self.assert_invalid("only Success may restore power")

    def test_missing_pickup_command_is_rejected(self):
        self.element("PickupActionElement")["components"] = []
        self.assert_invalid("Only PickupAction")

    def test_empty_executable_entry_is_rejected(self):
        self.element("EventEntryElement")["content"] = None
        self.assert_invalid("Executable elements need nonempty content")

    def test_empty_executable_html_is_rejected(self):
        self.element("PickupActionElement")["content"] = "<p> </p>"
        self.assert_invalid("Executable elements need nonempty content")

    def test_empty_localized_executable_content_is_rejected(self):
        self.project = copy.deepcopy(self.localized)
        self.localized_content("EventEntryElement")["text"] = None
        self.assert_invalid("Executable elements need nonempty content")

    def test_localized_display_assignment_is_rejected(self):
        self.project = copy.deepcopy(self.localized)
        self.localized_content("PresentationCollectingElement")["text"] = "<pre><code>powerCells = 99</code></pre>"
        self.assert_invalid("Presentation may only assign quest_ui fields")


if __name__ == "__main__":
    unittest.main()
