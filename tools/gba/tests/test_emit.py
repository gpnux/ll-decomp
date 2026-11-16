"""Check label visibility through GNU ARM assembly and linking."""

from pathlib import Path
import shutil
import subprocess
import tempfile
from types import SimpleNamespace
import unittest

from tools.gba.analysis import Analysis
from tools.gba.disasm import parse
from tools.gba.emit import code, data, undefined_text


BASE = 0x08000000
MACROS = Path(__file__).resolve().parents[1]
BINUTILS = ("arm-none-eabi-as", "arm-none-eabi-ld", "arm-none-eabi-objcopy",
            "arm-none-eabi-readelf")
LOOP_ASM = """thumb_func_start Loop
Loop: @ 0x08000000
    movs r0, #0
_08000002: @ 0x08000002
    adds r0, #1
    cmp r0, #7
    bls _08000002
_08000008: @ 0x08000008
    bx lr
non_word_aligned_thumb_func_start Caller
Caller: @ 0x0800000A
    b _08000008
PublicData: @ 0x0800000C
    .4byte 0x12345678
"""
LOOP_ROM = bytes.fromhex("00 20 01 30 07 28 fc d9 70 47 fd e7 78 56 34 12")
LITERAL_ASM = """thumb_func_start Load
Load: @ 0x08000000
    ldr r0, _08000008
    bx lr
thumb_func_start Other
Other: @ 0x08000004
    bx lr
    .hword 0
_08000008: @ 0x08000008
    .4byte 0x12345678
"""
LITERAL_ROM = bytes.fromhex("01 48 70 47 70 47 00 00 78 56 34 12")


@unittest.skipUnless(all(shutil.which(tool) for tool in BINUTILS),
                     "GNU ARM binutils are required")
class CodeLabelTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix="gba-label-test-")
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)

    def analysis(self, boundaries=(0,), public=False, text=LOOP_ASM, rom=LOOP_ROM,
                 extra_symbols=""):
        assembly = parse(text, BASE, BASE + len(rom), rom)
        hints = self.root / "test.cfg"
        hints.write_text("".join(f"thumb_func 0x{entry.address:08X}\n"
                                 for entry in assembly.entries))
        symbols = self.root / "symbols.txt"
        symbols.write_text(("PublicData = 0x0800000C; // type:data size:0x4\n"
                            if public else "") + extra_symbols)
        segments = [SimpleNamespace(address=BASE + start, end=BASE + end,
                                    kind="c", name=f"unit_{index}")
                    for index, (start, end) in
                    enumerate(zip(boundaries, (*boundaries[1:], len(rom))))]
        config = SimpleNamespace(root=self.root, hints=hints, symbol_paths=(symbols,),
                                 base=BASE, code_end=BASE + len(rom), rom=rom,
                                 segments=segments)
        return Analysis(config, assembly)

    def run_tool(self, *args):
        result = subprocess.run(args, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout

    def assemble(self, name, text):
        source = self.root / (name + ".s")
        obj = source.with_suffix(".o")
        source.write_text(text)
        self.run_tool("arm-none-eabi-as", "-mcpu=arm7tdmi", "-mthumb-interwork",
                      "-I", str(MACROS), str(source), "-o", str(obj))
        return obj

    def section_bytes(self, obj, section=".text"):
        binary = obj.with_suffix(".bin")
        self.run_tool("arm-none-eabi-objcopy", "-O", "binary",
                      "--only-section=" + section, str(obj), str(binary))
        return binary.read_bytes()

    def link(self, rows, entry="Loop", definitions=""):
        script = self.root / "linker.ld"
        sections = [f'  .part{index} 0x{address:08X} : SUBALIGN(1) '
                    f'{{ "{obj}"({section}) }}'
                    for index, (obj, address, section) in enumerate(rows)]
        script.write_text('OUTPUT_FORMAT("elf32-littlearm")\nOUTPUT_ARCH(arm)\n'
                          f"ENTRY({entry})\n" + definitions + "SECTIONS {\n"
                          + "\n".join(sections) + "\n  /DISCARD/ : { *(*) }\n}\n")
        elf = self.root / "image.elf"
        binary = elf.with_suffix(".bin")
        self.run_tool("arm-none-eabi-ld", "-T", str(script), "-o", str(elf))
        self.run_tool("arm-none-eabi-objcopy", "-O", "binary", str(elf), str(binary))
        return binary.read_bytes()

    def test_loop_branch_is_resolved_by_assembler(self):
        analysis = self.analysis()
        obj = self.assemble("module", code(analysis, BASE, BASE + len(LOOP_ROM))[0])
        self.assertEqual(self.section_bytes(obj)[6:8], LOOP_ROM[6:8])
        relocations = self.run_tool("arm-none-eabi-readelf", "-rW", str(obj))
        self.assertNotIn("R_ARM_THM_JUMP8", relocations)
        symbols = self.run_tool("arm-none-eabi-readelf", "-sW", str(obj))
        self.assertRegex(symbols, r"\bLOCAL\b[^\n]*_08000002\b")

    def test_same_module_fragments_keep_shared_labels_local(self):
        analysis = self.analysis()
        reference = self.assemble("reference", code(analysis, BASE, BASE + len(LOOP_ROM))[0])
        fragments = "".join(code(analysis, function.address, function.end)[0]
                            for function in analysis.functions)
        working = self.assemble("working", fragments)
        self.assertEqual(self.section_bytes(reference), LOOP_ROM)
        self.assertEqual(self.section_bytes(working), LOOP_ROM)
        symbols = self.run_tool("arm-none-eabi-readelf", "-sW", str(working))
        self.assertRegex(symbols, r"\bLOCAL\b[^\n]*_08000008\b")

    def test_shared_label_links_between_modules(self):
        analysis = self.analysis(boundaries=(0, 10))
        first = self.assemble("first", code(analysis, BASE, BASE + 10)[0])
        second = self.assemble("second", code(analysis, BASE + 10, BASE + len(LOOP_ROM))[0])
        self.assertEqual(self.link(((first, BASE, ".text"),
                                    (second, BASE + 10, ".text"))), LOOP_ROM)

    def test_explicit_data_symbol_links_from_another_object(self):
        analysis = self.analysis(public=True)
        module = self.assemble("module", code(analysis, BASE, BASE + len(LOOP_ROM))[0])
        consumer = self.assemble("consumer", '.section .rodata, "a", %progbits\n'
                                            "    .4byte PublicData\n")
        expected = LOOP_ROM + (BASE + 12).to_bytes(4, "little")
        self.assertEqual(self.link(((module, BASE, ".text"),
                                    (consumer, BASE + len(LOOP_ROM), ".rodata"))), expected)

    def test_cross_module_literal_does_not_need_a_global_pool(self):
        analysis = self.analysis(boundaries=(0, 4), text=LITERAL_ASM, rom=LITERAL_ROM)
        loader = self.assemble("loader", code(analysis, BASE, BASE + 4)[0])
        other = self.assemble("other", code(analysis, BASE + 4, BASE + len(LITERAL_ROM))[0])
        relocations = self.run_tool("arm-none-eabi-readelf", "-rW", str(loader))
        self.assertNotIn("_08000008", relocations)
        symbols = self.run_tool("arm-none-eabi-readelf", "-sW", str(other))
        self.assertRegex(symbols, r"\bLOCAL\b[^\n]*_08000008\b")
        self.assertEqual(self.link(((loader, BASE, ".text"),
                                    (other, BASE + 4, ".text")), entry="Load"), LITERAL_ROM)

    def test_hardware_literals_do_not_create_data_symbols(self):
        hardware = (0x04000000, 0x040000C6, 0x040003FF, 0x04000400, 0x04FFFFFF,
                    0x05000000, 0x0500018C, 0x050003FF, 0x05000400, 0x05FFFFFF,
                    0x06000000, 0x06005800, 0x06017FFF, 0x06018000, 0x06FFFFFF,
                    0x07000000, 0x07000020, 0x070003FF, 0x07000400, 0x07FFFFFF)
        ram = (0x02001234, 0x03007FFC, 0x0E000010)
        values = (*hardware, 0x04000208, *ram)
        raw = b"".join(value.to_bytes(4, "little") for value in values)
        rom = LOOP_ROM[:12] + raw
        text = LOOP_ASM.replace("0x12345678", ", ".join(f"0x{value:08X}" for value in values))
        expected_symbols = {f"D_{value:08X}": value for value in ram}
        expected_symbols["REG_ADDR_IME"] = 0x04000208

        for section in (".text", ".rodata"):
            with self.subTest(section=section):
                analysis = self.analysis(text=text, rom=rom,
                                         extra_symbols="REG_ADDR_IME = 0x04000208; // type:data\n")
                if section == ".text":
                    assembly = code(analysis, BASE, BASE + len(rom))[0]
                    entry, address, expected = "Loop", BASE, rom
                else:
                    segment = SimpleNamespace(kind="rodata", key="gfx/addresses", options={},
                                              start=12, size=len(raw), address=BASE + 12,
                                              end=BASE + len(rom))
                    assembly, _, entry, _, _ = data(analysis, segment)
                    address, expected = BASE + 12, raw
                self.assertEqual(analysis.undefined_syms, expected_symbols)
                obj = self.assemble("addresses", assembly)
                self.assertEqual(self.link(((obj, address, section),), entry=entry,
                                           definitions=undefined_text(analysis.undefined_syms, provide=True)),
                                 expected)

    def test_data_fallbacks_preserve_real_bss_and_common_definitions(self):
        obj = self.assemble("state", '.section .text, "ax", %progbits\n'
                                    '.global References\nReferences:\n'
                                    '    .4byte PrivateState, SharedState, UnknownState\n'
                                    '.lcomm PrivateState, 4\n.comm SharedState, 4\n')
        script = self.root / "state.ld"
        script.write_text('OUTPUT_FORMAT("elf32-littlearm")\nOUTPUT_ARCH(arm)\n'
                          'ENTRY(References)\n'
                          + undefined_text({"PrivateState": 0x03001100,
                                            "SharedState": 0x03001200,
                                            "UnknownState": 0x03001300}, provide=True)
                          + 'SECTIONS {\n'
                            '  .text 0x08000000 : { *(.text) }\n'
                            '  .bss 0x03001000 (NOLOAD) : { *(.bss) *(COMMON) }\n'
                            '  /DISCARD/ : { *(*) }\n}\n')
        elf = self.root / "state.elf"
        self.run_tool("arm-none-eabi-ld", "-T", str(script), "-o", str(elf), str(obj))
        expected = b"".join(address.to_bytes(4, "little")
                            for address in (0x03001000, 0x03001004, 0x03001300))
        self.assertEqual(self.section_bytes(elf), expected)
        symbols = self.run_tool("arm-none-eabi-readelf", "-sW", str(elf))
        self.assertRegex(symbols, r"\b03001000\b[^\n]*\bLOCAL\b[^\n]*\bPrivateState\b")
        self.assertNotRegex(symbols, r"\bGLOBAL\b[^\n]*\bPrivateState\b")
        self.assertRegex(symbols, r"\b03001004\b[^\n]*\bGLOBAL\b[^\n]*\bSharedState\b")


if __name__ == "__main__":
    unittest.main()
