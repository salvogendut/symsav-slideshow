---
name: project-slideshow
description: symsav-slideshow screensaver — SGX image slideshow for SymbOS CPC; key APIs, quirks, memory layout
metadata:
  type: project
---

Slideshow screensaver (`slideshow.c`) built with SCC targeting SymbOS on CPC (Mode 1, 4-colour). Compiled to `slideshow.sav`; add_preview.py patches in a 643-byte preview image at offset 256.

**Why:** User requested a new screensaver in the existing symsav-* family that shows SGX images from a directory.

**Key implementation facts:**

- Config block (64 bytes in SYMBOS.INI): magic "SLID", [4]=delay_idx(1-4), [5..56]=path string
- Default path: `A:\SLIDES\`
- Delay options: 3s/5s(default)/10s/30s, in 50 Hz ticks (150/250/500/1500)
- SGX 4-colour only (Mode 1); 16-colour chunks (byte1==0x05) are skipped
- Blitting: scanline-by-scanline Bank_Copy to VRAM (bank 0, 0xC000); address = 0xC000 + (y>>3)*80 + (y&7)*0x800 + x_off_bytes
- Compressed chunks: use File_ReadComp; uncompressed: use File_Read
- Dir scanning: Dir_Read with mask `*.SGX`, attrib filter = ATTRIB_HIDDEN|ATTRIB_SYSTEM|ATTRIB_VOLUME|ATTRIB_DIR, returns fixed DirEntry (22 bytes each)
- No VRAM shadow buffer (minor corruption acceptable for static photo display)
- `wait_delay` returns 0=expired, 1=user activity, 2=quit message

**Ctrl_Input struct (actual fields — NOT font/color/underline):**
```
char* text, scroll, cursor, selection, len, maxlen, flags, textcolor, linecolor
```

**Memory (data segment ~12 KB):** imgbuf[8192], dirbuf[1024], zero_plane[2000], cfgdat[64], filenames[40][14], chunk_hdr[4]

**How to apply:** When modifying this screensaver or building similar ones, use these patterns. Always check actual struct member names in SCC headers before using them.
