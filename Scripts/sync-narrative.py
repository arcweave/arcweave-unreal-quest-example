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


CATALOG_FIELDS = {
    "brand", "station_name", "mission_tagline", "cells_label", "station_footer",
    "terminal_label", "cell_a_label", "cell_b_label", "sign_station",
    "sign_distribution", "sign_gate", "sign_exit",
}
DISPLAY_FIELDS = {
    "mission_heading", "grid_status", "terminal_prompt", "cell_prompt",
    "generator_prompt", "generator_label", "gate_label",
}
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


def read_expression(node):
    # Presentation supports a deliberately small, side-effect-free expression subset.
    allowed = (
        ast.Expression, ast.Name, ast.Load, ast.Constant, ast.BinOp, ast.UnaryOp,
        ast.BoolOp, ast.Compare, ast.Add, ast.Sub, ast.Mult, ast.Div, ast.Mod,
        ast.UAdd, ast.USub, ast.Not, ast.And, ast.Or, ast.Eq, ast.NotEq,
        ast.Lt, ast.LtE, ast.Gt, ast.GtE,
    )
    names = {item[0] for item in VARIABLES.values()} | {"true", "false"}
    return all(isinstance(part, allowed) and (not isinstance(part, ast.Name) or part.id in names)
               for part in ast.walk(node))


def validate_display_content(content):
    blocks = CodeBlocks()
    blocks.feed(content or "")
    for script in blocks.blocks:
        try:
            statements = ast.parse(script).body
        except SyntaxError as error:
            raise ValueError("Display content must contain only text and show() calls.") from error
        for statement in statements:
            call = statement.value if isinstance(statement, ast.Expr) else None
            if not (
                isinstance(call, ast.Call) and isinstance(call.func, ast.Name)
                and call.func.id == "show" and not call.keywords
                and all(read_expression(argument) for argument in call.args)
            ):
                raise ValueError("Display content cannot assign state or call functions other than show().")


def element_content(project, element_id):
    element = project["elements"][element_id]
    if "content" in element:
        return element["content"]
    # The reproducible import keeps the API's all-locales representation.
    locale = next((item["iso"] for item in project.get("locales", []) if item.get("base") is None), "en")
    contents = project.get("contents", {}).get(element_id, {})
    return contents.get("content", {}).get(locale, {}).get("text")


def validate_metadata(project, element_id, fields):
    element = project["elements"][element_id]
    attributes = [project["attributes"][key] for key in element.get("attributes", [])]
    if len(attributes) != len(fields) or {item["name"] for item in attributes} != fields:
        raise ValueError("A display or catalog element is missing its required named metadata.")
    for attribute in attributes:
        value = attribute["value"]
        text = value.get("data")
        if (
            attribute.get("customId") or attribute["cType"] != "elements"
            or attribute["cId"] != element_id or value["type"] != "string"
            or value.get("plain") is not True or not isinstance(text, str) or not text.strip()
            or re.search(r"<[^>]+>|\$\{|\bshow\s*\(", text)
        ):
            raise ValueError("Display metadata must be nonempty plain strings without HTML, Arcscript, or custom IDs.")


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
    for attribute in project["attributes"].values():
        if attribute.get("cType") in {"boards", "components"}:
            value = attribute["value"]
            if value["type"] in {"boolean", "integer", "float"} or (value["type"] == "string" and value.get("plain")):
                raise ValueError("UI copy must remain metadata, not additional scoped variables.")

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

    event_ids = {bindings[name] for name in EVENT_ENTRIES}
    for source in event_ids:
        pending = list(edges[source])
        while pending:
            current = pending.pop()
            if current in event_ids:
                raise ValueError("Separate world-event entry points must not execute one another automatically.")
            if current == bindings["PickupActionElement"] and source != bindings["PickupEntryElement"]:
                raise ValueError("Only the pickup event supplies the physical identity needed by collect_cell.")
            pending.extend(edges[current])

    display_entry = bindings["PresentationEntryElement"]
    display_branch = bindings["PresentationBranch"]
    executable = set()
    pending = list(event_ids | {display_entry})
    while pending:
        ident = pending.pop()
        if ident in executable:
            continue
        executable.add(ident)
        pending.extend(edges[ident])
        if ident in project["elements"]:
            content = element_content(project, ident)
            parsed = CodeBlocks()
            parsed.feed(content or "")
            if not parsed.text.strip():
                raise ValueError("Executable elements need nonempty content for the released v2.1.0 plugin.")
    leaves = {bindings[name] for name in DISPLAY_LEAVES}
    if edges[display_entry] != [display_branch] or set(edges[display_branch]) != leaves:
        raise ValueError("Presentation must select one of its five bound display leaves.")
    for condition_id in branch_conditions[display_branch]:
        script = project["conditions"][condition_id].get("script")
        if script:
            expression = re.sub(r"!(?!=)", "not ", script).replace("&&", " and ").replace("||", " or ")
            try:
                parsed = ast.parse(expression.strip(), mode="eval")
            except SyntaxError as error:
                raise ValueError("Presentation conditions must be read-only expressions.") from error
            if not read_expression(parsed):
                raise ValueError("Presentation conditions cannot call functions or change state.")
    for ident in leaves | {display_entry}:
        element = project["elements"][ident]
        if element.get("components") or (ident in leaves and edges[ident]):
            raise ValueError("Presentation leaves must terminate without dispatching commands.")
        validate_display_content(element_content(project, ident))
    for ident in leaves:
        validate_metadata(project, ident, DISPLAY_FIELDS)
    catalog_id = bindings["TextCatalogElement"]
    if edges[catalog_id] or project["elements"][catalog_id].get("components"):
        raise ValueError("The text catalog must be metadata-only.")
    validate_metadata(project, catalog_id, CATALOG_FIELDS)


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
