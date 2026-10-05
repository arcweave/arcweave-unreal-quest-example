#!/usr/bin/env python3
"""Offline regression checks: python3 Scripts/test-sync-narrative.py."""

import copy
import hashlib
import importlib.util
import io
import json
import shutil
import tempfile
import urllib.error
from contextlib import redirect_stdout
from html import escape
import unittest
from unittest.mock import patch
from pathlib import Path
from uuid import uuid4


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
        cls.localized = json.loads((ROOT / "Narrative/import.json").read_text(encoding="utf-8"))

    def setUp(self):
        self.project = copy.deepcopy(self.unreal)

    def validate(self):
        SYNC.validate_bindings(self.project, self.bindings)

    def assert_invalid(self, message):
        with self.assertRaisesRegex(ValueError, message):
            self.validate()

    def element_id(self, binding):
        if binding == "EventEntryElement":
            return self.project["startingElement"]
        if binding in {"PresentationEntryElement", "InventoryEntryElement"}:
            marker = "objectives_ui" if binding == "PresentationEntryElement" else "inventory"
            board = self.project["boards"][self.bindings["Board"]]
            return next(ident for ident in board["elements"]
                        if any(self.project["attributes"][key].get("name") == "entry_point"
                               and self.project["attributes"][key].get("value", {}).get("data") == marker
                               for key in self.project["elements"][ident].get("attributes", []) or []))
        return self.bindings[binding]

    def presentation_marker(self):
        return next(self.project["attributes"][key]
                    for key in self.element("PresentationEntryElement")["attributes"]
                    if self.project["attributes"][key].get("name") == "entry_point")

    def element(self, binding):
        return self.project["elements"][self.element_id(binding)]

    def state_attribute(self, binding):
        return self.project["attributes"][self.bindings[binding]]

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
            "sourceid": self.element_id(source_binding), "sourceType": "elements",
            "targetid": self.element_id(target_binding), "targetType": "elements",
        }
        for previous in self.element(source_binding).get("outputs", []) or []:
            del self.project["connections"][previous]
            self.project["boards"][self.bindings["Board"]]["connections"].remove(previous)
        self.element(source_binding)["outputs"] = [ident]
        self.project["boards"][self.bindings["Board"]]["connections"].append(ident)

    def localized_content(self, binding):
        locale = next(item["iso"] for item in self.project["locales"] if item["base"] is None)
        return self.project["contents"][self.element_id(binding)]["content"][locale]

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

    def return_connection(self, binding="PresentationReadyElement"):
        return self.project["connections"][self.element(binding)["outputs"][0]]

    def return_jumper(self, binding="PresentationReadyElement"):
        return self.project["jumpers"][self.return_connection(binding)["targetid"]]

    def menu_connection(self, event="use_terminal", cell_id=""):
        return next(self.project["connections"][ident]
                    for ident in self.element("EventEntryElement")["outputs"]
                    if self.project["connections"][ident]["targetType"] == "branches"
                    and SYNC.validate_menu_label(SYNC.localized_field(
                        self.project, "connections", ident, "label")) == (event, cell_id))

    def query_connection(self, binding="InventoryEntryElement"):
        destination = self.element_id(binding)
        return next(self.project["connections"][ident]
                    for ident in self.element("EventEntryElement")["outputs"]
                    if self.project["connections"][ident]["targetType"] == "jumpers"
                    and self.project["jumpers"][self.project["connections"][ident]["targetid"]]["elementId"] == destination)

    def menu_label(self, event="use_terminal", cell_id=""):
        return ("<p>Use this interaction</p>" + self.code_block(f'game_event.type = "{event}"')
                + self.code_block(f'game_event.cell_id = "{cell_id}"'))

    def ui_folder(self):
        ui_ids = {self.bindings[name] for name in SYNC.UI_COMPONENTS}
        return next(item for item in self.project["components"].values()
                    if set(item.get("children", [])) == ui_ids)

    def state_folder(self):
        state_ids = {self.bindings[name] for name in SYNC.STATE_COMPONENTS}
        return next(item for item in self.project["components"].values()
                    if set(item.get("children", [])) == state_ids)

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
        self.state_attribute("RequiredPowerCellsAttribute")["value"]["data"] = 1
        self.validate()

    def test_state_display_names_can_change_without_affecting_scopes(self):
        self.project["components"][self.bindings["PlayerComponent"]]["name"] = "Station visitor"
        self.project["components"][self.bindings["QuestStateComponent"]]["name"] = "Power restoration"
        self.state_attribute("PowerCellsAttribute")["name"] = "Collected cells"
        self.validate()

    def test_state_components_require_their_scopes(self):
        for binding in SYNC.STATE_COMPONENTS:
            with self.subTest(binding=binding):
                component = self.project["components"][self.bindings[binding]]
                original = component["customId"]
                component["customId"] = "other_state"
                self.assert_invalid("data component must have its required custom ID")
                component["customId"] = original

    def test_state_components_require_their_exact_bound_attributes(self):
        for binding in SYNC.STATE_COMPONENTS:
            with self.subTest(binding=binding):
                attributes = self.project["components"][self.bindings[binding]]["attributes"]
                removed = attributes.pop()
                self.assert_invalid("component must contain exactly its bound state attributes")
                attributes.append(removed)

    def test_state_attributes_require_their_names_owners_and_types(self):
        for _, fields in SYNC.STATE_COMPONENTS.values():
            for binding in fields:
                original = copy.deepcopy(self.state_attribute(binding))
                for key, value in (
                    ("customId", "other_state"), ("cType", "boards"),
                    ("cId", self.bindings["GameEventComponent"]),
                    ("value", {"type": "string", "data": "false", "plain": True}),
                ):
                    with self.subTest(binding=binding, key=key):
                        self.project["attributes"][self.bindings[binding]] = dict(original, **{key: value})
                        self.assert_invalid("state attribute must keep its bound owner, name, and type")
                self.project["attributes"][self.bindings[binding]] = original

    def test_state_attributes_require_importable_typed_data(self):
        for _, fields in SYNC.STATE_COMPONENTS.values():
            for binding, (_, kind, _) in fields.items():
                value = self.state_attribute(binding)["value"]
                original = value["data"]
                for invalid in (None, "0", 0 if kind == "boolean" else False, 0.0):
                    with self.subTest(binding=binding, data=invalid):
                        value["data"] = invalid
                        self.assert_invalid("initial value has the wrong type")
                del value["data"]
                self.assert_invalid("initial value has the wrong type")
                value["data"] = original

    def test_state_cannot_start_with_inventory_power_or_completion(self):
        for binding, invalid in (
            ("PowerCellsAttribute", 1), ("PowerRestoredAttribute", True),
            ("QuestCompletedAttribute", True),
        ):
            with self.subTest(binding=binding):
                value = self.state_attribute(binding)["value"]
                original = value["data"]
                value["data"] = invalid
                self.assert_invalid("not a valid new-game default")
                value["data"] = original

    def test_state_scope_cannot_be_duplicated(self):
        for scope, _ in SYNC.STATE_COMPONENTS.values():
            with self.subTest(scope=scope):
                self.project["components"][self.bindings["OpenGateComponent"]]["customId"] = scope
                self.assert_invalid("data component scopes must be unique")

    def test_state_components_are_data_not_referenced_commands(self):
        for binding in SYNC.STATE_COMPONENTS:
            with self.subTest(binding=binding):
                self.element("TerminalAcceptElement")["components"] = [self.bindings[binding]]
                self.assert_invalid("State, UI, and game_event components must remain standalone data")

    def test_restore_power_command_cannot_duplicate_quest_state(self):
        self.project["components"]["legacy-restore-power"] = {"customId": "restore_power"}
        self.assert_invalid("exactly the collect_cell and open_gate action components")

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
        self.project["startingElement"] = self.bindings["TerminalAcceptElement"]
        with self.assertRaises(ValueError):
            self.validate()

    def test_event_entry_can_be_rekeyed_without_changing_bindings(self):
        self.assertNotIn("EventEntryElement", self.bindings)
        for name, project in (("unreal", self.unreal), ("authoring", self.authoring), ("localized", self.localized)):
            with self.subTest(export=name):
                original = project["startingElement"]
                replacement = str(uuid4())
                # Rekey the node and its references, including localized content.
                self.project = json.loads(json.dumps(project).replace(original, replacement))
                self.assertNotIn(original, self.project["elements"])
                self.assertEqual(self.element_id("EventEntryElement"), replacement)
                self.validate()

    def test_missing_starting_element_is_rejected(self):
        del self.project["startingElement"]
        self.assert_invalid("starting element must identify an element on the world-event board")

    def test_null_starting_element_is_rejected(self):
        self.project["startingElement"] = None
        self.assert_invalid("starting element must identify an element on the world-event board")

    def test_empty_starting_element_is_rejected(self):
        self.project["startingElement"] = ""
        self.assert_invalid("starting element must identify an element on the world-event board")

    def test_unknown_starting_element_is_rejected(self):
        self.project["startingElement"] = str(uuid4())
        self.assert_invalid("starting element must identify an element on the world-event board")

    def test_starting_element_cannot_be_a_branch(self):
        self.project["startingElement"] = self.bindings["EventRouterBranch"]
        self.assert_invalid("starting element must identify an element on the world-event board")

    def test_starting_element_must_belong_to_world_board(self):
        entry = self.project["startingElement"]
        self.project["boards"][self.bindings["Board"]]["elements"].remove(entry)
        self.assert_invalid("starting element must identify an element on the world-event board")

    def test_presentation_entry_and_marker_can_be_rekeyed_without_changing_bindings(self):
        self.assertNotIn("PresentationEntryElement", self.bindings)
        for name, project in (("unreal", self.unreal), ("authoring", self.authoring), ("localized", self.localized)):
            with self.subTest(export=name):
                self.project = copy.deepcopy(project)
                entry = self.element_id("PresentationEntryElement")
                marker = self.element("PresentationEntryElement")["attributes"][0]
                replacement = str(uuid4())
                self.project = json.loads(json.dumps(project).replace(entry, replacement).replace(marker, str(uuid4())))
                self.assertNotIn(entry, self.project["elements"])
                self.assertNotIn(marker, self.project["attributes"])
                self.assertEqual(self.element_id("PresentationEntryElement"), replacement)
                self.validate()

    def test_shared_board_display_name_and_custom_id_can_change(self):
        board = self.project["boards"][self.bindings["Board"]]
        board.update(name="Restore station power", customId="station_quest")
        self.validate()

    def test_extra_executable_board_is_rejected(self):
        self.project["boards"][str(uuid4())] = {"customId": "quest_presentation", "elements": []}
        self.assert_invalid("must use one shared board")

    def test_presentation_entry_requires_its_marker_reference(self):
        self.element("PresentationEntryElement")["attributes"] = []
        self.assert_invalid("must contain exactly one objectives_ui entry marker")

    def test_presentation_entry_requires_its_marker_attribute(self):
        marker = self.element("PresentationEntryElement")["attributes"][0]
        del self.project["attributes"][marker]
        self.assert_invalid("must contain exactly one objectives_ui entry marker")

    def test_presentation_entry_marker_name_is_required(self):
        marker = self.presentation_marker()
        for name in (None, "", "other_entry"):
            with self.subTest(name=name):
                marker["name"] = name
                self.assert_invalid("must contain exactly one objectives_ui entry marker")

    def test_presentation_entry_marker_must_be_unique(self):
        for binding in ("PresentationEntryElement", "PresentationReadyElement"):
            with self.subTest(element=binding):
                self.project = copy.deepcopy(self.unreal)
                target = self.element_id(binding)
                marker_id = str(uuid4())
                self.project["attributes"][marker_id] = dict(copy.deepcopy(self.presentation_marker()), cId=target)
                self.project["elements"][target].setdefault("attributes", []).append(marker_id)
                self.assert_invalid("must contain exactly one objectives_ui entry marker")

    def test_presentation_marker_requires_plain_string_objectives_ui(self):
        marker = self.presentation_marker()
        for value in (
            {"type": "string", "plain": False, "data": "objectives_ui"},
            {"type": "string", "data": "objectives_ui"},
            {"type": "integer", "plain": True, "data": 1},
            {"type": "string", "plain": True, "data": None},
            {"type": "string", "plain": True, "data": ""},
            {"type": "string", "plain": True, "data": "other_entry"},
            {"type": "string", "plain": True},
        ):
            with self.subTest(value=value):
                marker["value"] = value
                self.assert_invalid("exactly one objectives_ui entry marker|objectives_ui entry marker must be a plain-string element attribute owned by its entry")

    def test_presentation_marker_requires_its_element_owner(self):
        for owner in (
            {"cType": "elements", "cId": self.bindings["PresentationReadyElement"]},
            {"cType": "elements", "cId": str(uuid4())},
            {"cType": "components", "cId": self.bindings["QuestUIComponent"]},
        ):
            with self.subTest(owner=owner):
                self.presentation_marker().update(owner)
                self.assert_invalid("objectives_ui entry marker must be a plain-string element attribute owned by its entry")

    def test_presentation_marker_cannot_move_to_interaction_menu(self):
        marker_id = self.element("PresentationEntryElement")["attributes"].pop()
        self.element("EventEntryElement")["attributes"] = [marker_id]
        self.project["attributes"][marker_id]["cId"] = self.project["startingElement"]
        self.assert_invalid("objectives_ui entry must be distinct from the interaction menu")

    def test_custom_id_cannot_replace_presentation_marker_name(self):
        self.presentation_marker().update(name="Objectives refresh entry", customId="entry_point")
        self.assert_invalid("must contain exactly one objectives_ui entry marker")

    def test_presentation_entry_cannot_add_display_metadata_beside_marker(self):
        entry = self.element_id("PresentationEntryElement")
        attribute = str(uuid4())
        self.project["attributes"][attribute] = {
            "cType": "elements", "cId": entry, "customId": "objective_title",
            "value": {"type": "string", "plain": True, "data": "Legacy objective"},
        }
        self.element("PresentationEntryElement")["attributes"].append(attribute)
        self.assert_invalid("Presentation elements must not retain metadata besides the entry marker")

    def test_event_entry_cannot_bypass_router(self):
        self.menu_connection().update(targetid=self.bindings["TerminalBranch"], targetType="branches")
        self.assert_invalid("shared world-event entry must connect directly to its routing branch")

    def test_menu_choices_can_be_reordered(self):
        self.element("EventEntryElement")["outputs"].reverse()
        self.validate()

    def test_inventory_entry_and_marker_need_no_uuid_bindings(self):
        self.assertNotIn("InventoryEntryElement", self.bindings)
        for project in (self.unreal, self.authoring, self.localized):
            with self.subTest(localized="contents" in project):
                self.project = copy.deepcopy(project)
                entry = self.element_id("InventoryEntryElement")
                marker = self.element("InventoryEntryElement")["attributes"][0]
                self.assertNotIn(entry, self.bindings.values())
                self.assertNotIn(marker, self.bindings.values())
                replacement = str(uuid4())
                self.project = json.loads(json.dumps(project).replace(entry, replacement).replace(marker, str(uuid4())))
                self.assertEqual(self.element_id("InventoryEntryElement"), replacement)
                self.validate()

    def test_inventory_requires_its_entry_marker(self):
        self.element("InventoryEntryElement")["attributes"] = []
        self.assert_invalid("must contain exactly one inventory entry marker")

    def test_inventory_marker_must_be_unique(self):
        entry = self.element_id("InventoryEntryElement")
        marker = self.element("InventoryEntryElement")["attributes"][0]
        duplicate = str(uuid4())
        self.project["attributes"][duplicate] = dict(self.project["attributes"][marker])
        self.project["elements"][entry]["attributes"].append(duplicate)
        self.assert_invalid("must contain exactly one inventory entry marker")

    def test_inventory_marker_requires_a_plain_string_owned_by_its_entry(self):
        for change in ({"cType": "components"}, {"cId": self.bindings["PlayerComponent"]},
                       {"value": {"type": "string", "data": "inventory", "plain": False}}):
            with self.subTest(change=change):
                self.project = copy.deepcopy(self.unreal)
                marker = self.element("InventoryEntryElement")["attributes"][0]
                self.project["attributes"][marker].update(change)
                self.assert_invalid("inventory entry marker must be a plain-string element attribute owned by its entry")

    def test_inventory_marker_cannot_move_to_station_menu(self):
        marker = self.element("InventoryEntryElement")["attributes"].pop()
        self.element("EventEntryElement")["attributes"] = [marker]
        self.project["attributes"][marker]["cId"] = self.project["startingElement"]
        self.assert_invalid("inventory entry must be distinct from the interaction menu")

    def test_query_entries_cannot_share_one_element(self):
        marker = self.element("InventoryEntryElement")["attributes"].pop()
        self.element("PresentationEntryElement")["attributes"].append(marker)
        self.project["attributes"][marker]["cId"] = self.element_id("PresentationEntryElement")
        self.assert_invalid("inventory entry must be distinct from the interaction menu and other queries")

    def test_extra_entry_marker_is_rejected(self):
        marker = str(uuid4())
        self.project["attributes"][marker] = {
            "name": "entry_point", "cType": "elements", "cId": self.project["startingElement"],
            "value": {"type": "string", "plain": True, "data": "other_query"},
        }
        self.element("EventEntryElement")["attributes"] = [marker]
        self.assert_invalid("only its objectives_ui and inventory entry markers")

    def test_query_labels_can_change_without_changing_routing(self):
        self.query_connection()["label"] = "<p>Look in your backpack</p>"
        self.query_connection("PresentationEntryElement")["label"] = "<p>Review the task</p>"
        self.validate()

    def test_query_choices_require_visible_text(self):
        self.query_connection()["label"] = self.code_block("show(player.power_cells)")
        self.assert_invalid("Each query menu label must include visible choice text")

    def test_query_choices_cannot_change_state_or_event_inputs(self):
        for binding in ("InventoryEntryElement", "PresentationEntryElement"):
            for script in ('game_event.type = "collect_cell"', 'game_event.cell_id = "cell_b"',
                           "player.power_cells += 1", "quest.started = true", "cell_a.collected = true",
                           'quest_ui.mission_heading = "Changed"', "resetAll()"):
                with self.subTest(query=binding, script=script):
                    self.project = copy.deepcopy(self.unreal)
                    self.query_connection(binding)["label"] += self.code_block(script)
                    self.assert_invalid("Query menu labels must be feedback only")

    def test_query_choices_must_use_jumpers(self):
        connection = self.query_connection()
        jumper = connection["targetid"]
        destination = self.project["jumpers"].pop(jumper)["elementId"]
        self.project["boards"][self.bindings["Board"]]["jumpers"].remove(jumper)
        connection.update(targetid=destination, targetType="elements")
        self.assert_invalid("plus two query jumpers")

    def test_menu_requires_both_query_choices(self):
        connection = self.query_connection()
        output = next(ident for ident, item in self.project["connections"].items() if item is connection)
        jumper = connection["targetid"]
        self.element("EventEntryElement")["outputs"].remove(output)
        del self.project["connections"][output]
        del self.project["jumpers"][jumper]
        board = self.project["boards"][self.bindings["Board"]]
        board["connections"].remove(output)
        board["jumpers"].remove(jumper)
        self.assert_invalid("plus two query jumpers")

    def test_query_menu_cannot_offer_the_same_query_twice(self):
        jumper = self.query_connection()["targetid"]
        self.project["jumpers"][jumper]["elementId"] = self.element_id("PresentationEntryElement")
        self.assert_invalid("inventory and objectives exactly once")

    def test_query_menu_cannot_dispatch_gameplay(self):
        jumper = self.query_connection()["targetid"]
        self.project["jumpers"][jumper]["elementId"] = self.bindings["PickupActionElement"]
        self.assert_invalid("Menu query jumpers must target the inventory or objectives_ui entry")

    def test_inventory_content_can_read_the_shared_count(self):
        self.script("InventoryEntryElement", 'show(hud.cells_label, ": ", player.power_cells)')
        self.validate()

    def test_inventory_cannot_change_state_or_inputs(self):
        for script in ("player.power_cells += 1", "quest.started = true", "cell_a.collected = true",
                       'game_event.type = "enter_exit"', 'game_event.cell_id = "cell_a"',
                       'quest_ui.mission_heading = "Changed"', "resetAll()", "show(random(10))"):
            with self.subTest(script=script):
                self.project = copy.deepcopy(self.unreal)
                self.script("InventoryEntryElement", script)
                self.assert_invalid("Inventory query must be feedback only")

    def test_localized_inventory_cannot_change_state(self):
        self.project = copy.deepcopy(self.localized)
        self.localized_content("InventoryEntryElement")["text"] = self.code_block("player.power_cells = 99")
        self.assert_invalid("Inventory query must be feedback only")

    def test_inventory_cannot_call_a_physical_command(self):
        self.element("InventoryEntryElement")["components"] = [self.bindings["CollectCellComponent"]]
        self.assert_invalid("Only PickupAction may collect a cell")

    def test_inventory_must_return_to_station(self):
        self.return_jumper("InventoryEntryElement")["elementId"] = self.element_id("PresentationEntryElement")
        self.assert_invalid("Every return jumper must target the interaction menu")

    def test_inventory_requires_its_own_return_jumper(self):
        self.return_connection("InventoryEntryElement")["targetid"] = self.return_connection()["targetid"]
        self.assert_invalid("Each return jumper must be used only by its own")

    def test_inventory_cannot_enter_world_gameplay(self):
        self.return_connection("InventoryEntryElement").update(
            targetid=self.bindings["PickupActionElement"], targetType="elements")
        self.assert_invalid("inventory query must use its own return jumper")

    def test_inventory_cannot_define_extra_metadata(self):
        self.element("InventoryEntryElement")["attributes"].append("legacy_inventory_field")
        self.assert_invalid("inventory query must retain only its inventory entry marker")

    def test_inventory_requires_executable_feedback(self):
        self.element("InventoryEntryElement")["content"] = "<p> </p>"
        self.assert_invalid("Executable elements need nonempty content")

    def test_world_events_cannot_enter_optional_inventory(self):
        self.return_connection("TerminalAcceptedElement").update(
            targetid=self.element_id("InventoryEntryElement"), targetType="elements")
        self.assert_invalid("World events must return to the menu without entering the optional query flows")

    def test_exit_completion_ends_the_playthrough(self):
        for binding in SYNC.EXIT_ENDINGS:
            self.assertFalse(self.element(binding).get("outputs"))
        self.validate()

    def test_completed_exit_cannot_continue_to_the_menu(self):
        self.add_output("CompletedElement", "EventEntryElement")
        self.assert_invalid("Only the completed exit outcomes may end a playthrough")

    def test_repeated_exit_cannot_continue_to_the_menu(self):
        self.add_output("ExitAlreadyCompletedElement", "EventEntryElement")
        self.assert_invalid("Only the completed exit outcomes may end a playthrough")

    def test_menu_input_assignments_can_be_reordered(self):
        self.menu_connection()["label"] = ("<p>Consult the station terminal</p>"
            + self.code_block("game_event.cell_id = ''")
            + self.code_block("game_event.type = 'use_terminal'"))
        self.validate()

    def test_menu_requires_all_five_interaction_choices(self):
        removed = next(ident for ident in self.element("EventEntryElement")["outputs"]
                       if self.project["connections"][ident]["targetType"] == "branches")
        self.element("EventEntryElement")["outputs"].remove(removed)
        del self.project["connections"][removed]
        self.project["boards"][self.bindings["Board"]]["connections"].remove(removed)
        self.assert_invalid("routing branch with five menu choices")

    def test_menu_cannot_repeat_one_event_pair_instead_of_another(self):
        self.menu_connection("collect_cell", "cell_b")["label"] = self.menu_label("collect_cell", "cell_a")
        self.assert_invalid("both cells, generator, and exit exactly once")

    def test_menu_cannot_leave_stale_inputs_from_the_previous_choice(self):
        for script in ('game_event.type = "use_terminal"', 'game_event.cell_id = ""'):
            with self.subTest(script=script):
                self.project = copy.deepcopy(self.unreal)
                self.menu_connection()["label"] = "<p>Use terminal</p>" + self.code_block(script)
                self.assert_invalid("complete supported event type and cell identity pair")

    def test_menu_requires_a_supported_event_and_matching_identity(self):
        for event, cell_id in (("collect_cell", ""), ("collect_cell", "cell_c"),
                               ("use_terminal", "cell_a"), ("enter_exit", "cell_b"),
                               ("unrecognized", "")):
            with self.subTest(event=event, cell_id=cell_id):
                self.project = copy.deepcopy(self.unreal)
                self.menu_connection()["label"] = self.menu_label(event, cell_id)
                self.assert_invalid("complete supported event type and cell identity pair")

    def test_menu_cannot_change_shared_state_or_call_commands(self):
        for script in ("quest.started = true", "player.power_cells += 1", "cell_a.collected = true",
                       "resetAll()", 'game_event.unknown = ""', 'game_event.type = hud.station_name'):
            with self.subTest(script=script):
                self.project = copy.deepcopy(self.unreal)
                self.menu_connection()["label"] += self.code_block(script)
                self.assert_invalid("set each game_event input exactly once with a literal string")

    def test_menu_cannot_assign_an_input_twice(self):
        self.menu_connection()["label"] += self.code_block('game_event.cell_id = "cell_a"')
        self.assert_invalid("set each game_event input exactly once with a literal string")

    def test_menu_requires_visible_choice_text(self):
        self.menu_connection()["label"] = (self.code_block('game_event.type = "use_terminal"')
            + self.code_block('game_event.cell_id = ""'))
        self.assert_invalid("include visible action text")

    def test_menu_statement_blocks_follow_the_native_plugin_format(self):
        for separator in ("\n", "; "):
            with self.subTest(separator=separator):
                self.project = copy.deepcopy(self.unreal)
                self.menu_connection()["label"] = "<p>Use terminal</p>" + self.code_block(separator.join((
                    'game_event.type = "use_terminal"', 'game_event.cell_id = ""')))
                self.assert_invalid("Each menu label code block must contain exactly one statement")

    def test_all_locales_menu_input_assignments_are_validated(self):
        self.project = copy.deepcopy(self.localized)
        ident = next(ident for ident in self.element("EventEntryElement")["outputs"]
                     if self.project["connections"][ident]["targetType"] == "branches")
        locale = next(item["iso"] for item in self.project["locales"] if item["base"] is None)
        self.project["contents"][ident]["label"][locale]["text"] = self.menu_label("collect_cell", "unknown")
        self.assert_invalid("complete supported event type and cell identity pair")

    def test_automatic_labels_cannot_hide_progression_skipped_by_unreal(self):
        for script in ('quest.started = true', 'game_event.type = "enter_exit"', "cell_b.collected = true",
                       "player.power_cells += 1", "resetAll()"):
            with self.subTest(script=script):
                self.generator_connection()["label"] = self.code_block(script)
                self.assert_invalid("Automatic connection labels must be feedback only")

    def test_boundary_labels_can_display_known_shared_values(self):
        ident = self.element("TerminalAcceptedElement")["outputs"][0]
        self.project["connections"][ident]["label"] = self.code_block('show(player.power_cells, quest.required_power_cells)')
        self.validate()

    def test_boundary_labels_can_offer_plain_continue_text(self):
        for binding in ("TerminalAcceptedElement", "PresentationReadyElement"):
            ident = self.element(binding)["outputs"][0]
            self.project["connections"][ident]["label"] = "<p>Continue</p>"
        self.validate()

    def test_condition_output_labels_cannot_override_the_selected_action(self):
        for binding in ("EventRouterBranch", "PickupBranch", "GeneratorBranch", "PresentationBranch"):
            for content in ("<p>Generic route label</p>", self.code_block("show(player.power_cells)")):
                with self.subTest(binding=binding, content=content):
                    self.project = copy.deepcopy(self.unreal)
                    self.route_connection(binding, 0)["label"] = content
                    self.assert_invalid("Condition-output labels must stay empty")

    def test_localized_condition_labels_cannot_override_the_selected_action(self):
        self.project = copy.deepcopy(self.localized)
        ident = self.conditions("PickupBranch")[0]["output"]
        locale = next(item["iso"] for item in self.project["locales"] if item["base"] is None)
        self.project["contents"][ident]["label"][locale]["text"] = "<p>Already collected</p>"
        self.assert_invalid("Condition-output labels must stay empty")

    def test_localized_automatic_labels_cannot_change_state(self):
        self.project = copy.deepcopy(self.localized)
        ident = self.conditions("GeneratorBranch")[0]["output"]
        locale = next(item["iso"] for item in self.project["locales"] if item["base"] is None)
        self.project["contents"][ident]["label"][locale]["text"] = self.code_block("quest.started = true")
        self.assert_invalid("Automatic connection labels must be feedback only")

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

    def test_pickup_requires_shared_collected_state_for_the_supplied_identity(self):
        self.conditions("PickupBranch")[0]["script"] = "player.power_cells > 0"
        self.assert_invalid("pickup branch must check duplicate identity first")

    def test_pickup_acceptance_check_cannot_be_inverted(self):
        self.conditions("PickupBranch")[1]["script"] = "quest.started"
        self.assert_invalid("pickup branch must check duplicate identity first")

    def test_duplicate_feedback_cannot_fall_through_to_collection(self):
        self.add_output("DuplicatePickupElement", "PickupActionElement")
        self.assert_invalid("Duplicate-pickup feedback must continue directly")

    def test_task_required_feedback_cannot_fall_through_to_collection(self):
        self.add_output("PickupTerminalRequiredElement", "PickupActionElement")
        self.assert_invalid("Task-required pickup feedback must continue directly")

    def test_shared_entry_can_show_current_input_without_changing_it(self):
        self.script("EventEntryElement", "show(game_event.type)")
        self.validate()

    def test_duplicate_feedback_cannot_change_quest_or_input_state(self):
        for script in ('quest.started = true', 'game_event.type = "use_terminal"', 'resetAll()', 'show(random(10))'):
            with self.subTest(script=script):
                self.script("DuplicatePickupElement", script)
                self.assert_invalid("Duplicate-pickup feedback must be feedback only")

    def test_duplicate_feedback_cannot_emit_a_world_command(self):
        self.element("DuplicatePickupElement")["components"] = [self.bindings["OpenGateComponent"]]
        self.assert_invalid("only Success may open the gate")

    def test_duplicate_feedback_cannot_enter_another_lane(self):
        self.add_output("DuplicatePickupElement", "MissingCellsElement")
        self.assert_invalid("world-event lanes must not execute one another")

    def test_shared_entry_cannot_accept_the_task_during_routing(self):
        self.script("EventEntryElement", "quest.started = true")
        self.assert_invalid("shared event entry must be feedback only")

    def test_shared_entry_cannot_overwrite_the_cell_identity(self):
        self.script("EventEntryElement", 'game_event.cell_id = "cell_a"')
        self.assert_invalid("shared event entry must be feedback only")

    def test_localized_duplicate_feedback_cannot_change_state(self):
        self.project = copy.deepcopy(self.localized)
        self.localized_content("DuplicatePickupElement")["text"] = self.code_block("quest.started = true")
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

    def test_save_ui_requires_each_control_and_feedback_field(self):
        for field in SYNC.SAVE_FIELDS:
            with self.subTest(field=field):
                self.project = copy.deepcopy(self.unreal)
                component = self.project["components"][self.bindings["SaveUIComponent"]]
                attribute = next(self.project["attributes"][ident] for ident in component["attributes"]
                                 if self.project["attributes"][ident]["customId"] == field)
                attribute["customId"] = "unsupported_message"
                self.assert_invalid("save_ui component must have exactly its required string attributes")

    def test_save_ui_feedback_must_remain_nonempty_plain_text(self):
        for value in ({"type": "string", "plain": True, "data": ""},
                      {"type": "string", "plain": False, "data": "<p>Saved.</p>"},
                      {"type": "string", "plain": True, "data": 'show("Saved")'},
                      {"type": "boolean", "data": True}):
            with self.subTest(value=value):
                self.ui_attribute("SaveUIComponent")["value"] = value
                self.assert_invalid("UI attributes must be nonempty plain strings")

    def test_queries_cannot_overwrite_save_controls_or_feedback(self):
        for field in SYNC.SAVE_FIELDS:
            for binding, error in (("InventoryEntryElement", "Inventory query must be feedback only"),
                                   ("PresentationCollectingElement", "Presentation may only assign quest_ui fields")):
                with self.subTest(field=field, binding=binding):
                    self.project = copy.deepcopy(self.unreal)
                    self.script(binding, f'save_ui.{field} = "Changed"')
                    self.assert_invalid(error)

    def test_queries_can_read_authored_save_text(self):
        for binding in ("InventoryEntryElement", "PresentationCollectingElement"):
            self.script(binding, "show(save_ui.controls, save_ui.saved)")
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
        self.assert_invalid("UI folder must contain exactly its required components")

    def test_state_components_must_stay_together_in_the_state_folder(self):
        self.project = copy.deepcopy(self.localized)
        self.state_folder()["children"].remove(self.bindings["PlayerComponent"])
        self.assert_invalid("State folder must contain exactly its required components")

    def test_input_component_cannot_be_grouped_with_actions(self):
        self.project = copy.deepcopy(self.localized)
        components = self.project["components"]
        inputs = next(item for item in components.values()
                      if item.get("children") == [self.bindings["GameEventComponent"]])
        actions = next(item for item in components.values()
                       if self.bindings["OpenGateComponent"] in item.get("children", []))
        inputs["children"].remove(self.bindings["GameEventComponent"])
        actions["children"].append(self.bindings["GameEventComponent"])
        self.assert_invalid("Inputs folder must contain exactly its required components")

    def test_state_folder_cannot_introduce_a_runtime_scope(self):
        self.project = copy.deepcopy(self.localized)
        self.state_folder()["customId"] = "state"
        self.assert_invalid("State folder is organizational")

    def test_component_groups_are_top_level_siblings(self):
        self.project = copy.deepcopy(self.localized)
        root = next(item for item in self.project["components"].values() if item.get("root"))
        state_id = next(ident for ident, item in self.project["components"].items()
                        if item is self.state_folder())
        root["children"].remove(state_id)
        self.assert_invalid("four top-level component folders")

    def test_ui_scope_cannot_be_duplicated_by_another_component(self):
        self.project["components"][self.bindings["OpenGateComponent"]]["customId"] = "quest_ui"
        self.assert_invalid("State, UI, and game_event data component scopes must be unique")

    def test_event_scope_cannot_be_duplicated_by_another_component(self):
        self.project["components"][self.bindings["OpenGateComponent"]]["customId"] = "game_event"
        self.assert_invalid("State, UI, and game_event data component scopes must be unique")

    def test_event_component_must_keep_its_input_scope(self):
        self.project["components"][self.bindings["GameEventComponent"]]["customId"] = "interaction"
        self.assert_invalid("game_event data component must have its required custom ID")

    def test_event_component_cannot_be_placed_in_the_ui_folder(self):
        self.project = copy.deepcopy(self.localized)
        self.ui_folder()["children"].append(self.bindings["GameEventComponent"])
        self.assert_invalid("UI folder must contain exactly its required components")

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
                    self.assert_invalid("input must keep its bound owner, type, and empty-string default")
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
                self.assert_invalid("game_event.type input must keep its bound owner, type, and empty-string default")

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
                self.assert_invalid("input must keep its bound owner, type, and empty-string default")
                value["data"] = original

    def test_cell_identity_input_requires_an_empty_plain_string(self):
        attribute = self.project["attributes"][self.bindings["CellIdAttribute"]]
        for value in (
            {"type": "boolean", "data": False},
            {"type": "integer", "data": 0},
            {"type": "string", "data": "cell_a", "plain": True},
            {"type": "string", "data": "", "plain": False},
        ):
            with self.subTest(value=value):
                attribute["value"] = value
                self.assert_invalid("game_event.cell_id input must keep its bound owner, type, and empty-string default")

    def test_missing_bound_branch_is_rejected(self):
        del self.project["branches"][self.bindings["TerminalBranch"]]
        self.assert_invalid("missing the TerminalBranch binding")

    def test_display_assignment_is_rejected(self):
        self.element("PresentationCollectingElement")["content"] = "<pre><code>player.power_cells = 99</code></pre>"
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

    def test_completed_display_condition_is_read_only(self):
        branch = self.project["branches"][self.bindings["PresentationBranch"]]
        self.project["conditions"][branch["conditions"]["ifCondition"]]["script"] = "reset(quest_ui.mission_heading)"
        self.assert_invalid("Presentation conditions cannot call functions")

    def test_shared_power_setup_cannot_modify_gameplay(self):
        self.element("PresentationEntryElement")["content"] = ("".join(self.entry_resets())
            + self.code_block("if quest.completed || quest.power_restored")
            + self.code_block("quest.completed = true") + self.code_block("endif"))
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
        self.assert_invalid("Presentation must return to the interaction menu through exactly its five bound display leaves")

    def test_events_cannot_enter_the_optional_objective_flow(self):
        self.generator_connection().update(
            targetid=self.bindings["PresentationReadyElement"], targetType="elements")
        self.assert_invalid("World events must return to the menu without entering the optional query flows")

    def test_ordinary_world_outcomes_must_return_to_the_menu(self):
        element = self.element("TerminalAcceptedElement")
        output = element["outputs"].pop()
        del self.project["connections"][output]
        self.project["boards"][self.bindings["Board"]]["connections"].remove(output)
        self.assert_invalid("Only the completed exit outcomes may end a playthrough")

    def test_world_outcomes_return_to_the_menu_through_local_jumpers(self):
        output = self.element("TerminalAcceptedElement")["outputs"][0]
        self.project["connections"][output].update(targetid=self.element_id("EventEntryElement"), targetType="elements")
        self.assert_invalid("Each world-event lane must share its own return jumper")

    def test_presentation_must_return_to_the_menu(self):
        element = self.element("PresentationReadyElement")
        output = element["outputs"].pop()
        del self.project["connections"][output]
        self.project["boards"][self.bindings["Board"]]["connections"].remove(output)
        self.assert_invalid("five presentation leaves must use its own return jumper")

    def test_presentation_cannot_reenter_its_own_refresh_boundary(self):
        self.return_jumper()["elementId"] = self.element_id("PresentationEntryElement")
        self.assert_invalid("Every return jumper must target the interaction menu")

    def test_presentation_cannot_restart_world_events_with_stale_inputs(self):
        self.return_jumper()["elementId"] = self.bindings["EventRouterBranch"]
        self.assert_invalid("Every return jumper must target the interaction menu")

    def test_presentation_internal_cycle_is_still_rejected(self):
        self.route_connection("PresentationBranch", 0).update(
            targetid=self.bindings["PresentationBranch"], targetType="branches")
        self.assert_invalid("automatic event path contains a cycle before its execution boundary")

    def test_return_jumpers_need_no_uuid_bindings(self):
        for project in (self.unreal, self.authoring, self.localized):
            with self.subTest(localized="contents" in project):
                self.project = copy.deepcopy(project)
                for ident in list(self.project["jumpers"]):
                    self.assertNotIn(ident, self.bindings.values())
                    self.project = json.loads(json.dumps(self.project).replace(ident, str(uuid4())))
                self.validate()

    def test_missing_return_jumper_is_rejected(self):
        del self.project["jumpers"][self.return_connection()["targetid"]]
        self.assert_invalid("boundary jumper must exist on the same board")

    def test_unowned_return_jumper_is_rejected(self):
        self.project["boards"][self.bindings["Board"]]["jumpers"].remove(self.return_connection()["targetid"])
        self.assert_invalid("boundary jumper must exist on the same board")

    def test_return_jumper_requires_its_destination(self):
        del self.return_jumper()["elementId"]
        self.assert_invalid("Every return jumper must target the interaction menu")

    def test_return_jumper_cannot_target_an_unknown_element(self):
        self.return_jumper()["elementId"] = str(uuid4())
        self.assert_invalid("Every return jumper must target the interaction menu")

    def test_return_jumper_cannot_chain_to_another_jumper(self):
        self.return_jumper()["elementId"] = self.return_connection("PresentationCollectingElement")["targetid"]
        self.assert_invalid("Every return jumper must target the interaction menu")

    def test_unused_return_jumper_is_rejected(self):
        ident = str(uuid4())
        self.project["jumpers"][ident] = dict(self.return_jumper())
        self.project["boards"][self.bindings["Board"]]["jumpers"].append(ident)
        self.assert_invalid("no unused jumpers")

    def test_stale_board_jumper_reference_is_rejected(self):
        self.project["boards"][self.bindings["Board"]]["jumpers"].append(str(uuid4()))
        self.assert_invalid("no unused jumpers")

    def test_each_presentation_leaf_uses_its_own_jumper(self):
        self.return_connection()["targetid"] = self.return_connection("PresentationCollectingElement")["targetid"]
        self.assert_invalid("Each return jumper must be used only by its own presentation leaf")

    def test_world_lane_cannot_share_a_presentation_return_jumper(self):
        ident = self.element("TerminalAcceptedElement")["outputs"][0]
        self.project["connections"][ident].update(targetid=self.return_connection()["targetid"], targetType="jumpers")
        self.assert_invalid("separate return jumpers|Each return jumper must be used only by its own")

    def test_condition_cannot_jump_directly_to_menu(self):
        self.generator_connection().update(targetid=self.return_connection()["targetid"], targetType="jumpers")
        self.assert_invalid("Only element outcomes may use boundary jumpers")

    def test_long_return_connection_cannot_replace_local_jumper(self):
        self.return_connection().update(targetid=self.element_id("EventEntryElement"), targetType="elements")
        self.assert_invalid("five presentation leaves must use its own return jumper")

    def test_world_outcomes_share_one_jumper_per_lane(self):
        for bindings in SYNC.EVENT_LANES.values():
            targets = {self.return_connection(binding)["targetid"] for binding in bindings
                       if binding not in SYNC.EXIT_ENDINGS}
            self.assertEqual(len(targets), 1)
            self.assertEqual(self.project["jumpers"][targets.pop()]["elementId"],
                             self.element_id("EventEntryElement"))
        self.validate()

    def test_world_jumper_requires_an_owned_destination(self):
        for change in ("missing", "unowned", "target"):
            with self.subTest(change=change):
                self.project = copy.deepcopy(self.unreal)
                ident = self.return_connection("TerminalAcceptedElement")["targetid"]
                if change == "missing":
                    del self.project["jumpers"][ident]
                elif change == "unowned":
                    self.project["boards"][self.bindings["Board"]]["jumpers"].remove(ident)
                else:
                    self.project["jumpers"][ident]["elementId"] = self.bindings["PickupActionElement"]
                self.assert_invalid("boundary jumper must exist on the same board|World-event jumpers must target")

    def test_world_lanes_cannot_share_the_same_return_jumper(self):
        self.return_connection("TerminalAcceptedElement")["targetid"] = self.return_connection("PickupActionElement")["targetid"]
        self.assert_invalid("Each world-event lane must share its own return jumper")

    def test_world_lane_outcome_cannot_bypass_its_shared_jumper(self):
        self.return_connection("TerminalAcceptedElement").update(
            targetid=self.element_id("EventEntryElement"), targetType="elements")
        self.assert_invalid("Each world-event lane must share its own return jumper")

    def test_world_lane_cannot_split_its_return_across_multiple_jumpers(self):
        ident = str(uuid4())
        original = self.return_connection("TerminalAcceptedElement")["targetid"]
        self.project["jumpers"][ident] = dict(self.project["jumpers"][original])
        self.project["boards"][self.bindings["Board"]]["jumpers"].append(ident)
        self.return_connection("TerminalAcceptedElement")["targetid"] = ident
        self.assert_invalid("Each world-event lane must share its own return jumper")

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
        self.ui_attribute()["cId"] = self.bindings["OpenGateComponent"]
        self.assert_invalid("UI attributes must be nonempty plain strings")

    def test_additional_ui_attribute_is_rejected(self):
        extra = "c916868d-e2ab-4d42-8f92-d4152a29a9e6"
        self.project["attributes"][extra] = dict(self.ui_attribute(), customId="debug_label")
        self.project["components"][self.bindings["HUDTextComponent"]]["attributes"].append(extra)
        self.assert_invalid("exactly its required string attributes and custom IDs")

    def test_additional_command_component_variable_is_rejected(self):
        extra = "ed23bc46-83b3-4937-ab43-91c2c8b85d08"
        owner = self.bindings["OpenGateComponent"]
        self.project["attributes"][extra] = dict(self.ui_attribute(), cId=owner, customId="debug_label")
        self.project["components"][owner]["attributes"] = [extra]
        self.assert_invalid("Only the seven state values, twenty-six UI strings, and two game_event inputs may add scoped variables")

    def test_additional_board_variable_is_rejected(self):
        extra = "5aa30329-45b8-42df-b65b-1e94f0a76a84"
        owner = self.bindings["Board"]
        self.project["attributes"][extra] = dict(self.ui_attribute(), cType="boards", cId=owner, customId="debug_label")
        self.project["boards"][owner]["attributes"] = [extra]
        self.assert_invalid("Only the seven state values, twenty-six UI strings, and two game_event inputs may add scoped variables")

    def test_ui_component_cannot_be_attached_as_a_command(self):
        for binding in SYNC.UI_COMPONENTS:
            with self.subTest(component=binding):
                self.element("EventEntryElement")["components"] = [self.bindings[binding]]
                self.assert_invalid("State, UI, and game_event components must remain standalone data")

    def test_presentation_can_show_known_ui_field(self):
        self.script("PresentationCollectingElement",
                    "show(hud.station_name, world_text.sign_exit, quest_ui.mission_heading, player.power_cells)")
        self.validate()

    def test_presentation_can_read_event_inputs(self):
        self.script("PresentationCollectingElement", "show(game_event.type, game_event.cell_id)")
        self.validate()

    def test_presentation_can_read_player_and_quest_state(self):
        self.script("PresentationCollectingElement",
                    "show(player.power_cells, quest.started, quest.power_restored, quest.completed, quest.required_power_cells)")
        self.conditions("PresentationBranch")[0]["script"] = (
            "quest.started && !quest.completed && player.power_cells >= quest.required_power_cells")
        self.validate()

    def test_presentation_cannot_write_player_or_quest_state(self):
        for scope, fields in SYNC.STATE_COMPONENTS.values():
            for field, kind, _ in fields.values():
                with self.subTest(field=f"{scope}.{field}"):
                    self.script("PresentationCollectingElement", f"{scope}.{field} = {'true' if kind == 'boolean' else 1}")
                    self.assert_invalid("Presentation may only assign quest_ui fields")

    def test_presentation_cannot_read_unknown_state_fields_or_bare_components(self):
        for expression in ("player.started", "quest.power_cells", "player.unknown", "quest.unknown", "player", "quest"):
            with self.subTest(expression=expression):
                self.script("PresentationCollectingElement", f"show({expression})")
                self.assert_invalid("Presentation may only assign quest_ui fields")

    def test_presentation_cannot_read_former_global_names(self):
        for name in ("powerCells", "questStarted", "powerRestored", "questCompleted", "requiredPowerCells"):
            with self.subTest(name=name):
                self.script("PresentationCollectingElement", f"show({name})")
                self.assert_invalid("Presentation may only assign quest_ui fields")

    def test_world_scripts_cannot_use_former_global_names(self):
        for name in ("powerCells", "questStarted", "powerRestored", "questCompleted", "requiredPowerCells"):
            for script in (f"show({name})", f"{name} = 1"):
                with self.subTest(script=script):
                    self.script("TerminalAcceptElement", script)
                    self.assert_invalid("state fields instead of former global names")

    def test_world_conditions_cannot_use_former_global_names(self):
        self.conditions("GeneratorBranch")[0]["script"] = "questStarted && powerCells >= requiredPowerCells"
        self.assert_invalid("state fields instead of former global names")

    def test_localized_world_scripts_cannot_use_former_global_names(self):
        self.project = copy.deepcopy(self.localized)
        self.localized_content("TerminalAcceptElement")["text"] = self.code_block("questStarted = true")
        self.assert_invalid("state fields instead of former global names")

    def test_legacy_names_in_narrative_string_literals_are_not_variable_references(self):
        self.script("TerminalAcceptElement", 'show("The old powerCells value", \'questStarted\', "powerRestored and questCompleted")')
        self.validate()

    def test_world_events_may_still_update_authored_ui_text(self):
        self.element("TerminalAcceptElement")["content"] = (
            self.code_block('hud.station_name = "Powered outpost"')
            + self.code_block('world_text.sign_exit = "Exit open"'))
        self.validate()

    def test_world_code_blocks_reject_multiple_statements_in_all_exports(self):
        pairs = (
            ("quest.started = true", 'show("Task accepted")'),
            ('hud.station_name = "RELAY 08"', 'world_text.sign_exit = "OPEN"'),
        )
        for export in (self.unreal, self.authoring, self.localized):
            for statements in pairs:
                for separator in ("\n", "; "):
                    with self.subTest(localized="contents" in export, statements=statements, separator=separator):
                        self.project = copy.deepcopy(export)
                        content = self.code_block(separator.join(statements))
                        if "content" in self.element("TerminalAcceptElement"):
                            self.element("TerminalAcceptElement")["content"] = content
                        else:
                            self.localized_content("TerminalAcceptElement")["text"] = content
                        self.assert_invalid("Each world-event code block must contain exactly one statement")

    def test_world_statements_in_separate_blocks_are_allowed_in_all_exports(self):
        content = "".join(self.code_block(script) for script in (
            "quest.started = true", 'show("Task accepted")',
            'hud.station_name = "RELAY 08"', 'world_text.sign_exit = "OPEN"',
        ))
        for export in (self.unreal, self.authoring, self.localized):
            with self.subTest(localized="contents" in export):
                self.project = copy.deepcopy(export)
                if "content" in self.element("TerminalAcceptElement"):
                    self.element("TerminalAcceptElement")["content"] = content
                else:
                    self.localized_content("TerminalAcceptElement")["text"] = content
                self.validate()

    def test_world_statement_count_preserves_strings_and_multiline_calls(self):
        for script in (
            'show("Continue; // instructions /* here */ #1", \'say "go"; then exit\')',
            'hud.station_name = "Say \\"ready;\\" before entering"',
            'show(\n    "Collected; cells: ",\n    player.power_cells, "/", quest.required_power_cells\n)',
            "player.power_cells += 1",
        ):
            with self.subTest(script=script):
                self.script("TerminalAcceptElement", script)
                self.validate()

    def test_world_control_flow_fragments_remain_allowed_in_separate_blocks(self):
        self.element("TerminalAcceptElement")["content"] = "".join(self.code_block(script) for script in (
            "if !quest.started && player.power_cells >= 0",
            "quest.started = true",
            "elseif quest.power_restored",
            'show("Power is restored")',
            "else",
            'show("Continue restoring power")',
            "endif",
        ))
        self.validate()

    def test_world_control_flow_fragment_cannot_hide_another_statement(self):
        for fragment in ("if !quest.started", "elseif quest.power_restored", "else", "endif"):
            with self.subTest(fragment=fragment):
                self.script("TerminalAcceptElement", fragment + "\nquest.started = true")
                self.assert_invalid("Each world-event code block must contain exactly one statement")

    def test_world_statement_count_preserves_rich_text_element_references(self):
        mention = (f'<span class="mention mention-element" data-type="element" '
                   f'data-id="{self.bindings["TerminalAcceptElement"]}">Terminal · accept task</span>')
        self.element("TerminalAcceptElement")["content"] = f"<pre><code>show(visits({mention}))</code></pre>"
        self.validate()

    def test_world_reference_cannot_hide_another_statement_in_its_code_block(self):
        mention = (f'<span class="mention mention-element" data-type="element" '
                   f'data-id="{self.bindings["TerminalAcceptElement"]}">Terminal · accept task</span>')
        self.element("TerminalAcceptElement")["content"] = (
            f"<pre><code>show(visits({mention}))\nquest.started = true</code></pre>")
        self.assert_invalid("Each world-event code block must contain exactly one statement")

    def test_feedback_can_read_state_without_changing_it(self):
        self.script("DuplicatePickupElement", 'show("Already carrying ", player.power_cells, "/", quest.required_power_cells)')
        self.validate()

    def test_presentation_cannot_assign_event_inputs(self):
        for script in ('game_event.type = "use_terminal"', 'game_event.cell_id = "cell_a"'):
            with self.subTest(script=script):
                self.script("PresentationCollectingElement", script)
                self.assert_invalid("Presentation may only assign quest_ui fields")

    def test_presentation_cannot_read_an_unknown_event_input(self):
        self.script("PresentationCollectingElement", "show(game_event.unknown)")
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
        self.element("PresentationEntryElement")["content"] = (self.code_block("if quest.started")
            + "".join(self.entry_resets()) + self.code_block("endif"))
        self.assert_invalid("reset all seven fields before any other statements")

    def test_entry_shared_overrides_accept_read_only_conditionals_in_all_exports(self):
        content = ("".join(self.entry_resets()) + "".join(self.code_block(script) for script in (
            "if quest.completed || quest.power_restored", 'quest_ui.grid_status = "POWER ONLINE"',
            "elseif quest.started", 'quest_ui.grid_status = "RESTORATION IN PROGRESS"',
            "else", 'show("Visit the terminal")', "endif", "show(player.power_cells)",
        )))
        for export in (self.unreal, self.authoring, self.localized):
            with self.subTest(localized="contents" in export):
                self.project = copy.deepcopy(export)
                if "content" in self.element("PresentationEntryElement"):
                    self.element("PresentationEntryElement")["content"] = content
                else:
                    self.localized_content("PresentationEntryElement")["text"] = content
                self.validate()

    def test_nested_ui_conditionals_remain_read_only(self):
        self.element("PresentationEntryElement")["content"] = ("".join(self.entry_resets())
            + "".join(self.code_block(script) for script in (
                "if quest.power_restored", "if quest.completed", 'quest_ui.grid_status = "COMPLETE"',
                "else", 'quest_ui.grid_status = "ONLINE"', "endif", "endif")))
        self.validate()

    def test_ui_conditional_expressions_cannot_call_or_write(self):
        for expression in ("resetAll()", "quest.started = true", "random(2) > 0", "other.state"):
            with self.subTest(expression=expression):
                self.element("PresentationEntryElement")["content"] = ("".join(self.entry_resets())
                    + self.code_block("if " + expression) + self.code_block('quest_ui.grid_status = "ONLINE"')
                    + self.code_block("endif"))
                self.assert_invalid("Presentation conditions")

    def test_conditional_ui_assignments_cannot_change_shared_state(self):
        for script in ("quest.completed = true", "player.power_cells += 1", "cell_a.collected = true",
                       'game_event.type = "enter_exit"', 'hud.station_name = "Changed"',
                       'save_ui.controls = "Changed"'):
            with self.subTest(script=script):
                self.element("PresentationEntryElement")["content"] = ("".join(self.entry_resets())
                    + self.code_block("if quest.power_restored") + self.code_block(script) + self.code_block("endif"))
                self.assert_invalid("Presentation may only assign quest_ui fields")

    def test_ui_condition_fragments_require_separate_code_blocks(self):
        for scripts in (("if quest.power_restored", 'quest_ui.grid_status = "ONLINE"'),
                        ("elseif quest.started", 'quest_ui.grid_status = "READY"')):
            with self.subTest(scripts=scripts):
                self.element("PresentationEntryElement")["content"] = ("".join(self.entry_resets())
                    + self.code_block("\n".join(scripts)))
                self.assert_invalid("Presentation conditions must be read-only expressions in their own code blocks")

    def test_ui_conditional_fragments_must_be_balanced(self):
        for scripts in (("if quest.started",), ("else",), ("endif",), ("elseif quest.started",),
                        ("if quest.started", "else", "else", "endif"),
                        ("if quest.started", "else", "elseif quest.power_restored", "endif")):
            with self.subTest(scripts=scripts):
                self.element("PresentationEntryElement")["content"] = ("".join(self.entry_resets())
                    + "".join(self.code_block(script) for script in scripts))
                self.assert_invalid("Presentation conditional blocks must use balanced")

    def test_entry_cannot_reset_again_inside_conditional_overrides(self):
        self.element("PresentationEntryElement")["content"] = ("".join(self.entry_resets())
            + self.code_block("if quest.power_restored") + self.entry_resets()[0] + self.code_block("endif"))
        self.assert_invalid("reset all seven fields before any other statements")

    def test_reset_all_cannot_replace_entry_defaults(self):
        self.script("PresentationEntryElement", "resetAll()")
        self.assert_invalid("Presentation may only assign quest_ui fields")

    def test_entry_cannot_reset_gameplay_or_static_ui(self):
        for target in (
            "quest.started", "player.power_cells", "hud.station_name", "world_text.sign_exit", "save_ui.saved", "quest_ui.unknown",
            "game_event.type", "game_event.cell_id",
        ):
            with self.subTest(target=target):
                self.element("PresentationEntryElement")["content"] = (
                    "".join(self.entry_resets()) + self.code_block(f"reset({target})"))
                self.assert_invalid("Only the presentation entry may reset known quest_ui fields")

    def test_later_presentation_elements_cannot_reset_defaults(self):
        self.element("PresentationPoweredElement")["content"] = self.entry_resets()[0]
        self.assert_invalid("Only the presentation entry may reset known quest_ui fields")

    def test_multiple_statements_in_one_code_block_are_rejected(self):
        for separator in ("\n", "; "):
            with self.subTest(separator=separator):
                self.script("PresentationPoweredElement", separator.join((
                    "quest_ui.mission_heading = hud.station_name",
                    "quest_ui.grid_status = world_text.sign_gate",
                )))
                self.assert_invalid("Each presentation code block must contain exactly one simple statement")

    def test_multiple_assignments_in_separate_code_blocks_are_allowed(self):
        self.element("PresentationPoweredElement")["content"] = (
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
        self.assert_invalid("must not define global variables")

    def test_integer_is_not_accepted_as_boolean_default(self):
        self.state_attribute("QuestCompletedAttribute")["value"]["data"] = 0
        self.assert_invalid("initial value has the wrong type")

    def test_already_accepted_new_game_is_rejected(self):
        self.state_attribute("QuestStartedAttribute")["value"]["data"] = True
        self.assert_invalid("not a valid new-game default")

    def test_zero_required_cells_is_rejected(self):
        self.state_attribute("RequiredPowerCellsAttribute")["value"]["data"] = 0
        self.assert_invalid("not a valid new-game default")

    def test_multiple_element_outputs_are_rejected(self):
        outputs = self.element("TerminalAcceptElement")["outputs"]
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
        self.assert_invalid("Every shared-board node must be reachable")

    def test_collection_requires_pickup_event_context(self):
        self.generator_connection().update(targetid=self.bindings["PickupActionElement"], targetType="elements")
        self.assert_invalid("Only the pickup event supplies the physical identity")

    def test_changed_command_custom_id_is_rejected(self):
        self.project["components"][self.bindings["CollectCellComponent"]]["customId"] = "collect_other"
        self.assert_invalid("custom ID no longer matches")

    def test_collection_command_on_terminal_is_rejected(self):
        self.element("TerminalAcceptElement")["components"] = [self.bindings["CollectCellComponent"]]
        self.assert_invalid("Only PickupAction")

    def test_gate_command_on_shared_event_entry_is_rejected(self):
        self.element("EventEntryElement")["components"] = [self.bindings["OpenGateComponent"]]
        self.assert_invalid("only Success may open the gate")

    def test_gate_command_on_exit_is_rejected(self):
        self.element("CompletedElement")["components"] = [self.bindings["OpenGateComponent"]]
        self.assert_invalid("only Success may open the gate")

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
        self.localized_content("PresentationCollectingElement")["text"] = "<pre><code>player.power_cells = 99</code></pre>"
        self.assert_invalid("Presentation may only assign quest_ui fields")


class NarrativeSyncTests(unittest.TestCase):
    FILES = (
        "Content/ArcweaveExport/quest.json", "Narrative/authoring.json",
        "Narrative/import.json", "Narrative/project.json", "Narrative/bindings.json",
    )

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        for filename in self.FILES:
            target = self.root / filename
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / filename, target)
        self.unreal = json.loads((self.root / self.FILES[0]).read_text(encoding="utf-8"))
        self.authoring = json.loads((self.root / self.FILES[1]).read_text(encoding="utf-8"))
        self.localized = json.loads((self.root / self.FILES[2]).read_text(encoding="utf-8"))
        # Match the API: localized exports omit layout; only the Unreal export carries it.
        for collection in ("elements", "branches", "notes", "jumpers"):
            for item in self.localized[collection].values():
                for key in ("x", "y", "width", "height", "autoHeight"):
                    item.pop(key, None)
        self.requests = []

    def snapshot(self):
        return {filename: (self.root / filename).read_bytes() for filename in self.FILES}

    def download(self, request, timeout):
        self.requests.append(request)
        suffix = request.full_url.rsplit("/", 1)[-1]
        value = {"unreal": self.unreal, "json": self.authoring,
                 "json?allLocales=true": self.localized}[suffix]
        return io.BytesIO(json.dumps(value, ensure_ascii=False).encode("utf-8"))

    def sync(self, project_hash=None, download=None):
        with patch.object(SYNC.urllib.request, "build_opener") as build_opener:
            build_opener.return_value.open.side_effect = download or self.download
            with redirect_stdout(io.StringIO()):
                SYNC.sync_exports(self.root, "fixture-token", project_hash)

    def test_sync_updates_all_exports_and_digest_metadata_together(self):
        controls = "aaa35a32-93ac-43fb-94a4-55e4f21ca8a3"
        for project in (self.unreal["project"], self.authoring, self.localized):
            project["attributes"][controls]["value"]["data"] = "Custom controls from Arcweave"
        self.sync()
        metadata = json.loads((self.root / "Narrative/project.json").read_text(encoding="utf-8"))
        for filename, digest_key in zip(self.FILES[:3], ("unrealSha256", "authoringSha256", "importSha256")):
            with self.subTest(filename=filename):
                data = (self.root / filename).read_bytes()
                project = json.loads(data)
                project = project.get("project", project)
                self.assertEqual(project["attributes"][controls]["value"]["data"], "Custom controls from Arcweave")
                self.assertEqual(metadata[digest_key], hashlib.sha256(data).hexdigest())
        self.assertEqual(metadata["importExport"], "import.json")
        self.assertTrue(metadata["importExportUrl"].endswith("/json?allLocales=true"))
        self.assertEqual(len(self.requests), 3)
        self.assertTrue(all(request.headers["Authorization"] == "Bearer fixture-token" for request in self.requests))

    def test_import_is_a_direct_graph_and_preserves_layout_and_translations(self):
        entry = self.localized["startingElement"]
        self.localized["locales"].append({"iso": "fr", "base": "en", "name": "French"})
        self.localized["contents"][entry]["content"]["fr"] = {"text": "<p>Explorer la station.</p>"}
        self.unreal["project"]["elements"][entry].update(x=120, y=-345, width=456, height=234, autoHeight=False)
        before = copy.deepcopy(self.localized)
        self.sync()
        imported = json.loads((self.root / "Narrative/import.json").read_text(encoding="utf-8"))
        self.assertNotIn("project", imported)
        self.assertEqual(imported["locales"], self.localized["locales"])
        self.assertEqual(imported["contents"], self.localized["contents"])
        for collection in ("elements", "branches", "notes", "jumpers"):
            for ident, item in self.unreal["project"][collection].items():
                for key in ("x", "y", "width", "height", "autoHeight"):
                    if key in item:
                        self.assertEqual(imported[collection][ident][key], item[key])
        self.assertEqual(self.localized, before)

    def test_project_override_becomes_the_next_sync_default(self):
        self.authoring["name"] = "My mission copy"
        self.sync("MyProjectCopy")
        metadata = json.loads((self.root / "Narrative/project.json").read_text(encoding="utf-8"))
        self.assertEqual(metadata["projectHash"], "MyProjectCopy")
        self.assertEqual(metadata["projectUrl"], "https://arcweave.com/app/project/MyProjectCopy")
        self.assertEqual(metadata["name"], "My mission copy")
        self.assertNotIn("workspaceHash", metadata)
        self.assertNotIn("workspaceUrl", metadata)
        for key in ("unrealExportUrl", "authoringExportUrl", "importExportUrl"):
            self.assertTrue(metadata[key].startswith("https://arcweave.com/api/v1/MyProjectCopy/"))
        self.sync()
        self.assertEqual(len(self.requests), 6)
        self.assertTrue(all("/MyProjectCopy/" in request.full_url for request in self.requests))

    def test_same_project_sync_preserves_known_workspace(self):
        before = json.loads((self.root / "Narrative/project.json").read_text(encoding="utf-8"))
        self.sync()
        after = json.loads((self.root / "Narrative/project.json").read_text(encoding="utf-8"))
        for key in ("projectHash", "workspaceHash", "workspaceUrl"):
            self.assertEqual(after[key], before[key])

    def test_failed_final_download_leaves_all_exports_and_project_selection_unchanged(self):
        before = self.snapshot()

        def fail_final_request(request, timeout):
            if request.full_url.endswith("?allLocales=true"):
                raise urllib.error.URLError("Fixture network failure")
            return self.download(request, timeout)

        with self.assertRaises(urllib.error.URLError):
            self.sync("MyProjectCopy", download=fail_final_request)
        self.assertEqual(self.snapshot(), before)

    def test_invalid_import_leaves_all_exports_and_project_selection_unchanged(self):
        before = self.snapshot()
        bindings = json.loads((self.root / "Narrative/bindings.json").read_text(encoding="utf-8"))
        self.localized["components"][bindings["HUDTextComponent"]]["customId"] = "wrong_scope"
        with self.assertRaises(ValueError):
            self.sync("MyProjectCopy")
        self.assertEqual(self.snapshot(), before)

    def test_bundled_import_matches_generated_snapshot(self):
        imported = SYNC.make_import_project(self.localized, self.unreal["project"])
        self.assertEqual(imported, json.loads((ROOT / "Narrative/import.json").read_text(encoding="utf-8")))
        metadata = json.loads((ROOT / "Narrative/project.json").read_text(encoding="utf-8"))
        for filename, digest_key in zip(self.FILES[:3], ("unrealSha256", "authoringSha256", "importSha256")):
            self.assertEqual(metadata[digest_key], hashlib.sha256((ROOT / filename).read_bytes()).hexdigest())


if __name__ == "__main__":
    unittest.main()
