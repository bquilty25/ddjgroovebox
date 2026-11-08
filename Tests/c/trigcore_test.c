/* SPDX-License-Identifier: GPL-3.0-only */
/* TRIGDECK engine tests (C). The engine's sources are compiled in, so a test can look inside when the API alone cannot
 * show it (voices, the delay's return, the autopilot's overlay). Build and run from the repository root:
 *   cc -O2 -ffast-math -fno-finite-math-only -ISources/TrigCore/include -ISources/TrigCore Sources/TrigCore/td_*.c \
 *      Tests/c/trigcore_test.c -lpthread -lm -o .build-c/trigcore_test && .build-c/trigcore_test
 * Exit status: the number of failed checks. */
#include "td_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static int fails, checks;
static void check(const char *what, int ok)
{
    checks++;
    if (!ok) fails++;
    printf("  %-74s %s\n", what, ok ? "ok" : "FAIL");
}
static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
#define TEST(name) static void name(void)
#define RUN(name) do { double t0_ = now(); printf("%s\n", #name); name(); printf("  (%.2f s)\n", now() - t0_); } while (0)

/* ---- helpers */
typedef struct { int ch, note, vel; int64_t frame; } note_t;
static note_t notes[20000];
static int nnotes;
static int64_t rendered;
static float big[2 * 48000 * 12];

static void drain_midi(int64_t base)
{
    uint8_t b[3];
    int n;
    int64_t fa;
    uint64_t ah;
    while (td_midi_out(b, &n, &fa, &ah))
        if ((b[0] & 0xF0) == 0x90 && b[2] > 0 && nnotes < 20000)
            notes[nnotes++] = (note_t){b[0] & 15, b[1], b[2], base + fa};
}
static void render_frames(int64_t frames, int chunk)            /* renders, collecting note-ons on one timeline */
{
    static float buf[2 * 4096];
    while (frames > 0) {
        int k = frames > chunk ? chunk : (int)frames;
        td_render(buf, (uint32_t)k, 0);
        drain_midi(rendered);
        rendered += k;
        frames -= k;
    }
}
static void setup_empty(float bpm)                             /* an empty pattern (the default sounds), stopped */
{
    td_init();
    for (int p = 12; p < 16; p++) {                            /* P13..P16 made empty for the tests: P01's sounds */
        E.pat[p] = E.pat[0];
        memset(E.pat[p].step, 0, sizeof E.pat[p].step);
        memset(E.pat[p].lock, 0, sizeof E.pat[p].lock);
        for (int t = 0; t < TD_TRACKS; t++) E.pat[p].length[t] = 16;
        E.pat[p].bpm10 = 0;
    }
    td_pattern_queue(12);
    td_set_tempo(bpm);
    td_set_swing(50);
    td_midi_config(0, 1);
    nnotes = 0;
    rendered = 0;
}
static int count_ch(int ch)
{
    int c = 0;
    for (int i = 0; i < nnotes; i++) c += notes[i].ch == ch;
    return c;
}
static float max_jump(const float *lr, int frames, int ch)
{
    float m = 0;
    for (int i = 1; i < frames; i++) {
        float d = fabsf(lr[2 * i + ch] - lr[2 * (i - 1) + ch]);
        if (d > m) m = d;
    }
    return m;
}
static void sine_source(void)                                  /* track 1 (deck A): a held 220 Hz sine, nothing else */
{
    setup_empty(120);
    td_set_machine(0, TD_M_SYNTH);
    td_param_set(0, P_SRC + SY_SHAPE, 0);
    td_param_set(0, P_SRC + SY_DETUNE, 0);
    td_param_set(0, P_SRC + SY_SUB, 0);
    td_param_set(0, P_CUT, 127);
    td_param_set(0, P_FENV, 0);
    td_param_set(0, P_ASUS, 127);
    td_param_set(0, P_LEVEL, 100);
    for (int t = 0; t < TD_TRACKS; t++) {
        td_param_set(t, P_DLY, 0);
        td_param_set(t, P_REV, 0);
        td_param_set(t, P_CHO, 0);
    }
    td_global_set(G_COMP, 0);
    td_global_set(G_FILTER, 0);
    td_trigger(0, 57, 100);
}

/* ---- timing */
TEST(test_timing)
{
    setup_empty(120);                                          /* 6000 frames a step */
    td_step_set(0, 0, 1);
    td_step_set(0, 4, 1); td_step_micro(0, 4, 6);
    td_step_set(0, 8, 1); td_step_micro(0, 8, -12);
    td_step_set(0, 12, 1);
    td_step_set(1, 1, 1);
    td_step_set(1, 3, 1);
    td_step_set(2, 2, 1); td_step_retrig(2, 2, 3, 2);         /* 3 more hits, 1/32 apart */
    td_step_set(3, 5, 1); td_step_micro(3, 5, 3);
    td_set_swing(60);                                          /* odd steps 0.2 step late */
    td_play(1);
    render_frames(2 * 96000, 37);
    const double fps = 6000.0;
    struct { int ch; double step; } want[] = {
        {0, 0}, {0, 4 + 6 / 24.0}, {0, 8 - 12 / 24.0}, {0, 12}, {0, 16}, {0, 20 + 6 / 24.0}, {0, 24 - 0.5}, {0, 28},
        {1, 1.2}, {1, 3.2}, {1, 17.2}, {1, 19.2},
        {2, 2}, {2, 2.5}, {2, 3}, {2, 3.5}, {2, 18}, {2, 18.5}, {2, 19}, {2, 19.5},
        {3, 5 + 0.2 + 3 / 24.0}, {3, 21 + 0.2 + 3 / 24.0},
    };
    int nw = (int)(sizeof want / sizeof want[0]), ok = 1, worst = 0;
    for (int w = 0; w < nw; w++) {
        double ideal = want[w].step * fps;
        int found = 0;
        for (int i = 0; i < nnotes; i++)
            if (notes[i].ch == want[w].ch && fabs((double)notes[i].frame - ideal) <= 1.0) found = 1;
        if (!found) { ok = 0; printf("    missing ch %d at step %.3f (frame %.0f)\n", want[w].ch, want[w].step, ideal); }
    }
    for (int i = 0; i < nnotes; i++) {
        double best = 1e9;
        for (int w = 0; w < nw; w++)
            if (notes[i].ch == want[w].ch) best = fmin(best, fabs((double)notes[i].frame - want[w].step * fps));
        if (best > worst) worst = (int)best;
    }
    char msg[120];
    snprintf(msg, sizeof msg, "trigs at their ideal frames (micro, swing, retrigs), worst error %d frame(s)", worst);
    check(msg, ok && worst <= 1);
    check("note counts: 8 + 4 + 8 (retrigs) + 2", count_ch(0) == 8 && count_ch(1) == 4 && count_ch(2) == 8 && count_ch(3) == 2);
    int fade_ok = 1, last = 999;
    for (int i = 0; i < nnotes; i++)
        if (notes[i].ch == 2 && notes[i].frame < 30000) {
            if (notes[i].vel > last) fade_ok = 0;
            last = notes[i].vel;
        }
    check("retrig velocities fade (RTRG VEL -16)", fade_ok && last < 100);
    /* a tempo with a fractional step length and odd render sizes */
    setup_empty(133);
    td_step_set(5, 0, 1);
    td_step_set(5, 7, 1); td_step_micro(5, 7, -5);
    td_set_swing(67);
    td_play(1);
    render_frames(150000, 1023);
    double f2 = 720000.0 / 133.0;
    int ok2 = count_ch(5) >= 4;
    for (int i = 0; i < nnotes; i++) {
        double k = notes[i].frame / f2, best = 1e9;
        for (int bar = 0; bar < 3; bar++) {
            best = fmin(best, fabs(notes[i].frame - (bar * 16) * f2));
            best = fmin(best, fabs(notes[i].frame - (bar * 16 + 7 + (67 / 50.0 - 1) - 5 / 24.0) * f2));
        }
        (void)k;
        if (best > 1.0) ok2 = 0;
    }
    check("133 BPM, swing 67 %, micro -5: within one frame", ok2);
}

/* ---- conditions */
TEST(test_conditions)
{
    setup_empty(300);
    td_step_set(0, 0, 1); td_step_cond(0, 0, TD_COND_FILL);
    td_step_set(0, 4, 1); td_step_cond(0, 4, TD_COND_NOT_FILL);
    td_step_set(0, 8, 1); td_step_cond(0, 8, TD_COND_FIRST);
    td_step_set(0, 12, 1); td_step_cond(0, 12, TD_COND_NOT_FIRST);
    for (int s = 0; s < 16; s++) { td_step_set(1, s, 1); td_step_cond(1, s, TD_COND_P1 + 3); }   /* 50 % */
    for (int s = 0; s < 16; s++) { td_step_set(2, s, 1); td_step_cond(2, s, TD_COND_P1); }       /* 12.5 % */
    td_step_set(3, 0, 1); td_step_cond(3, 0, TD_COND_AB + 0);   /* 1:2 */
    td_step_set(3, 4, 1); td_step_cond(3, 4, TD_COND_AB + 3);   /* 2:3 */
    td_step_set(3, 8, 1); td_step_cond(3, 8, TD_COND_AB + 9);   /* 1:8 */
    td_play(1);
    const double fpb = 16 * 720000.0 / 300.0;
    render_frames((int64_t)(4 * fpb), 512);
    int fill = 0, nfill = 0, first = 0, nfirst = 0;
    for (int i = 0; i < nnotes; i++) {
        if (notes[i].ch != 0) continue;
        int s = (int)(lround(notes[i].frame / (fpb / 16)) % 16);
        fill += s == 0; nfill += s == 4; first += s == 8; nfirst += s == 12;
    }
    check("4 loops without FILL: FILL 0, NOT FILL 4, FIRST 1, NOT FIRST 3", fill == 0 && nfill == 4 && first == 1 && nfirst == 3);
    td_fill(1);
    int n0 = nnotes;
    render_frames((int64_t)(4 * fpb), 512);
    fill = nfill = 0;
    for (int i = n0; i < nnotes; i++) {
        if (notes[i].ch != 0) continue;
        int s = (int)(lround(notes[i].frame / (fpb / 16)) % 16);
        fill += s == 0; nfill += s == 4;
    }
    check("4 loops with FILL: FILL 4, NOT FILL 0", fill == 4 && nfill == 0);
    td_fill(0);
    render_frames((int64_t)(56 * fpb), 512);                   /* 64 loops in all */
    int p50 = count_ch(1), p12 = count_ch(2);
    char msg[120];
    snprintf(msg, sizeof msg, "50 %% over 1024 trigs: %d (expected 512 +- 64)", p50);
    check(msg, abs(p50 - 512) <= 64);
    snprintf(msg, sizeof msg, "12 %% over 1024 trigs: %d (expected 128 +- 42)", p12);
    check(msg, abs(p12 - 128) <= 42);
    int ab12 = 0, ab23 = 0, ab18 = 0, ab_ok = 1;
    for (int i = 0; i < nnotes; i++) {
        if (notes[i].ch != 3) continue;
        long st = lround(notes[i].frame / (fpb / 16));
        int loop = (int)(st / 16), s = (int)(st % 16);
        if (s == 0) { ab12++; if (loop % 2 != 0) ab_ok = 0; }
        if (s == 4) { ab23++; if (loop % 3 != 1) ab_ok = 0; }
        if (s == 8) { ab18++; if (loop % 8 != 0) ab_ok = 0; }
    }
    snprintf(msg, sizeof msg, "A:B over 64 loops: 1:2 %d (32), 2:3 %d (21), 1:8 %d (8), on the right loops", ab12, ab23, ab18);
    check(msg, ab_ok && ab12 == 32 && ab23 == 21 && ab18 == 8);
}

/* ---- locks */
TEST(test_locks)
{
    setup_empty(120);
    int t = 4;                                                 /* SYNTH */
    td_param_set(t, P_CUT, 100);
    td_param_set(t, P_RES, 10);
    td_param_set(t, P_LEN, 16);                                /* INF: the note rings on */
    td_step_set(t, 0, 1);
    td_step_set(t, 4, 1);
    td_step_lock(t, 4, P_CUT, 20);
    td_step_lock(t, 6, P_RES, 90);                             /* a lock-only step */
    td_step_set(t, 8, 1);
    td_step_view_t v[TD_STEPS];
    td_steps_get(t, v);
    check("step 4 has 1 lock, step 6 is lock-only", v[4].nlocks == 1 && v[4].trig && v[6].nlocks == 1 && !v[6].trig);
    int val = 0;
    check("td_step_lock_get reads the lock", td_step_lock_get(t, 4, P_CUT, &val) == 1 && val == 20);
    check("an unlocked parameter reads not locked", td_step_lock_get(t, 4, P_RES, &val) == 0);
    td_play(1);
    render_frames(2 * 6000 + 100, 64);
    check("step 0 (no locks): cutoff 100", td_param_live(t, P_CUT) == 100);
    render_frames(3 * 6000, 64);                               /* step 5 */
    check("step 4: cutoff locked to 20", td_param_live(t, P_CUT) == 20);
    render_frames(1 * 6000 + 200, 64);                         /* step 6 */
    int voices = 0;
    for (int i = 0; i < TD_VOICES; i++) voices += E.trk[t].v[i].active && E.trk[t].v[i].gated;
    check("lock-only step 6: reso 90 on the ringing note, cutoff still 20, no new trig",
          td_param_live(t, P_RES) == 90 && td_param_live(t, P_CUT) == 20 && voices == 1 && fabsf(E.trk[t].eff[P_RES] - 90.f) < 0.5f);
    render_frames(2 * 6000, 64);                               /* step 8 */
    check("step 8 (a trig without locks): cutoff 100 and reso 10 again",
          td_param_live(t, P_CUT) == 100 && td_param_live(t, P_RES) == 10);
    check("the base value never changed", td_param_get(t, P_CUT) == 100);
    /* TRIG page knobs edit a held step */
    td_step_lock(t, 4, P_NOTE, 62);
    td_step_lock(t, 4, P_COND, TD_COND_FILL);
    td_steps_get(t, v);
    check("TRIG page on a held step: NOTE and COND are the step's own", v[4].note == 62 && v[4].cond == TD_COND_FILL && v[4].nlocks == 1);
    td_step_unlock(t, 4, P_CUT);
    td_steps_get(t, v);
    check("unlock removes the lock", v[4].nlocks == 0);
    for (int i = 0; i < 10; i++) td_step_lock(t, 9, P_SRC + (i % 8), i);
    td_steps_get(t, v);
    check("at most 8 locks a step", v[9].nlocks == 8);
}

/* ---- patterns */
TEST(test_pattern_queue)
{
    setup_empty(120);
    td_pattern_queue(13);
    for (int s = 0; s < 16; s++) td_step_note(0, s, 48, 100);
    td_pattern_queue(12);
    for (int s = 0; s < 16; s++) td_step_note(0, s, 36, 100);
    td_play(1);
    render_frames(96000 + 30000, 100);                         /* into bar 2 */
    td_pattern_queue(13);
    td_state_t st;
    td_get_state(&st);
    check("queued: still pattern 13 (index 12), 14 (index 13) queued", st.pattern == 12 && st.queued_pattern == 13);
    render_frames(192000 - rendered - 32, 100);
    td_get_state(&st);
    check("32 frames before the bar line: still the old pattern", st.pattern == 12);
    render_frames(64, 64);
    td_get_state(&st);
    check("32 frames after: the new pattern, nothing queued", st.pattern == 13 && st.queued_pattern == -1);
    render_frames(30000, 100);
    int64_t first48 = -1, last36 = -1;
    for (int i = 0; i < nnotes; i++) {
        if (notes[i].note == 48 && first48 < 0) first48 = notes[i].frame;
        if (notes[i].note == 36) last36 = notes[i].frame;
    }
    char msg[120];
    snprintf(msg, sizeof msg, "the switch is on the boundary: last old trig %lld, first new %lld (192000)", (long long)last36,
             (long long)first48);
    check(msg, first48 == 192000 && last36 == 186000);
    /* the longest track sets the pattern's end */
    setup_empty(120);
    td_pattern_queue(15);
    td_step_note(0, 0, 50, 100);
    td_pattern_queue(14);
    td_set_length(0, 12);
    td_set_length(1, 20);
    td_step_note(0, 0, 40, 100);
    td_play(1);
    render_frames(6000 * 3, 100);
    td_pattern_queue(15);
    render_frames(6000 * 40, 100);
    first48 = -1;
    for (int i = 0; i < nnotes; i++)
        if (notes[i].note == 50) { first48 = notes[i].frame; break; }
    check("lengths 12 and 20: the queued pattern starts at step 20", first48 == 20 * 6000);
}

/* ---- scenes */
TEST(test_scenes)
{
    setup_empty(120);
    int t = 4;
    td_param_set(t, P_CUT, 100);
    td_scene_set(0, t, P_CUT, 20);
    td_scene_set(1, t, P_CUT, 120);
    td_scene_set(1, t, P_RES, 80);
    float xs[4] = {0.f, 0.25f, 0.5f, 1.f};
    int want[4] = {20, 60, 100, 120}, ok = 1;
    for (int i = 0; i < 4; i++) {
        td_crossfader(xs[i]);
        if (td_param_live(t, P_CUT) != want[i]) { ok = 0; printf("    x %.2f: %d\n", xs[i], td_param_live(t, P_CUT)); }
    }
    check("CUTOFF base 100, A 20, B 120: 20 / 60 / 100 / 120 at x 0 / .25 / .5 / 1", ok);
    td_crossfader(0.f);
    check("a parameter only in scene B stays at its base at x 0", td_param_live(t, P_RES) == td_param_get(t, P_RES));
    td_crossfader(0.75f);
    int r = td_param_live(t, P_RES), base = td_param_get(t, P_RES);
    check("halfway to B: half way to its value", abs(r - (base + 80) / 2) <= 1);
    int v = 0;
    check("td_scene_get", td_scene_get(1, t, P_CUT, &v) == 1 && v == 120 && td_scene_get(0, t, P_RES, &v) == 0);
    td_scene_unset(1, t, P_CUT);
    td_crossfader(1.f);
    check("unset: back to base", td_param_live(t, P_CUT) == 100);
    /* scenes act on top of locks */
    td_crossfader(0.5f);
    td_step_set(t, 0, 1);
    td_step_lock(t, 0, P_CUT, 40);
    td_play(1);
    render_frames(3000, 64);
    td_crossfader(0.25f);
    check("on top of a lock: lock 40, A 20, x .25 -> 30", td_param_live(t, P_CUT) == 30);
}

/* ---- scratch */
TEST(test_scratch)
{
    const int N = 48000 * 2;
    static float ref[2 * 48000 * 2], scr[2 * 48000 * 2];
    sine_source();
    for (int i = 0; i < N; i += 64) td_render(ref + 2 * i, 64, 0);
    sine_source();
    int T0 = 24000, T1 = 35968, T2 = 59968;                    /* (multiples of the 64-frame renders) */
    for (int i = 0; i < N; i += 64) {
        if (i == T0) td_scratch(0, 1, 1.f);
        if (i == T1) td_scratch(0, 1, 0.f);
        if (i == T2) td_scratch(0, 0, 0.f);
        td_render(scr + 2 * i, 64, 0);
    }
    float best = 1e9;
    int bd = 0;
    for (int d = 0; d < 12; d++) {
        float e = 0;
        for (int i = T0 + 2000; i < T1; i++) e = fmaxf(e, fabsf(scr[2 * i] - ref[2 * (i - d)]));
        if (e < best) { best = e; bd = d; }
    }
    char msg[120];
    snprintf(msg, sizeof msg, "touched at rate 1: the deck plays its buffer (delay %d frames, error %.5f)", bd, best);
    check(msg, best < 2e-3);
    double sr = 0, rr = 0;
    for (int i = T2 - 960; i < T2; i++) { sr += scr[2 * i] * scr[2 * i]; rr += ref[2 * i] * ref[2 * i]; }
    snprintf(msg, sizeof msg, "held still: silent while the sequence runs on (%.1f dB under live)", 10 * log10(sr / rr + 1e-30));
    check(msg, sr < rr * 0.0025);
    float e = 0;
    for (int i = T2 + 480 + 48; i < N; i++) e = fmaxf(e, fabsf(scr[2 * i] - ref[2 * i]));   /* (+48: the limiter's lookahead) */
    snprintf(msg, sizeof msg, "released: back to the live bus within 10 ms (then identical, error %.6f)", e);
    check(msg, e < 1e-4);
    float j = max_jump(scr, N, 0), jr = max_jump(ref, N, 0);
    snprintf(msg, sizeof msg, "no click: max step %.4f (live %.4f)", j, jr);
    check(msg, j < jr * 2.f + 0.005f);
    td_state_t st;
    td_get_state(&st);
    check("state: not scratching after the release", st.scratching[0] == 0);
    /* backwards and fast */
    sine_source();
    for (int i = 0; i < N / 2; i += 64) {
        if (i == T0) td_scratch(0, 1, -1.5f);
        td_render(scr + 2 * i, 64, 0);
    }
    float pk = 0;
    for (int i = T0 + 2000; i < N / 2; i++) pk = fmaxf(pk, fabsf(scr[2 * i]));
    check("rate -1.5: plays backwards, audible, bounded", pk > 0.05f && pk <= 1.f && max_jump(scr, N / 2, 0) < 0.05f);
}

/* ---- performance FX */
TEST(test_perf)
{
    static float out[2 * 48000 * 2];
    const int N = 48000 * 2, on = 30080, off = 30080 + 19200;   /* (multiples of the 128-frame renders) */
    for (int p = 0; p < TD_PERFS; p++) {
        sine_source();
        td_param_set(0, P_DLY, 40);                            /* (something for the delay to hold) */
        for (int i = 0; i < N; i += 128) {
            if (i == on) td_perf(p, 2, p == TD_PERF_DELAYTHROW || p == TD_PERF_REVERBTHROW ? TD_TARGET_DECK_A : TD_TARGET_MASTER, 1,
                                 p == TD_PERF_FILTER ? 0.15f : 0.5f);
            if (i == off) td_perf(p, 2, TD_TARGET_MASTER, 0, 0.5f);
            td_render(out + 2 * i, 128, 0);
        }
        float live = max_jump(out, on - 100, 0);
        float during = 0;
        for (int i = on + 2400; i < off - 100; i++) during = fmaxf(during, fabsf(out[2 * i] - out[2 * i - 2]));
        float edge = 0;
        for (int i = on - 600; i < on + 1500; i++) edge = fmaxf(edge, fabsf(out[2 * i] - out[2 * i - 2]));
        for (int i = off - 600; i < off + 1500; i++) edge = fmaxf(edge, fabsf(out[2 * i] - out[2 * i - 2]));
        float pk = 0;
        int finite = 1;
        for (int i = 0; i < 2 * N; i++) { pk = fmaxf(pk, fabsf(out[i])); finite &= isfinite(out[i]) != 0; }
        char msg[160];
        snprintf(msg, sizeof msg, "%-9s on / off without a click: edges %.4f (live %.4f, during %.4f)", td_perf_name(p), edge, live, during);
        check(msg, finite && pk <= 1.f && edge <= 1.5f * fmaxf(live, during) + 0.01f);
    }
    /* every insert changes the sound: the same 0.5 s with and without it differ (rms of the difference) */
    {
        static float ref[2 * 24000], fx[2 * 24000];
        sine_source();
        td_trigger(1, 60, 127);
        td_render(ref, 24000, 0);
        int all = 1;
        char bad[200] = "";
        for (int p = TD_PERF_ECHO; p < TD_PERFS; p++) {
            sine_source();
            td_trigger(1, 60, 127);
            td_perf(p, 2, TD_TARGET_MASTER, 1, 0.8f);
            td_render(fx, 24000, 0);
            double d = 0;
            for (int i = 2 * 4800; i < 2 * 24000; i++) d += (double)(fx[i] - ref[i]) * (fx[i] - ref[i]);
            double rms = sqrt(d / (2.0 * 19200));
            if (!(rms > 0.003)) { all = 0; snprintf(bad + strlen(bad), sizeof bad - strlen(bad), " %s %.4f", td_perf_name(p), rms); }
        }
        char msg[260];
        snprintf(msg, sizeof msg, "ECHO .. SWEEP each change the sound%s", bad);
        check(msg, all);
    }
    /* beyond a bar: a 4-bar REPEAT at 120 BPM loops every 384000 frames (8 s) */
    {
        static float r4[2 * 48000 * 26];                       /* (live capture pass + two looped passes: 24.1 s) */
        sine_source();
        td_set_tempo(120);
        for (int s = 0; s < 64; s += 3) td_step_note(0, s, 40 + s % 24, 100);   /* something that changes over 4 bars */
        td_play(1);
        for (int i = 0; i < 48000 * 26; i += 32) {
            if (i == 4800) td_perf(TD_PERF_REPEAT, 7, TD_TARGET_MASTER, 1, 1.f);
            td_render(r4 + 2 * i, 32, 0);
        }
        float err = 0;                                         /* the third pass equals the second */
        for (int i = 4800 + 384000 + 2000; i < 4800 + 2 * 384000 - 2000; i += 7)
            err = fmaxf(err, fabsf(r4[2 * (i + 384000)] - r4[2 * i]));
        char msg[100];
        snprintf(msg, sizeof msg, "REPEAT 4 bars loops every 384000 frames (error %.5f)", err);
        check(msg, err < 1e-3f);
    }
    td_state_t st;
    sine_source();
    td_perf(TD_PERF_REPEAT, 1, TD_TARGET_DECK_A, 1, 1.f);
    td_render_offline(4800);
    td_get_state(&st);
    check("state shows the effect while held", st.perf_active && st.perf_type == TD_PERF_REPEAT && st.perf_target == TD_TARGET_DECK_A);
    /* REPEAT repeats: the output loops with the division's length */
    sine_source();
    td_trigger(1, 60, 127);                                    /* a hat: something that changes */
    td_set_tempo(120);
    static float r[2 * 48000];
    for (int i = 0; i < 48000; i += 32) {
        if (i == 4800) td_perf(TD_PERF_REPEAT, 1, TD_TARGET_MASTER, 1, 1.f);   /* 1/16: 6000 frames */
        td_render(r + 2 * i, 32, 0);
    }
    float e = 0;
    for (int i = 4800 + 6000 + 200; i < 4800 + 6000 + 200 + 12000; i++) e = fmaxf(e, fabsf(r[2 * i] - r[2 * (i + 6000)]));
    char msg[100];
    snprintf(msg, sizeof msg, "REPEAT 1/16 loops every 6000 frames (error %.5f)", e);
    check(msg, e < 1e-3);
}

/* ---- machines */
TEST(test_machines)
{
    static float buf[2 * 7200];
    int bad = 0, silent = 0, tested = 0;
    for (int m = 0; m < TD_MACHINES; m++) {
        setup_empty(120);
        td_set_machine(0, m);
        for (int t = 1; t < TD_TRACKS; t++) td_mute(t, 1);
        int note = m >= TD_M_SYNTH ? 57 : 60;
        for (int p = 0; p < TD_NPARAMS; p++) {
            td_param_info_t inf;
            td_param_info(0, p, &inf);
            int vals[2] = {inf.min, inf.max};
            for (int k = 0; k < 2; k++) {
                td_param_set(0, p, vals[k]);
                td_trigger(0, note, 110);
                float pk = 0, tpk = 0;
                int finite = 1;
                for (int c = 0; c < 2; c++) {
                    td_render(buf, 1200, 0);
                    for (int i = 0; i < 2400; i++) { finite &= isfinite(buf[i]) != 0; pk = fmaxf(pk, fabsf(buf[i])); }
                    tpk = fmaxf(tpk, E.trk[0].peak);
                }
                td_release(0, note);
                td_render(buf, 1200, 0);
                tested++;
                int may_be_silent = (p == P_LEVEL && k == 0) || (p == P_FTYPE && k == 1) || (p == P_CUT && k == 0) ||
                                    (m == TD_M_SAMPLE && p == P_SRC + SM_SLOT && k == 1) || (p == P_VEL);
                if (!finite || pk > 1.f || tpk > 16.f) {
                    bad++;
                    printf("    %s %s=%d: finite %d peak %.3f track %.2f\n", td_machine_name(m), inf.name, vals[k], finite, pk, tpk);
                }
                if (pk < 1e-4f && !may_be_silent) {
                    silent++;
                    printf("    %s %s=%d: silent (%.2g)\n", td_machine_name(m), inf.name, vals[k], pk);
                }
            }
            td_param_set(0, p, td_pdef(m, p));
            if (m == TD_M_SAMPLE && p == P_SRC + SM_SLOT) td_param_set(0, p, 0);
        }
        /* hold the gate open for long and let it all ring out: no runaway */
        for (int v = 0; v < 6; v++) td_trigger(0, 40 + v * 5, 127);
        float pk = 0;
        for (int c = 0; c < 20; c++) {
            td_render(buf, 2400, 0);
            for (int i = 0; i < 4800; i++) pk = fmaxf(pk, fabsf(buf[i]));
        }
        if (pk > 1.f) bad++;
    }
    char msg[120];
    snprintf(msg, sizeof msg, "%d renders: every parameter at min and max finite and bounded", tested);
    check(msg, bad == 0);
    check("... and never silent (except LEVEL 0, HP at 20 kHz, cutoff 20 Hz, an empty slot)", silent == 0);
    /* each machine's sound: it sounds, it ends */
    for (int m = 0; m < TD_MACHINES; m++) {
        setup_empty(120);
        td_set_machine(0, m);
        td_trigger(0, m >= TD_M_SYNTH ? 57 : 60, 100);
        float pk = td_render_offline(4800);
        td_param_set(0, P_AREL, 40);                           /* (a 0.3 s release, not the drums' 10 s) */
        td_release(0, m >= TD_M_SYNTH ? 57 : 60);
        td_render_offline(48000 * (m == TD_M_KICK || m == TD_M_TOM ? 3 : 1));
        int active = 0;
        for (int i = 0; i < TD_VOICES; i++) active += E.trk[0].v[i].active;
        snprintf(msg, sizeof msg, "%-6s sounds (peak %.2f) and its voice ends", td_machine_name(m), pk);
        check(msg, pk > 0.01f && active == 0);
    }
}

/* ---- bounds under abuse */
TEST(test_fuzz)
{
    static float buf[2 * 1024];
    td_init();
    td_play(1);
    uint32_t rng = 12345;
    int finite = 1;
    float pk = 0;
    for (int it = 0; it < 500; it++) {
        int t = (int)(td_xs(&rng) % 8), p = (int)(td_xs(&rng) % TD_NPARAMS);
        td_param_info_t inf;
        td_param_info(t, p, &inf);
        td_param_set(t, p, inf.min + (int)(td_xs(&rng) % (uint32_t)(inf.max - inf.min + 1)));
        if (it % 50 == 0) td_set_machine(t, (int)(td_xs(&rng) % TD_MACHINES));
        if (it % 7 == 0) td_crossfader(td_rand01(&rng));
        if (it % 13 == 0) td_perf((int)(td_xs(&rng) % TD_PERFS), (int)(td_xs(&rng) % 6), (int)(td_xs(&rng) % 3), (int)(td_xs(&rng) & 1), td_rand01(&rng));
        if (it % 17 == 0) td_scratch((int)(td_xs(&rng) & 1), (int)(td_xs(&rng) & 1), td_noise(&rng) * 3.f);
        if (it % 19 == 0) td_set_tempo(30.f + td_rand01(&rng) * 270.f);
        if (it % 23 == 0) td_global_set((int)(td_xs(&rng) % 8), (int)(td_xs(&rng) % 128) - (it & 64));
        if (it % 5 == 0) td_trigger(t, (int)(td_xs(&rng) % 128), 1 + (int)(td_xs(&rng) % 127));
        td_render(buf, 1024, 0);
        for (int i = 0; i < 2048; i++) { finite &= isfinite(buf[i]) != 0; pk = fmaxf(pk, fabsf(buf[i])); }
    }
    td_global_set(G_DFB, 127);                                 /* self-oscillating tape delay */
    for (int i = 0; i < 200; i++) {
        td_render(buf, 1024, 0);
        for (int k = 0; k < 2048; k++) { finite &= isfinite(buf[k]) != 0; pk = fmaxf(pk, fabsf(buf[k])); }
    }
    char msg[100];
    snprintf(msg, sizeof msg, "500 random edits, 13 s: finite, within [-1, 1] (peak %.3f)", pk);
    check(msg, finite && pk <= 1.f);
}

/* ---- the project */
TEST(test_project)
{
    td_init();
    size_t n = td_project_size();
    uint8_t *a = malloc(n), *b = malloc(n), *c = malloc(n);
    int na = td_project_save(a, n);
    check("saves (a version 1 TDPJ blob)", na > 1000 && !memcmp(a, "TDPJ", 4));
    td_set_tempo(140);
    td_param_set(3, P_CUT, 11);
    td_step_set(2, 9, 1);
    td_scene_set(1, 2, P_REV, 3);
    td_set_length(6, 33);
    td_mute(5, 1);
    td_global_set(G_COMP, 99);
    check("loads it back", td_project_load(a, (size_t)na) == 0);
    int nb = td_project_save(b, n);
    check("round trip: the same bytes", nb == na && !memcmp(a, b, (size_t)na));
    td_state_t st;
    td_get_state(&st);
    check("tempo, parameters, steps and mutes restored", fabsf(st.bpm - 122.f) < 0.05f && td_param_get(3, P_CUT) != 11 && !st.muted[5]);
    check("the patterns' own tempos too (version 2)", E.pat[12].bpm10 == 1740 && E.pat[0].bpm10 == 1220);
    td_set_tempo(99);
    int nc = td_project_save(c, n);
    memcpy(b, c, (size_t)nc);
    b[nc / 2] ^= 0x10;
    check("a damaged blob is refused", td_project_load(b, (size_t)nc) == -1);
    memcpy(b, c, (size_t)nc);
    b[0] = 'X';
    check("a foreign blob is refused", td_project_load(b, (size_t)nc) == -1);
    memcpy(b, c, (size_t)nc);
    b[4] = 9;
    check("another version is refused", td_project_load(b, (size_t)nc) == -1);
    check("a truncated blob is refused", td_project_load(c, (size_t)nc - 5) == -1);
    check("too small a buffer: save says -1", td_project_save(b, 100) == -1);
    int nd = td_project_save(b, n);
    check("after the refusals nothing changed", nd == nc && !memcmp(b, c, (size_t)nc));
    free(a); free(b); free(c);
}

/* ---- determinism and render sizes */
static uint64_t fnv(const float *x, size_t n)
{
    uint64_t h = 1469598103934665603ull;
    const uint8_t *p = (const uint8_t *)x;
    for (size_t i = 0; i < n * sizeof(float); i++) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}
TEST(test_determinism)
{
    const int N = 48000 * 4;
    td_init(); td_play(1); td_autopilot(1);
    for (int i = 0; i < N; i += 512) td_render(big + 2 * i, 512, 0);
    uint64_t h1 = fnv(big, 2 * (size_t)N);
    td_init(); td_play(1); td_autopilot(1);
    for (int i = 0; i < N; i += 512) td_render(big + 2 * i, 512, 0);
    uint64_t h2 = fnv(big, 2 * (size_t)N);
    check("same input, same output (hash of 4 s)", h1 == h2);
    td_init(); td_play(1);
    td_render(big, (uint32_t)N, 0);
    uint64_t h3 = fnv(big, 2 * (size_t)N);
    td_init(); td_play(1);
    static const int sizes[4] = {1, 7, 256, 1024};
    for (int i = 0, k = 0; i < N; k++) {
        int s = sizes[k % 4];
        if (s > N - i) s = N - i;
        td_render(big + 2 * i, (uint32_t)s, 0);
        i += s;
    }
    uint64_t h4 = fnv(big, 2 * (size_t)N);
    check("renders of 1, 7, 256, 1024 frames == one render of 4 s", h3 == h4);
}

/* ---- CPU */
TEST(test_cpu)
{
    const int N = 48000 * 10;
    static float b[2 * 512];
    td_init(); td_play(1);
    td_render_offline(48000);
    double t0 = now();
    for (int i = 0; i < N; i += 512) td_render(b, 512, 0);
    double ns = (now() - t0) * 1e9 / N;
    printf("    default project (8 tracks, FX): %.0f ns a frame = %.2f %% of one core\n", ns, ns / (1e9 / 48000) * 100);
    check("default project under 5 % of a core", ns / (1e9 / 48000) < 0.05);
    td_init(); td_play(1); td_autopilot(1);
    td_perf(TD_PERF_STUTTER, 1, TD_TARGET_MASTER, 1, 0.5f);
    td_perf(TD_PERF_DELAYTHROW, 2, TD_TARGET_DECK_B, 1, 0.8f);
    td_scratch(0, 1, -0.7f);
    for (int t = 4; t < 7; t++) td_trigger(t, 57, 100);       /* chords held over the pattern */
    td_trigger(5, 64, 100);
    t0 = now();
    for (int i = 0; i < N; i += 512) td_render(b, 512, 0);
    double ns2 = (now() - t0) * 1e9 / N;
    printf("    stress (autopilot, held chords, scratch, stutter, throw): %.0f ns a frame = %.2f %%\n", ns2, ns2 / (1e9 / 48000) * 100);
    check("stress under 10 % of a core", ns2 / (1e9 / 48000) < 0.10);
}

/* ---- capture, slices */
TEST(test_capture_slices)
{
    td_init();
    td_play(1);
    double fpb = 16 * 720000.0 / 124.0;
    td_render_offline((uint32_t)(fpb * 2.5));
    check("capture deck A, 1 bar, into slot 9", td_capture(0, 1, 9) == 0);
    int64_t want = E.bar_frame[0] - E.bar_frame[1];
    check("named DECK A 1, one bar long, bar-aligned", !strcmp(td_sample_name(9), "DECK A 1") && td_slots[9].len == (uint32_t)want);
    const float *d = td_pool + td_slots[9].off;
    float pk = 0;
    for (uint32_t i = 0; i < td_slots[9].len; i++) pk = fmaxf(pk, fabsf(d[i]));
    float ring0 = 0.5f * (td_ring[0][(uint32_t)((E.bar_frame[1] + 1000) & TD_RING_MASK) * 2] + td_ring[0][(uint32_t)((E.bar_frame[1] + 1000) & TD_RING_MASK) * 2 + 1]);
    check("the deck's audio (mono), faded at the ends", pk > 0.05f && d[0] == 0.f && fabsf(d[1000] - ring0) < 1e-6f);
    check("2 and 4 bars", td_capture(1, 2, 10) == 0 && td_capture(0, 4, 11) == 0 && !strcmp(td_sample_name(10), "DECK B 1"));
    check("a bad deck or slot is refused", td_capture(2, 1, 9) == -1 && td_capture(0, 1, 16) == -1);
    /* slices: 4 tones of a test sample, picked by the note */
    static float s[4 * 4800];
    for (int i = 0; i < 4 * 4800; i++) s[i] = 0.5f * sinf(6.2831853f * (float)(i / 4800 + 1) * 200.f * (float)i / 48000.f);
    check("load a 4-slice test sample", td_sample_load(12, s, 4 * 4800, 48000, "SLICES") == 0);
    td_play(0);
    td_set_machine(7, TD_M_SAMPLE);
    td_set_sample(7, 12);
    td_param_set(7, P_SRC + SM_SLICES, 1);                    /* 4 */
    int notes4[5] = {60, 61, 63, 59, 64}, want_sl[5] = {0, 1, 3, 3, 0}, ok = 1;
    for (int k = 0; k < 5; k++) {
        for (int i = 0; i < TD_VOICES; i++) E.trk[7].v[i].active = 0;
        td_trigger(7, notes4[k], 100);
        td_voice_t *v = NULL;
        for (int i = 0; i < TD_VOICES; i++) if (E.trk[7].v[i].active) v = &E.trk[7].v[i];
        if (!v || v->m.smp.a != (uint32_t)want_sl[k] * 4800u || v->m.smp.b != (uint32_t)(want_sl[k] + 1) * 4800u ||
            fabsf(v->m.smp.rate - 1.f) > 1e-4f) ok = 0;
    }
    check("SLICES 4: notes 60, 61, 63, 59, 64 play slices 1, 2, 4, 4, 1 at the slot's pitch", ok);
    check("a sample that does not fit the pool is refused", td_sample_load(13, big, TD_POOL_FRAMES, 48000, "BIG") == -1);
    char buf[32];
    td_param_info_t inf;
    td_param_info(7, P_SRC + SM_SLOT, &inf);
    td_format_value(&inf, 12, buf, sizeof buf);
    check("the SLOT knob reads the sample's name", !strcmp(buf, "SLICES"));
}

/* ---- generative */
TEST(test_generative)
{
    setup_empty(120);
    td_step_set(0, 3, 1);
    td_step_lock(0, 3, P_CUT, 33);
    td_step_note(0, 3, 70, 90);
    td_euclid(0, 3, 8, 0);
    td_step_view_t v[TD_STEPS];
    td_steps_get(0, v);
    int on[16], ok = 1;
    for (int s = 0; s < 16; s++) on[s] = v[s].trig;
    int want[16] = {1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 0};
    for (int s = 0; s < 16; s++) ok &= on[s] == want[s];
    check("E(3,8) over 16 steps: x..x..x.x..x..x.", ok);
    check("a step that stays on keeps its note and lock", v[3].note == 70 && v[3].nlocks == 1);
    td_euclid(0, 3, 8, 1);
    td_steps_get(0, v);
    check("rotation 1: from step 2", v[1].trig && v[4].trig && v[7].trig && !v[0].trig);
    setup_empty(120);
    td_step_note(4, 0, 50, 90);
    td_step_micro(4, 0, 5);
    td_step_cond(4, 0, TD_COND_FILL);
    td_step_lock(4, 0, P_RES, 77);
    td_step_note(4, 15, 40, 60);
    td_rotate(4, 1);
    td_steps_get(4, v);
    check("rotate +1: the trig, note, micro, cond and lock move together; 16 wraps to 1",
          v[1].trig && v[1].note == 50 && v[1].micro == 5 && v[1].cond == TD_COND_FILL && v[1].nlocks == 1 && v[0].note == 40 && !v[15].trig);
    td_rotate(4, -1);
    td_steps_get(4, v);
    check("rotate -1: back", v[0].note == 50 && v[15].note == 40);
    /* dice */
    setup_empty(120);
    for (int s = 0; s < 16; s += 2) td_step_note(4, s, 57, 100);
    td_step_view_t before[TD_STEPS], after[TD_STEPS], again[TD_STEPS];
    td_steps_get(4, before);
    td_dice(4, 0);
    td_steps_get(4, after);
    check("dice 0 %: nothing changes", !memcmp(before, after, sizeof before));
    td_dice(4, 50);
    td_steps_get(4, after);
    int changed = 0, inscale = 1, near = 1;
    for (int s = 0; s < 16; s++) {
        changed += memcmp(&before[s], &after[s], sizeof before[s]) != 0;
        if (after[s].trig) {
            int pc = ((after[s].note - 9) % 12 + 12) % 12;
            inscale &= td_scales[0][pc];
            int from = before[s].trig ? before[s].note : td_param_get(4, P_NOTE);   /* (a new trig starts at NOTE) */
            near &= abs(after[s].note - from) <= 12;
        }
    }
    char msg[100];
    snprintf(msg, sizeof msg, "dice 50 %%: %d of 16 steps changed, notes in A minor within an octave", changed);
    check(msg, changed >= 4 && changed <= 8 && inscale && near);
    setup_empty(120);
    for (int s = 0; s < 16; s += 2) td_step_note(4, s, 57, 100);
    td_dice(4, 0);
    td_dice(4, 50);
    td_steps_get(4, again);
    check("dice is deterministic (the same calls after td_init, the same steps)", !memcmp(after, again, sizeof again));
}

/* ---- acid, chords, ducking */
TEST(test_acid_chord_duck)
{
    setup_empty(120);
    int t = 4;
    td_step_note(t, 0, 45, 100);
    td_step_slide(t, 0, 1);
    td_step_note(t, 1, 57, 100);
    td_step_note(t, 4, 45, 100);
    td_step_accent(t, 4, 1);
    td_step_view_t v[TD_STEPS];
    td_steps_get(t, v);
    check("slide and accent show on the steps", v[0].slide && !v[1].slide && v[4].accent);
    td_play(1);
    render_frames(6000 + 3000, 32);
    int voices = 0, ok = 0;
    for (int i = 0; i < TD_VOICES; i++)
        if (E.trk[t].v[i].active && E.trk[t].v[i].gated) {
            voices++;
            ok = E.trk[t].v[i].ptarget == 57.f && E.trk[t].v[i].pitch > 45.f && E.trk[t].v[i].ast != ENV_ATT;
        }
    check("SLIDE: the held note glides to the next one, no new attack", voices == 1 && ok);
    render_frames(3 * 6000, 32);
    float accv = 0, plain = 0;
    for (int i = 0; i < TD_VOICES; i++) if (E.trk[t].v[i].active && E.trk[t].v[i].gated) accv = E.trk[t].v[i].vel;
    td_step_accent(t, 4, 0);
    render_frames(16 * 6000, 32);
    for (int i = 0; i < TD_VOICES; i++) if (E.trk[t].v[i].active && E.trk[t].v[i].gated) plain = E.trk[t].v[i].vel;
    check("ACCENT: louder", accv > plain * 1.3f);
    /* chord memory */
    setup_empty(120);
    td_param_set(5, P_SRC + SY_CHORD, 1);                      /* MIN7 */
    td_trigger(5, 57, 100);
    int got[4] = {0}, n = 0;
    for (int i = 0; i < TD_VOICES; i++)
        if (E.trk[5].v[i].active && n < 4) got[n++] = E.trk[5].v[i].note;
    check("CHORD MIN7 on A: A C E G (4 voices)", n == 4 && got[0] == 57 && got[1] == 60 && got[2] == 64 && got[3] == 67);
    td_param_set(6, P_SRC + FM_CHORD, 5);                      /* FIFTH on FM */
    td_trigger(6, 60, 100);
    n = 0;
    for (int i = 0; i < TD_VOICES; i++) n += E.trk[6].v[i].active;
    check("CHORD FIFTH on FM: 3 voices", n == 3);
    /* ducking: the sub under the kick */
    static float b[2 * 4800];
    sine_source();
    td_set_machine(4, TD_M_SYNTH);
    td_param_set(4, P_SRC + SY_SHAPE, 0);
    td_param_set(4, P_ASUS, 127);
    td_param_set(4, P_DUCK, 127);
    td_trigger(4, 45, 100);
    td_release(0, 57);
    td_deck_level(0, 0.f);                                     /* (the kick itself out of the way) */
    td_render_offline(24000);
    td_render(b, 4800, 0);
    float before_pk = 0;
    for (int i = 0; i < 9600; i++) before_pk = fmaxf(before_pk, fabsf(b[i]));
    td_set_machine(0, TD_M_KICK);
    td_trigger(0, 60, 120);
    td_render(b, 1200, 0);                                     /* 25 ms after the kick */
    float dip = 0;
    for (int i = 1200; i < 2400; i++) dip = fmaxf(dip, fabsf(b[i]));
    td_render_offline(48000);
    td_render(b, 2400, 0);
    float back = 0;
    for (int i = 0; i < 4800; i++) back = fmaxf(back, fabsf(b[i]));
    char msg[120];
    snprintf(msg, sizeof msg, "DUCK 127: %.1f dB under the kick, back within a beat (%.1f dB)", 20 * log10(dip / before_pk),
             20 * log10(back / before_pk));
    check(msg, dip < before_pk * 0.35f && back > before_pk * 0.9f);
}

/* ---- tape delay and DUB HOLD */
TEST(test_dubhold)
{
    static float tap[48000 * 6], b[2 * 512];
    setup_empty(120);
    td_param_set(2, P_DLY, 127);
    td_global_set(G_DTIME, 7);                                 /* 1/4: 24000 frames */
    td_global_set(G_DFB, 70);
    E.dbg_what = 1;
    E.dbg_buf = tap;
    E.dbg_len = 48000 * 6;
    E.dbg_n = 0;
    td_trigger(2, 60, 127);                                    /* a clap into the delay */
    td_render_offline(48000);
    td_perf(TD_PERF_DUBHOLD, 0, TD_TARGET_MASTER, 1, 1.f);
    for (int i = 0; i < 6; i++) td_trigger(2, 60, 127), td_render_offline(4800);   /* more claps: not let in */
    td_render_offline(48000 * 2 - 6 * 4800);
    uint32_t h0 = 48000 + 2400, T = (uint32_t)E.dly.hold_time;
    float e = 0, pk = 0, en = 0;
    for (uint32_t i = h0 + T; i < E.dbg_n; i++) {
        e = fmaxf(e, fabsf(tap[i] - tap[i - T]));
        pk = fmaxf(pk, fabsf(tap[i]));
        en += tap[i] * tap[i];
    }
    char msg[140];
    snprintf(msg, sizeof msg, "DUB HOLD: the delay loops every %u frames (the delay time) exactly (error %.6f)", T, e);
    check(msg, T > 23900 && T < 24100 && e < 1e-5f && en > 1e-3f);
    check("... bounded, no build-up", pk < 1.f);
    td_perf(TD_PERF_DUBHOLD, 0, TD_TARGET_MASTER, 0, 1.f);
    td_render_offline(48000 * 3);
    float late = 0;
    for (uint32_t i = E.dbg_n - 24000; i < E.dbg_n; i++) late = fmaxf(late, fabsf(tap[i]));
    snprintf(msg, sizeof msg, "released: the loop fades away (%.1f dB after 3 s)", 20 * log10(late / pk + 1e-9));
    check(msg, late < pk * 0.5f);
    E.dbg_what = 0;
    E.dbg_buf = NULL;
    /* tape feedback beyond 100 %: self-oscillates, stays bounded */
    setup_empty(120);
    td_param_set(2, P_DLY, 127);
    td_global_set(G_DFB, 127);
    td_trigger(2, 60, 127);
    float mx = 0;
    for (int i = 0; i < 48000 * 6; i += 512) {
        td_render(b, 512, 0);
        for (int k = 0; k < 1024; k++) mx = fmaxf(mx, fabsf(b[k]));
    }
    check("DLY FDBK 127 for 6 s: bounded", mx <= 1.f);
    td_param_info_t inf;
    char s[32];
    td_global_info(G_DTIME, &inf);
    int has316 = 0, has14d = 0;
    for (int v = inf.min; v <= inf.max; v++) {
        td_format_value(&inf, v, s, sizeof s);
        has316 |= !strcmp(s, "3/16");
        has14d |= !strcmp(s, "1/4D");
    }
    check("DLY TIME has 3/16 and 1/4D", has316 && has14d);
}

/* ---- AUTOPILOT */
TEST(test_autopilot)
{
    td_init();
    td_set_tempo(300);                                         /* (fast: 64 bars in 51 s of audio) */
    for (int t = 0; t < TD_TRACKS; t++) td_mute(t, 1);       /* (silent: quick to render; the arranger runs all the same) */
    td_autopilot(1);
    td_play(1);
    const int64_t fpb = 16 * 720000 / 300;                     /* 38400 frames a bar */
    uint8_t prev[TD_TRACKS];
    memcpy(prev, E.ap.mute, sizeof prev);
    int prev_tr = E.ap.transpose, changes = 0, off_bar = 0, tr_changes = 0, tr_off_phrase = 0, user_ok = 1, range_ok = 1;
    int kinds_seen = 0;
    int64_t pos = 0;
    for (int bar = 1; bar <= 64; bar++) {
        /* up to the bar line: nothing may change; then the 32 frames from the bar line, where changes belong */
        td_render_offline((uint32_t)(bar * fpb - pos));
        pos = bar * fpb;
        if (memcmp(prev, E.ap.mute, sizeof prev) || E.ap.transpose != prev_tr) off_bar++;
        td_render_offline(32);
        pos += 32;
        if (memcmp(prev, E.ap.mute, sizeof prev)) changes++;
        if (E.ap.transpose != prev_tr) {
            tr_changes++;
            if (bar % 8 != 0) tr_off_phrase++;
        }
        memcpy(prev, E.ap.mute, sizeof prev);
        prev_tr = E.ap.transpose;
        kinds_seen |= 1 << E.ap.kind;
        td_state_t st;
        td_get_state(&st);
        if (!st.muted[7]) user_ok = 0;
        for (int t = 0; t < TD_TRACKS; t++) {
            if (fabsf(E.ap.walk[t][0]) > 16.01f || fabsf(E.ap.walk[t][1]) > 20.01f || fabsf(E.ap.walk[t][2]) > 18.01f) range_ok = 0;
            if (E.ap.cut_ramp[t] < -28.1f || E.ap.cut_ramp[t] > 18.1f) range_ok = 0;
        }
        if (E.ap.dfb > 32.1f || abs(E.ap.transpose) > 6) range_ok = 0;
    }
    char msg[160];
    snprintf(msg, sizeof msg, "64 bars: %d mute changes, all on bar lines (%d off); %d chord moves, on phrase starts (%d off)",
             changes, off_bar, tr_changes, tr_off_phrase);
    check(msg, changes >= 2 && off_bar == 0 && tr_off_phrase == 0);
    check("several kinds of phrase (groove, drop, breakdown, build)", __builtin_popcount((unsigned)kinds_seen) >= 2);
    check("the user's mute held all along", user_ok);
    check("walks, ramps, feedback and transposition stay in their bounds", range_ok);
    td_state_t st;
    td_get_state(&st);
    check("state: autopilot on, phrase bar 1..8", st.autopilot == 1 && st.phrase_bar >= 1 && st.phrase_bar <= 8);
    td_autopilot(0);
    td_render_offline(48000 * 5);
    int clear = 1;
    for (int t = 0; t < TD_TRACKS; t++) {
        if (E.ap.mute[t] || fabsf(E.ap.walk[t][0]) > 0.05f || fabsf(E.ap.cut_ramp[t]) > 0.05f) clear = 0;
    }
    td_get_state(&st);
    check("off: its mutes, offsets and transposition gone, the user's mute stays",
          clear && E.ap.transpose == 0 && st.muted[7] && fabsf(E.ap.dfb) < 0.05f);
    /* the same seed, the same arrangement */
    uint8_t m1[32], m2[32];
    for (int run = 0; run < 2; run++) {
        td_init();
        td_set_tempo(300);
        for (int t = 0; t < TD_TRACKS; t++) td_mute(t, 1);
        td_autopilot(1);
        td_play(1);
        for (int bar = 0; bar < 32; bar++) {
            td_render_offline((uint32_t)fpb + (bar == 0 ? 32u : 0u));
            uint8_t m = 0;
            for (int t = 0; t < 8; t++) m |= (uint8_t)(E.ap.mute[t] << t);
            (run ? m2 : m1)[bar] = m ^ (uint8_t)(E.ap.transpose & 15);
        }
    }
    check("deterministic: two runs, the same 32 bars", !memcmp(m1, m2, sizeof m1));
}

/* ---- descriptions, formatting, MIDI clock */
TEST(test_info)
{
    td_init();
    int ok = 1, named = 1;
    for (int m = 0; m < TD_MACHINES; m++) {
        td_set_machine(0, m);
        for (int p = 0; p < TD_NPARAMS; p++) {
            td_param_info_t a;
            if (!td_param_info(0, p, &a) || a.min > a.max || a.def < a.min || a.def > a.max) ok = 0;
            if (!a.name[0] || !a.short_name[0]) named = 0;
        }
    }
    check("48 parameters on every machine, defaults in range, all named", ok && named);
    /* a name reads the same way everywhere */
    int same = 1;
    for (int m = 0; m < TD_MACHINES; m++)
        for (int p = 0; p < TD_NPARAMS; p++)
            for (int m2 = 0; m2 < TD_MACHINES; m2++)
                for (int p2 = 0; p2 < TD_NPARAMS; p2++) {
                    const td_pdesc_t *a = td_pdesc(m, p), *b = td_pdesc(m2, p2);
                    if (!strcmp(a->name, b->name) && a->kind != b->kind) same = 0;
                }
    check("a parameter name always has one unit", same);
    char s[32];
    td_param_info_t inf;
    td_init();
    td_param_info(4, P_CUT, &inf);
    td_format_value(&inf, 127, s, sizeof s); int c1 = !strcmp(s, "20 kHz");
    td_format_value(&inf, 0, s, sizeof s); int c2 = !strcmp(s, "20 Hz");
    td_format_value(&inf, 77, s, sizeof s); int c3 = strstr(s, "kHz") != NULL;
    check("CUTOFF reads 20 Hz .. 20 kHz", c1 && c2 && c3);
    td_param_info(4, P_NOTE, &inf);
    td_format_value(&inf, 60, s, sizeof s);
    check("NOTE 60 reads C3", !strcmp(s, "C3"));
    td_param_info(4, P_PAN, &inf);
    td_format_value(&inf, -12, s, sizeof s);
    check("PAN -12 reads L12", !strcmp(s, "L12"));
    td_param_info(4, P_SRC + SY_SHAPE, &inf);
    td_format_value(&inf, 85, s, sizeof s);
    check("SHAPE 85 reads SAW", !strcmp(s, "SAW"));
    td_param_info(4, P_RRATE, &inf);
    td_format_value(&inf, 0, s, sizeof s);
    check("RTRG RATE 0 reads 1/16", !strcmp(s, "1/16"));
    td_param_info(4, P_SRC + SY_TUNE, &inf);
    td_format_value(&inf, -12, s, sizeof s);
    check("TUNE -12 reads -12", !strcmp(s, "-12"));
    td_param_info(4, P_REV, &inf);
    td_format_value(&inf, 127, s, sizeof s);
    check("REVERB 127 reads 100 %", !strcmp(s, "100 %"));
    td_param_info(4, P_ADEC, &inf);
    td_format_value(&inf, 60, s, sizeof s);
    check("a time reads in ms or s", strstr(s, "ms") || strstr(s, " s"));
    check("names", !strcmp(td_machine_name(TD_M_FM), "FM") && !strcmp(td_page_name(TD_PAGE_MASTER), "MASTER") &&
                       !strcmp(td_cond_name(TD_COND_P1 + 3), "50%") && !strcmp(td_cond_name(TD_COND_AB), "1:2") &&
                       !strcmp(td_perf_name(TD_PERF_DUBHOLD), "DUBHOLD") && !strcmp(td_scale_name(0), "MINOR"));
    check("the built-in samples", !strcmp(td_sample_name(0), "VINYL") && !strcmp(td_sample_name(4), "CRACKLE") && !td_sample_name(5)[0]);
    check("scale notes for the pads (A minor from C3: C D E F G)", td_scale_note(60, 0) == 60 && td_scale_note(60, 1) == 62 &&
                                                                     td_scale_note(61, 0) == 62 && td_scale_note(60, 4) == 67);
    /* MIDI clock: 24 a beat, start and stop, stamped on the frame */
    td_init();
    td_set_tempo(120);
    td_play(1);
    uint8_t b[3];
    int n;
    int64_t fa;
    uint64_t ah;
    int clocks = 0, starts = 0, worst = 0;
    int64_t base = 0;
    static float buf[2 * 333];
    for (int i = 0; i < 48000 * 2 / 333; i++) {
        td_render(buf, 333, 777);
        while (td_midi_out(b, &n, &fa, &ah)) {
            if (b[0] == 0xFA) starts++;
            if (b[0] == 0xF8) {
                int64_t ideal = (int64_t)clocks * 1000;            /* 24 ppq at 120 BPM: 1000 frames */
                int e = (int)llabs(base + fa - ideal);
                if (e > worst) worst = e;
                clocks++;
            }
            if (ah != 777) worst = 9999;
        }
        base += 333;
    }
    td_play(0);
    td_render(buf, 64, 0);
    int stop = 0;
    while (td_midi_out(b, &n, &fa, &ah)) stop |= b[0] == 0xFC;
    char msg[100];
    snprintf(msg, sizeof msg, "MIDI clock: START, %d pulses within %d frame(s), STOP", clocks, worst);
    check(msg, starts == 1 && clocks >= 94 && worst <= 1 && stop);
}

/* ---- the default project sounds */
TEST(test_default_project)
{
    td_init();
    td_state_t st;
    td_get_state(&st);
    int four = 1, lengths = 1;
    for (int s = 0; s < 16; s++) four &= E.pat[0].step[0][s].trig == (s % 4 == 0 || s >= 14);   /* (14, 15: FILL) */
    for (int t = 0; t < TD_TRACKS; t++) lengths &= st.length[t] == (t >= 4 && t <= 6 ? 64 : 16);
    check("opens on P01, deep house at 122 BPM: four on the floor; bass, chords, melody over 4 bars",
          st.pattern == 0 && fabsf(st.bpm - 122.f) < 0.01f && four && lengths && E.pat[0].step[0][14].cond == TD_COND_FILL);
    int chordTypes = 0;                                        /* the stabs lock the progression's chord types */
    for (int b = 0; b < 4; b++)
        for (int k = 0; k < E.pat[0].step[5][b * 16 + 2].nlocks; k++)
            if (E.pat[0].lock[5][b * 16 + 2][k].param == P_SRC + SY_CHORD) chordTypes |= 1 << E.pat[0].lock[5][b * 16 + 2][k].value;
    check("P01's chords: minor 9, major 7, minor 7 locked per bar", chordTypes == ((1 << 3) | (1 << 2) | (1 << 1)));
    int fotf = 0;                                              /* house, techno and P10..P12 garage: four on the floor */
    for (int p = 0; p < 12; p++) {
        int k = 1;
        for (int s = 0; s < 16; s += 4) k &= E.pat[p].step[0][s].trig;
        fotf += k;
    }
    check("four on the floor: the house and techno patterns (but dub's own) and three of the garage ones", fotf >= 10);
    static const int tempos[16] = {122, 126, 124, 122, 130, 132, 122, 124, 132, 132, 132, 138, 174, 174, 165, 172};
    int tok = 1;
    for (int p = 0; p < 16; p++) tok &= E.pat[p].bpm10 == tempos[p] * 10;
    check("each pattern has its tempo (house ~124, techno ~130, garage 132-138, drum & bass 165-174)", tok);
    int dnb = 1;                                               /* the two-step: kick on 1 and the "and" of 3, snare 2 and 4 */
    for (int p = 12; p < 16; p++) dnb &= E.pat[p].step[2][4].trig && E.pat[p].step[2][12].trig && E.pat[p].step[0][0].trig;
    check("drum & bass P13..P16: snare on 2 and 4 over a kick on 1, track 3 a snare", dnb && E.pat[13].machine[2] == TD_M_SNARE);
    check("P09 2-step garage: the skipping kick (1, the 'and' of 3), shuffled hats",
          E.pat[8].step[0][0].trig && E.pat[8].step[0][10].trig && !E.pat[8].step[0][4].trig && E.pat[8].step[3][1].micro > 0);
    /* the tempo follows the pattern: queue P13 (174) while playing; it switches at the boundary and brings its tempo */
    td_play(1);
    td_pattern_queue(12);
    td_render_offline((uint32_t)(48000 * 60 / 122.0 * 4 * 4 + 4800));
    td_get_state(&st);
    check("switching to P13 brings 174 BPM", st.pattern == 12 && fabsf(st.bpm - 174.f) < 0.01f);
    td_set_tempo(170);
    check("the tempo slider edits the pattern's own tempo", E.pat[12].bpm10 == 1700);
    td_play(0);
    const int N = (int)(48000 * 60 / 124.0 * 4 * 8);
    static float b[2 * 1024];
    for (int p = 0; p < 16; p++) {                             /* each of the sixteen, 8 bars at 124 (or so) */
        td_init();
        td_pattern_queue(p);
        td_play(1);
        double e = 0;
        float pk = 0;
        int finite = 1;
        for (int i = 0; i < N; i += 1024) {
            td_render(b, 1024, 0);
            for (int k = 0; k < 2048; k++) { e += b[k] * b[k]; pk = fmaxf(pk, fabsf(b[k])); finite &= isfinite(b[k]) != 0; }
        }
        double rms = 10 * log10(e / (2.0 * N));
        char msg[100];
        snprintf(msg, sizeof msg, "P%02d, 8 bars: rms %.1f dBFS, peak %.2f, finite", p + 1, rms, pk);
        check(msg, finite && pk <= 1.f && rms > -24 && rms < -9);
    }
    int all = 1;
    for (int t = 0; t < TD_TRACKS; t++) {
        td_init();
        for (int u = 0; u < TD_TRACKS; u++) td_mute(u, u != t);
        td_play(1);
        float p = td_render_offline((uint32_t)(N / 2));          /* four bars each (the vocal is on 3:4) */
        if (p < 0.005f) { all = 0; printf("    track %d silent\n", t + 1); }
    }
    check("every track of P01 plays", all);
}

/* ---- sound packs */
TEST(test_packs)
{
    check("26 packs, named", td_pack_count() == 26 && !strcmp(td_pack_name(0), "808") && !strcmp(td_pack_name(2), "KEYS")
          && !strcmp(td_pack_name(15), "LIQUID") && !strcmp(td_pack_name(25), "ELECTRO"));
    for (int k = 0; k < td_pack_count(); k++) {
        td_init();                                             /* P01: the house beat */
        td_step_t before = E.pat[0].step[0][0];
        int16_t trigNote = E.pat[0].param[0][P_NOTE];
        td_pack_load(k);
        int map = E.pat[0].machine[0] == TD_M_KICK && E.pat[0].machine[1] == TD_M_HAT && E.pat[0].machine[3] == TD_M_HAT;
        int kept = !memcmp(&E.pat[0].step[0][0], &before, sizeof before) && E.pat[0].param[0][P_NOTE] == trigNote;
        td_play(1);
        const int N = 48000 * 4;
        static float b[2 * 1024];
        double e = 0;
        float pk = 0;
        int finite = 1;
        for (int i = 0; i < N; i += 1024) {
            td_render(b, 1024, 0);
            for (int s = 0; s < 2048; s++) { e += b[s] * b[s]; pk = fmaxf(pk, fabsf(b[s])); finite &= isfinite(b[s]) != 0; }
        }
        double rms = 10 * log10(e / (2.0 * N));
        int silent = 0;
        for (int t = 0; t < TD_TRACKS; t++) {                  /* every track of the house beat sounds with this kit */
            td_init();
            td_pack_load(k);
            for (int u = 0; u < TD_TRACKS; u++) td_mute(u, u != t);
            td_play(1);
            if (td_render_offline((uint32_t)(48000 * 60 / 124.0 * 4 * 4)) < 0.003f) silent |= 1 << t;   /* (4 bars: the vocal is on 3:4) */
        }
        char msg[160];
        snprintf(msg, sizeof msg, "%-7s kick / hats on 1, 2, 4, steps kept, 4 s: rms %.1f dBFS, peak %.2f, silent tracks %02x",
                 td_pack_name(k), rms, pk, silent);
        check(msg, map && kept && finite && pk <= 1.f && rms > -26 && rms < -8 && !silent);
    }
    td_init();
    int16_t cut = E.pat[0].param[4][P_CUT];
    uint8_t m6 = E.pat[0].machine[5];
    td_pack_load(2);                                           /* KEYS: track 6 becomes the FM piano */
    int changed = E.pat[0].machine[5] == TD_M_FM;
    check("revert: the kit before the load comes back", changed && td_pack_revert() == 0 && E.pat[0].machine[5] == m6
          && E.pat[0].param[4][P_CUT] == cut);
    check("revert again: back to the pack", td_pack_revert() == 0 && E.pat[0].machine[5] == TD_M_FM);
}

/* ---- live recording, tap tempo, nudge */
TEST(test_live)
{
    setup_empty(120);
    td_record(1);
    td_play(1);
    render_frames(5 * 6000 - 1500, 64);                        /* a quarter step before step 6 (index 5) */
    td_trigger(4, 50, 90);                                     /* played early: quantised to the nearest step */
    render_frames(3000 + 2 * 6000, 64);                        /* (past step 6 and on to step 8) */
    td_trigger(4, 52, 70);                                     /* a little after step 8: index 7 */
    td_record(0);
    td_trigger(4, 55, 70);                                     /* not recording: plays, writes nothing */
    td_step_view_t v[TD_STEPS];
    td_steps_get(4, v);
    int others = 0;
    for (int s = 0; s < 16; s++) others += v[s].trig && s != 5 && s != 7;
    check("live notes recorded on the nearest steps with their note and velocity",
          v[5].trig && v[5].note == 50 && v[5].velocity == 90 && v[7].trig && v[7].note == 52 && others == 0);
    int64_t n0 = nnotes;
    render_frames(16 * 6000, 64);
    int heard5 = 0;
    for (int i = (int)n0; i < nnotes; i++) heard5 += notes[i].note == 50 && llabs(notes[i].frame - (16 + 5) * 6000) <= 1;
    check("... and the sequencer plays them on the next loop (no double trigger before)", heard5 == 1);
    /* tap tempo: four taps 0.25 s apart */
    td_init();
    double t_first = 0, t_last = 0;
    for (int i = 0; i < 4; i++) {
        t_last = now();
        if (!i) t_first = t_last;
        td_tap_tempo();
        struct timespec ts = {0, 250000000};
        if (i < 3) nanosleep(&ts, NULL);
    }
    double want_bpm = 60.0 / ((t_last - t_first) / 3.0);       /* (the intervals as actually slept) */
    td_state_t st;
    td_get_state(&st);
    char msg[80];
    snprintf(msg, sizeof msg, "tap tempo: taps about 0.25 s apart give %.1f BPM (%.1f)", st.bpm, want_bpm);
    check(msg, fabs(st.bpm - want_bpm) < 1.0);
    /* nudge: a temporary bend */
    setup_empty(120);
    td_step_set(0, 0, 1);
    td_play(1);
    td_nudge(1.f);
    render_frames(96000, 64);
    td_nudge(0.f);
    td_get_state(&st);
    check("nudge +1 runs 8 % fast while held, the tempo stays", st.bar == 2 && st.step[0] >= 1 && fabsf(st.bpm - 120.f) < 0.01f);
}

int main(void)
{
    double t0 = now();
    RUN(test_timing);
    RUN(test_conditions);
    RUN(test_locks);
    RUN(test_pattern_queue);
    RUN(test_scenes);
    RUN(test_scratch);
    RUN(test_perf);
    RUN(test_machines);
    RUN(test_fuzz);
    RUN(test_project);
    RUN(test_determinism);
    RUN(test_cpu);
    RUN(test_capture_slices);
    RUN(test_generative);
    RUN(test_acid_chord_duck);
    RUN(test_dubhold);
    RUN(test_autopilot);
    RUN(test_info);
    RUN(test_default_project);
    RUN(test_packs);
    RUN(test_live);
    printf("\n%d checks, %d failed (%.1f s)\n", checks, fails, now() - t0);
    return fails;
}
