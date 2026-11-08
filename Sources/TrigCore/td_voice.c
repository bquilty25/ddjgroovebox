/* SPDX-License-Identifier: GPL-3.0-only */
/* TRIGDECK machines and the per-voice chain: machine -> drive -> multimode filter (with its AD envelope, key tracking)
 * -> AHDSR amp envelope x velocity -> (fade when retriggered or stolen).
 *
 * The drum models follow the classic analogue circuits, as SLOOP's drum_synth.c does (vendor/sloop, GPL-3.0, isod89),
 * rewritten here in floating point with this instrument's controls:
 *   KICK   a sine whose pitch falls exponentially from SWEEP semitones above to the tuned pitch (SWTIME), held (HOLD)
 *          then decaying (DECAY, T60); a click of bright noise and a short tick; drive; a TONE low-pass.
 *   SNARE  two body modes (1 : 1.63, the second decaying faster) with a SNAP pitch drop, plus high-passed noise with
 *          its own decay and an attack burst; TONE moves the noise band.
 *   HAT    the 808 cymbal: six square oscillators at 205.3, 304.4, 369.6, 522.7, 540 and 800 Hz (stretched by SPREAD),
 *          mixed with noise (COLOR), band-passed (BRIGHT, RESO), high-passed; OPEN lengthens the decay up to x16.
 *   CLAP   band-passed noise in BURSTS separated by SPREAD (jittered by RAND), then a tail (DECAY) and a darker ROOM.
 *   TOM    a sine with a pitch sweep and phase modulation by a 1.5x partial (FM), a noise stick, a TONE low-pass.
 *   SYNTH  two oscillators morphing sine -> triangle -> saw -> square (polyBLEP), DETUNE and INTVL on the second, a
 *          sine SUB an octave down, NOISE, GLIDE, CHORD memory.
 *   FM     two operators: a self-modulated modulator (RATIO, FINE, FDBK) phase-modulating a sine carrier; the index is
 *          INDEX plus ENVAMT times an envelope decaying over ENVDEC; CHORD memory.
 *   SAMPLE a slot played with cubic interpolation at any pitch, from START to END, reversed, looped, or in SLICES
 *          chosen by the note. */
#include "td_internal.h"

/* ---- oscillators */
static inline float polyblep(float t, float dt)
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.f;
    }
    if (t > 1.f - dt) {
        t = (t - 1.f) / dt;
        return t * t + t + t + 1.f;
    }
    return 0.f;
}
static inline float w_tri(float ph) { return ph < 0.25f ? 4.f * ph : (ph < 0.75f ? 2.f - 4.f * ph : 4.f * ph - 4.f); }
static inline float w_saw(float ph, float dt)
{
    float t = ph + 0.5f;
    if (t >= 1.f) t -= 1.f;
    return 2.f * t - 1.f - polyblep(t, dt);
}
static inline float w_sqr(float ph, float dt)
{
    float t2 = ph + 0.5f;
    if (t2 >= 1.f) t2 -= 1.f;
    return (ph < 0.5f ? 1.f : -1.f) + polyblep(ph, dt) - polyblep(t2, dt);
}
static inline float osc_shape(float ph, float dt, float s)   /* s 0..3: sine, triangle, saw, square */
{
    if (s <= 1.f) {
        float a = sin01(ph);
        return s <= 0.f ? a : a + (w_tri(ph) - a) * s;
    }
    if (s <= 2.f) {
        float a = w_tri(ph);
        return a + (w_saw(ph, dt) - a) * (s - 1.f);
    }
    float a = w_saw(ph, dt);
    return a + (w_sqr(ph, dt) - a) * (s - 2.f);
}
static inline float wrap1(float p) { return p >= 1.f ? p - 1.f : p; }

/* ---- KICK */
static void kick_start(td_voice_t *v, const float *e, int note)
{
    td_kick_t *m = &v->m.kick;
    float semi = (float)(note - 60) + e[P_SRC + KK_TUNE];
    m->f0 = clampf(50.f * exp2f(semi / 12.f), 15.f, 2000.f);
    m->sweep = e[P_SRC + KK_SWEEP];
    m->kp = tau_coef(map_tsweep(e[P_SRC + KK_STIME]));
    m->penv = 1.f;
    m->ka = t60_coef(map_tdec(e[P_SRC + KK_DECAY]) + 0.03f);
    m->hold = (int32_t)(map_thold(e[P_SRC + KK_HOLD]) * TD_SR);
    m->aenv = 1.f;
    m->ck = e[P_SRC + KK_CLICK] * (1.f / 127.f);
    m->kck = tau_coef(0.0012f);
    m->ph = m->tph = m->hpz = m->lp = 0.f;
}
static int kick_render(td_voice_t *v, const float *e, float *o, int n)
{
    td_kick_t *m = &v->m.kick;
    float drv = e[P_SRC + KK_DRIVE] * (1.f / 127.f);
    float lpa = onepole_a(200.f * exp2f(e[P_SRC + KK_TONE] * (6.5f / 127.f)));
    for (int i = 0; i < n; i++) {
        m->penv *= m->kp;
        float f = m->f0 * fast_exp2(m->sweep * m->penv * (1.f / 12.f));
        m->ph = wrap1(m->ph + f * (1.f / TD_SR));
        float body = sin01(m->ph);
        if (m->hold > 0) m->hold--;
        else m->aenv *= m->ka;
        float nz = td_noise(&v->rng), hp = nz - m->hpz;
        m->hpz = nz;
        m->tph = wrap1(m->tph + 2600.f / TD_SR);
        float clk = (hp * 0.35f + sin01(m->tph) * 0.55f) * m->ck;
        m->ck *= m->kck;
        float x = drive_sat(body * m->aenv + clk, drv);
        m->lp += (x - m->lp) * lpa;
        o[i] = m->lp * 0.92f;
    }
    return m->aenv > 1e-4f || m->ck > 1e-4f;
}

/* ---- SNARE */
static void snare_start(td_voice_t *v, const float *e, int note)
{
    td_snare_t *m = &v->m.sn;
    float semi = (float)(note - 60) + e[P_SRC + SN_TUNE];
    m->f1 = clampf(180.f * exp2f(semi / 12.f), 40.f, 4000.f);
    m->f2 = m->f1 * 1.63f;
    float snap = e[P_SRC + SN_SNAP] * (1.f / 127.f);
    m->psw = snap * 14.f;
    m->penv = 1.f;
    m->kp = tau_coef(0.006f);
    float dec = map_tdec(e[P_SRC + SN_DECAY]) + 0.02f;
    m->e1 = m->e2 = 1.f;
    m->k1 = t60_coef(dec);
    m->k2 = t60_coef(dec * 0.6f);
    m->body = e[P_SRC + SN_BODY] * (1.f / 127.f);
    m->nlev = e[P_SRC + SN_NOISE] * (1.f / 127.f);
    m->ne = 1.f;
    m->kn = t60_coef(map_tdec(e[P_SRC + SN_NDECAY]) + 0.02f);
    m->nt = snap * 1.2f;
    m->knt = tau_coef(0.004f);
    float fc = 600.f * exp2f(e[P_SRC + SN_TONE] * (3.5f / 127.f));
    m->g = svf_g(fc);
    m->k = 1.3f;
    m->lpa = onepole_a(fminf(16000.f, fc * 3.f + 4000.f));
    m->ph1 = m->ph2 = m->lp = 0.f;
    memset(&m->hp, 0, sizeof m->hp);
}
static int snare_render(td_voice_t *v, const float *e, float *o, int n)
{
    td_snare_t *m = &v->m.sn;
    float drv = e[P_SRC + SN_DRIVE] * (1.f / 127.f);
    for (int i = 0; i < n; i++) {
        m->penv *= m->kp;
        float fm = fast_exp2(m->psw * m->penv * (1.f / 12.f));
        m->ph1 = wrap1(m->ph1 + m->f1 * fm * (1.f / TD_SR));
        m->ph2 = wrap1(m->ph2 + m->f2 * fm * (1.f / TD_SR));
        float body = (sin01(m->ph1) * m->e1 + sin01(m->ph2) * m->e2 * 0.6f) * m->body;
        m->e1 *= m->k1;
        m->e2 *= m->k2;
        float lp, bp, hp;
        svf_run(&m->hp, td_noise(&v->rng), m->g, m->k, &lp, &bp, &hp);
        m->lp += (hp - m->lp) * m->lpa;
        float nz = m->lp * (m->ne * m->nlev + m->nt);
        m->ne *= m->kn;
        m->nt *= m->knt;
        o[i] = drive_sat(body * 0.75f + nz * 0.9f, drv);
    }
    return m->e1 > 1e-4f || m->ne > 1e-4f;
}

/* ---- HAT */
static const float HAT_R[6] = {1.f, 1.4827f, 1.8003f, 2.5460f, 2.6303f, 3.8967f};
static void hat_start(td_voice_t *v, const float *e, int note)
{
    td_hat_t *m = &v->m.hat;
    float semi = (float)(note - 60) + e[P_SRC + HH_TUNE];
    float base = 205.3f * exp2f(semi / 12.f) * 1.6f;
    float spread = 1.f + e[P_SRC + HH_SPREAD] * (0.6f / 127.f);
    for (int i = 0; i < 6; i++) {
        m->inc[i] = fminf(base * powf(HAT_R[i], spread) / TD_SR, 0.45f);
        m->ph[i] = td_rand01(&v->rng);
    }
    m->color = e[P_SRC + HH_COLOR] * (1.f / 127.f);
    float fb = 3000.f * exp2f(e[P_SRC + HH_BRIGHT] * (2.2f / 127.f));
    m->gb = svf_g(fb);
    m->kb = 1.f / (0.7f + e[P_SRC + HH_RESO] * (5.f / 127.f));
    m->gh = svf_g(fb * 0.7f);
    float dec = (map_tdec(e[P_SRC + HH_DECAY]) + 0.01f) * exp2f(e[P_SRC + HH_OPEN] * (4.f / 127.f));
    m->kd = t60_coef(dec);
    m->env = 0.f;
    m->att = 1.f;
    memset(&m->bp, 0, sizeof m->bp);
    memset(&m->hp, 0, sizeof m->hp);
}
static int hat_render(td_voice_t *v, const float *e, float *o, int n)
{
    td_hat_t *m = &v->m.hat;
    float drv = e[P_SRC + HH_DRIVE] * (1.f / 127.f);
    for (int i = 0; i < n; i++) {
        float metal = 0.f;
        for (int k = 0; k < 6; k++) {
            m->ph[k] = wrap1(m->ph[k] + m->inc[k]);
            metal += m->ph[k] < 0.5f ? 1.f : -1.f;
        }
        float src = metal * (1.f / 6.f) * m->color + td_noise(&v->rng) * (1.f - m->color) * 0.8f;
        float lp, bp, hp, lp2, bp2, hp2;
        svf_run(&m->bp, src, m->gb, m->kb, &lp, &bp, &hp);
        svf_run(&m->hp, bp * m->kb, m->gh, 1.4f, &lp2, &bp2, &hp2);
        if (m->att > 0.f) {                                    /* 0.4 ms onset */
            m->env += 1.f / 20.f;
            if (m->env >= 1.f) { m->env = 1.f; m->att = 0.f; }
        } else
            m->env *= m->kd;
        o[i] = drive_sat(hp2 * m->env * 4.0f, drv);
    }
    return m->att > 0.f || m->env > 1e-4f;
}

/* ---- CLAP */
static void clap_start(td_voice_t *v, const float *e, int note)
{
    td_clap_t *m = &v->m.clap;
    float semi = (float)(note - 60) + e[P_SRC + CP_TUNE];
    float fc = 1150.f * exp2f(semi / 12.f) * exp2f((e[P_SRC + CP_TONE] - 64.f) / 64.f);
    m->g = svf_g(fc);
    m->k = 0.55f;
    m->gr = svf_g(fc * 0.7f);
    float sp = (0.003f + e[P_SRC + CP_SPREAD] * (0.017f / 127.f)) * TD_SR;
    float rnd = e[P_SRC + CP_RAND] * (0.8f / 127.f);
    m->nb = clampi((int)(e[P_SRC + CP_BURSTS] + 0.5f), 1, 8);
    float t = 0.f;
    for (int i = 0; i < m->nb; i++) {
        m->bt[i] = (int32_t)t;
        t += sp * (1.f + (td_rand01(&v->rng) - 0.5f) * rnd);
    }
    m->kb = tau_coef(sp * 0.35f / TD_SR);
    m->kt = t60_coef(map_tdec(e[P_SRC + CP_DECAY]) + 0.02f);
    float room = e[P_SRC + CP_ROOM] * (1.f / 127.f);
    m->roomlev = room * 0.35f;
    m->kr = t60_coef(0.25f + room * 0.6f);
    m->be = m->te = m->re = 0.f;
    m->t = 0;
    m->bi = 0;
    memset(&m->bp, 0, sizeof m->bp);
    memset(&m->room, 0, sizeof m->room);
}
static int clap_render(td_voice_t *v, const float *e, float *o, int n)
{
    td_clap_t *m = &v->m.clap;
    float drv = e[P_SRC + CP_DRIVE] * (1.f / 127.f);
    for (int i = 0; i < n; i++) {
        if (m->bi < m->nb && m->t >= m->bt[m->bi]) {
            m->be = 1.f;
            if (m->bi == 0) m->re = 1.f;
            if (++m->bi == m->nb) m->te = 1.f;
        }
        m->t++;
        float env = m->be + m->te * 0.85f;
        m->be *= m->kb;
        m->te *= m->kt;
        float nz = td_noise(&v->rng), lp, bp, hp, lp2, bp2, hp2;
        svf_run(&m->bp, nz, m->g, m->k, &lp, &bp, &hp);
        svf_run(&m->room, nz, m->gr, 1.2f, &lp2, &bp2, &hp2);
        float x = bp * m->k * env * 2.9f + bp2 * m->re * m->roomlev;
        m->re *= m->kr;
        o[i] = drive_sat(x, drv);
    }
    return m->bi < m->nb || m->te > 1e-4f || m->re > 1e-4f || m->be > 1e-4f;
}

/* ---- TOM */
static void tom_start(td_voice_t *v, const float *e, int note)
{
    td_tom_t *m = &v->m.tom;
    float semi = (float)(note - 60) + e[P_SRC + TM_TUNE];
    m->f0 = clampf(120.f * exp2f(semi / 12.f), 20.f, 3000.f);
    m->sweep = e[P_SRC + TM_SWEEP];
    m->kp = tau_coef(map_tsweep(e[P_SRC + TM_STIME]));
    m->penv = 1.f;
    float dec = map_tdec(e[P_SRC + TM_DECAY]) + 0.02f;
    m->ae = 1.f;
    m->ka = t60_coef(dec);
    m->fe = 1.f;
    m->kf = t60_coef(dec * 0.4f);
    m->idx = e[P_SRC + TM_FM] * (4.f / 127.f);
    m->nlev = e[P_SRC + TM_NOISE] * (1.f / 127.f);
    m->nz = 1.f;
    m->knz = tau_coef(0.005f);
    m->phc = m->phm = m->hpz = m->lp = 0.f;
}
static int tom_render(td_voice_t *v, const float *e, float *o, int n)
{
    td_tom_t *m = &v->m.tom;
    float drv = e[P_SRC + TM_DRIVE] * (1.f / 127.f);
    float lpa = onepole_a(200.f * exp2f(e[P_SRC + TM_TONE] * (6.5f / 127.f)));
    for (int i = 0; i < n; i++) {
        m->penv *= m->kp;
        float f = m->f0 * fast_exp2(m->sweep * m->penv * (1.f / 12.f));
        m->phc = wrap1(m->phc + f * (1.f / TD_SR));
        m->phm = wrap1(m->phm + f * 1.5f * (1.f / TD_SR));
        float mod = sin01(m->phm) * m->idx * m->fe;
        float body = sin01(m->phc + mod * 0.15915494f) * m->ae;
        m->ae *= m->ka;
        m->fe *= m->kf;
        float w = td_noise(&v->rng), hp = w - m->hpz;
        m->hpz = w;
        float x = body + hp * m->nz * m->nlev * 0.6f;
        m->nz *= m->knz;
        x = drive_sat(x, drv);
        m->lp += (x - m->lp) * lpa;
        o[i] = m->lp * 0.9f;
    }
    return m->ae > 1e-4f;
}

/* ---- melodic: glide */
static void glide_step(td_voice_t *v, int n, float *p0, float *p1)
{
    *p0 = v->pitch;
    if (v->glide_t > 0.0005f && v->pitch != v->ptarget) {
        float k = expf(-(float)n / (v->glide_t * TD_SR * 0.33f));
        v->pitch = v->ptarget + (v->pitch - v->ptarget) * k;
        if (fabsf(v->pitch - v->ptarget) < 0.001f)
            v->pitch = v->ptarget;
    } else
        v->pitch = v->ptarget;
    *p1 = v->pitch;
}

/* ---- SYNTH */
static int synth_render(td_voice_t *v, const float *e, float *o, int n)
{
    td_synth_t *m = &v->m.sy;
    float p0, p1;
    glide_step(v, n, &p0, &p1);
    float tune = e[P_SRC + SY_TUNE];
    float fa = note_hz(p0 + tune), fb = note_hz(p1 + tune);
    float det = e[P_SRC + SY_DETUNE] * (0.5f / 127.f);
    float r1 = fast_exp2(-det * 0.5f / 12.f), r2 = fast_exp2((e[P_SRC + SY_INTVL] + det * 0.5f) / 12.f);
    float s = clampf(e[P_SRC + SY_SHAPE], 0.f, 127.f) * (3.f / 127.f);
    float sub = e[P_SRC + SY_SUB] * (0.8f / 127.f), nz = e[P_SRC + SY_NOISE] * (0.5f / 127.f);
    float df = (fb - fa) / (float)n;
    for (int i = 0; i < n; i++) {
        float f = fa + df * (float)(i + 1);
        float dt1 = fminf(f * r1 * (1.f / TD_SR), 0.45f), dt2 = fminf(f * r2 * (1.f / TD_SR), 0.45f);
        m->ph1 = wrap1(m->ph1 + dt1);
        m->ph2 = wrap1(m->ph2 + dt2);
        m->phs = wrap1(m->phs + f * (0.5f / TD_SR));
        float x = (osc_shape(m->ph1, dt1, s) + osc_shape(m->ph2, dt2, s)) * 0.5f + sin01(m->phs) * sub;
        if (nz > 0.f)
            x += td_noise(&v->rng) * nz;
        o[i] = x * 0.55f;
    }
    return 1;
}

/* ---- FM */
static int fm_render(td_voice_t *v, const float *e, float *o, int n)
{
    td_fm_t *m = &v->m.fm;
    float p0, p1;
    glide_step(v, n, &p0, &p1);
    float tune = e[P_SRC + FM_TUNE];
    float fa = note_hz(p0 + tune), fb = note_hz(p1 + tune);
    float ratio = td_fm_ratio[clampi((int)(e[P_SRC + FM_RATIO] + 0.5f), 0, 15)] * fast_exp2(e[P_SRC + FM_FINE] * (1.f / 1200.f));
    float idx0 = e[P_SRC + FM_INDEX] * (5.f / 127.f), ienv = e[P_SRC + FM_ENVAMT] * (8.f / 127.f);
    float fbk = e[P_SRC + FM_FDBK] * (0.25f / 127.f);
    m->kie = t60_coef(map_tdec(e[P_SRC + FM_ENVDEC]) + 0.005f);
    float df = (fb - fa) / (float)n;
    for (int i = 0; i < n; i++) {
        float f = fa + df * (float)(i + 1);
        m->phc = wrap1(m->phc + f * (1.f / TD_SR));
        m->phm = wrap1(m->phm + fminf(f * ratio * (1.f / TD_SR), 0.45f));
        float mod = sin01(m->phm + fbk * (m->m1 + m->m2) * 0.5f);
        m->m2 = m->m1;
        m->m1 = mod;
        float I = idx0 + ienv * m->ienv;
        m->ienv *= m->kie;
        o[i] = sin01(m->phc + I * 0.15915494f * mod) * 0.7f;
    }
    return 1;
}

/* ---- SAMPLE */
static const int SLICE_N[4] = {0, 4, 8, 16};
static void smp_start(td_voice_t *v, const float *e, int note)
{
    td_smp_t *m = &v->m.smp;
    int slot = clampi((int)(e[P_SRC + SM_SLOT] + 0.5f), 0, TD_SAMPLE_SLOTS - 1);
    td_slot_t *s = &td_slots[slot];
    m->data = NULL;
    if (!s->used || s->loading || s->len < 64)
        return;
    m->data = td_pool + s->off;
    m->len = s->len;
    m->slot = (uint8_t)slot;
    float fa = clampf(e[P_SRC + SM_START], 0.f, 127.f) * (1.f / 127.f) * (float)m->len;
    float fb = clampf(e[P_SRC + SM_END], 0.f, 127.f) * (1.f / 127.f) * (float)m->len;
    if (fb < fa) { float t = fa; fa = fb; fb = t; }
    uint32_t a = (uint32_t)fa, b = (uint32_t)fb;
    if (b > m->len) b = m->len;
    if (b < a + 64u) { b = a + 64u; if (b > m->len) { b = m->len; a = b - 64u; } }
    int ns = SLICE_N[clampi((int)(e[P_SRC + SM_SLICES] + 0.5f), 0, 3)];
    float semi = e[P_SRC + SM_TUNE];
    if (ns) {
        int sl = ((note - 60) % ns + ns) % ns;
        uint32_t w = (b - a) / (uint32_t)ns;
        if (w < 64u) w = 64u;
        a += (uint32_t)sl * w;
        if (a + 64u > m->len) a = m->len - 64u;
        b = a + w > m->len ? m->len : a + w;
    } else
        semi += (float)(note - 60);
    m->a = a;
    m->b = b;
    m->rate = (float)s->rate / TD_SR * exp2f(semi / 12.f);
    m->rev = e[P_SRC + SM_REV] >= 0.5f;
    m->loop = e[P_SRC + SM_LOOP] >= 0.5f;
    m->pos = m->rev ? (double)b - 1.0 : (double)a;
    m->gain = exp2f(e[P_SRC + SM_GAIN] * (1.f / 6.0206f));
    m->fin = (a == 0 && !m->rev) ? 1.f : 0.f;                  /* a start inside the sound: a short fade in */
}
static inline float smp_at(const td_smp_t *m, int i)
{
    if (i < 0) i = 0;
    if (i >= (int)m->len) i = (int)m->len - 1;
    return m->data[i];
}
static int smp_render(td_voice_t *v, const float *e, float *o, int n)
{
    (void)e;
    td_smp_t *m = &v->m.smp;
    if (!m->data)
        return 0;
    float span = (float)(m->b - m->a);
    for (int i = 0; i < n; i++) {
        double p = m->pos;
        int ip = (int)floor(p);
        float f = (float)(p - ip);
        float y = hermite(smp_at(m, ip - 1), smp_at(m, ip), smp_at(m, ip + 1), smp_at(m, ip + 2), f);
        float dist = m->rev ? (float)(p - m->a) : (float)(m->b - p);
        float ed = dist / m->rate, g = 1.f;
        float edge = m->loop ? 32.f : 48.f;
        if (ed < edge) g = ed / edge;
        if (m->fin < 1.f) {
            g *= m->fin;
            m->fin += 1.f / 32.f;
        }
        o[i] = y * g * m->gain;
        m->pos += m->rev ? -(double)m->rate : (double)m->rate;
        if (!m->rev && m->pos >= m->b) {
            if (!m->loop) return 0;
            m->pos -= span;
            m->fin = 0.f;
        } else if (m->rev && m->pos < m->a) {
            if (!m->loop) return 0;
            m->pos += span;
            m->fin = 0.f;
        }
    }
    return 1;
}

/* ---- the voice */
void td_voice_start(td_voice_t *v, int machine, const float *e, int note, float vel, int32_t gate, int accent)
{
    uint32_t seed = v->rng ? v->rng : 0x9E3779B9u;
    memset(v, 0, sizeof *v);
    v->rng = seed ^ (uint32_t)(note * 2654435761u) ^ 0x6D2B79F5u;
    if (!v->rng) v->rng = 1u;
    td_xs(&v->rng);
    v->active = 1;
    v->gated = 1;
    v->machine = (uint8_t)machine;
    v->note = (uint8_t)clampi(note, 0, 127);
    v->live_note = 255;
    v->vel = vel;
    v->gate = gate;
    v->accent = (uint8_t)accent;
    v->ast = ENV_ATT;
    v->fst = ENV_ATT;
    v->cut_s = e[P_CUT];
    v->pitch = v->ptarget = (float)note;
    switch (machine) {
    case TD_M_KICK: kick_start(v, e, note); break;
    case TD_M_SNARE: snare_start(v, e, note); break;
    case TD_M_HAT: hat_start(v, e, note); break;
    case TD_M_CLAP: clap_start(v, e, note); break;
    case TD_M_TOM: tom_start(v, e, note); break;
    case TD_M_SYNTH:
        v->m.sy.ph2 = td_rand01(&v->rng);
        v->glide_t = map_tglide(e[P_SRC + SY_GLIDE]);
        break;
    case TD_M_FM:
        v->m.fm.ienv = 1.f;
        break;
    case TD_M_SAMPLE: smp_start(v, e, note); break;
    default: break;
    }
}

void td_voice_fade(td_voice_t *v, float ms)
{
    if (!v->active || v->fading)
        return;
    v->fading = 1;
    v->fade = 1.f;
    v->fade_dec = 1.f / (ms * 48.f);
}

void td_voice_gate_off(td_voice_t *v)
{
    v->gated = 0;
    if (v->ast < ENV_REL)
        v->ast = ENV_REL;
}

void td_voice_render(td_voice_t *v, const float *e, float *out, int n, float unused)
{
    (void)unused;
    float raw[TD_BLK];
    memset(raw, 0, sizeof(float) * (size_t)n);
    int alive;
    switch (v->machine) {
    case TD_M_KICK: alive = kick_render(v, e, raw, n); break;
    case TD_M_SNARE: alive = snare_render(v, e, raw, n); break;
    case TD_M_HAT: alive = hat_render(v, e, raw, n); break;
    case TD_M_CLAP: alive = clap_render(v, e, raw, n); break;
    case TD_M_TOM: alive = tom_render(v, e, raw, n); break;
    case TD_M_SYNTH: alive = synth_render(v, e, raw, n); break;
    case TD_M_FM: alive = fm_render(v, e, raw, n); break;
    default: alive = smp_render(v, e, raw, n); break;
    }
    /* filter and its envelope */
    float q = 0.5f * exp2f(clampf(e[P_RES], 0.f, 127.f) * (5.3f / 127.f));
    float k = 1.f / q;
    int type = clampi((int)(e[P_FTYPE] + 0.5f), 0, 2);
    float fenv = e[P_FENV] * 2.f + (v->accent ? 30.f : 0.f);
    float base = e[P_CUT] + e[P_KEY] * (1.f / 127.f) * ((float)v->note - 60.f) * 1.0619f;
    float fdrv = e[P_FDRV] * (1.f / 127.f);
    float fatk = map_tatk(e[P_FATK]);
    float finc = fatk * TD_SR < 1.f ? 1.f : 1.f / (fatk * TD_SR);
    float fdec = t60_coef(map_tdec(e[P_FDEC]) * (v->accent ? 0.6f : 1.f));
    /* amp envelope */
    int melodic = v->machine >= TD_M_SYNTH;
    float aatk = map_tatk(e[P_AATK]);
    float amin = melodic ? 24.f : 1.f;
    float ainc = 1.f / fmaxf(aatk * TD_SR, amin);
    float sus = clampf(e[P_ASUS], 0.f, 127.f) * (1.f / 127.f);
    float kdec = t60_coef(map_tdec(e[P_ADEC]));
    float krel = t60_coef(map_trel(e[P_AREL]));
    if (v->ast == ENV_ATT && v->ahold == 0 && v->alev == 0.f)
        v->ahold = (int32_t)(map_thold(e[P_AHOLD]) * TD_SR);
    float gvel = v->vel;
    for (int i = 0; i < n; i++) {
        if (v->fst == ENV_ATT) {
            v->flev += finc;
            if (v->flev >= 1.f) { v->flev = 1.f; v->fst = ENV_DEC; }
        } else
            v->flev *= fdec;
        v->cut_s += (base - v->cut_s) * 0.02f;
        float x = raw[i];
        if (fdrv > 0.f)
            x = drive_sat(x, fdrv);
        float lp, bp, hp;
        svf_run(&v->flt, x, cut_g(v->cut_s + v->flev * fenv), k, &lp, &bp, &hp);
        float y = type == 0 ? lp : (type == 1 ? bp * k : hp);
        switch (v->ast) {
        case ENV_ATT:
            v->alev += ainc;
            if (v->alev >= 1.f) { v->alev = 1.f; v->ast = v->ahold > 0 ? ENV_HOLD : ENV_DEC; }
            break;
        case ENV_HOLD:
            if (--v->ahold <= 0) v->ast = ENV_DEC;
            break;
        case ENV_DEC:
            v->alev = sus + (v->alev - sus) * kdec;
            if (sus <= 0.f && v->alev < 1e-4f) v->ast = ENV_DONE;
            break;
        case ENV_REL:
            v->alev *= krel;
            if (v->alev < 1e-4f) v->ast = ENV_DONE;
            break;
        default:
            v->alev = 0.f;
            break;
        }
        y *= v->alev * gvel;
        if (v->fading) {
            v->fade -= v->fade_dec;
            if (v->fade <= 0.f) { v->fade = 0.f; alive = 0; }
            y *= v->fade;
        }
        out[i] += y;
    }
    if (v->gated && v->gate >= 0) {
        v->gate -= n;
        if (v->gate <= 0)
            td_voice_gate_off(v);
    }
    if (!alive || v->ast == ENV_DONE || (v->fading && v->fade <= 0.f))
        v->active = 0;
}
