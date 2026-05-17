; slideshow_msx.s — VDP helpers for slideshow screensaver (MSX Screen 7)
; SCC cdecl convention: args on stack, callee preserves IX, IY, BC.
;
; MSX2 V9938 VDP ports:
;   0x98 — VRAM data read/write
;   0x99 — control: address setup, register write
;
; VRAM write address setup:
;   di
;   out (0x99), addr_lo               ; A7:A0
;   out (0x99), (addr_hi & 0x3F)|0x40 ; A13:A8, bits7:6 = 01 = write
;   ei
;
; R#14 selects the 16KB VRAM bank (bits A16:A14).
; Register write: out(0x99, value); out(0x99, 0x80 | regnum).

.z80
.code

; -----------------------------------------------------------------------
; void vdp_fill(unsigned int vram_addr, unsigned char fill_byte,
;               unsigned short len)
;
; Fills len bytes of MSX VRAM starting at vram_addr with fill_byte.
; -----------------------------------------------------------------------
.export _vdp_fill
_vdp_fill:
	push bc
	push ix
	ld   ix, #2
	add  ix, sp

	ld   e, (ix+4)      ; E = vram_addr lo
	ld   d, (ix+5)      ; D = vram_addr hi
	ld   l, (ix+6)      ; L = fill byte
	ld   c, (ix+8)      ; BC = len
	ld   b, (ix+9)

	; Set R#14 = VRAM bank (bits A16:A14)
	di
	ld   a, d
	rlca
	rlca
	and  #3
	out  (0x99), a
	ld   a, #0x8E
	out  (0x99), a
	; Set VRAM write address
	ld   a, e
	out  (0x99), a
	ld   a, d
	and  #0x3F
	or   #0x40
	out  (0x99), a
	ei

	ld   a, b
	or   c
	jr   z, _vdp_fill_done
_vdp_fill_loop:
	ld   a, l
	out  (0x98), a
	dec  bc
	ld   a, b
	or   c
	jr   nz, _vdp_fill_loop
_vdp_fill_done:
	pop  ix
	pop  bc
	ret

; -----------------------------------------------------------------------
; void vdp_write(unsigned int vram_addr, char *src, unsigned short len)
;
; Copies len bytes from Z80 memory src to MSX VRAM starting at vram_addr.
; -----------------------------------------------------------------------
.export _vdp_write
_vdp_write:
	push bc
	push ix
	ld   ix, #2
	add  ix, sp

	ld   e, (ix+4)      ; E = vram_addr lo
	ld   d, (ix+5)      ; D = vram_addr hi
	ld   l, (ix+6)      ; HL = src pointer
	ld   h, (ix+7)
	ld   c, (ix+8)      ; BC = len
	ld   b, (ix+9)

	; Set R#14 = VRAM bank (bits A16:A14)
	di
	ld   a, d
	rlca
	rlca
	and  #3
	out  (0x99), a
	ld   a, #0x8E
	out  (0x99), a
	; Set VRAM write address
	ld   a, e
	out  (0x99), a
	ld   a, d
	and  #0x3F
	or   #0x40
	out  (0x99), a
	ei

	ld   a, b
	or   c
	jr   z, _vdp_write_done
_vdp_write_loop:
	ld   a, (hl)
	out  (0x98), a
	inc  hl
	dec  bc
	ld   a, b
	or   c
	jr   nz, _vdp_write_loop
_vdp_write_done:
	pop  ix
	pop  bc
	ret

; -----------------------------------------------------------------------
; void vdp_fill_bank(unsigned char bank, unsigned int vram_addr,
;                    unsigned char fill_byte, unsigned short len)
;
; Like vdp_fill but uses an explicit R#14 bank value (0 or 4).
; bank=0 → VRAM 0x00000 (page 0), bank=4 → VRAM 0x10000 (page 1).
; -----------------------------------------------------------------------
.export _vdp_fill_bank
_vdp_fill_bank:
	push bc
	push ix
	ld   ix, #2
	add  ix, sp

	ld   a, (ix+4)      ; A = bank (R#14 value)
	ld   e, (ix+6)      ; E = vram_addr lo
	ld   d, (ix+7)      ; D = vram_addr hi
	ld   l, (ix+8)      ; L = fill byte
	ld   c, (ix+10)     ; BC = len
	ld   b, (ix+11)

	di
	out  (0x99), a
	ld   a, #0x8E
	out  (0x99), a
	ld   a, e
	out  (0x99), a
	ld   a, d
	and  #0x3F
	or   #0x40
	out  (0x99), a
	ei

	ld   a, b
	or   c
	jr   z, _vdp_fb_done
_vdp_fb_loop:
	ld   a, l
	out  (0x98), a
	dec  bc
	ld   a, b
	or   c
	jr   nz, _vdp_fb_loop
_vdp_fb_done:
	pop  ix
	pop  bc
	ret

; -----------------------------------------------------------------------
; void vdp_write_bank(unsigned char bank, unsigned int vram_addr,
;                     char *src, unsigned short len)
;
; Like vdp_write but uses an explicit R#14 bank value (0 or 4).
; -----------------------------------------------------------------------
.export _vdp_write_bank
_vdp_write_bank:
	push bc
	push ix
	ld   ix, #2
	add  ix, sp

	ld   a, (ix+4)      ; A = bank (R#14 value)
	ld   e, (ix+6)      ; E = vram_addr lo
	ld   d, (ix+7)      ; D = vram_addr hi
	ld   l, (ix+8)      ; HL = src pointer
	ld   h, (ix+9)
	ld   c, (ix+10)     ; BC = len
	ld   b, (ix+11)

	di
	out  (0x99), a
	ld   a, #0x8E
	out  (0x99), a
	ld   a, e
	out  (0x99), a
	ld   a, d
	and  #0x3F
	or   #0x40
	out  (0x99), a
	ei

	ld   a, b
	or   c
	jr   z, _vdp_wb_done
_vdp_wb_loop:
	ld   a, (hl)
	out  (0x98), a
	inc  hl
	dec  bc
	ld   a, b
	or   c
	jr   nz, _vdp_wb_loop
_vdp_wb_done:
	pop  ix
	pop  bc
	ret

; -----------------------------------------------------------------------
; void vdp_set_page(unsigned char page)
;
; Switches the VDP display to VRAM page 0 (0x00000) or page 1 (0x10000)
; by writing R#2. For MSX Screen 7 (V9938 G6): base = R#2[6:1] * 0x800.
; page 0 → R#2 = 0x00 (base 0x00000)
; page 1 → R#2 = 0x40 (base 0x10000: 32 * 0x800 = 0x10000)
; -----------------------------------------------------------------------
.export _vdp_set_page
_vdp_set_page:
	push ix
	ld   ix, #2
	add  ix, sp

	ld   a, (ix+4)      ; A = page (0 or 1)
	; R#2 = page * 0x40  (0 → 0x00, 1 → 0x40)
	rlca
	rlca
	rlca
	rlca
	rlca
	rlca                 ; shift bit 0 → bit 6
	di
	out  (0x99), a
	ld   a, #0x82        ; write R#2
	out  (0x99), a
	ei

	pop  ix
	ret
