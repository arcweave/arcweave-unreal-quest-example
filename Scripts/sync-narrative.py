#!/usr/bin/env python3
"""Download this sample's Arcweave exports without storing API credentials."""

import argparse
import ast
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
    "brand", "station_name", "mission_tagline", "cells_label", "station_footer",
}
WORLD_FIELDS = {
    "terminal_label", "cell_a_label", "cell_b_label", "sign_station",
    "sign_distribution", "sign_gate", "sign_exit",
}
DISPLAY_FIELDS = {
    "mission_heading", "grid_status", "terminal_prompt", "cell_prompt",
    "generator_prompt", "generator_label", "gate_label",
}
UI_COMPONENTS = {
    "HUDTextComponent": ("hud", HUD_FIELDS),
    "WorldTextComponent": ("world_text", WORLD_FIELDS),
    "QuestUIComponent": ("quest_ui", DISPLAY_FIELDS),
}
SCOPED_FIELDS = dict(UI_COMPONENTS.values())
VARIABLES = {
    "QuestStartedVariable": ("questStarted", "boolean", False),
    "PowerCellsVariable": ("powerCells", "integer", 0),
    "RequiredPowerCellsVariable": ("requiredPowerCells", "integer", None),
    "PowerRestoredVariable": ("powerRestored", "boolean", False),
    "QuestCompletedVariable": ("questCompleted", "boolean", False),
}
EVENT_ENTRIES = (
    "InitializationElement", "TerminalEntryElement", "PickupEntryElement",
    "DuplicatePickupElement", "GeneratorElement", "ExitEntryElement",
)
DISPLAY_LEAVES = tuple("Presentation" + state + "Element" for state in (
    "Completed", "Powered", "Unaccepted", "Collecting", "Ready",
))


class CodeBlocks(HTMLParser):
    def __init__(self):
        super().__init__()
        self.blocks = []
        self.in_code = False
        self.text = ""

    def handle_starttag(self, tag, attrs):
        if tag == "code":
            self.in_code = True
            self.blocks.append("")

    def handle_endtag(self, tag):
        if tag == "code":
            self.in_code = False

    def handle_data(self, data):
        self.text += data
        if self.in_code:
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
    names = {item[0] for item in VARIABLES.values()} | {"true", "false"}
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
    statements = []
    for script in blocks.blocks:
        try:
            parsed = ast.parse(arcscript_expression(script).strip()).body
        except SyntaxError as error:
            raise ValueError("Presentation scripts must use simple assignments, show(), or entry resets.") from error
        if len(parsed) != 1:
            raise ValueError("Each presentation code block must contain exactly one simple statement for the native plugin.")
        statements.extend(parsed)
    resets = []
    for index, statement in enumerate(statements):
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
    if entry and (len(resets) != len(DISPLAY_FIELDS) or set(resets) != DISPLAY_FIELDS):
        raise ValueError("Presentation entry must reset each of the seven quest_ui fields exactly once.")


def element_content(project, element_id):
    element = project["elements"][element_id]
    if "content" in element:
        return element["content"]
    # The reproducible import keeps the API's all-locales representation.
    locale = next((item["iso"] for item in project.get("locales", []) if item.get("base") is None), "en")
    contents = project.get("contents", {}).get(element_id, {})
    return contents.get("content", {}).get(locale, {}).get("text")


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
    if project["startingElement"] != bindings["InitializationElement"]:
        raise ValueError("The starting element must initialize the station without accepting the task.")

    for binding, (name, kind, default) in VARIABLES.items():
        variable = project["variables"][bindings[binding]]
        value = variable["value"]
        if variable["name"] != name or variable["type"] != kind or variable.get("cType") != "global":
            raise ValueError(f"The {name} variable no longer matches its runtime contract.")
        if type(value) is not (bool if kind == "boolean" else int):
            raise ValueError(f"The {name} initial value has the wrong type.")
        if (default is None and value < 1) or (default is not None and value != default):
            raise ValueError(f"The {name} initial value is not a valid new-game default.")
    variable_ids = {key for key, item in project["variables"].items()
                    if not item.get("root") and "children" not in item}
    if variable_ids != {bindings[name] for name in VARIABLES}:
        raise ValueError("The sample requires exactly five global variables.")
    ui_ids = {bindings[name] for name in UI_COMPONENTS}
    ui_attributes = {}
    for binding, (scope, fields) in UI_COMPONENTS.items():
        component_id = bindings[binding]
        validate_ui_component(project, component_id, scope, fields)
        for attribute_id in project["components"][component_id]["attributes"]:
            ui_attributes[attribute_id] = component_id
    for attribute_id, attribute in project["attributes"].items():
        if attribute.get("cType") in {"boards", "components"}:
            value = attribute["value"]
            if value["type"] in {"boolean", "integer", "float"} or (value["type"] == "string" and value.get("plain")):
                if (attribute_id not in ui_attributes or attribute["cType"] != "components"
                        or attribute.get("cId") != ui_attributes[attribute_id]):
                    raise ValueError("Only the nineteen UI string attributes may add scoped variables.")
    for component_id, component in project["components"].items():
        if component_id not in ui_ids and component.get("customId") in SCOPED_FIELDS:
            raise ValueError("UI data component scopes must be unique.")

    # Runtime exports flatten folders; validate organization when the tree is serialized.
    folders = [item for item in project["components"].values() if "children" in item]
    if folders:
        ui_folders = [item for item in folders if not item.get("root")
                      and set(item.get("children", [])) == ui_ids]
        memberships = [child for item in folders for child in item.get("children", []) if child in ui_ids]
        if len(ui_folders) != 1 or len(memberships) != len(ui_ids):
            raise ValueError("The UI folder must contain exactly the three data components together.")
        if ui_folders[0].get("customId") or ui_folders[0].get("attributes"):
            raise ValueError("The UI folder is organizational and must not define a runtime scope or attributes.")

    commands = {
        "RestorePowerComponent": "restore_power", "OpenGateComponent": "open_gate",
        "CollectCellComponent": "collect_cell",
    }
    for name, custom_id in commands.items():
        if project["components"][bindings[name]]["customId"] != custom_id:
            raise ValueError(f"The {name} custom ID no longer matches C++.")
    command_elements = {
        bindings["PickupActionElement"]: {bindings["CollectCellComponent"]},
        bindings["SuccessElement"]: {bindings["RestorePowerComponent"], bindings["OpenGateComponent"]},
    }

    # Model automatic execution through elements and every possible branch outcome.
    owners = {}
    for board_id, board in project["boards"].items():
        for collection in ("elements", "branches", "connections"):
            for ident in board.get(collection, []) or []:
                if ident in owners:
                    raise ValueError("An automatic graph object belongs to multiple boards.")
                owners[ident] = board_id
    edges = {ident: [] for collection in ("elements", "branches") for ident in project[collection]}
    used_connections = set()
    used_conditions = set()

    def edge(origin, source_id, source_type, connection_id):
        connection = project["connections"][connection_id]
        target = connection["targetid"]
        if (
            connection_id in used_connections or connection["sourceid"] != source_id
            or connection["sourceType"] != source_type
            or connection["targetType"] not in {"elements", "branches"}
            or target not in project[connection["targetType"]]
            or owners.get(origin) is None
            or owners.get(origin) != owners.get(target)
            or owners.get(origin) != owners.get(connection_id)
        ):
            raise ValueError("An automatic connection has an invalid source, target, or board.")
        used_connections.add(connection_id)
        edges[origin].append(target)

    for ident, element in project["elements"].items():
        outputs = element.get("outputs", []) or []
        if len(outputs) > 1:
            raise ValueError("Event elements must have at most one automatic output.")
        for output in outputs:
            edge(ident, ident, "elements", output)
        components = element.get("components", []) or []
        if ui_ids.intersection(components):
            raise ValueError("UI components must remain standalone data, not attached gameplay commands.")
        expected = command_elements.get(ident, set())
        if set(components) != expected or len(components) != len(expected):
            raise ValueError("Only PickupAction may collect a cell; only Success may restore power and open the gate.")
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

    visited, active = set(), set()
    def visit(ident):
        if ident in active:
            raise ValueError("An automatic event path contains a cycle.")
        if ident in visited:
            return
        active.add(ident)
        for target in edges[ident]:
            visit(target)
        active.remove(ident)
        visited.add(ident)
    for ident in edges:
        visit(ident)

    def reachable(starts):
        result = set()
        pending = list(starts)
        while pending:
            ident = pending.pop()
            if ident not in result:
                result.add(ident)
                pending.extend(edges[ident])
        return result

    display_entry = bindings["PresentationEntryElement"]
    display_ids = reachable({display_entry})
    required_display = {bindings[name] for name in (
        "PresentationBranch", "PresentationPoweredSetupElement", "PresentationCompletionBranch",
    )}
    if not required_display.issubset(display_ids):
        raise ValueError("Presentation must reach its shared power setup and bound state branches.")
    leaves = {bindings[name] for name in DISPLAY_LEAVES}
    terminal_ids = {ident for ident in display_ids if not edges[ident]}
    if terminal_ids != leaves:
        raise ValueError("Presentation must terminate at exactly its five bound display leaves.")
    if any(item.get("cType") == "elements" and item.get("cId") in display_ids
           for item in project["attributes"].values()):
        raise ValueError("Presentation elements must not retain metadata; use quest_ui component fields.")

    event_ids = {bindings[name] for name in EVENT_ENTRIES}
    for source in event_ids:
        for current in reachable(edges[source]):
            if current in event_ids:
                raise ValueError("Separate world-event entry points must not execute one another automatically.")
            if current in display_ids:
                raise ValueError("World events must not automatically enter the presentation graph.")
            if current == bindings["PickupActionElement"] and source != bindings["PickupEntryElement"]:
                raise ValueError("Only the pickup event supplies the physical identity needed by collect_cell.")
    if display_ids.intersection(reachable(event_ids)):
        raise ValueError("World-event and presentation graphs must execute separately.")

    for ident in reachable(event_ids | {display_entry}):
        if ident in project["elements"]:
            content = element_content(project, ident)
            parsed = CodeBlocks()
            parsed.feed(content or "")
            if not parsed.text.strip():
                raise ValueError("Executable elements need nonempty content for the released v2.1.0 plugin.")
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
            if project["elements"][ident].get("attributes"):
                raise ValueError("Presentation elements must not retain metadata; use quest_ui component fields.")
            validate_display_content(element_content(project, ident), entry=ident == display_entry)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--token-file", required=True, type=Path,
        help="Path outside this repository to a file containing an Arcweave API key.",
    )
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    token_path = args.token_file.expanduser().resolve()
    if token_path.is_relative_to(root):
        parser.error("Keep the token file outside this repository.")

    metadata_path = root / "Narrative/project.json"
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    bindings = json.loads((root / "Narrative/bindings.json").read_text(encoding="utf-8"))
    token = token_path.read_text(encoding="utf-8-sig").strip()
    project_hash = urllib.parse.quote(metadata["projectHash"], safe="")
    base_url = f"https://arcweave.com/api/v1/{project_hash}"
    opener = urllib.request.build_opener(NoRedirect())

    def download(export_format):
        request = urllib.request.Request(
            f"{base_url}/{export_format}",
            headers={"Authorization": f"Bearer {token}", "Accept": "application/json"},
        )
        with opener.open(request, timeout=30) as response:
            return response.read()

    # Fetch and validate both exports before replacing either bundled file.
    unreal_bytes = download("unreal")
    authoring_bytes = download("json")
    unreal = json.loads(unreal_bytes)
    authoring = json.loads(authoring_bytes)
    validate_bindings(unreal["project"], bindings)
    validate_bindings(authoring, bindings)

    unreal_path = root / "Content/ArcweaveExport/quest.json"
    authoring_path = root / "Narrative/authoring.json"
    unreal_path.parent.mkdir(parents=True, exist_ok=True)
    unreal_path.write_bytes(unreal_bytes)
    authoring_path.write_bytes(authoring_bytes)
    metadata["exportedAt"] = datetime.now(timezone.utc).isoformat()
    metadata["unrealExportUrl"] = f"{base_url}/unreal"
    metadata["authoringExportUrl"] = f"{base_url}/json"
    metadata["unrealSha256"] = hashlib.sha256(unreal_bytes).hexdigest()
    metadata["authoringSha256"] = hashlib.sha256(authoring_bytes).hexdigest()
    metadata_path.write_text(
        json.dumps(metadata, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    print(f"Updated Unreal and authoring exports for {metadata['projectHash']}.")
    print(metadata["projectUrl"])


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
