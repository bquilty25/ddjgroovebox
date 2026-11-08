/* SPDX-License-Identifier: GPL-3.0-only */
/* TRIGDECK engine internals, shared by the Sources/TrigCore C files (not part of the public API).
 *
 * Layout of the engine:
 *   td_params.c   the parameter tables (pages, machines, ranges, defaults) and value formatting
 *   td_dsp.c      tables and the shared effects: reverb (FDN), tape delay, chorus, compressor, lookahead limiter
 *   td_voice.c    the machines (KICK SNARE HAT CLAP TOM SYNTH FM SAMPLE), the per-voice filter and envelopes
 *   td_seq.c      the sequencer: step decisions, events, conditions, locks, retrigs, pattern switching, LFO,
 *                 the generative tools (Euclid, rotate, dice) and the AUTOPILOT arranger
 *   td_engine.c   td_init / td_render, the track chain, deck buses, scratch, performance FX, master, the API
 *   td_samples.c  the sample pool, the built-in samples and deck capture (resampling)
 *   td_project.c  project save / load (versioned, CRC-checked)
 *   td_default.c  the default project (dub techno, polymetric)
 *
 * Threading: one pthread mutex (td_lock / td_unlock). Every public function takes it; td_render takes it once per
 * internal block of TD_BLK frames, so a UI call waits at most one block. Internal functions (x_*, static) assume
 * the lock is held and never call the public API. */
#pragma once
#include "trigdeck.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

#define TD_SR 48000.0f
#define TD_BLK 32                         /* internal block (frames): control rate, the FIFO granule */
#define TD_VOICES 12                      /* voice slots per track (drums use 2: the new hit and the fading one) */
#define TD_MAXNOTES 8                     /* sounding (not fading) voices per melodic track (chords x live notes) */
#define TD_LPS 8                          /* parameter locks per step */
#define TD_SCENE_MAX 128                  /* entries per scene */
#define TD_POOL_FRAMES (60u * 48000u)     /* sample pool: 60 s mono at 48 kHz */
#define TD_RING (1u << 20)                /* deck scratch / capture ring: 21.8 s stereo (4 bars down to 44 BPM) */
#define TD_RING_MASK (TD_RING - 1u)
#define TD_DLY (1u << 18)                 /* delay lines (master tape delay, throw delay): 5.4 s */
#define TD_DLY_MASK (TD_DLY - 1u)
#define TD_PERF_BUF (16u * 48000u + 2048u) /* REPEAT / STUTTER / TAPE STOP .. capture (16 s: 4 bars down to 60 BPM) */
#define TD_PERF_DIVS 8                    /* beat FX divisions: 1/32 .. 1 bar, 2 bars, 4 bars */
#define TD_MIDI_Q 2048
#define TD_EVQ 128
#define TD_WIDTH_BUF 1024

/* ---- track parameter indices (page * 8 + slot) */
enum {
    P_NOTE = 0, P_VEL, P_LEN, P_RTRG, P_RRATE, P_RVEL, P_COND, P_MICRO,
    P_SRC = 8,
    P_CUT = 16, P_RES, P_FTYPE, P_FENV, P_FATK, P_FDEC, P_KEY, P_FDRV,
    P_AATK = 24, P_AHOLD, P_ADEC, P_ASUS, P_AREL, P_PAN, P_LEVEL, P_ODRV,
    P_DLY = 32, P_REV, P_CHO, P_BITS, P_SRR, P_LOWCUT, P_DUCK, P_WIDTH,
    P_LSPD = 40, P_LMUL, P_LWAVE, P_LDEST, P_LDEPTH, P_LFADE, P_LMODE, P_LPHASE
};
/* SRC slots per machine */
enum { KK_TUNE, KK_DECAY, KK_SWEEP, KK_STIME, KK_CLICK, KK_DRIVE, KK_TONE, KK_HOLD };
enum { SN_TUNE, SN_DECAY, SN_NOISE, SN_NDECAY, SN_SNAP, SN_BODY, SN_TONE, SN_DRIVE };
enum { HH_TUNE, HH_DECAY, HH_OPEN, HH_COLOR, HH_BRIGHT, HH_RESO, HH_SPREAD, HH_DRIVE };
enum { CP_TUNE, CP_DECAY, CP_SPREAD, CP_BURSTS, CP_TONE, CP_ROOM, CP_RAND, CP_DRIVE };
enum { TM_TUNE, TM_DECAY, TM_SWEEP, TM_STIME, TM_FM, TM_NOISE, TM_TONE, TM_DRIVE };
enum { SY_TUNE, SY_SHAPE, SY_DETUNE, SY_INTVL, SY_SUB, SY_NOISE, SY_CHORD, SY_GLIDE };
enum { FM_TUNE, FM_RATIO, FM_INDEX, FM_FDBK, FM_ENVDEC, FM_ENVAMT, FM_FINE, FM_CHORD };
enum { SM_TUNE, SM_SLOT, SM_START, SM_END, SM_SLICES, SM_REV, SM_LOOP, SM_GAIN };
/* MASTER page */
enum { G_DTIME, G_DFB, G_DTONE, G_RSIZE, G_RDAMP, G_COMP, G_FILTER, G_VOLUME };

/* ---- value kinds: how an integer value maps to a unit (shared by the DSP and td_format_value) */
enum {
    KD_INT, KD_BIP, KD_PCT, KD_SEMI, KD_NOTE, KD_CUT, KD_LOWCUT, KD_TDEC, KD_TATK, KD_THOLD, KD_TREL, KD_TSWEEP,
    KD_TGLIDE, KD_ONOFF, KD_LEN, KD_RRATE, KD_COND, KD_FTYPE, KD_SHAPE, KD_RATIO, KD_SLOT, KD_CHORD, KD_SLICES,
    KD_BITS, KD_SRR, KD_PAN, KD_MULT, KD_LWAVE, KD_LDEST, KD_LMODE, KD_DIV, KD_DJF, KD_DB, KD_KINDS
};
typedef struct {
    const char *name, *sname;
    int16_t min, max;
    uint8_t kind, lockable;
} td_pdesc_t;

/* td_params.c */
const td_pdesc_t *td_pdesc(int machine, int param);            /* never NULL for 0 <= param < TD_NPARAMS */
int td_pdef(int machine, int param);                           /* the default value on that machine */
const td_pdesc_t *td_gdesc(int slot);
int td_gdef(int slot);
extern const float td_len_steps[17];                           /* LENGTH index -> steps (16 = INF: -1) */
extern const float td_rrate_steps[4];                          /* retrig rate -> steps between hits */
extern const float td_div_steps[12];                           /* delay time divisions -> steps */
extern const float td_perf_div_steps[TD_PERF_DIVS];            /* td_perf divisions -> steps */
extern const float td_fm_ratio[16];
extern const int8_t td_chord_iv[8][4];                         /* chord memory: intervals (-128: no note) */
extern const uint8_t td_scales[8][12];                         /* scale masks per pitch class */

/* ---- mappings (value -> unit) */
static inline float map_tdec(float v) { float x = v * (1.f / 127.f); return 0.005f + 6.0f * x * x * x; }   /* 5 ms .. 6 s (T60) */
static inline float map_tatk(float v) { float x = v * (1.f / 127.f); return 4.0f * x * x * x; }            /* 0 .. 4 s */
static inline float map_thold(float v) { float x = v * (1.f / 127.f); return 2.0f * x * x * x; }           /* 0 .. 2 s */
static inline float map_trel(float v) { float x = v * (1.f / 127.f); return 0.005f + 10.0f * x * x * x; }  /* 5 ms .. 10 s */
static inline float map_tsweep(float v) { float x = v * (1.f / 127.f); return 0.001f + 0.3f * x * x; }     /* 1 .. 300 ms */
static inline float map_tglide(float v) { float x = v * (1.f / 127.f); return x * x * x; }                 /* 0 .. 1 s */
static inline float map_cut_hz(float v) { return 20.f * exp2f(v * (9.9657843f / 127.f)); }                 /* 20 Hz .. 20 kHz */
static inline float map_lowcut_hz(float v) { return 10.f * exp2f(v * (7.6438562f / 127.f)); }              /* 10 Hz .. 2 kHz */

/* ---- small DSP helpers */
static inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline int clampi(int x, int a, int b) { return x < a ? a : (x > b ? b : x); }
static inline float softclip(float x)                          /* tanh-like, smooth, |y| <= 1 */
{
    if (x > 3.f) return 1.f;
    if (x < -3.f) return -1.f;
    float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}
static inline float drive_sat(float x, float d)                /* d 0..1: clean at 0, saturated (peak-normalised) at 1 */
{
    if (d <= 0.f) return x;
    float g = 1.f + 9.f * d;
    float y = softclip(x * g) * (1.f / softclip(g)) * (1.f - 0.15f * d);
    float m = d * 8.f;
    return m >= 1.f ? y : x + (y - x) * m;
}
static inline uint32_t td_xs(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}
static inline float td_noise(uint32_t *s) { return (float)(int32_t)td_xs(s) * (1.0f / 2147483648.0f); }
static inline float td_rand01(uint32_t *s) { return (float)(td_xs(s) >> 8) * (1.0f / 16777216.0f); }
static inline float fast_exp2(float x)
{
    if (x < -120.f) return 0.f;
    if (x > 120.f) x = 120.f;
    float fi = floorf(x), f = x - fi;
    float p = 1.0f + f * (0.69314718f + f * (0.24022651f + f * (0.05550411f + f * (0.00961813f + f * (0.00133336f + f * 0.00015403f)))));
    union { float f; int32_t i; } u;
    u.i = ((int32_t)fi + 127) << 23;
    return p * u.f;
}
static inline float fast_log2(float x)                         /* x > 0 */
{
    union { float f; int32_t i; } u = {x};
    float e = (float)(((u.i >> 23) & 255) - 128);
    u.i = (u.i & 0x007FFFFF) | 0x3F800000;                     /* mantissa in [1, 2) */
    float m = u.f;
    return e + (-0.34484843f * m + 2.02466578f) * m - 0.67487759f;   /* (the polynomial is log2(m) + 1) */
}
extern float td_sin_tab[4097];
static inline float sin01(float ph)                            /* sin(2 pi ph), any ph */
{
    ph -= floorf(ph);
    float x = ph * 4096.f;
    int i = (int)x;
    float f = x - (float)i;
    return td_sin_tab[i] + (td_sin_tab[i + 1] - td_sin_tab[i]) * f;
}
static inline float note_hz(float note) { return 440.f * fast_exp2((note - 69.f) * (1.f / 12.f)); }
static inline float t60_coef(float seconds) { return expf(-6.9077553f / (seconds * TD_SR)); }
static inline float tau_coef(float seconds) { return expf(-1.0f / (seconds * TD_SR)); }
static inline float onepole_a(float hz) { return 1.f - expf(-6.2831853f * hz / TD_SR); }

/* the TPT state-variable filter (A. Simper / Cytomic): stable under fast modulation */
typedef struct { float ic1, ic2; } td_svf_t;
static inline void svf_run(td_svf_t *s, float v0, float g, float k, float *lp, float *bp, float *hp)
{
    float a1 = 1.f / (1.f + g * (g + k));
    float v3 = v0 - s->ic2;
    float v1 = a1 * s->ic1 + g * a1 * v3;
    float v2 = s->ic2 + g * v1;
    s->ic1 = 2.f * v1 - s->ic1;
    s->ic2 = 2.f * v2 - s->ic2;
    *lp = v2;
    *bp = v1;
    *hp = v0 - k * v1 - v2;
}
static inline float svf_g(float hz)
{
    hz = clampf(hz, 5.f, 21000.f);
    return tanf(3.14159265f * hz / TD_SR);
}
extern float td_cut_g[1025];                                   /* svf g for cutoff units 0..127 (1024 steps) */
static inline float cut_g(float u)
{
    u = clampf(u, 0.f, 127.f) * (1024.f / 127.f);
    int i = (int)u;
    if (i >= 1024) return td_cut_g[1024];
    float f = u - (float)i;
    return td_cut_g[i] + (td_cut_g[i + 1] - td_cut_g[i]) * f;
}
static inline float hermite(float xm1, float x0, float x1, float x2, float t)
{
    float c = (x1 - xm1) * 0.5f;
    float v = x0 - x1;
    float w = c + v;
    float a = w + v + (x2 - x0) * 0.5f;
    float b = w + a;
    return ((a * t - b) * t + c) * t + x0;
}

/* ---- pattern data */
typedef struct {
    uint8_t trig, note, vel, cond, retrig, rrate;
    int8_t micro;
    uint8_t nlocks, slide, accent;
} td_step_t;
typedef struct { uint8_t param, pad; int16_t value; } td_lock_t;
typedef struct { uint8_t track, param; int16_t value; } td_scene_ent_t;
typedef struct {
    uint8_t machine[TD_TRACKS], length[TD_TRACKS];
    int16_t param[TD_TRACKS][TD_NPARAMS];
    td_step_t step[TD_TRACKS][TD_STEPS];
    td_lock_t lock[TD_TRACKS][TD_STEPS][TD_LPS];
    uint16_t nscene[2];
    td_scene_ent_t scene[2][TD_SCENE_MAX];
} td_pattern_t;

/* ---- voices */
typedef struct {
    float ph, f0, sweep, kp, penv, aenv, ka, ck, kck, tph, hpz, lp, lpa;
    int32_t hold;
} td_kick_t;
typedef struct {
    float ph1, ph2, f1, f2, psw, penv, kp, e1, e2, k1, k2, body, ne, kn, nlev, nt, knt, lp, lpa, g, k;
    td_svf_t hp;
} td_snare_t;
typedef struct {
    float ph[6], inc[6], env, kd, color, gb, kb, gh, att;
    td_svf_t bp, hp;
} td_hat_t;
typedef struct {
    float g, k, gr, be, kb, te, kt, re, kr, roomlev;
    int32_t t, bt[8];
    int32_t nb, bi;
    td_svf_t bp, room;
} td_clap_t;
typedef struct {
    float phc, phm, f0, sweep, kp, penv, ae, ka, fe, kf, idx, nz, knz, nlev, hpz, lp, lpa;
} td_tom_t;
typedef struct { float ph1, ph2, phs; } td_synth_t;
typedef struct { float phc, phm, m1, m2, ienv, kie; } td_fm_t;
typedef struct {
    const float *data;
    uint32_t len, a, b;                                        /* region [a, b) in frames */
    double pos;
    float rate, gain, fin;
    uint8_t rev, loop, slot;
} td_smp_t;

enum { ENV_ATT, ENV_HOLD, ENV_DEC, ENV_SUS, ENV_REL, ENV_DONE };
typedef struct {
    uint8_t active, fading, gated, seq, machine, accent, note, live_note;
    uint32_t age;
    float vel;                                                 /* velocity gain */
    int32_t gate;                                              /* frames left with the gate on; < 0: held */
    float fade, fade_dec;
    uint8_t ast, fst;                                          /* amp / filter envelope stage */
    float alev, flev;
    int32_t ahold;
    float cut_s;                                               /* smoothed base cutoff (units) */
    td_svf_t flt;
    float pitch, ptarget, glide_t;                             /* melodic: semitones (MIDI note), glide time */
    uint32_t rng;
    union {
        td_kick_t kick;
        td_snare_t sn;
        td_hat_t hat;
        td_clap_t clap;
        td_tom_t tom;
        td_synth_t sy;
        td_fm_t fm;
        td_smp_t smp;
    } m;
} td_voice_t;

/* ---- events (the sequencer's schedule, in global steps) */
enum { EV_SWITCH, EV_BAR, EV_LOCK, EV_TRIG, EV_RETRIG, EV_CLOCK };
typedef struct {
    double t;
    uint8_t type, track, pat, step;
    int32_t loop;
} td_event_t;

typedef struct {
    td_voice_t v[TD_VOICES];
    float eff[TD_NPARAMS];                                     /* effective parameters this segment */
    uint64_t lk_mask;                                          /* the live parameter locks (the last trig's) */
    int16_t lk_val[TD_NPARAMS];
    /* retrigs */
    int rt_left, rt_total, rt_i;
    double rt_next, rt_int;
    uint8_t rt_note, rt_accent;
    float rt_vel, rt_fade;
    /* slide (acid): the last sequencer note slides into the next */
    uint8_t slide_pending;
    /* LFO */
    float lfo_ph, lfo_out, lfo_hold, lfo_rnd, lfo_fade, lfo_mod;
    uint8_t lfo_done, lfo_trigged;
    uint32_t lfo_rng;
    /* post chain (smoothed gains and states) */
    float g_level, g_l, g_r, g_dly, g_rev, g_cho, g_mute, g_width, g_duck;
    td_svf_t lowcut;
    float srr_ph, srr_hold;
    float wbuf[TD_WIDTH_BUF];
    uint32_t wpos;
    float peak;
    float last_pitch;
    uint32_t age;
    int midi_note;
    int64_t midi_off;
} td_track_t;

typedef struct {
    float level_t, level_s, drive_t, drive_s;
    uint64_t wabs;                                             /* frames written into the ring */
    int touching;
    float rate_t, rate_s, mix, sgain;
    double head;
    float dc_x[2], dc_y[2];
    float meter;
    int captures;
} td_deck_t;

typedef struct {                                               /* the performance insert (REPEAT .. CRUSH) */
    int active, type, target, div;
    float amount, amount_s, mix;
    int releasing;
    uint32_t len, rec, pos, dur, t;
    double tpos;
    td_svf_t f[2];
    float hold[2], ph, gate;
    float fg, fk, cq, cinc;                                    /* per segment: filter g, k; crush step, hold rate */
    int fhp;
    float ap[2][4], apa, apfb[2];                              /* PHASER: allpass states, coefficient, feedback */
    float lfo;                                                 /* per segment: 0..1 through the division */
    uint32_t rng;                                              /* NOISE */
    float drive;                                               /* DISTORT */
    /* the next request */
    int r_on, r_type, r_target, r_div;
    float r_amount;
} td_insert_t;

typedef struct {                                               /* a stereo tape-style delay */
    float *buf;                                                /* TD_DLY stereo frames, interleaved */
    uint32_t w;
    float time_s, lpl, lpr, hpl, hpr, wow_ph, flut_ph, drift, drift_t;
    float hold, hold_t;                                        /* DUB HOLD 0..1 */
    float hold_time;
    uint32_t rng;
} td_tape_t;

typedef struct {
    int on;
    uint32_t rng, count;
    int64_t bar;                                               /* global bar (from play start) last handled */
    int kind, next_kind, drop_bars, fill_bar, chord_wait, degree, transpose, phrases;
    uint8_t mute[TD_TRACKS], override[TD_TRACKS];
    int fill;
    float walk[TD_TRACKS][3], walk_t[TD_TRACKS][3];            /* cutoff, delay send, FM index */
    float cut_ramp[TD_TRACKS], cut_ramp_t[TD_TRACKS];          /* breakdown / build filter moves */
    float dfb, dfb_t;                                          /* build: delay feedback */
} td_auto_t;

typedef struct { uint8_t b[3], n; int64_t frame; } td_midimsg_t;   /* frame: on the internal timeline */

typedef struct {
    int inited;
    td_pattern_t pat[TD_PATTERNS];
    int16_t gparam[TD_SLOTS];
    float bpm, nudge;
    int swing;
    int scale_root, scale;
    /* transport */
    int playing, recording, fill_user;
    int cur, dec_pat, queued;
    int64_t cur_origin, dec_origin, kdec;
    int64_t next_bar, next_clk;                                /* the next bar line and MIDI clock pulse to send */
    double gpos;
    td_event_t ev[TD_EVQ];
    int nev;
    /* tracks */
    td_track_t trk[TD_TRACKS];
    uint8_t muted[TD_TRACKS];
    uint32_t rng, dice_count;
    float xf, xf_s;
    float duck;
    int duck_att;
    /* buses */
    td_deck_t deck[TD_DECKS];
    td_insert_t ins;
    float thr_d_t, thr_d_s, thr_r_t, thr_r_s;                  /* throw send gains (target, smoothed) */
    int thr_d_target, thr_r_target, thr_d_div;
    float thr_d_amount;
    int perf_last_type, perf_last_target, perf_active;
    td_tape_t dly, thr;
    float dubhold_t;
    td_svf_t mf_lp[2], mf_hp[2];
    float mf_s;
    float comp_env, comp_g;
    /* lookahead limiter */
    float lim_dl[2][64];
    float lim_box[64];
    double lim_sum;
    float lim_r;
    float lim_dq_v[64];
    int64_t lim_dq_i[64];
    int lim_dq_h, lim_dq_n;
    int64_t lim_n;
    int lim_pos;
    /* output FIFO (renders TD_BLK at a time, whatever the host asks) */
    float fifo[TD_BLK * 2];
    int fifo_rd, fifo_n;
    uint64_t frames_int, frames_out;
    int64_t anchor;
    uint64_t anchor_host;
    /* bar lines (frames), for capture */
    int64_t bar_frame[8];
    int nbar;
    /* MIDI out */
    td_midimsg_t mq[TD_MIDI_Q];
    int mq_rd, mq_n, midi_clock, midi_notes;
    /* meters */
    float peak_l, peak_r;
    /* tap tempo */
    double tap_last, tap_iv[4];
    int ntap;
    td_auto_t ap;
    /* debug taps for the C tests: 0 none, 1 the master delay's return (left) */
    int dbg_what;
    float *dbg_buf;
    uint32_t dbg_len, dbg_n;
} td_engine_t;

extern td_engine_t td_E;
#define E td_E

/* big buffers (td_engine.c, td_samples.c) */
extern float td_ring[TD_DECKS][TD_RING * 2];
extern float td_pool[TD_POOL_FRAMES];
typedef struct {
    uint32_t off, len, rate;
    char name[16];
    uint8_t used, loading;
} td_slot_t;
extern td_slot_t td_slots[TD_SAMPLE_SLOTS];

/* locking */
void td_lock(void);
void td_unlock(void);

/* td_dsp.c */
void td_dsp_tables(void);
void td_fx_reset(void);
void td_reverb_process(const float *inl, const float *inr, float *outl, float *outr, int n, float size, float damp);
void td_chorus_process(const float *inl, const float *inr, float *outl, float *outr, int n);
void td_tape_process(td_tape_t *d, const float *inl, const float *inr, float *outl, float *outr, int n,
                     float time_frames, float fb, float tone, int tape);
void td_comp_process(float *l, float *r, int n, float amount);
void td_limiter_process(float *l, float *r, int n);

/* td_voice.c */
void td_voice_start(td_voice_t *v, int machine, const float *eff, int note, float vel, int32_t gate, int accent);
void td_voice_render(td_voice_t *v, const float *eff, float *out, int n, float rate_steps);
void td_voice_fade(td_voice_t *v, float ms);
void td_voice_gate_off(td_voice_t *v);

/* td_seq.c */
double td_step_rate(void);                                     /* steps per frame now */
double td_frames_per_step(void);
int td_master_len(const td_pattern_t *p);
void td_seq_block(double g0, double rate, td_event_t *out, int *off, int *nout, int max);
void td_seq_apply(const td_event_t *ev, int64_t frame);
void td_seq_reset_transport(void);
void td_seq_stop_notes(void);
void td_track_eff(int t);                                      /* compute trk[t].eff from base, locks, scenes, LFO, autopilot */
void td_lfo_advance(int t, int n, double rate);
void td_note_on(int t, int note, float vel, int32_t gate, int flags);
enum { NF_SEQ = 1, NF_ACCENT = 2, NF_LIVE = 4, NF_LEGATO = 8 };
int td_eff_muted(int t);
int td_is_melodic(int machine);
void td_auto_bar(int64_t bar);
void td_auto_block(void);
int td_scale_snap(int note);
void td_midi_push(uint8_t a, uint8_t b, uint8_t c, int n, int64_t frame);
float td_param_live_f(int t, int p);

/* td_samples.c */
void td_samples_init(void);
void td_samples_builtin(void);
int x_sample_load(int slot, const float *mono, uint32_t frames, uint32_t rate, const char *name);
int x_capture(int deck, int bars, int slot);

/* td_default.c */
void td_default_project(void);
void td_pattern_init_kit(td_pattern_t *p);
void td_set_machine_x(td_pattern_t *p, int t, int machine);

/* td_project.c */
uint32_t td_crc32(const void *data, size_t n);
