/* SPDX-License-Identifier: GPL-3.0-only */
/* The cartridge ABI: an M-VAVE FM-1 firmware (Felucca or a fork), compiled for the Mac against a host version of the
 * FM-1's hardware layer, as a dynamic library (.dylib) the app loads with dlopen. Each cartridge exports exactly these
 * C symbols (prefix fm1c_). One firmware instance per library (its globals are the firmware's RAM).
 *
 * The FM-1's panel: 27 keys F3..G5 (16 white, 11 black), 14 buttons, 7 encoders, a 240 x 240 RGB565 screen, LEDs on
 * keys and buttons, a 1 MiB NOR flash, USB MIDI. Threading: one lock inside the cartridge; fm1c_render runs on the
 * audio thread and runs the firmware's UI between audio blocks as the device does; the other calls are short. */
#pragma once
#include <stdint.h>

#define FM1C_ABI_VERSION 1

/* buttons by printed label, as Felucca's panel.c B_* order */
enum { FM1C_B_FX, FM1C_B_SEL, FM1C_B_ENV, FM1C_B_LFO, FM1C_B_EDIT, FM1C_B_GLO, FM1C_B_HOME, FM1C_B_SAVE,
       FM1C_B_ARP, FM1C_B_SEQ, FM1C_B_PLAY, FM1C_B_REC, FM1C_B_OCTDN, FM1C_B_OCTUP, FM1C_NBUTTONS };
/* encoders by printed role, as Felucca's panel.c EN_* order */
enum { FM1C_E_SELECT, FM1C_E_ALGORITHM, FM1C_E_PRESETS, FM1C_E_K1, FM1C_E_K2, FM1C_E_K3, FM1C_E_K4, FM1C_NENCODERS };

typedef struct {
    uint32_t abi;                 /* FM1C_ABI_VERSION */
    char name[32];                /* "SLOOP", "X0X" */
    char version[16];             /* "2.8.5" */
    char author[48];
    char license[16];             /* "GPL-3.0" */
    char url[96];
    uint32_t sample_rate;         /* the rate fm1c_render produces (the FM-1: 44100) */
} fm1c_info_t;

typedef struct {
    uint32_t keys_lit, keys_dim;  /* bit k: key k (0 = F3) */
    uint32_t buttons_lit;         /* bit b: FM1C_B_* */
    float peak_l, peak_r;         /* since the last call */
    uint32_t ui_frames;
} fm1c_state_t;

typedef struct { int16_t x, y, w, h; uint32_t argb; char text[24]; } fm1c_text_t;

void fm1c_info(fm1c_info_t *out);
int fm1c_init(const char *flash_path);                    /* 0 ok, 1 damaged flash kept aside, < 0 failure */
void fm1c_render(float *interleaved_lr, uint32_t frames, uint64_t host_time);
void fm1c_key(int key, int down);                          /* 0..26 */
void fm1c_button(int button, int down);
void fm1c_encoder(int encoder, int steps);
void fm1c_barrier(void);                                   /* input before this applies before input after it */
void fm1c_midi_in(uint8_t status, uint8_t d1, uint8_t d2);
int fm1c_midi_out(uint8_t b[3], int *n, int64_t *frames_after_anchor, uint64_t *anchor_host_time);
void fm1c_get_state(fm1c_state_t *out);
uint32_t fm1c_copy_screen(uint32_t *argb240x240, uint32_t last_frame);   /* returns the frame; copies only if newer */
int fm1c_screen_texts(fm1c_text_t *out, int max);          /* the text tap (optional: return 0 if not supported) */
int fm1c_flash_dirty(void);
int fm1c_save_flash(const char *path);
void fm1c_panic(void);
