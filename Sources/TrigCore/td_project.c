/* SPDX-License-Identifier: GPL-3.0-only */
/* TRIGDECK project files: all 16 patterns (sounds, steps, locks, scenes, lengths), tempo, swing, key, mutes and the
 * MASTER page. Not the samples' audio, not the deck faders or the crossfader (physical controls).
 *
 * Layout (little-endian):
 *   "TDPJ"  u16 version (1)  u16 0  u32 payload bytes  u32 CRC-32 of the payload
 *   payload: u16 bpm x 10, u8 swing, u8 pattern, u8 key root, u8 scale, u8 mutes (bit per track), u8 0, i16 master[8],
 *            then per pattern: u8 machine[8], u8 length[8], i16 param[8][48],
 *            per track and step: u8 trig note vel cond retrig rate, i8 micro, u8 flags (1 slide, 2 accent),
 *                                u8 locks, locks x (u8 param, i16 value),
 *            per scene: u16 count, count x (u8 track, u8 param, i16 value).
 * td_project_size() is the largest possible project; td_project_save returns the bytes used. A blob with another
 * magic or version, a wrong length, a bad CRC or anything that does not parse is refused and nothing changes. */
#include "td_internal.h"

#define TD_PROJ_VERSION 1
#define HDR 16

uint32_t td_crc32(const void *data, size_t n)
{
    static uint32_t tab[256];
    static int ready;
    if (!ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1u ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            tab[i] = c;
        }
        ready = 1;
    }
    const uint8_t *p = data;
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = tab[(c ^ p[i]) & 255u] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

size_t td_project_size(void)
{
    size_t per = 16 + TD_TRACKS * TD_NPARAMS * 2 + (size_t)TD_TRACKS * TD_STEPS * (9 + TD_LPS * 3) + 2 * (2 + TD_SCENE_MAX * 4);
    return HDR + 8 + TD_SLOTS * 2 + TD_PATTERNS * per;
}

typedef struct { uint8_t *p; size_t n, i; int bad; } wr_t;
static void w8(wr_t *w, int v) { if (w->i + 1 > w->n) { w->bad = 1; return; } w->p[w->i++] = (uint8_t)v; }
static void w16(wr_t *w, int v) { w8(w, v & 255); w8(w, (v >> 8) & 255); }
static void w32(wr_t *w, uint32_t v) { w16(w, (int)(v & 0xFFFF)); w16(w, (int)(v >> 16)); }

int td_project_save(void *buf, size_t n)
{
    if (!buf || n < HDR)
        return -1;
    td_lock();
    if (!E.inited) {
        td_unlock();
        return -1;
    }
    wr_t w = {(uint8_t *)buf, n, HDR, 0};
    w16(&w, (int)lroundf(E.bpm * 10.f));
    w8(&w, E.swing);
    w8(&w, E.cur);
    w8(&w, E.scale_root);
    w8(&w, E.scale);
    int mutes = 0;
    for (int t = 0; t < TD_TRACKS; t++) mutes |= E.muted[t] ? 1 << t : 0;
    w8(&w, mutes);
    w8(&w, 0);
    for (int s = 0; s < TD_SLOTS; s++) w16(&w, E.gparam[s]);
    for (int pi = 0; pi < TD_PATTERNS; pi++) {
        const td_pattern_t *P = &E.pat[pi];
        for (int t = 0; t < TD_TRACKS; t++) w8(&w, P->machine[t]);
        for (int t = 0; t < TD_TRACKS; t++) w8(&w, P->length[t]);
        for (int t = 0; t < TD_TRACKS; t++)
            for (int q = 0; q < TD_NPARAMS; q++) w16(&w, P->param[t][q]);
        for (int t = 0; t < TD_TRACKS; t++)
            for (int s = 0; s < TD_STEPS; s++) {
                const td_step_t *st = &P->step[t][s];
                w8(&w, st->trig); w8(&w, st->note); w8(&w, st->vel); w8(&w, st->cond); w8(&w, st->retrig);
                w8(&w, st->rrate); w8(&w, (uint8_t)st->micro); w8(&w, (st->slide ? 1 : 0) | (st->accent ? 2 : 0));
                int nl = st->nlocks > TD_LPS ? TD_LPS : st->nlocks;
                w8(&w, nl);
                for (int k = 0; k < nl; k++) { w8(&w, P->lock[t][s][k].param); w16(&w, P->lock[t][s][k].value); }
            }
        for (int sc = 0; sc < 2; sc++) {
            w16(&w, P->nscene[sc]);
            for (int k = 0; k < P->nscene[sc]; k++) {
                w8(&w, P->scene[sc][k].track); w8(&w, P->scene[sc][k].param); w16(&w, P->scene[sc][k].value);
            }
        }
    }
    td_unlock();
    if (w.bad)
        return -1;
    size_t payload = w.i - HDR;
    uint8_t *h = (uint8_t *)buf;
    h[0] = 'T'; h[1] = 'D'; h[2] = 'P'; h[3] = 'J';
    wr_t hw = {h, HDR, 4, 0};
    w16(&hw, TD_PROJ_VERSION);
    w16(&hw, 0);
    w32(&hw, (uint32_t)payload);
    w32(&hw, td_crc32(h + HDR, payload));
    return (int)w.i;
}

typedef struct { const uint8_t *p; size_t n, i; int bad; } rd_t;
static int r8(rd_t *r) { if (r->i + 1 > r->n) { r->bad = 1; return 0; } return r->p[r->i++]; }
static int r16(rd_t *r) { int a = r8(r), b = r8(r); return (int16_t)(uint16_t)(a | (b << 8)); }
static uint32_t r32(rd_t *r) { uint32_t a = (uint16_t)r16(r), b = (uint16_t)r16(r); return a | (b << 16); }

static td_pattern_t g_tmp_pat[TD_PATTERNS];

int td_project_load(const void *buf, size_t n)
{
    const uint8_t *h = buf;
    if (!buf || n < HDR || h[0] != 'T' || h[1] != 'D' || h[2] != 'P' || h[3] != 'J')
        return -1;
    rd_t hr = {h, HDR, 4, 0};
    int version = (uint16_t)r16(&hr);
    r16(&hr);
    uint32_t payload = r32(&hr), crc = r32(&hr);
    if (version != TD_PROJ_VERSION || (size_t)payload != n - HDR || td_crc32(h + HDR, payload) != crc)
        return -1;
    td_lock();
    rd_t r = {h, n, HDR, 0};
    float bpm = (float)(uint16_t)r16(&r) * 0.1f;
    int swing = r8(&r), cur = r8(&r), root = r8(&r), scale = r8(&r), mutes = r8(&r);
    r8(&r);
    int16_t gp[TD_SLOTS];
    for (int s = 0; s < TD_SLOTS; s++) {
        const td_pdesc_t *d = td_gdesc(s);
        gp[s] = (int16_t)clampi(r16(&r), d->min, d->max);
    }
    for (int pi = 0; pi < TD_PATTERNS && !r.bad; pi++) {
        td_pattern_t *P = &g_tmp_pat[pi];
        memset(P, 0, sizeof *P);
        for (int t = 0; t < TD_TRACKS; t++) P->machine[t] = (uint8_t)clampi(r8(&r), 0, TD_MACHINES - 1);
        for (int t = 0; t < TD_TRACKS; t++) P->length[t] = (uint8_t)clampi(r8(&r), 1, TD_STEPS);
        for (int t = 0; t < TD_TRACKS; t++)
            for (int q = 0; q < TD_NPARAMS; q++) {
                const td_pdesc_t *d = td_pdesc(P->machine[t], q);
                P->param[t][q] = (int16_t)clampi(r16(&r), d->min, d->max);
            }
        for (int t = 0; t < TD_TRACKS && !r.bad; t++)
            for (int s = 0; s < TD_STEPS && !r.bad; s++) {
                td_step_t *st = &P->step[t][s];
                st->trig = r8(&r) ? 1 : 0;
                st->note = (uint8_t)clampi(r8(&r), 0, 127);
                st->vel = (uint8_t)clampi(r8(&r), 0, 127);
                st->cond = (uint8_t)clampi(r8(&r), 0, TD_CONDS - 1);
                st->retrig = (uint8_t)clampi(r8(&r), 0, 8);
                st->rrate = (uint8_t)clampi(r8(&r), 0, 3);
                st->micro = (int8_t)clampi((int8_t)r8(&r), -23, 23);
                int fl = r8(&r);
                st->slide = fl & 1;
                st->accent = (fl >> 1) & 1;
                int nl = r8(&r);
                if (nl > TD_LPS) { r.bad = 1; break; }
                st->nlocks = (uint8_t)nl;
                for (int k = 0; k < nl; k++) {
                    int q = r8(&r), v = r16(&r);
                    if (q >= TD_NPARAMS) { r.bad = 1; break; }
                    const td_pdesc_t *d = td_pdesc(P->machine[t], q);
                    P->lock[t][s][k].param = (uint8_t)q;
                    P->lock[t][s][k].value = (int16_t)clampi(v, d->min, d->max);
                }
            }
        for (int sc = 0; sc < 2 && !r.bad; sc++) {
            int cnt = (uint16_t)r16(&r);
            if (cnt > TD_SCENE_MAX) { r.bad = 1; break; }
            P->nscene[sc] = (uint16_t)cnt;
            for (int k = 0; k < cnt; k++) {
                int t = r8(&r), q = r8(&r), v = r16(&r);
                if (t >= TD_TRACKS || q >= TD_NPARAMS) { r.bad = 1; break; }
                P->scene[sc][k].track = (uint8_t)t;
                P->scene[sc][k].param = (uint8_t)q;
                P->scene[sc][k].value = (int16_t)v;
            }
        }
    }
    if (r.bad || r.i != n || !E.inited) {
        td_unlock();
        return -1;
    }
    memcpy(E.pat, g_tmp_pat, sizeof E.pat);
    E.bpm = clampf(bpm, 30.f, 300.f);
    E.swing = clampi(swing, 50, 80);
    E.cur = clampi(cur, 0, TD_PATTERNS - 1);
    E.queued = -1;
    E.scale_root = clampi(root, 0, 11);
    E.scale = clampi(scale, 0, 7);
    for (int t = 0; t < TD_TRACKS; t++) {
        E.muted[t] = (uint8_t)((mutes >> t) & 1);
        E.trk[t].lk_mask = 0;
    }
    memcpy(E.gparam, gp, sizeof gp);
    td_seq_reset_transport();                                  /* (playing on from the start of the loaded pattern) */
    td_unlock();
    return 0;
}
