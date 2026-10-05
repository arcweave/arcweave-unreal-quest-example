#!/usr/bin/env python3
"""Download this sample's Arcweave exports without storing API credentials."""

import argparse
import ast
import copy
import hashlib
import json
import re
import sys
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timezone
from pathlib import Path
from html.parser import HTMLParser


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, response, code, message, headers, new_url):
        return None


HUD_FIELDS = {
    "brand", "station_name", "mission_tagline", "cells_label", "station_footer", "controls",
}
WORLD_FIELDS = {
    "terminal_label", "cell_a_label", "cell_b_label", "sign_station",
    "sign_distribution", "sign_gate", "sign_exit",
}
DISPLAY_FIELDS = {
    "mission_heading", "grid_status", "terminal_prompt", "cell_prompt",
    "generator_prompt", "generator_label", "gate_label",
}
SAVE_FIELDS = {
    "controls", "saved", "loaded", "no_save", "save_failed", "load_failed", "incompatible_save",
}
UI_COMPONENTS = {
    "HUDTextComponent": ("hud", HUD_FIELDS),
    "WorldTextComponent": ("world_text", WORLD_FIELDS),
    "QuestUIComponent": ("quest_ui", DISPLAY_FIELDS),
    "SaveUIComponent": ("save_ui", SAVE_FIELDS),
}
SCOPED_FIELDS = dict(UI_COMPONENTS.values())
EVENT_INPUTS = {
    "EventTypeAttribute": ("type", "string", ""),
    "CellIdAttribute": ("cell_id", "string", ""),
}
SCOPED_FIELDS["game_event"] = {item[0] for item in EVENT_INPUTS.values()}
STATE_COMPONENTS = {
    "CellAComponent": ("cell_a", {
        "CellACollectedAttribute": ("collected", "boolean", False),
    }),
    "CellBComponent": ("cell_b", {
        "CellBCollectedAttribute": ("collected", "boolean", False),
    }),
    "PlayerComponent": ("player", {
        "PowerCellsAttribute": ("power_cells", "integer", 0),
    }),
    "QuestStateComponent": ("quest", {
        "QuestStartedAttribute": ("started", "boolean", False),
        "PowerRestoredAttribute": ("power_restored", "boolean", False),
        "QuestCompletedAttribute": ("completed", "boolean", False),
        "RequiredPowerCellsAttribute": ("required_power_cells", "integer", None),
    }),
}
SCOPED_FIELDS.update({scope: {item[0] for item in attributes.values()}
                      for scope, attributes in STATE_COMPONENTS.values()})
COMMANDS = {"OpenGateComponent": "open_gate", "CollectCellComponent": "collect_cell"}
EVENT_ROUTES = {
    "use_terminal": "TerminalBranch",
    "collect_cell": "PickupBranch",
    "check_generator": "GeneratorBranch",
    "enter_exit": "ExitBranch",
}
MENU_EVENTS = {
    ("use_terminal", ""), ("collect_cell", "cell_a"), ("collect_cell", "cell_b"),
    ("check_generator", ""), ("enter_exit", ""),
}
EVENT_LANES = {
    "TerminalBranch": (
        "TerminalAcceptElement", "TerminalAcceptedElement", "TerminalPoweredElement", "TerminalCompletedElement",
    ),
    "PickupBranch": (
        "DuplicatePickupElement", "PickupTerminalRequiredElement",
        "PickupActionElement",
    ),
    "GeneratorBranch": (
        "SuccessElement", "MissingCellsElement", "TerminalRequiredElement", "AlreadyOnlineElement",
    ),
    "ExitBranch": ("CompletedElement", "ExitDeniedElement", "ExitAlreadyCompletedElement"),
}
DISPLAY_LEAVES = tuple("Presentation" + state + "Element" for state in (
    "Completed", "Powered", "Unaccepted", "Collecting", "Ready",
))
EXIT_ENDINGS = ("CompletedElement", "ExitAlreadyCompletedElement")


class CodeBlocks(HTMLParser):
    def __init__(self, mask_references=False):
        super().__init__()
        self.blocks = []
        self.in_code = False
        self.text = ""
        self.visible_text = ""
        self.mask_references = mask_references
        self.reference_depth = 0

    def handle_starttag(self, tag, attrs):
        if tag == "code":
            self.in_code = True
            self.blocks.append("")
        elif self.mask_references and self.in_code and tag == "span":
            attributes = dict(attrs)
            if self.reference_depth:
                self.reference_depth += 1
            elif (attributes.get("data-id")
                  and attributes.get("data-type") in {"element", "component", "board"}):
                self.blocks[-1] += "__arcweave_reference__"
                self.reference_depth = 1

    def handle_endtag(self, tag):
        if tag == "code":
            self.in_code = False
        elif tag == "span" and self.reference_depth:
            self.reference_depth -= 1

    def handle_data(self, data):
        self.text += data
        if not self.in_code:
            self.visible_text += data
        if self.in_code and not self.reference_depth:
            self.blocks[-1] += data


def arcscript_expression(script):
    # Translate operators outside string literals before parsing the read-only subset.
    operators = {"&&": " and ", "||": " or ", "!": "not "}
    return re.sub(
        r'''("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')|(&&|\|\||!(?!=))''',
        lambda match: match[1] if match[1] is not None else operators[match[2]],
        script,
    )


def scoped_field(node, scope=None):
    return (
        isinstance(node, ast.Attribute) and isinstance(node.value, ast.Name)
        and node.value.id in SCOPED_FIELDS
        and (scope is None or node.value.id == scope)
        and node.attr in SCOPED_FIELDS[node.value.id]
    )


def read_expression(node):
    # Presentation supports a deliberately small, side-effect-free expression subset.
    allowed = (
        ast.Expression, ast.Name, ast.Attribute, ast.Load, ast.Constant, ast.BinOp, ast.UnaryOp,
        ast.BoolOp, ast.Compare, ast.Add, ast.Sub, ast.Mult, ast.Div, ast.Mod,
        ast.UAdd, ast.USub, ast.Not, ast.And, ast.Or, ast.Eq, ast.NotEq,
        ast.Lt, ast.LtE, ast.Gt, ast.GtE,
    )
    names = {"true", "false"}
    attributes = [part for part in ast.walk(node) if isinstance(part, ast.Attribute)]
    for attribute in attributes:
        if not scoped_field(attribute):
            return False
    return all(
        isinstance(part, allowed) and (
            not isinstance(part, ast.Name) or part.id in names
            or (part.id in SCOPED_FIELDS and any(part is attribute.value for attribute in attributes))
        ) for part in ast.walk(node)
    )


def validate_display_content(content, entry=False):
    blocks = CodeBlocks()
    blocks.feed(content or "")
    resets = []
    conditionals = []
    for index, script in enumerate(blocks.blocks):
        code = arcscript_expression(script).strip()
        conditional = re.match(r"^(if|elseif)\b", code)
        if conditional or code in {"else", "endif"}:
            token = conditional[1] if conditional else code
            if conditional:
                try:
                    parsed = ast.parse(code[conditional.end():].strip(), mode="eval")
                except SyntaxError as error:
                    raise ValueError("Presentation conditions must be read-only expressions in their own code blocks.") from error
                if not read_expression(parsed):
                    raise ValueError("Presentation conditions cannot call functions or change state.")
            if token == "if":
                conditionals.append(False)
            elif not conditionals or (token in {"elseif", "else"} and conditionals[-1]):
                raise ValueError("Presentation conditional blocks must use balanced if/elseif/else/endif fragments.")
            elif token == "else":
                conditionals[-1] = True
            elif token == "endif":
                conditionals.pop()
            continue
        try:
            statements = ast.parse(code).body
        except SyntaxError as error:
            raise ValueError("Presentation scripts must use simple assignments, show(), or entry resets.") from error
        if len(statements) != 1:
            raise ValueError("Each presentation code block must contain exactly one simple statement for the native plugin.")
        statement = statements[0]
        if isinstance(statement, ast.Assign):
            if (len(statement.targets) == 1 and scoped_field(statement.targets[0], "quest_ui")
                    and read_expression(statement.value)):
                continue
        call = statement.value if isinstance(statement, ast.Expr) else None
        if isinstance(call, ast.Call) and isinstance(call.func, ast.Name) and not call.keywords:
            if call.func.id == "show" and all(read_expression(argument) for argument in call.args):
                continue
            if call.func.id == "reset":
                if not (entry and len(call.args) == 1 and scoped_field(call.args[0], "quest_ui")):
                    raise ValueError("Only the presentation entry may reset known quest_ui fields.")
                if index != len(resets):
                    raise ValueError("Presentation entry must reset all seven fields before any other statements.")
                resets.append(call.args[0].attr)
                continue
        raise ValueError("Presentation may only assign quest_ui fields, show known values, or reset its entry defaults.")
    if conditionals:
        raise ValueError("Presentation conditional blocks must use balanced if/elseif/else/endif fragments.")
    if entry and (len(resets) != len(DISPLAY_FIELDS) or set(resets) != DISPLAY_FIELDS):
        raise ValueError("Presentation entry must reset each of the seven quest_ui fields exactly once.")


def validate_feedback_content(content, label):
    blocks = CodeBlocks()
    blocks.feed(content or "")
    for script in blocks.blocks:
        try:
            statements = ast.parse(arcscript_expression(script).strip()).body
        except SyntaxError as error:
            raise ValueError(f"{label} must be feedback only, without changing state or calling commands.") from error
        call = statements[0].value if len(statements) == 1 and isinstance(statements[0], ast.Expr) else None
        if not (
            isinstance(call, ast.Call) and isinstance(call.func, ast.Name)
            and call.func.id == "show" and not call.keywords
            and all(read_expression(argument) for argument in call.args)
        ):
            raise ValueError(f"{label} must be feedback only, without changing state or calling commands.")


def validate_state_names(script):
    # Former globals are no longer imported. Ignore narrative text inside string literals.
    code = re.sub(r'''("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')''', "", script or "")
    if re.search(r"\b(?:questStarted|powerCells|requiredPowerCells|powerRestored|questCompleted)\b", code):
        raise ValueError("Arcscript must use player and quest state fields instead of former global names.")


def validate_world_code_block(script):
    # Native if/elseif/else/endif fragments occupy their own code blocks, just
    # like assignments and calls. World scripts retain their state-changing role.
    code = arcscript_expression(script).strip()
    if code in {"else", "endif"}:
        return
    conditional = re.match(r"^(?:if|elseif)\b", code)
    message = "Each world-event code block must contain exactly one statement for the native plugin."
    try:
        if conditional:
            ast.parse(code[conditional.end():].strip(), mode="eval")
        elif len(ast.parse(code).body) != 1:
            raise ValueError(message)
    except SyntaxError as error:
        raise ValueError(message) from error


def condition_expression(script):
    try:
        return ast.dump(ast.parse(arcscript_expression(script or "").strip(), mode="eval"))
    except SyntaxError:
        return None


def localized_field(project, collection, ident, field):
    item = project[collection][ident]
    if field in item:
        return item[field]
    # The reproducible import keeps the API's all-locales representation.
    locale = next((item["iso"] for item in project.get("locales", []) if item.get("base") is None), "en")
    contents = project.get("contents", {}).get(ident, {})
    return contents.get(field, {}).get(locale, {}).get("text")


def element_content(project, element_id):
    return localized_field(project, "elements", element_id, "content")


def validate_menu_label(content):
    blocks = CodeBlocks()
    blocks.feed(content or "")
    if not blocks.visible_text.strip():
        raise ValueError("Each interaction menu label must include visible action text.")
    inputs = {}
    for script in blocks.blocks:
        try:
            statements = ast.parse(arcscript_expression(script).strip()).body
        except SyntaxError as error:
            raise ValueError("Menu labels must set each game_event input exactly once with a literal string.") from error
        if len(statements) != 1:
            raise ValueError("Each menu label code block must contain exactly one statement for the native plugin.")
        statement = statements[0]
        if not (
            isinstance(statement, ast.Assign) and len(statement.targets) == 1
            and scoped_field(statement.targets[0], "game_event")
            and isinstance(statement.value, ast.Constant) and isinstance(statement.value.value, str)
            and statement.targets[0].attr not in inputs
        ):
            raise ValueError("Menu labels must set each game_event input exactly once with a literal string.")
        inputs[statement.targets[0].attr] = statement.value.value
    if set(inputs) != {"type", "cell_id"} or (inputs["type"], inputs["cell_id"]) not in MENU_EVENTS:
        raise ValueError("Each menu choice must supply a complete supported event type and cell identity pair.")
    return inputs["type"], inputs["cell_id"]


def validate_ui_component(project, component_id, scope, fields):
    component = project["components"][component_id]
    if component.get("customId") != scope or "children" in component:
        raise ValueError(f"The {scope} data component must have its required custom ID and cannot be a folder.")
    attributes = [project["attributes"][key] for key in component.get("attributes", []) or []]
    if len(attributes) != len(fields) or {item.get("customId") for item in attributes} != fields:
        raise ValueError(f"The {scope} component must have exactly its required string attributes and custom IDs.")
    for attribute in attributes:
        value = attribute["value"]
        text = value.get("data")
        if (
            attribute.get("cType") != "components" or attribute.get("cId") != component_id
            or value.get("type") != "string" or value.get("plain") is not True
            or not isinstance(text, str) or not text.strip()
            or re.search(r"<[^>]+>|\$\{|\bshow\s*\(", text)
        ):
            raise ValueError("UI attributes must be nonempty plain strings owned by the UI component, without HTML or Arcscript.")


def validate_bindings(project, bindings):
    suffixes = {
        "Board": "boards", "Element": "elements", "Branch": "branches",
        "Condition": "conditions", "Connection": "connections", "Component": "components",
        "Variable": "variables", "Attribute": "attributes", "Note": "notes",
    }
    for name, ident in bindings.items():
        collection = next((value for suffix, value in suffixes.items() if name.endswith(suffix)), None)
        if collection is None or ident not in project[collection]:
            raise ValueError(f"The export is missing the {name} binding.")
    event_entry = project.get("startingElement")
    if (event_entry not in project["elements"]
            or event_entry not in (project["boards"][bindings["Board"]].get("elements") or [])):
        raise ValueError("The starting element must identify an element on the world-event board.")
    actual_boards = {ident for ident, board in project["boards"].items() if "children" not in board}
    if actual_boards != {bindings["Board"]}:
        raise ValueError("The sample must use one shared board for interactions and objectives/UI.")
    presentation_board = project["boards"][bindings["Board"]]
    entry_markers = [
        (element_id, attribute_id)
        for element_id in presentation_board.get("elements", []) or []
        for attribute_id in project["elements"][element_id].get("attributes", []) or []
        if project["attributes"].get(attribute_id, {}).get("name") == "entry_point"
    ]
    query_entries = {}
    for name in ("objectives_ui", "inventory"):
        matches = [(element_id, attribute_id) for element_id, attribute_id in entry_markers
                   if project["attributes"][attribute_id].get("value", {}).get("data") == name]
        if len(matches) != 1:
            raise ValueError(f"The shared board must contain exactly one {name} entry marker.")
        entry, marker_id = matches[0]
        if entry == event_entry or any(entry == item[0] for item in query_entries.values()):
            raise ValueError(f"The {name} entry must be distinct from the interaction menu and other queries.")
        marker = project["attributes"][marker_id]
        value = marker.get("value", {})
        if (marker.get("cType") != "elements" or marker.get("cId") != entry
                or value.get("type") != "string" or value.get("plain") is not True):
            raise ValueError(f"The {name} entry marker must be a plain-string element attribute owned by its entry.")
        query_entries[name] = (entry, marker_id)
    if len(entry_markers) != len(query_entries):
        raise ValueError("The shared board must contain only its objectives_ui and inventory entry markers.")
    display_entry, display_marker = query_entries["objectives_ui"]
    inventory_entry, inventory_marker = query_entries["inventory"]

    variable_ids = {key for key, item in project["variables"].items()
                    if not item.get("root") and "children" not in item}
    if variable_ids:
        raise ValueError("The sample uses component state and must not define global variables.")
    state_attributes = {}
    for binding, (scope, fields) in STATE_COMPONENTS.items():
        component_id = bindings[binding]
        component = project["components"][component_id]
        if component.get("customId") != scope or "children" in component:
            raise ValueError(f"The {scope} data component must have its required custom ID and cannot be a folder.")
        attributes = component.get("attributes", []) or []
        if len(attributes) != len(fields) or set(attributes) != {bindings[name] for name in fields}:
            raise ValueError(f"The {scope} component must contain exactly its bound state attributes.")
        for name, (field, kind, default) in fields.items():
            attribute_id = bindings[name]
            attribute = project["attributes"][attribute_id]
            value = attribute["value"]
            data = value.get("data")
            if (attribute.get("customId") != field or attribute.get("cType") != "components"
                    or attribute.get("cId") != component_id or value.get("type") != kind):
                raise ValueError(f"The {scope}.{field} state attribute must keep its bound owner, name, and type.")
            if "data" not in value or type(data) is not (bool if kind == "boolean" else int):
                raise ValueError(f"The {scope}.{field} initial value has the wrong type.")
            if (default is None and data < 1) or (default is not None and data != default):
                raise ValueError(f"The {scope}.{field} initial value is not a valid new-game default.")
            state_attributes[attribute_id] = component_id
    ui_ids = {bindings[name] for name in UI_COMPONENTS}
    ui_attributes = {}
    for binding, (scope, fields) in UI_COMPONENTS.items():
        component_id = bindings[binding]
        validate_ui_component(project, component_id, scope, fields)
        for attribute_id in project["components"][component_id]["attributes"]:
            ui_attributes[attribute_id] = component_id
    event_component_id = bindings["GameEventComponent"]
    event_component = project["components"][event_component_id]
    if event_component.get("customId") != "game_event" or "children" in event_component:
        raise ValueError("The game_event data component must have its required custom ID and cannot be a folder.")
    event_attributes = event_component.get("attributes", []) or []
    if len(event_attributes) != len(EVENT_INPUTS) or set(event_attributes) != {bindings[name] for name in EVENT_INPUTS}:
        raise ValueError("The game_event component must contain exactly its two bound input attributes.")
    for binding, (name, kind, default) in EVENT_INPUTS.items():
        attribute = project["attributes"][bindings[binding]]
        value = attribute["value"]
        data = value.get("data")
        # Arcweave exports empty string attributes as null; the released plugin imports them as "".
        if kind == "string" and data is None:
            data = ""
        if (
            attribute.get("customId") != name or attribute.get("cType") != "components"
            or attribute.get("cId") != event_component_id or value.get("type") != kind
            or "data" not in value or type(data) is not type(default) or data != default
            or (kind == "string" and value.get("plain") is not True)
        ):
            raise ValueError(f"The game_event.{name} input must keep its bound owner, type, and empty-string default.")
    scoped_attributes = {**state_attributes, **ui_attributes,
                         **{ident: event_component_id for ident in event_attributes}}
    for attribute_id, attribute in project["attributes"].items():
        if attribute.get("cType") in {"boards", "components"}:
            value = attribute["value"]
            if value["type"] in {"boolean", "integer", "float"} or (value["type"] == "string" and value.get("plain")):
                if (attribute_id not in scoped_attributes or attribute["cType"] != "components"
                        or attribute.get("cId") != scoped_attributes[attribute_id]):
                    raise ValueError("Only the seven state values, twenty-six UI strings, and two game_event inputs may add scoped variables.")
    state_ids = {bindings[name] for name in STATE_COMPONENTS}
    data_ids = ui_ids | state_ids | {event_component_id}
    for component_id, component in project["components"].items():
        if component_id not in data_ids and component.get("customId") in SCOPED_FIELDS:
            raise ValueError("State, UI, and game_event data component scopes must be unique.")

    # Export formats may omit the root or flatten all folders.
    folders = {ident: item for ident, item in project["components"].items() if "children" in item}
    if folders:
        groups = {
            "UI": ui_ids, "State": state_ids, "Inputs": {event_component_id},
            "Actions": {bindings[name] for name in COMMANDS},
        }
        group_folders = set()
        for name, children in groups.items():
            matching = {ident: item for ident, item in folders.items() if not item.get("root")
                        and set(item.get("children", [])) == children}
            memberships = [child for item in folders.values() for child in item.get("children", [])
                           if child in children]
            if len(matching) != 1 or len(memberships) != len(children):
                raise ValueError(f"The {name} folder must contain exactly its required components together.")
            ident, folder = next(iter(matching.items()))
            if folder.get("customId") or folder.get("attributes"):
                raise ValueError(f"The {name} folder is organizational and must not define a runtime scope or attributes.")
            group_folders.add(ident)
        roots = [item for item in folders.values() if item.get("root")]
        if (len(roots) > 1 or len(folders) != len(group_folders) + len(roots)
                or (roots and set(roots[0].get("children", [])) != group_folders)):
            raise ValueError("State, Inputs, Actions, and UI must be the four top-level component folders.")

    for name, custom_id in COMMANDS.items():
        component = project["components"][bindings[name]]
        if component.get("customId") != custom_id:
            raise ValueError(f"The {name} custom ID no longer matches C++.")
    component_ids = {ident for ident, item in project["components"].items() if "children" not in item}
    if component_ids != data_ids | {bindings[name] for name in COMMANDS}:
        raise ValueError("The sample requires eight data components and exactly the collect_cell and open_gate action components.")
    command_elements = {
        bindings["PickupActionElement"]: {bindings["CollectCellComponent"]},
        bindings["SuccessElement"]: {bindings["OpenGateComponent"]},
    }

    # Model automatic execution through elements and every possible branch outcome.
    owners = {}
    for board_id, board in project["boards"].items():
        for collection in ("elements", "branches", "connections", "jumpers"):
            for ident in board.get(collection, []) or []:
                if ident in owners:
                    raise ValueError("An automatic graph object belongs to multiple boards.")
                owners[ident] = board_id
    edges = {ident: [] for collection in ("elements", "branches") for ident in project[collection]}
    used_connections = set()
    used_conditions = set()
    used_jumpers = set()
    jumper_roles = {}
    return_sources = set()
    query_destinations = set()
    world_jumper_sources = {}
    display_leaves = {bindings[name] for name in DISPLAY_LEAVES}
    exit_endings = {bindings[name] for name in EXIT_ENDINGS}

    def edge(origin, source_id, source_type, connection_id):
        connection = project["connections"][connection_id]
        target = connection["targetid"]
        target_type = connection["targetType"]
        if target_type == "jumpers":
            if target not in project["jumpers"] or owners.get(target) != owners.get(origin):
                raise ValueError("A boundary jumper must exist on the same board as its connection.")
            if source_type != "elements":
                raise ValueError("Only element outcomes may use boundary jumpers.")
            destination = project["jumpers"][target].get("elementId")
            if origin == event_entry:
                if destination not in {display_entry, inventory_entry}:
                    raise ValueError("Menu query jumpers must target the inventory or objectives_ui entry.")
                if destination in query_destinations:
                    raise ValueError("The interaction menu must offer inventory and objectives exactly once through separate jumpers.")
                query_destinations.add(destination)
                role = ("query", destination)
            elif origin in display_leaves or origin == inventory_entry:
                if destination != event_entry:
                    raise ValueError("Every return jumper must target the interaction menu.")
                if target in used_jumpers:
                    raise ValueError("Each return jumper must be used only by its own presentation leaf or inventory query.")
                return_sources.add(origin)
                role = ("return", origin)
            else:
                if destination != event_entry:
                    raise ValueError("World-event jumpers must target the interaction menu.")
                world_jumper_sources.setdefault(target, set()).add(origin)
                role = ("world", None)
            if target in jumper_roles and jumper_roles[target] != role:
                raise ValueError("World-event lanes and queries must use separate return jumpers.")
            jumper_roles[target] = role
            used_jumpers.add(target)
            target = destination
            target_type = "elements"
        if (
            connection_id in used_connections or connection["sourceid"] != source_id
            or connection["sourceType"] != source_type
            or target_type not in {"elements", "branches"}
            or target not in project[target_type]
            or owners.get(origin) is None
            or owners.get(origin) != owners.get(target)
            or owners.get(origin) != owners.get(connection_id)
        ):
            raise ValueError("An automatic connection has an invalid source, target, or board.")
        used_connections.add(connection_id)
        edges[origin].append(target)

    for ident, element in project["elements"].items():
        outputs = element.get("outputs", []) or []
        if ident != event_entry and len(outputs) > 1:
            raise ValueError("Event elements must have at most one automatic output.")
        for output in outputs:
            edge(ident, ident, "elements", output)
        components = element.get("components", []) or []
        if data_ids.intersection(components):
            raise ValueError("State, UI, and game_event components must remain standalone data, not attached gameplay commands.")
        expected = command_elements.get(ident, set())
        if set(components) != expected or len(components) != len(expected):
            raise ValueError("Only PickupAction may collect a cell; only Success may open the gate.")
    branch_conditions = {}
    condition_outputs = {}
    for connection_id, connection in project["connections"].items():
        if connection["sourceType"] == "conditions":
            condition_outputs.setdefault(connection["sourceid"], []).append(connection_id)
    for ident, branch in project["branches"].items():
        group = branch["conditions"]
        condition_ids = [group["ifCondition"]] + (group.get("elseIfConditions", []) or [])
        if group.get("elseCondition"):
            condition_ids.append(group["elseCondition"])
        branch_conditions[ident] = condition_ids
        for condition_id in condition_ids:
            if condition_id in used_conditions:
                raise ValueError("A condition belongs to multiple branches.")
            output = project["conditions"][condition_id]["output"]
            if condition_outputs.get(condition_id) != [output]:
                raise ValueError("Each condition row must have exactly one outgoing connection matching its output.")
            used_conditions.add(condition_id)
            edge(ident, condition_id, "conditions", output)
    if used_connections != set(project["connections"]) or used_conditions != set(project["conditions"]):
        raise ValueError("Every condition and connection must belong to an automatic graph path.")
    if return_sources.intersection(display_leaves) != display_leaves:
        raise ValueError("Each of the five presentation leaves must use its own return jumper.")
    if inventory_entry not in return_sources:
        raise ValueError("The inventory query must use its own return jumper to the interaction menu.")
    if (used_jumpers != set(project["jumpers"])
            or used_jumpers != set(project["boards"][bindings["Board"]].get("jumpers", []) or [])):
        raise ValueError("Every jumper must be owned and used, with no unused jumpers.")

    def reachable(starts, stop):
        result = set()
        pending = list(starts)
        while pending:
            ident = pending.pop()
            if ident not in result and ident != stop:
                result.add(ident)
                pending.extend(edges[ident])
        return result

    def validate_acyclic(node_ids, stop):
        visited, active = set(), set()
        def visit(ident):
            if ident == stop:
                return
            if ident in active:
                raise ValueError("An automatic event path contains a cycle before its execution boundary.")
            if ident in visited:
                return
            active.add(ident)
            for target in edges[ident]:
                visit(target)
            active.remove(ident)
            visited.add(ident)
        for ident in node_ids:
            visit(ident)

    display_ids = reachable({display_entry}, event_entry)
    validate_acyclic(display_ids, event_entry)
    if bindings["PresentationBranch"] not in display_ids:
        raise ValueError("Presentation must reach its bound state branch.")
    leaves = display_leaves
    terminal_ids = {ident for ident in display_ids if edges[ident] == [event_entry]}
    if terminal_ids != leaves or any(not edges[ident] for ident in display_ids):
        raise ValueError("Presentation must return to the interaction menu through exactly its five bound display leaves.")
    if any(ident != display_marker and item.get("cType") == "elements" and item.get("cId") in display_ids
           for ident, item in project["attributes"].items()):
        raise ValueError("Presentation elements must not retain metadata besides the entry marker; use quest_ui component fields.")

    inventory_ids = reachable({inventory_entry}, event_entry)
    validate_acyclic(inventory_ids, event_entry)
    if inventory_ids != {inventory_entry} or edges[inventory_entry] != [event_entry]:
        raise ValueError("The inventory query must show inventory and return directly to the interaction menu.")
    if (project["elements"][inventory_entry].get("attributes") or []) != [inventory_marker]:
        raise ValueError("The inventory query must retain only its inventory entry marker.")
    validate_feedback_content(element_content(project, inventory_entry), "Inventory query")

    event_router = bindings["EventRouterBranch"]
    menu_connections = set(project["elements"][event_entry]["outputs"])
    event_connections = {ident for ident in menu_connections
                         if project["connections"][ident]["targetType"] == "branches"
                         and project["connections"][ident]["targetid"] == event_router}
    if (len(menu_connections) != len(MENU_EVENTS) + 2 or len(event_connections) != len(MENU_EVENTS)
            or query_destinations != {display_entry, inventory_entry}):
        raise ValueError("The shared world-event entry must connect directly to its routing branch with five menu choices, plus two query jumpers.")
    menu_events = [validate_menu_label(localized_field(project, "connections", ident, "label"))
                   for ident in event_connections]
    if len(menu_events) != len(MENU_EVENTS) or set(menu_events) != MENU_EVENTS:
        raise ValueError("The interaction menu must offer terminal, both cells, generator, and exit exactly once.")
    for ident in menu_connections - event_connections:
        content = localized_field(project, "connections", ident, "label")
        label = CodeBlocks()
        label.feed(content or "")
        if not label.visible_text.strip():
            raise ValueError("Each query menu label must include visible choice text.")
        validate_feedback_content(content, "Query menu labels")
    for ident in set(project["connections"]) - menu_connections:
        content = localized_field(project, "connections", ident, "label")
        validate_feedback_content(content, "Automatic connection labels")
        if project["connections"][ident]["sourceType"] == "conditions":
            label = CodeBlocks()
            label.feed(content or "")
            if label.text.strip() or label.blocks:
                raise ValueError("Condition-output labels must stay empty so they cannot replace interaction menu text.")
    router_group = project["branches"][event_router]["conditions"]
    router_conditions = branch_conditions[event_router]
    router_else = router_group.get("elseCondition")
    expected_routes = {
        condition_expression(f'game_event.type == "{event}"'): bindings[entry]
        for event, entry in EVENT_ROUTES.items()
    }
    actual_routes = {
        condition_expression(project["conditions"][ident].get("script")): target
        for ident, target in zip(router_conditions, edges[event_router]) if ident != router_else
    }
    if (
        len(router_conditions) != len(EVENT_ROUTES) or actual_routes != expected_routes or router_else
    ):
        raise ValueError("The event router must select terminal, collect_cell, generator, or exit directly through its branch, without an else fallback.")

    pickup_branch = bindings["PickupBranch"]
    pickup_conditions = branch_conditions[pickup_branch]
    pickup_else = project["branches"][pickup_branch]["conditions"].get("elseCondition")
    if (
        len(pickup_conditions) != 3 or pickup_conditions[-1] != pickup_else
        or project["conditions"][pickup_else].get("script")
        or [condition_expression(project["conditions"][ident].get("script")) for ident in pickup_conditions[:-1]]
        != [condition_expression('(game_event.cell_id == "cell_a" && cell_a.collected) || '
                                 '(game_event.cell_id == "cell_b" && cell_b.collected)'),
            condition_expression("!quest.started")]
        or edges[pickup_branch] != [bindings[name] for name in (
            "DuplicatePickupElement", "PickupTerminalRequiredElement", "PickupActionElement",
        )]
    ):
        raise ValueError("The pickup branch must check duplicate identity first, then task acceptance, before collecting.")

    world_ids = reachable({event_router}, event_entry)
    if world_ids.intersection(display_ids | inventory_ids):
        raise ValueError("World events must return to the menu without entering the optional query flows.")
    validate_acyclic(world_ids, event_entry)
    if {ident for ident in world_ids if not edges[ident]} != exit_endings:
        raise ValueError("Only the completed exit outcomes may end a playthrough; all other world outcomes must return to the menu.")
    lane_ids = {name: reachable({bindings[name]}, event_entry) for name in EVENT_LANES}
    for name, current in lane_ids.items():
        if name != "PickupBranch" and bindings["PickupActionElement"] in current:
            raise ValueError("Only the pickup event supplies the physical identity needed by collect_cell.")
    for name, current in lane_ids.items():
        if any(current.intersection(other) for other_name, other in lane_ids.items() if other_name != name):
            raise ValueError("Separate world-event lanes must not execute one another or share automatic paths.")
        if not {bindings[item] for item in EVENT_LANES[name]}.issubset(current):
            raise ValueError("Each world-event lane must retain its bound outcomes and gameplay checks.")
        boundary_sources = {ident for ident in current if event_entry in edges[ident]}
        jumpers = {ident: sources for ident, sources in world_jumper_sources.items()
                   if sources.intersection(current)}
        if len(jumpers) != 1 or next(iter(jumpers.values())) != boundary_sources:
            raise ValueError("Each world-event lane must share its own return jumper without mixing lanes or bypassing it.")
    world_ids.add(event_entry)
    world_board = project["boards"][bindings["Board"]]
    world_nodes = set(world_board.get("elements", []) or []) | set(world_board.get("branches", []) or [])
    if world_ids | display_ids | inventory_ids != world_nodes:
        raise ValueError("Every shared-board node must be reachable from the interaction menu or its two query entries.")

    feedback_nodes = {
        event_entry: "The shared event entry",
        bindings["DuplicatePickupElement"]: "Duplicate-pickup feedback",
        bindings["PickupTerminalRequiredElement"]: "Task-required pickup feedback",
    }
    for ident, label in feedback_nodes.items():
        if ident != event_entry and edges[ident] != [event_entry]:
            raise ValueError(f"{label} must continue directly to the interaction menu.")
        validate_feedback_content(element_content(project, ident), label)

    for ident in world_ids | display_ids | inventory_ids:
        if ident in project["elements"]:
            content = element_content(project, ident)
            parsed = CodeBlocks()
            parsed.feed(content or "")
            if not parsed.text.strip():
                raise ValueError("Executable elements need nonempty content for the Unreal plugin.")
    for ident in display_ids:
        if ident in project["branches"]:
            for condition_id in branch_conditions[ident]:
                script = project["conditions"][condition_id].get("script")
                if script:
                    try:
                        parsed = ast.parse(arcscript_expression(script).strip(), mode="eval")
                    except SyntaxError as error:
                        raise ValueError("Presentation conditions must be read-only expressions.") from error
                    if not read_expression(parsed):
                        raise ValueError("Presentation conditions cannot call functions or change state.")
        else:
            allowed_attributes = [display_marker] if ident == display_entry else []
            if (project["elements"][ident].get("attributes") or []) != allowed_attributes:
                raise ValueError("Presentation elements must not retain metadata besides the entry marker; use quest_ui component fields.")
            validate_display_content(element_content(project, ident), entry=ident == display_entry)

    for ident in world_ids:
        if ident in project["branches"]:
            for condition_id in branch_conditions[ident]:
                validate_state_names(project["conditions"][condition_id].get("script"))
        else:
            content = element_content(project, ident) or ""
            blocks = CodeBlocks()
            blocks.feed(content)
            for script in blocks.blocks:
                validate_state_names(script)
            # Count a rich-text reference as one expression token, while the
            # existing state-name checks retain their original text handling.
            blocks = CodeBlocks(mask_references=True)
            blocks.feed(content)
            for script in blocks.blocks:
                validate_world_code_block(script)


def make_import_project(localized, unreal_project):
    # The all-locales JSON export preserves translations but omits board geometry.
    # The Unreal export supplies the original layout for the web importer's graph.
    result = copy.deepcopy(localized)
    for collection in ("elements", "branches", "notes", "jumpers"):
        for ident, item in result[collection].items():
            for key in ("x", "y", "width", "height", "autoHeight"):
                if key in unreal_project[collection][ident]:
                    item[key] = unreal_project[collection][ident][key]
    return result


def sync_exports(root, token, project_hash=None):
    metadata_path = root / "Narrative/project.json"
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    bindings = json.loads((root / "Narrative/bindings.json").read_text(encoding="utf-8"))
    project_hash = project_hash or metadata["projectHash"]
    encoded_hash = urllib.parse.quote(project_hash, safe="")
    base_url = f"https://arcweave.com/api/v1/{encoded_hash}"
    opener = urllib.request.build_opener(NoRedirect())

    def download(export_format):
        request = urllib.request.Request(
            f"{base_url}/{export_format}",
            headers={"Authorization": f"Bearer {token}", "Accept": "application/json"},
        )
        with opener.open(request, timeout=30) as response:
            return response.read()

    # Fetch and validate every export before replacing any bundled file.
    unreal_bytes = download("unreal")
    authoring_bytes = download("json")
    localized_bytes = download("json?allLocales=true")
    unreal = json.loads(unreal_bytes)
    authoring = json.loads(authoring_bytes)
    import_project = make_import_project(json.loads(localized_bytes), unreal["project"])
    validate_bindings(unreal["project"], bindings)
    validate_bindings(authoring, bindings)
    validate_bindings(import_project, bindings)
    import_bytes = (json.dumps(import_project, indent=2, ensure_ascii=False) + "\n").encode("utf-8")

    unreal_path = root / "Content/ArcweaveExport/quest.json"
    authoring_path = root / "Narrative/authoring.json"
    unreal_path.parent.mkdir(parents=True, exist_ok=True)
    unreal_path.write_bytes(unreal_bytes)
    authoring_path.write_bytes(authoring_bytes)
    (root / "Narrative/import.json").write_bytes(import_bytes)
    if project_hash != metadata["projectHash"]:
        # A copied project may live in a different workspace; the export does not identify it.
        metadata.pop("workspaceHash", None)
        metadata.pop("workspaceUrl", None)
    metadata["name"] = authoring["name"]
    metadata["projectHash"] = project_hash
    metadata["projectUrl"] = f"https://arcweave.com/app/project/{encoded_hash}"
    metadata["exportedAt"] = datetime.now(timezone.utc).isoformat()
    metadata["unrealExportUrl"] = f"{base_url}/unreal"
    metadata["authoringExportUrl"] = f"{base_url}/json"
    metadata["importExport"] = "import.json"
    metadata["importExportUrl"] = f"{base_url}/json?allLocales=true"
    metadata["unrealSha256"] = hashlib.sha256(unreal_bytes).hexdigest()
    metadata["authoringSha256"] = hashlib.sha256(authoring_bytes).hexdigest()
    metadata["importSha256"] = hashlib.sha256(import_bytes).hexdigest()
    metadata_path.write_text(
        json.dumps(metadata, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    print(f"Updated Unreal, authoring, and uploadable import exports for {metadata['projectHash']}.")
    print(metadata["projectUrl"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--token-file", required=True, type=Path,
        help="Path outside this repository to a file containing an Arcweave API key.",
    )
    parser.add_argument(
        "--project", help="Project hash of your imported copy; saved as the default after a successful sync.",
    )
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    token_path = args.token_file.expanduser().resolve()
    if token_path.is_relative_to(root):
        parser.error("Keep the token file outside this repository.")
    token = token_path.read_text(encoding="utf-8-sig").strip()
    sync_exports(root, token, args.project)


if __name__ == "__main__":
    try:
        main()
    except urllib.error.HTTPError as error:
        retry = error.headers.get("Retry-After")
        hint = f" Retry after {retry} seconds." if retry else ""
        print(f"Arcweave returned HTTP {error.code}.{hint} No exports were updated.", file=sys.stderr)
        sys.exit(1)
    except urllib.error.URLError:
        print("Could not reach the Arcweave API. No exports were updated.", file=sys.stderr)
        sys.exit(1)
    except (OSError, ValueError, KeyError) as error:
        print(f"Narrative sync failed: {error}", file=sys.stderr)
        sys.exit(1)
