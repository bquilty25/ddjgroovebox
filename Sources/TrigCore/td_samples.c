/* SPDX-License-Identifier: GPL-3.0-only */
/* TRIGDECK samples: a static pool of 60 s (mono, 48 kHz frames) shared by 16 slots, five built-in sounds synthesised
 * at td_init (so SAMPLE works without files), td_sample_load, and td_capture (resampling a deck's last bars).
 *
 * td_sample_load reserves room under the lock, copies outside it (the slot is marked loading: no voice starts on it),
 * then commits under the lock. When the pool is fragmented it is compacted (under the lock; voices playing moved
 * audio follow it). A load that does not fit is refused (-1). */
#include "td_internal.h"
#include <stdio.h>

float td_pool[TD_POOL_FRAMES];
td_slot_t td_slots[TD_SAMPLE_SLOTS];
static int g_loading;
static float g_tmp[3 * 48000];                                /* the built-ins are synthesised here (td_init only) */

void td_samples_init(void)
{
    memset(td_slots, 0, sizeof td_slots);
    g_loading = 0;
}

static void fade_slot_voices(int slot)
{
    for (int t = 0; t < TD_TRACKS; t++)
        for (int i = 0; i < TD_VOICES; i++) {
            td_voice_t *v = &E.trk[t].v[i];
            if (v->active && v->machine == TD_M_SAMPLE && v->m.smp.data && v->m.smp.slot == slot)
                td_voice_fade(v, 3.f);
        }
}

/* room for n frames for `slot`; its old region is reused only if nothing else fits. -1: no room */
static int64_t pool_alloc(int slot, uint32_t n)
{
    for (int pass = 0; pass < 2; pass++) {
        uint64_t used = 0;
        int idx[TD_SAMPLE_SLOTS], nr = 0;
        for (int i = 0; i < TD_SAMPLE_SLOTS; i++) {
            if (!(td_slots[i].used || td_slots[i].loading) || !td_slots[i].len) continue;
            if (i == slot && pass == 1) continue;
            used += td_slots[i].len;
            idx[nr++] = i;
        }
        if (used + n > TD_POOL_FRAMES) continue;
        for (int a = 1; a < nr; a++) {                         /* by offset */
            int x = idx[a], b = a - 1;
            while (b >= 0 && td_slots[idx[b]].off > td_slots[x].off) { idx[b + 1] = idx[b]; b--; }
            idx[b + 1] = x;
        }
        uint32_t pos = 0;
        for (int a = 0; a < nr; a++) {
            if (td_slots[idx[a]].off >= pos + n) return pos;
            uint32_t end = td_slots[idx[a]].off + td_slots[idx[a]].len;
            if (end > pos) pos = end;
        }
        if (TD_POOL_FRAMES - pos >= n) return pos;
        if (pass == 0) continue;
        if (g_loading) return -1;                              /* (never move audio someone is copying into) */
        pos = 0;
        for (int a = 0; a < nr; a++) {
            td_slot_t *s = &td_slots[idx[a]];
            if (s->off != pos) {
                memmove(td_pool + pos, td_pool + s->off, sizeof(float) * s->len);
                const float *lo = td_pool + s->off, *hi = lo + s->len;
                ptrdiff_t delta = (ptrdiff_t)s->off - (ptrdiff_t)pos;
                for (int t = 0; t < TD_TRACKS; t++)
                    for (int i = 0; i < TD_VOICES; i++) {
                        td_voice_t *v = &E.trk[t].v[i];
                        if (v->active && v->machine == TD_M_SAMPLE && v->m.smp.data >= lo && v->m.smp.data < hi)
                            v->m.smp.data -= delta;
                    }
                s->off = pos;
            }
            pos += s->len;
        }
        return TD_POOL_FRAMES - pos >= n ? (int64_t)pos : -1;
    }
    return -1;
}

static int put_sample(int slot, const float *data, uint32_t n, uint32_t rate, const char *name)
{
    int64_t off = pool_alloc(slot, n);
    if (off < 0)
        return -1;
    fade_slot_voices(slot);
    td_slot_t *s = &td_slots[slot];
    memcpy(td_pool + off, data, sizeof(float) * n);
    s->off = (uint32_t)off;
    s->len = n;
    s->rate = rate;
    s->used = 1;
    s->loading = 0;
    snprintf(s->name, sizeof s->name, "%s", name ? name : "");
    return 0;
}

int td_sample_load(int slot, const float *mono, uint32_t frames, uint32_t rate, const char *name)
{
    if (slot < 0 || slot >= TD_SAMPLE_SLOTS || !mono || frames < 64u || frames > TD_POOL_FRAMES || rate < 4000u || rate > 192000u)
        return -1;
    td_lock();
    if (td_slots[slot].loading) {
        td_unlock();
        return -1;
    }
    int64_t off = pool_alloc(slot, frames);
    if (off < 0) {
        td_unlock();
        return -1;
    }
    fade_slot_voices(slot);
    td_slot_t *s = &td_slots[slot];
    s->used = 0;
    s->loading = 1;
    s->off = (uint32_t)off;
    s->len = frames;
    g_loading++;
    td_unlock();
    float *dst = td_pool + off;                                /* (copied outside the lock: nothing plays this region) */
    for (uint32_t i = 0; i < frames; i++) {
        float x = mono[i];
        dst[i] = x == x && fabsf(x) < 8.f ? x : 0.f;
    }
    td_lock();
    s->rate = rate;
    snprintf(s->name, sizeof s->name, "%s", name ? name : "");
    s->used = 1;
    s->loading = 0;
    g_loading--;
    td_unlock();
    return 0;
}

const char *td_sample_name(int slot)
{
    if (slot < 0 || slot >= TD_SAMPLE_SLOTS || !td_slots[slot].used)
        return "";
    return td_slots[slot].name;
}

/* ---- resampling: the deck's last bars into a slot (the lock is held) */
int x_capture(int deck, int bars, int slot)
{
    if (deck < 0 || deck >= TD_DECKS || slot < 0 || slot >= TD_SAMPLE_SLOTS || td_slots[slot].loading)
        return -1;
    bars = bars >= 4 ? 4 : (bars >= 2 ? 2 : 1);
    const td_deck_t *k = &E.deck[deck];
    double fpb = td_frames_per_step() * 16.0;
    int64_t end, start;
    if (E.playing && E.nbar >= bars + 1) {                     /* bar lines as they were played */
        end = E.bar_frame[0];
        start = E.bar_frame[bars];
    } else {
        end = E.playing && E.nbar >= 1 ? E.bar_frame[0] : (int64_t)k->wabs;
        start = end - (int64_t)(fpb * bars);
    }
    int64_t oldest = (int64_t)k->wabs - (int64_t)(TD_RING - 64u);
    if (oldest < 0) oldest = 0;
    if (start < oldest) start = oldest;                       /* (capped at the ring: 21.8 s) */
    if (end > (int64_t)k->wabs) end = (int64_t)k->wabs;
    int64_t n = end - start;
    if (n < 4800)
        return -1;
    int64_t off = pool_alloc(slot, (uint32_t)n);
    if (off < 0)
        return -1;
    fade_slot_voices(slot);
    const float *ring = td_ring[deck];
    float *dst = td_pool + off;
    const int64_t fade = 96;
    for (int64_t i = 0; i < n; i++) {
        uint32_t w = (uint32_t)((start + i) & TD_RING_MASK) * 2u;
        float g = 1.f;
        if (i < fade) g = (float)i / (float)fade;
        else if (n - 1 - i < fade) g = (float)(n - 1 - i) / (float)fade;
        dst[i] = 0.5f * (ring[w] + ring[w + 1u]) * g;
    }
    td_slot_t *s = &td_slots[slot];
    s->off = (uint32_t)off;
    s->len = (uint32_t)n;
    s->rate = 48000u;
    s->used = 1;
    s->loading = 0;
    E.deck[deck].captures++;
    snprintf(s->name, sizeof s->name, "DECK %c %d", deck ? 'B' : 'A', E.deck[deck].captures);
    return 0;
}

/* ---- the built-in sounds */
static void normalise(float *b, int n, float peak)
{
    float m = 0.f;
    for (int i = 0; i < n; i++) if (fabsf(b[i]) > m) m = fabsf(b[i]);
    if (m > 0.f) for (int i = 0; i < n; i++) b[i] *= peak / m;
}
static float saw_bl(float ph, float dt)
{
    float t = ph, y = 2.f * t - 1.f;
    if (t < dt) { t /= dt; y -= t + t - t * t - 1.f; }
    else if (t > 1.f - dt) { t = (t - 1.f) / dt; y -= t * t + t + t + 1.f; }
    return y;
}
static int gen_vinyl(float *b, uint32_t *rng)                  /* a dusty break hit: kick and snare together */
{
    int n = (int)(0.55f * TD_SR);
    float ph = 0.f, lp = 0.f, nlp = 0.f;
    td_svf_t bp = {0, 0};
    float g = svf_g(1900.f);
    for (int i = 0; i < n; i++) {
        float t = (float)i / TD_SR;
        float f = 58.f + 90.f * expf(-t / 0.018f);
        ph += f / TD_SR;
        ph -= floorf(ph);
        float kick = sinf(6.2831853f * ph) * expf(-t / 0.16f);
        float body = sinf(6.2831853f * 190.f * t) * expf(-t / 0.05f) * 0.35f;
        float l, bpo, h;
        svf_run(&bp, td_noise(rng), g, 1.1f, &l, &bpo, &h);
        float sn = bpo * expf(-t / 0.09f) * 1.4f;
        float crackle = (td_rand01(rng) < 0.0015f) ? td_noise(rng) * 0.6f : 0.f;
        float x = softclip((kick * 0.9f + body + sn) * 1.6f) + crackle;
        lp += (x - lp) * 0.45f;                                /* ~7 kHz: dusty */
        nlp += (lp - nlp) * 0.6f;
        b[i] = nlp;
    }
    normalise(b, n, 0.9f);
    return n;
}
static int gen_vox(float *b, uint32_t *rng)                    /* a formant blip: "ah" to "oh" at 261.6 Hz */
{
    int n = (int)(0.5f * TD_SR);
    td_svf_t f[3] = {{0, 0}, {0, 0}, {0, 0}};
    float ph = 0.f;
    (void)rng;
    for (int i = 0; i < n; i++) {
        float t = (float)i / TD_SR, x = t / 0.5f;
        float f0 = 261.63f * (1.f + 0.006f * sinf(6.2831853f * 5.5f * t));
        float dt = f0 / TD_SR;
        ph += dt;
        ph -= floorf(ph);
        float src = saw_bl(ph, dt);
        const float F[3][2] = {{800.f, 450.f}, {1150.f, 800.f}, {2900.f, 2830.f}};
        const float G[3] = {1.f, 0.6f, 0.25f};
        float y = 0.f;
        for (int k = 0; k < 3; k++) {
            float fc = F[k][0] + (F[k][1] - F[k][0]) * x;
            float l, bp, h;
            svf_run(&f[k], src, svf_g(fc), 0.12f, &l, &bp, &h);
            y += bp * 0.12f * G[k];
        }
        float env = fminf(1.f, t / 0.02f) * expf(-t / 0.18f);
        b[i] = y * env;
    }
    normalise(b, n, 0.85f);
    return n;
}
static int gen_stab(float *b, uint32_t *rng)                   /* a chord stab: C minor 7 over C, filtered */
{
    int n = (int)(1.2f * TD_SR);
    static const float notes[5] = {48.f, 60.f, 63.f, 67.f, 70.f};
    float ph[10];
    for (int i = 0; i < 10; i++) ph[i] = td_rand01(rng);
    td_svf_t f = {0, 0};
    for (int i = 0; i < n; i++) {
        float t = (float)i / TD_SR, x = 0.f;
        for (int k = 0; k < 10; k++) {
            float fr = 440.f * exp2f((notes[k / 2] - 69.f) / 12.f) * (k & 1 ? 1.004f : 0.996f);
            float dt = fr / TD_SR;
            ph[k] += dt;
            ph[k] -= floorf(ph[k]);
            x += saw_bl(ph[k], dt) * (k < 2 ? 0.6f : 1.f);
        }
        float fc = 600.f + 3800.f * expf(-t / 0.09f);
        float l, bp, h;
        svf_run(&f, x * 0.1f, svf_g(fc), 0.7f, &l, &bp, &h);
        float env = fminf(1.f, t / 0.004f) * expf(-t / 0.22f);
        b[i] = softclip(l * env * 1.5f);
    }
    normalise(b, n, 0.85f);
    return n;
}
static int gen_riser(float *b, uint32_t *rng)                  /* a noise riser over 2.5 s */
{
    int n = (int)(2.5f * TD_SR);
    td_svf_t f = {0, 0};
    for (int i = 0; i < n; i++) {
        float x = (float)i / (float)n;
        float fc = 300.f * powf(30.f, x);
        float l, bp, h;
        svf_run(&f, td_noise(rng), svf_g(fc), 0.35f, &l, &bp, &h);
        float env = 0.04f + 0.96f * x * x;
        if (n - i < 960) env *= (float)(n - i) / 960.f;
        b[i] = bp * env;
    }
    normalise(b, n, 0.8f);
    return n;
}
static int gen_crackle(float *b, uint32_t *rng)                /* vinyl crackle, a 2 s loop */
{
    int n = (int)(2.0f * TD_SR);
    float hiss = 0.f, hlp = 0.f, pop = 0.f, popf = 0.f, popph = 0.f;
    td_svf_t f = {0, 0};
    float g = svf_g(3200.f);
    float ce = 0.f, cs = 0.f;
    for (int i = 0; i < n; i++) {
        float w = td_noise(rng);
        hiss += (w - hiss) * 0.2f;
        hlp += (hiss - hlp) * 0.05f;
        float h = (hiss - hlp) * 0.05f;
        if (td_rand01(rng) < 0.0006f) { ce = 0.3f + td_rand01(rng) * 0.7f; cs = td_noise(rng) > 0.f ? 1.f : -1.f; }
        float l, bp, hp;
        svf_run(&f, td_noise(rng) * ce * cs, g, 0.9f, &l, &bp, &hp);
        ce *= 0.93f;
        if (td_rand01(rng) < 0.00003f) { pop = 0.5f + td_rand01(rng) * 0.4f; popf = 40.f + td_rand01(rng) * 60.f; popph = 0.f; }
        popph += popf / TD_SR;
        float p = sinf(6.2831853f * popph) * pop;
        pop *= 0.9985f;
        b[i] = h + bp * 1.2f + p * 0.5f;
    }
    for (int i = 0; i < 2400; i++) {                           /* the loop's end blends into its start */
        float x = (float)i / 2400.f;
        b[n - 2400 + i] = b[n - 2400 + i] * (1.f - x) + b[i] * x;
    }
    normalise(b, n, 0.7f);
    return n;
}
void td_samples_builtin(void)
{
    uint32_t rng = 0xB0B5EEDu;
    int n;
    n = gen_vinyl(g_tmp, &rng);
    put_sample(0, g_tmp, (uint32_t)n, 48000u, "VINYL");
    n = gen_vox(g_tmp, &rng);
    put_sample(1, g_tmp, (uint32_t)n, 48000u, "VOX");
    n = gen_stab(g_tmp, &rng);
    put_sample(2, g_tmp, (uint32_t)n, 48000u, "STAB");
    n = gen_riser(g_tmp, &rng);
    put_sample(3, g_tmp, (uint32_t)n, 48000u, "RISER");
    n = gen_crackle(g_tmp, &rng);
    put_sample(4, g_tmp, (uint32_t)n, 48000u, "CRACKLE");
}
