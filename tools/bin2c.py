#!/usr/bin/env python3
"""Convert a binary file to C initializer values or a named constant array."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys
from typing import Sequence


_TYPES = {
    "u8": (1, False, "unsigned char"),
    "u16": (2, False, "unsigned short"),
    "u32": (4, False, "unsigned int"),
    "s8": (1, True, "signed char"),
    "s16": (2, True, "signed short"),
    "s32": (4, True, "signed int"),
}
_C_KEYWORDS = frozenset(
    """
    auto break case char const continue default do double else enum extern float
    for goto if inline int long register restrict return short signed sizeof
    static struct switch typedef union unsigned void volatile while
    _Alignas _Alignof _Atomic _Bool _Complex _Generic _Imaginary _Noreturn
    _Static_assert _Thread_local alignas alignof bool constexpr false nullptr
    static_assert thread_local true typeof typeof_unqual
    asm __asm __asm__ __attribute __attribute__
    """.split()
)


def format_binary(
    data: bytes, name: str | None = None, c_type: str = "u8"
) -> str:
    """Format little-endian data as C text, with sixteen bytes per output row.

    Without ``name``, return values suitable for an include inside braces.
    With ``name``, return a complete ``const`` array declaration. Empty data is
    valid for a fragment, but cannot declare a standard C array.
    """
    if not isinstance(c_type, str) or c_type not in _TYPES:
        raise ValueError(f"unsupported type {c_type!r}; choose {', '.join(_TYPES)}")
    if not isinstance(data, (bytes, bytearray, memoryview)):
        raise TypeError("data must be bytes-like")
    data = bytes(data)
    if name is not None:
        if (
            not isinstance(name, str)
            or re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name) is None
            or name in _C_KEYWORDS
        ):
            raise ValueError(f"invalid C array name: {name!r}")
        if not data:
            raise ValueError("cannot declare an empty array in standard C")

    width, signed, declaration = _TYPES[c_type]
    if len(data) % width:
        raise ValueError(
            f"input size {len(data)} is not divisible by {width} for {c_type}"
        )

    rows = []
    if name is not None:
        rows.append(f"const {declaration} {name}[] = {{\n")
    for row_start in range(0, len(data), 16):
        values = []
        for offset in range(row_start, min(row_start + 16, len(data)), width):
            value = int.from_bytes(data[offset : offset + width], "little", signed=signed)
            if signed:
                # Avoid the out-of-range positive token in -2147483648 on C90.
                literal = "(-2147483647 - 1)" if value == -(1 << 31) else str(value)
            else:
                literal = f"0x{value:0{width * 2}X}"
            values.append(literal)
        rows.append("    " + ", ".join(values) + ",\n")
    if name is not None:
        rows.append("};\n")
    return "".join(rows)


def _check_output_path(input_path: Path, output_path: Path) -> None:
    if input_path.resolve() == output_path.resolve():
        raise ValueError("output must not overwrite the input file")
    try:
        same_file = input_path.samefile(output_path)
    except FileNotFoundError:
        same_file = False
    if same_file:
        raise ValueError("output must not overwrite the input file")


def _write_if_changed(path: Path, text: str) -> None:
    content = text.encode("ascii")
    try:
        if path.read_bytes() == content:
            return
    except FileNotFoundError:
        pass
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(content)


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="binary input path")
    parser.add_argument("output", help="C output path, or '-' for stdout")
    parser.add_argument("-n", "--name", help="emit a complete array with this C name")
    parser.add_argument("--type", dest="c_type", choices=_TYPES, default="u8")
    args = parser.parse_args(argv)
    try:
        output_path = None if args.output == "-" else Path(args.output)
        if output_path is not None:
            _check_output_path(args.input, output_path)
        text = format_binary(args.input.read_bytes(), args.name, args.c_type)
        if output_path is None:
            sys.stdout.write(text)
        else:
            _write_if_changed(output_path, text)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    sys.exit(main())
