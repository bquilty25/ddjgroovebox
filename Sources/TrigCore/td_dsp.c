/* SPDX-License-Identifier: GPL-3.0-only */
/* TRIGDECK shared DSP: lookup tables and the master effects.
 *   reverb      an 8-line feedback delay network: pre-delay, input band-limit, four Schroeder all-pass diffusers, eight
 *               coprime lines mixed by a Hadamard matrix and damped in the loop, two lines slowly modulated so long
 *               tails do not ring metallic. After the 4-line FDN in SLOOP's fx.c (vendor/sloop, GPL-3.0, by Leo
 *               Kuroshita / Hugelton Instruments and isod89), widened to 8 lines and floating point.
 *   tape delay  stereo ping-pong with tape character: wow, flutter and a slow drift on the read head, a low-pass and
 *               high-pass and a soft saturator in the feedback (so feedback above 100 % self-oscillates but stays
 *               bounded), the time gliding like a tape machine when it changes. DUB HOLD turns the loop into an exact
 *               copy (input off, no filtering, no modulation, an integer delay): the buffer repeats, neither growing
 *               nor fading.
 *   chorus      two modulated taps in quadrature
 *   compressor  stereo-linked peak compressor with a soft knee
 *   limiter     1 ms lookahead peak limiter: a sliding minimum of the gain each sample needs, released slowly, then
 *               smoothed by a box filter as long as the lookahead, so the gain is already down when the peak leaves
 *               the delay: the output never exceeds the ceiling. */
#include "td_internal.h"

float td_sin_tab[4097];
float td_cut_g[1025];

void td_dsp_tables(void)
{
    for (int i = 0; i <= 4096; i++)
        td_sin_tab[i] = sinf(6.28318530718f * (float)i / 4096.f);
    td_sin_tab[4096] = td_sin_tab[0];
    for (int i = 0; i <= 1024; i++)
        td_cut_g[i] = svf_g(map_cut_hz((float)i * (127.f / 1024.f)));
}

/* ---- reverb */
#define RV_MASK 4095u
static const uint32_t RV_LEN[8] = {1597, 1871, 2141, 2423, 2713, 2999, 3299, 3613};
static const uint32_t RV_AP[4] = {142, 107, 379, 277};
static float rv_line[8][4096];
static float rv_ap[4][512];
static float rv_pre[1024];
static float rv_lp[8], rv_inlp, rv_inhp, rv_ph[2];
static uint32_t rv_w;

/* ---- chorus */
static float ch_buf[2048];
static uint32_t ch_w;
static float ch_ph;

void td_fx_reset(void)
{
    memset(rv_line, 0, sizeof rv_line);
    memset(rv_ap, 0, sizeof rv_ap);
    memset(rv_pre, 0, sizeof rv_pre);
    memset(rv_lp, 0, sizeof rv_lp);
    rv_inlp = rv_inhp = 0;
    rv_ph[0] = 0;
    rv_ph[1] = 0.37f;
    rv_w = 0;
    memset(ch_buf, 0, sizeof ch_buf);
    ch_w = 0;
    ch_ph = 0;
    E.comp_env = 0;
    E.comp_g = 1.f;
    memset(E.lim_dl, 0, sizeof E.lim_dl);
    for (int i = 0; i < 64; i++)
        E.lim_box[i] = 1.f;
    E.lim_sum = 48.0;
    E.lim_r = 1.f;
    E.lim_dq_h = E.lim_dq_n = 0;
    E.lim_n = 0;
    E.lim_pos = 0;
}

void td_reverb_process(const float *inl, const float *inr, float *outl, float *outr, int n, float size, float damp)
{
    float rt60 = 0.5f * exp2f(size * (4.6f / 127.f));            /* 0.5 .. 12 s */
    float g[8];
    for (int i = 0; i < 8; i++)
        g[i] = powf(10.f, -3.f * (float)RV_LEN[i] / (rt60 * TD_SR));
    float da = onepole_a(18000.f * exp2f(-damp * (3.6f / 127.f)));   /* in-loop damping: 18 kHz .. 1.5 kHz */
    float hpa = onepole_a(140.f), lpa = onepole_a(9000.f);
    const float apg = 0.62f;
    float mi0 = 0.37f / TD_SR, mi1 = 0.53f / TD_SR;
    for (int s = 0; s < n; s++) {
        float x = (inl[s] + inr[s]) * 0.5f;
        rv_pre[rv_w & 1023u] = x;
        x = rv_pre[(rv_w - 720u) & 1023u];                       /* 15 ms pre-delay */
        rv_inhp += (x - rv_inhp) * hpa;
        x -= rv_inhp;
        rv_inlp += (x - rv_inlp) * lpa;
        x = rv_inlp;
        for (int a = 0; a < 4; a++) {                            /* diffusers */
            float d = rv_ap[a][(rv_w - RV_AP[a]) & 511u];
            float v = x - apg * d;
            rv_ap[a][rv_w & 511u] = v;
            x = d + apg * v;
        }
        float y[8];
        for (int i = 0; i < 8; i++)
            y[i] = rv_line[i][(rv_w - RV_LEN[i]) & RV_MASK];
        {   /* two modulated lines (fractional reads) */
            rv_ph[0] += mi0;
            rv_ph[1] += mi1;
            if (rv_ph[0] >= 1.f) rv_ph[0] -= 1.f;
            if (rv_ph[1] >= 1.f) rv_ph[1] -= 1.f;
            float d0 = (float)RV_LEN[0] + 5.f + 4.f * sin01(rv_ph[0]);
            float d4 = (float)RV_LEN[4] + 5.f + 4.f * sin01(rv_ph[1]);
            int i0 = (int)d0, i4 = (int)d4;
            float f0 = d0 - (float)i0, f4 = d4 - (float)i4;
            y[0] = rv_line[0][(rv_w - (uint32_t)i0) & RV_MASK] * (1.f - f0) + rv_line[0][(rv_w - (uint32_t)i0 - 1u) & RV_MASK] * f0;
            y[4] = rv_line[4][(rv_w - (uint32_t)i4) & RV_MASK] * (1.f - f4) + rv_line[4][(rv_w - (uint32_t)i4 - 1u) & RV_MASK] * f4;
        }
        float z[8];
        for (int i = 0; i < 8; i++) {
            rv_lp[i] += (y[i] - rv_lp[i]) * da;
            z[i] = rv_lp[i] * g[i];
        }
        float s0 = z[0] + z[1], d0 = z[0] - z[1], s1 = z[2] + z[3], d1 = z[2] - z[3];
        float s2 = z[4] + z[5], d2 = z[4] - z[5], s3 = z[6] + z[7], d3 = z[6] - z[7];
        float t0 = s0 + s1, t1 = d0 + d1, t2 = s0 - s1, t3 = d0 - d1;
        float t4 = s2 + s3, t5 = d2 + d3, t6 = s2 - s3, t7 = d2 - d3;
        const float h = 0.35355339f;
        float in = x * 0.5f;
        rv_line[0][rv_w & RV_MASK] = (t0 + t4) * h + in;
        rv_line[1][rv_w & RV_MASK] = (t1 + t5) * h - in;
        rv_line[2][rv_w & RV_MASK] = (t2 + t6) * h + in;
        rv_line[3][rv_w & RV_MASK] = (t3 + t7) * h - in;
        rv_line[4][rv_w & RV_MASK] = (t0 - t4) * h + in;
        rv_line[5][rv_w & RV_MASK] = (t1 - t5) * h - in;
        rv_line[6][rv_w & RV_MASK] = (t2 - t6) * h + in;
        rv_line[7][rv_w & RV_MASK] = (t3 - t7) * h - in;
        rv_w++;
        outl[s] = 0.42f * (y[0] - y[3] + y[5] + y[6]);
        outr[s] = 0.42f * (y[1] + y[2] - y[4] + y[7]);
    }
}

void td_chorus_process(const float *inl, const float *inr, float *outl, float *outr, int n)
{
    float inc = 0.35f / TD_SR;
    for (int s = 0; s < n; s++) {
        ch_buf[ch_w & 2047u] = (inl[s] + inr[s]) * 0.5f;
        ch_ph += inc;
        if (ch_ph >= 1.f) ch_ph -= 1.f;
        float dl = 480.f + 160.f * sin01(ch_ph), dr = 480.f + 160.f * sin01(ch_ph + 0.25f);
        int il = (int)dl, ir = (int)dr;
        float fl = dl - (float)il, fr = dr - (float)ir;
        outl[s] = ch_buf[(ch_w - (uint32_t)il) & 2047u] * (1.f - fl) + ch_buf[(ch_w - (uint32_t)il - 1u) & 2047u] * fl;
        outr[s] = ch_buf[(ch_w - (uint32_t)ir) & 2047u] * (1.f - fr) + ch_buf[(ch_w - (uint32_t)ir - 1u) & 2047u] * fr;
        ch_w++;
    }
}

static inline float dly_read(const float *buf, uint32_t w, float d, int ch)
{
    int i = (int)d;
    float f = d - (float)i;
    float a = buf[((w - (uint32_t)i) & TD_DLY_MASK) * 2u + (uint32_t)ch];
    float b = buf[((w - (uint32_t)i - 1u) & TD_DLY_MASK) * 2u + (uint32_t)ch];
    return a + (b - a) * f;
}

void td_tape_process(td_tape_t *d, const float *inl, const float *inr, float *outl, float *outr, int n,
                     float time_frames, float fb, float tone_hz, int tape)
{
    float lpa = onepole_a(tone_hz), hpa = onepole_a(tape ? 90.f : 160.f);
    time_frames = clampf(time_frames, 32.f, (float)(TD_DLY - 4096u));
    if (d->time_s < 32.f)
        d->time_s = time_frames;
    for (int s = 0; s < n; s++) {
        if (d->hold != d->hold_t) {
            if (d->hold == 0.f)                                   /* holding from now: the loop is the current time */
                d->hold_time = floorf(d->time_s + 0.5f);
            d->hold += d->hold_t > d->hold ? 1.f / 480.f : -1.f / 480.f;
            if (fabsf(d->hold - d->hold_t) < 1.f / 960.f)
                d->hold = d->hold_t;
            d->hold = clampf(d->hold, 0.f, 1.f);
        }
        if (d->hold == 0.f)
            d->time_s += (time_frames - d->time_s) * 0.00025f;    /* tape-like glide to a new time */
        float mod = 0.f;
        if (tape) {
            d->wow_ph += 0.53f / TD_SR;
            d->flut_ph += 6.3f / TD_SR;
            if (d->wow_ph >= 1.f) d->wow_ph -= 1.f;
            if (d->flut_ph >= 1.f) {
                d->flut_ph -= 1.f;
                d->drift_t = td_noise(&d->rng) * 9.f;             /* a new slow drift target about 6 times a second */
            }
            d->drift += (d->drift_t - d->drift) * 0.00004f;
            mod = 18.f * sin01(d->wow_ph) + 1.4f * sin01(d->flut_ph) + d->drift;
        }
        float dl = d->time_s + 24.f + mod;                        /* (+24: never closer than the modulation) */
        if (d->hold > 0.f)
            dl = dl + (d->hold_time - dl) * d->hold;
        float rl = dly_read(d->buf, d->w, dl, 0), rr = dly_read(d->buf, d->w, dl, 1);
        /* ping-pong feedback: high-pass, low-pass, saturation */
        float xl = rr, xr = rl;
        d->hpl += (xl - d->hpl) * hpa;
        d->hpr += (xr - d->hpr) * hpa;
        xl -= d->hpl;
        xr -= d->hpr;
        d->lpl += (xl - d->lpl) * lpa;
        d->lpr += (xr - d->lpr) * lpa;
        xl = softclip(d->lpl * fb);
        xr = softclip(d->lpr * fb);
        float in = (inl[s] + inr[s]) * 0.5f * (1.f - d->hold);
        float wl = softclip(in + xl), wr = xr;
        if (d->hold > 0.f) {                                      /* DUB HOLD: the read goes back in unchanged */
            wl += (rl - wl) * d->hold;
            wr += (rr - wr) * d->hold;
        }
        uint32_t wi = (d->w & TD_DLY_MASK) * 2u;
        d->buf[wi] = wl + 1e-20f;
        d->buf[wi + 1u] = wr + 1e-20f;
        d->w++;
        outl[s] = rl;
        outr[s] = rr;
    }
}

void td_comp_process(float *l, float *r, int n, float amount)
{
    float a = clampf(amount, 0.f, 1.f);
    float thr = -6.f - 20.f * a, ratio = 1.f + 5.f * a, makeup = 7.f * a;
    float slope = 1.f - 1.f / ratio;
    const float att = 0.0069f, rel = 0.000139f;                  /* 3 ms attack, 150 ms release */
    for (int i = 0; i < n; i++) {
        float lv = fmaxf(fabsf(l[i]), fabsf(r[i]));
        E.comp_env += (lv - E.comp_env) * (lv > E.comp_env ? att : rel);
        float gt = 1.f;
        if (a > 0.f) {
            float edb = 6.0206f * fast_log2(E.comp_env + 1e-9f);
            float over = edb - thr, red;
            if (over <= -3.f) red = 0.f;
            else if (over < 3.f) red = slope * (over + 3.f) * (over + 3.f) * (1.f / 12.f);
            else red = slope * over;
            gt = fast_exp2((makeup - red) * (1.f / 6.0206f));
        }
        E.comp_g += (gt - E.comp_g) * 0.05f;
        if (fabsf(E.comp_g - 1.f) < 1e-6f && gt == 1.f)
            E.comp_g = 1.f;
        l[i] *= E.comp_g;
        r[i] *= E.comp_g;
    }
}

#define LIM_W 48
void td_limiter_process(float *l, float *r, int n)
{
    const float ceil = 0.966f;
    const float krel = 1.f - 0.99974f;                           /* ~80 ms release */
    for (int i = 0; i < n; i++) {
        float xl = l[i], xr = r[i];
        if (!(xl == xl) || fabsf(xl) > 1e6f) xl = 0.f;           /* (NaN or runaway: silence, never noise) */
        if (!(xr == xr) || fabsf(xr) > 1e6f) xr = 0.f;
        float a = fmaxf(fabsf(xl), fabsf(xr));
        float t = a > ceil ? ceil / a : 1.f;
        while (E.lim_dq_n > 0 && E.lim_dq_v[(E.lim_dq_h + E.lim_dq_n - 1) & 63] >= t)
            E.lim_dq_n--;
        int bi = (E.lim_dq_h + E.lim_dq_n) & 63;
        E.lim_dq_v[bi] = t;
        E.lim_dq_i[bi] = E.lim_n;
        E.lim_dq_n++;
        while (E.lim_dq_i[E.lim_dq_h] <= E.lim_n - (LIM_W + 1)) {
            E.lim_dq_h = (E.lim_dq_h + 1) & 63;
            E.lim_dq_n--;
        }
        float m = E.lim_dq_v[E.lim_dq_h];
        float rr = E.lim_r + (1.f - E.lim_r) * krel;
        E.lim_r = m < rr ? m : rr;
        int p = E.lim_pos;
        E.lim_sum += (double)E.lim_r - (double)E.lim_box[p];
        E.lim_box[p] = E.lim_r;
        float g = (float)(E.lim_sum * (1.0 / LIM_W));
        if (g > 1.f) g = 1.f;
        float yl = E.lim_dl[0][p], yr = E.lim_dl[1][p];
        E.lim_dl[0][p] = xl;
        E.lim_dl[1][p] = xr;
        E.lim_pos = p + 1 >= LIM_W ? 0 : p + 1;
        if ((++E.lim_n & 65535) == 0) {                          /* (re-sum now and then: no drift) */
            double s = 0;
            for (int k = 0; k < LIM_W; k++) s += E.lim_box[k];
            E.lim_sum = s;
        }
        l[i] = clampf(yl * g, -1.f, 1.f);
        r[i] = clampf(yr * g, -1.f, 1.f);
    }
}
