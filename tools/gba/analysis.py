"""Conservative marked-entry spans, local labels, and cross-slice references."""

from bisect import bisect_left, bisect_right
from collections import Counter
from dataclasses import dataclass
import re

from .config import CODE, digest, require
from . import disasm, symbols
from .source import C_TOKENS, storage_definitions


@dataclass(frozen=True)
class Function:
    address: int
    size: int
    name: str
    isa: str
    module: str
    line: int

    @property
    def end(self):
        return self.address + self.size

    @property
    def source(self):
        """Default reference path; C verification records the final path in the manifest."""
        return f"nonmatchings/{self.module}/{self.name}.s"


class Analysis:
    def __init__(self, config, assembly=None):
        self.config = config
        self.hints = symbols.load(config.hints, config.base, len(config.rom))
        self.address_symbols = symbols.load_addresses(config.symbol_paths, self.hints)
        self.assembly = assembly if assembly is not None else disasm.run(config, self.hints, self.address_symbols)
        self.positions = [node.address for node in self.assembly.nodes]
        self.symbols = self.assembly.symbols
        self.functions = []
        entries = self.assembly.entries
        starts = {entry.address for entry in entries}
        for segment in config.segments:
            if segment.kind not in CODE:
                continue
            require(segment.address in starts, "Code module must start at an entry marker: " + segment.name)
            members = [entry for entry in entries if segment.address <= entry.address < segment.end]
            for index, entry in enumerate(members):
                end = members[index + 1].address if index + 1 < len(members) else segment.end
                self.functions.append(Function(entry.address, end - entry.address, entry.name,
                                               entry.isa, segment.name, entry.line))
        require(len(self.functions) == len(entries), "Entry ownership is incomplete")
        self.references = {}
        for node in self.assembly.nodes:
            if node.kind not in {"instruction", "data"}:
                continue
            tokens = symbols.IDENT.findall(node.text.split("@", 1)[0])
            self.references[node.line] = tuple(dict.fromkeys(t for t in tokens if t in self.symbols))
        # Entry fragments are included in their module's object. Labels shared
        # within that object do not need ELF exports, even across entry spans.
        self.exported_labels = set(self.address_symbols.by_name) & self.symbols.keys()
        modules = [segment for segment in config.segments if segment.kind in CODE]
        module_starts = [segment.address for segment in modules]
        for node in self.assembly.nodes:
            references = self.references.get(node.line, ())
            if not references:
                continue
            index = bisect_right(module_starts, node.address) - 1
            module = modules[index] if index >= 0 and node.address < modules[index].end else None
            literal = disasm.literal_load(node, config.rom, config.base) if node.kind == "instruction" else None
            for name in references:
                if module is not None and module.address <= self.symbols[name].address < module.end:
                    continue
                # Loads outside the module use fixed PC offsets, so their
                # original pool labels are not imports in the emitted object.
                if literal is None or name != literal[0]:
                    self.exported_labels.add(name)
        self.counts = Counter(node.isa for node in self.assembly.nodes if node.kind == "instruction")
        self.suppressed_hints = [dict(address=h.address, command=h.command, line=h.line)
                                 for h in self.hints if h.command.endswith("_func") and h.address not in starts]
        self.disassembly_hash = digest(self.assembly.text.encode("utf-8"))
        self.undefined_syms = {}
        self.undefined_funcs = {}
        self.defined_symbols = set(self.symbols)
        self.symbol_aliases = {}
        for address, aliases in self.address_symbols.by_address.items():
            if aliases[0].kind == "func":
                match = next((entry for entry in entries if entry.address == address), None)
                if match:
                    self.symbol_aliases[address] = [row.name for row in aliases if row.name != match.name]
                    self.defined_symbols.update(row.name for row in aliases)
        self.data_anchors = {}
        for segment in config.segments:
            if segment.kind in CODE:
                continue
            named = self.address_symbols.aliases(segment.address)
            anchor = named[0].name if getattr(segment, "rodata_owner", None) and named else f"_gba_data_{segment.address:08X}"
            self.data_anchors[segment.address] = anchor
        self.defined_symbols.update(self.data_anchors.values())
        for symbol in self.address_symbols.by_name.values():
            if symbol.kind != "func" and any(segment.kind not in CODE and segment.address <= symbol.address < segment.end
                                              for segment in config.segments):
                self.defined_symbols.add(symbol.name)
        self.resource_exports = {}
        self.decoded_resources = {}
        self.pointer_names = {symbol.value: name for name, symbol in self.symbols.items() if symbol.isa is not None}
        sound_groups = {}
        for segment in config.segments:
            if segment.kind != "sound":
                continue
            start = segment.options.get("resource_start", segment.start)
            end = segment.options.get("resource_end", segment.start + segment.size)
            name = segment.options.get("resource_name", segment.key)
            options = {key: value for key, value in segment.options.items()
                       if key not in {"resource_start", "resource_end", "resource_name"}}
            key = start, end, name, tuple(sorted(options.items()))
            sound_groups.setdefault(key, []).append(segment)
        for (start, end, name, options), members in sound_groups.items():
            from .sound import dump_sound
            resource = dump_sound(config.rom, start, end, name, dict(options), config.base,
                                  [(s.start, s.start + s.size) for s in members])
            for index, segment in enumerate(members):
                self.resource_exports[segment.key] = dict(resource, body=resource["bodies"][segment.start],
                                                          files=resource["files"] if index == 0 else {})
        for segment in config.segments:
            if segment.kind == "sound":
                resource = self.resource_exports[segment.key]
                for name, value in resource["symbols"].items():
                    require(name not in self.symbols or self.symbols[name].address == value,
                            "Sound symbol conflicts with an existing symbol: " + name)
                self.defined_symbols.update(resource["symbols"])
                self.pointer_names.update({value: name for name, value in resource["symbols"].items()})
                for name, value in resource["symbols"].items():
                    self.symbols[name] = disasm.Symbol(value, None)
        c_definitions = set()
        c_sources = {}
        self.storage_owners = {}
        for path in (config.root / "src").rglob("*.c"):
            text = path.read_text(encoding="utf-8")
            module = path.relative_to(config.root / "src").with_suffix("").as_posix()
            c_sources[module] = text
            for name, private in storage_definitions(text).items():
                if private:
                    self.storage_owners.setdefault(name, set()).add(module)
                else:
                    self.defined_symbols.add(name)
            text = re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)
            c_definitions.update(re.findall(
                r"(?:^|[;}])\s*(?:[A-Za-z_]\w*[\s*]+)+([A-Za-z_]\w*)\s*\([^;{}]*\)\s*\{", text))
        self.defined_symbols.update(c_definitions)
        ram = config.root / "config/ram.ll.ld"
        if ram.is_file():
            raw = ram.read_bytes()
            if hasattr(config, "fingerprints"):
                config.fingerprints[ram.relative_to(config.root).as_posix()] = digest(raw)
            text = C_TOKENS.sub(lambda match: " " if match[0].startswith(("/", '"', "'")) else match[0], raw.decode("utf-8"))
            assignments = set(re.findall(r"\b(" + symbols.IDENTIFIER + r")\s*=(?!=)", text))
            self.defined_symbols.update(assignments & self.address_symbols.by_name.keys())
        # A C expression can use a base symbol while the original instruction
        # literals only reference its field offsets, so include source uses too.
        for module, text in c_sources.items():
            for token in C_TOKENS.finditer(text):
                if token[0] in self.address_symbols.by_name:
                    self.reference_symbol(token[0], module)

    def reference_symbol(self, name, module=None):
        """Record only references needing a linker address, not owned storage."""
        if name in self.defined_symbols or module in self.storage_owners.get(name, ()):
            return
        symbol = self.address_symbols.by_name.get(name)
        if symbol is not None:
            (self.undefined_funcs if symbol.kind == "func" else self.undefined_syms)[name] = symbol.value

    def address_expression(self, value, module=None):
        """Name referenced memory/data addresses; leave scalar constants numeric."""
        expression = self.address_symbols.reference(value)
        if expression is not None:
            self.reference_symbol(symbols.IDENT.match(expression)[0], module)
            return expression
        if value in self.data_anchors:
            return self.data_anchors[value]
        if value in self.pointer_names:
            return self.pointer_names[value]
        # MMIO, palette RAM, VRAM and OAM stay numeric unless explicitly named.
        ranges = ((0x02000000, 0x02040000), (0x03000000, 0x03008000),
                  (0x0E000000, 0x0E010000),
                  (self.config.code_end, self.config.base + len(self.config.rom)))
        if any(start <= value <= end for start, end in ranges):
            name = f"D_{value:08X}"
            self.undefined_syms[name] = value
            return name
        return None

    def data_text(self, node):
        statement, *comment = node.text.split("@", 1)
        directive, operands = statement.strip().split(None, 1)
        if disasm.WIDTHS[directive] != 4:
            return node.text
        rewritten = []
        module = next((segment.name for segment in self.config.segments if segment.kind in CODE
                       and segment.address <= node.address < segment.end), None)
        for token in operands.split(","):
            token = token.strip()
            if not symbols.IDENT.fullmatch(token):
                token = self.address_expression(int(token, 0), module) or token
            else:
                self.reference_symbol(token, module)
            rewritten.append(token)
        return directive + " " + ", ".join(rewritten) + (" @" + comment[0] if comment else "")

    def nodes(self, start, end):
        left, right = bisect_left(self.positions, start), bisect_left(self.positions, end)
        if left and self.assembly.nodes[left - 1].end > start:
            raise ValueError(f"Slice cuts an instruction or literal at 0x{start:08X}")
        result = self.assembly.nodes[left:right]
        require(not result or result[-1].end <= end, f"Slice cuts an instruction or literal at 0x{end:08X}")
        return result
