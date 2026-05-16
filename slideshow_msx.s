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
