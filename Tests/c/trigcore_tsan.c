/* SPDX-License-Identifier: GPL-3.0-only */
/* TRIGDECK thread-safety smoke test: one thread renders as an audio callback would, two others hammer the whole API
 * (parameters, steps, locks, scenes, patterns, performance FX, scratch, samples, capture, project save / load,
 * generative tools, AUTOPILOT, state, MIDI). Run it under ThreadSanitizer (from the repository root):
 *   cc -O1 -g -fsanitize=thread -ffast-math -fno-finite-math-only -ISources/TrigCore/include Sources/TrigCore/td_*.c \
 *      Tests/c/trigcore_tsan.c -lpthread -lm -o .build-c/trigcore_tsan && .build-c/trigcore_tsan
 * It exits 0 when the output stayed finite and bounded; ThreadSanitizer reports any data race (and exits 66). */
#include "trigdeck.h"
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static atomic_int running = 1;
static int bad_out;
static long blocks;

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
static void *audio(void *arg)
{
    (void)arg;
    float buf[2 * 512];
    uint64_t host = 1;
    unsigned sizes[5] = {512, 1, 77, 256, 333}, k = 0;
    while (running) {
        unsigned n = sizes[k++ % 5];
        td_render(buf, n, host);
        host += n;
        for (unsigned i = 0; i < 2 * n; i++)
            if (!isfinite(buf[i]) || fabsf(buf[i]) > 1.f) bad_out++;
        blocks++;
    }
    return NULL;
}
static void *ui(void *arg)
{
    unsigned s = (unsigned)(uintptr_t)arg;
    size_t psize = td_project_size();
    uint8_t *proj = malloc(psize);
    static float smp[2][48000];
    float *mine = smp[s & 1];
    for (int i = 0; i < 48000; i++) mine[i] = sinf((float)i * 0.05f) * 0.5f;
    char txt[32];
    td_param_info_t inf;
    td_state_t st;
    td_step_view_t steps[TD_STEPS];
    while (running) {
        s = s * 1103515245u + 12345u;
        int t = (int)((s >> 8) % TD_TRACKS), p = (int)((s >> 12) % TD_NPARAMS), v = (int)((s >> 16) % 128);
        int step = (int)((s >> 4) % 16);
        switch ((s >> 20) % 24) {
        case 0: td_param_set(t, p, v); break;
        case 1: td_step_toggle(t, step); break;
        case 2: td_step_lock(t, step, p, v); break;
        case 3: td_scene_set((int)(s & 1), t, p, v); break;
        case 4: td_crossfader((float)v / 127.f); break;
        case 5: td_perf((int)(s % TD_PERFS), (int)(s % 6), (int)(s % 3), (int)((s >> 3) & 1), (float)v / 127.f); break;
        case 6: td_scratch((int)(s & 1), (int)((s >> 1) & 1), ((float)v - 64.f) / 32.f); break;
        case 7: td_get_state(&st); (void)td_param_live(t, p); break;
        case 8: td_param_info(t, p, &inf); td_format_value(&inf, v, txt, sizeof txt); break;
        case 9: td_pattern_queue((int)(s % 4)); break;
        case 10: if (td_project_save(proj, psize) > 0 && (s & 7) == 0) td_project_load(proj, psize); break;
        case 11: td_sample_load(8 + (int)(s % 4), mine, 4800 + (s % 40000), 48000, "T"); break;
        case 12: td_capture((int)(s & 1), 1, 12 + (int)(s % 3)); break;
        case 13: td_trigger(t, 40 + v / 4, 100); td_release(t, 40 + v / 4); break;
        case 14: td_mute(t, (int)(s & 1)); break;
        case 15: td_set_machine(t, (int)(s % TD_MACHINES)); break;
        case 16: td_euclid(t, (int)(s % 9), 8 + (int)(s % 9), (int)(s % 4)); break;
        case 17: td_rotate(t, (int)(s % 5) - 2); break;
        case 18: td_dice(t, v % 101); break;
        case 19: td_autopilot((int)(s & 1)); break;
        case 20: td_steps_get(t, steps); break;
        case 21: { uint8_t b[3]; int n; int64_t fa; uint64_t ah; while (td_midi_out(b, &n, &fa, &ah)) {} } break;
        case 22: td_set_tempo(60.f + (float)v); td_set_swing(50 + v % 30); td_nudge(((float)v - 64.f) / 64.f); break;
        default: td_fill((int)(s & 1)); td_step_slide(t, step, (int)(s & 1)); td_step_accent(t, step, (int)(s & 2)); break;
        }
    }
    free(proj);
    return NULL;
}

int main(void)
{
    td_init();
    td_midi_config(1, 1);
    td_play(1);
    pthread_t a, u1, u2;
    pthread_create(&a, NULL, audio, NULL);
    pthread_create(&u1, NULL, ui, (void *)(uintptr_t)1);
    pthread_create(&u2, NULL, ui, (void *)(uintptr_t)2);
    double t0 = now();
    while (now() - t0 < 1.5) {
        struct timespec ts = {0, 10000000};
        nanosleep(&ts, NULL);
    }
    running = 0;
    pthread_join(a, NULL);
    pthread_join(u1, NULL);
    pthread_join(u2, NULL);
    printf("thread smoke: %ld renders while two threads called the API: %s\n", blocks,
           bad_out ? "OUTPUT OUT OF BOUNDS" : "output finite and within [-1, 1]");
    return bad_out ? 1 : 0;
}
