"""Generate GUI configuration from the YAML layout; split reports are optional."""

import json
from pathlib import Path

from .config import CODE, load
from .emit import json_text


def generate_config(config_path, output=None):
    config_path = Path(config_path).resolve()
    if config_path.suffix == ".json":
        # Keep old explicit report invocations usable by existing analysis tools.
        manifest = json.loads(config_path.read_text(encoding="utf-8"))
        assembly = config_path.parent
        root = (assembly / manifest["project_root"]).resolve()
        units = manifest["chunks"]
        renames = manifest.get("unit_renames", {})
    else:
        project = load(config_path, output)
        assembly, root = project.output, project.root
        units = [dict(name=segment.name, type=segment.kind, source=segment.source)
                 for segment in project.segments if segment.kind in CODE]
        renames = {}
        report_path = assembly / "manifest.json"
        if report_path.is_file():
            try:
                report = json.loads(report_path.read_text(encoding="utf-8"))
                if isinstance(report, dict) and isinstance(report.get("unit_renames"), dict):
                    renames = report["unit_renames"]
            except (ValueError, OSError):
                pass
    path = root / "objdiff.json"
    document = json.loads(path.read_text(encoding="utf-8")) if path.exists() else {}
    previous = {unit["name"]: unit for unit in document.get("units", [])}
    document.update(custom_make="make", build_base=True, build_target=True,
                    watch_patterns=["src/**/*.c", "src/**/*.s", "include/**/*.h", "asm/**/*.s",
                                    "asm/**/*.inc", "config/*.yaml", "config/*.cfg", "Makefile"], units=[])
    for unit in units:
        if unit["type"] not in CODE:
            continue
        row = dict(previous.get(unit["name"], previous.get(renames.get(unit["name"]), {})))
        metadata = dict(row.get("metadata", {}))
        metadata.setdefault("complete", False)
        source = next((root / "src" / (unit["name"] + suffix) for suffix in (".c", ".s")
                       if (root / "src" / (unit["name"] + suffix)).is_file()), assembly / unit["source"])
        metadata["source_path"] = source.relative_to(root).as_posix()
        row.update(name=unit["name"], target_path="expected/" + unit["name"] + ".o",
                   base_path="build/" + unit["name"] + ".o", metadata=metadata)
        document["units"].append(row)
    text = json_text(document)
    if not path.exists() or path.read_text(encoding="utf-8") != text:
        path.write_text(text, encoding="utf-8")
    return document
