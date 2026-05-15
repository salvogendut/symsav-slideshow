# symsav-slideshow

A photo slideshow screensaver for [SymbOS](https://www.symbos.org/) on the Amstrad CPC.

> **Alpha version — use at your own risk.** This software is in an early alpha state and may cause harm to your system. If you choose to try it, you do so entirely at your own risk.

> **Requires Mode 1** — this screensaver only works in 320×200 Mode 1 (4 colours). Running it in any other screen mode will produce incorrect output.

Displays a rotating sequence of SGX images loaded from a configurable directory on disk.

---

## Building

```bash
./build.sh
```

Requires the SCC compiler (set `SCC=` env var if not at `../scc/bin/cc`) and Python 3.

Build steps:

1. SCC compiles `slideshow.c` → `slideshow.sav`
2. `add_preview.py` generates and patches the preview thumbnail into the binary at file offset 256

Output: `slideshow.sav`

---

## Installing

1. Copy `slideshow.sav` into your `C:\SYMBOS\` directory.
2. Open **Display Properties** and go to the **Screen Saver** tab.
3. Click **Browse** and select `slideshow.sav`.
4. Click **Setup** to configure the effect:
   - **Delay**: 3 s / 5 s / 10 s / 30 s — time each image is shown before advancing
   - **Path**: directory containing the SGX image files (e.g. `A:\SLIDES\`)

---

## Preparing images

Images must be in **4-colour SGX format** (CPC Mode 1, 320×200). Use the
[conv2SGX](https://github.com/salvogendut/conv2SGX) tool to convert PNG or JPG files:

```bash
python3 conv2sgx.py photo.png -c 4 -W 320 -H 200 --no-aspect
```

Copy the resulting `.sgx` files into the configured image directory on the CPC disk (default `A:\SLIDES\`).

---

## Effect

- On activation, the screensaver scans the configured directory for `*.SGX` files (up to 40).
- It clears the screen to black and blits each image directly into Mode-1 VRAM.
- After the configured delay the screen clears briefly and the next image is loaded.
- The rotation is sequential and wraps around to the first image after the last.
- If no images are found the screen stays black until user activity.

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
3. Clear all 8 CPC character planes via `Bank_Copy` to VRAM (bank 0, all bytes = `0xF0` = ink 1 = black)
4. Open the SGX file and read each chunk; blit scanline-by-scanline via `Bank_Copy` into VRAM
5. Wait the configured delay, checking for key or mouse activity each `Idle()` tick
6. Exit on user activity: resume desktop, close window, `Screen_Redraw()`

VRAM address formula for scanline y, byte column x_byte:

```
addr = 0xC000 + (y/8)*80 + (y%8)*0x800 + x_byte
```

### SGX chunk loading

Each SGX file contains one or more chunks followed by a `00 00 00` terminator.
A 4-colour 320×200 image has two 160×200 chunks (40 bytes/row each), placed
left then right:

| Chunk | x byte offset | Pixel columns |
|-------|--------------|---------------|
| 0 | 0 | 0–159 |
| 1 | 40 | 160–319 |

Compressed chunks (header byte bit 7 set) are decompressed using `File_ReadComp`,
which handles the SymbOS ZX0 wrapper format transparently.
Uncompressed chunks are read directly with `File_Read`.
