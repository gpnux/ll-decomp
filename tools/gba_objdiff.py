#!/usr/bin/env python3
"""Generate objdiff.json from the GBA YAML layout."""

import argparse
from pathlib import Path
import sys

from gba.objdiff import generate_config


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("config", nargs="?", type=Path, default=Path("config/splat.ll.yaml"),
                        help="YAML layout (legacy explicit manifest paths are also accepted)")
    parser.add_argument("--output", help="Assembly directory relative to the project root")
    args = parser.parse_args()
    try:
        document = generate_config(args.config, args.output)
        print(f"objdiff: {len(document['units'])} code modules configured")
    except (ValueError, OSError, KeyError, TypeError) as error:
        print("gba_objdiff: " + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
