/* SPDX-License-Identifier: GPL-3.0-only */
/* TRIGDECK engine: td_init, td_render and the signal flow.
 *
 *   tracks 0-3 -> deck A bus, tracks 4-7 -> deck B bus
 *   track:  voices (machine -> filter -> amp env) -> OVERDRIVE -> BITS -> SRR -> LOWCUT -> DUCK (under track 1's
 *           trigs) -> LEVEL x mute -> PAN / WIDTH -> deck bus; DELAY / REVERB / CHORUS sends (post fader)
 *   deck:   TRIM drive -> the scratch ring (always recording, 21.8 s) -> scratch / slip -> performance insert (if
 *           targeted) -> fader. While a deck is scratched or repeated its sends are faded (the effects ring out).
 *   master: decks + tape delay + reverb + chorus returns -> performance insert (if on the master) + throw delay ->
 *           DJ filter -> compressor -> VOLUME -> lookahead limiter (never beyond -0.3 dBFS) -> out
 *
 * td_render renders blocks of TD_BLK frames into a FIFO and copies out whatever the host asks for, so the output does
 * not depend on the host's buffer sizes. Within a block the sequencer's events split the work into segments: every
 * trig starts on its exact frame. */
#include "td_internal.h"
#include <pthread.h>
#include <time.h>
#if defined(__x86_64__)
#include <xmmintrin.h>
#endif

td_engine_t td_E;
float td_ring[TD_DECKS][TD_RING * 2];
static float g_dly_buf[TD_DLY * 2], g_thr_buf[TD_DLY * 2];
static float g_perf[TD_PERF_BUF * 2];

/* ---- the lock */
#if defined(__wasm__)
/* in a browser the engine lives in the AudioWorklet alone: every call arrives on its thread, nothing to lock */
void td_lock(void) {}
void td_unlock(void) {}
#else
static pthread_mutex_t g_mx;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static void mx_init(void)
{
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_setprotocol(&a, PTHREAD_PRIO_INHERIT);   /* the audio thread never waits behind a preempted UI */
    pthread_mutex_init(&g_mx, &a);
    pthread_mutexattr_destroy(&a);
}
void td_lock(void)
{
    pthread_once(&g_once, mx_init);
    pthread_mutex_lock(&g_mx);
}
void td_unlock(void) { pthread_mutex_unlock(&g_mx); }
#endif

/* ---- denormals: flushed to zero while rendering */
#if defined(__aarch64__)
static inline uint64_t fpcr_get(void) { uint64_t v; __asm__ volatile("mrs %0, fpcr" : "=r"(v)); return v; }
static inline void fpcr_set(uint64_t v) { __asm__ volatile("msr fpcr, %0" : : "r"(v)); }
#define FTZ_ON() uint64_t ftz_saved_ = fpcr_get(); fpcr_set(ftz_saved_ | (1ull << 24))
#define FTZ_OFF() fpcr_set(ftz_saved_)
#elif defined(__x86_64__)
#define FTZ_ON() unsigned ftz_saved_ = _mm_getcsr(); _mm_setcsr(ftz_saved_ | 0x8040u)
#define FTZ_OFF() _mm_setcsr(ftz_saved_)
#else
#define FTZ_ON() do {} while (0)
#define FTZ_OFF() do {} while (0)
#endif

/* ---- the performance insert */
#define XF 96u                                                 /* REPEAT loop crossfade (2 ms) */
#define FL_RING 2048u                                          /* FLANGER's delay line (in g_perf), frames */
static int is_insert(int t)
{
    return t == TD_PERF_REPEAT || t == TD_PERF_STUTTER || t == TD_PERF_TAPESTOP || t == TD_PERF_FILTER || t == TD_PERF_CRUSH
        || (t >= TD_PERF_ECHO && t <= TD_PERF_SWEEP);
}
/* the effects that replace the live sound with captured audio (the deck's slip / scratch defer to them) */
static int is_capture(int t)
{
    return t == TD_PERF_REPEAT || t == TD_PERF_STUTTER || t == TD_PERF_TAPESTOP || t == TD_PERF_REVERSE || t == TD_PERF_HALF;
}
static void ins_update(int n)
{
    td_insert_t *u = &E.ins;
    if (u->active && !u->releasing && (!u->r_on || u->r_type != u->type || u->r_target != u->target))
        u->releasing = 1;
    if (u->active && !u->releasing)
        u->amount = u->r_amount;
    if (!u->active && u->r_on && is_insert(u->r_type)) {
        u->active = 1;
        u->releasing = 0;
        u->type = u->r_type;
        u->target = u->r_target;
        u->div = u->r_div;
        u->amount = u->amount_s = u->r_amount;
        double len = td_perf_div_steps[clampi(u->div, 0, TD_PERF_DIVS - 1)] * td_frames_per_step();
        double cap = (double)(TD_PERF_BUF - XF - 8u);
        if (len > cap) len = cap;
        if (len < 64.0) len = 64.0;
        u->len = (uint32_t)len;
        u->dur = u->len;
        u->rec = u->pos = u->t = 0;
        u->tpos = 0.0;
        u->mix = 0.f;
        u->gate = 1.f;
        u->ph = 1.f;
        u->hold[0] = u->hold[1] = 0.f;
        memset(u->f, 0, sizeof u->f);
        memset(u->ap, 0, sizeof u->ap);
        u->apfb[0] = u->apfb[1] = 0.f;
        u->rng = 0x9E3779B9u;
        if (u->type == TD_PERF_ECHO) memset(g_perf, 0, (size_t)u->len * 2u * sizeof(float));   /* (the echo's line) */
        if (u->type == TD_PERF_FLANGER) memset(g_perf, 0, FL_RING * 2u * sizeof(float));
    }
    if (!u->active)
        return;
    u->amount_s += (u->amount - u->amount_s) * fminf(1.f, (float)n * 0.003f);
    u->lfo = (float)(u->t % u->len) / (float)u->len;
    float sweep = 0.5f - 0.5f * cosf(6.2831853f * u->lfo);    /* 0 .. 1 .. 0 once a division */
    if (u->type == TD_PERF_PHASER) {
        float tn = tanf(3.14159265f * 180.f * exp2f(5.f * sweep) / TD_SR);
        u->apa = (tn - 1.f) / (tn + 1.f);
    } else if (u->type == TD_PERF_SWEEP) {
        u->fg = svf_g(120.f * exp2f(6.5f * sweep));
        u->fk = 1.f / (0.7f + u->amount_s * 7.f);
    } else if (u->type == TD_PERF_NOISE) {
        u->fg = svf_g(250.f * exp2f(u->amount_s * 5.5f));
        u->fk = 1.2f;
    } else if (u->type == TD_PERF_DISTORT) {
        u->drive = 1.f + u->amount_s * u->amount_s * 40.f;
    } else if (u->type == TD_PERF_FILTER) {
        float a2 = (u->amount_s - 0.5f) * 2.f;
        u->fhp = a2 > 0.f;
        float fc = a2 < 0.f ? 20000.f * exp2f(a2 * 7.6f) : 20.f * exp2f(a2 * 8.6f);
        u->fg = svf_g(fc);
        u->fk = 1.f / (0.75f + fabsf(a2) * 1.2f);
    } else if (u->type == TD_PERF_CRUSH) {
        u->cq = exp2f(15.f - u->amount_s * 12.f);
        u->cinc = 1.f / (1.f + u->amount_s * 20.f);
    }
}
static inline float perf_at(uint32_t i, int c) { return g_perf[i * 2u + (uint32_t)c]; }
static void ins_tick(float *pl, float *pr)
{
    td_insert_t *u = &E.ins;
    float l = *pl, r = *pr, wl = l, wr = r;
    switch (u->type) {
    case TD_PERF_REPEAT:
    case TD_PERF_STUTTER:
        if (u->rec < u->len + XF) {
            g_perf[u->rec * 2u] = l;
            g_perf[u->rec * 2u + 1u] = r;
            u->rec++;
        }
        if (u->rec > u->len) {                                 /* (the first pass is live, being captured) */
            uint32_t p = u->pos;
            wl = perf_at(p, 0);
            wr = perf_at(p, 1);
            if (p < XF) {                                      /* the loop's end runs on into its start */
                float x = (float)p / (float)XF;
                wl = perf_at(u->len + p, 0) * (1.f - x) + wl * x;
                wr = perf_at(u->len + p, 1) * (1.f - x) + wr * x;
            }
            if (++u->pos >= u->len) u->pos = 0;
        }
        if (u->type == TD_PERF_STUTTER) {
            uint32_t per = u->len / 2u > 32u ? u->len / 2u : 32u;
            float c = (float)(u->t % per) / (float)per;
            float target = c < 1.f - 0.75f * u->amount_s ? 1.f : 0.f;
            u->gate += target > u->gate ? 1.f / 72.f : -1.f / 72.f;
            u->gate = clampf(u->gate, 0.f, 1.f);
            wl *= u->gate;
            wr *= u->gate;
        }
        u->t++;
        break;
    case TD_PERF_TAPESTOP: {
        if (u->rec < TD_PERF_BUF) {
            g_perf[u->rec * 2u] = l;
            g_perf[u->rec * 2u + 1u] = r;
            u->rec++;
        }
        float rr = 1.f - (float)u->t / (float)u->dur;
        if (rr < 0.f) rr = 0.f;
        rr = rr * sqrtf(rr);
        u->tpos += rr;
        double maxp = (double)u->rec - 3.0;
        if (u->tpos > maxp) u->tpos = maxp > 0 ? maxp : 0;
        int ip = (int)u->tpos;
        float f = (float)(u->tpos - ip), g = fminf(1.f, rr * 6.f);
        uint32_t i0 = ip > 0 ? (uint32_t)ip - 1u : 0u;
        wl = hermite(perf_at(i0, 0), perf_at((uint32_t)ip, 0), perf_at((uint32_t)ip + 1u, 0), perf_at((uint32_t)ip + 2u, 0), f) * g;
        wr = hermite(perf_at(i0, 1), perf_at((uint32_t)ip, 1), perf_at((uint32_t)ip + 1u, 1), perf_at((uint32_t)ip + 2u, 1), f) * g;
        u->t++;
        break;
    }
    case TD_PERF_FILTER: {
        float lp, bp, hp;
        svf_run(&u->f[0], l, u->fg, u->fk, &lp, &bp, &hp);
        wl = u->fhp ? hp : lp;
        svf_run(&u->f[1], r, u->fg, u->fk, &lp, &bp, &hp);
        wr = u->fhp ? hp : lp;
        break;
    }
    case TD_PERF_CRUSH:
        u->ph += u->cinc;
        if (u->ph >= 1.f) {
            u->ph -= 1.f;
            u->hold[0] = floorf(l * u->cq + 0.5f) / u->cq;
            u->hold[1] = floorf(r * u->cq + 0.5f) / u->cq;
        }
        wl = u->hold[0];
        wr = u->hold[1];
        break;
    case TD_PERF_ECHO: {                                       /* ping-pong: left feeds right feeds left */
        uint32_t i = u->t % u->len;
        float yl = g_perf[i * 2u], yr = g_perf[i * 2u + 1u];
        float fb = 0.35f + 0.5f * u->amount_s;
        g_perf[i * 2u] = 0.5f * (l + r) * 0.8f + yr * fb;
        g_perf[i * 2u + 1u] = yl * fb;
        wl = l + yl * 0.8f;
        wr = r + yr * 0.8f;
        u->t++;
        break;
    }
    case TD_PERF_FLANGER: {
        uint32_t w = u->t & (FL_RING - 1u);
        float c = (float)(u->t % u->len) / (float)u->len;
        float fb = 0.2f + 0.6f * u->amount_s;
        float y[2];
        for (int ch = 0; ch < 2; ch++) {                       /* (the right side a quarter of a sweep later) */
            float s = 0.5f - 0.5f * cosf(6.2831853f * (c + 0.25f * (float)ch));
            float d = 24.f + 216.f * s;                       /* 0.5 .. 5 ms */
            float rp = (float)w - d + (float)FL_RING;
            uint32_t i0 = (uint32_t)rp, i1 = i0 + 1u;
            float f = rp - (float)i0;
            y[ch] = perf_at(i0 & (FL_RING - 1u), ch) * (1.f - f) + perf_at(i1 & (FL_RING - 1u), ch) * f;
        }
        g_perf[w * 2u] = l + y[0] * fb;
        g_perf[w * 2u + 1u] = r + y[1] * fb;
        wl = 0.7f * (l + y[0]);
        wr = 0.7f * (r + y[1]);
        u->t++;
        break;
    }
    case TD_PERF_PHASER: {                                     /* four first-order allpasses with feedback */
        float in[2] = {l, r}, out[2];
        float fb = 0.2f + 0.5f * u->amount_s;
        for (int ch = 0; ch < 2; ch++) {
            float x = in[ch] + u->apfb[ch] * fb;
            for (int k = 0; k < 4; k++) {
                float y = u->apa * x + u->ap[ch][k];
                u->ap[ch][k] = x - u->apa * y;
                x = y;
            }
            u->apfb[ch] = x;
            out[ch] = 0.5f * (in[ch] + x * (0.5f + 0.5f * u->amount_s)) * 1.3f;
        }
        wl = out[0];
        wr = out[1];
        u->t++;
        break;
    }
    case TD_PERF_TRANS: {                                      /* the live sound, gated in time */
        float c = (float)(u->t % u->len) / (float)u->len;
        float target = c < 1.f - 0.75f * u->amount_s ? 1.f : 0.f;
        u->gate += target > u->gate ? 1.f / 72.f : -1.f / 72.f;
        u->gate = clampf(u->gate, 0.f, 1.f);
        wl = l * u->gate;
        wr = r * u->gate;
        u->t++;
        break;
    }
    case TD_PERF_AUTOPAN: {
        float p = sinf(6.2831853f * (float)(u->t % u->len) / (float)u->len) * u->amount_s;
        wl = l * (1.f - fmaxf(0.f, p));
        wr = r * (1.f - fmaxf(0.f, -p));
        u->t++;
        break;
    }
    case TD_PERF_REVERSE: {                                    /* the first pass is live (captured), then backwards */
        if (u->rec < u->len) {
            g_perf[u->rec * 2u] = l;
            g_perf[u->rec * 2u + 1u] = r;
            u->rec++;
        } else {
            uint32_t p = u->pos, idx = u->len - 1u - p;
            /* (each pass starts on the newest sample, so the first is seamless; later wraps fade through zero) */
            float e = fminf(1.f, (float)(u->len - p) / (float)XF);
            if (u->dur > 0u) e = fminf(e, (float)p / (float)XF);
            wl = perf_at(idx, 0) * e;
            wr = perf_at(idx, 1) * e;
            if (++u->pos >= u->len) { u->pos = 0; u->dur++; }
        }
        break;
    }
    case TD_PERF_HALF: {                                       /* read at half speed while capturing at full */
        if (u->rec < u->len) {
            g_perf[u->rec * 2u] = l;
            g_perf[u->rec * 2u + 1u] = r;
            u->rec++;
        }
        int ip = (int)u->tpos;
        float f = (float)(u->tpos - ip);
        uint32_t i0 = (uint32_t)ip, i1 = i0 + 1u < u->len ? i0 + 1u : i0;
        float e = fminf(1.f, (float)((double)u->len - u->tpos) / (float)XF);
        if (u->t > 0u) e = fminf(e, (float)u->tpos / (float)XF);    /* (t: the passes done) */
        wl = (perf_at(i0, 0) * (1.f - f) + perf_at(i1, 0) * f) * e;
        wr = (perf_at(i0, 1) * (1.f - f) + perf_at(i1, 1) * f) * e;
        u->tpos += 0.5;
        if (u->tpos >= (double)u->len) { u->tpos -= (double)u->len; u->t++; }
        break;
    }
    case TD_PERF_NOISE: {                                      /* filtered white noise over the sound */
        float lp, bp, hp, nl, nr;
        svf_run(&u->f[0], td_rand01(&u->rng) * 2.f - 1.f, u->fg, u->fk, &lp, &bp, &hp);
        nl = hp;
        svf_run(&u->f[1], td_rand01(&u->rng) * 2.f - 1.f, u->fg, u->fk, &lp, &bp, &hp);
        nr = hp;
        float lv = 0.03f + 0.3f * u->amount_s * u->amount_s;
        wl = l + nl * lv;
        wr = r + nr * lv;
        break;
    }
    case TD_PERF_DISTORT: {
        float k = 1.f / tanhf(fminf(u->drive, 4.f));          /* (about the same loudness at any drive) */
        wl = tanhf(l * u->drive) * k * 0.6f;
        wr = tanhf(r * u->drive) * k * 0.6f;
        break;
    }
    case TD_PERF_SWEEP: {
        float lp, bp, hp;
        svf_run(&u->f[0], l, u->fg, u->fk, &lp, &bp, &hp);
        wl = lp;
        svf_run(&u->f[1], r, u->fg, u->fk, &lp, &bp, &hp);
        wr = lp;
        u->t++;
        break;
    }
    default: break;
    }
    if (u->releasing) {
        u->mix -= 1.f / 480.f;
        if (u->mix <= 0.f) { u->mix = 0.f; u->active = 0; }
    } else if (u->mix < 1.f)
        u->mix = fminf(1.f, u->mix + 1.f / 240.f);
    *pl = l + (wl - l) * u->mix;
    *pr = r + (wr - r) * u->mix;
}
static float ins_takeover(void)                                /* how much the insert replaces the live sound */
{
    const td_insert_t *u = &E.ins;
    if (!u->active || !is_capture(u->type)) return 0.f;
    return u->mix;
}

/* ---- one segment (n <= TD_BLK frames) of everything */
static void render_segment(float *out, int n)
{
    float dry[TD_DECKS][2][TD_BLK], snd[TD_DECKS][3][2][TD_BLK];
    float duck[TD_BLK];
    memset(dry, 0, sizeof dry);
    memset(snd, 0, sizeof snd);
    E.xf_s += (E.xf - E.xf_s) * fminf(1.f, (float)n * 0.004f);
    if (fabsf(E.xf_s - E.xf) < 1e-5f) E.xf_s = E.xf;
    {   /* the sidechain: a fast attack on track 1's trigs, a release a fifth of a beat long */
        float rel = tau_coef(60.f / E.bpm * 0.22f);
        for (int i = 0; i < n; i++) {
            if (E.duck_att) {
                E.duck += 1.f / 144.f;
                if (E.duck >= 1.f) { E.duck = 1.f; E.duck_att = 0; }
            } else
                E.duck *= rel;
            duck[i] = E.duck;
        }
    }
    float sm = fminf(1.f, (float)n * 0.0042f);                 /* (per-segment step of a 5 ms smoother) */
    (void)sm;
    float mdecay = expf(-(float)n / (0.3f * TD_SR));
    for (int t = 0; t < TD_TRACKS; t++) {
        td_track_t *tr = &E.trk[t];
        td_track_eff(t);
        const float *e = tr->eff;
        float mono[TD_BLK];
        memset(mono, 0, sizeof(float) * (size_t)n);
        int voices = 0;
        for (int i = 0; i < TD_VOICES; i++)
            if (tr->v[i].active) {
                td_voice_render(&tr->v[i], e, mono, n, 0.f);
                voices++;
            }
        td_lfo_advance(t, n, 0.0);
        /* the track chain */
        float drv = e[P_ODRV] * (1.f / 127.f);
        float bits = e[P_BITS], srr = e[P_SRR];
        float cq = bits > 0.f ? exp2f(15.f - bits * (14.f / 127.f)) : 0.f;
        float sinc = srr > 0.f ? 1.f / (1.f + srr * (31.f / 127.f)) : 0.f;
        float lcg = svf_g(map_lowcut_hz(e[P_LOWCUT]));
        float lv = e[P_LEVEL] * 0.01f;
        float pan = clampf(e[P_PAN] * (1.f / 64.f), -1.f, 1.f);
        float tl = lv * lv, tgl = pan <= 0.f ? 1.f : 1.f - pan, tgr = pan >= 0.f ? 1.f : 1.f + pan;
        float tmute = td_eff_muted(t) ? 0.f : 1.f;
        float d1 = e[P_DLY] * (1.f / 127.f), d2 = e[P_REV] * (1.f / 127.f), d3 = e[P_CHO] * (1.f / 127.f);
        float tdl = d1 * d1, trv = d2 * d2, tch = d3 * d3;
        float tduck = e[P_DUCK] * (0.9f / 127.f), twid = e[P_WIDTH] * (0.4f / 127.f);
        if (!voices && tr->peak < 1e-7f && fabsf(tr->lowcut.ic1) + fabsf(tr->lowcut.ic2) < 1e-7f) {
            tr->peak *= mdecay;                                /* silent, its chain quiet: nothing to do but be ready */
            tr->g_level = tl; tr->g_mute = tmute; tr->g_l = tgl; tr->g_r = tgr;
            tr->g_dly = tdl; tr->g_rev = trv; tr->g_cho = tch; tr->g_duck = tduck; tr->g_width = twid;
            continue;
        }
        int deck = t / 4;
        uint32_t wofs = 400u + (uint32_t)t * 37u;
        float pk = tr->peak * mdecay;
        for (int i = 0; i < n; i++) {
            float x = mono[i];
            if (drv > 0.f) x = drive_sat(x, drv);
            if (cq > 0.f) x = floorf(x * cq + 0.5f) / cq;
            if (sinc > 0.f) {
                tr->srr_ph += sinc;
                if (tr->srr_ph >= 1.f) { tr->srr_ph -= 1.f; tr->srr_hold = x; }
                x = tr->srr_hold;
            }
            float lp, bp, hp;
            svf_run(&tr->lowcut, x, lcg, 1.4142f, &lp, &bp, &hp);
            x = hp;
            const float k = 0.0042f;
            tr->g_level += (tl - tr->g_level) * k;
            tr->g_mute += (tmute - tr->g_mute) * k;
            tr->g_l += (tgl - tr->g_l) * k;
            tr->g_r += (tgr - tr->g_r) * k;
            tr->g_dly += (tdl - tr->g_dly) * k;
            tr->g_rev += (trv - tr->g_rev) * k;
            tr->g_cho += (tch - tr->g_cho) * k;
            tr->g_duck += (tduck - tr->g_duck) * k;
            tr->g_width += (twid - tr->g_width) * k;
            x *= tr->g_level * tr->g_mute * (1.f - tr->g_duck * duck[i]);
            float ax = fabsf(x);
            if (ax > pk) pk = ax;
            tr->wbuf[tr->wpos & (TD_WIDTH_BUF - 1u)] = x;
            float side = tr->wbuf[(tr->wpos - wofs) & (TD_WIDTH_BUF - 1u)] * tr->g_width;
            tr->wpos++;
            float L = x * tr->g_l + side, R = x * tr->g_r - side;
            dry[deck][0][i] += L;
            dry[deck][1][i] += R;
            snd[deck][0][0][i] += L * tr->g_dly;
            snd[deck][0][1][i] += R * tr->g_dly;
            snd[deck][1][0][i] += L * tr->g_rev;
            snd[deck][1][1][i] += R * tr->g_rev;
            snd[deck][2][0][i] += L * tr->g_cho;
            snd[deck][2][1][i] += R * tr->g_cho;
        }
        tr->peak = pk;
    }
    /* decks */
    ins_update(n);
    float dk[TD_DECKS][2][TD_BLK];
    float tap_dly[2][TD_BLK], tap_rev[2][TD_BLK];
    memset(tap_dly, 0, sizeof tap_dly);
    memset(tap_rev, 0, sizeof tap_rev);
    for (int d = 0; d < TD_DECKS; d++) {
        td_deck_t *k = &E.deck[d];
        float *ring = td_ring[d];
        float pk = k->meter * mdecay;
        int ins_here = E.ins.active && E.ins.target == d;
        for (int i = 0; i < n; i++) {
            k->drive_s += (k->drive_t - k->drive_s) * 0.0042f;
            k->level_s += (k->level_t * k->level_t - k->level_s) * 0.0042f;
            float l = drive_sat(dry[d][0][i], k->drive_s), r = drive_sat(dry[d][1][i], k->drive_s);
            uint32_t wi = (uint32_t)(k->wabs & TD_RING_MASK) * 2u;
            ring[wi] = l;
            ring[wi + 1u] = r;
            k->wabs++;
            float take = 0.f;
            if (k->touching || k->mix > 0.f) {
                k->rate_s += (k->rate_t - k->rate_s) * 0.0014f;    /* the platter's inertia (15 ms) */
                k->head += (double)k->rate_s;
                double maxh = (double)k->wabs - 4.0, minh = (double)k->wabs - (double)(TD_RING - 16u);
                if (k->head > maxh) k->head = maxh;
                if (k->head < minh) k->head = minh;
                int64_t ih = (int64_t)floor(k->head);
                float f = (float)(k->head - (double)ih);
                uint32_t a = (uint32_t)((ih - 1) & TD_RING_MASK) * 2u, b = (uint32_t)(ih & TD_RING_MASK) * 2u;
                uint32_t c = (uint32_t)((ih + 1) & TD_RING_MASK) * 2u, e2 = (uint32_t)((ih + 2) & TD_RING_MASK) * 2u;
                float sl = hermite(ring[a], ring[b], ring[c], ring[e2], f);
                float sr = hermite(ring[a + 1u], ring[b + 1u], ring[c + 1u], ring[e2 + 1u], f);
                float gt = fminf(1.f, fabsf(k->rate_s) * 4.f);
                k->sgain += (gt - k->sgain) * 0.01f;
                if (k->touching) k->mix = fminf(1.f, k->mix + 1.f / 96.f);
                else k->mix = fmaxf(0.f, k->mix - 1.f / 480.f);   /* slip: back to the live bus in 10 ms */
                l += (sl * k->sgain - l) * k->mix;
                r += (sr * k->sgain - r) * k->mix;
                take = k->mix;
            }
            if (ins_here) {
                ins_tick(&l, &r);
                take = fmaxf(take, ins_takeover());
            }
            l *= k->level_s;
            r *= k->level_s;
            dk[d][0][i] = l;
            dk[d][1][i] = r;
            float sg = k->level_s * (1.f - take);
            for (int s = 0; s < 3; s++) {
                snd[d][s][0][i] *= sg;
                snd[d][s][1][i] *= sg;
            }
            if (E.thr_d_target == d) { tap_dly[0][i] += l; tap_dly[1][i] += r; }
            if (E.thr_r_target == d) { tap_rev[0][i] += l; tap_rev[1][i] += r; }
            float al = fmaxf(fabsf(l), fabsf(r));
            if (al > pk) pk = al;
        }
        k->meter = pk;
    }
    /* master effects */
    float sdl[2][TD_BLK], srv[2][TD_BLK], sch[2][TD_BLK], thr_in[2][TD_BLK];
    float rdl[2][TD_BLK], rrv[2][TD_BLK], rch[2][TD_BLK], rth[2][TD_BLK];
    for (int i = 0; i < n; i++) {
        E.thr_d_s += (E.thr_d_t - E.thr_d_s) * 0.0042f;
        E.thr_r_s += (E.thr_r_t - E.thr_r_s) * 0.0042f;
        float ml = dk[0][0][i] + dk[1][0][i], mr = dk[0][1][i] + dk[1][1][i];
        float tdl = E.thr_d_target == TD_TARGET_MASTER ? ml : tap_dly[0][i];
        float tdr = E.thr_d_target == TD_TARGET_MASTER ? mr : tap_dly[1][i];
        float trl = E.thr_r_target == TD_TARGET_MASTER ? ml : tap_rev[0][i];
        float trr = E.thr_r_target == TD_TARGET_MASTER ? mr : tap_rev[1][i];
        float rg = E.thr_r_s;                                  /* (0.5 .. 1.5 with the amount) */
        for (int c = 0; c < 2; c++) {
            sdl[c][i] = snd[0][0][c][i] + snd[1][0][c][i];
            srv[c][i] = snd[0][1][c][i] + snd[1][1][c][i] + (c ? trr : trl) * rg;
            sch[c][i] = snd[0][2][c][i] + snd[1][2][c][i];
        }
        thr_in[0][i] = tdl * E.thr_d_s;
        thr_in[1][i] = tdr * E.thr_d_s;
    }
    double fps = td_frames_per_step();
    {
        float dfb = clampf((float)E.gparam[G_DFB] + E.ap.dfb, 0.f, 127.f) * (1.1f / 127.f);
        float tone = 800.f * exp2f((float)E.gparam[G_DTONE] * (4.3f / 127.f));
        float tm = td_div_steps[clampi(E.gparam[G_DTIME], 0, 11)] * (float)fps;
        td_tape_process(&E.dly, sdl[0], sdl[1], rdl[0], rdl[1], n, tm, dfb, tone, 1);
        float ttm = td_perf_div_steps[clampi(E.thr_d_div, 0, TD_PERF_DIVS - 1)] * (float)fps;
        ttm = fminf(ttm, (float)(TD_DLY - 4096u));             /* (the throw's line: 2 and 4 bars at slow tempos) */
        td_tape_process(&E.thr, thr_in[0], thr_in[1], rth[0], rth[1], n, ttm, 0.55f + 0.35f * E.thr_d_amount, 5000.f, 0);
        td_reverb_process(srv[0], srv[1], rrv[0], rrv[1], n, (float)E.gparam[G_RSIZE], (float)E.gparam[G_RDAMP]);
        td_chorus_process(sch[0], sch[1], rch[0], rch[1], n);
    }
    float L[TD_BLK], R[TD_BLK];
    int ins_master = E.ins.active && E.ins.target == TD_TARGET_MASTER;
    for (int i = 0; i < n; i++) {
        float l = dk[0][0][i] + dk[1][0][i] + rdl[0][i] * 0.8f + rrv[0][i] + rch[0][i] * 0.9f;
        float r = dk[0][1][i] + dk[1][1][i] + rdl[1][i] * 0.8f + rrv[1][i] + rch[1][i] * 0.9f;
        if (ins_master) ins_tick(&l, &r);
        L[i] = l + rth[0][i] * 0.8f;
        R[i] = r + rth[1][i] * 0.8f;
    }
    if (E.dbg_what == 1 && E.dbg_buf)
        for (int i = 0; i < n && E.dbg_n < E.dbg_len; i++) E.dbg_buf[E.dbg_n++] = rdl[0][i];
    {   /* DJ filter: low-pass below 0, high-pass above; at 0 it is out of the signal (faded in over the first 2 steps) */
        float tgt = (float)E.gparam[G_FILTER];
        E.mf_s += (tgt - E.mf_s) * fminf(1.f, (float)n * 0.003f);
        if (fabsf(E.mf_s - tgt) < 1e-4f) E.mf_s = tgt;
        float v = E.mf_s * (1.f / 64.f);
        float glp = svf_g(v < 0.f ? 20000.f * exp2f(v * 7.6f) : 20000.f);
        float ghp = svf_g(v > 0.f ? 10.f * exp2f(v * 9.6f) : 10.f);
        float k = 1.f / (0.707f + fabsf(v) * 0.5f);
        float wet = fminf(1.f, fabsf(E.mf_s) * 0.5f);
        for (int i = 0; i < n; i++) {                          /* (the filter always runs: no stale state when it comes in) */
            float lp, bp, hp;
            svf_run(&E.mf_lp[0], L[i], glp, k, &lp, &bp, &hp);
            svf_run(&E.mf_hp[0], lp, ghp, k, &lp, &bp, &hp);
            L[i] += (hp - L[i]) * wet;
            svf_run(&E.mf_lp[1], R[i], glp, k, &lp, &bp, &hp);
            svf_run(&E.mf_hp[1], lp, ghp, k, &lp, &bp, &hp);
            R[i] += (hp - R[i]) * wet;
        }
    }
    float vol = (float)E.gparam[G_VOLUME] * 0.01f;
    vol = vol * vol * 0.5f;                                    /* (headroom: the limiter only catches the odd peak) */
    for (int i = 0; i < n; i++) {
        L[i] *= vol;
        R[i] *= vol;
    }
    td_comp_process(L, R, n, (float)E.gparam[G_COMP] * (1.f / 127.f));
    td_limiter_process(L, R, n);
    float pl = E.peak_l * mdecay, pr = E.peak_r * mdecay;
    for (int i = 0; i < n; i++) {
        out[2 * i] = L[i];
        out[2 * i + 1] = R[i];
        if (fabsf(L[i]) > pl) pl = fabsf(L[i]);
        if (fabsf(R[i]) > pr) pr = fabsf(R[i]);
    }
    E.peak_l = pl;
    E.peak_r = pr;
}

/* ---- one block: events at their frames, segments between them */
static int ev_cmp(const td_event_t *a, int oa, const td_event_t *b, int ob)
{
    if (oa != ob) return oa < ob ? -1 : 1;
    if (a->type != b->type) return a->type < b->type ? -1 : 1;
    return a->t < b->t ? -1 : (a->t > b->t ? 1 : 0);
}
static void render_block(float *out)
{
    td_event_t ev[TD_EVQ + 64];
    int off[TD_EVQ + 64], nev = 0;
    double rate = E.playing ? td_step_rate() : 0.0;
    double g0 = E.gpos;
    int64_t f0 = (int64_t)E.frames_int;
    E.dly.hold_t = E.dubhold_t;
    td_seq_block(g0, rate, ev, off, &nev, TD_EVQ + 64);
    for (int i = 1; i < nev; i++) {                            /* insertion sort: by frame, then by kind */
        td_event_t e = ev[i];
        int o = off[i], j = i - 1;
        while (j >= 0 && ev_cmp(&ev[j], off[j], &e, o) > 0) {
            ev[j + 1] = ev[j];
            off[j + 1] = off[j];
            j--;
        }
        ev[j + 1] = e;
        off[j + 1] = o;
    }
    int s = 0, i = 0;
    while (s < TD_BLK) {
        while (i < nev && off[i] <= s) {
            td_seq_apply(&ev[i], f0 + off[i]);
            i++;
        }
        int e = i < nev ? off[i] : TD_BLK;
        if (e > s) {
            render_segment(out + 2 * s, e - s);
            s = e;
        }
    }
    if (E.playing)
        E.gpos = g0 + TD_BLK * rate;
    E.frames_int += TD_BLK;
    for (int t = 0; t < TD_TRACKS; t++) {                      /* MIDI note offs due in this block */
        td_track_t *tr = &E.trk[t];
        if (tr->midi_note >= 0 && tr->midi_off >= 0 && tr->midi_off < f0 + TD_BLK) {
            td_midi_push((uint8_t)(0x80 | t), (uint8_t)tr->midi_note, 0, 3, tr->midi_off > f0 ? tr->midi_off : f0);
            tr->midi_note = -1;
        }
    }
    td_auto_block();
}

static void snap_gains(void)
{
    for (int t = 0; t < TD_TRACKS; t++) {
        td_track_t *tr = &E.trk[t];
        td_track_eff(t);
        const float *e = tr->eff;
        float lv = e[P_LEVEL] * 0.01f, pan = clampf(e[P_PAN] / 64.f, -1.f, 1.f);
        float d1 = e[P_DLY] / 127.f, d2 = e[P_REV] / 127.f, d3 = e[P_CHO] / 127.f;
        tr->g_level = lv * lv;
        tr->g_mute = td_eff_muted(t) ? 0.f : 1.f;
        tr->g_l = pan <= 0.f ? 1.f : 1.f - pan;
        tr->g_r = pan >= 0.f ? 1.f : 1.f + pan;
        tr->g_dly = d1 * d1;
        tr->g_rev = d2 * d2;
        tr->g_cho = d3 * d3;
        tr->g_duck = e[P_DUCK] * (0.9f / 127.f);
        tr->g_width = e[P_WIDTH] * (0.4f / 127.f);
    }
}

int td_init(void)
{
    td_lock();
    td_dsp_tables();
    memset(&E, 0, sizeof E);
    memset(td_ring, 0, sizeof td_ring);
    memset(g_dly_buf, 0, sizeof g_dly_buf);
    memset(g_thr_buf, 0, sizeof g_thr_buf);
    memset(g_perf, 0, sizeof g_perf);
    E.bpm = 122.f;
    E.swing = 54;
    E.queued = -1;
    E.xf = E.xf_s = 0.5f;
    E.rng = 0x2545F491u;
    E.midi_clock = 1;
    E.midi_notes = 0;
    E.scale_root = 9;                                          /* A minor */
    E.scale = 0;
    E.thr_d_target = E.thr_r_target = TD_TARGET_MASTER;
    E.thr_d_div = 2;
    for (int t = 0; t < TD_TRACKS; t++) {
        E.trk[t].midi_note = -1;
        E.trk[t].midi_off = -1;
        E.trk[t].lfo_rng = 0x51ED27u + (uint32_t)t * 7919u;
        for (int v = 0; v < TD_VOICES; v++)
            E.trk[t].v[v].rng = 0x1234567u + (uint32_t)(t * TD_VOICES + v) * 104729u;
    }
    for (int d = 0; d < TD_DECKS; d++) {
        E.deck[d].level_t = 1.f;
        E.deck[d].level_s = 1.f;
    }
    E.dly.buf = g_dly_buf;
    E.dly.rng = 0xDE1A7u;
    E.thr.buf = g_thr_buf;
    E.thr.rng = 0x7A9Eu;
    for (int s = 0; s < TD_SLOTS; s++)
        E.gparam[s] = (int16_t)td_gdef(s);
    td_fx_reset();
    td_samples_init();
    td_samples_builtin();
    td_default_project();
    E.dec_pat = E.cur;
    E.inited = 1;
    snap_gains();
    td_unlock();
    return 0;
}

void td_project_new(void)
{
    td_play(0);
    td_lock();
    if (E.inited) {
        for (int t = 0; t < TD_TRACKS; t++)
            for (int i = 0; i < TD_VOICES; i++) td_voice_fade(&E.trk[t].v[i], 5.f);
        td_default_project();
        memset(E.muted, 0, sizeof E.muted);
        E.queued = -1;
        E.dec_pat = E.cur;
    }
    td_unlock();
}

void td_render(float *buf, uint32_t frames, uint64_t host_time)
{
    if (!buf || !frames)
        return;
    FTZ_ON();
    td_lock();
    if (!E.inited) {
        td_unlock();
        memset(buf, 0, sizeof(float) * 2u * frames);
        FTZ_OFF();
        return;
    }
    E.anchor = (int64_t)E.frames_out;
    E.anchor_host = host_time;
    uint32_t done = 0;
    while (done < frames) {
        if (E.fifo_rd >= E.fifo_n) {
            render_block(E.fifo);
            E.fifo_rd = 0;
            E.fifo_n = TD_BLK;
        }
        uint32_t k = frames - done, have = (uint32_t)(E.fifo_n - E.fifo_rd);
        if (k > have) k = have;
        memcpy(buf + 2u * done, E.fifo + 2 * E.fifo_rd, sizeof(float) * 2u * k);
        E.fifo_rd += (int)k;
        done += k;
        if (done < frames) {                                   /* other threads get the lock between blocks */
            td_unlock();
            td_lock();
        }
    }
    E.frames_out += frames;
    td_unlock();
    FTZ_OFF();
}

float td_render_offline(uint32_t frames)
{
    static float buf[2 * 512];
    float peak = 0.f;
    while (frames) {
        uint32_t k = frames > 512u ? 512u : frames;
        td_render(buf, k, 0);
        for (uint32_t i = 0; i < 2u * k; i++)
            if (fabsf(buf[i]) > peak) peak = fabsf(buf[i]);
        frames -= k;
    }
    return peak;
}

/* ---- transport and global */
void td_play(int on)
{
    td_lock();
    if (E.inited) {
        if (on && !E.playing) {
            if (E.queued >= 0) { E.cur = E.queued; E.queued = -1; td_pattern_tempo(); }
            td_seq_reset_transport();
            E.playing = 1;
            if (E.midi_clock) td_midi_push(0xFA, 0, 0, 1, (int64_t)E.frames_int);
        } else if (!on && E.playing) {
            E.playing = 0;
            td_seq_stop_notes();
            if (E.midi_clock) td_midi_push(0xFC, 0, 0, 1, (int64_t)E.frames_int);
        }
    }
    td_unlock();
}
void td_restart(void)
{
    td_lock();
    if (E.inited) {
        td_seq_reset_transport();
        if (E.playing && E.midi_clock) td_midi_push(0xFA, 0, 0, 1, (int64_t)E.frames_int);
    }
    td_unlock();
}
void td_set_tempo(float bpm)
{
    td_lock();
    if (bpm == bpm) {
        E.bpm = clampf(bpm, 30.f, 300.f);
        if (E.pat[E.cur].bpm10) E.pat[E.cur].bpm10 = (uint16_t)lroundf(E.bpm * 10.f);   /* (it edits the pattern's own) */
    }
    td_unlock();
}
static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
void td_tap_tempo(void)
{
    double t = now_s();
    td_lock();
    double dt = t - E.tap_last;
    if (E.tap_last > 0.0 && dt > 0.15 && dt < 2.0) {
        memmove(&E.tap_iv[1], &E.tap_iv[0], sizeof(double) * 3);
        E.tap_iv[0] = dt;
        if (E.ntap < 4) E.ntap++;
        double s = 0;
        for (int i = 0; i < E.ntap; i++) s += E.tap_iv[i];
        E.bpm = clampf((float)(60.0 / (s / E.ntap)), 30.f, 300.f);
    } else
        E.ntap = 0;
    E.tap_last = t;
    td_unlock();
}
void td_nudge(float amount)
{
    td_lock();
    E.nudge = amount == amount ? clampf(amount, -1.f, 1.f) : 0.f;
    td_unlock();
}
void td_set_swing(int percent)
{
    td_lock();
    E.swing = clampi(percent, 50, 80);
    td_unlock();
}
void td_global_set(int slot, int value)
{
    td_lock();
    if (slot >= 0 && slot < TD_SLOTS) {
        const td_pdesc_t *d = td_gdesc(slot);
        E.gparam[slot] = (int16_t)clampi(value, d->min, d->max);
    }
    td_unlock();
}
int td_global_get(int slot)
{
    td_lock();
    int v = slot >= 0 && slot < TD_SLOTS ? E.gparam[slot] : 0;
    td_unlock();
    return v;
}
void td_fill(int on)
{
    td_lock();
    E.fill_user = on != 0;
    td_unlock();
}
void td_record(int on)
{
    td_lock();
    E.recording = on != 0;
    td_unlock();
}
void td_midi_config(int clock, int notes)
{
    td_lock();
    E.midi_clock = clock != 0;
    E.midi_notes = notes != 0;
    td_unlock();
}
int td_midi_out(uint8_t b[3], int *n, int64_t *frames_after_anchor, uint64_t *anchor_host_time)
{
    int r = 0;
    td_lock();
    if (E.mq_n > 0) {
        const td_midimsg_t *m = &E.mq[E.mq_rd];
        if (b) { b[0] = m->b[0]; b[1] = m->b[1]; b[2] = m->b[2]; }
        if (n) *n = m->n;
        if (frames_after_anchor) *frames_after_anchor = m->frame - E.anchor;
        if (anchor_host_time) *anchor_host_time = E.anchor_host;
        E.mq_rd = (E.mq_rd + 1) % TD_MIDI_Q;
        E.mq_n--;
        r = 1;
    }
    td_unlock();
    return r;
}

/* ---- tracks */
static int ok_t(int t) { return t >= 0 && t < TD_TRACKS && E.inited; }
void td_set_machine(int track, int machine)
{
    td_lock();
    if (ok_t(track) && machine >= 0 && machine < TD_MACHINES && E.pat[E.cur].machine[track] != machine) {
        for (int i = 0; i < TD_VOICES; i++)
            td_voice_fade(&E.trk[track].v[i], 5.f);
        td_set_machine_x(&E.pat[E.cur], track, machine);
        E.trk[track].lk_mask = 0;
    }
    td_unlock();
}
void td_set_sample(int track, int sample_slot)
{
    td_lock();
    if (ok_t(track)) E.pat[E.cur].param[track][P_SRC + SM_SLOT] = (int16_t)clampi(sample_slot, 0, TD_SAMPLE_SLOTS - 1);
    td_unlock();
}
void td_param_set(int track, int param, int value)
{
    td_lock();
    if (ok_t(track) && param >= 0 && param < TD_NPARAMS) {
        const td_pdesc_t *d = td_pdesc(E.pat[E.cur].machine[track], param);
        E.pat[E.cur].param[track][param] = (int16_t)clampi(value, d->min, d->max);
    }
    td_unlock();
}
int td_param_get(int track, int param)
{
    td_lock();
    int v = ok_t(track) && param >= 0 && param < TD_NPARAMS ? E.pat[E.cur].param[track][param] : 0;
    td_unlock();
    return v;
}
int td_param_live(int track, int param)
{
    td_lock();
    int v = 0;
    if (ok_t(track) && param >= 0 && param < TD_NPARAMS) {
        float f = td_param_live_f(track, param);
        v = (int)lroundf(f);
    }
    td_unlock();
    return v;
}
void td_mute(int track, int on)
{
    td_lock();
    if (ok_t(track)) {
        E.muted[track] = on != 0;
        E.ap.override[track] = 1;                              /* the user's word stands for the rest of the phrase */
    }
    td_unlock();
}
void td_set_length(int track, int steps)
{
    td_lock();
    if (ok_t(track)) E.pat[E.cur].length[track] = (uint8_t)clampi(steps, 1, TD_STEPS);
    td_unlock();
}
void td_set_length_repeat(int track, int steps)
{
    td_lock();
    if (ok_t(track)) {
        td_pattern_t *P = &E.pat[E.cur];
        int old = P->length[track] ? P->length[track] : 16, n = clampi(steps, 1, TD_STEPS);
        for (int s = old; s < n; s++) {                         /* the new steps repeat the old ones (with their locks) */
            P->step[track][s] = P->step[track][s % old];
            memcpy(P->lock[track][s], P->lock[track][s % old], sizeof P->lock[track][s]);
        }
        P->length[track] = (uint8_t)n;
    }
    td_unlock();
}
void td_trigger(int track, int note, int velocity)
{
    td_lock();
    if (ok_t(track)) {
        note = clampi(note, 0, 127);
        velocity = clampi(velocity, 1, 127);
        td_track_eff(track);
        float x = velocity / 127.f;
        td_note_on(track, note, x * sqrtf(x), -1, NF_LIVE);
        if (track == 0) E.duck_att = 1;
        if (E.recording && E.playing) {                        /* live recording: the nearest step */
            td_pattern_t *P = &E.pat[E.cur];
            int len = P->length[track];
            int64_t k = (int64_t)floor(E.gpos - (double)E.cur_origin + 0.5);
            int s = (int)(((k % len) + len) % len);
            td_step_t *st = &P->step[track][s];
            if (!st->trig) {
                const int16_t *p = P->param[track];
                memset(st, 0, sizeof *st);
                st->cond = (uint8_t)clampi(p[P_COND], 0, TD_CONDS - 1);
                st->rrate = (uint8_t)clampi(p[P_RRATE], 0, 3);
                st->trig = 1;
            }
            st->note = (uint8_t)note;
            st->vel = (uint8_t)velocity;
        }
    }
    td_unlock();
}
void td_release(int track, int note)
{
    td_lock();
    if (ok_t(track))
        for (int i = 0; i < TD_VOICES; i++) {
            td_voice_t *v = &E.trk[track].v[i];
            if (v->active && v->live_note == note && v->gated)
                td_voice_gate_off(v);
        }
    td_unlock();
}

/* ---- patterns */
void td_pattern_queue(int pattern)
{
    td_lock();
    if (E.inited && pattern >= 0 && pattern < TD_PATTERNS) {
        if (!E.playing) {
            E.cur = E.dec_pat = pattern;
            E.queued = -1;
            td_pattern_tempo();
        } else
            E.queued = pattern;
    }
    td_unlock();
}
void td_pattern_copy(int from, int to)
{
    td_lock();
    if (E.inited && from >= 0 && from < TD_PATTERNS && to >= 0 && to < TD_PATTERNS && from != to)
        E.pat[to] = E.pat[from];
    td_unlock();
}
void td_pattern_clear(int pattern)
{
    td_lock();
    if (E.inited && pattern >= 0 && pattern < TD_PATTERNS) {
        td_pattern_t *P = &E.pat[pattern];
        memset(P->step, 0, sizeof P->step);
        memset(P->lock, 0, sizeof P->lock);
        P->nscene[0] = P->nscene[1] = 0;
        for (int t = 0; t < TD_TRACKS; t++) P->length[t] = 16;
    }
    td_unlock();
}

/* ---- scenes */
void td_scene_set(int scene, int track, int param, int value)
{
    td_lock();
    if (E.inited && (scene == 0 || scene == 1) && track >= 0 && track < TD_TRACKS && param >= 0 && param < TD_NPARAMS) {
        td_pattern_t *P = &E.pat[E.cur];
        const td_pdesc_t *d = td_pdesc(P->machine[track], param);
        int v = clampi(value, d->min, d->max), i;
        for (i = 0; i < P->nscene[scene]; i++)
            if (P->scene[scene][i].track == track && P->scene[scene][i].param == param) break;
        if (i < P->nscene[scene] || P->nscene[scene] < TD_SCENE_MAX) {
            P->scene[scene][i].track = (uint8_t)track;
            P->scene[scene][i].param = (uint8_t)param;
            P->scene[scene][i].value = (int16_t)v;
            if (i == P->nscene[scene]) P->nscene[scene]++;
        }
    }
    td_unlock();
}
void td_scene_unset(int scene, int track, int param)
{
    td_lock();
    if (E.inited && (scene == 0 || scene == 1)) {
        td_pattern_t *P = &E.pat[E.cur];
        for (int i = 0; i < P->nscene[scene]; i++)
            if (P->scene[scene][i].track == track && P->scene[scene][i].param == param) {
                P->scene[scene][i] = P->scene[scene][--P->nscene[scene]];
                break;
            }
    }
    td_unlock();
}
void td_scene_clear(int scene)
{
    td_lock();
    if (E.inited && (scene == 0 || scene == 1)) E.pat[E.cur].nscene[scene] = 0;
    td_unlock();
}
int td_scene_get(int scene, int track, int param, int *value)
{
    int r = 0;
    td_lock();
    if (E.inited && (scene == 0 || scene == 1)) {
        const td_pattern_t *P = &E.pat[E.cur];
        for (int i = 0; i < P->nscene[scene]; i++)
            if (P->scene[scene][i].track == track && P->scene[scene][i].param == param) {
                if (value) *value = P->scene[scene][i].value;
                r = 1;
                break;
            }
    }
    td_unlock();
    return r;
}
void td_crossfader(float x)
{
    td_lock();
    E.xf = x == x ? clampf(x, 0.f, 1.f) : 0.5f;
    td_unlock();
}

/* ---- decks */
void td_deck_level(int deck, float level)
{
    td_lock();
    if (deck >= 0 && deck < TD_DECKS) E.deck[deck].level_t = level == level ? clampf(level, 0.f, 1.f) : 1.f;
    td_unlock();
}
void td_deck_drive(int deck, float drive)
{
    td_lock();
    if (deck >= 0 && deck < TD_DECKS) E.deck[deck].drive_t = drive == drive ? clampf(drive, 0.f, 1.f) : 0.f;
    td_unlock();
}
void td_scratch(int deck, int touching, float rate)
{
    td_lock();
    if (E.inited && deck >= 0 && deck < TD_DECKS) {
        td_deck_t *k = &E.deck[deck];
        if (touching && !k->touching && k->mix <= 0.f) {      /* a new grab: the head is where the sound is now */
            k->head = (double)k->wabs - 4.0;
            k->rate_s = 1.f;
            k->sgain = 1.f;
        }
        k->touching = touching != 0;
        if (rate == rate) k->rate_t = clampf(rate, -4.f, 4.f);
    }
    td_unlock();
}

/* ---- performance FX */
void td_perf(int perf, int division, int target, int on, float amount)
{
    td_lock();
    if (E.inited && perf >= 0 && perf < TD_PERFS) {
        division = clampi(division, 0, TD_PERF_DIVS - 1);
        target = clampi(target, 0, 2);
        amount = amount == amount ? clampf(amount, 0.f, 1.f) : 0.5f;
        td_insert_t *u = &E.ins;
        switch (perf) {
        case TD_PERF_DELAYTHROW:
            if (on) {
                E.thr_d_t = 1.f;
                E.thr_d_target = target;
                E.thr_d_div = division;
                E.thr_d_amount = amount;
            } else
                E.thr_d_t = 0.f;
            break;
        case TD_PERF_REVERBTHROW:
            if (on) {
                E.thr_r_t = 0.5f + amount;
                E.thr_r_target = target;
            } else
                E.thr_r_t = 0.f;
            break;
        case TD_PERF_DUBHOLD:
            E.dubhold_t = on ? 1.f : 0.f;
            break;
        default:
            if (on) {
                u->r_on = 1;
                u->r_type = perf;
                u->r_target = target;
                u->r_div = division;
                u->r_amount = amount;
            } else if (u->r_type == perf)
                u->r_on = 0;
            break;
        }
        if (on) {
            E.perf_last_type = perf;
            E.perf_last_target = target;
        }
    }
    td_unlock();
}

/* ---- state */
void td_get_state(td_state_t *s)
{
    if (!s) return;
    memset(s, 0, sizeof *s);
    td_lock();
    if (E.inited) {
        const td_pattern_t *P = &E.pat[E.cur];
        s->playing = E.playing;
        s->recording = E.recording;
        s->fill = E.fill_user || (E.ap.on && E.ap.fill);
        s->pattern = E.cur;
        s->queued_pattern = E.queued;
        s->bpm = E.bpm;
        s->swing = E.swing;
        int64_t kp = (int64_t)floor(E.gpos - (double)E.cur_origin);
        for (int t = 0; t < TD_TRACKS; t++) {
            int len = P->length[t] ? P->length[t] : 16;
            s->step[t] = E.playing && kp >= 0 ? (int32_t)(kp % len) : -1;
            s->machine[t] = P->machine[t];
            s->muted[t] = td_eff_muted(t);
            s->length[t] = len;
            s->level[t] = fminf(1.f, E.trk[t].peak);
        }
        for (int d = 0; d < TD_DECKS; d++) {
            s->deck_level[d] = fminf(1.f, E.deck[d].meter);
            s->scratching[d] = E.deck[d].touching || E.deck[d].mix > 0.f;
        }
        s->crossfader = E.xf;
        int ins_on = E.ins.active && !E.ins.releasing;
        s->perf_active = ins_on || E.thr_d_t > 0.f || E.thr_r_t > 0.f || E.dubhold_t > 0.f;
        s->perf_type = E.perf_last_type;
        s->perf_target = E.perf_last_target;
        s->peak_l = fminf(1.f, E.peak_l);
        s->peak_r = fminf(1.f, E.peak_r);
        int64_t g = (int64_t)floor(E.gpos);
        s->bar = (uint32_t)(g / 16 + 1);
        s->beat = (uint32_t)((g % 16) / 4 + 1);
        s->autopilot = E.ap.on;
        s->phrase_bar = E.playing ? (int32_t)((g / 16) % 8 + 1) : 1;
        s->scale_root = E.scale_root;
        s->scale = E.scale;
        s->transpose = E.ap.on ? E.ap.transpose : 0;
    }
    td_unlock();
}

int td_capture(int deck, int bars, int slot)
{
    td_lock();
    int r = E.inited ? x_capture(deck, bars, slot) : -1;
    td_unlock();
    return r;
}
