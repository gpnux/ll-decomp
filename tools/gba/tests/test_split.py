"""Check source classification, resource splitting, and generated builds."""

import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from tools.gba.analysis import Analysis
from tools.gba.config import load
from tools.gba.disasm import parse
from tools.gba.emit import check_generated, rewrite_asm_includes, split
from tools.gba.objdiff import generate_config


ROOT = Path(__file__).resolve().parents[3]
BASE = 0x08000000
TOOLS = ("make", "arm-none-eabi-cpp", "arm-none-eabi-as", "arm-none-eabi-ld", "arm-none-eabi-objcopy")
SIMPLE_ASM = """thumb_func_start Good
Good:
    movs r0, #7
    bx lr
thumb_func_start AssemblyOnly
AssemblyOnly:
    movs r0, #8
    bx lr
thumb_func_start Disabled
Disabled:
    movs r0, #9
    bx lr
thumb_func_start Empty
Empty:
    bx lr
    .align 2, 0
thumb_func_start Wrong
Wrong:
    movs r0, #10
    bx lr
"""
SIMPLE_ROM = bytes.fromhex("07 20 70 47 08 20 70 47 09 20 70 47 70 47 00 00 0a 20 70 47")
SIMPLE_C = """#include "include_asm.h"
#if 1
unsigned Good(void) { return 7; }
#else
INCLUDE_ASM("asm/nonmatchings/unit", Good);
#endif
INCLUDE_ASM("asm/nonmatchings/unit", AssemblyOnly);
#if 0
unsigned Disabled(void) { return 9; }
#else
INCLUDE_ASM("asm/nonmatchings/unit", Disabled);
#endif
void Empty(void) {}
unsigned Wrong(void) { return 11; }
"""
RAM_ASM = """thumb_func_start Read
Read:
    ldr r0, _08000008
    ldr r0, [r0]
    bx lr
    .align 2, 0
_08000008:
    .4byte 0x03001000
thumb_func_start Write
Write:
    ldr r1, _08000014
    str r0, [r1]
    bx lr
    .align 2, 0
_08000014:
    .4byte 0x03001000
thumb_func_start Pointer
Pointer:
    ldr r0, _0800001C
    bx lr
_0800001C:
    .4byte 0x02001234
"""
RAM_ROM = bytes.fromhex("01 48 00 68 70 47 00 00 00 10 00 03 "
                        "01 49 08 60 70 47 00 00 00 10 00 03 "
                        "00 48 70 47 34 12 00 02")
RAM_C = """static unsigned state;
extern unsigned value, other;
unsigned Read(void) { return state; }
void Write(unsigned x) { state = x; }
unsigned Pointer(void) { return (unsigned)&value; }
"""


class SplitFixture(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix="gba-split-test-")
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        for name in ("config", "include", "src"):
            (self.root / name).mkdir()
        shutil.copy2(ROOT / "Makefile", self.root / "Makefile")
        shutil.copy2(ROOT / "include/include_asm.h", self.root / "include/include_asm.h")
        for path in ROOT.glob("charmap*.txt"):
            shutil.copy2(path, self.root / path.name)
        (self.root / "tools").symlink_to(ROOT / "tools", target_is_directory=True)
        (self.root / "config/ram.ll.ld").write_text("SECTIONS {}\n")

    def fixture(self, text=SIMPLE_ASM, rom=SIMPLE_ROM, source=SIMPLE_C, boundaries=(0,), extra_symbols=""):
        self.assembly, self.rom = text, rom
        parsed = parse(text, BASE, BASE + len(rom), rom)
        (self.root / "baserom.gba").write_bytes(rom)
        (self.root / "config/ll.cfg").write_text("".join(
            f"{entry.isa}_func 0x{entry.address:08X}\n" for entry in parsed.entries))
        (self.root / "config/symbols.ll.txt").write_text("".join(
            f"{entry.name} = 0x{entry.address:08X}; // type:func isa:{entry.isa}\n"
            for entry in parsed.entries) + extra_symbols)
        units = [f"      - [0x{start:X}, c, {'unit' if index == 0 else 'broken'}]\n"
                 for index, start in enumerate(boundaries)]
        (self.root / "config/splat.ll.yaml").write_text(
            "options:\n  platform: gba\n  base_path: ..\n  target_path: baserom.gba\n"
            "  output_path: asm\n  gbadisasm_config: config/ll.cfg\n"
            "  symbol_addrs_path: [config/symbols.ll.txt]\n"
            f"sha1: '{hashlib.sha1(rom).hexdigest()}'\n"
            "segments:\n  - name: test\n    type: code\n    start: 0\n    vram: 0x08000000\n"
            "    subsegments:\n" + "".join(units)
            + f"      - [0x{len(rom):X}]\n  - [0x{len(rom):X}]\n")
        if source is not None:
            (self.root / "src/unit.c").write_text(source)

    def analysis(self):
        config = load(self.root / "config/splat.ll.yaml")
        return Analysis(config, parse(self.assembly, BASE, config.code_end, config.rom))

    def split(self):
        return split(self.analysis())

    def statuses(self, manifest):
        return {row["name"]: row["match_status"] for row in manifest["functions"]}

    def make(self, *targets):
        result = subprocess.run(["make", "--no-print-directory", "-s", *targets], cwd=self.root,
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


@unittest.skipUnless(all(shutil.which(tool) for tool in TOOLS)
                     and (ROOT / "tools/agbcc/bin/agbcc").is_file(),
                     "The project's agbcc and GNU ARM toolchain are required")
class SplitBuildTests(SplitFixture):
    def rodata_fixture(self):
        text = """thumb_func_start Pointer
Pointer:
    ldr r0, _08000004
    bx lr
_08000004:
    .4byte 0x08000028
thumb_func_start Good
Good:
    movs r0, #7
    bx lr
"""
        code = bytes.fromhex("00 48 70 47 28 00 00 08 07 20 70 47")
        source = """extern const unsigned char Values[4];
unsigned Target(void);
const unsigned char *Pointer(void) { return Values; }
unsigned Good(void) { return 7; }
unsigned (*const Callbacks[2])(void) = {Good, Target};
const unsigned char Values[4] = {0x7A, 0x34, 0x56, 0x78};
"""
        self.fixture(text, code, source, extra_symbols=
                     "Callbacks = 0x08000020; // type:rodata size:0x8\n"
                     "Values = 0x08000028; // type:rodata size:0x4\n"
                     "Target = 0x08000100; // type:func isa:thumb\n")
        rom = code + bytes(0x20 - len(code)) + bytes.fromhex("09 00 00 08 01 01 00 08 7a 34 56 78")
        (self.root / "baserom.gba").write_bytes(rom)
        config = self.root / "config/splat.ll.yaml"
        config.write_text(config.read_text()
                          .replace(hashlib.sha1(code).hexdigest(), hashlib.sha1(rom).hexdigest())
                          .replace("\n  - [0xC]\n", "\n  - name: gfx\n    type: gfx\n    start: 0xC\n"
                                   "    subsegments:\n      - [0xC, bin, padding]\n"
                                   "      - [0x20, rodata, unit]\n      - [0x2C]\n  - [0x2C]\n"))
        return source, rom

    def test_module_rodata_links_from_c_and_reference_at_the_original_address(self):
        _, rom = self.rodata_fixture()
        manifest = self.split()
        self.assertEqual(set(self.statuses(manifest).values()), {"matching"})
        self.make("all", "objdiff")
        self.assertEqual((self.root / "test.gba").read_bytes(), rom)
        reference = self.root / "asm/data/unit.rodata.s"
        self.assertTrue(reference.is_file())
        self.assertNotIn('.set ', reference.read_text())
        self.assertNotIn('PROVIDE(_gba_data_', (self.root / 'asm/linker.ld').read_text())
        self.assertIn('.include "data/unit.rodata.s"', (self.root / "asm/obj/unit.s").read_text())
        script = self.root / "reference.ld"
        script.write_text((self.root / "asm/linker.ld").read_text().replace('"build/unit.o"', '"expected/unit.o"'))
        result = subprocess.run(["arm-none-eabi-ld", "-T", str(script), "-o", "reference.elf"],
                                cwd=self.root, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        result = subprocess.run(["arm-none-eabi-objcopy", "-O", "binary", "reference.elf", "reference.gba"],
                                cwd=self.root, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.root / "reference.gba").read_bytes(), rom)

    def test_multiple_rodata_fragments_keep_their_addresses_and_partial_words(self):
        source, previous_rom = self.rodata_fixture()
        rom = previous_rom + bytes(4) + bytes.fromhex("34 12 78 56 bc 9a")
        (self.root / "baserom.gba").write_bytes(rom)
        (self.root / "src/unit.c").write_text(source +
            'const unsigned short Extra[3] __attribute__((section(".rodata.00000030"), aligned(1))) '
            '= {0x1234, 0x5678, 0x9ABC};\n')
        symbols = self.root / "config/symbols.ll.txt"
        symbols.write_text(symbols.read_text() + "Extra = 0x08000030; // type:rodata size:0x6\n")
        config = self.root / "config/splat.ll.yaml"
        config.write_text(config.read_text()
                          .replace(hashlib.sha1(previous_rom).hexdigest(), hashlib.sha1(rom).hexdigest())
                          .replace("      - [0x2C]\n  - [0x2C]\n",
                                   "      - [0x2C, bin, middle]\n      - [0x30, rodata, unit]\n"
                                   "      - [0x36]\n  - [0x36]\n"))
        manifest = self.split()
        self.assertEqual(set(self.statuses(manifest).values()), {"matching"})
        self.make("all", "objdiff")
        self.assertEqual((self.root / "test.gba").read_bytes(), rom)
        self.assertEqual((self.root / "asm/obj/unit.s").read_text().count('.include "data/unit.rodata.s"'), 1)
        check_generated(self.root / "asm")

    def test_c_tables_can_split_a_structured_sound_resource(self):
        code = bytes.fromhex('07 20 70 47')
        source = ('unsigned Good(void) { return 7; }\n'
                  'const unsigned Tables[5] = {0x03001000, 0x03002000, 1, 0x08000040, 0};\n')
        self.fixture('thumb_func_start Good\nGood:\n    movs r0, #7\n    bx lr\n', code, source,
                     extra_symbols='Tables = 0x08000020; // type:rodata size:0x14\n')
        rom = bytearray(0x54)
        rom[:4] = code
        rom[8:20] = bytes.fromhex('04 3c 00 00 00 00 00 00 00 00 00 00')
        rom[0x20:0x34] = bytes.fromhex('00 10 00 03 00 20 00 03 01 00 00 00 40 00 00 08 00 00 00 00')
        rom[0x40:0x4c] = bytes.fromhex('01 00 00 00 08 00 00 08 50 00 00 08')
        rom[0x50] = 0xB1
        (self.root / 'baserom.gba').write_bytes(rom)
        config = self.root / 'config/splat.ll.yaml'
        document = config.read_text().replace(hashlib.sha1(code).hexdigest(), hashlib.sha1(rom).hexdigest())
        options = ('        resource_start: 0x4\n        resource_end: 0x54\n'
                   '        resource_name: sound/music\n        song_table: 0x2C\n'
                   '        song_count: 1\n        mplay_table: 0x20\n'
                   '        mplay_count: 1\n        voicegroup_end: 0x14\n')
        document = document.replace('\n  - [0x4]\n',
            '\n  - name: music\n    type: sound\n    start: 0x4\n    subsegments:\n'
            '      - start: 0x4\n        type: sound\n        name: music_head\n' + options
            + '      - [0x20, rodata, unit]\n'
            '      - start: 0x34\n        type: sound\n        name: music_tail\n' + options
            + '      - [0x54]\n  - [0x54]\n')
        config.write_text(document)
        manifest = self.split()
        self.assertEqual(self.statuses(manifest), {'Good': 'matching'})
        self.make('all', 'objdiff')
        self.assertEqual((self.root / 'test.gba').read_bytes(), rom)
        for filename in ('music_head.s', 'music_tail.s'):
            body = (self.root / 'asm/data/sound' / filename).read_text()
            self.assertNotIn('song_table.inc', body)
            self.assertNotIn('mplay_table.inc', body)
        check_generated(self.root / 'asm')
        config.write_text(document.replace('[0x20, rodata, unit]', '[0x22, rodata, unit]'))
        with self.assertRaisesRegex(ValueError, 'Sound slice cuts mplay_table'):
            self.analysis()

    def test_module_and_function_renames_preserve_c_and_classification(self):
        self.fixture()
        self.split()
        config = self.root / "config/splat.ll.yaml"
        config.write_text(config.read_text().replace(", c, unit]", ", c, renamed]"))
        symbols = self.root / "config/symbols.ll.txt"
        symbols.write_text(symbols.read_text().replace("Good =", "NewGood ="))
        self.assembly = self.assembly.replace("Good", "NewGood")
        manifest = self.split()
        self.assertEqual(self.statuses(manifest)["NewGood"], "assembly")
        self.assertFalse((self.root / "src/unit.c").exists())
        self.assertIn("unsigned NewGood(void) { return 7; }", (self.root / "src/renamed.c").read_text())
        self.assertIn('INCLUDE_ASM("asm/nonmatchings/renamed", NewGood);', (self.root / "src/renamed.c").read_text())
        self.assertTrue((self.root / "asm/nonmatchings/renamed/NewGood.s").is_file())
        self.assertFalse((self.root / "asm/nonmatchings/unit/Good.s").exists())
        self.make("build/renamed.o", "expected/renamed.o")

    def test_existing_c_on_first_split_uses_the_generated_header(self):
        self.fixture()
        (self.root / "include/include_asm.h").unlink()
        manifest = self.split()
        self.assertEqual(self.statuses(manifest)["Good"], "assembly")
        self.make("build/unit.o")

    def test_bootstrap_without_c_and_generated_edit_protection(self):
        self.fixture(source=None)
        (self.root / "include/include_asm.h").unlink()
        manifest = self.split()
        self.assertEqual(set(self.statuses(manifest).values()), {"assembly"})
        self.make("all")
        self.assertEqual((self.root / "test.gba").read_bytes(), SIMPLE_ROM)
        self.assertEqual(set(self.statuses(self.split()).values()), {"assembly"})
        path = self.root / "src/unit.c"
        path.write_text(path.read_text().replace('INCLUDE_ASM("asm/nonmatchings/unit", Wrong);',
                                                 'unsigned Wrong(void) { return 10; }'))
        self.assertEqual(self.statuses(self.split())["Wrong"], "matching")
        self.make("all")
        source = (self.root / "src/unit.c").read_text()
        reference = self.root / "asm/nonmatchings/unit/Good.s"
        reference.write_text(reference.read_text() + "@ manual edit\n")
        with self.assertRaisesRegex(ValueError, "Generated file was edited"):
            self.split()
        self.assertEqual((self.root / "src/unit.c").read_text(), source)


class SourceSplitTests(SplitFixture):
    def test_fixed_address_used_by_c_but_not_original_literals_is_bound(self):
        rom = RAM_ROM[:-4] + (0x02001238).to_bytes(4, "little")
        source = RAM_C.replace("(unsigned)&value", "(unsigned)&value + 4")
        self.fixture(RAM_ASM.replace("0x02001234", "0x02001238"), rom, source,
                     extra_symbols="value = 0x02001234; // type:data\n")
        report = self.split()
        self.assertEqual(report["linker_symbols"]["data"]["value"], 0x02001234)
        self.assertIn("PROVIDE(value = 0x02001234);", (self.root / "asm/linker.ld").read_text())

    def test_separate_data_file_and_owned_storage_do_not_generate_fallback_files(self):
        source = RAM_C.replace("static unsigned state", "unsigned state")
        self.fixture(RAM_ASM, RAM_ROM, source,
                     extra_symbols="state = 0x03001000; // type:bss size:0x4\nvalue = 0x02001234; // type:data\n")
        path = self.root / "config/symbols.ll.txt"
        lines = path.read_text().splitlines()
        path.write_text("\n".join(line for line in lines if "type:func" in line) + "\n")
        (self.root / "config/symbols.ll.data.txt").write_text("\n".join(line for line in lines if "type:func" not in line) + "\n")
        config = self.root / "config/splat.ll.yaml"
        config.write_text(config.read_text().replace("[config/symbols.ll.txt]",
                          "[config/symbols.ll.txt, config/symbols.ll.data.txt]"))
        (self.root / "config/ram.ll.ld").write_text(
            'SECTIONS { .bss.unit 0x03001000 (NOLOAD) : { "build/unit.o"(.bss .bss.* COMMON) } }\n')
        report = self.split()
        image = (self.root / "asm/linker.ld").read_text()
        self.assertNotIn("PROVIDE(state", image)
        self.assertIn("PROVIDE(value = 0x02001234);", image)
        self.assertEqual(report["linker_symbols"]["data"], {"value": 0x02001234})
        self.assertNotIn("INCLUDE undefined_", image)
        self.assertFalse((self.root / "undefined_funcs_auto.txt").exists())
        self.assertFalse((self.root / "undefined_syms_auto.txt").exists())
        self.make("all")
        self.assertEqual((self.root / "test.gba").read_bytes(), RAM_ROM)

    def test_private_storage_only_resolves_its_own_module_references(self):
        self.fixture(RAM_ASM, RAM_ROM, RAM_C,
                     extra_symbols="state = 0x03001000; // type:bss size:0x4\nvalue = 0x02001234; // type:data\n")
        analysis = self.analysis()
        analysis.reference_symbol("state", "unit")
        self.assertNotIn("state", analysis.undefined_syms)
        analysis.reference_symbol("state", "foreign")
        self.assertEqual(analysis.undefined_syms["state"], 0x03001000)

    def test_progress_counts_exclude_library_and_assembly_units(self):
        text = ('thumb_func_start Good\nGood:\n    movs r0, #7\n    bx lr\n'
                'thumb_func_start Pending\nPending:\n    movs r0, #8\n    bx lr\n'
                'thumb_func_start Library\nLibrary:\n    movs r0, #9\n    bx lr\n')
        rom = bytes.fromhex('07 20 70 47 08 20 70 47 09 20 70 47')
        source = ('#include "include_asm.h"\nunsigned Good(void) { return 7; }\n'
                  'INCLUDE_ASM("asm/nonmatchings/unit", Pending);\n')
        for kind, name in (("asm", "broken"), ("asm", "sdk/library"), ("c", "sdk/library")):
            with self.subTest(kind=kind, name=name):
                self.fixture(text, rom, source, boundaries=(0, 8))
                config = self.root / "config/splat.ll.yaml"
                config.write_text(config.read_text().replace(", c, broken]", f", {kind}, {name}]"))
                report = self.split()
                self.assertEqual(len(report["functions"]), 3)
                self.assertEqual(report["diagnostics"]["classification"],
                                 dict(matchings=1, nonmatchings=1, total=2, scope="game_c_modules",
                                      method="splat_include_asm_references"))

    def test_source_references_classify_without_make_or_compilers(self):
        self.fixture()
        (self.root / "Makefile").unlink()
        with patch("subprocess.run", side_effect=AssertionError("split invoked a build")):
            report = self.split()
        self.assertEqual(self.statuses(report), dict(Good="assembly", AssemblyOnly="assembly",
                         Disabled="assembly", Empty="matching", Wrong="matching"))
        self.assertEqual(report["diagnostics"]["classification"]["method"], "splat_include_asm_references")
        self.assertNotIn("matching", report["diagnostics"])
        self.assertNotIn("section", report["functions"][0])
        self.assertTrue((self.root / "asm/matchings/unit/Wrong.s").is_file())
        self.assertFalse((self.root / "build/unit.o").exists())

    def test_inactive_fallbacks_count_but_comments_and_strings_do_not(self):
        source = SIMPLE_C.replace('INCLUDE_ASM("asm/nonmatchings/unit", Good);',
                                  '// INCLUDE_ASM("asm/nonmatchings/unit", Good);')
        source += '\nchar *note = "INCLUDE_ASM(\\\"asm/nonmatchings/unit\\\", Wrong);";\n'
        self.fixture(source=source)
        report = self.split()
        self.assertEqual(self.statuses(report)["Good"], "matching")
        self.assertEqual(self.statuses(report)["Disabled"], "assembly")
        self.assertEqual(self.statuses(report)["Wrong"], "matching")

    def test_alias_references_keep_the_shared_entry_in_nonmatchings(self):
        source = SIMPLE_C.replace('INCLUDE_ASM("asm/nonmatchings/unit", Good);',
                                  'INCLUDE_ASM("asm/nonmatchings/unit", GoodAlias);')
        self.fixture(source=source, extra_symbols="GoodAlias = 0x08000000; // type:func isa:thumb\n")
        report = self.split()
        self.assertEqual(self.statuses(report)["Good"], "assembly")
        self.assertIn('.include "nonmatchings/unit/Good.s"',
                      (self.root / "asm/nonmatchings/unit/GoodAlias.s").read_text())

    def test_classification_moves_with_include_references_not_c_bytes(self):
        self.fixture(source=None)
        self.split()
        source = self.root / "src/unit.c"
        source.write_text(source.read_text().replace('INCLUDE_ASM("asm/nonmatchings/unit", Good);',
                                                    'unsigned Good(void) { return 123; }'))
        report = self.split()
        self.assertEqual(self.statuses(report)["Good"], "matching")
        self.assertTrue((self.root / "asm/matchings/unit/Good.s").is_file())
        self.assertFalse((self.root / "asm/nonmatchings/unit/Good.s").exists())
        source.write_text(source.read_text().replace('unsigned Good(void) { return 123; }',
                                                    'INCLUDE_ASM("asm/matchings/unit", Good);'))
        self.assertEqual(self.statuses(self.split())["Good"], "assembly")
        self.assertTrue((self.root / "asm/nonmatchings/unit/Good.s").is_file())
        self.assertIn('INCLUDE_ASM("asm/nonmatchings/unit", Good);', source.read_text())

    def test_compile_errors_do_not_prevent_source_split(self):
        self.fixture(source="unsigned Good(void) { return missing_member.x; }\n")
        with patch("subprocess.run", side_effect=AssertionError("split invoked a build")):
            report = self.split()
        self.assertEqual(set(self.statuses(report).values()), {"matching"})
        self.assertTrue((self.root / "asm/matchings/unit/Good.s").is_file())

    def test_upstream_size_mismatch_does_not_reclassify_following_c(self):
        text = ('thumb_func_start First\nFirst:\n    movs r0, #7\n    adds r0, #1\n'
                '    bx lr\n    .hword 0\nthumb_func_start Second\nSecond:\n    movs r0, #9\n    bx lr\n')
        rom = bytes.fromhex("07 20 01 30 70 47 00 00 09 20 70 47")
        self.fixture(text, rom, 'unsigned First(void) { return 7; }\nunsigned Second(void) { return 9; }\n')
        report = self.split()
        self.assertEqual(self.statuses(report), dict(First="matching", Second="matching"))

    def test_disabled_c_without_a_fallback_is_classified_by_references(self):
        source = SIMPLE_C.replace('INCLUDE_ASM("asm/nonmatchings/unit", Disabled);', '')
        self.fixture(source=source)
        self.assertEqual(self.statuses(self.split())["Disabled"], "matching")

    def test_resplit_without_report_recovers_layout_and_renames(self):
        self.fixture()
        self.split()
        (self.root / "asm/manifest.json").unlink()
        config = self.root / "config/splat.ll.yaml"
        config.write_text(config.read_text().replace(", c, unit]", ", c, renamed]"))
        symbols = self.root / "config/symbols.ll.txt"
        symbols.write_text(symbols.read_text().replace("Good =", "NewGood ="))
        self.assembly = self.assembly.replace("Good", "NewGood")
        report = self.split()
        self.assertEqual(report["unit_renames"], {"renamed": "unit"})
        self.assertFalse((self.root / "src/unit.c").exists())
        self.assertIn("unsigned NewGood(void) { return 7; }", (self.root / "src/renamed.c").read_text())
        self.assertTrue((self.root / "asm/nonmatchings/renamed/NewGood.s").is_file())
        self.assertFalse((self.root / "asm/nonmatchings/unit/Good.s").exists())
        check_generated(self.root / "asm")

    def test_objdiff_uses_yaml_without_a_split_report(self):
        self.fixture()
        self.split()
        config = self.root / "config/splat.ll.yaml"
        document = generate_config(config)
        document["units"][0]["metadata"]["complete"] = True
        document["units"][0]["symbol_mappings"] = {"expected": "actual"}
        (self.root / "objdiff.json").write_text(json.dumps(document))
        (self.root / "asm/manifest.json").unlink()
        current = generate_config(config)
        self.assertEqual(current["units"][0]["base_path"], "build/unit.o")
        self.assertTrue(current["units"][0]["metadata"]["complete"])
        self.assertEqual(current["units"][0]["symbol_mappings"], {"expected": "actual"})
        (self.root / "asm/manifest.json").write_text("invalid optional report")
        self.assertEqual(generate_config(config), current)

    def test_objdiff_accepts_legacy_explicit_report_paths(self):
        self.fixture()
        self.split()
        self.assertEqual(generate_config(self.root / "config/splat.ll.yaml"),
                         generate_config(self.root / "asm/manifest.json"))

    def test_source_resplit_preserves_unchanged_file_timestamps(self):
        self.fixture()
        report = self.split()
        reference = self.root / "asm/matchings/unit/Wrong.s"
        timestamp = reference.stat().st_mtime_ns
        (self.root / "asm/manifest.json").unlink()
        self.assertEqual(self.split(), report)
        self.assertEqual(reference.stat().st_mtime_ns, timestamp)

    def test_missing_report_does_not_allow_unknown_files_to_be_deleted(self):
        self.fixture()
        self.split()
        (self.root / "asm/manifest.json").unlink()
        note = self.root / "asm/notes.txt"
        note.write_text("keep this file")
        with self.assertRaisesRegex(ValueError, "Unrecognized file"):
            self.split()
        self.assertEqual(note.read_text(), "keep this file")


class IncludePathTests(unittest.TestCase):
    def test_comments_and_strings_are_preserved(self):
        text = ('// INCLUDE_ASM("asm/nonmatchings/unit", Good);\n'
                'char *s = "INCLUDE_ASM(\\\"asm/nonmatchings/unit\\\", Good);";\n'
                '#if 0\nINCLUDE_ASM("asm/nonmatchings/unit", Good);\n#endif\n')
        expected = text.replace('#if 0\nINCLUDE_ASM("asm/nonmatchings/unit", Good);',
                                '#if 0\nINCLUDE_ASM("asm/matchings/unit", Good);')
        self.assertEqual(rewrite_asm_includes(text, {"Good": "asm/matchings/unit"}), expected)


if __name__ == "__main__":
    unittest.main()
