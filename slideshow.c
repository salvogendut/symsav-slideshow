// slideshow.c — SGX image slideshow screensaver for SymbOS
// Displays a rotating slideshow of 4-colour (Mode 1) SGX images from a directory.
// Config: delay (3/5/10/30 s) and path stored in 64-byte SYMBOS.INI config block.

#include <symbos.h>
#include <symbos/msgid.h>
#include <symbos/keys.h>
#include <symbos/file.h>
#include <stdlib.h>
#include <string.h>

#define MSC_SAV_INIT   1
#define MSC_SAV_START  2
#define MSC_SAV_CONFIG 3
#define MSR_SAV_CONFIG 4

#define SCREEN_W   320
#define SCREEN_H   200
#define MAX_FILES  40
#define MAX_PATH   52

// --------------------------------------------------------------------------
// Data-segment buffers
// --------------------------------------------------------------------------

// Buffer for one 4-colour SGX chunk (max 160×200 = 8000 bytes)
_data unsigned char imgbuf[8192];

// Directory listing buffer — enough for MAX_FILES DirEntry structs (22 bytes each)
_data char dirbuf[1024];

// All-0xF0 plane used to clear VRAM to ink1 (black)
_data unsigned char zero_plane[2000];

// Config block: [0-3]="SLID", [4]=delay_idx(1-4), [5..56]=image path (null-term)
_data char cfgdat[64];
_data char init_tmp[64];

// File list (up to MAX_FILES filenames, each max 13 chars + null)
_data char filenames[MAX_FILES][14];
_data unsigned char file_count;
_data unsigned char cur_file;

// 4-byte scratch buffer for reading SGX chunk headers
_data unsigned char chunk_hdr[4];

// --------------------------------------------------------------------------
// Transfer segment: animation window
// --------------------------------------------------------------------------
_transfer Ctrl       anim_ctrl[1];
_transfer Ctrl_Group anim_cg;
_transfer Window     anim_win;
_transfer char       empty_str[1];

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
// Desktop stop / resume (same pattern as mountain.c / xroach / xmatrix)
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
// Fill VRAM with ink1 (black = 0xF0 per byte) across all 8 interleave planes
// --------------------------------------------------------------------------
static void vram_clear(void) {
    unsigned char k;
    for (k = 0; k < 8; k++) {
        Bank_Copy(0,
            (char *)(0xC000u + (unsigned short)k * 0x0800u),
            _symbank, (char *)zero_plane, 2000u);
    }
}

// --------------------------------------------------------------------------
// Blit the current imgbuf (one decoded SGX chunk) into CPC Mode-1 VRAM.
//
// Mode-1 VRAM layout: address = 0xC000 + (y/8)*80 + (y%8)*0x800 + x_byte
// SGX pixel data is in linear scanline order, same byte encoding as VRAM.
//
// x_off_bytes : byte offset within each VRAM row (0 = left, 40 = right half)
// row_bytes   : bytes per scanline in imgbuf (= width_px / 4 for Mode 1)
// height      : number of scanlines to blit
// --------------------------------------------------------------------------
static void blit_chunk(unsigned char x_off_bytes,
                       unsigned char row_bytes,
                       unsigned char height) {
    unsigned char y, scan, crow;
    unsigned short addr;
    for (y = 0; y < height; y++) {
        scan = y & 7;
        crow = y >> 3;
        addr = 0xC000u
             + (unsigned short)crow * 80u
             + (unsigned short)scan * 0x0800u
             + (unsigned short)x_off_bytes;
        Bank_Copy(0, (char *)addr,
                  _symbank,
                  (char *)imgbuf + (unsigned short)y * (unsigned short)row_bytes,
                  (unsigned short)row_bytes);
    }
}

// --------------------------------------------------------------------------
// Open and display all chunks of one SGX file.
// Supports 4-colour uncompressed and ZX0-compressed chunks.
// 16-colour extended chunks (byte1 == 0x05) stop processing that file.
// Returns 1 on success, 0 if the file could not be opened.
// --------------------------------------------------------------------------
static unsigned char load_image(char *path) {
    unsigned char fd, compressed, row_bytes, height, x_off;
    unsigned short bytes_read, data_size;

    fd = File_Open(_symbank, path);
    if (fd > 7) return 0;

    x_off = 0;
    for (;;) {
        bytes_read = File_Read(fd, _symbank, (char *)chunk_hdr, 3u);
        if (bytes_read < 3 || chunk_hdr[0] == 0) break;

        if (chunk_hdr[1] == 0x05) break;   // 16-colour extended chunk — unsupported

        compressed = (chunk_hdr[0] & 0x80) ? 1 : 0;
        row_bytes  = chunk_hdr[0] & 0x3F;
        height     = chunk_hdr[2];

        if (row_bytes == 0 || height == 0) break;

        data_size = (unsigned short)row_bytes * (unsigned short)height;
        if (data_size > 8190u) break;       // safety: won't fit in imgbuf

        if (compressed) {
            File_ReadComp(fd, _symbank, (char *)imgbuf, data_size);
        } else {
            File_Read(fd, _symbank, (char *)imgbuf, data_size);
        }

        blit_chunk(x_off, row_bytes, height);
        x_off = (unsigned char)(x_off + row_bytes);
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
// Scan the configured directory for *.SGX files and populate filenames[].
// Returns the number of files found.
// --------------------------------------------------------------------------
static unsigned char scan_dir(void) {
    char searchpath[64];
    int n, i;
    unsigned char plen;
    DirEntry *entry;

    strncpy(searchpath, cfgdat + 5, 52);
    searchpath[52] = 0;

    plen = (unsigned char)strlen(searchpath);
    while (plen > 0 && searchpath[plen - 1] == ' ') searchpath[--plen] = 0;
    if (plen > 0 && searchpath[plen - 1] != '\\' && searchpath[plen - 1] != '/') {
        if (plen < 62) {
            searchpath[plen]     = '\\';
            searchpath[plen + 1] = 0;
            plen++;
        }
    }
    if (plen < 58) strcat(searchpath, "*.SGX");

    n = Dir_Read(searchpath,
                 ATTRIB_HIDDEN | ATTRIB_SYSTEM | ATTRIB_VOLUME | ATTRIB_DIR,
                 (void *)dirbuf, (unsigned short)sizeof(dirbuf), 0);
    if (n <= 0) { file_count = 0; return 0; }

    file_count = 0;
    entry = (DirEntry *)dirbuf;
    for (i = 0; i < n && file_count < MAX_FILES; i++) {
        if (!(entry->attrib & ATTRIB_DIR) && strlen(entry->name) >= 4) {
            strncpy(filenames[file_count], entry->name, 13);
            filenames[file_count][13] = 0;
            file_count++;
        }
        entry++;
    }
    return file_count;
}

// --------------------------------------------------------------------------
// Wait up to `ticks` Idle() calls, returning early on user activity.
// Returns: 0 = delay expired  1 = mouse/key  2 = quit message received
// --------------------------------------------------------------------------
static unsigned char wait_delay(unsigned short ticks) {
    unsigned short t, mx0, my0, resp;

    mx0 = Mouse_X();
    my0 = Mouse_Y();

    for (t = 0; t < ticks; t++) {
        resp = Msg_Receive(_sympid, -1, _symmsg);
        if (resp & 1) {
            if ((unsigned char)_symmsg[0] == 0) return 2;
        }
        if (Mouse_X() != mx0 || Mouse_Y() != my0 || Mouse_Buttons()) return 1;
        if (any_key_down()) return 1;
        Idle();
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

    scan_dir();

    memset(zero_plane, 0xF0, sizeof(zero_plane));

    // Open fullscreen window (no title, not in taskbar, not moveable)
    empty_str[0] = 0;

    anim_ctrl[0].value  = 0;
    anim_ctrl[0].type   = C_AREA;
    anim_ctrl[0].bank   = -1;
    anim_ctrl[0].param  = AREA_16COLOR | COLOR_BLACK;
    anim_ctrl[0].x      = 0;
    anim_ctrl[0].y      = 0;
    anim_ctrl[0].w      = SCREEN_W;
    anim_ctrl[0].h      = SCREEN_H;
    anim_ctrl[0].unused = 0;

    memset(&anim_cg, 0, sizeof(anim_cg));
    anim_cg.controls = 1;
    anim_cg.pid      = _sympid;
    anim_cg.first    = &anim_ctrl[0];

    memset(&anim_win, 0, sizeof(anim_win));
    anim_win.state    = WIN_NORMAL;
    anim_win.flags    = WIN_NOTTASKBAR | WIN_NOTMOVEABLE;
    anim_win.pid      = _sympid;
    anim_win.w        = SCREEN_W;
    anim_win.h        = SCREEN_H;
    anim_win.wfull    = SCREEN_W;
    anim_win.hfull    = SCREEN_H;
    anim_win.wmin     = 32;
    anim_win.hmin     = 24;
    anim_win.wmax     = SCREEN_W;
    anim_win.hmax     = SCREEN_H;
    anim_win.title    = empty_str;
    anim_win.status   = empty_str;
    anim_win.controls = &anim_cg;

    wid = Win_Open(_symbank, &anim_win);
    if (wid < 0) return;

    desktop_stop((unsigned char)wid);

    delay = get_delay_ticks((unsigned char)cfgdat[4]);

    // Build base path (ensure trailing backslash)
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

    vram_clear();
    Idle();

    cur_file = 0;
    fc       = file_count;
    result   = 0;

    while (result == 0) {
        if (fc > 0) {
            // Build full path and load the current image
            strncpy(filepath, basepath, 58);
            strncat(filepath, filenames[cur_file], 13);

            vram_clear();
            Idle();
            load_image(filepath);

            result = wait_delay(delay);
            if (result == 0)
                cur_file = (unsigned char)((cur_file + 1) % fc);
        } else {
            // No images configured — black screen, keep checking until user acts
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
// Main — screensaver protocol (same structure as mountain.c)
// --------------------------------------------------------------------------
int main(int argc, char *argv[]) {
    unsigned short resp;
    unsigned char  got_msg, sender, b;

    // Initialise defaults (_data is not statically initialised by the loader)
    cfgdat[0] = 'S'; cfgdat[1] = 'L'; cfgdat[2] = 'I'; cfgdat[3] = 'D';
    cfgdat[4] = 2;   // delay: 5 s
    // Default path: A:\SLIDES\ (null-terminated)
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
        // Demo mode: no message from desktop, start immediately
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
