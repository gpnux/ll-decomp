"""GBA BIOS compression, 4/8bpp tiles, and BG maps using the standard library.

PNG files are views of the tile data. Keep the original resource bytes when
linking the ROM: recompressing a resource need not reproduce its original LZ
token choices or alignment padding.
"""

from collections import deque
from collections.abc import Sequence
import struct
import zlib


MAX_LZ_SIZE = 0xFFFFFF  # The BIOS header stores a 24-bit uncompressed size.
MAX_IMAGE_PIXELS = 64 * 1024 * 1024


def decompress_lz(data: bytes, *, max_output_size: int = MAX_LZ_SIZE) -> tuple[bytes, int]:
    """Decode a BIOS type-0x10 stream, returning (bytes, bytes_consumed).

    Consumption includes the header and used tokens, but excludes any trailing
    alignment padding. Invalid headers, truncated tokens, references preceding
    the output, and tokens exceeding the declared output size raise ValueError.
    """
    if not isinstance(max_output_size, int) or isinstance(max_output_size, bool) or max_output_size < 0:
        raise ValueError("LZ output limit must be a non-negative integer")
    data = memoryview(data).cast("B")
    if len(data) < 4:
        raise ValueError("Truncated GBA LZ77 header")
    if data[0] != 0x10:
        raise ValueError(f"Unsupported GBA compression header 0x{data[0]:02X}; expected 0x10")
    size = data[1] | data[2] << 8 | data[3] << 16
    if size > max_output_size:
        raise ValueError(f"GBA LZ77 output size {size} exceeds limit {max_output_size}")
    output = bytearray()
    cursor = 4
    while len(output) < size:
        if cursor >= len(data):
            raise ValueError(f"Truncated GBA LZ77 flag byte at 0x{cursor:X}")
        flags = data[cursor]
        cursor += 1
        for bit in range(7, -1, -1):
            if len(output) == size:
                break
            if flags & (1 << bit):
                if cursor + 2 > len(data):
                    raise ValueError(f"Truncated GBA LZ77 reference at 0x{cursor:X}")
                first, second = data[cursor], data[cursor + 1]
                cursor += 2
                length = (first >> 4) + 3
                distance = ((first & 15) << 8 | second) + 1
                if distance > len(output):
                    raise ValueError(f"GBA LZ77 reference before output at 0x{cursor - 2:X}")
                if len(output) + length > size:
                    raise ValueError(f"GBA LZ77 reference exceeds declared output at 0x{cursor - 2:X}")
                # Copy a byte at a time: a valid reference may overlap itself.
                for _ in range(length):
                    output.append(output[-distance])
            else:
                if cursor >= len(data):
                    raise ValueError(f"Truncated GBA LZ77 literal at 0x{cursor:X}")
                output.append(data[cursor])
                cursor += 1
    return bytes(output), cursor


def compress_lz(data: bytes) -> bytes:
    """Encode a deterministic BIOS type-0x10 stream without alignment padding.

    Greedy matches use the full 4096-byte window and at most 18 bytes per token.
    The result is BIOS-compatible; matching another compressor's bytes is not
    promised. Only the last 4096 candidate positions are kept in memory.
    """
    data = bytes(data)
    size = len(data)
    if size > MAX_LZ_SIZE:
        raise ValueError(f"GBA LZ77 input exceeds the 24-bit size limit ({MAX_LZ_SIZE})")
    result = bytearray((0x10, size & 255, size >> 8 & 255, size >> 16))
    candidates: dict[int, deque[int]] = {}
    window: list[tuple[int, int] | None] = [None] * 4096

    def key_at(position):
        return data[position] << 16 | data[position + 1] << 8 | data[position + 2]

    def remember(position):
        if position + 3 > size:
            return
        slot = position & 4095
        old = window[slot]
        if old is not None:
            old_key, _ = old
            chain = candidates[old_key]
            chain.popleft()
            if not chain:
                del candidates[old_key]
        key = key_at(position)
        candidates.setdefault(key, deque()).append(position)
        window[slot] = (key, position)

    position = 0
    while position < size:
        flag_position = len(result)
        result.append(0)
        for bit in range(7, -1, -1):
            if position == size:
                break
            best_length, best_distance = 2, 0
            limit = min(18, size - position)
            if limit >= 3:
                for source in reversed(candidates.get(key_at(position), ())):
                    length = 3
                    while length < limit and data[source + length] == data[position + length]:
                        length += 1
                    if length > best_length:
                        best_length = length
                        best_distance = position - source
                    if length == limit:
                        break
            if best_length >= 3:
                result[flag_position] |= 1 << bit
                token = (best_length - 3) << 12 | (best_distance - 1)
                result.extend((token >> 8, token & 255))
                advance = best_length
            else:
                result.append(data[position])
                advance = 1
            for candidate in range(position, position + advance):
                remember(candidate)
            position += advance
    return bytes(result)


def _compression_header(data, kinds, max_output_size, name):
    if not isinstance(max_output_size, int) or isinstance(max_output_size, bool) or max_output_size < 0:
        raise ValueError("Compression output limit must be a non-negative integer")
    data = memoryview(data).cast("B")
    if len(data) < 4:
        raise ValueError(f"Truncated GBA {name} header")
    if data[0] not in kinds:
        raise ValueError(f"Unsupported GBA {name} compression header 0x{data[0]:02X}")
    size = data[1] | data[2] << 8 | data[3] << 16
    if size > max_output_size:
        raise ValueError(f"GBA {name} output size {size} exceeds limit {max_output_size}")
    return data, size


def decompress_rle(data: bytes, max_output_size: int = MAX_LZ_SIZE) -> tuple[bytes, int]:
    """Decode BIOS type-0x30 RLE, returning (bytes, bytes_consumed).

    Bit 7 selects a repeated byte (length + 3) or literals (length + 1).
    Truncated controls/data and runs exceeding the header size are rejected;
    trailing alignment padding is not consumed.
    """
    data, size = _compression_header(data, (0x30,), max_output_size, "RLE")
    output = bytearray()
    cursor = 4
    while len(output) < size:
        if cursor >= len(data):
            raise ValueError(f"Truncated GBA RLE control at 0x{cursor:X}")
        control = data[cursor]
        cursor += 1
        repeated = bool(control & 0x80)
        length = (control & 0x7F) + (3 if repeated else 1)
        if len(output) + length > size:
            raise ValueError(f"GBA RLE run exceeds declared output at 0x{cursor - 1:X}")
        consumed = 1 if repeated else length
        if cursor + consumed > len(data):
            raise ValueError(f"Truncated GBA RLE data at 0x{cursor:X}")
        if repeated:
            output.extend(bytes((data[cursor],)) * length)
        else:
            output.extend(data[cursor:cursor + length])
        cursor += consumed
    return bytes(output), cursor


def compress_rle(data: bytes) -> bytes:
    """Encode deterministic BIOS type-0x30 RLE without alignment padding."""
    data = bytes(data)
    size = len(data)
    if size > MAX_LZ_SIZE:
        raise ValueError(f"GBA RLE input exceeds the 24-bit size limit ({MAX_LZ_SIZE})")
    output = bytearray((0x30, size & 255, size >> 8 & 255, size >> 16))

    def run_at(position):
        length = 1
        while length < 130 and position + length < size and data[position + length] == data[position]:
            length += 1
        return length

    position = 0
    while position < size:
        length = run_at(position)
        if length >= 3:
            output.extend((0x80 | (length - 3), data[position]))
            position += length
        else:
            start = position
            position += 1
            while position < size and position - start < 128 and run_at(position) < 3:
                position += 1
            output.append(position - start - 1)
            output.extend(data[start:position])
    return bytes(output)


def decompress_huffman(data: bytes, max_output_size: int = MAX_LZ_SIZE) -> tuple[bytes, int]:
    """Decode BIOS 0x24 (4-bit) or 0x28 (8-bit) Huffman data.

    The tree-size byte is included in its region: tree_end = 4 + 2*(size+1).
    Numbering tree bytes from stream offset 4, byte 0 is size, byte 1 is root.
    A child is (node & ~1) + 2*((descriptor & 63)+1) + bit; descriptor bits
    7 and 6 mark leaves for bit 0 and 1 respectively. Child addresses always
    advance, preventing cycles; every reachable branch is checked for bounds.
    Little-endian 32-bit words supply bits MSB first. 4-bit symbols fill the
    low nibble first. Consumption includes the entire last word but no padding.
    """
    data, size = _compression_header(data, (0x24, 0x28), max_output_size, "Huffman")
    if len(data) < 5:
        raise ValueError("Truncated GBA Huffman tree-size byte")
    tree_end = 4 + (data[4] + 1) * 2
    if tree_end > len(data):
        raise ValueError("Truncated GBA Huffman tree")
    tree = data[4:tree_end]
    symbol_bits = data[0] & 15
    pending, visited = [1], set()
    while pending:
        node = pending.pop()
        if node in visited:
            continue
        visited.add(node)
        descriptor = tree[node]
        for bit in (0, 1):
            child = (node & ~1) + ((descriptor & 63) + 1) * 2 + bit
            if child <= node or child >= len(tree):
                raise ValueError(f"GBA Huffman tree child outside forward tree at 0x{node + 4:X}")
            if descriptor & (0x80 >> bit):
                if symbol_bits == 4 and tree[child] > 15:
                    raise ValueError(f"GBA 4-bit Huffman leaf exceeds 15 at 0x{child + 4:X}")
            else:
                pending.append(child)
    symbols = size * (2 if symbol_bits == 4 else 1)
    if ((len(data) - tree_end) // 4) * 32 < symbols:
        raise ValueError("Truncated GBA Huffman bitstream")
    output = bytearray(size)
    cursor, word, bits_left = tree_end, 0, 0
    for symbol in range(symbols):
        node = 1
        while True:
            if not bits_left:
                if cursor + 4 > len(data):
                    raise ValueError(f"Truncated GBA Huffman 32-bit word at 0x{cursor:X}")
                word = int.from_bytes(data[cursor:cursor + 4], "little")
                cursor += 4
                bits_left = 32
            bits_left -= 1
            bit = word >> bits_left & 1
            descriptor = tree[node]
            node = (node & ~1) + ((descriptor & 63) + 1) * 2 + bit
            if descriptor & (0x80 >> bit):
                value = tree[node]
                if symbol_bits == 4:
                    output[symbol // 2] |= value << ((symbol & 1) * 4)
                else:
                    output[symbol] = value
                break
    return bytes(output), cursor


def _dimension(value, name, kind="4bpp"):
    if not isinstance(value, int) or isinstance(value, bool) or value <= 0 or value % 8:
        raise ValueError(f"{kind} {name} must be a positive multiple of 8 pixels")


def decode_tiles4bpp(data: bytes, width: int = 128) -> tuple[bytes, int, int]:
    """Return (palette_indices, width, height) for row-major 8x8 GBA tiles.

    Each 32-byte tile contains eight rows of four bytes, low nibble first.
    A partial last tile row is padded with palette index 0 for display; input
    bytes are never modified. Empty or partial tiles are rejected.
    """
    _dimension(width, "width")
    data = memoryview(data).cast("B")
    if not data or len(data) % 32:
        raise ValueError("4bpp data must contain a non-empty whole number of 32-byte tiles")
    tile_count = len(data) // 32
    tiles_per_row = width // 8
    height = ((tile_count + tiles_per_row - 1) // tiles_per_row) * 8
    if width * height > MAX_IMAGE_PIXELS:
        raise ValueError(f"4bpp image exceeds the {MAX_IMAGE_PIXELS}-pixel limit")
    pixels = bytearray(width * height)
    for tile in range(tile_count):
        origin = (tile // tiles_per_row) * 8 * width + (tile % tiles_per_row) * 8
        for y in range(8):
            for pair in range(4):
                value = data[tile * 32 + y * 4 + pair]
                position = origin + y * width + pair * 2
                pixels[position] = value & 15
                pixels[position + 1] = value >> 4
    return bytes(pixels), width, height


def decode_tiles8bpp(data: bytes, width: int = 128) -> tuple[bytes, int, int]:
    """Return (palette_indices, width, height) for 64-byte, 8x8 GBA tiles.

    Each byte is a palette index; incomplete tile rows are padded with index 0
    for display. Empty or partial tiles are rejected.
    """
    _dimension(width, "width", "8bpp")
    data = memoryview(data).cast("B")
    if not data or len(data) % 64:
        raise ValueError("8bpp data must contain a non-empty whole number of 64-byte tiles")
    tile_count = len(data) // 64
    tiles_per_row = width // 8
    height = ((tile_count + tiles_per_row - 1) // tiles_per_row) * 8
    if width * height > MAX_IMAGE_PIXELS:
        raise ValueError(f"8bpp image exceeds the {MAX_IMAGE_PIXELS}-pixel limit")
    pixels = bytearray(width * height)
    for tile in range(tile_count):
        origin = (tile // tiles_per_row) * 8 * width + (tile % tiles_per_row) * 8
        for y in range(8):
            source = tile * 64 + y * 8
            target = origin + y * width
            pixels[target:target + 8] = data[source:source + 8]
    return bytes(pixels), width, height


def encode_tiles4bpp(pixels: bytes, width: int, height: int, *, tile_count: int | None = None) -> bytes:
    """Encode palette indices back to tiles, optionally dropping zero padding.

    Pass the original tile count to reverse decode_tiles4bpp for a partial last
    tile row. Discarding a tile containing nonzero pixels is rejected.
    """
    _dimension(width, "width")
    _dimension(height, "height")
    pixels = memoryview(pixels).cast("B")
    if width * height > MAX_IMAGE_PIXELS or len(pixels) != width * height:
        raise ValueError("4bpp pixel buffer has the wrong size or exceeds the image limit")
    if any(value > 15 for value in pixels):
        raise ValueError("4bpp palette indices must be between 0 and 15")
    total_tiles = width // 8 * (height // 8)
    if tile_count is None:
        tile_count = total_tiles
    if not isinstance(tile_count, int) or isinstance(tile_count, bool) or not 0 < tile_count <= total_tiles:
        raise ValueError("4bpp tile count must be between 1 and the image's tile count")
    output = bytearray()
    tiles_per_row = width // 8
    for tile in range(total_tiles):
        origin = (tile // tiles_per_row) * 8 * width + (tile % tiles_per_row) * 8
        for y in range(8):
            for pair in range(4):
                position = origin + y * width + pair * 2
                value = pixels[position] | pixels[position + 1] << 4
                if tile < tile_count:
                    output.append(value)
                elif value:
                    raise ValueError("Cannot discard nonzero 4bpp padding tiles")
    return bytes(output)


def _palette_rgb(palette, *, color_counts=(16,)):
    color_description = " or ".join(str(count) for count in color_counts)
    if palette is None:
        count = color_counts[0]
        return bytes(component for index in range(count) for component in (index * 255 // (count - 1),) * 3)
    if isinstance(palette, (bytes, bytearray, memoryview)):
        palette = bytes(palette)
        if len(palette) not in tuple(count * 2 for count in color_counts):
            raise ValueError(f"GBA palette must contain {color_description} little-endian BGR555 colors")
        rgb = bytearray()
        for offset in range(0, len(palette), 2):
            color = palette[offset] | palette[offset + 1] << 8
            for shift in (0, 5, 10):
                component = color >> shift & 31
                rgb.append(component << 3 | component >> 2)
        return bytes(rgb)
    if not isinstance(palette, Sequence) or len(palette) not in color_counts:
        raise ValueError(f"GBA palette must contain {color_description} RGB triples or BGR555 bytes")
    rgb = bytearray()
    for color in palette:
        if not isinstance(color, Sequence) or len(color) != 3:
            raise ValueError("Each 4bpp palette color must be an RGB triple")
        for component in color:
            if not isinstance(component, int) or isinstance(component, bool) or not 0 <= component <= 255:
                raise ValueError("4bpp palette RGB components must be integers between 0 and 255")
            rgb.append(component)
    return bytes(rgb)


def _png_chunk(kind, content):
    return (struct.pack(">I", len(content)) + kind + content
            + struct.pack(">I", zlib.crc32(kind + content) & 0xFFFFFFFF))


def _indexed_png(pixels, width, height, palette):
    bit_depth = 4 if len(palette) == 16 * 3 else 8
    rows = bytearray()
    for y in range(height):
        rows.append(0)  # PNG filter: None.
        origin = y * width
        if bit_depth == 4:
            # PNG packs its first pixel in the high nibble; GBA does the opposite.
            for x in range(0, width, 2):
                rows.append(pixels[origin + x] << 4 | pixels[origin + x + 1])
        else:
            rows.extend(pixels[origin:origin + width])
    header = struct.pack(">IIBBBBB", width, height, bit_depth, 3, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + _png_chunk(b"IHDR", header)
            + _png_chunk(b"PLTE", palette) + _png_chunk(b"IDAT", zlib.compress(rows))
            + _png_chunk(b"IEND", b""))


def export_4bpp(data: bytes, width: int = 128, palette=None) -> bytes:
    """Export tile bytes as a 4-bit indexed PNG (16 grayscale colors by default).

    palette accepts 32 GBA BGR555 bytes or a sequence of 16 (r, g, b) triples.
    No palette index is implicitly made transparent.
    """
    palette = _palette_rgb(palette)
    pixels, width, height = decode_tiles4bpp(data, width)
    return _indexed_png(pixels, width, height, palette)


def export_8bpp(data: bytes, width: int = 128, palette=None) -> bytes:
    """Export 64-byte tiles as an 8-bit indexed PNG, defaulting to 256 grays.

    A custom palette contains 512 BGR555 bytes or 256 RGB triples. No palette
    index is implicitly made transparent.
    """
    palette = _palette_rgb(palette, color_counts=(256,))
    pixels, width, height = decode_tiles8bpp(data, width)
    return _indexed_png(pixels, width, height, palette)


def decode_tilemap(data: bytes, width: int = 32, layout: str = "screenblocks",
                   format: str = "text") -> tuple[tuple[int, ...], int, int]:
    """Return (row-major entries, width_tiles, height_tiles) from a GBA BG map.

    Text maps contain little-endian 16-bit entries: tile index bits 0..9,
    horizontal/vertical flip bits 10/11, and palette bank bits 12..15.
    screenblocks maps store 32x32-tile blocks in row order and must have both
    dimensions divisible by 32; linear maps store complete rows directly.
    Affine maps contain one-byte tile indices and require linear layout.
    Unlike tile previews, missing map rows or screenblocks are not padded.
    """
    if not isinstance(width, int) or isinstance(width, bool) or width <= 0:
        raise ValueError("Tilemap width must be a positive integer number of tiles")
    if layout not in ("screenblocks", "linear"):
        raise ValueError("Tilemap layout must be 'screenblocks' or 'linear'")
    if format not in ("text", "affine"):
        raise ValueError("Tilemap format must be 'text' or 'affine'")
    if format == "affine" and layout != "linear":
        raise ValueError("GBA affine tilemaps require layout: linear")
    data = memoryview(data).cast("B")
    entry_size = 2 if format == "text" else 1
    if not data or len(data) % entry_size:
        raise ValueError(f"{format.capitalize()} tilemap requires non-empty whole {entry_size}-byte entries")
    count = len(data) // entry_size
    if count % width:
        raise ValueError(f"Tilemap has {count} entries, which do not fill rows of {width} tiles")
    height = count // width
    if count * 64 > MAX_IMAGE_PIXELS:
        raise ValueError(f"Tilemap image exceeds the {MAX_IMAGE_PIXELS}-pixel limit")
    if layout == "screenblocks" and (width % 32 or height % 32):
        raise ValueError("Screenblock tilemap width and height must be multiples of 32 tiles; use layout: linear for packed rows")
    if format == "text":
        entries = struct.unpack(f"<{count}H", data)
    else:
        entries = tuple(data)
    if layout == "screenblocks":
        ordered = [0] * count
        blocks_per_row = width // 32
        for block_y in range(height // 32):
            for block_x in range(blocks_per_row):
                source = (block_y * blocks_per_row + block_x) * 1024
                for row in range(32):
                    target = (block_y * 32 + row) * width + block_x * 32
                    ordered[target:target + 32] = entries[source + row * 32:source + (row + 1) * 32]
        entries = tuple(ordered)
    return entries, width, height


def export_tilemap(tiles: bytes, data: bytes, width: int = 32, layout: str = "screenblocks",
                   format: str = "text", palette=None, bpp: int = 4) -> bytes:
    """Render a BG map with 4/8bpp tiles and text-map flips to PNG.

    width is in tiles. A 16-color palette (including default grayscale) is
    bank 0 only; maps referring to another bank require a complete 256-color
    palette: 512 BGR555 bytes or 256 RGB triples. Palette indices are displayed
    directly without making color 0 transparent. With bpp=8 the text palette
    bank bits are ignored, and all palettes contain 256 colors. Affine maps
    require bpp=8 and layout='linear', with no flip or bank bits.
    """
    if not isinstance(bpp, int) or isinstance(bpp, bool) or bpp not in (4, 8):
        raise ValueError("Tilemap graphics bpp must be 4 or 8")
    if format == "affine" and bpp != 8:
        raise ValueError("Affine BG tilemap rendering requires bpp=8")
    tiles = memoryview(tiles).cast("B")
    tile_size = 32 if bpp == 4 else 64
    if not tiles or len(tiles) % tile_size:
        raise ValueError(f"Tilemap graphics must contain a non-empty whole number of {tile_size}-byte {bpp}bpp tiles")
    entries, map_width, map_height = decode_tilemap(data, width, layout, format)
    tile_count = len(tiles) // tile_size
    for position, entry in enumerate(entries):
        tile = entry & 0x3FF if format == "text" else entry
        if tile >= tile_count:
            raise ValueError(f"Tilemap entry ({position % map_width}, {position // map_width}) references tile {tile}, but graphics contain only {tile_count} tiles")
    palette = _palette_rgb(palette, color_counts=(16, 256) if bpp == 4 else (256,))
    if len(palette) == 16 * 3:
        for position, entry in enumerate(entries):
            bank = entry >> 12
            if bank:
                raise ValueError(f"Tilemap entry ({position % map_width}, {position // map_width}) references palette bank {bank}; supply a 256-color palette (512 BGR555 bytes)")
    image_width, image_height = map_width * 8, map_height * 8
    pixels = bytearray(image_width * image_height)
    for position, entry in enumerate(entries):
        tile = entry & 0x3FF if format == "text" else entry
        tile_origin = tile * tile_size
        palette_base = (entry >> 12) * 16 if bpp == 4 else 0
        hflip = format == "text" and bool(entry & 0x400)
        vflip = format == "text" and bool(entry & 0x800)
        origin = (position // map_width) * 8 * image_width + (position % map_width) * 8
        for y in range(8):
            source_y = 7 - y if vflip else y
            for x in range(8):
                source_x = 7 - x if hflip else x
                if bpp == 4:
                    value = tiles[tile_origin + source_y * 4 + source_x // 2]
                    index = value >> ((source_x & 1) * 4) & 15
                else:
                    index = tiles[tile_origin + source_y * 8 + source_x]
                pixels[origin + y * image_width + x] = palette_base + index
    return _indexed_png(pixels, image_width, image_height, palette)
