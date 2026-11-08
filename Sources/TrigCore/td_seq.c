/* SPDX-License-Identifier: GPL-3.0-only */
/* TRIGDECK sequencer.
 *
 * Time is counted in steps (1/16 notes) since PLAY, as a double (gpos), advancing by `rate` steps a frame. Step k of
 * every track is decided one step ahead (when the transport passes k - 1): its trig (or lock-only step) becomes an
 * event at k + swing + micro / 24, which td_render applies at the exact frame (the first frame at or after it). Deciding
 * a step ahead is what lets a trig with negative micro timing on step 1 play before the bar line, and what lets the
 * first step of a queued pattern be read from that pattern.
 *
 * Tracks run polymetrically: track step = (k - pattern start) mod track length, the track's loop count (for FIRST and
 * A:B) = (k - pattern start) / length. A queued pattern starts when the pattern's longest track completes (the master
 * length: 16 steps for a one-bar pattern); the switch is an event exactly on that step.
 * Conditions are evaluated when the trig plays. Locks: a trig replaces the track's live locks with its own (none =
 * the base values come back); a lock-only step merges its locks into them, changing the sound of the note that rings.
 * Scenes act on top of base values and locks (Octatrack-like): value = lerp(base or lock, scene value, morph). */
#include "td_internal.h"
#include <stdio.h>

static float pmin[TD_MACHINES][TD_NPARAMS], pmax[TD_MACHINES][TD_NPARAMS];
static int ptab_ready;
static void ptab_init(void)
{
    if (ptab_ready)
        return;
    for (int m = 0; m < TD_MACHINES; m++)
        for (int p = 0; p < TD_NPARAMS; p++) {
            const td_pdesc_t *d = td_pdesc(m, p);
            pmin[m][p] = d->min;
            pmax[m][p] = d->max;
        }
    ptab_ready = 1;
}

double td_frames_per_step(void) { return 720000.0 / (double)E.bpm; }
double td_step_rate(void) { return (double)E.bpm * (1.0 + 0.08 * (double)E.nudge) / 720000.0; }
int td_master_len(const td_pattern_t *p)
{
    int m = 1;
    for (int t = 0; t < TD_TRACKS; t++)
        if (p->length[t] > m) m = p->length[t];
    return m;
}
int td_is_melodic(int m) { return m == TD_M_SYNTH || m == TD_M_FM || m == TD_M_SAMPLE; }
static int slides(int m) { return m == TD_M_SYNTH || m == TD_M_FM; }
int td_eff_muted(int t) { return E.muted[t] || (E.ap.on && E.ap.mute[t] && !E.ap.override[t]); }
static float vel_gain(int v) { float x = clampi(v, 0, 127) * (1.f / 127.f); return x * sqrtf(x); }

void td_midi_push(uint8_t a, uint8_t b, uint8_t c, int n, int64_t frame)
{
    if (E.mq_n >= TD_MIDI_Q)
        return;
    int i = (E.mq_rd + E.mq_n) % TD_MIDI_Q;
    E.mq[i].b[0] = a;
    E.mq[i].b[1] = b;
    E.mq[i].b[2] = c;
    E.mq[i].n = (uint8_t)n;
    E.mq[i].frame = frame;
    E.mq_n++;
}

/* ---- effective parameters (t: a runtime track; its deck's pattern, track t % 8) */
static void apply_scene(int t, float *eff, float x)
{
    td_pattern_t *P = RT_PAT(t);
    int pt = RT_TR(t), sc;
    float amt;
    if (RT_DECK(t) != E.on_air || E.blending) return;          /* (the scenes: the deck on air's; not in a blend) */
    if (x < 0.4999f) { sc = 0; amt = (0.5f - x) * 2.f; }
    else if (x > 0.5001f) { sc = 1; amt = (x - 0.5f) * 2.f; }
    else return;
    if (amt > 1.f) amt = 1.f;
    for (int i = 0; i < P->nscene[sc]; i++) {
        const td_scene_ent_t *s = &P->scene[sc][i];
        if (s->track == pt && s->param < TD_NPARAMS)
            eff[s->param] += ((float)s->value - eff[s->param]) * amt;
    }
}
static void compute_eff(int t, float *eff, float xf, int with_lfo)
{
    ptab_init();
    td_track_t *tr = &E.trk[t];
    td_pattern_t *P = RT_PAT(t);
    int pt = RT_TR(t), m = P->machine[pt];
    for (int p = 0; p < TD_NPARAMS; p++)
        eff[p] = (tr->lk_mask >> p) & 1u ? (float)tr->lk_val[p] : (float)P->param[pt][p];
    apply_scene(t, eff, xf);
    eff[P_CUT] += E.ap.walk[t][0] + E.ap.cut_ramp[t];
    eff[P_DLY] += E.ap.walk[t][1];
    if (m == TD_M_FM)
        eff[P_SRC + FM_INDEX] += E.ap.walk[t][2];
    if (with_lfo) {
        int dest = (int)(eff[P_LDEST] + 0.5f);
        if (dest > 0 && dest <= 32)
            eff[TD_SLOTS + dest - 1] += tr->lfo_mod;
    }
    for (int p = 0; p < TD_NPARAMS; p++)
        eff[p] = clampf(eff[p], pmin[m][p], pmax[m][p]);
}
void td_track_eff(int t) { compute_eff(t, E.trk[t].eff, E.xf_s, 1); }
float td_param_live_f(int t, int p)
{
    float eff[TD_NPARAMS];
    compute_eff(t, eff, E.xf, 1);
    return eff[p];
}

/* ---- LFO */
static float lfo_wave(int w, float ph, float rnd)
{
    switch (w) {
    case 0: return sin01(ph);
    case 1: return ph < 0.25f ? 4.f * ph : (ph < 0.75f ? 2.f - 4.f * ph : 4.f * ph - 4.f);
    case 2: return ph < 0.5f ? 2.f * ph : 2.f * ph - 2.f;
    case 3: return ph < 0.5f ? 1.f : -1.f;
    default: return rnd;
    }
}
static void lfo_trig(int t)
{
    td_track_t *tr = &E.trk[t];
    const float *e = tr->eff;
    int mode = (int)(e[P_LMODE] + 0.5f);
    if (mode == 1 || mode == 3) {
        tr->lfo_ph = e[P_LPHASE] * (1.f / 128.f);
        tr->lfo_done = 0;
    } else if (mode == 2)
        tr->lfo_hold = lfo_wave((int)(e[P_LWAVE] + 0.5f), tr->lfo_ph, tr->lfo_rnd);
    tr->lfo_fade = 0.f;
    tr->lfo_trigged = 1;
}
void td_lfo_advance(int t, int n, double rate)
{
    (void)rate;
    td_track_t *tr = &E.trk[t];
    const float *e = tr->eff;
    int mode = (int)(e[P_LMODE] + 0.5f), wave = (int)(e[P_LWAVE] + 0.5f);
    float cpb = e[P_LSPD] * (float)(1 << clampi((int)(e[P_LMUL] + 0.5f), 0, 11)) * (1.f / 128.f);   /* cycles a bar */
    float inc = cpb * (1.f / 16.f) * (float)((double)E.bpm / 720000.0) * (float)n;
    if (inc > 0.45f) inc = 0.45f;                              /* (at most ~1.5 kHz: the control rate) */
    if (!(mode == 3 && tr->lfo_done)) {
        tr->lfo_ph += inc;
        while (tr->lfo_ph >= 1.f) {
            tr->lfo_ph -= 1.f;
            tr->lfo_rnd = td_noise(&tr->lfo_rng);
            if (mode == 3) { tr->lfo_done = 1; tr->lfo_ph = 0.9999f; break; }
        }
    }
    float out = mode == 2 && tr->lfo_trigged ? tr->lfo_hold : lfo_wave(wave, tr->lfo_ph, tr->lfo_rnd);
    float fade = e[P_LFADE], lvl = 1.f;
    if (fade != 0.f) {
        float T = fabsf(fade) * (4.f / 64.f);
        tr->lfo_fade = fminf(1.f, tr->lfo_fade + (float)n / (T * TD_SR));
        lvl = fade < 0.f ? tr->lfo_fade : 1.f - tr->lfo_fade;
    }
    int dest = (int)(e[P_LDEST] + 0.5f);
    int m = RT_PAT(t)->machine[RT_TR(t)];
    float range = dest > 0 ? pmax[m][TD_SLOTS + dest - 1] - pmin[m][TD_SLOTS + dest - 1] : 0.f;
    tr->lfo_out = out * lvl;
    tr->lfo_mod = out * lvl * e[P_LDEPTH] * (1.f / 64.f) * range * 0.5f;
}

/* ---- notes */
static td_voice_t *alloc_voice(td_track_t *tr)
{
    td_voice_t *best = NULL;
    for (int i = 0; i < TD_VOICES; i++)
        if (!tr->v[i].active)
            return &tr->v[i];
    for (int i = 0; i < TD_VOICES; i++)                         /* the oldest fading voice */
        if (tr->v[i].fading && (!best || tr->v[i].age < best->age))
            best = &tr->v[i];
    if (best)
        return best;
    for (int i = 0; i < TD_VOICES; i++)
        if (!best || tr->v[i].age < best->age)
            best = &tr->v[i];
    return best;
}
void td_note_on(int t, int note, float vel, int32_t gate, int flags)
{
    td_track_t *tr = &E.trk[t];                                /* (t: a runtime track) */
    int m = RT_PAT(t)->machine[RT_TR(t)];
    const float *e = tr->eff;
    int accent = (flags & NF_ACCENT) != 0;
    tr->age++;
    if (!td_is_melodic(m)) {                                   /* drums: one voice; the ringing one fades in 3 ms */
        for (int i = 0; i < TD_VOICES; i++)
            if (tr->v[i].active)
                td_voice_fade(&tr->v[i], 3.f);
        td_voice_t *v = alloc_voice(tr);
        td_voice_start(v, m, e, note, vel, gate, accent);
        v->seq = (flags & NF_SEQ) != 0;
        v->age = tr->age;
        if (flags & NF_LIVE) v->live_note = (uint8_t)note;
        return;
    }
    int chord = m == TD_M_SYNTH ? (int)(e[P_SRC + SY_CHORD] + 0.5f) : m == TD_M_FM ? (int)(e[P_SRC + FM_CHORD] + 0.5f) : 0;
    chord = clampi(chord, 0, 7);
    int notes[4], nn = 0;
    for (int i = 0; i < 4; i++)
        if (td_chord_iv[chord][i] != -128)
            notes[nn++] = clampi(note + td_chord_iv[chord][i], 0, 127);
    if (flags & NF_SEQ) {
        if ((flags & NF_LEGATO) && slides(m)) {                /* acid slide: the held notes glide, no new attack */
            int k = 0;
            for (int i = 0; i < TD_VOICES && k < nn; i++) {
                td_voice_t *v = &tr->v[i];
                if (v->active && v->seq && v->gated && !v->fading) {
                    v->ptarget = (float)notes[k];
                    v->note = (uint8_t)notes[k];
                    float g = m == TD_M_SYNTH ? map_tglide(e[P_SRC + SY_GLIDE]) : 0.f;
                    v->glide_t = g > 0.06f ? g : 0.06f;
                    v->gate = gate;
                    if (accent) { v->accent = 1; v->flev = 1.f; v->fst = ENV_DEC; }
                    k++;
                }
            }
            if (k > 0) {
                tr->last_pitch = (float)note;
                return;
            }
        }
        for (int i = 0; i < TD_VOICES; i++)                   /* the sequencer plays one note (or chord) at a time */
            if (tr->v[i].active && tr->v[i].seq && tr->v[i].gated)
                td_voice_gate_off(&tr->v[i]);
    }
    for (int c = 0; c < nn; c++) {
        int sounding = 0;
        td_voice_t *oldest = NULL;
        for (int i = 0; i < TD_VOICES; i++) {
            td_voice_t *v = &tr->v[i];
            if (v->active && !v->fading) {
                sounding++;
                if (!oldest || v->age < oldest->age) oldest = v;
            }
        }
        if (sounding >= TD_MAXNOTES && oldest)
            td_voice_fade(oldest, 4.f);
        td_voice_t *v = alloc_voice(tr);
        td_voice_start(v, m, e, notes[c], vel, gate, accent);
        if (m == TD_M_SYNTH && v->glide_t > 0.f && tr->last_pitch > 0.f)
            v->pitch = tr->last_pitch + (float)(notes[c] - note);
        v->seq = (flags & NF_SEQ) != 0;
        v->age = tr->age;
        if (flags & NF_LIVE) v->live_note = (uint8_t)note;
    }
    tr->last_pitch = (float)note;
}

/* ---- conditions */
static int cond_ok(int c, int32_t loop)
{
    int fill = E.fill_user || (E.ap.on && E.ap.fill);
    if (c <= TD_COND_ALWAYS || c >= TD_CONDS) return 1;
    if (c == TD_COND_FILL) return fill;
    if (c == TD_COND_NOT_FILL) return !fill;
    if (c == TD_COND_FIRST) return loop == 0;
    if (c == TD_COND_NOT_FIRST) return loop > 0;
    if (c < TD_COND_AB) {
        float p = (float)(c - TD_COND_P1 + 1) * 0.125f;
        return td_rand01(&E.rng) < p;
    }
    int i = c - TD_COND_AB;
    return loop % td_cond_ab[i][1] == td_cond_ab[i][0] - 1;
}

/* ---- events */
static void ev_push(double t, int type, int track, int pat, int step, int32_t loop)
{
    if (E.nev >= TD_EVQ)
        return;
    td_event_t *e = &E.ev[E.nev++];
    e->t = t;
    e->type = (uint8_t)type;
    e->track = (uint8_t)track;
    e->pat = (uint8_t)pat;
    e->step = (uint8_t)step;
    e->loop = loop;
}
static void decide_deck(int d, int64_t k)
{
    td_dk_t *dk = &E.dk[d];
    int64_t kp = k - dk->dec_origin;
    if (!dk->playing || kp < 0)                                /* (stopped, or starting on a later bar line) */
        return;
    td_pattern_t *P = &E.pat[dk->dec_pat];
    int ml = td_master_len(P);
    if (kp > 0 && kp % ml == 0 && dk->queued >= 0) {           /* the end of the pattern: the queued one starts */
        ev_push((double)k, EV_SWITCH, d, dk->queued, 0, 0);
        dk->dec_pat = dk->queued;
        dk->dec_origin = k;
        kp = 0;
        P = &E.pat[dk->dec_pat];
    }
    for (int t = 0; t < TD_TRACKS; t++) {
        int len = P->length[t] ? P->length[t] : 16;
        int s = (int)(kp % len);
        int32_t loop = (int32_t)(kp / len);
        const td_step_t *st = &P->step[t][s];
        if (!st->trig && !st->nlocks)
            continue;
        double sw = (s & 1) ? ((double)E.swing / 50.0 - 1.0) : 0.0;
        double tt = (double)k + sw + (double)st->micro / 24.0;
        if (tt < E.gpos) tt = E.gpos;
        ev_push(tt, st->trig ? EV_TRIG : EV_LOCK, d * TD_TRACKS + t, dk->dec_pat, s, loop);
    }
}
static void decide_step(int64_t k)
{
    for (int d = 0; d < TD_DECKS; d++) decide_deck(d, k);
}
/* a deck moved (a scratch let go: its origin changed): the steps already decided ahead, decided again from there */
void td_seq_redecide_deck(int d)
{
    for (int64_t k = (int64_t)ceil(E.gpos); k < E.kdec; k++) decide_deck(d, k);
}

/* the frame of this block an event falls on: the first frame at or after its time (a large number: a later block) */
static int ev_off(double t, double g0, double rate)
{
    double x = (t - g0) / rate;
    if (x <= 0.0) return 0;
    x = ceil(x - 1e-6);
    return x > 1e6 ? 1000000 : (int)x;
}
void td_seq_block(double g0, double rate, td_event_t *out, int *off, int *nout, int max)
{
    int n = 0;
    *nout = 0;
    if (!E.playing || rate <= 0.0)
        return;
    double g1 = g0 + TD_BLK * rate;
    while ((double)(E.kdec - 1) < g1)
        decide_step(E.kdec++);
    for (int t = 0; t < TD_RT_TRACKS; t++) {
        td_track_t *tr = &E.trk[t];
        int o;
        while (tr->rt_left > 0 && (o = ev_off(tr->rt_next, g0, rate)) < TD_BLK && n < max) {
            td_event_t *e = &out[n];
            off[n++] = o;
            e->t = tr->rt_next;
            e->type = EV_RETRIG;
            e->track = (uint8_t)t;
            e->loop = ++tr->rt_i;
            tr->rt_next += tr->rt_int;
            tr->rt_left--;
        }
    }
    for (int i = 0; i < E.nev && n < max;) {
        int o = ev_off(E.ev[i].t, g0, rate);
        if (o < TD_BLK) {
            off[n] = o;
            out[n++] = E.ev[i];
            E.ev[i] = E.ev[--E.nev];
        } else
            i++;
    }
    int o;
    while ((o = ev_off((double)E.next_bar * 16.0, g0, rate)) < TD_BLK && n < max) {
        td_event_t *e = &out[n];
        off[n++] = o;
        e->t = (double)E.next_bar * 16.0;
        e->type = EV_BAR;
        e->loop = (int32_t)E.next_bar++;
    }
    while ((o = ev_off((double)E.next_clk / 6.0, g0, rate)) < TD_BLK && n < max) {
        if (E.midi_clock) {
            td_event_t *e = &out[n];
            off[n++] = o;
            e->t = (double)E.next_clk / 6.0;
            e->type = EV_CLOCK;
        }
        E.next_clk++;
    }
    *nout = n;
}

static void midi_note(int t, int note, int vel, int64_t frame, int32_t gate)
{
    td_track_t *tr = &E.trk[t];
    if (!E.midi_notes)
        return;
    if (tr->midi_note >= 0)
        td_midi_push((uint8_t)(0x80 | t), (uint8_t)tr->midi_note, 0, 3, frame);
    td_midi_push((uint8_t)(0x90 | t), (uint8_t)note, (uint8_t)clampi(vel, 1, 127), 3, frame);
    tr->midi_note = note;
    tr->midi_off = gate >= 0 ? frame + gate : -1;
}

static void release_seq(int t)
{
    td_track_t *tr = &E.trk[t];
    for (int i = 0; i < TD_VOICES; i++)
        if (tr->v[i].active && tr->v[i].seq && tr->v[i].gated)
            td_voice_gate_off(&tr->v[i]);
}

static void fire_trig(const td_event_t *ev, int64_t frame)
{
    int t = ev->track, pt = RT_TR(t);                          /* (a runtime track; its pattern's track) */
    td_track_t *tr = &E.trk[t];
    td_pattern_t *P = &E.pat[ev->pat];
    const td_step_t *st = &P->step[pt][ev->step];
    int m = P->machine[pt];
    if (!st->trig)
        return;
    if (td_eff_muted(t) || !cond_ok(st->cond, ev->loop)) {
        if (tr->slide_pending) { release_seq(t); tr->slide_pending = 0; }
        return;
    }
    tr->lk_mask = 0;
    for (int i = 0; i < st->nlocks && i < TD_LPS; i++) {
        const td_lock_t *l = &P->lock[pt][ev->step][i];
        if (l->param < TD_NPARAMS) {
            tr->lk_val[l->param] = l->value;
            tr->lk_mask |= 1ull << l->param;
        }
    }
    td_track_eff(t);
    const float *e = tr->eff;
    int note = st->note;
    if (E.ap.on && slides(m))
        note = clampi(note + E.ap.transpose, 0, 127);
    float vel = vel_gain(st->vel) * (st->accent ? 1.4f : 1.f);
    double fps = td_frames_per_step();
    float ls = td_len_steps[clampi((int)(e[P_LEN] + 0.5f), 0, 16)];
    int32_t gate = ls < 0.f ? -1 : (int32_t)(ls * fps);
    int slide_now = st->slide && slides(m);
    if (slide_now && gate >= 0 && gate < (int32_t)(fps * 1.6))
        gate = (int32_t)(fps * 1.6);                           /* held into the next step */
    int flags = NF_SEQ | (st->accent ? NF_ACCENT : 0) | (tr->slide_pending ? NF_LEGATO : 0);
    td_note_on(t, note, vel, gate, flags);
    tr->slide_pending = (uint8_t)slide_now;
    tr->rt_left = tr->rt_total = st->retrig;
    tr->rt_i = 0;
    tr->rt_int = td_rrate_steps[st->rrate & 3];
    tr->rt_next = ev->t + tr->rt_int;
    tr->rt_note = (uint8_t)note;
    tr->rt_vel = vel;
    tr->rt_fade = e[P_RVEL] * (1.f / 64.f);
    tr->rt_accent = st->accent;
    lfo_trig(t);
    if (pt == 0)                                               /* (track 1 keys its own deck's sidechain) */
        E.duck_att[RT_DECK(t)] = 1;
    midi_note(t, note, (int)(st->vel * (st->accent ? 1.25f : 1.f)), frame, gate);
}
static void fire_retrig(const td_event_t *ev, int64_t frame)
{
    int t = ev->track;
    td_track_t *tr = &E.trk[t];
    if (td_eff_muted(t) || tr->rt_total <= 0)
        return;
    float f = (float)ev->loop / (float)tr->rt_total, fd = tr->rt_fade;
    float k = fd < 0.f ? 1.f + fd * f : (1.f - fd) + fd * f;
    float vel = tr->rt_vel * fmaxf(0.02f, k);
    int32_t gate = (int32_t)(tr->rt_int * td_frames_per_step());
    td_note_on(t, tr->rt_note, vel, gate, NF_SEQ | (tr->rt_accent ? NF_ACCENT : 0));
    if (RT_TR(t) == 0)
        E.duck_att[RT_DECK(t)] = 1;
    float v127 = powf(clampf(vel, 0.f, 1.f), 1.f / 1.5f) * 127.f;
    midi_note(t, tr->rt_note, (int)v127, frame, gate);
}
static void fire_locks(const td_event_t *ev)
{
    int t = ev->track, pt = RT_TR(t);
    td_track_t *tr = &E.trk[t];
    td_pattern_t *P = &E.pat[ev->pat];
    const td_step_t *st = &P->step[pt][ev->step];
    for (int i = 0; i < st->nlocks && i < TD_LPS; i++) {
        const td_lock_t *l = &P->lock[pt][ev->step][i];
        if (l->param < TD_NPARAMS) {
            tr->lk_val[l->param] = l->value;
            tr->lk_mask |= 1ull << l->param;
        }
    }
}

void td_seq_apply(const td_event_t *ev, int64_t frame)
{
    switch (ev->type) {
    case EV_SWITCH: {                                          /* (track: the deck) */
        td_dk_t *dk = &E.dk[ev->track];
        dk->pat = ev->pat;
        dk->origin = (int64_t)llround(ev->t);
        if (dk->queued == ev->pat)
            dk->queued = -1;
        if (ev->track == E.on_air && !E.blending) td_pattern_tempo();   /* (a blend glides the tempo itself) */
        td_sync_cur();
        break;
    }
    case EV_BAR:
        memmove(&E.bar_frame[1], &E.bar_frame[0], sizeof(E.bar_frame[0]) * 7);
        E.bar_frame[0] = frame;
        if (E.nbar < 8) E.nbar++;
        td_auto_bar(ev->loop);
        break;
    case EV_CLOCK:
        td_midi_push(0xF8, 0, 0, 1, frame);
        break;
    case EV_LOCK: fire_locks(ev); break;
    case EV_TRIG: fire_trig(ev, frame); break;
    case EV_RETRIG: fire_retrig(ev, frame); break;
    default: break;
    }
}

/* one deck's notes released, its retrigs and scheduled events dropped (the deck stops; the clock may run on) */
void td_seq_stop_deck(int d)
{
    for (int pt = 0; pt < TD_TRACKS; pt++) {
        int t = d * TD_TRACKS + pt;
        td_track_t *tr = &E.trk[t];
        release_seq(t);
        tr->rt_left = 0;
        tr->slide_pending = 0;
        if (tr->midi_note >= 0 && E.midi_notes)
            td_midi_push((uint8_t)(0x80 | t), (uint8_t)tr->midi_note, 0, 3, (int64_t)E.frames_int);
        tr->midi_note = -1;
    }
    for (int i = 0; i < E.nev;) {
        const td_event_t *e = &E.ev[i];
        int mine = e->type == EV_SWITCH ? e->track == d : (e->type == EV_TRIG || e->type == EV_LOCK) && RT_DECK(e->track) == d;
        if (mine) E.ev[i] = E.ev[--E.nev];
        else i++;
    }
}
void td_seq_stop_notes(void)
{
    for (int d = 0; d < TD_DECKS; d++) td_seq_stop_deck(d);
    E.nev = 0;
}
void td_seq_reset_transport(void)
{
    E.gpos = 0.0;
    E.kdec = 0;
    E.next_bar = 0;
    E.next_clk = 0;
    for (int d = 0; d < TD_DECKS; d++) {
        E.dk[d].origin = E.dk[d].dec_origin = 0;
        E.dk[d].dec_pat = E.dk[d].pat;
    }
    E.nev = 0;
    E.nbar = 0;
    for (int t = 0; t < TD_RT_TRACKS; t++) {
        E.trk[t].rt_left = 0;
        E.trk[t].slide_pending = 0;
    }
}

/* ---- the key */
int td_scale_snap(int note)
{
    const uint8_t *sc = td_scales[clampi(E.scale, 0, 7)];
    for (int d = 0; d <= 6; d++) {
        int a = note - d, b = note + d;
        if (a >= 0 && sc[((a - E.scale_root) % 12 + 12) % 12]) return a;
        if (b <= 127 && sc[((b - E.scale_root) % 12 + 12) % 12]) return b;
    }
    return clampi(note, 0, 127);
}
static int scale_degrees(int *semis)                           /* the scale's pitch classes from the root */
{
    int n = 0;
    for (int i = 0; i < 12; i++)
        if (td_scales[clampi(E.scale, 0, 7)][i]) semis[n++] = i;
    return n;
}

/* ---- AUTOPILOT: an arranger in 8-bar phrases (after DUB-1's generative arrangement) */
enum { AP_GROOVE, AP_DROP, AP_BREAK, AP_BUILD };
static int ap_pick(td_auto_t *a, const float *w, int n)
{
    float s = 0.f;
    for (int i = 0; i < n; i++) s += w[i];
    float r = td_rand01(&a->rng) * s;
    for (int i = 0; i < n; i++) {
        if (r < w[i]) return i;
        r -= w[i];
    }
    return n - 1;
}
static void ap_next_chord(td_auto_t *a)
{
    int semis[12], n = scale_degrees(semis);
    if (n < 2) { a->transpose = 0; return; }
    float w[12] = {0};
    if (a->degree == 0) {                                      /* from home: iv, VI, VII, v, III */
        static const int cand[5] = {3, 5, 6, 4, 2};
        static const float cw[5] = {0.3f, 0.25f, 0.2f, 0.15f, 0.1f};
        for (int i = 0; i < 5; i++) w[cand[i] % n] += cw[i];
    } else {                                                   /* away: tends to resolve home */
        w[0] = 0.5f;
        for (int i = 1; i < n; i++)
            if (i != a->degree) w[i] = 0.5f / (float)(n - 2 > 0 ? n - 2 : 1);
    }
    a->degree = ap_pick(a, w, n);
    int s = semis[a->degree];
    a->transpose = s > 6 ? s - 12 : s;
}
static void ap_phrase(td_auto_t *a)
{
    td_pattern_t *P = &E.pat[E.cur];
    memset(a->override, 0, sizeof a->override);
    if (a->phrases == 0)
        a->kind = AP_GROOVE;
    else {
        static const float W[4][4] = {
            {0.35f, 0.35f, 0.2f, 0.1f},   /* after a groove */
            {0.5f, 0.0f, 0.3f, 0.2f},     /* after a drop */
            {0.8f, 0.2f, 0.0f, 0.0f},     /* after a breakdown */
            {0.9f, 0.1f, 0.0f, 0.0f},     /* after a build */
        };
        a->kind = ap_pick(a, W[clampi(a->kind, 0, 3)], 4);
    }
    a->drop_bars = 1 + (td_rand01(&a->rng) < 0.4f);
    a->fill_bar = (a->kind == AP_GROOVE && td_rand01(&a->rng) < 0.5f) || a->kind != AP_GROOVE;
    if (a->phrases > 0 && --a->chord_wait <= 0) {
        ap_next_chord(a);
        a->chord_wait = 1 + (td_rand01(&a->rng) < 0.5f);
    }
    a->phrases++;
    (void)P;
}
static void ap_walk(td_auto_t *a)                              /* (the deck on air's tracks) */
{
    td_pattern_t *P = &E.pat[E.dk[E.on_air].pat];
    int base = E.on_air * TD_TRACKS;
    for (int t = 0; t < TD_TRACKS; t++) {
        int m = P->machine[t];
        float *w = a->walk_t[base + t];
        int walk_cut = m == TD_M_SYNTH || m == TD_M_FM || m == TD_M_HAT;
        w[0] = walk_cut ? clampf(w[0] * 0.8f + (td_rand01(&a->rng) - 0.5f) * 10.f, -16.f, 16.f) : 0.f;
        w[1] = P->param[t][P_DLY] > 0 ? clampf(w[1] * 0.8f + (td_rand01(&a->rng) - 0.5f) * 12.f, -20.f, 20.f) : 0.f;
        w[2] = m == TD_M_FM ? clampf(w[2] * 0.8f + (td_rand01(&a->rng) - 0.5f) * 12.f, -18.f, 18.f) : 0.f;
    }
}
static void ap_bar(td_auto_t *a, int pb)                       /* (the deck on air's tracks: base + 0..7) */
{
    td_pattern_t *P = &E.pat[E.dk[E.on_air].pat];
    int base = E.on_air * TD_TRACKS;
    memset(a->mute, 0, sizeof a->mute);
    a->fill = 0;
    a->dfb_t = 0.f;
    for (int t = 0; t < TD_RT_TRACKS; t++) a->cut_ramp_t[t] = 0.f;
    switch (a->kind) {
    case AP_DROP:
        if (pb >= 8 - a->drop_bars) a->mute[base] = 1;
        a->fill = pb == 7;
        break;
    case AP_BREAK:
        for (int t = 0; t < TD_TRACKS; t++) {
            int drums = t < 4, m = P->machine[t];                           /* (tracks 1-4: the drums) */
            if (pb < 4 && drums && m != TD_M_HAT) a->mute[base + t] = 1;    /* breakdown: the drums but the hats */
            if (pb >= 4 && t == 0) a->mute[base + t] = 1;                    /* build: no kick yet */
            if (!drums && slides(m))
                a->cut_ramp_t[base + t] = pb < 4 ? -28.f + 8.f * (float)pb : 4.f * (float)(pb - 3);
        }
        if (pb >= 4) a->dfb_t = 8.f * (float)(pb - 3);
        a->fill = pb == 7;
        break;
    case AP_BUILD:
        for (int t = 4; t < TD_TRACKS; t++)
            if (slides(P->machine[t])) a->cut_ramp_t[base + t] = 2.5f * (float)pb;
        a->dfb_t = 3.5f * (float)pb;
        a->fill = pb >= 6;
        if (pb == 7) a->mute[base] = 1;
        break;
    default:
        a->fill = pb == 7 && a->fill_bar;
        break;
    }
}
void td_auto_bar(int64_t bar)
{
    td_auto_t *a = &E.ap;
    if (!a->on)
        return;
    int pb = (int)(bar % 8);
    if (pb == 0)
        ap_phrase(a);
    a->bar = bar;
    if (a->phrases == 0)                                       /* turned on mid-phrase: waits for the phrase start */
        return;
    ap_walk(a);
    ap_bar(a, pb);
}
void td_auto_block(void)
{
    td_auto_t *a = &E.ap;
    for (int t = 0; t < TD_RT_TRACKS; t++) {
        for (int i = 0; i < 3; i++) {
            float tgt = a->on ? a->walk_t[t][i] : 0.f;
            a->walk[t][i] += (tgt - a->walk[t][i]) * 0.0012f;
            if (fabsf(a->walk[t][i]) < 1e-5f && tgt == 0.f) a->walk[t][i] = 0.f;
        }
        float ct = a->on ? a->cut_ramp_t[t] : 0.f;
        a->cut_ramp[t] += (ct - a->cut_ramp[t]) * 0.0015f;
        if (fabsf(a->cut_ramp[t]) < 1e-5f && ct == 0.f) a->cut_ramp[t] = 0.f;
    }
    float dt = a->on ? a->dfb_t : 0.f;
    a->dfb += (dt - a->dfb) * 0.0015f;
    if (fabsf(a->dfb) < 1e-5f && dt == 0.f) a->dfb = 0.f;
}

/* ---- the public step and track API (the active pattern) */
static int ok_ts(int t, int s) { return t >= 0 && t < TD_TRACKS && s >= 0 && s < TD_STEPS && E.inited; }
static void init_trig(td_step_t *st, int t)
{
    const int16_t *p = E.pat[E.cur].param[t];
    st->trig = 1;
    st->note = (uint8_t)clampi(p[P_NOTE], 0, 127);
    st->vel = (uint8_t)clampi(p[P_VEL], 1, 127);
    st->cond = (uint8_t)clampi(p[P_COND], 0, TD_CONDS - 1);
    st->retrig = (uint8_t)clampi(p[P_RTRG], 0, 8);
    st->rrate = (uint8_t)clampi(p[P_RRATE], 0, 3);
    st->micro = (int8_t)clampi(p[P_MICRO], -23, 23);
    st->slide = st->accent = 0;
}
static void clear_step(td_step_t *st)
{
    st->trig = 0;
    st->nlocks = 0;
    st->slide = st->accent = 0;
    st->retrig = 0;
    st->micro = 0;
    st->cond = 0;
}
void td_step_set(int track, int step, int on)
{
    td_lock();
    if (ok_ts(track, step)) {
        td_step_t *st = &E.pat[E.cur].step[track][step];
        if (on && !st->trig) init_trig(st, track);
        else if (!on && st->trig) clear_step(st);
    }
    td_unlock();
}
void td_step_toggle(int track, int step)
{
    td_lock();
    if (ok_ts(track, step)) {
        td_step_t *st = &E.pat[E.cur].step[track][step];
        if (st->trig) clear_step(st);
        else init_trig(st, track);
    }
    td_unlock();
}
void td_step_note(int track, int step, int note, int velocity)
{
    td_lock();
    if (ok_ts(track, step)) {
        td_step_t *st = &E.pat[E.cur].step[track][step];
        if (!st->trig) init_trig(st, track);
        st->note = (uint8_t)clampi(note, 0, 127);
        st->vel = (uint8_t)clampi(velocity, 1, 127);
    }
    td_unlock();
}
void td_step_micro(int track, int step, int micro)
{
    td_lock();
    if (ok_ts(track, step)) E.pat[E.cur].step[track][step].micro = (int8_t)clampi(micro, -23, 23);
    td_unlock();
}
void td_step_retrig(int track, int step, int count, int rate)
{
    td_lock();
    if (ok_ts(track, step)) {
        td_step_t *st = &E.pat[E.cur].step[track][step];
        st->retrig = (uint8_t)clampi(count, 0, 8);
        st->rrate = (uint8_t)clampi(rate, 0, 3);
    }
    td_unlock();
}
void td_step_cond(int track, int step, int cond)
{
    td_lock();
    if (ok_ts(track, step)) E.pat[E.cur].step[track][step].cond = (uint8_t)clampi(cond, 0, TD_CONDS - 1);
    td_unlock();
}
void td_step_slide(int track, int step, int on)
{
    td_lock();
    if (ok_ts(track, step)) E.pat[E.cur].step[track][step].slide = on != 0;
    td_unlock();
}
void td_step_accent(int track, int step, int on)
{
    td_lock();
    if (ok_ts(track, step)) E.pat[E.cur].step[track][step].accent = on != 0;
    td_unlock();
}
/* TRIG-page parameters a held step edits directly (its fields): -1 if a real lock */
static int step_field(int param) { return param < TD_SLOTS && param != P_LEN && param != P_RVEL ? param : -1; }
static void set_field(td_step_t *st, int f, int v)
{
    switch (f) {
    case P_NOTE: st->note = (uint8_t)clampi(v, 0, 127); break;
    case P_VEL: st->vel = (uint8_t)clampi(v, 1, 127); break;
    case P_RTRG: st->retrig = (uint8_t)clampi(v, 0, 8); break;
    case P_RRATE: st->rrate = (uint8_t)clampi(v, 0, 3); break;
    case P_COND: st->cond = (uint8_t)clampi(v, 0, TD_CONDS - 1); break;
    case P_MICRO: st->micro = (int8_t)clampi(v, -23, 23); break;
    default: break;
    }
}
static int get_field(const td_step_t *st, int f)
{
    switch (f) {
    case P_NOTE: return st->note;
    case P_VEL: return st->vel;
    case P_RTRG: return st->retrig;
    case P_RRATE: return st->rrate;
    case P_COND: return st->cond;
    default: return st->micro;
    }
}
void td_step_lock(int track, int step, int param, int value)
{
    td_lock();
    if (ok_ts(track, step) && param >= 0 && param < TD_NPARAMS) {
        td_pattern_t *P = &E.pat[E.cur];
        td_step_t *st = &P->step[track][step];
        int f = step_field(param);
        if (f >= 0)
            set_field(st, f, value);
        else {
            const td_pdesc_t *d = td_pdesc(P->machine[track], param);
            int v = clampi(value, d->min, d->max), i;
            td_lock_t *l = P->lock[track][step];
            for (i = 0; i < st->nlocks; i++)
                if (l[i].param == param) break;
            if (i < st->nlocks || st->nlocks < TD_LPS) {
                l[i].param = (uint8_t)param;
                l[i].value = (int16_t)v;
                if (i == st->nlocks) st->nlocks++;
            }
        }
    }
    td_unlock();
}
void td_step_unlock(int track, int step, int param)
{
    td_lock();
    if (ok_ts(track, step) && param >= 0 && param < TD_NPARAMS) {
        td_pattern_t *P = &E.pat[E.cur];
        td_step_t *st = &P->step[track][step];
        int f = step_field(param);
        if (f >= 0)
            set_field(st, f, P->param[track][param]);
        else {
            td_lock_t *l = P->lock[track][step];
            for (int i = 0; i < st->nlocks; i++)
                if (l[i].param == param) {
                    l[i] = l[--st->nlocks];
                    break;
                }
        }
    }
    td_unlock();
}
void td_step_clear_locks(int track, int step)
{
    td_lock();
    if (ok_ts(track, step)) E.pat[E.cur].step[track][step].nlocks = 0;
    td_unlock();
}
int td_step_lock_get(int track, int step, int param, int *value)
{
    int r = 0;
    td_lock();
    if (ok_ts(track, step) && param >= 0 && param < TD_NPARAMS) {
        const td_pattern_t *P = &E.pat[E.cur];
        const td_step_t *st = &P->step[track][step];
        int f = step_field(param);
        if (f >= 0) {
            if (st->trig) { if (value) *value = get_field(st, f); r = 1; }
        } else
            for (int i = 0; i < st->nlocks; i++)
                if (P->lock[track][step][i].param == param) {
                    if (value) *value = P->lock[track][step][i].value;
                    r = 1;
                    break;
                }
    }
    td_unlock();
    return r;
}
void td_steps_get(int track, td_step_view_t out[TD_STEPS])
{
    if (!out) return;
    memset(out, 0, sizeof(td_step_view_t) * TD_STEPS);
    td_lock();
    if (track >= 0 && track < TD_TRACKS && E.inited)
        for (int s = 0; s < TD_STEPS; s++) {
            const td_step_t *st = &E.pat[E.cur].step[track][s];
            td_step_view_t *o = &out[s];
            o->trig = st->trig;
            o->note = st->note;
            o->velocity = st->vel;
            o->cond = st->cond;
            o->retrig = st->retrig;
            o->retrig_rate = st->rrate;
            o->nlocks = st->nlocks;
            o->micro = st->micro;
            o->slide = st->slide;
            o->accent = st->accent;
        }
    td_unlock();
}
void td_clear_track(int track)
{
    td_lock();
    if (track >= 0 && track < TD_TRACKS && E.inited)
        for (int s = 0; s < TD_STEPS; s++) clear_step(&E.pat[E.cur].step[track][s]);
    td_unlock();
}

/* ---- generative tools */
void td_euclid(int track, int pulses, int steps, int rotation)
{
    td_lock();
    if (track >= 0 && track < TD_TRACKS && E.inited) {
        td_pattern_t *P = &E.pat[E.cur];
        steps = clampi(steps, 1, TD_STEPS);
        pulses = clampi(pulses, 0, steps);
        int len = P->length[track];
        for (int i = 0; i < len; i++) {
            int j = ((i - rotation) % steps + steps) % steps;
            int on = (j * pulses) % steps < pulses;
            td_step_t *st = &P->step[track][i];
            if (on && !st->trig) init_trig(st, track);
            else if (!on && st->trig) clear_step(st);
        }
    }
    td_unlock();
}
void td_rotate(int track, int amount)
{
    static td_step_t st[TD_STEPS];
    static td_lock_t lk[TD_STEPS][TD_LPS];
    td_lock();
    if (track >= 0 && track < TD_TRACKS && E.inited) {
        td_pattern_t *P = &E.pat[E.cur];
        int len = P->length[track];
        memcpy(st, P->step[track], sizeof(td_step_t) * (size_t)len);
        memcpy(lk, P->lock[track], sizeof(lk[0]) * (size_t)len);
        for (int i = 0; i < len; i++) {
            int j = ((i + amount) % len + len) % len;
            P->step[track][j] = st[i];
            memcpy(P->lock[track][j], lk[i], sizeof lk[0]);
        }
    }
    td_unlock();
}
void td_dice(int track, int amount)
{
    td_lock();
    if (track >= 0 && track < TD_TRACKS && E.inited) {
        td_pattern_t *P = &E.pat[E.cur];
        uint32_t rng = 0xD1CE5EEDu ^ (++E.dice_count * 2654435761u) ^ ((uint32_t)track * 40503u);
        if (!rng) rng = 1;
        int len = P->length[track], m = P->machine[track];
        int nch = (int)((float)clampi(amount, 0, 100) * (float)len / 100.f + 0.5f);
        int idx[TD_STEPS];
        for (int i = 0; i < len; i++) idx[i] = i;
        for (int i = 0; i < nch; i++) {                         /* a partial shuffle: nch distinct steps */
            int j = i + (int)(td_rand01(&rng) * (float)(len - i));
            if (j >= len) j = len - 1;
            int tmp = idx[i]; idx[i] = idx[j]; idx[j] = tmp;
            td_step_t *st = &P->step[track][idx[i]];
            float r = td_rand01(&rng);
            if (r < 0.4f) {
                if (st->trig) clear_step(st);
                else {
                    init_trig(st, track);
                    if (td_is_melodic(m) && m != TD_M_SAMPLE) st->note = (uint8_t)td_scale_snap(st->note);
                }
            } else if (!st->trig) {
                init_trig(st, track);
            }
            if (st->trig) {
                if (td_is_melodic(m) && m != TD_M_SAMPLE && td_rand01(&rng) < 0.7f) {
                    int off = (int)(td_rand01(&rng) * 25.f) - 12;
                    st->note = (uint8_t)td_scale_snap(clampi(st->note + off, 0, 127));
                }
                st->vel = (uint8_t)clampi(55 + (int)(td_rand01(&rng) * 72.f), 1, 127);
            }
        }
    }
    td_unlock();
}

void td_set_scale(int root, int scale)
{
    td_lock();
    E.scale_root = ((root % 12) + 12) % 12;
    E.scale = clampi(scale, 0, 7);
    td_unlock();
}
int td_scale_note(int base, int index)
{
    td_lock();
    int n = td_scale_snap(clampi(base, 0, 127));
    if (n < base) {                                            /* the first scale note at or above base */
        for (int k = base; k <= 127; k++)
            if (td_scales[clampi(E.scale, 0, 7)][((k - E.scale_root) % 12 + 12) % 12]) { n = k; break; }
    }
    for (int i = 0; i < index && n < 127; i++) {
        n++;
        while (n < 127 && !td_scales[clampi(E.scale, 0, 7)][((n - E.scale_root) % 12 + 12) % 12]) n++;
    }
    td_unlock();
    return clampi(n, 0, 127);
}

void td_autopilot(int on)
{
    td_lock();
    td_auto_t *a = &E.ap;
    if (on && !a->on) {
        a->rng = 0xA5170D1Bu ^ (++a->count * 0x9E3779B9u);    /* deterministic: the n-th time since td_init */
        if (!a->rng) a->rng = 1;
        a->on = 1;
        a->phrases = 0;
        a->kind = AP_GROOVE;
        a->degree = 0;
        a->transpose = 0;
        a->chord_wait = 1;
        memset(a->mute, 0, sizeof a->mute);
        memset(a->override, 0, sizeof a->override);
        memset(a->walk_t, 0, sizeof a->walk_t);
        a->fill = 0;
    } else if (!on && a->on) {
        a->on = 0;
        memset(a->mute, 0, sizeof a->mute);
        a->fill = 0;
        a->transpose = 0;
        memset(a->walk_t, 0, sizeof a->walk_t);
        memset(a->cut_ramp_t, 0, sizeof a->cut_ramp_t);
        a->dfb_t = 0.f;
    }
    td_unlock();
}
