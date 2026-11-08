/* SPDX-License-Identifier: GPL-3.0-only */
/* The TRIGDECK engine for the browser: what the AudioWorklet and the page need, in plain numbers. (TrigCore's own
 * functions, td_play, td_pattern_queue, td_step_toggle ..., are exported as they are; see build.sh.) */
#include "td_internal.h"
#include <stdlib.h>

#define WEB_MAX 1024
static float g_out[WEB_MAX * 2];
static td_state_t g_st;

#define EXPORT(name) __attribute__((export_name(#name)))

EXPORT(tdw_init) int tdw_init(void) { return td_init(); }

/* render n frames (interleaved stereo) into the shared buffer; returns its address */
EXPORT(tdw_render) float *tdw_render(int n)
{
    if (n > WEB_MAX) n = WEB_MAX;
    td_render(g_out, (uint32_t)n, 0);
    return g_out;
}

/* the state, read after tdw_state() */
EXPORT(tdw_state) void tdw_state(void) { td_get_state(&g_st); }
EXPORT(tdw_pattern) int tdw_pattern(void) { return g_st.pattern; }
EXPORT(tdw_queued) int tdw_queued(void) { return g_st.queued_pattern; }
EXPORT(tdw_bpm) float tdw_bpm(void) { return g_st.bpm; }
EXPORT(tdw_playing) int tdw_playing(void) { return g_st.playing; }
EXPORT(tdw_step) int tdw_step(int t) { return t >= 0 && t < TD_TRACKS ? g_st.step[t] : -1; }
EXPORT(tdw_level) float tdw_level(int t) { return t >= 0 && t < TD_TRACKS ? g_st.level[t] : 0.f; }
EXPORT(tdw_length) int tdw_length(int t) { return t >= 0 && t < TD_TRACKS ? g_st.length[t] : 16; }
EXPORT(tdw_machine) int tdw_machine(int t) { return t >= 0 && t < TD_TRACKS ? g_st.machine[t] : 0; }
EXPORT(tdw_muted) int tdw_muted(int t) { return t >= 0 && t < TD_TRACKS ? g_st.muted[t] : 0; }

/* ---- the model's snapshot (the page mirrors the engine through it; layout in web/src/engine.js)
 * light part, every time:  playing, recording, pattern, queued, bpm x 100, swing, autopilot, pattern hash,
 *                          per track: machine, muted, length, playing step, level x 1e6, base[48], live[48],
 *                          master[8]
 * heavy part (full = 1):   per track and step: trig, note, velocity, cond, retrig, micro, locks, slide, accent;
 *                          per track and step, 8 locks: param, value (param -1: none);
 *                          per scene: count, then 128 x (track, param, value);
 *                          the 16 patterns' names, 12 characters each */
#define SNAP_LIGHT (8 + TD_TRACKS * (5 + 2 * TD_NPARAMS) + TD_SLOTS + 2)   /* (+ the output peaks, x 1e6) */
#define SNAP_FULL (SNAP_LIGHT + TD_TRACKS * TD_STEPS * 9 + TD_TRACKS * TD_STEPS * TD_LPS * 2 + 2 * (1 + TD_SCENE_MAX * 3) \
                   + TD_PATTERNS * TD_PATTERN_NAME)
static int32_t g_snap[SNAP_FULL];

static uint32_t pattern_hash(const td_pattern_t *P)        /* FNV-1a over the steps, locks, scenes and lengths */
{
    uint32_t h = 2166136261u;
    const uint8_t *parts[4] = {(const uint8_t *)P->step, (const uint8_t *)P->lock, (const uint8_t *)P->scene, P->length};
    size_t sizes[4] = {sizeof P->step, sizeof P->lock, sizeof P->scene, sizeof P->length};
    for (int k = 0; k < 4; k++)
        for (size_t i = 0; i < sizes[k]; i++) h = (h ^ parts[k][i]) * 16777619u;
    h = (h ^ P->nscene[0]) * 16777619u;
    for (int p = 0; p < TD_PATTERNS; p++)                      /* (the names: a copy or a load renames another) */
        for (int k = 0; k < TD_PATTERN_NAME; k++) h = (h ^ (uint8_t)E.pat[p].name[k]) * 16777619u;
    return (h ^ P->nscene[1]) * 16777619u;
}

EXPORT(tdw_snapshot_len) int tdw_snapshot_len(int full) { return full ? SNAP_FULL : SNAP_LIGHT; }
EXPORT(tdw_snapshot) int32_t *tdw_snapshot(int full)
{
    td_get_state(&g_st);
    int32_t *o = g_snap;
    *o++ = g_st.playing; *o++ = g_st.recording; *o++ = g_st.pattern; *o++ = g_st.queued_pattern;
    *o++ = (int32_t)lroundf(g_st.bpm * 100.f); *o++ = g_st.swing; *o++ = g_st.autopilot;
    *o++ = (int32_t)pattern_hash(&E.pat[E.cur]);
    for (int t = 0; t < TD_TRACKS; t++) {
        *o++ = g_st.machine[t]; *o++ = g_st.muted[t]; *o++ = g_st.length[t]; *o++ = g_st.step[t];
        *o++ = (int32_t)lroundf(g_st.level[t] * 1e6f);
        for (int p = 0; p < TD_NPARAMS; p++) *o++ = td_param_get(t, p);
        for (int p = 0; p < TD_NPARAMS; p++) *o++ = td_param_live(t, p);
    }
    for (int s = 0; s < TD_SLOTS; s++) *o++ = td_global_get(s);
    *o++ = (int32_t)lroundf(g_st.peak_l * 1e6f);
    *o++ = (int32_t)lroundf(g_st.peak_r * 1e6f);
    if (full) {
        const td_pattern_t *P = &E.pat[E.cur];
        for (int t = 0; t < TD_TRACKS; t++)
            for (int s = 0; s < TD_STEPS; s++) {
                const td_step_t *st = &P->step[t][s];
                *o++ = st->trig; *o++ = st->note; *o++ = st->vel; *o++ = st->cond; *o++ = st->retrig; *o++ = st->micro;
                *o++ = st->nlocks; *o++ = st->slide; *o++ = st->accent;
            }
        for (int t = 0; t < TD_TRACKS; t++)
            for (int s = 0; s < TD_STEPS; s++)
                for (int k = 0; k < TD_LPS; k++) {
                    int on = k < P->step[t][s].nlocks;
                    *o++ = on ? P->lock[t][s][k].param : -1;
                    *o++ = on ? P->lock[t][s][k].value : 0;
                }
        for (int sc = 0; sc < 2; sc++) {
            *o++ = P->nscene[sc];
            for (int k = 0; k < TD_SCENE_MAX; k++) {
                int on = k < P->nscene[sc];
                *o++ = on ? P->scene[sc][k].track : -1;
                *o++ = on ? P->scene[sc][k].param : -1;
                *o++ = on ? P->scene[sc][k].value : 0;
            }
        }
        for (int p = 0; p < TD_PATTERNS; p++)
            for (int k = 0; k < TD_PATTERN_NAME; k++) *o++ = (uint8_t)E.pat[p].name[k];
    }
    return g_snap;
}
EXPORT(tdw_pattern_hash) int32_t tdw_pattern_hash(void) { return (int32_t)pattern_hash(&E.pat[E.cur]); }

/* ---- descriptions and formatting (a second, silent instance on the page answers these: by machine, not track)
 * info layout (32 bytes): name[16], short_name[8], i16 min, i16 max, i16 def, u8 fmt, u8 lockable */
static td_param_info_t g_info;
static char g_text[32];
EXPORT(tdw_info) td_param_info_t *tdw_info(int machine, int param)
{
    td_set_machine(0, machine);                               /* (track 1 of this silent instance takes the machine) */
    return td_param_info(0, param, &g_info) ? &g_info : 0;
}
EXPORT(tdw_ginfo) td_param_info_t *tdw_ginfo(int slot) { return td_global_info(slot, &g_info) ? &g_info : 0; }
EXPORT(tdw_format) char *tdw_format(int machine, int param, int value)
{
    if (!tdw_info(machine, param)) return 0;
    td_format_value(&g_info, value, g_text, sizeof g_text);
    return g_text;
}
EXPORT(tdw_gformat) char *tdw_gformat(int slot, int value)
{
    if (!td_global_info(slot, &g_info)) return 0;
    td_format_value(&g_info, value, g_text, sizeof g_text);
    return g_text;
}
EXPORT(tdw_cond_name) const char *tdw_cond_name(int c) { return td_cond_name(c); }
EXPORT(tdw_cond_count) int tdw_cond_count(void) { return TD_CONDS; }
EXPORT(tdw_machine_name) const char *tdw_machine_name(int m) { return td_machine_name(m); }
EXPORT(tdw_pack_name) const char *tdw_pack_name(int p) { return td_pack_name(p); }

/* ---- the project, saved in the browser: save into / load from a buffer of tdw_project_size() bytes */
static uint8_t *g_proj;
EXPORT(tdw_project_buf) uint8_t *tdw_project_buf(void)
{
    if (!g_proj) g_proj = (uint8_t *)malloc(td_project_size());
    return g_proj;
}
EXPORT(tdw_project_size) int tdw_project_size(void) { return (int)td_project_size(); }
EXPORT(tdw_project_save) int tdw_project_save(void) { return td_project_save(tdw_project_buf(), td_project_size()); }
EXPORT(tdw_project_load) int tdw_project_load(int n) { return td_project_load(tdw_project_buf(), (size_t)n); }

/* the current pattern's trigs: 8 tracks x 64 steps, 1 = trig (written into the shared buffer as floats) */
EXPORT(tdw_grid) float *tdw_grid(void)
{
    td_lock();
    const td_pattern_t *P = &E.pat[E.cur];
    for (int t = 0; t < TD_TRACKS; t++)
        for (int s = 0; s < TD_STEPS; s++) g_out[t * TD_STEPS + s] = P->step[t][s].trig ? 1.f : 0.f;
    td_unlock();
    return g_out;
}
