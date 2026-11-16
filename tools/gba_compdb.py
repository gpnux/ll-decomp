#!/usr/bin/env python3
"""Generate clangd commands for the C units and include paths supplied by Make."""

import argparse
import json
from pathlib import Path
import shlex


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cppflags", required=True)
    parser.add_argument("--output", type=Path, default=Path("compile_commands.json"))
    parser.add_argument("sources", nargs="+", type=Path)
    args = parser.parse_args()
    root = Path.cwd()
    commands = []
    for source in sorted(set(args.sources)):
        if not source.is_file():
            parser.error(f"C source does not exist: {source}")
        commands.append({
            "directory": str(root),
            "file": str(source.resolve()),
            "arguments": [
                "clang", "--target=arm-none-eabi", "-mcpu=arm7tdmi", "-mthumb",
                "-std=gnu89", *shlex.split(args.cppflags), "-D__AGBCC__",
                # Keep agbcc-era pointer conversions visible as warnings.
                "-Wno-error=incompatible-pointer-types",
                "-c", str(source),
            ],
        })
    content = json.dumps(commands, ensure_ascii=False, indent=2) + "\n"
    if not args.output.exists() or args.output.read_text(encoding="utf-8") != content:
        args.output.write_text(content, encoding="utf-8")
    print(f"{args.output}: {len(commands)} C translation units")


if __name__ == "__main__":
    main()
