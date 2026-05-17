// slideshow.c — SGX image slideshow screensaver for SymbOS (CPC and MSX)
//
// CPC: 4-colour Mode-1 320x200, images stored as simple SGX chunks.
// MSX: 16-colour Screen 7 512x212, images stored as uncompressed extended SGX chunks.
//      Note: compressed MSX SGX files are not supported (omit --compress when converting).
//
// Config: delay (3/5/10/30 s) and path stored in 64-byte SYMBOS.INI block.

#include <symbos.h>
#include <symbos/msgid.h>
#include <symbos/keys.h>
#include <symbos/file.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MSC_SAV_INIT   1
#define MSC_SAV_START  2
#define MSC_SAV_CONFIG 3
#define MSR_SAV_CONFIG 4

// CPC screen geometry
#define SCREEN_W_CPC  320
#define SCREEN_H_CPC  200

// MSX Screen 7 geometry
#define SCREEN_W_MSX  512
#define SCREEN_H_MSX  212

#define MAX_FILES  40
#define MAX_PATH   52

// VDP helpers (MSX only)
extern void vdp_fill(unsigned int vram_addr, unsigned char fill_byte, unsigned short len);
extern void vdp_write(unsigned int vram_addr, char *src, unsigned short len);

// --------------------------------------------------------------------------
// Data-segment buffers
// --------------------------------------------------------------------------

_data unsigned char imgbuf[256];

// Write-through shadow of CPC VRAM char rows 12-24 (pixel rows 96-199).
// Size: 8 * 13 * 80 = 8320 bytes.
_data unsigned char lbuf[8320];

// Directory listing buffer
_data char dirbuf[1024];

// Config block: [0-3]="SLID", [4]=delay_idx(1-4), [5..56]=path (null-term)
_data char cfgdat[64];
_data char init_tmp[64];

// File list (up to MAX_FILES filenames, each max 13 chars + null)
_data char filenames[MAX_FILES][14];
_data unsigned char file_count;
_data unsigned char cur_file;

// Debug variables
_data unsigned char dir_read_ok;
_data int dir_read_count;
_data unsigned char dbg_fc_ret;
_data unsigned char dbg_msg2;
_data unsigned char dbg_msg8;
_data unsigned char dbg_msg9;
_data unsigned char dbg_e_att;
_data char dbg_ename[13];

// Scratch buffers for reading SGX chunk headers
_data unsigned char chunk_hdr[4];
_data unsigned char ext_hdr[6];

// --------------------------------------------------------------------------
// Transfer segment: animation state
// --------------------------------------------------------------------------
_transfer char           is_msx;
_transfer unsigned short screen_w;
_transfer unsigned short screen_h;

// --------------------------------------------------------------------------
// Transfer segment: animation window
// --------------------------------------------------------------------------
_transfer Ctrl       anim_ctrl[3];
_transfer Ctrl_Group anim_cg;
_transfer Window     anim_win;
_transfer char       empty_str[1];

_transfer char       dbg_line1[64];
_transfer char       dbg_line2[32];
_transfer Ctrl_Text  dbg_ct1;
_transfer Ctrl_Text  dbg_ct2;

// --------------------------------------------------------------------------
// Transfer segment: config state
// --------------------------------------------------------------------------
_transfer char        tmp_delay    = 2;
_transfer char        cfg_prz      = 0;
_transfer signed char cfgwin_id    = -1;
_transfer char        rg_delay[4]  = { -1, -1, -1, -1 };
_transfer char        tmp_path[MAX_PATH + 1];

// --------------------------------------------------------------------------
// Transfer segment: config controls (must be consecutive)
// --------------------------------------------------------------------------
_transfer Ctrl_Input  cfg_inp;
_transfer Ctrl_TFrame cfg_tf    = { "Settings", (COLOR_BLACK<<2)|COLOR_ORANGE, 0 };
_transfer Ctrl_Text   cfg_lbl_d = { "Delay:",   (COLOR_BLACK<<2)|COLOR_ORANGE, 0 };
_transfer Ctrl_Text   cfg_lbl_p = { "Path:",    (COLOR_BLACK<<2)|COLOR_ORANGE, 0 };
_transfer Ctrl_Radio  cfg_rd1   = { &tmp_delay, "3s",  (COLOR_BLACK<<2)|COLOR_ORANGE, 1, rg_delay };
_transfer Ctrl_Radio  cfg_rd2   = { &tmp_delay, "5s",  (COLOR_BLACK<<2)|COLOR_ORANGE, 2, rg_delay };
_transfer Ctrl_Radio  cfg_rd3   = { &tmp_delay, "10s", (COLOR_BLACK<<2)|COLOR_ORANGE, 3, rg_delay };
_transfer Ctrl_Radio  cfg_rd4   = { &tmp_delay, "30s", (COLOR_BLACK<<2)|COLOR_ORANGE, 4, rg_delay };

_transfer Ctrl ccc0  = { 0,  C_AREA,   -1, COLOR_ORANGE,                     0,  0, 220, 80, 0 };
_transfer Ctrl ccc1  = { 0,  C_TFRAME, -1, (unsigned short)&cfg_tf,           2,  1, 216, 56, 0 };
_transfer Ctrl ccc2  = { 0,  C_TEXT,   -1, (unsigned short)&cfg_lbl_d,        8,  8,  36,  8, 0 };
_transfer Ctrl ccc3  = { 0,  C_RADIO,  -1, (unsigned short)&cfg_rd1,         48,  8,  22,  8, 0 };
_transfer Ctrl ccc4  = { 0,  C_RADIO,  -1, (unsigned short)&cfg_rd2,         72,  8,  22,  8, 0 };
_transfer Ctrl ccc5  = { 0,  C_RADIO,  -1, (unsigned short)&cfg_rd3,         96,  8,  28,  8, 0 };
_transfer Ctrl ccc6  = { 0,  C_RADIO,  -1, (unsigned short)&cfg_rd4,        126,  8,  28,  8, 0 };
_transfer Ctrl ccc7  = { 0,  C_TEXT,   -1, (unsigned short)&cfg_lbl_p,        8, 26,  30,  8, 0 };
_transfer Ctrl ccc8  = { 0,  C_INPUT,  -1, (unsigned short)&cfg_inp,         40, 24, 168, 10, 0 };
_transfer Ctrl ccc9  = { 9,  C_BUTTON, -1, (unsigned short)"OK",             66, 62,  32, 12, 0 };
_transfer Ctrl ccc10 = { 10, C_BUTTON, -1, (unsigned short)"Cancel",        104, 62,  52, 12, 0 };

_transfer Ctrl_Group cfgcg;
_transfer Window     cfgwin;
_transfer char       cfg_title[10] = { 'S','l','i','d','e','s','h','o','w', 0 };

// --------------------------------------------------------------------------
// Key scan — polls all hardware scan codes; works while desktop is stopped
// --------------------------------------------------------------------------
static unsigned char any_key_down(void) {
    unsigned char sc;
    for (sc = 0; sc < 80; sc++)
        if (Key_Down(sc)) return 1;
    return 0;
}

// --------------------------------------------------------------------------
// Desktop stop / resume
// --------------------------------------------------------------------------
static void desktop_stop(unsigned char wid) {
    _symmsg[0] = MSC_DSK_DSKSRV;
    _symmsg[1] = DSK_SRV_DSKSTP;
    _symmsg[2] = 0xFF;
    _symmsg[3] = wid;
    while (Msg_Send(_sympid, 2, _symmsg) == 0);
    Msg_Wait(_sympid, 2, _symmsg, MSR_DSK_DSKSRV);
}

static void desktop_cont(void) {
    _symmsg[0] = MSC_DSK_DSKSRV;
    _symmsg[1] = DSK_SRV_DSKCNT;
    while (Msg_Send(_sympid, 2, _symmsg) == 0);
    Idle();
}

// --------------------------------------------------------------------------
// Clear the screen to black and reset the CPC lower-half shadow to black.
// --------------------------------------------------------------------------
static void vram_clear(void) {
    unsigned char k;
    unsigned short i;
    if (is_msx) {
        vdp_fill(0u, 0x11u, 54272u);
        return;
    }
    for (i = 0; i < 8320u; i++) lbuf[i] = 0xF0u;
    for (k = 0; k < 8; k++) {
        Bank_Copy(0,
            (char *)(0xC000u + (unsigned short)k * 0x0800u),
            _symbank, (char *)lbuf, 2000u);
    }
}

// --------------------------------------------------------------------------
// Rebuild lbuf shadow by reading char rows 12-24 back from CPC VRAM.
// Called after load_image_cpc finishes blitting.
// --------------------------------------------------------------------------
static void lbuf_rebuild_from_vram(void) {
    unsigned char k;
    for (k = 0; k < 8; k++) {
        Bank_Copy(_symbank,
                  (char *)lbuf + (unsigned short)k * 1040u,
                  0,
                  (char *)(0xC000u + (unsigned short)k * 0x0800u + 960u),
                  1040u);
    }
}

// --------------------------------------------------------------------------
// Restore CPC VRAM char rows 12-24 from lbuf after each Idle().
// --------------------------------------------------------------------------
static void vram_restore_lower(void) {
    unsigned char k;
    if (is_msx) return;
    for (k = 0; k < 8; k++) {
        Bank_Copy(0,
            (char *)(0xC000u + (unsigned short)k * 0x0800u + 960u),
            _symbank,
            (char *)lbuf + (unsigned short)k * 1040u,
            1040u);
    }
}

// --------------------------------------------------------------------------
// MSX: Load all SGX extended chunks into VRAM row-by-row.
// Reads all chunk headers first, then processes in batches of 32 rows,
// staging each batch in lbuf before blitting top-to-bottom.
// No column artifact: all horizontal strips appear per scan line.
// --------------------------------------------------------------------------
static void load_image_msx(unsigned char fd) {
    unsigned char nc, ci;
    unsigned short crb[4];
    unsigned short cds[4];
    unsigned short ccum[4];
    unsigned short ht, fpos, bs, brows, r2, vx, loff, br;

    nc = 0; ht = 0; fpos = 0;
    while (nc < 4) {
        br = File_Read(fd, _symbank, (char *)chunk_hdr, 3u);
        fpos += br;
        if (br < 3 || chunk_hdr[0] == 0) break;
        if (!(chunk_hdr[0] & 0x40) || (chunk_hdr[0] & 0x80)) break;
        br = File_Read(fd, _symbank, (char *)ext_hdr, 5u);
        fpos += br;
        if (br < 5 || chunk_hdr[1] != 0x05) break;
        crb[nc] = (unsigned short)chunk_hdr[2] | ((unsigned short)ext_hdr[0] << 8);
        ht      = (unsigned short)ext_hdr[3]   | ((unsigned short)ext_hdr[4] << 8);
        cds[nc] = fpos;
        fpos   += ht * crb[nc];
        File_Seek(fd, (long)fpos, SEEK_SET);
        nc++;
    }
    if (nc == 0 || ht == 0) return;

    ccum[0] = 0;
    for (ci = 1; ci < nc; ci++)
        ccum[ci] = ccum[ci - 1] + crb[ci - 1];

    // Process in batches of 32 rows (32*256=8192 bytes staging fits in lbuf)
    for (bs = 0; bs < ht; bs += 32u) {
        brows = (bs + 32u <= ht) ? 32u : (ht - bs);
        for (ci = 0; ci < nc; ci++) {
            File_Seek(fd, (long)(cds[ci] + bs * crb[ci]), SEEK_SET);
            File_Read(fd, _symbank,
                      (char *)lbuf + 32u * ccum[ci],
                      brows * crb[ci]);
        }
        for (r2 = 0; r2 < brows; r2++) {
            vx = 0;
            for (ci = 0; ci < nc; ci++) {
                loff = 32u * ccum[ci] + r2 * crb[ci];
                vdp_write((bs + r2) * 256u + vx,
                          (char *)lbuf + loff,
                          crb[ci]);
                vx += crb[ci];
            }
        }
    }
}

// --------------------------------------------------------------------------
// Open and display all chunks of one SGX file.
//
// CPC: stages chunk 0 in lbuf, blits full rows (both halves together).
// MSX: batch-staged row-by-row blit — no column artifact.
// --------------------------------------------------------------------------
static unsigned char load_image(char *path) {
    unsigned char fd;
    unsigned short bytes_read;
    unsigned short rb0, rb1, ht, r;
    unsigned char scan, crow;
    unsigned short addr;

    fd = File_Open(_symbank, path);
    if (fd > 7) return 0;

    if (is_msx) {
        load_image_msx(fd);
    } else {
        // CPC: stage chunk 0 in lbuf, then blit full rows top-to-bottom

        // Read chunk 0 header
        bytes_read = File_Read(fd, _symbank, (char *)chunk_hdr, 3u);
        if (bytes_read < 3 || chunk_hdr[0] == 0 ||
            (chunk_hdr[0] & 0x40) || (chunk_hdr[0] & 0x80))
            goto cpc_done;
        rb0 = (unsigned short)(chunk_hdr[0] & 0x3F);
        ht  = (unsigned short)chunk_hdr[2];
        if (rb0 == 0 || ht == 0) goto cpc_done;

        // Stage all chunk 0 rows sequentially into lbuf[r * rb0]
        for (r = 0; r < ht; r++) {
            bytes_read = File_Read(fd, _symbank,
                                   (char *)lbuf + r * rb0, rb0);
            if (bytes_read < rb0) goto cpc_done;
        }

        // Read chunk 1 header (optional — image may have only one chunk)
        rb1 = 0;
        bytes_read = File_Read(fd, _symbank, (char *)chunk_hdr, 3u);
        if (bytes_read >= 3 && chunk_hdr[0] != 0 &&
            !(chunk_hdr[0] & 0x40) && !(chunk_hdr[0] & 0x80))
            rb1 = (unsigned short)(chunk_hdr[0] & 0x3F);

        if (rb1 > 0 && rb0 + rb1 <= (unsigned short)sizeof(imgbuf)) {
            // Blit chunk0 (left) + chunk1 (right) per row, top to bottom
            for (r = 0; r < ht; r++) {
                memcpy((char *)imgbuf, (char *)lbuf + r * rb0, rb0);
                bytes_read = File_Read(fd, _symbank,
                                       (char *)imgbuf + rb0, rb1);
                if (bytes_read < rb1) break;
                scan = (unsigned char)(r & 7u);
                crow = (unsigned char)(r >> 3u);
                addr = 0xC000u
                     + (unsigned short)crow * 80u
                     + (unsigned short)scan * 0x0800u;
                Bank_Copy(0, (char *)addr, _symbank,
                          (char *)imgbuf, rb0 + rb1);
            }
        } else {
            // Only chunk 0 available
            for (r = 0; r < ht; r++) {
                memcpy((char *)imgbuf, (char *)lbuf + r * rb0, rb0);
                scan = (unsigned char)(r & 7u);
                crow = (unsigned char)(r >> 3u);
                addr = 0xC000u
                     + (unsigned short)crow * 80u
                     + (unsigned short)scan * 0x0800u;
                Bank_Copy(0, (char *)addr, _symbank, (char *)imgbuf, rb0);
            }
        }

        // Rebuild lbuf shadow from VRAM (needed by vram_restore_lower)
        lbuf_rebuild_from_vram();

cpc_done:;
    }

    File_Close(fd);
    return 1;
}

// --------------------------------------------------------------------------
// Map delay config index (1-4) to 50 Hz tick count
// --------------------------------------------------------------------------
static unsigned short get_delay_ticks(unsigned char idx) {
    if (idx == 1) return 150;    // 3 s
    if (idx == 3) return 500;    // 10 s
    if (idx == 4) return 1500;   // 30 s
    return 250;                  // 5 s (default)
}

// --------------------------------------------------------------------------
// Returns 1 if name ends with .SGX or .sgx (case-insensitive).
// --------------------------------------------------------------------------
static unsigned char is_sgx(char *name) {
    unsigned char n = (unsigned char)strlen(name);
    char c1, c2, c3;
    if (n < 4) return 0;
    if (name[n - 4] != '.') return 0;
    c1 = name[n - 3]; if (c1 >= 'a') c1 -= 32;
    c2 = name[n - 2]; if (c2 >= 'a') c2 -= 32;
    c3 = name[n - 1]; if (c3 >= 'a') c3 -= 32;
    return (c1 == 'S' && c2 == 'G' && c3 == 'X') ? 1 : 0;
}

// File_Command is an internal SCC library function not exposed in headers.
extern unsigned char File_Command(void);

// --------------------------------------------------------------------------
// Scan the configured directory for SGX files and populate filenames[].
// DIRINP requires a wildcard component — append '*.*' to the directory path.
// --------------------------------------------------------------------------
static unsigned char scan_dir(void) {
    char searchpath[60];
    int n, i;
    unsigned char plen;

    strncpy(searchpath, cfgdat + 5, 52);
    searchpath[52] = 0;

    plen = (unsigned char)strlen(searchpath);
    while (plen > 0 && searchpath[plen - 1] == ' ') searchpath[--plen] = 0;
    if (plen > 0 && searchpath[plen - 1] != '\\' && searchpath[plen - 1] != '/') {
        if (plen < 56) {
            searchpath[plen]     = '\\';
            searchpath[plen + 1] = 0;
            plen++;
        }
    }
    if (plen < 56) {
        searchpath[plen]     = '*';
        searchpath[plen + 1] = '.';
        searchpath[plen + 2] = '*';
        searchpath[plen + 3] = 0;
        plen += 3;
    }

    // Write path into dbg_line1 (_transfer = common memory) before the call.
    strncpy(dbg_line1, searchpath, 60);
    dbg_line1[60] = 0;

    _msemaon();
    _symmsg[1]  = 38;
    _symmsg[3]  = _symbank;
    *((unsigned short*)(_symmsg + 4))  = (unsigned short)sizeof(dirbuf);
    *((char**)(_symmsg + 6))           = (void*)dirbuf;
    *((char**)(_symmsg + 8))           = searchpath;
    _symmsg[10] = ATTRIB_DIR | ATTRIB_VOLUME;
    _symmsg[11] = _symbank;
    *((unsigned short*)(_symmsg + 12)) = 0;
    dbg_fc_ret = File_Command();
    dbg_msg2   = (unsigned char)_symmsg[2];
    dbg_msg8   = (unsigned char)_symmsg[8];
    dbg_msg9   = (unsigned char)_symmsg[9];
    if (dbg_fc_ret == 0)
        n = (int)((unsigned int)dbg_msg8 | ((unsigned int)dbg_msg9 << 8));
    else
        n = -1;
    _msemaoff();
    dir_read_count = n;

    if (dbg_fc_ret == 0 && (dbg_msg8 > 0 || dbg_msg9 > 0))
        dbg_e_att = (unsigned char)dirbuf[8];
    else
        dbg_e_att = 0xFF;

    if (n <= 0) { dir_read_ok = 0; file_count = 0; return 0; }
    dir_read_ok = 1;

    file_count = 0;
    {
        char *raw = dirbuf;
        unsigned char name_len;
        for (i = 0; i < n && file_count < MAX_FILES; i++) {
            unsigned char raw_attrib = (unsigned char)raw[8];
            char *name = raw + 9;
            name_len = (unsigned char)strlen(name);
            if (!(raw_attrib & ATTRIB_DIR) && is_sgx(name)) {
                strncpy(filenames[file_count], name, 13);
                filenames[file_count][13] = 0;
                file_count++;
            }
            raw += 9 + name_len + 1;
        }
    }
    return file_count;
}

// --------------------------------------------------------------------------
// Wait up to `ticks` 50 Hz ticks, returning early on user activity.
// Returns: 0 = delay expired  1 = mouse/key  2 = quit message received
// --------------------------------------------------------------------------
static unsigned char wait_delay(unsigned short ticks) {
    unsigned short start, mx0, my0, resp;

    start = Sys_Counter16();
    mx0 = Mouse_X();
    my0 = Mouse_Y();

    while ((unsigned short)(Sys_Counter16() - start) < ticks) {
        resp = Msg_Receive(_sympid, -1, _symmsg);
        if (resp & 1) {
            if ((unsigned char)_symmsg[0] == 0) return 2;
        }
        if (Mouse_X() != mx0 || Mouse_Y() != my0 || Mouse_Buttons()) return 1;
        if (any_key_down()) return 1;
        Idle();
        vram_restore_lower();
    }
    return 0;
}

// --------------------------------------------------------------------------
// Config dialog helpers
// --------------------------------------------------------------------------
static void cfg_open(void) {
    if (cfgwin_id >= 0) return;

    tmp_delay = cfgdat[4];
    strncpy(tmp_path, cfgdat + 5, MAX_PATH);
    tmp_path[MAX_PATH] = 0;

    rg_delay[0] = rg_delay[1] = rg_delay[2] = rg_delay[3] = -1;

    cfg_inp.text      = tmp_path;
    cfg_inp.scroll    = 0;
    cfg_inp.cursor    = 0;
    cfg_inp.selection = 0;
    cfg_inp.len       = (unsigned short)strlen(tmp_path);
    cfg_inp.maxlen    = MAX_PATH;
    cfg_inp.flags     = 0;
    cfg_inp.textcolor = (COLOR_BLACK << 2) | COLOR_ORANGE;
    cfg_inp.linecolor = (COLOR_BLACK << 2) | COLOR_ORANGE;

    memset(&cfgcg, 0, sizeof(cfgcg));
    cfgcg.controls = 11;
    cfgcg.pid      = _sympid;
    cfgcg.first    = &ccc0;

    memset(&cfgwin, 0, sizeof(cfgwin));
    cfgwin.state    = WIN_NORMAL;
    cfgwin.flags    = WIN_TITLE | WIN_CENTERED | WIN_NOTTASKBAR;
    cfgwin.pid      = _sympid;
    cfgwin.w        = 220;
    cfgwin.h        = 80;
    cfgwin.wfull    = 220;
    cfgwin.hfull    = 80;
    cfgwin.wmin     = 220;
    cfgwin.hmin     = 80;
    cfgwin.wmax     = 220;
    cfgwin.hmax     = 80;
    cfgwin.title    = cfg_title;
    cfgwin.controls = &cfgcg;

    cfgwin_id = Win_Open(_symbank, &cfgwin);
}

static void cfg_close(void) {
    if (cfgwin_id < 0) return;
    Win_Close((unsigned char)cfgwin_id);
    cfgwin_id = -1;
}

static void cfg_ok(void) {
    cfgdat[4] = tmp_delay;
    strncpy(cfgdat + 5, tmp_path, MAX_PATH);
    cfgdat[5 + MAX_PATH] = 0;
    cfg_close();
    if (cfg_prz) {
        _symmsg[0] = MSR_SAV_CONFIG;
        _symmsg[1] = _symbank;
        _symmsg[2] = (char)((unsigned short)cfgdat & 0xFF);
        _symmsg[3] = (char)((unsigned short)cfgdat >> 8);
        while (!Msg_Send(_sympid, cfg_prz, _symmsg));
        cfg_prz = 0;
    }
}

static void cfg_cancel(void) {
    cfg_close();
    cfg_prz = 0;
}

// --------------------------------------------------------------------------
// Animation entry point — called on MSC_SAV_START or in demo mode
// --------------------------------------------------------------------------
void start_animation(void) {
    signed char  wid;
    unsigned short delay, resp;
    unsigned char  result, plen, fc;
    char filepath[70];
    char basepath[60];

    is_msx = ((Sys_Type() & TYPE_MSX) != 0) ? 1 : 0;
    if (is_msx) {
        screen_w = SCREEN_W_MSX;
        screen_h = SCREEN_H_MSX;
    } else {
        screen_w = SCREEN_W_CPC;
        screen_h = SCREEN_H_CPC;
    }

    scan_dir();

    empty_str[0] = 0;

    anim_ctrl[0].value  = 0;
    anim_ctrl[0].type   = C_AREA;
    anim_ctrl[0].bank   = -1;
    anim_ctrl[0].param  = AREA_16COLOR | COLOR_BLACK;
    anim_ctrl[0].x      = 0;
    anim_ctrl[0].y      = 0;
    anim_ctrl[0].w      = screen_w;
    anim_ctrl[0].h      = screen_h;
    anim_ctrl[0].unused = 0;

    memset(&anim_cg, 0, sizeof(anim_cg));
    anim_cg.controls = 1;
    anim_cg.pid      = _sympid;
    anim_cg.first    = &anim_ctrl[0];

    memset(&anim_win, 0, sizeof(anim_win));
    anim_win.state    = WIN_NORMAL;
    anim_win.flags    = WIN_NOTTASKBAR | WIN_NOTMOVEABLE;
    anim_win.pid      = _sympid;
    anim_win.w        = screen_w;
    anim_win.h        = screen_h;
    anim_win.wfull    = screen_w;
    anim_win.hfull    = screen_h;
    anim_win.wmin     = 32;
    anim_win.hmin     = 24;
    anim_win.wmax     = screen_w;
    anim_win.hmax     = screen_h;
    anim_win.title    = empty_str;
    anim_win.status   = empty_str;
    anim_win.controls = &anim_cg;

    wid = Win_Open(_symbank, &anim_win);
    if (wid < 0) return;

    desktop_stop((unsigned char)wid);
    delay = get_delay_ticks((unsigned char)cfgdat[4]);

    strncpy(basepath, cfgdat + 5, 52);
    basepath[52] = 0;
    plen = (unsigned char)strlen(basepath);
    while (plen > 0 && basepath[plen - 1] == ' ') basepath[--plen] = 0;
    if (plen > 0 && basepath[plen - 1] != '\\' && basepath[plen - 1] != '/') {
        if (plen < 58) {
            basepath[plen]     = '\\';
            basepath[plen + 1] = 0;
            plen++;
        }
    }

    cur_file = 0;
    fc       = file_count;
    result   = 0;

    while (result == 0) {
        if (fc > 0) {
            strncpy(filepath, basepath, 58);
            strncat(filepath, filenames[cur_file], 13);

            vram_clear();
            Idle();
            load_image(filepath);

            result = wait_delay(delay);
            if (result == 0)
                cur_file = (unsigned char)((cur_file + 1) % fc);
        } else {
            vram_clear();
            result = wait_delay(500u);
        }
    }

    desktop_cont();
    Idle();
    Win_Close((unsigned char)wid);
    Screen_Redraw();
    if (result == 2) exit(0);
}

// --------------------------------------------------------------------------
// Main — screensaver protocol
// --------------------------------------------------------------------------
int main(int argc, char *argv[]) {
    unsigned short resp;
    unsigned char  got_msg, sender, b;

    dir_read_ok    = 0;
    dir_read_count = 0;
    cfgdat[0] = 'S'; cfgdat[1] = 'L'; cfgdat[2] = 'I'; cfgdat[3] = 'D';
    cfgdat[4] = 2;   // delay: 5 s
    cfgdat[5]  = 'A'; cfgdat[6]  = ':'; cfgdat[7]  = '\\';
    cfgdat[8]  = 'S'; cfgdat[9]  = 'L'; cfgdat[10] = 'I';
    cfgdat[11] = 'D'; cfgdat[12] = 'E'; cfgdat[13] = 'S';
    cfgdat[14] = '\\'; cfgdat[15] = 0;

    got_msg = 0;
    sender  = 0;

    for (b = 0; b < 10; b++) {
        Idle();
        resp = Msg_Receive(_sympid, -1, _symmsg);
        if (resp & 0x01) {
            got_msg = 1;
            sender  = (unsigned char)(resp >> 8);
            break;
        }
    }

    if (!got_msg) {
        start_animation();
        exit(0);
    }

    while (1) {
        switch (_symmsg[0]) {

        case 0:
            exit(0);

        case MSC_SAV_INIT:
            Bank_Copy(
                _symbank, init_tmp,
                (unsigned char)_symmsg[1],
                (char *)((unsigned short)((unsigned char)_symmsg[3] << 8)
                         | (unsigned char)_symmsg[2]),
                64u);
            if (init_tmp[0] == 'S' && init_tmp[1] == 'L' &&
                init_tmp[2] == 'I' && init_tmp[3] == 'D') {
                memcpy(cfgdat, init_tmp, 64);
            }
            break;

        case MSC_SAV_START:
            start_animation();
            break;

        case MSC_SAV_CONFIG:
            cfg_prz = sender;
            cfg_open();
            break;

        default:
            if ((unsigned char)_symmsg[0] == MSR_DSK_WCLICK &&
                cfgwin_id >= 0 &&
                (unsigned char)_symmsg[1] == (unsigned char)cfgwin_id) {

                if ((unsigned char)_symmsg[2] == DSK_ACT_CLOSE) {
                    cfg_cancel();
                } else if ((unsigned char)_symmsg[2] == DSK_ACT_CONTENT) {
                    if ((unsigned char)_symmsg[8] == 9)
                        cfg_ok();
                    else if ((unsigned char)_symmsg[8] == 10)
                        cfg_cancel();
                }
            }
            break;
        }

        do {
            resp = Msg_Sleep(_sympid, -1, _symmsg);
        } while (!(resp & 0x01));

        sender = (unsigned char)(resp >> 8);
    }
}
