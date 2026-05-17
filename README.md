# symsav-slideshow

A photo slideshow screensaver for [SymbOS](https://www.symbos.org/) on the Amstrad CPC and MSX.

> **Beta version — working on CPC.** The CPC slideshow is working. MSX support is implemented but not yet tested on real hardware.

> **CPC: Requires Mode 1** — on the Amstrad CPC this screensaver only works in 320×200 Mode 1 (4 colours). Running it in any other screen mode will produce incorrect output.

Displays a rotating sequence of SGX images loaded from a configurable directory on disk.

---

## Building

```bash
./build.sh
```

Requires the SCC compiler (set `SCC=` env var if not at `../scc/bin/cc`) and Python 3.

Build steps:

1. SCC compiles `slideshow.c` + `slideshow_msx.s` → `slides.sav`
2. `add_preview.py` generates a preview thumbnail and patches it into the binary at offset 256

Output: `slides.sav`

---

## Installing

1. Copy `slides.sav` into your `C:\SYMBOS\` directory.
2. Open **Display Properties** and go to the **Screen Saver** tab.
3. Click **Browse** and select `slides.sav`.
4. Click **Setup** to configure the effect:
   - **Delay**: 3 s / 5 s / 10 s / 30 s — time each image is shown before advancing
   - **Path**: directory containing the SGX image files (e.g. `A:\SLIDES\`)

---

## Preparing images

Images must be in SGX format. Use the [conv2SGX](https://github.com/salvogendut/conv2SGX) tool to convert PNG or JPG files.

**CPC** — 4-colour SGX (Mode 1, 320×200):

```bash
python3 conv2sgx.py photo.png -c 4 -W 320 -H 200 --no-aspect
```

**MSX** — 16-colour SGX (Screen 7, 512×212), without compression:

```bash
python3 conv2sgx.py photo.png -c 16 -W 512 -H 212 --no-aspect
```

> **Note:** do not pass `--compress` for MSX images — compressed extended chunks are not supported and will be skipped during playback.

Keep CPC and MSX image sets in separate directories, as each platform only reads its own chunk format.

---

## Effect

- On activation, the screensaver scans the configured directory for `*.SGX` files (up to 40). The directory scan uses `DIRINP` with a `*.*` wildcard — a bare `*` is rejected by SymbOS with `ERR_BADNAME`.
- It clears the screen to black and blits each image directly into VRAM.
- After the configured delay the screen clears briefly and the next image is loaded.
- The rotation is sequential and wraps around to the first image after the last.
- If no images are found the screen stays black until user activity.

Platform is detected at runtime via `Sys_Type()`.

---

## Screensaver protocol

Standard SymbOS screensaver messages:

| Message | Action |
|---------|--------|
| `MSC_SAV_INIT` (1) | Load saved config from manager |
| `MSC_SAV_START` (2) | Start fullscreen slideshow |
| `MSC_SAV_CONFIG` (3) | Open config dialog |
| `MSR_SAV_CONFIG` (4) | Send updated config back |

Config is 64 bytes stored in `SYMBOS.INI`: magic `"SLID"` + delay index byte + null-terminated path string (up to 52 chars).

---

## Rendering

Fullscreen rendering follows the same approach as the other screensavers in this family:

1. Open a fullscreen `WIN_NOTTASKBAR | WIN_NOTMOVEABLE` window
2. `DSK_SRV_DSKSTP` to freeze the desktop
3. Clear the screen to black
4. Open the SGX file and read each chunk, blitting into VRAM
5. Wait the configured delay, checking for key or mouse activity each `Idle()` tick
6. Exit on user activity: resume desktop, close window, `Screen_Redraw()`

### CPC rendering

- Screen clear: `Bank_Copy` all 8 Mode-1 scan planes to `0xF0` (ink 1 = black)
- Chunk blit: `Bank_Copy` scanline by scanline via `Bank_Copy` into VRAM bank 0
- VRAM address formula for scanline y, byte column x_byte:

```
addr = 0xC000 + (y/8)*80 + (y%8)*0x800 + x_byte
```

### MSX rendering

- Screen clear: `vdp_fill(0, 0x11, 54272)` — fills all 512×212 Screen 7 VRAM with COLOR_BLACK
- Chunk blit: one row at a time via `vdp_write` to VDP data port (0x98)
- VRAM address formula for row y, byte column x_byte:

```
addr = y * 256 + x_byte
```

No shadow buffer needed — the MSX kernel does not corrupt VRAM during `Idle()`.

### SGX chunk loading

Each SGX file contains one or more chunks followed by a `00 00 00` terminator.
Images are split into horizontal column strips; the x byte offset accumulates
across chunks.

**CPC** — simple chunks (4-colour, bit 6 of byte 0 = 0):

A 320×200 image produces two 160×200 chunks (40 bytes/row each):

| Chunk | x byte offset | Pixel columns |
|-------|--------------|---------------|
| 0 | 0 | 0–159 |
| 1 | 40 | 160–319 |

Compressed simple chunks (bit 7 set) are decompressed with `File_ReadComp`.
Uncompressed chunks are read directly with `File_Read`.

**MSX** — extended chunks (16-colour, bit 6 of byte 0 = 1, type byte = `0x05`):

A 512×212 image is split into 4 strips so each uncompressed payload ≤ 16 384 bytes
(approximately 152 pixels = 76 bytes per strip):

| Chunk | x byte offset | Pixel columns |
|-------|--------------|---------------|
| 0 | 0 | 0–151 |
| 1 | 76 | 152–303 |
| 2 | 152 | 304–455 |
| 3 | 228 | 456–511 |

Each strip covers all 212 rows. Rows are read one at a time via `File_Read`
and written directly to VDP VRAM with `vdp_write`. Compressed extended chunks
are not supported.
