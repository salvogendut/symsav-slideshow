#!/usr/bin/env python3
"""Insert screensaver preview image into slideshow.sav at code offset 256.

The preview is a 64×40 Mode-1 (4-colour) image showing a simple film-frame
pattern: dark background with white border lines and a central bright area
to suggest a photo slideshow.
"""

import struct

SAV_PATH = "slides.sav"

INSERT_AT    = 256
PREVIEW_W    = 64    # pixels (= 16 bytes per row in Mode-1)
PREVIEW_H    = 40    # lines
ROW_BYTES    = PREVIEW_W // 4   # 16 bytes per row
PREVIEW_DATA = ROW_BYTES * PREVIEW_H   # 640 bytes
PREVIEW_TOTAL = 3 + PREVIEW_DATA       # 643 bytes total

# CPC Mode-1 ink encoding for a byte of 4 consecutive pixels (each 0–3):
#   bit7=p0_lo  bit6=p1_lo  bit5=p2_lo  bit4=p3_lo
#   bit3=p0_hi  bit2=p1_hi  bit1=p2_hi  bit0=p3_hi
#   ink: lo=bit0(ink), hi=bit1(ink)
#   ink0=white(00), ink1=black(11→lo=1,hi=0→0xF0 for 4 pixels)
#   ink2=dim(lo=0,hi=1→0x0F), ink3=bright(lo=1,hi=1→0xFF)
INK_BLACK  = 0xF0   # all 4 pixels = ink1
INK_WHITE  = 0x00   # all 4 pixels = ink0
INK_DIM    = 0x0F   # all 4 pixels = ink2
INK_BRIGHT = 0xFF   # all 4 pixels = ink3


def encode_row(pixels):
    """Encode a list of 64 ink values (0-3) into 16 Mode-1 bytes."""
    result = bytearray(ROW_BYTES)
    for i, ink in enumerate(pixels):
        byte_idx = i >> 2
        pos = i & 3
        lo_mask = 0x80 >> pos
        hi_mask = 0x08 >> pos
        if ink & 1:
            result[byte_idx] |= lo_mask
        if ink & 2:
            result[byte_idx] |= hi_mask
    return bytes(result)


def make_preview():
    """Generate a 64×40 preview image: photo frame border with bright centre."""
    rows = []
    for y in range(PREVIEW_H):
        row = []
        for x in range(PREVIEW_W):
            border = (x < 3 or x >= PREVIEW_W - 3 or
                      y < 3 or y >= PREVIEW_H - 3)
            inner  = (6 <= x < PREVIEW_W - 6 and
                      6 <= y < PREVIEW_H - 6)
            if border:
                row.append(0)    # white border
            elif inner:
                # Simulate a photo: dim horizontal bands
                band = (y - 6) // 4
                row.append(2 + (band % 2))  # alternating dim/bright
            else:
                row.append(1)    # black mat between border and photo
        rows.append(encode_row(row))

    data = b"".join(rows)
    # 3-byte header: format=16 (Mode1), width=64, height=40
    header = bytes([16, PREVIEW_W, PREVIEW_H])
    return header + data


def patch(sav_path, preview):
    ins = len(preview)   # 643

    with open(sav_path, "rb") as f:
        data = bytearray(f.read())

    code_len    = struct.unpack_from("<H", data, 0)[0]
    data_len    = struct.unpack_from("<H", data, 2)[0]
    trans_len   = struct.unpack_from("<H", data, 4)[0]
    reloc_count = struct.unpack_from("<H", data, 8)[0]

    trans_start = code_len + data_len
    reloc_start = trans_start + trans_len
    assert reloc_start + reloc_count * 2 == len(data), \
        f"File size mismatch: expected {reloc_start + reloc_count * 2}, got {len(data)}"

    relocs = list(struct.unpack_from("<" + "H" * reloc_count, data, reloc_start))

    new_data = data[:INSERT_AT] + bytearray(preview) + data[INSERT_AT:reloc_start]

    new_relocs = [r + ins if r >= INSERT_AT else r for r in relocs]
    for nr in new_relocs:
        v = struct.unpack_from("<H", new_data, nr)[0]
        if v >= INSERT_AT:
            struct.pack_into("<H", new_data, nr, v + ins)

    struct.pack_into("<H", new_data, 0, code_len + ins)

    new_reloc_raw = struct.pack("<" + "H" * reloc_count, *new_relocs)
    result = bytes(new_data) + new_reloc_raw

    with open(sav_path, "wb") as f:
        f.write(result)

    print(f"Inserted {ins} preview bytes at offset {INSERT_AT}")
    print(f"code_len: {code_len} -> {code_len + ins}")
    print(f"File size: {len(data)} -> {len(result)}")


if __name__ == "__main__":
    preview = make_preview()
    assert len(preview) == PREVIEW_TOTAL, \
        f"Preview size {len(preview)} != {PREVIEW_TOTAL}"
    print(f"Generated {len(preview)}-byte preview image")
    patch(SAV_PATH, preview)
