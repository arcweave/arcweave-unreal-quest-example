#!/usr/bin/env python3
"""Download this sample's Arcweave exports without storing API credentials."""

import argparse
import hashlib
import json
import sys
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timezone
from pathlib import Path


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, response, code, message, headers, new_url):
        return None


def validate_bindings(project, bindings):
    collections = {
        "Board": "boards",
        "StartElement": "elements",
        "GeneratorElement": "elements",
        "SuccessElement": "elements",
        "MissingCellsElement": "elements",
        "PowerBranch": "branches",
        "PowerCondition": "conditions",
        "ElseCondition": "conditions",
        "StartConnection": "connections",
        "GeneratorConnection": "connections",
        "SuccessConnection": "connections",
        "MissingConnection": "connections",
        "PowerCellsVariable": "variables",
        "QuestStartedVariable": "variables",
        "PowerRestoredVariable": "variables",
        "RestorePowerComponent": "components",
        "OpenGateComponent": "components",
    }
    for name, collection in collections.items():
        if bindings[name] not in project[collection]:
            raise ValueError(f"The export is missing the {name} binding.")

    for name, custom_id in (
        ("RestorePowerComponent", "restore_power"),
        ("OpenGateComponent", "open_gate"),
    ):
        if project["components"][bindings[name]]["customId"] != custom_id:
            raise ValueError(f"The {name} custom ID no longer matches C++.")


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
