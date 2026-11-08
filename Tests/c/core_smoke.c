/* SPDX-License-Identifier: GPL-3.0-only */
/* Smoke test of SloopCore through its public API only: boot, screen, a key sounds, transport, buttons light,
 * absolute controls, flash save / reload. Writes the screens to OUT_DIR/*.ppm for review. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sloop_core.h"

static int fails;
static void check(const char *what, int ok) { printf("%-64s %s\n", what, ok ? "ok" : "FAIL"); if (!ok) fails++; }
static uint32_t scr[240 * 240];
static const char *outdir = ".";
static void shot(const char *name)
{
    char p[512];
    FILE *f;
    unsigned i;
    sloop_copy_screen(scr);
    snprintf(p, sizeof p, "%s/%s.ppm", outdir, name);
    f = fopen(p, "wb");
    if (!f) return;
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240 * 240; i++) { unsigned char c[3] = {scr[i] >> 16, scr[i] >> 8, scr[i]}; fwrite(c, 1, 3, f); }
    fclose(f);
}
static unsigned lit_pixels(void)
{
    unsigned i, n = 0;
    sloop_copy_screen(scr);
    for (i = 0; i < 240 * 240; i++) n += (scr[i] & 0xFFFFFF) != 0;
    return n;
}

int main(int argc, char **argv)
{
    sloop_state_t s;
    char flash[512];
    float pk;
    if (argc > 1) outdir = argv[1];
    snprintf(flash, sizeof flash, "%s/flash.bin", outdir);
    remove(flash);

    check("boot", sloop_init(flash) == 0);
    sloop_run_frames(30);
    check("the screen draws (home page)", lit_pixels() > 500);
    shot("boot");

    pk = sloop_run_frames(20);
    check("silent when idle", pk < 0.01f);

    sloop_key(sloop_white_key(4), 1);              /* C4 on track 1 */
    pk = sloop_run_frames(20);
    check("a white key sounds", pk > 0.02f);
    sloop_get_state(&s);
    check("the key's LED lights while held", (s.keys_lit >> sloop_white_key(4)) & 1u);
    sloop_key(sloop_white_key(4), 0);
    sloop_run_frames(200);
    pk = sloop_run_frames(10);
    check("released: silent again", pk < 0.01f);

    /* a tap sent at once (press + release together) still plays */
    sloop_key(sloop_white_key(6), 1);
    sloop_key(sloop_white_key(6), 0);
    pk = sloop_run_frames(10);
    check("a press and release sent together still sound", pk > 0.02f);
    sloop_run_frames(200);

    /* the drum track: ALGORITHM selects tracks */
    sloop_encoder(SLOOP_E_ALGORITHM, 3);            /* several detents in one pass: one track (the firmware's rule) */
    sloop_run_frames(5);
    sloop_get_state(&s);
    check("ALGORITHM: 3 detents in one pass move one track", s.selected_track == 1);
    sloop_encoder(SLOOP_E_ALGORITHM, 1); sloop_input_barrier();
    sloop_encoder(SLOOP_E_ALGORITHM, 1);
    sloop_run_frames(5);
    sloop_get_state(&s);
    check("ALGORITHM: two more single detents select the drum track", s.selected_track == 3);
    shot("drums");
    sloop_key(sloop_white_key(0), 1);              /* F3: the kick */
    pk = sloop_run_frames(10);
    sloop_key(sloop_white_key(0), 0);
    check("a drum key sounds (kick)", pk > 0.05f);
    sloop_run_frames(100);

    /* PLAY starts the transport */
    sloop_button(SLOOP_B_PLAY, 1); sloop_button(SLOOP_B_PLAY, 0);
    sloop_run_frames(20);
    sloop_get_state(&s);
    check("PLAY starts the transport", s.playing == 1);
    {
        int on = 0, off = 0;
        for (int i = 0; i < 60; i++) {
            sloop_run_frames(1);
            sloop_get_state(&s);
            if ((s.buttons_lit >> SLOOP_B_PLAY) & 1u) on++; else off++;
        }
        check("PLAY's LED blinks on the beat while playing", on > 0 && off > 0);
    }
    /* record a kick on the drum track: REC, then hit while recording */
    sloop_button(SLOOP_B_PLAY, 1); sloop_button(SLOOP_B_PLAY, 0);
    sloop_run_frames(20);
    sloop_get_state(&s);
    check("PLAY again stops", s.playing == 0);

    /* SEQ held + white keys: steps (drum track: the sound played last, the kick) */
    sloop_button(SLOOP_B_SEQ, 1);
    sloop_input_barrier();
    for (int st = 0; st < 16; st += 4) { sloop_key(sloop_white_key(st), 1); sloop_key(sloop_white_key(st), 0); }
    sloop_run_frames(10);
    shot("seq-layer");
    sloop_button(SLOOP_B_SEQ, 0);
    sloop_run_frames(10);
    sloop_button(SLOOP_B_PLAY, 1); sloop_button(SLOOP_B_PLAY, 0);
    pk = sloop_run_frames(125);                     /* 2 s at 90 BPM: 3 beats */
    check("four-on-the-floor written with SEQ + keys plays", pk > 0.05f);
    sloop_get_state(&s);
    check("the step position moves while playing", s.step >= 0);
    shot("playing");

    /* absolute controls */
    sloop_set_bpm(128);
    sloop_set_track_level(3, 0);
    sloop_run_frames(30);
    sloop_get_state(&s);
    check("BPM set directly", s.bpm == 128);
    check("drum level set directly", s.track_level[3] == 0);
    pk = sloop_run_frames(60);
    check("drum level 0: silent", pk < 0.01f);
    sloop_set_track_level(3, 110);
    sloop_set_master_filter(-40);
    pk = sloop_run_frames(60);
    check("level back up, master filter low-pass: still sounds", pk > 0.02f);
    sloop_set_master_filter(0);
    sloop_button(SLOOP_B_PLAY, 1); sloop_button(SLOOP_B_PLAY, 0);
    sloop_run_frames(20);

    /* layer pages open: HOME tapped from a page, then a page of a layer button */
    sloop_button(SLOOP_B_ENV, 1); sloop_button(SLOOP_B_ENV, 0);
    sloop_run_frames(10);
    shot("env-page");
    sloop_button(SLOOP_B_HOME, 1); sloop_button(SLOOP_B_HOME, 0);
    sloop_run_frames(10);

    /* the project survives: autosave into flash, saved to the file */
    sloop_run_frames(400);                          /* autosave waits for a quiet moment */
    check("flash save", sloop_save_flash(flash) == 0);
    {
        FILE *f = fopen(flash, "rb");
        long n = 0;
        if (f) { fseek(f, 0, SEEK_END); n = ftell(f); fclose(f); }
        check("flash image is 1 MiB", n == 0x100000);
    }
    printf("%s\n", fails ? "FAILED" : "PASS");
    return fails != 0;
}
