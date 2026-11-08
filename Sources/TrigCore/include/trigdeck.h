/* SPDX-License-Identifier: GPL-3.0-only */
/* TRIGDECK engine: an 8-track groovebox (synthesised drum machines, synth, FM, sampler), an Elektron-style sequencer
 * (trigs, parameter locks, conditions, micro timing, retrigs), two scenes morphed by the crossfader, two deck buses
 * that can be scratched, performance FX and master FX. See docs/VISION.md.
 *
 * Threading: one internal lock. td_render() is for the audio thread and is real-time safe (no allocation, no I/O, no
 * unbounded loops); every other call is short and may come from any thread. Values are integers in each parameter's
 * own range (td_param_info); the UI formats them with td_format_value. */
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TD_SAMPLE_RATE 48000
#define TD_TRACKS 8
#define TD_DECKS 2                 /* deck A: tracks 0-3, deck B: tracks 4-7 */
#define TD_STEPS 64
#define TD_PATTERNS 16
#define TD_PATTERN_NAME 12             /* a pattern's name: up to 11 characters */
#define TD_SLOTS 8                 /* parameters per page: knobs A..H */
#define TD_SAMPLE_SLOTS 16

typedef enum { TD_PAGE_TRIG, TD_PAGE_SRC, TD_PAGE_FLTR, TD_PAGE_AMP, TD_PAGE_FX, TD_PAGE_LFO, TD_PAGES } td_page_t;
#define TD_PAGE_MASTER 100         /* the global page (td_global_*), not a track page */
/* a track parameter: page * TD_SLOTS + slot (0 .. TD_PAGES * TD_SLOTS - 1) */
#define TD_PARAM(page, slot) ((page) * TD_SLOTS + (slot))
#define TD_NPARAMS (TD_PAGES * TD_SLOTS)

typedef enum { TD_M_KICK, TD_M_SNARE, TD_M_HAT, TD_M_CLAP, TD_M_TOM, TD_M_SYNTH, TD_M_FM, TD_M_SAMPLE, TD_MACHINES } td_machine_t;

typedef enum {                      /* trig conditions */
    TD_COND_ALWAYS, TD_COND_FILL, TD_COND_NOT_FILL, TD_COND_FIRST, TD_COND_NOT_FIRST,
    TD_COND_P1,                    /* probability: TD_COND_P1 + k = (k+1) * 12.5 %, k 0..6 (12 % .. 87 %) */
    TD_COND_AB = TD_COND_P1 + 7,   /* A:B: TD_COND_AB + index into td_cond_ab[] (1:2, 2:2, 1:3 ... 4:4) */
    TD_CONDS = TD_COND_AB + 10
} td_cond_t;

typedef enum {                      /* performance effects (beat FX section) */
    TD_PERF_REPEAT, TD_PERF_STUTTER, TD_PERF_TAPESTOP, TD_PERF_DELAYTHROW, TD_PERF_REVERBTHROW, TD_PERF_FILTER,
    TD_PERF_CRUSH,
    TD_PERF_DUBHOLD,               /* (added) freezes the master tape delay into a clean loop; target ignored */
    /* (added, after Mixxx's effects) inserts like REPEAT .. CRUSH, in time with the division; amount = depth */
    TD_PERF_ECHO,                  /* a ping-pong echo of the division; amount: feedback */
    TD_PERF_FLANGER,               /* one sweep a division; amount: feedback */
    TD_PERF_PHASER,                /* one sweep a division; amount: depth */
    TD_PERF_TRANS,                 /* the live sound gated on and off at the division; amount: how short */
    TD_PERF_AUTOPAN,               /* left, right, once a division; amount: width */
    TD_PERF_REVERSE,               /* the division captured, then played backwards, looping */
    TD_PERF_HALF,                  /* the division captured, played at half speed (an octave down), looping */
    TD_PERF_NOISE,                 /* white noise rising over the sound; amount: level and brightness */
    TD_PERF_DISTORT,               /* drive; amount: how much */
    TD_PERF_SWEEP,                 /* a resonant low-pass sweeping once a division; amount: resonance */
    TD_PERFS
} td_perf_t;
typedef enum { TD_TARGET_DECK_A, TD_TARGET_DECK_B, TD_TARGET_MASTER } td_target_t;

typedef enum { TD_FMT_INT, TD_FMT_BIPOLAR, TD_FMT_PERCENT, TD_FMT_HZ, TD_FMT_MS, TD_FMT_NOTE, TD_FMT_ENUM, TD_FMT_ONOFF } td_fmt_t;
typedef struct {
    char name[16];                 /* "CUTOFF" */
    char short_name[8];            /* "CUT" */
    int16_t min, max, def;
    uint8_t fmt;                   /* td_fmt_t */
    uint8_t lockable;              /* can be parameter-locked on a step */
} td_param_info_t;

/* ---- lifecycle and audio */
int td_init(void);                                       /* a default project (a beat on deck A, a bass line on deck B) */
void td_render(float *interleaved_lr, uint32_t frames, uint64_t host_time);   /* audio thread */

/* ---- describing parameters (for knobs and the screen) */
const char *td_machine_name(int machine);                /* "KICK" */
const char *td_page_name(int page);                      /* "TRIG", "SRC", ... ; TD_PAGE_MASTER: "MASTER" */
int td_param_info(int track, int param, td_param_info_t *out);   /* depends on the track's machine; 0 if no parameter there */
int td_global_info(int slot, td_param_info_t *out);      /* the MASTER page's 8 parameters */
int td_format_value(const td_param_info_t *info, int value, char *out, int n);   /* "1.2 kHz", "-12", "1/16" */
const char *td_cond_name(int cond);                      /* "ALWAYS", "FILL", "50%", "1:2" ... */
const char *td_perf_name(int perf);                      /* "REPEAT" ... */

/* (added) sound packs: a kit for all 8 tracks (machines and every page but TRIG, scenes), made by TRIGDECK's own
 * engines, on a fixed voice map (1 kick, 2 open hat, 3 snare / clap, 4 closed hat, 5 bass, 6 keys / chords, 7 perc /
 * lead, 8 sample or extra). Loading copies the kit into the current pattern and keeps its steps; revert puts back the
 * kit the last load replaced. */
/* (added) a new project: stops, then the default patterns, tempos, sounds and MASTER page (the samples stay) */
void td_project_new(void);

int td_pack_count(void);
const char *td_pack_name(int pack);                      /* "808" ... */
void td_pack_load(int pack);
int td_pack_revert(void);                                /* 0: reverted, -1: nothing to revert */
/* (added) the A:B table behind TD_COND_AB: {A, B} = 1:2, 2:2, 1:3, 2:3, 3:3, 1:4, 2:4, 3:4, 4:4, 1:8 */
extern const uint8_t td_cond_ab[10][2];
const char *td_scale_name(int scale);                    /* (added) "MINOR", "MAJOR", "DORIAN", ... (0..7) */

/* ---- transport and global */
void td_play(int on);
void td_restart(void);                                   /* back to step 1 (playing or not) */
void td_set_tempo(float bpm);                            /* 30 .. 300 */
void td_tap_tempo(void);
void td_nudge(float amount);                             /* -1 .. 1: a temporary tempo bend while non-zero (DJ nudge) */
void td_set_swing(int percent);                          /* 50 (straight) .. 80 */
void td_global_set(int slot, int value);                 /* MASTER page */
int td_global_get(int slot);
void td_fill(int on);                                    /* fill mode while held */
void td_record(int on);                                  /* live recording of td_trigger into the active pattern */
/* (added) the key the generative tools and AUTOPILOT work in: root 0..11 (0 = C), scale 0..7 (td_scale_name) */
void td_set_scale(int root, int scale);
int td_scale_note(int base, int index);                  /* (added) the index-th scale note at or above base (pads) */
/* (added) AUTOPILOT: an arranger in 8-bar phrases (kick drops, breakdowns, builds, fills, slow parameter walks, a chord
 * progression) on the playing pattern. It never edits the pattern: its mutes, offsets and transposition are an overlay
 * that is removed when it is turned off. A mute set by the user always wins. */
void td_autopilot(int on);

/* ---- tracks */
void td_set_machine(int track, int machine);
void td_set_sample(int track, int sample_slot);          /* SAMPLE machine: which slot it plays */
void td_param_set(int track, int param, int value);      /* the track's base value (what plays without locks) */
int td_param_get(int track, int param);
void td_mute(int track, int on);
void td_set_length(int track, int steps);                /* 1 .. 64 */
void td_set_length_repeat(int track, int steps);         /* (added) the same, the new steps repeating the old ones */
void td_trigger(int track, int note, int velocity);      /* live play (recorded when recording); note 0..127 */
void td_release(int track, int note);
void td_clear_track(int track);
/* (added) generative tools on the active pattern, over the track's length */
void td_euclid(int track, int pulses, int steps, int rotation);   /* Euclidean trigs E(pulses, steps) rotated, tiled over
                                                                    * the length; steps that stay on keep notes and locks */
void td_rotate(int track, int amount);                   /* steps move later by amount (trigs, notes, locks, micro, conds) */
void td_dice(int track, int amount);                     /* 0..100 % of the steps changed: trig on/off, note (melodic
                                                          * machines, in the scale, within an octave), velocity */

/* ---- steps of the active pattern */
void td_step_set(int track, int step, int on);
void td_step_toggle(int track, int step);
void td_step_note(int track, int step, int note, int velocity);
void td_step_micro(int track, int step, int micro);      /* -23 .. +23 (1/24 of a step) */
void td_step_retrig(int track, int step, int count, int rate);   /* count 0..8 extra hits; rate 1/16 .. 1/64 as 0..3 */
void td_step_cond(int track, int step, int cond);
void td_step_lock(int track, int step, int param, int value);    /* a parameter lock (a lock-only step if the step has no trig) */
void td_step_unlock(int track, int step, int param);
void td_step_clear_locks(int track, int step);
/* (added) acid, for SYNTH and FM: SLIDE glides into the next trig without retriggering the envelopes; ACCENT is louder
 * with more filter envelope (drums: louder) */
void td_step_slide(int track, int step, int on);
void td_step_accent(int track, int step, int on);

typedef struct {
    uint8_t trig, note, velocity, cond, retrig, retrig_rate, nlocks;
    int8_t micro;
    uint8_t slide, accent;                               /* (added) */
} td_step_view_t;
void td_steps_get(int track, td_step_view_t out[TD_STEPS]);
int td_step_lock_get(int track, int step, int param, int *value);   /* 1 if locked */

/* ---- patterns */
void td_pattern_queue(int pattern);                      /* switches at the end of the current pattern (at once when stopped) */
void td_pattern_copy(int from, int to);
const char *td_pattern_name(int pattern);                /* "DEEP"; "" without one (copied with the pattern, saved) */
void td_pattern_clear(int pattern);

/* ---- scenes and the crossfader */
void td_scene_set(int scene, int track, int param, int value);   /* scene 0 = A, 1 = B */
void td_scene_unset(int scene, int track, int param);
void td_scene_clear(int scene);
int td_scene_get(int scene, int track, int param, int *value);   /* 1 if the scene holds it */
void td_crossfader(float x);                             /* 0 = scene A, 0.5 = the pattern as written, 1 = scene B */

/* ---- decks (buses) */
void td_deck_level(int deck, float level);               /* 0 .. 1 (fader) */
void td_deck_drive(int deck, float drive);               /* 0 .. 1 (TRIM) */
void td_scratch(int deck, int touching, float rate);     /* touching: the deck plays its recent audio at `rate` (1 = forward
                                                          * at speed, 0 = held still, -1 = backward); release: back in time */
/* ---- performance FX */
void td_perf(int perf, int division, int target, int on, float amount);   /* division 0..7: 1/32, 1/16, 1/8, 1/4, 1/2, 1, 2, 4 bars */
/* (added) resampling: the deck's last `bars` bars (1, 2 or 4; ending at the last bar line) into sample slot `slot`
 * (mono, short fades, named "DECK A 1" ...). 0 ok, -1 if not enough audio recorded or the pool is full. */
int td_capture(int deck, int bars, int slot);

/* ---- state, for the UI and the LEDs */
typedef struct {
    int32_t playing, recording, fill;
    int32_t pattern, queued_pattern;                     /* queued -1: none */
    float bpm;
    int32_t swing;
    int32_t step[TD_TRACKS];                             /* each track's playing step, -1 stopped */
    int32_t machine[TD_TRACKS], muted[TD_TRACKS], length[TD_TRACKS];
    float level[TD_TRACKS];                              /* each track's output level (peak, 0..1, decays) */
    float deck_level[TD_DECKS];                          /* each bus's output level */
    int32_t scratching[TD_DECKS];
    float crossfader;
    int32_t perf_active, perf_type, perf_target;
    float peak_l, peak_r;
    uint32_t bar, beat;                                  /* the transport position (1-based) */
    int32_t autopilot, phrase_bar;                       /* (added) AUTOPILOT on; the bar in its 8-bar phrase, 1..8 */
    int32_t scale_root, scale, transpose;                /* (added) the key; AUTOPILOT's chord transposition */
} td_state_t;
void td_get_state(td_state_t *s);
/* the value a parameter has right now on a track, after locks, scenes and the LFO (for the knobs' arcs) */
int td_param_live(int track, int param);

/* ---- samples (SAMPLE machine) */
int td_sample_load(int slot, const float *mono, uint32_t frames, uint32_t rate, const char *name);   /* copies; 0 ok */
const char *td_sample_name(int slot);

/* ---- the project (all patterns, sounds, scenes, settings; not the samples' audio) */
size_t td_project_size(void);
int td_project_save(void *buf, size_t n);                /* bytes written, -1 on error */
int td_project_load(const void *buf, size_t n);          /* 0 ok; refuses a damaged or foreign blob (version, CRC) */

/* ---- MIDI (clock out, the sequencer to external gear) */
int td_midi_out(uint8_t b[3], int *n, int64_t *frames_after_anchor, uint64_t *anchor_host_time);
/* (added) clock: 24 ppq with start / stop (default on); notes: the sequencer's trigs on channel track + 1 (default off) */
void td_midi_config(int clock, int notes);

/* ---- tests: render n frames offline, returns the peak */
float td_render_offline(uint32_t frames);

#ifdef __cplusplus
}
#endif
