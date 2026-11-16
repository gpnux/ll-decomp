"""Project layout, separate from gbadisasm's instruction and label hints."""

from dataclasses import dataclass, field
import hashlib
from pathlib import Path
import re

import yaml


CODE = {"c", "asm"}
GRAPHICS = {"rodata", "bin", "4bpp", "8bpp", "tilemap"}
RESOURCE = GRAPHICS | {"sound"}
KINDS = CODE | RESOURCE
PARENTS = {"code": CODE, "gfx": GRAPHICS, "sound": {"sound", "rodata", "bin"}}
COMPRESSION = {"lz", "rle", "rlegba", "huffman"}
NAME = re.compile(r"[a-z][a-z0-9_]*(?:/[a-z][a-z0-9_]*)*")


class LayoutLoader(yaml.SafeLoader):
    """Reject duplicate YAML keys instead of silently replacing layout fields."""


def unique_mapping(loader, node, deep=False):
    mapping = {}
    for key_node, value_node in node.value:
        key = loader.construct_object(key_node, deep=deep)
        require(isinstance(key, str), "YAML mapping keys must be strings")
        require(key not in mapping, f"Duplicate YAML key '{key}' on line {key_node.start_mark.line + 1}")
        mapping[key] = loader.construct_object(value_node, deep=deep)
    return mapping


LayoutLoader.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, unique_mapping)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def contained_path(root, name):
    require(isinstance(name, str) and name, "Expected a nonempty relative path")
    require(not Path(name).is_absolute(), "Expected a relative path: " + name)
    path = (root / name).resolve()
    require(path.is_relative_to(root), "Path escapes project root: " + name)
    return path


def output_path(root, name):
    path = contained_path(root, name)
    require(path == root / "asm" or (path != root / "build" and path.is_relative_to(root / "build")),
            "Output must be asm/ or a subdirectory of build/")
    return path


@dataclass(frozen=True)
class Segment:
    start: int
    size: int
    address: int
    kind: str
    name: str
    options: dict = field(default_factory=dict)
    namespace: str = "code"
    subalign: int = 1
    rodata_owner: str | None = None
    rodata_section: str | None = None

    @property
    def end(self):
        return self.address + self.size

    @property
    def section(self):
        return f".gba.{self.start:08X}.{'text' if self.kind in CODE else 'rodata'}"

    @property
    def source(self):
        if self.rodata_owner is not None:
            return "data/" + self.rodata_owner + ".rodata.s"
        return ("obj/" + self.name if self.kind in CODE else "data/" + self.key) + ".s"

    @property
    def key(self):
        return self.namespace + "/" + self.name

    @property
    def unit(self):
        return self.name if self.kind in CODE else self.key

    @property
    def object(self):
        if self.rodata_owner is not None:
            return "build/" + self.rodata_owner + ".o"
        return ("build/" if self.kind in CODE else "build/data/") + self.unit + ".o"


@dataclass(frozen=True)
class Config:
    path: Path
    root: Path
    target: Path
    output: Path
    hints: Path
    disassembler: Path
    base: int
    code_end: int
    segments: tuple[Segment, ...]
    rom: bytes
    fingerprints: dict[str, str]
    name: str
    symbol_paths: tuple[Path, ...] = ()


def subsegment_row(child, namespace):
    if isinstance(child, list):
        require(3 <= len(child) <= 5,
                "Subsegment list must be [start, type, name, compress?, width?]")
        row = dict(zip(("start", "type", "name", "compress", "width"), child))
    else:
        require(isinstance(child, dict) and {"start", "type", "name"} <= set(child),
                "Subsegment must contain start, type, name")
        row = dict(child)
    kind, name = row["type"], row["name"]
    require(isinstance(kind, str) and kind in PARENTS[namespace],
            "Unsupported " + namespace + " subsegment type: " + str(kind))
    require(isinstance(name, str) and NAME.fullmatch(name), "Invalid segment name: " + str(name))
    allowed = {
        "sound": {"song_table", "song_count", "mplay_table", "mplay_count", "voicegroup_end",
                  "resource_start", "resource_end", "resource_name"},
        "4bpp": {"compress", "width", "palette", "tilemap"},
        "8bpp": {"compress", "width", "palette", "tilemap"},
        "tilemap": {"compress", "width", "layout", "format"},
        "bin": {"compress"}, "rodata": {"compress"},
    }.get(kind, set())
    extras = set(row) - {"start", "type", "name"}
    require(extras <= allowed, "Invalid options for " + kind + " subsegment: " + name)
    compression = row.get("compress")
    require(compression is None or isinstance(compression, str) and compression in COMPRESSION,
            "compress must be lz, rle, rlegba, huffman or null: " + name)
    if compression is None:
        row.pop("compress", None)
    elif compression == "rlegba":
        row["compress"] = "rle"
    if kind == "sound":
        require("song_table" in row and "song_count" in row, "sound needs song_table and song_count: " + name)
        for key in extras:
            if key == "resource_name":
                require(isinstance(row[key], str) and NAME.fullmatch(row[key]), "Invalid sound resource_name: " + name)
            else:
                require(type(row[key]) is int and row[key] >= 0, "Invalid sound " + key + ": " + name)
        require(row["song_count"] > 0, "song_count must be positive: " + name)
        require(("mplay_table" in row) == ("mplay_count" in row), "mplay_table and mplay_count must be paired")
        require(row.get("mplay_count", 1) > 0, "mplay_count must be positive: " + name)
    if "width" in row:
        require(type(row["width"]) is int and row["width"] > 0, "width must be positive: " + name)
        if kind in {"4bpp", "8bpp"}:
            require(row["width"] % 8 == 0, kind + " width must be a multiple of 8 pixels: " + name)
    if "palette" in row:
        require(type(row["palette"]) is int or isinstance(row["palette"], (str, list)), "Invalid palette: " + name)
    if "tilemap" in row:
        require(isinstance(row["tilemap"], str) and NAME.fullmatch(row["tilemap"]),
                "tilemap must name a tilemap subsegment: " + name)
    if kind == "tilemap":
        require(row.get("format", "text") in {"text", "affine"}, "tilemap format must be text or affine: " + name)
        layout = row.get("layout", "linear" if row.get("format") == "affine" else "screenblocks")
        require(layout in {"linear", "screenblocks"}, "tilemap layout must be linear or screenblocks: " + name)
        require(row.get("format") != "affine" or layout == "linear", "affine tilemaps require linear layout: " + name)
    return row


def load(path, output=None):
    path = Path(path).resolve()
    try:
        document = yaml.load(path.read_text(encoding="utf-8"), Loader=LayoutLoader)
    except yaml.YAMLError as error:
        raise ValueError(f"Invalid YAML in {path}: {error}") from None
    require(isinstance(document, dict), "YAML project configuration must be a mapping")
    require(not set(document) - {"sha1", "options", "segments"}, "Unknown top-level option")
    options = document.get("options", {})
    allowed = {"platform", "base_path", "target_path", "output_path", "gbadisasm_config", "symbol_addrs_path"}
    require(isinstance(options, dict) and not set(options) - allowed, "Unsupported project option")
    require(options.get("platform") == "gba", "Only platform = 'gba' is supported")
    root = (path.parent / options.get("base_path", ".")).resolve()
    require(path.is_relative_to(root), "Project config must be inside the project root")
    target = contained_path(root, options["target_path"])
    hints = contained_path(root, options["gbadisasm_config"])
    symbol_names = options.get("symbol_addrs_path", [])
    if isinstance(symbol_names, str):
        symbol_names = [symbol_names]
    require(isinstance(symbol_names, list), "symbol_addrs_path must be a path or list of paths")
    symbol_paths = tuple(contained_path(root, name) for name in symbol_names)
    disassembler = root / "tools/gbadisasm/gbadisasm"
    destination = output_path(root, output or options.get("output_path", "asm"))
    rom = target.read_bytes()
    require(rom and len(rom) <= 0x2000000 and len(rom) % 2 == 0,
            "GBA ROM must be nonempty, halfword-aligned, and at most 32 MiB")
    require(document.get("sha1") == hashlib.sha1(rom).hexdigest(), "ROM SHA1 does not match config")
    parents = document.get("segments")
    require(isinstance(parents, list) and len(parents) >= 2, "Expected parent segments and an EOF marker")
    require(parents[-1] == [len(rom)], "ROM EOF marker does not match file size")
    groups = parents[:-1]
    group_starts = []
    for parent in groups:
        require(isinstance(parent, dict) and not set(parent) -
                {"name", "type", "start", "vram", "align", "subalign", "subsegments"},
                "Unsupported parent segment field")
        require(isinstance(parent.get("type"), str) and parent["type"] in PARENTS,
                "Parent type must be code, sound or gfx")
        require(isinstance(parent.get("name"), str) and NAME.fullmatch(parent["name"]), "Invalid parent name")
        require(type(parent.get("start")) is int and 0 <= parent["start"] < len(rom),
                "Parent start is outside ROM: " + parent["name"])
        group_starts.append(parent["start"])
    require(group_starts[0] == 0 and group_starts == sorted(set(group_starts)),
            "Parent segments must start at zero and be strictly ordered")
    require(groups[0]["type"] == "code", "Expected a code parent starting at zero")
    base = groups[0].get("vram")
    require(type(base) is int and base == 0x08000000, "GBA ROM base must be 0x08000000")
    code_end = base + next((parent["start"] for parent in groups if parent["type"] != "code"), len(rom))
    require(code_end > base and code_end % 2 == 0, "Code/resource boundary must be halfword-aligned")
    rows, starts, names = [], [], {}
    for index, parent in enumerate(groups):
        group_start = parent["start"]
        group_end = group_starts[index + 1] if index + 1 < len(groups) else len(rom)
        namespace = parent["type"]
        require(parent.get("vram", base + group_start) == base + group_start,
                "Parent VMA must follow ROM offsets: " + parent["name"])
        for key in ("align", "subalign"):
            value = parent.get(key, 1)
            require(type(value) is int and value > 0 and value & (value - 1) == 0,
                    key + " must be a positive power of two: " + parent["name"])
        require(namespace != "code" or base + group_end <= code_end, "Code parents must precede resource parents")
        children = parent.get("subsegments")
        require(isinstance(children, list) and len(children) >= 2 and children[-1] == [group_end],
                f"Subsegments of {parent['name']} need end marker [0x{group_end:X}]")
        local_starts = []
        for child in children[:-1]:
            row = subsegment_row(child, namespace)
            start, kind, name = row.get("start"), row.get("type"), row.get("name")
            require(type(start) is int and group_start <= start < group_end,
                    "Subsegment is outside parent: " + parent["name"])
            previous_kind = names.get((namespace, name))
            require(previous_kind is None or previous_kind == kind == "rodata",
                    "Duplicate " + namespace + " name: " + name)
            rows.append((row, namespace, parent.get("subalign", 1)))
            starts.append(start)
            local_starts.append(start)
            names[namespace, name] = kind
        require(local_starts[0] == group_start and local_starts == sorted(set(local_starts)),
                "Subsegments must cover their parent in order: " + parent["name"])
    require(starts[0] == 0 and starts == sorted(set(starts)),
            "Segments must start at zero and be strictly ordered")
    segments = []
    code_names = {row["name"] for row, _, _ in rows if row["type"] in CODE}
    rodata_owners = set()
    standalone_rodata = set()
    for index, (row, namespace, subalign) in enumerate(rows):
        start = row["start"]
        end = starts[index + 1] if index + 1 < len(rows) else len(rom)
        address = base + start
        if row["type"] in CODE:
            require(start % 2 == 0 and end % 2 == 0 and base + end <= code_end,
                    "Code segment is unaligned or extends past code_end: " + row["name"])
        else:
            require(address >= code_end, "Data segment overlaps the disassembly: " + row["name"])
        if row["type"] in {"4bpp", "8bpp"} and not row.get("compress"):
            tile_size = 32 if row["type"] == "4bpp" else 64
            require((end - start) % tile_size == 0,
                    row["type"] + f" resource must contain whole {tile_size}-byte tiles: " + row["name"])
        if row["type"] == "sound":
            require(0 <= row.get("resource_start", start) <= start
                    and end <= row.get("resource_end", end) <= len(rom),
                    "Sound slice is outside its resource: " + row["name"])
        owner = row["name"] if row["type"] == "rodata" and row["name"] in code_names else None
        rodata_section = None
        if owner is not None:
            require(not row.get("compress"), "Module rodata cannot be compressed: " + owner)
            rodata_section = f".rodata.{start:08X}" if owner in rodata_owners else ".rodata"
            rodata_owners.add(owner)
        elif row["type"] == "rodata":
            key = namespace, row["name"]
            require(key not in standalone_rodata, "Duplicate " + namespace + " name: " + row["name"])
            standalone_rodata.add(key)
        segments.append(Segment(start, end - start, address, row["type"], row["name"],
                                {key: value for key, value in row.items() if key not in {"start", "type", "name"}},
                                namespace, subalign, owner, rodata_section))
    by_key = {segment.key: segment for segment in segments}
    for segment in segments:
        if "tilemap" in segment.options:
            target_map = by_key.get(segment.namespace + "/" + segment.options["tilemap"])
            require(target_map is not None and target_map.kind == "tilemap",
                    "Unknown tilemap for " + segment.key + ": " + segment.options["tilemap"])
            require(segment.kind == "8bpp" or target_map.options.get("format") != "affine",
                    "Affine tilemap needs 8bpp tiles: " + segment.key)
    require(any(s.kind in CODE and s.end == code_end for s in segments),
            "Code segments must cover exactly up to code_end")
    inputs = [path, target, hints, *symbol_paths]
    inputs += [contained_path(root, segment.options["palette"]) for segment in segments
               if isinstance(segment.options.get("palette"), str)]
    fingerprints = {str(p.relative_to(root)): digest(p.read_bytes()) for p in inputs}
    config = Config(path, root, target, destination, hints, disassembler,
                    base, code_end, tuple(segments), rom, fingerprints, groups[0]["name"], symbol_paths)
    return config
