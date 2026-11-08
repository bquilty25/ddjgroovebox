/* SPDX-License-Identifier: GPL-3.0-only */
/* Engine regressions found by review (docs/REVIEW.md E1, E4, E5, E6, E8), with SloopCore's internals in reach (the
 * core included as source, as upstream's host tests include the firmware):
 *   E4  clock out: every MIDI clock pulse stamped within one block (0.73 ms) of its ideal time
 *   E5  saves at once from two threads: every one succeeds, the file is a whole image
 *   E6  a damaged flash file is kept aside as .damaged, not overwritten
 *   E8  a burst of 300 MIDI notes in one go: none lost
 * (E1, the calibration hang, is in Tests/SloopCoreTests: it needs a thread for the audio.) */
#include "../../Sources/SloopCore/sloop_core.c"
#include <sys/stat.h>

static int fails;
static void check(const char *what, int ok) { printf("%-70s %s\n", what, ok ? "ok" : "FAIL"); if (!ok) fails++; }

static const char *flash_path;
static void *saver(void *p) { (void)p; int bad = 0; for (int i = 0; i < 40; i++) bad += sloop_save_flash(flash_path) != 0; return (void *)(intptr_t)bad; }

int main(int argc, char **argv)
{
    static float buf[2 * 256];
    char path[512], aside[600];
    const char *dir = argc > 1 ? argv[1] : ".";
    snprintf(path, sizeof path, "%s/engine-flash.bin", dir);
    snprintf(aside, sizeof aside, "%s.damaged", path);
    flash_path = path;
    remove(aside);

    /* E6: a short (damaged) file */
    {
        FILE *f = fopen(path, "wb");
        fwrite("garbage", 1, 7, f);
        fclose(f);
    }
    check("E6: a damaged flash file: sloop_init says so (1)", sloop_init(path) == 1);
    {
        struct stat st;
        check("E6: ... and keeps it aside as .damaged", stat(aside, &st) == 0 && st.st_size == 7);
    }

    /* E4: CLK OUT on, 120 BPM, play: the 0xF8 pulses' stamps */
    song.g[G_CLKOUT] = 1;
    song.g[G_BPM] = 120;
    sloop_run_frames(10);
    sloop_button(SLOOP_B_PLAY, 1); sloop_button(SLOOP_B_PLAY, 0);
    {
        uint64_t host = 1000000;                    /* (any start; frames are what is compared) */
        int64_t first = -1, prev = -1, n = 0, worst = 0;
        int started = 0;
        for (int call = 0; call < 2000; call++) {   /* ~11.6 s at 256 frames a call */
            uint8_t b[3]; int len; int64_t off; uint64_t ah;
            sloop_render_at(buf, 256, host);
            while (sloop_midi_out_timed(b, &len, &off, &ah)) {
                if (b[0] == 0xFA) started = 1;      /* (the clock runs stopped too: measured from START) */
                if (b[0] != 0xF8 || !started) continue;
                int64_t abs_frame = (int64_t)call * 256 + off;   /* the pulse's frame on one timeline */
                if (first < 0) first = abs_frame;
                else {
                    double ideal = first + (double)n * FS * 60.0 / 120.0 / 24.0;
                    int64_t err = (int64_t)(abs_frame - ideal);
                    if (err < 0) err = -err;
                    if (err > worst) worst = err;
                }
                prev = abs_frame;
                n++;
            }
            host += 256;
        }
        (void)prev;
        printf("    %lld clock pulses, worst error %lld frames\n", (long long)n, (long long)worst);
        check("E4: clock pulses out while playing (24 a beat)", n > 500);
        check("E4: every pulse stamped within one block (32 frames) of its ideal time", worst <= 32);
    }
    sloop_button(SLOOP_B_PLAY, 1); sloop_button(SLOOP_B_PLAY, 0);
    sloop_run_frames(30);
    song.g[G_CLKOUT] = 0;

    /* E8: a burst of 300 note on / off into the firmware at once: all reach it (counted at the firmware's ring) */
    {
        uint32_t before = mi_w;
        for (int i = 0; i < 150; i++) { sloop_midi_in(0x90, 60, 100); sloop_midi_in(0x80, 60, 0); }
        sloop_run_frames(60);
        check("E8: a 300-message burst: every message reaches the firmware", mi_w - before == 300u);
    }

    /* E5: saves from two threads at once */
    {
        pthread_t a, b;
        void *ra, *rb;
        struct stat st;
        sloop_set_bpm(133);                          /* (something to save) */
        sloop_run_frames(400);
        pthread_create(&a, 0, saver, 0);
        pthread_create(&b, 0, saver, 0);
        pthread_join(a, &ra);
        pthread_join(b, &rb);
        check("E5: 80 saves from two threads at once: none fails", (intptr_t)ra == 0 && (intptr_t)rb == 0);
        check("E5: the file is a whole 1 MiB image", stat(path, &st) == 0 && st.st_size == 0x100000);
        check("E5: saved: not dirty", sloop_flash_dirty() == 0);
    }
    printf("%s\n", fails ? "FAILED" : "PASS");
    return fails != 0;
}
