/* SPDX-License-Identifier: GPL-3.0-only */
/* SloopCore: the SLOOP groovebox firmware (github.com/isod89/sloop-fm1, based on Felucca) compiled for macOS.
 * The firmware's own UI, sequencer, engines, effects and flash storage run unchanged; this API stands in for
 * the FM-1's panel (27 keys, 14 buttons, 7 encoders), its 240x240 screen, its audio DAC and its NOR flash.
 *
 * Thread safety: every call takes one internal lock. sloop_render() is meant for the audio thread; the other
 * calls are short and may come from any thread. Input calls are queued and applied in order at the next UI
 * pass (every ~3 ms of rendered audio), never more than one change per key or button per pass, so a press and
 * a release sent together still read as a tap. */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SLOOP_SAMPLE_RATE 44100
#define SLOOP_SCREEN_W 240
#define SLOOP_SCREEN_H 240
#define SLOOP_NKEYS 27            /* F3 .. G5: 16 white keys, 11 black keys */
#define SLOOP_NTRACKS 4           /* 3 synth tracks and the drum track (index 3) */

/* the FM-1's buttons, by printed label */
typedef enum {
    SLOOP_B_FX, SLOOP_B_SEL, SLOOP_B_ENV, SLOOP_B_LFO, SLOOP_B_EDIT, SLOOP_B_GLO, SLOOP_B_HOME, SLOOP_B_SAVE,
    SLOOP_B_ARP, SLOOP_B_SEQ, SLOOP_B_PLAY, SLOOP_B_REC, SLOOP_B_OCTDN, SLOOP_B_OCTUP, SLOOP_NBUTTONS
} sloop_button_t;

/* the FM-1's encoders, by printed role */
typedef enum {
    SLOOP_E_SELECT, SLOOP_E_ALGORITHM, SLOOP_E_PRESETS, SLOOP_E_K1, SLOOP_E_K2, SLOOP_E_K3, SLOOP_E_K4,
    SLOOP_NENCODERS
} sloop_encoder_t;

/* Boot the firmware. flash_path: a 1 MiB file holding the FM-1's NOR (projects, presets, settings, samples);
 * loaded if it exists, created by sloop_save_flash otherwise. NULL: RAM only. Returns 0; 1 if the file was damaged
 * (it is renamed to <path>.damaged, never overwritten, and SLOOP starts blank); -1 if this build's memory layout cannot
 * address the user samples (the flash image must lie above the built-in samples within 4 GiB). */
int sloop_init(const char *flash_path);

/* Render interleaved stereo float (-1..1) at SLOOP_SAMPLE_RATE. Runs the UI between blocks, as the FM-1 does. */
void sloop_render(float *out, uint32_t frames);
/* The same, noting the host time (mach ticks) out[0] will be heard at: MIDI out is then stamped against it. */
void sloop_render_at(float *out, uint32_t frames, uint64_t host_time);

/* Panel input. key 0..26 (0 = F3, chromatic). */
void sloop_key(int key, int down);
void sloop_button(int button, int down);
void sloop_encoder(int encoder, int steps);       /* + = clockwise */
/* the input sent so far is applied before anything sent after this (a press must be seen before the next) */
void sloop_input_barrier(void);
/* select track 0..3 (in order with the input queue; as ALGORITHM does, not during a free take) */
void sloop_select_track(int track);
int sloop_white_key(int index);                   /* the key of white key 0..15, -1 if none */
int sloop_black_key(int index);                   /* the key of black key 0..10, -1 if none */

/* Direct values for absolute controls (faders): applied at once, clamped to the firmware's ranges. */
void sloop_set_track_level(int track, int value);  /* 0..127 */
void sloop_set_master_filter(int value);           /* -64 (low-pass) .. 0 (off) .. 63 (high-pass) */
void sloop_set_bpm(int bpm);                       /* 40..240 */
void sloop_set_swing(int swing);                   /* 0..100 */
void sloop_set_master_volume(int value);           /* 0..127 (the FM-1's MASTER knob) */

/* External MIDI into the firmware (as the FM-1's USB MIDI port): one short message. */
void sloop_midi_in(uint8_t status, uint8_t d1, uint8_t d2);
/* MIDI the firmware sends (clock, the sequencer to MIDI out): 1 and the bytes in b[0..2] / *n, 0 when empty. */
int sloop_midi_out(uint8_t b[3], int *n);
/* The same, with when: the message belongs frames_after_anchor frames after the first frame of the last
 * sloop_render_at call, whose host time is *anchor_host_time (0 if sloop_render_at was never called). */
int sloop_midi_out_timed(uint8_t b[3], int *n, int64_t *frames_after_anchor, uint64_t *anchor_host_time);

/* State, for the controller's LEDs and the window. */
typedef struct {
    uint32_t keys_lit;        /* bit k: key k lit (full) */
    uint32_t keys_dim;        /* bit k: key k glowing (a guide) */
    uint32_t buttons_lit;     /* bit b (sloop_button_t): button lit */
    int32_t selected_track;   /* 0..3 */
    int32_t playing, recording;
    int32_t bpm, swing;
    int32_t step;             /* the selected track's step playing, -1 stopped */
    int32_t track_level[SLOOP_NTRACKS];
    int32_t master_filter;
    int32_t octave;
    float peak_l, peak_r;     /* the output's peak since the last call (0..1) */
    uint32_t ui_frames;
    int32_t layer;            /* the layer held (0 none, 1 FX punch, 2 EDIT erase, 3 ARP roll, 4 SEQ steps, 5 SEL key,
                               * 6 GLO mix, 7 SAVE song) */
    int32_t layer_shown;      /* the screen shows that layer's 16 tiles (after a short hold, or at once on use) */
    int32_t home;             /* the TRACKS screen (HOME) is shown, not a page */
} sloop_state_t;
void sloop_get_state(sloop_state_t *s);

/* What the screen says: every text SLOOP has drawn and that is still visible, where it is (screen pixels, 0..239)
 * and its colour. The app reads the knobs' names off the dial strip and a layer's tiles off the tile grid. */
typedef struct {
    int16_t x, y, w, h;
    uint32_t argb;
    char text[24];
} sloop_text_t;
int sloop_screen_texts(sloop_text_t *out, int max);   /* returns how many were written */

/* Names, as SLOOP shows them: a track's sound ("808 BOOM", the drum track "DRUM") and a drum sound's short name, by its
 * lane 0..15 (the white keys on the drum track: "KICK", "SNARE"...). b holds at least 16 bytes. */
void sloop_track_name(int track, char *b);
void sloop_drum_name(int lane, char *b);
int sloop_drum_lane_picked(void);                      /* the drum sound picked last (SEQ writes it), 0..15 */

/* The screen as 0xAARRGGBB words, SLOOP_SCREEN_W x SLOOP_SCREEN_H; returns the UI frame it shows. */
uint32_t sloop_copy_screen(uint32_t *argb);
/* The same, only if the UI has drawn a frame since last_frame (argb untouched otherwise); returns the frame. */
uint32_t sloop_copy_screen_if_new(uint32_t *argb, uint32_t last_frame);

/* The flash image: changed since the last save? */
int sloop_flash_dirty(void);
int sloop_save_flash(const char *path);            /* 0 on success; writes the live sections first */

/* Stop everything that sounds (notes, arp holds, transport). */
void sloop_panic(void);

/* Offline helpers for tests: render n UI frames (16 ms each), discarding the audio; returns the peak (0..1). */
float sloop_run_frames(uint32_t n);

#ifdef __cplusplus
}
#endif
