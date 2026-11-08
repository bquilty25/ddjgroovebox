/* SPDX-License-Identifier: GPL-3.0-only */
/* SloopCore: SLOOP's firmware as one compilation unit for macOS. Same include order as firmware/src/felucca.c and
 * the upstream host harness tests/editor_host.c (which runs the real UI, storage, projects and editor protocol on
 * a RAM image of the NOR); the FM-1's hardware is replaced by the doubles below. Nothing of the firmware changes. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <stddef.h>
#include <unistd.h>
#include <fcntl.h>
#include "sloop_core.h"

/* the firmware sources come from vendor/sloop/firmware/src (on the include path) */
#define FELUCCA_ARRANGER 1
#define FELUCCA_FLASH 1
#define FELUCCA_OTA 1

/* ---- the NOR flash: a RAM image, read where it is (the XIP window), saved to a file */
static uint8_t nor[0x100000] __attribute__((aligned(4096)));
#define SMP_USER_XIP(k) ((const uint8_t *)nor + SMP_USER_OFF((uint32_t)(k)))
#define FM6_BANK_XIP(copy) ((const uint8_t *)nor + st_sector(OBJ_FM6BANK, copy) + ST_PAYLOAD_OFF)
#define ST_XIP_PTR(off) ((const uint8_t *)nor + (off))

/* the firmware's section attributes (.noinit, .ram) mean nothing here */
#define __attribute__(x)
#define memset felucca_memset
#define memcpy felucca_memcpy
#define memcmp felucca_memcmp
#include "felucca_tables.h"
#include "libc.c"
#undef memset
#undef memcpy
#undef memcmp

static struct { volatile uint32_t notes, buttons; } fm1_in;
static volatile uint32_t fm1_remote_btn;          /* the buttons held over MIDI REMOTE (fm1_input.h) */
static void fm1_irq_off(void) {}                  /* one lock around the whole firmware: no interrupts to mask */
static void fm1_irq_on(void) {}
#include "core.h"
#include "cpu1.h"
/* the second core: none here, every render unit runs on the calling thread (as firmware/host/preview.c) */
static int cpu1_run(cpu1_job_t fn, uint32_t arg) { (void)fn; (void)arg; return 0; }
static int cpu1_wait(void) { return 0; }
static int cpu1_on(void) { return 0; }
static uint32_t cpu1_ticks(void) { return 0; }
#include "engines.c"
#include "drums.c"
#include "params.c"
#include "voice.c"
#include "slicer.c"
#include "fx.c"
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#include "usb.c"
#include "midi_uart.c"
#include "arranger.c"
#include "seq.c"

/* ---- the screen */
static uint16_t screen[240 * 240];                /* RGB565, byte-swapped as the LCD takes it */

/* The text tap: every text SLOOP draws and where it lands on the screen (sloop_screen_texts), so the app can print
 * what the screen says next to the control that changes it (the knobs' names, a layer's 16 tiles). SLOOP draws text
 * into an off-screen canvas (gfx.c cv_begin, cv_text) and blits the canvas (cv_blit): the texts of a canvas join the
 * map when it is blitted, and whatever the map held under a blit or a fill goes. */
#define TXT_MAX 192u
#define TXT_PEND 64u
typedef struct { int16_t x, y, w, h; uint16_t c; char s[24]; } txt_t;
static txt_t txt_map[TXT_MAX], txt_pend[TXT_PEND];
static uint32_t txt_n, pend_n;
static void txt_clear(int32_t x, int32_t y, int32_t w, int32_t h)   /* the texts under a region go */
{
    uint32_t i, k = 0;
    for (i = 0; i < txt_n; i++) {
        const txt_t *t = &txt_map[i];
        int hit = t->x < x + w && t->x + t->w > x && t->y < y + h && t->y + t->h > y;
        if (!hit) txt_map[k++] = *t;
    }
    txt_n = k;
}
static void txt_add(const txt_t *t, int32_t dx, int32_t dy)
{
    if (txt_n >= TXT_MAX) return;
    txt_map[txt_n] = *t;
    txt_map[txt_n].x = (int16_t)(t->x + dx);
    txt_map[txt_n].y = (int16_t)(t->y + dy);
    txt_n++;
}

static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    uint32_t i, j;
    if (x + w > 240u || y + h > 240u) return;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            screen[(y + j) * 240u + x + i] = p[j * w + i];
    txt_clear((int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h);
}
/* gfx.c's canvas and text calls, renamed as it is included: the wrappers below stand in for them in the UI */
#define cv_begin cv_begin_fw
#define cv_text cv_text_fw
#define cv_blit cv_blit_fw
#define cv_blit_from cv_blit_from_fw
#define draw_text_box draw_text_box_fw
#include "gfx.c"
#undef cv_begin
#undef cv_text
#undef cv_blit
#undef cv_blit_from
#undef draw_text_box
static void cv_begin(uint32_t w, uint32_t h, uint16_t bg) { pend_n = 0; cv_begin_fw(w, h, bg); }
static int32_t cv_text(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    if (s && *s && pend_n < TXT_PEND) {
        txt_t *t = &txt_pend[pend_n++];
        uint32_t i;
        t->x = (int16_t)x; t->y = (int16_t)y; t->w = (int16_t)text_w(f, s); t->h = (int16_t)f->h; t->c = c;
        for (i = 0; i < sizeof t->s - 1u && s[i]; i++) t->s[i] = s[i];
        t->s[i] = 0;
    }
    return cv_text_fw(x, y, f, s, c);
}
static void cv_blit(uint32_t x, uint32_t y)
{
    uint32_t i;
    cv_blit_fw(x, y);                               /* (lcd_blit: the old texts under it go) */
    for (i = 0; i < pend_n; i++)
        if (txt_pend[i].y < (int32_t)cv_h) txt_add(&txt_pend[i], (int32_t)x, (int32_t)y);
}
static void cv_blit_from(uint32_t x, uint32_t y, uint32_t r0)
{
    uint32_t i;
    cv_blit_from_fw(x, y, r0);
    for (i = 0; i < pend_n; i++)
        if (txt_pend[i].y >= (int32_t)r0 && txt_pend[i].y < (int32_t)cv_h) txt_add(&txt_pend[i], (int32_t)x, (int32_t)y);
}
static void draw_text_box(uint32_t x, uint32_t y, uint32_t w, const felucca_font_t *f, const char *s, uint16_t c, int align)
{
    int32_t tw = text_w(f, s), tx = align == 1 ? ((int32_t)w - tw) / 2 : align == 2 ? (int32_t)w - tw : 0;
    txt_t t;
    uint32_t i;
    draw_text_box_fw(x, y, w, f, s, c, align);
    if (!s || !*s) return;
    t.x = (int16_t)tx; t.y = 0; t.w = (int16_t)tw; t.h = (int16_t)f->h; t.c = c;
    for (i = 0; i < sizeof t.s - 1u && s[i]; i++) t.s[i] = s[i];
    t.s[i] = 0;
    txt_add(&t, (int32_t)x, (int32_t)y);
}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    if (x + w > 240u || y + h > 240u) return;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            screen[(y + j) * 240u + x + i] = swap16(c);
    txt_clear((int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h);
}

/* ---- the panel: the FM-1's real key matrix (hal/fm1_input.h), so the LED picture decodes back to ids */
static int32_t encs[7];
static uint32_t fm1_ticks(void) { return fm1_ms * 1000u * 24u; }
#define FM1_TICKS_PER_US 24u
static int32_t fm1_enc_take(uint32_t e) { int32_t s = encs[e]; encs[e] = 0; return s; }
#define FM1_NCOL 11u
static uint8_t fm1_led[16], fm1_led_dim[16], fm1_led_bg[16];
static volatile uint16_t fm1_led_bg_ns;
static const int8_t FM1_KEYMAP[6][FM1_NCOL] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};
static void fm1_led_key(uint32_t id, int on) { (void)id; (void)on; }
static uint32_t edges_btn, notes_seen;
static uint32_t fm1_input_edges(int x) { uint32_t e = edges_btn; (void)x; edges_btn = 0; return e; }
static uint32_t fm1_input_note_edges(void) { uint32_t e = fm1_in.notes & ~notes_seen; notes_seen = fm1_in.notes; return e; }
/* The firmware's clock (fm1_ms) follows the audio rendered, plus a skew: a firmware loop that waits for time while it
 * feeds the watchdog (HARDWARE CALIBRATION's panel_setup, the update protocol) runs inside the render, where the audio
 * clock cannot move; each feed moves the skew 1 ms on, so such a loop reaches its timeout instead of hanging the app
 * (as upstream's tests/ui_pages_test.c does with its wdt_tick_ms). */
static uint32_t clock_skew_ms;
static void fm1_wdt_feed(void) { clock_skew_ms++; fm1_ms++; }
static int32_t fm1_adc_read(int c) { (void)c; return -1; }
static struct { uint32_t magic, stage, page, home, ui_frames; } felucca_dbg;
#define FELUCCA_ICONS 1
#define SCOPE_N 512u
static int16_t scope_buf[SCOPE_N], scope_bufr[SCOPE_N];
static uint32_t scope_w;
#include "panel.c"
#include "ui.c"
#include "ui_song.c"
#include "ui_studio.c"
#include "icons.c"
#include "ui_draw.c"
#include "ui_vis.c"
#include "ui_layers.c"
#include "ui_menu.c"
#include "ui_input.c"

/* ---- felucca.c's flash part, on the RAM image (as tests/editor_host.c and hal/fm1_flash.h) */
#define FL_DATA_LO 0x00097000u
#define FL_DATA_HI 0x000E0000u
#define FL_GLOB_LO 0x000FC000u
#define FL_GLOB_HI 0x000FF000u
#define FL_OTA_LO 0x000E0000u
#define FL_OTA_HI 0x000E5000u
#define FL_FM6_LO 0x000E5000u
#define FL_FM6_HI 0x000E7000u
#define FL_USR4_LO 0x000E7000u
#define FL_USR4_HI 0x000FB000u
#define FL_IN(off, n, lo, hi) ((uint32_t)(off) >= (lo) && (uint32_t)(off) <= (hi) && (uint32_t)(n) <= (hi) - (uint32_t)(off))
#define FL_STORE_OK(off, n) (FL_IN(off, n, FL_DATA_LO, FL_DATA_HI) || FL_IN(off, n, FL_GLOB_LO, FL_GLOB_HI) || \
                             FL_IN(off, n, FL_FM6_LO, FL_FM6_HI) || FL_IN(off, n, FL_USR4_LO, FL_USR4_HI))
#define FL_RANGE_OK(off, n) (FL_STORE_OK(off, n) || FL_IN(off, n, FL_OTA_LO, FL_OTA_HI))
#define FL_FAR(fn) fn
#include "fm1_flash_id.h"                             /* FL_JEDEC_OK: the flash parts the FM-1 ships with */
static uint32_t fl_writes;                           /* erases + programs: the image changed */
static uint32_t fl_jedec_ram(void) { return 0x856014u; }
static void fl_plain_window_init(void) {}
static uint32_t irq_save(void) { return 0; }
static void irq_restore(uint32_t f) { (void)f; }
static void fl_inval(uint32_t off, uint32_t n) { (void)off; (void)n; }
static int fl_erase4k_ram(uint32_t off, uint32_t *took)
{
    *took = 0;
    if ((off & 0xFFFu) || !FL_RANGE_OK(off, 0x1000u)) return -1;
    felucca_memset(nor + off, 0xFF, 0x1000u);
    fl_writes++;
    return 0;
}
static int fl_prog_ram(uint32_t off, const uint8_t *src, uint32_t n, uint32_t *took)
{
    uint32_t i;
    *took = 0;
    if (!FL_RANGE_OK(off, n) || n == 0 || n > 256u || ((off & 0xFFu) + n) > 256u) return -1;
    for (i = 0; i < n; i++)
        nor[off + i] &= src[i];                       /* NOR: a program only clears bits */
    fl_writes++;
    return 0;
}
static int fl_read_ram(uint32_t off, uint8_t *dst, uint32_t n)
{
    if (off >= sizeof nor || n > sizeof nor - off) return -1;
    felucca_memcpy(dst, nor + off, n);
    return 0;
}
static int fl_write(uint32_t off, const uint8_t *src_ram, uint32_t n)
{
    uint32_t took;
    while (n) {
        uint32_t chunk = 256u - (off & 0xFFu);
        int rc;
        if (chunk > n) chunk = n;
        rc = fl_prog_ram(off, src_ram, chunk, &took);
        if (rc) return rc;
        off += chunk; src_ram += chunk; n -= chunk;
    }
    return 0;
}
static uint8_t flash_ok;
static int st_read(uint32_t off, void *dst, uint32_t n) { return fl_read_ram(off, dst, n); }
static void audio_silence(void) {}
static int fl_erase4k_quiet(uint32_t off, uint32_t *took) { audio_silence(); return fl_erase4k_ram(off, took); }
static int st_erase(uint32_t off) { uint32_t took; if (!FL_STORE_OK(off, 0x1000u)) return -8; return fl_erase4k_quiet(off, &took); }
static int st_prog(uint32_t off, const void *src, uint32_t n) { if (!FL_STORE_OK(off, n)) return -8; return fl_write(off, src, n); }
#include "storage.c"
#include "upreset.c"
#include "project.c"
/* the update hooks usb.c and editor.c need (felucca.c): no updates here, the editor protocol only */
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void) { so_r = so_w; fm1_wdt_feed(); }   /* (no USB host to take SysEx replies: drop them, time moves) */
#include "editor.c"
#include "splash.c"

/* ======================================================================= the host ===================== */

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock(&mu)
#define UNLOCK() pthread_mutex_unlock(&mu)

/* the input queue: applied at the next UI pass, one change per key / button per pass */
enum { EV_KEY, EV_BTN, EV_ENC, EV_BARRIER, EV_SELECT };
typedef struct { uint8_t type, id; int16_t value; } ev_t;
#define EVQ 512u
static ev_t evq[EVQ];
static uint32_t ev_r, ev_w;

static uint64_t samples_done;                      /* the clock: fm1_ms follows the audio rendered */
static uint32_t last_ui_ms, last_pass_ms;
static uint32_t ui_frame_count;
static int32_t blk[CTL * 2];
static uint32_t blk_pos = CTL;                     /* frames of blk already handed out */
static float peak_l, peak_r;
static uint32_t screen_frame;
static uint32_t saved_writes;
static int booted;
static uint32_t local_btn, remote_was;            /* the buttons held from the app; MIDI REMOTE's, last pass */

/* MIDI into the firmware: a host ring in front of usb.c's 64-slot one, fed at each block while it has room (the FM-1
 * NAKs the USB host the same way), so a burst or input while the audio is stopped is not lost */
#define HMQ 2048u
static uint32_t hmi_q[HMQ], hmi_w, hmi_r;
/* MIDI out of the firmware, stamped with the sample it was produced at, for sending on time */
typedef struct { uint32_t pkt; uint64_t sample; } mo_t;
#define HMO 1024u
static mo_t hmo_q[HMO];
static uint32_t hmo_w, hmo_r;
static uint64_t anchor_sample, anchor_host;      /* the sample at the start of the last render, and its host time */

static void push(uint8_t type, uint8_t id, int16_t value)
{
    if (ev_w - ev_r >= EVQ) return;                /* full: dropped (never seen at human speed) */
    evq[ev_w % EVQ] = (ev_t){type, id, value};
    ev_w++;
}

/* one pass: the queued input up to a second change of the same key / button, then the UI reads it */
static void input_pass(void)
{
    uint64_t touched_keys = 0;
    uint32_t touched_btn = 0;
    while (ev_r != ev_w) {
        ev_t e = evq[ev_r % EVQ];
        if (e.type == EV_BARRIER) {
            ev_r++;
            break;
        }
        if (e.type == EV_KEY) {
            uint32_t bit = 1u << e.id;
            if (touched_keys & bit) break;
            touched_keys |= bit;
            if (e.value) fm1_in.notes |= bit;
            else fm1_in.notes &= ~bit;
        } else if (e.type == EV_BTN) {
            uint32_t bit = 1u << panel.btn[e.id];
            if (touched_btn & bit) break;
            touched_btn |= bit;
            if (e.value) { local_btn |= bit; edges_btn |= bit; }
            else local_btn &= ~bit;
        } else if (e.type == EV_ENC) {
            encs[panel.enc[e.id]] += e.value * panel.dir[e.id];
        } else if (e.type == EV_SELECT) {
            if (!ft_on) track_select(e.id);          /* (as ALGORITHM in ui_input: not in a free take) */
        }
        ev_r++;
    }
    /* the buttons held over MIDI (GLO > MIDI 3 > REMOTE) count as closed, as the FM-1's scan does (fm1_input.h) */
    edges_btn |= fm1_remote_btn & ~remote_was & ~local_btn;
    remote_was = fm1_remote_btn;
    fm1_in.buttons = local_btn | fm1_remote_btn;
    ui_input();
#if FELUCCA_OTA
    ed_service();
#endif
}

static void ui_frame(void)
{
    felucca_dbg.ui_frames++;
    input_pass();
    ui_leds();
    ui_draw();
    autosave_tick();
    sections_flush();
    ui_frame_count++;
    screen_frame = ui_frame_count;
}

/* one audio block of CTL frames, and the UI when its time comes (the device: ~60 frames/s, input in between) */
static void render_block(void)
{
    uint32_t i;
    while (hmi_r != hmi_w && MQ - (mi_w - mi_r) >= 24u)   /* MIDI in, as much as the firmware's ring takes */
        midi_in_event(hmi_q[hmi_r++ % HMQ]);
    mix_block(blk, CTL);
    for (i = 1; i < CTL; i += 2u) {                /* the scope, as audio.c fills it (the visualiser) */
        scope_bufr[scope_w & (SCOPE_N - 1u)] = vis_tap[2u * i + 1u];
        scope_buf[scope_w++ & (SCOPE_N - 1u)] = vis_tap[2u * i];
    }
    while (mo_r != mo_w) {                         /* MIDI out (clock, the sequencer): stamped with this block */
        if (hmo_w - hmo_r < HMO) hmo_q[hmo_w++ % HMO] = (mo_t){midi_out_q[mo_r % MQ], samples_done};
        mo_r++;
    }
    samples_done += CTL;
    fm1_ms = (uint32_t)(samples_done * 1000u / FS) + clock_skew_ms;
    if (fm1_ms - last_ui_ms >= 16u) {
        last_ui_ms = fm1_ms;
        last_pass_ms = fm1_ms;
        ui_frame();
    } else if (fm1_ms - last_pass_ms >= 3u) {
        last_pass_ms = fm1_ms;
        input_pass();
    }
}

/* power-on, as main.c felucca_init (Leo Kuroshita, isod89): three parts with their default sounds, the drum
 * track, empty patterns, then the project as it was left */
static void host_felucca_init(void)
{
    uint32_t i;
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    fm6_init();
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        track_defaults(t);
        if (i < NPART) {
            set_engine_of(t, TRK_DEF[i][0]);
            apply_preset_to(t, TRK_DEF[i][1]);
            t->engine = t->eng_req;
        }
        track_defaults_steps(t);
    }
    TDRUM->p[P_E0] = DRUM_DEFAULT_KIT;
    song.sel = 0;
    song.master_q12 = 2048;
    autosave_resume();
    song.g[G_SYNC] = (int16_t)lights_sync;
    song.g[G_MIDI] = (int16_t)lights_mout;
    song.g[G_ROUTE] = (int16_t)lights_min;
    song.g[G_CH1] = midi_set[0], song.g[G_CH2] = midi_set[1], song.g[G_CH3] = midi_set[2];
    song.g[G_DRCH] = midi_set[3], song.g[G_CHOTH] = midi_set[4], song.g[G_BEND] = midi_set[5];
    song.g[G_FXCH] = midi_set[6];
    song.g[G_DRMAP] = midi_set[7];
    song.g[G_CLKOUT] = midi_set[9];
    song.g[G_RMCH] = midi_set[8];
    song.g[G_FXFADE] = fxfade;
    layers_init();
    go_home();
    ui.force = 1;
}

int sloop_init(const char *flash_path)
{
    LOCK();
    if (booted) { UNLOCK(); return 0; }
    /* the user sample slots are read as 32-bit offsets from SMP_DATA (eng_sample.c smp_user_scan), as the flash is one
     * address space on the FM-1: the image must lie above SMP_DATA within 4 GiB (tests/editor_host.c checks the same) */
    if ((uintptr_t)nor < (uintptr_t)SMP_DATA || (uintptr_t)nor - (uintptr_t)SMP_DATA > 0xF0000000u) {
        UNLOCK();
        return -1;
    }
    int rc = 0;
    felucca_memset(nor, 0xFF, sizeof nor);
    if (flash_path) {
        FILE *f = fopen(flash_path, "rb");
        if (f) {
            size_t n = fread(nor, 1, sizeof nor, f);
            fclose(f);
            if (n != sizeof nor) {                   /* a damaged image: kept aside (never overwritten), start blank */
                char aside[1100];
                snprintf(aside, sizeof aside, "%s.damaged", flash_path);
                rename(flash_path, aside);
                felucca_memset(nor, 0xFF, sizeof nor);
                rc = 1;
            }
        }
    }
    panel = PANEL_DEFAULT;
    persist_boot();
    settings_init();
    panel_init();
    host_felucca_init();
    usb.config = 1;                                 /* the USB MIDI port "connected": MIDI in / out flow */
    saved_writes = fl_writes;
    booted = 1;
    ui_frame();
    UNLOCK();
    return rc;
}

void sloop_render_at(float *out, uint32_t frames, uint64_t host_time)
{
    LOCK();
    anchor_sample = samples_done - (blk_pos < CTL ? CTL - blk_pos : 0u);   /* the sample of out[0] */
    anchor_host = host_time;
    UNLOCK();
    sloop_render(out, frames);
}

void sloop_render(float *out, uint32_t frames)
{
    uint32_t i = 0;
    LOCK();
    while (i < frames) {
        if (blk_pos >= CTL) {
            render_block();
            blk_pos = 0;
        }
        while (blk_pos < CTL && i < frames) {
            int32_t l = blk[2u * blk_pos], r = blk[2u * blk_pos + 1u];
            float fl = (float)clamp(l, -32768, 32767) / 32768.0f, fr = (float)clamp(r, -32768, 32767) / 32768.0f;
            out[2u * i] = fl;
            out[2u * i + 1u] = fr;
            if (fabsf(fl) > peak_l) peak_l = fabsf(fl);
            if (fabsf(fr) > peak_r) peak_r = fabsf(fr);
            blk_pos++;
            i++;
        }
    }
    UNLOCK();
}

void sloop_key(int key, int down)
{
    if (key < 0 || key >= SLOOP_NKEYS) return;
    LOCK(); push(EV_KEY, (uint8_t)key, (int16_t)(down != 0)); UNLOCK();
}
void sloop_button(int button, int down)
{
    if (button < 0 || button >= SLOOP_NBUTTONS) return;
    LOCK(); push(EV_BTN, (uint8_t)button, (int16_t)(down != 0)); UNLOCK();
}
void sloop_encoder(int encoder, int steps)
{
    if (encoder < 0 || encoder >= SLOOP_NENCODERS || !steps) return;
    LOCK(); push(EV_ENC, (uint8_t)encoder, (int16_t)clamp(steps, -100, 100)); UNLOCK();
}
void sloop_input_barrier(void) { LOCK(); push(EV_BARRIER, 0, 0); UNLOCK(); }
void sloop_select_track(int track)
{
    if (track < 0 || track >= NTRK) return;
    LOCK(); push(EV_SELECT, (uint8_t)track, 0); UNLOCK();
}

/* F3 .. G5: semitone of each key in its octave from F */
static const uint8_t IS_BLACK[12] = {0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0};   /* F F# G G# A A# B C C# D D# E */
int sloop_white_key(int index)
{
    int k, n = 0;
    for (k = 0; k < SLOOP_NKEYS; k++)
        if (!IS_BLACK[k % 12] && n++ == index) return k;
    return -1;
}
int sloop_black_key(int index)
{
    int k, n = 0;
    for (k = 0; k < SLOOP_NKEYS; k++)
        if (IS_BLACK[k % 12] && n++ == index) return k;
    return -1;
}

static void set_track_param(uint32_t k, uint32_t id, int32_t value)   /* as firmware/host/preview.c preview_param */
{
    track_t *t = &trk[k];
    uint32_t i;
    t->p[id] = (int16_t)clamp(value, TP[id].min, TP[id].max);
    for (i = 0; i < t->lk_n; i++)                   /* an edit made while a lock is active changes its base too */
        if (t->lk_param[i] == id) t->lk_base[i] = t->p[id];
}
void sloop_set_track_level(int track, int value)
{
    if (track < 0 || track >= NTRK) return;
    LOCK();
    if (track == TRK_DRUM)                          /* the drum track's level is GLO > DRUMS LVL (drums.c) */
        song.g[G_DRLVL] = (int16_t)clamp(value, GP[G_DRLVL].min, GP[G_DRLVL].max);
    else
        set_track_param((uint32_t)track, P_LEVEL, value);
    UNLOCK();
}
void sloop_set_master_filter(int value) { LOCK(); song.g[G_FILT] = (int16_t)clamp(value, GP[G_FILT].min, GP[G_FILT].max); UNLOCK(); }
void sloop_set_bpm(int bpm) { LOCK(); song.g[G_BPM] = (int16_t)clamp(bpm, GP[G_BPM].min, GP[G_BPM].max); UNLOCK(); }
void sloop_set_swing(int swing) { LOCK(); song.g[G_SWING] = (int16_t)clamp(swing, GP[G_SWING].min, GP[G_SWING].max); UNLOCK(); }
void sloop_set_master_volume(int value)
{
    uint32_t k10 = (uint32_t)clamp(value, 0, 127) * 1023u / 127u;   /* as main.c reads the MASTER knob */
    LOCK(); song.master_q12 = (k10 * k10) >> 8; UNLOCK();
}

void sloop_midi_in(uint8_t status, uint8_t d1, uint8_t d2)
{
    uint32_t cin = (uint32_t)(status >> 4);
    if (status < 0x80u) return;
    if (status >= 0xF0u) cin = status == 0xF8u || status == 0xFAu || status == 0xFBu || status == 0xFCu ? 0xFu : 0;
    if (!cin) return;
    LOCK();
    if (hmi_w - hmi_r < HMQ)                        /* (fed to the firmware at the next block, render_block) */
        hmi_q[hmi_w++ % HMQ] = cin | (uint32_t)status << 8 | (uint32_t)(d1 & 0x7Fu) << 16 | (uint32_t)(d2 & 0x7Fu) << 24;
    UNLOCK();
}

int sloop_midi_out_timed(uint8_t b[3], int *n, int64_t *frames_after_anchor, uint64_t *anchor_host_time)
{
    int got = 0;
    LOCK();
    if (hmo_r != hmo_w) {
        mo_t m = hmo_q[hmo_r++ % HMO];
        uint32_t pkt = m.pkt, cin = pkt & 15u;
        b[0] = (uint8_t)(pkt >> 8); b[1] = (uint8_t)(pkt >> 16); b[2] = (uint8_t)(pkt >> 24);
        *n = cin == 0xFu || cin == 5u ? 1 : cin == 0xCu || cin == 0xDu || cin == 2u || cin == 6u ? 2 : 3;
        if (frames_after_anchor) *frames_after_anchor = (int64_t)(m.sample - anchor_sample);
        if (anchor_host_time) *anchor_host_time = anchor_host;
        got = 1;
    }
    UNLOCK();
    return got;
}

int sloop_midi_out(uint8_t b[3], int *n)
{
    int got = sloop_midi_out_timed(b, n, 0, 0);
    return got;
}

static uint32_t led_bits(const uint8_t *cols, uint32_t first, uint32_t count)
{
    uint32_t id, m = 0;
    for (id = first; id < first + count; id++) {
        uint8_t q = led_pos[id];
        if (q != 0xFF && (cols[q >> 3] >> (q & 7u)) & 1u)
            m |= 1u << (id - first);
    }
    return m;
}

void sloop_get_state(sloop_state_t *s)
{
    uint32_t b, raw;
    LOCK();
    felucca_memset(s, 0, sizeof *s);
    s->keys_lit = led_bits(fm1_led, 14u, 27u);
    s->keys_dim = led_bits(fm1_led_dim, 14u, 27u);
    raw = led_bits(fm1_led, 0u, 14u);
    for (b = 0; b < NB; b++)
        if ((raw >> panel.btn[b]) & 1u) s->buttons_lit |= 1u << b;
    s->selected_track = song.sel;
    s->playing = song.playing != 0;
    s->recording = song.rec != 0;
    s->bpm = song.g[G_BPM];
    s->swing = song.g[G_SWING];
    s->step = song.playing && TSEL->seq_abs != SEQ_NONE ? (int32_t)TSEL->seq_idx : -1;
    for (b = 0; b < NTRK; b++) s->track_level[b] = b == TRK_DRUM ? song.g[G_DRLVL] : trk[b].p[P_LEVEL];
    s->master_filter = song.g[G_FILT];
    s->octave = song.octave;
    s->peak_l = peak_l; s->peak_r = peak_r;
    peak_l = peak_r = 0;
    s->ui_frames = ui_frame_count;
    s->layer = ui.layer != LY_PLAY ? (int32_t)ui.layer : 0;
    s->layer_shown = layer_shown != 0;
    s->home = ui.home != 0;
    UNLOCK();
}

void sloop_track_name(int track, char *b)
{
    LOCK();
    if (track >= 0 && track < NTRK) trk_short_name((uint32_t)track, b);
    else b[0] = 0;
    UNLOCK();
}
void sloop_drum_name(int lane, char *b)
{
    uint32_t i;
    const char *s = lane >= 0 && lane < DRUM_LANES ? LANE_SHORT[lane] : "";
    for (i = 0; i < 15u && s[i]; i++) b[i] = s[i];
    b[i] = 0;
}
int sloop_drum_lane_picked(void) { int l; LOCK(); l = (int)pen_lane; UNLOCK(); return l; }

int sloop_screen_texts(sloop_text_t *out, int max)
{
    int n = 0;
    uint32_t i;
    LOCK();
    for (i = 0; i < txt_n && n < max; i++, n++) {
        const txt_t *t = &txt_map[i];
        uint32_t c = t->c, r = (c >> 11) * 255u / 31u, g = ((c >> 5) & 63u) * 255u / 63u, b = (c & 31u) * 255u / 31u;
        out[n].x = t->x; out[n].y = t->y; out[n].w = t->w; out[n].h = t->h;
        out[n].argb = 0xFF000000u | r << 16 | g << 8 | b;
        felucca_memcpy(out[n].text, t->s, sizeof out[n].text);
    }
    UNLOCK();
    return n;
}

uint32_t sloop_copy_screen_if_new(uint32_t *argb, uint32_t last_frame)
{
    static uint16_t raw[240 * 240];                /* (one caller at a time: the app's main thread) */
    uint32_t i, f;
    LOCK();                                        /* only the raw copy under the lock the audio thread takes */
    f = screen_frame;
    if (f != last_frame) felucca_memcpy(raw, screen, sizeof raw);
    UNLOCK();
    if (f == last_frame) return f;
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t p = swap16(raw[i]);
        uint32_t r = (p >> 11) * 255u / 31u, g = ((p >> 5) & 63u) * 255u / 63u, bl = (p & 31u) * 255u / 31u;
        argb[i] = 0xFF000000u | r << 16 | g << 8 | bl;
    }
    return f;
}
uint32_t sloop_copy_screen(uint32_t *argb) { return sloop_copy_screen_if_new(argb, 0xFFFFFFFFu); }

int sloop_flash_dirty(void) { int d; LOCK(); d = fl_writes != saved_writes; UNLOCK(); return d; }

/* Saves are serialised (a second save waits for the first), written to a unique temporary file, flushed to the disk
 * and renamed over the image: never a torn or mixed image. The image counts as saved only once the rename succeeded. */
static pthread_mutex_t save_mu = PTHREAD_MUTEX_INITIALIZER;
int sloop_save_flash(const char *path)
{
    static uint8_t copy[sizeof nor];
    char tmp[1100];
    uint32_t writes;
    int fd, rc = -1;
    if (!path || !booted) return -1;               /* (never save an image that was not booted: it holds nothing) */
    pthread_mutex_lock(&save_mu);
    LOCK();
    persist_flush_now();                            /* live sections, the song: RAM-only until now */
    felucca_memcpy(copy, nor, sizeof nor);
    writes = fl_writes;
    UNLOCK();
    snprintf(tmp, sizeof tmp, "%s.XXXXXX", path);
    fd = mkstemp(tmp);
    if (fd >= 0) {
        size_t done = 0;
        while (done < sizeof copy) {
            ssize_t w = write(fd, copy + done, sizeof copy - done);
            if (w <= 0) break;
            done += (size_t)w;
        }
        if (done == sizeof copy) {
            if (fcntl(fd, F_FULLFSYNC) == -1) fsync(fd);    /* (a disk without F_FULLFSYNC: the plain sync) */
            int closed = close(fd);
            fd = -1;
            if (closed == 0 && rename(tmp, path) == 0) rc = 0;
        }
        if (fd >= 0) close(fd);
        if (rc) remove(tmp);
    }
    if (rc == 0) { LOCK(); saved_writes = writes; UNLOCK(); }
    pthread_mutex_unlock(&save_mu);
    return rc;
}

void sloop_panic(void)
{
    uint32_t k;
    LOCK();
    transport_req = 2;
    panic_req = (1u << NTRK) - 1u;
    for (k = 0; k < NPART; k++) {
        trk[k].nheld = trk[k].arp_phys = trk[k].latched = 0;
        trk[k].arp_note = 0;
    }
    fm1_in.notes = 0;
    fm1_in.buttons = 0;
    local_btn = 0;
    ev_r = ev_w;
    UNLOCK();
}

float sloop_run_frames(uint32_t n)
{
    float pk = 0;
    static float buf[706 * 2];
    while (n--) {
        uint32_t i;
        sloop_render(buf, 706);                     /* 16 ms */
        for (i = 0; i < 706u * 2u; i++)
            if (fabsf(buf[i]) > pk) pk = fabsf(buf[i]);
    }
    return pk;
}
