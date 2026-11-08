/* SPDX-License-Identifier: GPL-3.0-only */
/* Renders TRIGDECK's default project to WAV files, to listen to:
 *   trigdeck_default.wav    16 bars of the default project as it starts (crossfader centred)
 *   trigdeck_perform.wav    16 bars with performance: scene morphs, throws, a scratch, REPEAT, STUTTER, FILTER,
 *                           DUB HOLD, TAPE STOP and a fill
 *   trigdeck_autopilot.wav  48 bars with AUTOPILOT on (six phrases: drops, breakdowns, builds, chord moves)
 * Build and run (from the repository root):
 *   cc -O2 -ffast-math -fno-finite-math-only -ISources/TrigCore/include Sources/TrigCore/td_*.c Tests/c/trigcore_demo.c \
 *      -lpthread -lm -o .build-c/trigcore_demo && .build-c/trigcore_demo <directory>
 * (argv[1]: a directory, or a path ending in .wav for the default render; the others go next to it.) */
#include "trigdeck.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put16(FILE *f, int v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }
static void put32(FILE *f, uint32_t v) { put16(f, (int)(v & 0xFFFF)); put16(f, (int)(v >> 16)); }
static int write_wav(const char *path, const float *lr, uint32_t frames)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    uint32_t data = frames * 4u;
    fwrite("RIFF", 1, 4, f); put32(f, 36 + data); fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put16(f, 1); put16(f, 2); put32(f, 48000); put32(f, 48000 * 4); put16(f, 4); put16(f, 16);
    fwrite("data", 1, 4, f); put32(f, data);
    uint32_t rng = 1;
    for (uint32_t i = 0; i < frames * 2u; i++) {
        rng = rng * 1664525u + 1013904223u;
        float d = ((float)(rng >> 9) / 8388608.f - 0.5f + (float)((rng * 69069u) >> 9) / 8388608.f - 0.5f) / 32768.f;
        float x = lr[i] + d;
        int v = (int)lrintf((x > 1.f ? 1.f : (x < -1.f ? -1.f : x)) * 32767.f);
        put16(f, v);
    }
    fclose(f);
    return 0;
}

static float *buf;
static uint32_t pos;
static double fpb;                                             /* frames per bar */
static void run_to(double bar)                                 /* render up to a bar position (1-based bars, fractional) */
{
    uint32_t end = (uint32_t)((bar - 1.0) * fpb);
    while (pos < end) {
        uint32_t k = end - pos > 256 ? 256 : end - pos;
        td_render(buf + 2u * pos, k, 0);
        pos += k;
    }
}
static void stats(const char *name, const float *b, uint32_t frames)
{
    double s = 0;
    float pk = 0;
    for (uint32_t i = 0; i < 2u * frames; i++) {
        s += (double)b[i] * b[i];
        if (fabsf(b[i]) > pk) pk = fabsf(b[i]);
    }
    printf("%-24s %6.1f s  peak %.3f (%.1f dBFS)  rms %.1f dBFS\n", name, frames / 48000.0, pk, 20 * log10(pk + 1e-12),
           10 * log10(s / (2.0 * frames) + 1e-20));
}

int main(int argc, char **argv)
{
    char dir[1024] = ".", path[1200];
    if (argc > 1) {
        snprintf(dir, sizeof dir, "%s", argv[1]);
        size_t L = strlen(dir);
        if (L > 4 && !strcmp(dir + L - 4, ".wav")) {
            char *sl = strrchr(dir, '/');
            if (sl) *sl = 0; else strcpy(dir, ".");
        }
    }
    const double bpm = 122.0;
    fpb = 48000.0 * 60.0 / bpm * 4.0;
    uint32_t total = (uint32_t)(48.0 * fpb) + 48000;
    buf = calloc((size_t)total * 2u, sizeof(float));

    /* 1: the default project */
    td_init();
    td_play(1);
    pos = 0;
    run_to(17.0);
    td_play(0);
    uint32_t tail = pos + 96000;
    while (pos < tail) { td_render(buf + 2u * pos, 256, 0); pos += 256; }
    snprintf(path, sizeof path, "%s/trigdeck_default.wav", dir);
    write_wav(path, buf, pos);
    stats("trigdeck_default.wav", buf, pos);

    /* 2: a performance */
    td_init();
    td_play(1);
    pos = 0;
    run_to(5.0);
    for (int i = 0; i <= 64; i++) { td_crossfader(0.5f - 0.5f * i / 64.f); run_to(5.0 + i / 64.0); }   /* to scene A */
    run_to(6.5);
    td_perf(TD_PERF_REVERBTHROW, 2, TD_TARGET_DECK_A, 1, 0.8f);
    run_to(6.75);
    td_perf(TD_PERF_REVERBTHROW, 2, TD_TARGET_DECK_A, 0, 0.8f);
    for (int i = 0; i <= 32; i++) { td_crossfader(0.5f * i / 32.f); run_to(6.75 + 0.25 * i / 32.0); }   /* back */
    run_to(7.5);
    td_perf(TD_PERF_DELAYTHROW, 2, TD_TARGET_DECK_B, 1, 0.7f);
    run_to(7.625);
    td_perf(TD_PERF_DELAYTHROW, 2, TD_TARGET_DECK_B, 0, 0.7f);
    run_to(8.0);
    for (int i = 0; i < 8; i++) {                              /* baby scratches on deck A, the beat runs on */
        td_scratch(0, 1, i & 1 ? -1.6f : 1.6f);
        run_to(8.0 + (i + 1) / 8.0 - 0.02);
        td_scratch(0, 1, 0.f);
        run_to(8.0 + (i + 1) / 8.0);
    }
    td_scratch(0, 0, 1.f);
    run_to(10.0);
    for (int i = 0; i <= 64; i++) { td_crossfader(0.5f + 0.5f * i / 64.f); run_to(10.0 + i / 64.0); }  /* to scene B */
    run_to(11.5);
    for (int i = 0; i <= 32; i++) { td_crossfader(1.f - 0.5f * i / 32.f); run_to(11.5 + 0.5 * i / 32.0); }
    td_perf(TD_PERF_REPEAT, 2, TD_TARGET_MASTER, 1, 1.f);
    run_to(12.25);
    td_perf(TD_PERF_REPEAT, 2, TD_TARGET_MASTER, 0, 1.f);
    run_to(12.5);
    td_perf(TD_PERF_STUTTER, 1, TD_TARGET_DECK_A, 1, 0.6f);
    run_to(13.0);
    td_perf(TD_PERF_STUTTER, 1, TD_TARGET_DECK_A, 0, 0.6f);
    for (int i = 0; i <= 64; i++) {                            /* a filter sweep down and back */
        float a = 0.5f - 0.42f * sinf(3.14159f * i / 64.f);
        td_perf(TD_PERF_FILTER, 3, TD_TARGET_MASTER, 1, a);
        run_to(13.0 + i / 64.0);
    }
    td_perf(TD_PERF_FILTER, 3, TD_TARGET_MASTER, 0, 0.5f);
    td_perf(TD_PERF_DUBHOLD, 0, TD_TARGET_MASTER, 1, 1.f);
    run_to(15.0);
    td_perf(TD_PERF_DUBHOLD, 0, TD_TARGET_MASTER, 0, 1.f);
    run_to(15.5);
    td_perf(TD_PERF_TAPESTOP, 3, TD_TARGET_MASTER, 1, 1.f);
    run_to(16.0);
    td_perf(TD_PERF_TAPESTOP, 3, TD_TARGET_MASTER, 0, 1.f);
    td_fill(1);
    run_to(17.0);
    td_fill(0);
    td_play(0);
    tail = pos + 96000;
    while (pos < tail) { td_render(buf + 2u * pos, 256, 0); pos += 256; }
    snprintf(path, sizeof path, "%s/trigdeck_perform.wav", dir);
    write_wav(path, buf, pos);
    stats("trigdeck_perform.wav", buf, pos);

    /* 3: AUTOPILOT, six phrases */
    td_init();
    td_autopilot(1);
    td_play(1);
    pos = 0;
    run_to(49.0);
    td_play(0);
    snprintf(path, sizeof path, "%s/trigdeck_autopilot.wav", dir);
    write_wav(path, buf, pos);
    stats("trigdeck_autopilot.wav", buf, pos);
    free(buf);
    return 0;
}
