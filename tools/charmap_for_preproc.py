#!/usr/bin/env python3
"""Generate a stock tools/preproc charmap from a lossless decoding charmap.

preproc accepts @ comments, single Unicode characters, and named constants.
Use the first encoding of each character for ordinary text; retain alternative
encodings and multi-character tiles as {GLYPH_HEXBYTES} constants.
"""

import argparse
import ast
from pathlib import Path
import re
import struct

CHARMAP_FILES = {
    "menu": "charmap.txt",
    "script": "charmap_script.txt",
    "title": "charmap_title.txt",
    "menu_tiles": "charmap_menu_tiles.txt",
}
BATTLE_CONTROLS = {
    0x06E0: "PARTY_NAME", 0x06E1: "CARD_NAME", 0x06E3: "SKILL_NAME",
    0x06E4: "NUMBER", 0x06E5: "ITEM_COUNT", 0x06E6: "ITEM_NAME",
    0x0800: "PAGE_BREAK", 0x0F00: "TEXT_END",
}


class Charmap:
    """Read the source mappings needed by the ROM build."""

    def __init__(self, path):
        self.path = Path(path)
        self.mapping = {}
        for number, line in enumerate(self.path.read_text(encoding="utf-8").splitlines(), 1):
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            match = re.fullmatch(r"\s*('(?:[^'\\]|\\.)*')\s*=\s*([0-9A-Fa-f ]+)\s*", line)
            if not match:
                raise ValueError(f"{self.path}:{number}: invalid charmap entry")
            text = ast.literal_eval(match[1])
            raw = bytes.fromhex(match[2])
            if not raw or not text or raw in self.mapping:
                raise ValueError(f"{self.path}:{number}: empty or duplicate byte mapping")
            self.mapping[raw] = text


def project_mappings(root):
    maps = {name: Charmap(Path(root) / filename).mapping for name, filename in CHARMAP_FILES.items()}
    # Static u16 name/battle tables store atlas ids without dialogue's 1000 bit.
    maps["words"] = {struct.pack("<H", int.from_bytes(raw, "little") & ~0x1000): text
                     for raw, text in maps["script"].items()}
    return {name: maps[name] for name in ("menu", "words", "title", "menu_tiles")}


def named_constants(profile, mapping):
    constants = {f"GLYPH_{raw.hex().upper()}": raw for raw in mapping}
    if profile in ("menu", "title"):
        constants["END"] = b"\xff"
    elif profile == "words":
        for word, name in BATTLE_CONTROLS.items():
            constants["END" if name == "TEXT_END" else name] = struct.pack("<H", word)
    elif profile == "menu_tiles":
        constants["BLANK"] = b"\x3f"
    for raw, text in mapping.items():
        if text.startswith("<ICON:") and text.endswith(">"):
            constants["ICON_" + text[6:-1]] = raw
        elif len(text) > 1 and text.isascii() and text.isidentifier():
            constants[text] = raw
    return constants


def write_mapping(mapping, destination, description, constants=None):
    seen = set()
    lines = [f"@ Generated from {description}; edit the source charmap.",
             "@ Alternative encodings use {GLYPH_HEXBYTES} in preproc strings.", ""]
    emitted = set()
    for raw, text in mapping.items():
        if len(text) == 1 and text not in seen:
            seen.add(text)
            quoted = text.replace("\\", "\\\\").replace("'", "\\'")
            lines.append(f"'{quoted}' = {raw.hex(' ').upper()}")
        else:
            name = f"GLYPH_{raw.hex().upper()}"
            lines.append(f"{name} = {raw.hex(' ').upper()} @ {text}")
            emitted.add(name)
    for name, raw in (constants or {}).items():
        if name not in emitted:
            lines.append(f"{name} = {raw.hex(' ').upper()}")
    destination = Path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text("\n".join(lines) + "\n", encoding="utf-8")


def convert(source, destination):
    write_mapping(Charmap(source).mapping, destination, Path(source).name)


def build_project_maps(destination, root="."):
    for profile, mapping in project_mappings(root).items():
        source = CHARMAP_FILES["script" if profile == "words" else profile]
        write_mapping(mapping, Path(destination) / f"{profile}.txt", source,
                      named_constants(profile, mapping))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, nargs="?")
    parser.add_argument("destination", type=Path, nargs="?")
    parser.add_argument("--all", type=Path, metavar="DIRECTORY", help="build all four project encoding profiles")
    args = parser.parse_args()
    if args.all is not None:
        if args.source is not None or args.destination is not None:
            parser.error("--all cannot be combined with source/destination")
        build_project_maps(args.all)
    elif args.source is None or args.destination is None:
        parser.error("provide source and destination, or --all DIRECTORY")
    else:
        convert(args.source, args.destination)


if __name__ == "__main__":
    main()
