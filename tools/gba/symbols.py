"""Read address/mode hints from CFG and hand-written names from symbol files."""

from dataclasses import dataclass
from bisect import bisect_right
import re

from .config import require


IDENTIFIER = r"[A-Za-z_.$][A-Za-z_0-9.$]*"
IDENT = re.compile(IDENTIFIER)
CODE_HINTS = {"arm_func", "thumb_func", "arm_label", "thumb_label",
              "arm_far_jump", "thumb_far_jump"}
COUNTS = {"pool_label", "jump_table"}
REGISTERS = {f"r{i}" for i in range(16)} | {"sp", "lr", "pc", "ip", "sl", "fp"}
SYMBOL_LINE = re.compile(r"^(" + IDENTIFIER + r")\s*=\s*(0x[0-9a-fA-F]+|\d+)\s*;\s*(?://(.*))?$")


@dataclass(frozen=True)
class AddressSymbol:
    name: str
    address: int
    kind: str = "data"
    size: int | None = None
    isa: str | None = None

    @property
    def value(self):
        return self.address | (self.isa == "thumb")


class Symbols:
    def __init__(self, rows=()):
        self.by_name, self.by_address = {}, {}
        for symbol in rows:
            if symbol.name in self.by_name:
                require(self.by_name[symbol.name] == symbol, "Conflicting symbol: " + symbol.name)
                continue
            self.by_name[symbol.name] = symbol
            self.by_address.setdefault(symbol.address, []).append(symbol)
        self.starts = sorted(self.by_address)
        for address, aliases in self.by_address.items():
            require(len({s.kind == "func" for s in aliases}) == 1, f"Function/data alias conflict at 0x{address:08X}")
            require(len({s.size for s in aliases if s.size is not None}) <= 1,
                    f"Alias size conflict at 0x{address:08X}")
            require(len({s.isa for s in aliases if s.isa is not None}) <= 1,
                    f"Alias ISA conflict at 0x{address:08X}")

    def aliases(self, address):
        return self.by_address.get(address, [])

    def reference(self, value):
        for address in (value, value & ~1):
            for symbol in self.aliases(address):
                if symbol.value == value:
                    return symbol.name
        index = bisect_right(self.starts, value) - 1
        if index >= 0:
            start = self.starts[index]
            aliases = self.aliases(start)
            size = next((s.size for s in aliases if s.size is not None), 0)
            if aliases[0].kind != "func" and start < value < start + size:
                return f"{aliases[0].name} + 0x{value - start:X}"
        return None


def load_addresses(paths, hints=()):
    rows = []
    modes = {hint.address: "arm" if hint.command.startswith("arm_") else "thumb"
             for hint in hints if hint.command in CODE_HINTS}
    for path in paths:
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            text = line.strip()
            if not text or text.startswith(("//", "#")):
                continue
            where = f"{path}:{number}"
            match = SYMBOL_LINE.fullmatch(text)
            require(match is not None, "Invalid symbol definition at " + where)
            name, raw_address, comment = match.groups()
            require(name not in REGISTERS and name not in {".", ".."}, "Invalid symbol name at " + where)
            address = int(raw_address, 0)
            require(0 <= address <= 0xFFFFFFFF, "Symbol is outside 32-bit address space at " + where)
            attrs = dict(re.findall(r"\b(type|size|isa):(\S+)", comment or ""))
            kind = attrs.get("type", "data")
            require(kind in {"func", "data", "rodata", "bss", "label"}, "Unsupported symbol type at " + where)
            size = int(attrs["size"], 0) if "size" in attrs else None
            require(size is None or size > 0, "Invalid symbol size at " + where)
            isa = attrs.get("isa")
            if kind == "func":
                isa = isa or ("thumb" if address & 1 else modes.get(address))
                address &= ~1
                require(isa in {None, "thumb", "arm"}, "Invalid function ISA at " + where)
                require(address % (4 if isa == "arm" else 2) == 0, "Unaligned function at " + where)
            else:
                require(isa is None, "isa requires type:func at " + where)
            require(size is None or address + size <= 0x100000000, "Symbol size wraps address space at " + where)
            rows.append(AddressSymbol(name, address, kind, size, isa))
    return Symbols(rows)


def project_symbol_text(root, include_linker=True):
    """Read all configured symbol files, plus generated fixed-address bindings."""
    from .config import load as load_config
    project = load_config(root / "config/splat.ll.yaml")
    texts = [path.read_text(encoding="utf-8") for path in project.symbol_paths]
    image = project.output / "linker.ld"
    if include_linker and image.is_file():
        texts.append(image.read_text(encoding="utf-8"))
    return "\n".join(texts)


@dataclass(frozen=True)
class Hint:
    command: str
    address: int
    count: int | None
    forced: bool
    line: int


def load(path, base, rom_size):
    hints = []
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        # main.c supports comment lines; inline comments are rejected rather than misread as 'f'.
        text = line.strip()
        if not text or text.startswith("#"):
            continue
        where = f"{path}:{number}"
        tokens = text.split()
        command = tokens[0]
        require(command in CODE_HINTS | COUNTS | {"data_label"}, "Unknown CFG command at " + where)
        require(len(tokens) >= 2, "Missing CFG address at " + where)
        try:
            address = int(tokens[1], 0)
        except ValueError:
            raise ValueError("Invalid CFG address at " + where) from None
        require(base <= address < base + rom_size, "CFG address outside ROM at " + where)
        count, forced = None, False
        if command in CODE_HINTS:
            require(len(tokens) == 2 or
                    (command.endswith("_func") and len(tokens) == 3 and tokens[2] == "f"),
                    "CFG code hints accept address and optional 'f'; put names in symbols.ll.txt at " + where)
            alignment = 4 if command.startswith("arm_") else 2
            require(address % alignment == 0, "Unaligned code hint at " + where)
            forced = len(tokens) == 3
        elif command in COUNTS:
            require(len(tokens) == 3 and address % 4 == 0, "Invalid word hint at " + where)
            try:
                count = int(tokens[2], 0)
            except ValueError:
                raise ValueError("Invalid CFG count at " + where) from None
            require(count > 0 and address + count * 4 <= base + rom_size,
                    "Word hint extends outside ROM at " + where)
        else:
            require(len(tokens) == 2, "Invalid data_label at " + where)
        hints.append(Hint(command, address, count, forced, number))
    require(any(h.command in CODE_HINTS for h in hints), "CFG needs at least one code hint")
    return tuple(hints)
