"""Dump M4A music data without changing its position or a single ROM byte.

The format and command names follow SoundDumper.cs / Nintendo's M4A driver.
Unlike the original dumper, tracks are followed through their branches, shared
objects have one definition, and padding is copied rather than regenerated.
No files are written here: the split emitter owns the returned file mapping.
"""

from __future__ import annotations

from bisect import bisect_right
from collections import Counter, deque
from dataclasses import dataclass
from pathlib import PurePosixPath
import re
import struct


_DURATIONS = tuple(range(25)) + (
    28, 30, 32, 36, 40, 42, 44, 48, 52, 54, 56, 60,
    64, 66, 68, 72, 76, 78, 80, 84, 88, 90, 92, 96,
)
_COMMANDS = {0x80 + i: f"W{n:02d}" for i, n in enumerate(_DURATIONS)}
_COMMANDS.update({0xCF + i: f"N{n:02d}" for i, n in enumerate(_DURATIONS) if i})
_COMMANDS.update({
    0xB1: "FINE", 0xB2: "GOTO", 0xB3: "PATT", 0xB4: "PEND",
    0xB5: "REPT", 0xB9: "MEMACC", 0xBA: "PRIO", 0xBB: "TEMPO",
    0xBC: "KEYSH", 0xBD: "VOICE", 0xBE: "VOL", 0xBF: "PAN",
    0xC0: "BEND", 0xC1: "BENDR", 0xC2: "LFOS", 0xC3: "LFODL",
    0xC4: "MOD", 0xC5: "MODT", 0xC8: "TUNE", 0xCD: "XCMD",
    0xCE: "EOT", 0xCF: "TIE",
})
_SINGLE_ARG = set(range(0xBA, 0xC6)) | {0xC8}
_NOTES = ("Cn", "Cs", "Dn", "Ds", "En", "Fn", "Fs", "Gn", "Gs", "An", "As", "Bn")
_KEYS = tuple(f"{_NOTES[i % 12]}{'M' + str(2 - i // 12) if i < 24 else str(i // 12 - 2)}" for i in range(128))
_MEMOPS = (
    "mem_set", "mem_add", "mem_sub", "mem_mem_set", "mem_mem_add",
    "mem_mem_sub", "mem_beq", "mem_bne", "mem_bhi", "mem_bhs",
    "mem_bls", "mem_blo", "mem_mem_beq", "mem_mem_bne", "mem_mem_bhi",
    "mem_mem_bhs", "mem_mem_bls", "mem_mem_blo",
)
_XCMD = {
    0: ("xWAVE", 4), 1: ("xTYPE", 1), 2: ("xATTA", 1),
    3: ("xDECA", 1), 4: ("xSUST", 1), 5: ("xRELE", 1),
    6: ("xXXXX", 0), 7: ("xXXXXX", 0), 8: ("xIECV", 1),
    9: ("xIECL", 1), 10: ("xLENG", 1), 11: ("xSWEE", 1),
}
_VOICE_NAMES = {
    0x00: "voice_directsound", 0x08: "voice_directsound_no_resample",
    0x10: "voice_directsound_alt", 0x01: "voice_square_1",
    0x09: "voice_square_1_alt", 0x02: "voice_square_2",
    0x0A: "voice_square_2_alt", 0x03: "voice_programmable_wave",
    0x0B: "voice_programmable_wave_alt", 0x04: "voice_noise",
    0x0C: "voice_noise_alt", 0x40: "voice_keysplit", 0x80: "voice_keysplit_all",
}


@dataclass(frozen=True)
class _Span:
    start: int
    end: int
    kind: str
    path: str


@dataclass(frozen=True)
class _Instruction:
    end: int
    lines: tuple[str, ...]


def _mplay_definitions() -> str:
    lines = [
        "@ M4A bytecode and voice macros. All reserved bytes remain explicit.",
        ".ifndef _GBA_SPLIT_MPLAY_DEFINITIONS", ".set _GBA_SPLIT_MPLAY_DEFINITIONS, 1",
    ]
    for opcode, name in sorted(_COMMANDS.items()):
        lines.append(f".equ {name}, 0x{opcode:02X}")
    for value, name in enumerate(_KEYS):
        lines.append(f".equ {name}, {value}")
        lines.append(f".equ v{value:03d}, {value}")
    for value, name in enumerate(_MEMOPS):
        lines.append(f".equ {name}, {value}")
    for value, (name, _) in _XCMD.items():
        lines.append(f".equ {name}, {value}")
    lines.extend([".equ c_v, 64", ".equ mxv, 127", ".equ reverb_set, 128"])
    lines.extend([
        ".macro song header, music_player, unknown",
        "\t.4byte \\header", "\t.2byte \\music_player, \\unknown", ".endm",
        ".macro music_player info, tracks, unknown1, unknown2, reserved=0",
        "\t.4byte \\info, \\tracks", "\t.byte \\unknown1, \\reserved",
        "\t.2byte \\unknown2", ".endm",
    ])
    for typ, name in _VOICE_NAMES.items():
        if typ in (0, 8, 16):
            args = "key, pan, sample, attack, decay, sustain, release, length=0"
            data = [f"\t.byte {typ}, \\key, \\length, \\pan", "\t.4byte \\sample"]
        elif typ in (3, 11):
            args = "key, pan, wave, attack, decay, sustain, release, length=0"
            data = [f"\t.byte {typ}, \\key, \\pan, \\length", "\t.4byte \\wave"]
        elif typ in (1, 9, 2, 10, 4, 12):
            args = "key, pan, sweep, duty, attack, decay, sustain, release, reserved=0"
            data = [
                f"\t.byte {typ}, \\key, \\pan, \\sweep, \\duty",
                "\t.byte (\\reserved & 255), ((\\reserved >> 8) & 255), ((\\reserved >> 16) & 255)",
            ]
        elif typ == 0x40:
            args = "group, keymap, key=0, length=0, pan=0"
            data = ["\t.byte 64, \\key, \\length, \\pan", "\t.4byte \\group, \\keymap"]
        else:
            args = "group, key=0, length=0, pan=0, reserved=0"
            data = ["\t.byte 128, \\key, \\length, \\pan", "\t.4byte \\group, \\reserved"]
        if typ not in (0x40, 0x80):
            data.append("\t.byte \\attack, \\decay, \\sustain, \\release")
        lines.extend([f".macro {name} {args}", *data, ".endm"])
    lines.extend([".endif", ""])
    return "\n".join(lines)


class _SoundDumper:
    def __init__(self, rom: bytes, start: int, end: int, name: str, options: dict, base: int):
        if not isinstance(rom, (bytes, bytearray, memoryview)):
            raise ValueError("sound ROM must be bytes")
        for key, value in (("start", start), ("end", end), ("base", base)):
            if not isinstance(value, int) or isinstance(value, bool):
                raise ValueError(f"sound {key} must be an integer")
        if not 0 <= start < end <= len(rom):
            raise ValueError(f"sound range [0x{start:X}, 0x{end:X}) is outside ROM size 0x{len(rom):X}")
        if not 0 <= base <= 0xFFFFFFFF or base + len(rom) > 0x100000000:
            raise ValueError("sound base + ROM size must fit in a 32-bit address")
        if not isinstance(name, str) or not name or any(c in name for c in '\\"\n\r'):
            raise ValueError("sound name must be a non-empty relative asset path")
        path = PurePosixPath(name)
        if path.is_absolute() or ".." in path.parts or path.as_posix() == ".":
            raise ValueError("sound name must be a relative asset path without '..'")
        if not isinstance(options, dict):
            raise ValueError("sound options must be a mapping")
        self.rom = bytes(rom)
        self.start, self.end, self.base = start, end, base
        self.prefix = re.sub(r"[^A-Za-z0-9_]", "_", name)
        if self.prefix[0].isdigit():
            self.prefix = "sound_" + self.prefix
        self.directory = f"assets/{path.as_posix()}"
        self.options = options
        self.files: dict[str, str | bytes] = {}
        self.symbols: dict[str, int] = {}
        self.labels: dict[int, list[str]] = {}
        self.spans: list[_Span] = []
        self.instructions: dict[int, _Instruction] = {}
        self.conflicts: set[int] = set()
        self.raw_ranges: list[dict] = []
        self.warnings: list[dict] = []
        self.warning_keys: set[tuple] = set()
        self.headers: dict[int, dict] = {}
        self.track_roots: dict[int, list[str]] = {}
        self.voicegroups: set[int] = set()
        self.samples: set[int] = set()
        self.waves: set[int] = set()
        self.keymaps: set[int] = set()
        self.voice_records: dict[int, list[int]] = {}
        self.group_ends: dict[int, int] = {}
        self.inferred_group_ends: dict[int, int] = {}
        self.sample_info: list[dict] = []
        self.song_table = self._option_address("song_table", required=True)
        self.song_count = self._count("song_count", required=True)
        self._require(self.song_table, 8 * self.song_count, "song table")
        self.mplay_table = self._option_address("mplay_table")
        self.mplay_count = self._count("mplay_count", required=self.mplay_table is not None)
        if self.mplay_table is None and self.mplay_count is not None:
            raise ValueError("sound mplay_count requires mplay_table")
        if self.mplay_table is not None:
            self._require(self.mplay_table, 12 * self.mplay_count, "music player table")
        self.voicegroup_end = self._option_address("voicegroup_end", allow_end=True)

    def _number(self, value: object, description: str) -> int:
        if isinstance(value, str):
            try:
                value = int(value, 0)
            except ValueError:
                raise ValueError(f"sound {description} must be an integer or hexadecimal address") from None
        if not isinstance(value, int) or isinstance(value, bool):
            raise ValueError(f"sound {description} must be an integer")
        return value

    def _count(self, key: str, required: bool = False) -> int | None:
        if key not in self.options:
            if required:
                raise ValueError(f"sound requires '{key}'")
            return None
        value = self._number(self.options[key], key)
        if value <= 0:
            raise ValueError(f"sound {key} must be positive")
        return value

    def _option_address(self, key: str, required: bool = False, allow_end: bool = False) -> int | None:
        if key not in self.options:
            if required:
                raise ValueError(f"sound requires '{key}'")
            return None
        address = self._number(self.options[key], key)
        if self.base <= address <= self.base + len(self.rom):
            address -= self.base
        self._require(address, 0 if allow_end else 1, key)
        return address

    def _require(self, offset: int, size: int, description: str) -> None:
        if not self.start <= offset <= self.end or size < 0 or offset + size > self.end:
            raise ValueError(
                f"sound {description} at ROM 0x{offset:X} needs 0x{size:X} bytes; "
                f"configured resource is [0x{self.start:X}, 0x{self.end:X})"
            )

    def _pointer(self, value: int, description: str, size: int = 1) -> int:
        if not self.base <= value < self.base + len(self.rom):
            raise ValueError(f"sound {description} has non-ROM pointer 0x{value:08X}")
        offset = value - self.base
        self._require(offset, size, description)
        return offset

    def _u32(self, offset: int) -> int:
        return struct.unpack_from("<I", self.rom, offset)[0]

    def _label(self, offset: int, name: str) -> str:
        self._require(offset, 0, f"label {name}")
        previous = self.symbols.get(name)
        if previous is not None and previous != self.base + offset:
            raise ValueError(f"sound label {name} refers to two addresses")
        self.symbols[name] = self.base + offset
        names = self.labels.setdefault(offset, [])
        if name not in names:
            names.append(name)
        return name

    def _ref(self, address: int) -> str:
        names = self.labels.get(address - self.base)
        return names[0] if names else f"0x{address:08X}"

    def _warning(self, reason: str, offset: int, **details) -> None:
        key = (reason, offset, tuple(sorted(details.items())))
        if key not in self.warning_keys:
            self.warning_keys.add(key)
            self.warnings.append({"reason": reason, "offset": offset, **details})

    def _span(self, start: int, end: int, kind: str, filename: str) -> None:
        self._require(start, end - start, kind)
        if start == end:
            return
        self.spans.append(_Span(start, end, kind, f"{self.directory}/{filename}"))

    def _read_songs(self) -> None:
        self._label(self.song_table, f"{self.prefix}_song_table")
        self._span(self.song_table, self.song_table + self.song_count * 8, "song_table", "song_table.inc")
        if self.mplay_table is not None:
            self._label(self.mplay_table, f"{self.prefix}_mplay_table")
            self._span(self.mplay_table, self.mplay_table + self.mplay_count * 12, "mplay_table", "mplay_table.inc")
        for index in range(self.song_count):
            entry = self.song_table + index * 8
            pointer = self._u32(entry)
            if pointer == 0:
                continue
            header = self._pointer(pointer, f"song table entry {index} header", 8)
            song = self._label(header, f"{self.prefix}_song_{index:04d}")
            if header in self.headers:
                self.headers[header]["ids"].append(index)
                continue
            count, blocks, priority, reverb = self.rom[header:header + 4]
            self._require(header, 8 + 4 * count, f"song {index} header/tracks")
            tone = self._u32(header + 4)
            if count:
                self.voicegroups.add(self._pointer(tone, f"song {index} voicegroup", 12))
            if blocks:
                self._warning("nonzero_song_block_count", header, block_count=blocks)
            tracks = []
            for track_index in range(count):
                track = self._pointer(self._u32(header + 8 + 4 * track_index), f"song {index} track {track_index + 1}")
                track_name = self._label(track, f"{song}_{track_index + 1}")
                self.track_roots.setdefault(track, []).append(track_name)
                tracks.append(track)
            self.headers[header] = {"ids": [index], "tracks": tracks, "tone": tone}
            self._span(header, header + 8 + 4 * count, "song", f"songs/{song}.inc")

    def _voice_limit(self, group: int) -> int:
        starts = self.voicegroups | self.samples | self.waves | self.keymaps | set(self.track_roots)
        starts.update(s.start for s in self.spans)
        limits = [p for p in starts if p > group]
        if group in self.inferred_group_ends:
            limits.append(self.inferred_group_ends[group])
        if self.voicegroup_end is not None:
            if group >= self.voicegroup_end:
                raise ValueError(f"sound voicegroup at 0x{group:X} is at or past voicegroup_end 0x{self.voicegroup_end:X}")
            limits.append(self.voicegroup_end)
        return min(limits + [self.end])

    def _read_voices(self) -> None:
        # A keysplit can introduce a group inside an initially larger range.
        # Repartition before making the final, strict pointer checks.
        while True:
            before = (frozenset(self.voicegroups), frozenset(self.samples), frozenset(self.waves), frozenset(self.keymaps))
            for group in sorted(self.voicegroups):
                limit = self._voice_limit(group)
                for record in range(group, limit - 11, 12):
                    # Samples and child groups discovered in this scan can
                    # shorten an initially inferred voicebank immediately.
                    if record + 12 > self._voice_limit(group):
                        break
                    typ = self.rom[record]
                    first = self._u32(record + 4)
                    if typ in (0, 8, 16):
                        target = self.samples
                    elif typ in (3, 11):
                        target = self.waves
                    elif typ in (0x40, 0x80):
                        target = self.voicegroups
                    else:
                        continue
                    if (
                        self.voicegroup_end is None and record > group and first
                        and not self.base <= first < self.base + len(self.rom)
                    ):
                        # Without an explicit bank end, adjacent tables can
                        # resemble a voice type. A non-ROM sample/group field
                        # is a boundary hint after a valid bank, not a new
                        # sample to manufacture. Keep the remainder raw and
                        # make the inference visible in the metadata.
                        self.inferred_group_ends[group] = record
                        self._warning("inferred_voicegroup_boundary", record, group=group, pointer=first)
                        break
                    if first and self.base + self.start <= first < self.base + self.end:
                        target.add(first - self.base)
                    if typ == 0x40:
                        keymap = self._u32(record + 8)
                        if self.base + self.start <= keymap < self.base + self.end:
                            self.keymaps.add(keymap - self.base)
            after = (frozenset(self.voicegroups), frozenset(self.samples), frozenset(self.waves), frozenset(self.keymaps))
            if before == after:
                break
        # Only references in the final group ranges are objects. Temporary
        # scans may have crossed a newly discovered sample or child group.
        limits = {group: self._voice_limit(group) for group in self.voicegroups}
        self.samples.clear()
        self.waves.clear()
        self.keymaps.clear()
        for index, group in enumerate(sorted(self.voicegroups)):
            limit = limits[group]
            if limit - group < 12:
                raise ValueError(f"sound voicegroup at 0x{group:X} has less than one 12-byte voice before 0x{limit:X}")
            self.group_ends[group] = limit
            self._label(group, f"{self.prefix}_voicegroup{index:03d}")
            self._span(group, limit, "voicegroup", f"voicegroups/{self.prefix}_voicegroup{index:03d}.inc")
            self.voice_records[group] = list(range(group, limit - 11, 12))
            for record in self.voice_records[group]:
                typ = self.rom[record]
                pointer = self._u32(record + 4)
                if typ in (0, 8, 16) and pointer:
                    self.samples.add(self._pointer(pointer, f"direct sound voice at 0x{record:X}", 16))
                elif typ in (3, 11) and pointer:
                    self.waves.add(self._pointer(pointer, f"programmable wave voice at 0x{record:X}", 16))
                elif typ in (0x40, 0x80):
                    self._pointer(pointer, f"keysplit voicegroup at 0x{record:X}", 12)
                    if typ == 0x40:
                        self.keymaps.add(self._pointer(self._u32(record + 8), f"keysplit map at 0x{record:X}", 128))
                elif typ not in _VOICE_NAMES:
                    self._warning("unknown_voice_type", record, type=typ)
            if (limit - group) % 12:
                self._warning("voicegroup_trailing_bytes", limit - (limit - group) % 12, size=(limit - group) % 12)

    def _read_samples(self) -> None:
        landmarks = {s.start for s in self.spans} | self.samples | self.waves | self.keymaps | set(self.track_roots)
        for sample in sorted(self.samples):
            typ, status, frequency, loop_start, size = struct.unpack_from("<HHIII", self.rom, sample)
            self._require(sample, 16 + size, "direct sound sample header/payload")
            nominal_end = sample + 16 + size
            # SoundDumper.cs includes one interpolation guard sample. Some
            # banks put the next object immediately after the declared data.
            guard = int(nominal_end < self.end and nominal_end not in landmarks)
            sample_end = nominal_end + guard
            label = self._label(sample, f"{self.prefix}_sample_{self.base + sample:08X}")
            self._span(sample, sample_end, "sample", f"direct_sound_samples/{label}.inc")
            payload = f"{self.directory}/direct_sound_samples/{label}.pcm"
            self.files[payload] = self.rom[sample + 16:sample_end]
            self.sample_info.append({
                "offset": sample, "type": typ, "status": status, "frequency": frequency,
                "loop_start": loop_start, "size": size, "guard_bytes": guard,
            })
            if typ not in (0, 1):
                self._warning("unknown_sample_type", sample, type=typ)
        for wave in sorted(self.waves):
            label = self._label(wave, f"{self.prefix}_wave_{self.base + wave:08X}")
            self._span(wave, wave + 16, "wave", f"programmable_wave_samples/{label}.inc")
            self.files[f"{self.directory}/programmable_wave_samples/{label}.pcm"] = self.rom[wave:wave + 16]
        for keymap in sorted(self.keymaps):
            label = self._label(keymap, f"{self.prefix}_keysplit_{self.base + keymap:08X}")
            self._span(keymap, keymap + 128, "keymap", f"keysplit_tables/{label}.inc")

    def _validate_spans(self) -> None:
        previous = None
        for span in sorted(self.spans, key=lambda s: s.start):
            if previous is not None and span.start < previous.end:
                raise ValueError(
                    f"sound objects overlap: {previous.kind} [0x{previous.start:X},0x{previous.end:X}) "
                    f"and {span.kind} [0x{span.start:X},0x{span.end:X})"
                )
            previous = span

    def _read_tracks(self) -> None:
        fixed = sorted(self.spans, key=lambda s: s.start)
        fixed_starts = [s.start for s in fixed]
        roots = sorted(self.track_roots)
        landmarks = sorted(set(fixed_starts + roots + [self.end]))
        track_spans: dict[int, _Span] = {}

        def owning_track(offset: int, origin: str) -> _Span:
            self._require(offset, 1, f"track {origin}")
            fixed_index = bisect_right(fixed_starts, offset) - 1
            if fixed_index >= 0 and offset < fixed[fixed_index].end:
                span = fixed[fixed_index]
                raise ValueError(f"sound track {origin} at 0x{offset:X} points into {span.kind} [0x{span.start:X},0x{span.end:X})")
            for span in track_spans.values():
                if span.start <= offset < span.end:
                    return span
            limit = min(
                [landmarks[bisect_right(landmarks, offset)]]
                + [s.start for s in track_spans.values() if s.start > offset]
            )
            label = self.labels.get(offset, [f"{self.prefix}_pattern_{self.base + offset:08X}"])[0]
            self._label(offset, label)
            span = _Span(offset, limit, "track", f"{self.directory}/tracks/{label}.inc")
            track_spans[offset] = span
            return span

        for root in roots:
            owning_track(root, self.track_roots[root][0])
        queue = deque((root, 0, self.track_roots[root][0], False) for root in roots)
        visited: set[tuple[int, int, bool]] = set()
        while queue:
            pc, status, origin, inside_pattern = queue.popleft()
            span = owning_track(pc, origin)
            while pc < span.end and (pc, status, inside_pattern) not in visited:
                visited.add((pc, status, inside_pattern))
                result = self._decode(pc, status, span.end, origin)
                if result is None:
                    break
                instruction, new_status, branches, terminate = result
                previous = self.instructions.get(pc)
                if previous is not None and previous != instruction:
                    self.conflicts.add(pc)
                    self._warning("ambiguous_running_status", pc)
                else:
                    self.instructions[pc] = instruction
                for target in branches:
                    if target not in self.labels:
                        self._label(target, f"{origin}_label_{self.base + target:08X}")
                    owning_track(target, origin)
                    queue.append((target, new_status, origin, inside_pattern or self.rom[pc] == 0xB3))
                # A pattern definition can occur inline on its first play.
                # The M4A PEND command falls through when the return stack is
                # empty, rather than terminating that song track.
                if self.rom[pc] == 0xB4 and not inside_pattern:
                    terminate = False
                pc, status = instruction.end, new_status
                if terminate:
                    break
        self.spans.extend(track_spans.values())

    def _decode(self, pc: int, status: int, limit: int, origin: str):
        first = self.rom[pc]
        explicit = first >= 0x80
        opcode = first if explicit else status
        if opcode not in _COMMANDS or (not explicit and opcode < 0xBD):
            self._warning("unknown_track_opcode" if explicit else "missing_running_status", pc, byte=first)
            return None
        name = _COMMANDS[opcode]
        pos = pc + int(explicit)
        values = [name] if explicit else []
        branches = []
        words = []
        new_status = opcode if explicit and opcode >= 0xBD else status

        def need(count: int) -> None:
            if pos + count > limit:
                raise ValueError(f"sound track {origin} {name} at 0x{pc:X} is truncated at object boundary 0x{limit:X}")

        def byte(formatter=str) -> int:
            nonlocal pos
            need(1)
            value = self.rom[pos]
            values.append(formatter(value))
            pos += 1
            return value

        def branch() -> None:
            nonlocal pos
            need(4)
            address = self._u32(pos)
            target = self._pointer(address, f"{origin} {name} at 0x{pc:X}")
            if target not in self.labels:
                self._label(target, f"{origin}_label_{address:08X}")
            words.append(self._ref(address))
            branches.append(target)
            pos += 4

        if opcode <= 0xB0 or opcode in (0xB1, 0xB4):
            pass
        elif opcode in (0xB2, 0xB3, 0xB5):
            if opcode == 0xB5:
                byte()
            branch()
        elif opcode == 0xB9:
            op = byte(lambda n: _MEMOPS[n] if n < len(_MEMOPS) else str(n))
            byte(lambda n: f"0x{n:02X}")
            byte()
            if 6 <= op < len(_MEMOPS):
                branch()
            elif op >= len(_MEMOPS):
                self._warning("unknown_memacc_operation", pc, operation=op)
        elif opcode in _SINGLE_ARG:
            def arg(n: int) -> str:
                if opcode == 0xBE and n < 128:
                    return f"v{n:03d}"
                if opcode in (0xBF, 0xC0, 0xC8):
                    return "c_v" if n == 64 else f"c_v{n - 64:+d}"
                return str(n)
            byte(arg)
        elif opcode == 0xCD:
            need(1)
            sub = self.rom[pos]
            if sub not in _XCMD:
                self._warning("unknown_extended_command", pc, command=sub)
                return None
            subname, size = _XCMD[sub]
            byte(lambda n: subname)
            need(size)
            if size == 4:
                words.append(self._ref(self._u32(pos)))
                pos += 4
            elif size:
                byte()
            elif sub in (6, 7):
                self._warning("unsupported_extended_stop_command", pc, command=sub)
        elif opcode >= 0xCE:
            count = 1 if opcode == 0xCE else 2 if opcode == 0xCF else 3
            for i in range(count):
                if pos >= limit or self.rom[pos] >= 0x80:
                    break
                formatter = (lambda n: _KEYS[n]) if i == 0 else (lambda n: f"v{n:03d}") if i == 1 else str
                byte(formatter)
        if pos == pc:
            self._warning("empty_running_status_command", pc, opcode=opcode)
            return None
        lines = ["\t.byte " + ", ".join(values)] if values else []
        lines.extend("\t.4byte " + word for word in words)
        if not explicit and lines:
            lines[0] += f" @ running {name}"
        terminate = opcode in (0xB1, 0xB2, 0xB4) or (opcode == 0xCD and sub in (6, 7))
        return _Instruction(pos, tuple(lines)), new_status, branches, terminate

    def _labels_at(self, offset: int) -> list[str]:
        lines = []
        for label in self.labels.get(offset, []):
            lines.extend([f"\t.global {label}", f"{label}: @ 0x{self.base + offset:08X}"])
        return lines

    def _raw(self, start: int, end: int, reason: str) -> list[str]:
        if start == end:
            return []
        self.raw_ranges.append({"offset": start, "size": end - start, "reason": reason})
        lines = [f"\t@ {reason}: ROM 0x{start:X}, {end - start} bytes"]
        if end - start > 32:
            path = f"{self.directory}/raw/{self.base + start:08X}.bin"
            self.files[path] = self.rom[start:end]
            lines.append(f'\t.incbin "{path}"')
        else:
            for pos in range(start, end, 16):
                lines.append("\t.byte " + ", ".join(f"0x{n:02X}" for n in self.rom[pos:min(pos + 16, end)]))
        return lines

    def _voice(self, offset: int) -> list[str]:
        data = self.rom[offset:offset + 12]
        typ = data[0]
        if typ not in _VOICE_NAMES:
            return self._raw(offset, offset + 12, f"unknown voice type 0x{typ:02X}")
        pointer = self._u32(offset + 4)
        tail = list(data[8:12])
        if typ in (0, 8, 16):
            args = [data[1], data[3], self._ref(pointer), *tail, data[2]]
        elif typ in (3, 11):
            args = [data[1], data[2], self._ref(pointer), *tail, data[3]]
        elif typ in (1, 9, 2, 10, 4, 12):
            reserved = int.from_bytes(data[5:8], "little")
            args = [data[1], data[2], data[3], data[4], *tail, f"0x{reserved:06X}"]
        elif typ == 0x40:
            args = [self._ref(pointer), self._ref(self._u32(offset + 8)), data[1], data[2], data[3]]
        else:
            args = [self._ref(pointer), data[1], data[2], data[3], f"0x{self._u32(offset + 8):08X}"]
        return [f"\t{_VOICE_NAMES[typ]} " + ", ".join(map(str, args)) + f" @ 0x{self.base + offset:08X}"]

    def _render_track(self, span: _Span) -> list[str]:
        relevant = sorted(p for p in self.instructions if span.start <= p < span.end)
        label_positions = sorted(p for p in self.labels if span.start <= p < span.end)
        boundaries = sorted(set(relevant + label_positions + [span.end]))
        lines = []
        pc = span.start
        while pc < span.end:
            lines.extend(self._labels_at(pc))
            instruction = self.instructions.get(pc)
            next_boundary = boundaries[bisect_right(boundaries, pc)]
            # Labels in an operand or different decodes of shared running
            # status are represented byte-for-byte instead of inventing a
            # single instruction interpretation.
            if instruction is not None and pc not in self.conflicts and instruction.end <= next_boundary:
                lines.extend(instruction.lines)
                pc = instruction.end
            else:
                reason = "unparsed track/padding"
                if instruction is not None:
                    reason = "overlapping track interpretation"
                    self._warning(reason, pc)
                lines.extend(self._raw(pc, next_boundary, reason))
                pc = next_boundary
        return lines

    def _render_span(self, span: _Span) -> str:
        lines = [f"@ {span.kind}: ROM [0x{span.start:X}, 0x{span.end:X})"]
        if span.kind == "track":
            lines.extend(self._render_track(span))
            return "\n".join(lines) + "\n"
        lines.extend(self._labels_at(span.start))
        if span.kind == "song_table":
            for index in range(self.song_count):
                offset = span.start + 8 * index
                header, start_player, end_player = struct.unpack_from("<IHH", self.rom, offset)
                lines.append(f"\tsong {self._ref(header)}, {start_player}, {end_player} @ {index}")
        elif span.kind == "mplay_table":
            for index in range(self.mplay_count):
                offset = span.start + 12 * index
                info, tracks, unknown1, reserved, unknown2 = struct.unpack_from("<IIBBH", self.rom, offset)
                lines.append(f"\tmusic_player {self._ref(info)}, {self._ref(tracks)}, {unknown1}, {unknown2}, {reserved}")
        elif span.kind == "song":
            for i, field in enumerate(("trackCount", "blockCount", "priority", "reverb")):
                lines.append(f"\t.byte {self.rom[span.start + i]} @ {field}")
            lines.append(f"\t.4byte {self._ref(self._u32(span.start + 4))} @ voicegroup")
            for offset in range(span.start + 8, span.end, 4):
                lines.append(f"\t.4byte {self._ref(self._u32(offset))} @ track")
        elif span.kind == "voicegroup":
            for record in self.voice_records[span.start]:
                lines.extend(self._voice(record))
            tail_start = span.end - (span.end - span.start) % 12
            lines.extend(self._raw(tail_start, span.end, "voicegroup trailing bytes"))
        elif span.kind == "sample":
            typ, status, frequency, loop_start, size = struct.unpack_from("<HHIII", self.rom, span.start)
            lines.extend([
                f"\t.2byte {typ}, {status} @ type, status",
                f"\t.4byte {frequency} @ frequency", f"\t.4byte {loop_start} @ loop start",
                f"\t.4byte {size} @ declared sample count",
                f'\t.incbin "{span.path[:-4]}.pcm"',
            ])
        elif span.kind == "wave":
            lines.append(f'\t.incbin "{span.path[:-4]}.pcm"')
        elif span.kind == "keymap":
            for offset in range(span.start, span.end, 16):
                lines.append("\t.byte " + ", ".join(map(str, self.rom[offset:offset + 16])))
        return "\n".join(lines) + "\n"

    def dump(self, ranges=None) -> dict:
        self._read_songs()
        self._read_voices()
        self._read_samples()
        self._validate_spans()
        self._read_tracks()
        self._validate_spans()
        definitions = f"{self.directory}/MPlayDef.inc"
        self.files[definitions] = _mplay_definitions()
        for span in sorted(self.spans, key=lambda s: s.start):
            self.files[span.path] = self._render_span(span)
        bodies = {}
        for start, end in ranges or [(self.start, self.end)]:
            self._require(start, end - start, "sound slice")
            body = [f'\t.include "{definitions}"']
            position = start
            for span in sorted(self.spans, key=lambda s: s.start):
                if span.end <= start or span.start >= end:
                    continue
                if not start <= span.start < span.end <= end:
                    raise ValueError(f"Sound slice cuts {span.kind} at ROM 0x{span.start:X}")
                body.extend(self._raw(position, span.start, "preserved sound gap/alignment"))
                body.append(f'\t.include "{span.path}"')
                position = span.end
            body.extend(self._raw(position, end, "preserved sound gap/alignment"))
            bodies[start] = "\n".join(body) + "\n"
        metadata = {
            "format": "m4a", "start": self.start, "end": self.end,
            "song_table": self.song_table, "song_count": self.song_count,
            "mplay_table": self.mplay_table, "mplay_count": self.mplay_count,
            "voicegroup_end": self.voicegroup_end, "inferred_voicegroup_ends": self.inferred_group_ends,
            "songs": len(self.headers), "track_roots": len(self.track_roots),
            "track_fragments": sum(s.kind == "track" for s in self.spans),
            "track_instructions": len(self.instructions), "voicegroups": len(self.voicegroups),
            "voice_records": sum(map(len, self.voice_records.values())),
            "voice_types": dict(sorted(Counter(self.rom[r] for records in self.voice_records.values() for r in records).items())),
            "direct_sound_samples": len(self.samples), "programmable_wave_samples": len(self.waves),
            "keysplit_tables": len(self.keymaps), "samples": self.sample_info,
            "duplicate_song_headers": {f"0x{p:X}": h["ids"] for p, h in self.headers.items() if len(h["ids"]) > 1},
            "shared_tracks": {f"0x{p:X}": names for p, names in self.track_roots.items() if len(names) > 1},
            "raw_bytes": sum(r["size"] for r in self.raw_ranges), "raw_ranges": self.raw_ranges,
            "warnings": self.warnings,
        }
        return {"body": next(iter(bodies.values())), "bodies": bodies,
                "files": self.files, "symbols": self.symbols, "metadata": metadata}


def dump_sound(rom: bytes, start: int, end: int, name: str, options: dict, base: int = 0x08000000,
               ranges=None) -> dict:
    """Return an exact M4A assembly dump of the configured resource.

    ``start`` and ``end`` are file offsets. ``song_table`` and the optional
    ``mplay_table`` / ``voicegroup_end`` accept file offsets or ROM addresses.
    ``song_count`` is required; ``mplay_count`` accompanies ``mplay_table``.
    Every referenced song, track, voicegroup and sample must fit in the range.
    The assembly body uses paths relative to the split assembly directory.
    Optional output ranges select whole objects while parsing the full resource.
    """
    return _SoundDumper(rom, start, end, name, options, base).dump(ranges)
