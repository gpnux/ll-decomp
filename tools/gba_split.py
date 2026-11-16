#!/usr/bin/env python3
"""Split a GBA ROM using a project YAML layout and a native gbadisasm CFG."""

import argparse
from pathlib import Path
import sys

from gba.analysis import Analysis
from gba.config import load
from gba.emit import split


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("config", nargs="?", type=Path, default=Path("config/splat.ll.yaml"), help="Project YAML layout (default: config/splat.ll.yaml)")
    parser.add_argument("--output", help="Output relative to the project root: asm/ or under build/")
    args = parser.parse_args()
    try:
        project = load(args.config, args.output)
        manifest = split(Analysis(project))
        print(f"Split {len(project.rom):#x} bytes: {len(manifest['chunks'])} segments, "
              f"{len(manifest['functions'])} marked entries, {len(manifest['data'])} data slices")
        classification = manifest["diagnostics"]["classification"]
        print(f"C source classification: {classification['matchings']} in matchings, "
              f"{classification['nonmatchings']} in nonmatchings")
        print("Assembly written to:", project.output)
    except (ValueError, OSError, KeyError, TypeError) as error:
        print("gba_split: " + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
