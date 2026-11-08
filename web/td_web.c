/* SPDX-License-Identifier: GPL-3.0-only */
/* The TRIGDECK engine for the browser: what the AudioWorklet and the page need, in plain numbers. (TrigCore's own
 * functions, td_play, td_pattern_queue, td_step_toggle ..., are exported as they are; see build.sh.) */
#include "td_internal.h"

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
