"""Run gbadisasm and account for every byte in its assembly output."""

from dataclasses import dataclass
import re
import struct
import subprocess

from .config import require
from .symbols import IDENTIFIER, IDENT, CODE_HINTS, load as load_hints


MARKER = re.compile(r"(arm_func_start|thumb_func_start|non_word_aligned_thumb_func_start)\s+(" + IDENTIFIER + r")")
LABEL = re.compile(r"^(" + IDENTIFIER + r"):\s*(.*)$")
ADDRESS_LABEL = re.compile(r"_(0[89][0-9A-F]{6})")
WIDTHS = {".byte": 1, ".hword": 2, ".short": 2, ".word": 4, ".4byte": 4}


@dataclass(frozen=True)
class Entry:
    address: int
    name: str
    isa: str
    line: int


@dataclass(frozen=True)
class Symbol:
    address: int
    isa: str | None

    @property
    def value(self):
        return self.address | (self.isa == "thumb")


@dataclass(frozen=True)
class Node:
    address: int
    size: int
    kind: str
    text: str
    isa: str
    line: int
    name: str | None = None

    @property
    def end(self):
        return self.address + self.size


@dataclass(frozen=True)
class Disassembly:
    text: str
    nodes: tuple[Node, ...]
    entries: tuple[Entry, ...]
    symbols: dict[str, Symbol]


def render_hints(config, hints, address_symbols=None):
    """Merge structural hints with the only hand-written name source in memory."""
    names = {address: aliases[0].name for address, aliases in address_symbols.by_address.items()
             if aliases[0].kind == "func" and config.base <= address < config.code_end} if address_symbols else {}
    lines, addresses = [], set()
    for hint in hints:
        name = names.get(hint.address) if hint.command in CODE_HINTS else None
        line = f"{hint.command} 0x{hint.address:08X}"
        if hint.count is not None:
            line += " " + str(hint.count)
        else:
            # gbadisasm's native forced-function flag occupies its fourth token.
            if hint.forced and name is None:
                name = f"func_{hint.address:08X}"
            if name:
                line += " " + name
        if hint.forced:
            line += " f"
        lines.append(line)
        addresses.add(hint.address)
    if address_symbols:
        for address, aliases in address_symbols.by_address.items():
            symbol = aliases[0]
            if symbol.kind == "func" and config.base <= address < config.code_end and address not in addresses:
                require(symbol.isa in {"thumb", "arm"}, "New function needs isa:thumb or isa:arm: " + symbol.name)
                lines.append(f"{symbol.isa}_func 0x{address:08X} {symbol.name}")
    return "\n".join(lines) + "\n"


def run(config, hints=(), address_symbols=None):
    require(config.disassembler.is_file(),
            "gbadisasm is missing; run make -C tools/gbadisasm first")
    if not hints:
        hints = load_hints(config.hints, config.base, len(config.rom))
    hint_text = render_hints(config, hints, address_symbols)
    result = subprocess.run([str(config.disassembler), str(config.target), "-c", "-",
                             "-l", hex(config.base)], cwd=config.root, input=hint_text,
                            capture_output=True, text=True)
    require(result.returncode == 0, "gbadisasm failed:\n" + result.stderr.strip())
    require(not result.stderr.strip(), "gbadisasm reported diagnostics:\n" + result.stderr.strip())
    require(result.stdout.strip(), "gbadisasm returned empty output")
    return parse(result.stdout, config.base, config.code_end, config.rom)


def parse(text, base, code_end, rom):
    nodes, entries, symbols = [], [], {}
    pc, isa, pending = base, "arm", None
    for number, source in enumerate(text.splitlines(), 1):
        statement = source.split("@", 1)[0].strip()
        if not statement:
            continue
        where = f"gbadisasm line {number}, 0x{pc:08X}"
        marker = MARKER.fullmatch(statement)
        if marker:
            require(pending is None, "Unclosed function marker at " + where)
            macro, name = marker.groups()
            isa = "arm" if macro == "arm_func_start" else "thumb"
            require(pc % (4 if isa == "arm" else 2) == 0, "Unaligned entry at " + where)
            require(macro != "thumb_func_start" or pc % 4 == 0, "Incorrect Thumb marker at " + where)
            require(pc < code_end, "Entry beyond code_end at " + where)
            entries.append(Entry(pc, name, isa, number))
            pending = name
            nodes.append(Node(pc, 0, "entry", statement, isa, number, name))
            continue
        match = LABEL.fullmatch(statement)
        if match:
            name, statement = match.groups()
            require(name not in symbols, "Duplicate assembly symbol: " + name)
            symbol_isa = None
            if pending is not None:
                require(name == pending, "Function marker/label mismatch at " + where)
                symbol_isa, pending = isa, None
            annotation = re.search(r"@\s*0x([0-9A-Fa-f]{8})\b", source)
            encoded_address = ADDRESS_LABEL.fullmatch(name)
            if annotation:
                require(pc == int(annotation.group(1), 16), "Address annotation mismatch at " + where)
            if encoded_address:
                require(pc == int(encoded_address.group(1), 16), "Address label mismatch at " + where)
            symbols[name] = Symbol(pc, symbol_isa)
            nodes.append(Node(pc, 0, "label", name + ":", isa, number, name))
            if not statement:
                continue
        require(pending is None, "Missing entry label at " + where)
        mnemonic, *operands = statement.split(None, 1)
        if mnemonic == ".align":
            args = operands[0].replace(" ", "").split(",") if operands else []
            require(args in (["2", "0"], ["1", "0"], ["2"], ["1"]),
                    "Unsupported alignment at " + where)
            size = (-pc) % (1 << int(args[0]))
            require(rom[pc - base:pc - base + size] == b"\0" * size,
                    "Alignment does not describe zero padding at " + where)
            kind = "padding"
        elif mnemonic in WIDTHS:
            require(operands, "Missing data operands at " + where)
            values = operands[0].split(",")
            require(all(value.strip() for value in values), "Empty data operand at " + where)
            size, kind = WIDTHS[mnemonic] * len(values), "data"
        elif mnemonic.startswith("."):
            raise ValueError("Unsupported assembly directive at " + where + ": " + mnemonic)
        else:
            size, kind = (4 if isa == "arm" or mnemonic == "bl" else 2), "instruction"
        require(pc + size <= code_end, "Disassembly extends beyond code_end at " + where)
        # Keep gbadisasm's useful literal-value comments, but not its inline label a second time.
        comment = " @" + source.split("@", 1)[1] if "@" in source and not match else ""
        nodes.append(Node(pc, size, kind, statement + comment, isa, number))
        pc += size
    require(pending is None and entries, "Incomplete or empty gbadisasm output")
    require(pc == code_end, f"Disassembly ends at 0x{pc:08X}, expected 0x{code_end:08X}")
    require([e.address for e in entries] == sorted({e.address for e in entries}),
            "Entry markers overlap or are out of order")
    for node in nodes:
        if node.kind != "data":
            continue
        directive, operands = node.text.split("@", 1)[0].split(None, 1)
        width = WIDTHS[directive]
        values = []
        for token in operands.split(","):
            token = token.strip()
            if IDENT.fullmatch(token):
                require(token in symbols, "Undefined data reference: " + token)
                value = symbols[token].value
            else:
                value = int(token, 0)
            require(0 <= value < 1 << (width * 8), "Data value does not fit its directive")
            values.append(value)
        raw = b"".join(value.to_bytes(width, "little") for value in values)
        require(raw == rom[node.address - base:node.end - base],
                f"Data/Thumb pointer mismatch at 0x{node.address:08X}")
    return Disassembly(text, tuple(nodes), tuple(entries), symbols)


def literal_load(node, rom, base):
    """Return an encoding-preserving PC-relative load without a GAS relocation."""
    instruction = node.text.split("@", 1)[0].strip()
    match = re.fullmatch(r"(ldr[a-z]*)\s+(r\d+|sp|lr|pc|ip|sl|fp),\s*(" + IDENTIFIER + r")", instruction)
    if not match:
        return None
    mnemonic, register, target = match.groups()
    offset = node.address - base
    if node.isa == "thumb":
        halfword = struct.unpack_from("<H", rom, offset)[0]
        require(halfword & 0xF800 == 0x4800, "Unexpected Thumb literal encoding")
        displacement = (halfword & 255) * 4
    else:
        word = struct.unpack_from("<I", rom, offset)[0]
        require(word & 0x0F7F0000 == 0x051F0000, "Unexpected ARM literal encoding")
        displacement = (word & 0xFFF) * (1 if word & (1 << 23) else -1)
    immediate = f"-0x{-displacement:X}" if displacement < 0 else f"0x{displacement:X}"
    return target, f"{mnemonic} {register}, [pc, #{immediate}]"
