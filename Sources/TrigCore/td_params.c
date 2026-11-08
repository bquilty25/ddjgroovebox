/* SPDX-License-Identifier: GPL-3.0-only */
/* TRIGDECK parameters: every page's 8 slots (A..H), per machine, with ranges, defaults and how values read.
 *
 * TRIG   NOTE  VELOCITY  LENGTH  RETRIG  RTRG RATE  RTRG VEL  COND  MICRO
 *        (held step: NOTE, VELOCITY, RETRIG, RTRG RATE, COND and MICRO edit the step itself; LENGTH and RTRG VEL lock)
 * SRC    KICK    TUNE DECAY SWEEP SWTIME CLICK DRIVE TONE HOLD
 *        SNARE   TUNE DECAY NOISE NDECAY SNAP BODY TONE DRIVE
 *        HAT     TUNE DECAY OPEN COLOR BRIGHT RESO SPREAD DRIVE
 *        CLAP    TUNE DECAY SPREAD BURSTS TONE ROOM RAND DRIVE
 *        TOM     TUNE DECAY SWEEP SWTIME FM NOISE TONE DRIVE
 *        SYNTH   TUNE SHAPE DETUNE INTVL SUB NOISE CHORD GLIDE
 *        FM      TUNE RATIO INDEX FDBK ENVDEC ENVAMT FINE CHORD
 *        SAMPLE  TUNE SLOT START END SLICES REVERSE LOOP GAIN
 * FLTR   CUTOFF RESO TYPE ENV ATTACK DECAY KEYTRK DRIVE
 * AMP    ATTACK HOLD DECAY SUSTAIN RELEASE PAN LEVEL OVERDRIVE
 * FX     DELAY REVERB CHORUS BITS SRR LOWCUT DUCK WIDTH
 * LFO    SPEED MULT WAVE DEST DEPTH FADE MODE PHASE
 * MASTER DLY TIME  DLY FDBK  DLY TONE  REV SIZE  REV DAMP  COMP  FILTER  VOLUME
 *
 * A name always reads the same way wherever it appears (td_format_value finds the unit by the name). */
#include "td_internal.h"
#include <stdio.h>

#define LK 1
static const td_pdesc_t TRIG_PAGE[8] = {
    {"NOTE", "NOT", 0, 127, KD_NOTE, LK},       {"VELOCITY", "VEL", 1, 127, KD_INT, LK},
    {"LENGTH", "LEN", 0, 16, KD_LEN, LK},       {"RETRIG", "RTG", 0, 8, KD_INT, LK},
    {"RTRG RATE", "RRT", 0, 3, KD_RRATE, LK},   {"RTRG VEL", "RVL", -64, 63, KD_BIP, LK},
    {"COND", "CND", 0, TD_CONDS - 1, KD_COND, LK}, {"MICRO", "MIC", -23, 23, KD_BIP, LK},
};
static const int16_t TRIG_DEF[8] = {60, 100, 4, 0, 2, -16, 0, 0};

#define TUNE_D {"TUNE", "TUN", -24, 24, KD_SEMI, LK}
#define DECAY_D {"DECAY", "DEC", 0, 127, KD_TDEC, LK}
#define DRIVE_D {"DRIVE", "DRV", 0, 127, KD_PCT, LK}
#define TONE_D {"TONE", "TON", 0, 127, KD_PCT, LK}
#define NOISE_D {"NOISE", "NOI", 0, 127, KD_PCT, LK}
#define SWTIME_D {"SWTIME", "STM", 0, 127, KD_TSWEEP, LK}
#define CHORD_D {"CHORD", "CHD", 0, 7, KD_CHORD, LK}
static const td_pdesc_t SRC_PAGE[TD_MACHINES][8] = {
    [TD_M_KICK] = {TUNE_D, DECAY_D, {"SWEEP", "SWP", 0, 48, KD_SEMI, LK}, SWTIME_D, {"CLICK", "CLK", 0, 127, KD_PCT, LK},
                   DRIVE_D, TONE_D, {"HOLD", "HLD", 0, 127, KD_THOLD, LK}},
    [TD_M_SNARE] = {TUNE_D, DECAY_D, NOISE_D, {"NDECAY", "NDC", 0, 127, KD_TDEC, LK}, {"SNAP", "SNP", 0, 127, KD_PCT, LK},
                    {"BODY", "BDY", 0, 127, KD_PCT, LK}, TONE_D, DRIVE_D},
    [TD_M_HAT] = {TUNE_D, DECAY_D, {"OPEN", "OPN", 0, 127, KD_PCT, LK}, {"COLOR", "COL", 0, 127, KD_PCT, LK},
                  {"BRIGHT", "BRT", 0, 127, KD_PCT, LK}, {"RESO", "RES", 0, 127, KD_PCT, LK},
                  {"SPREAD", "SPR", 0, 127, KD_PCT, LK}, DRIVE_D},
    [TD_M_CLAP] = {TUNE_D, DECAY_D, {"SPREAD", "SPR", 0, 127, KD_PCT, LK}, {"BURSTS", "BST", 1, 8, KD_INT, LK}, TONE_D,
                   {"ROOM", "ROM", 0, 127, KD_PCT, LK}, {"RAND", "RND", 0, 127, KD_PCT, LK}, DRIVE_D},
    [TD_M_TOM] = {TUNE_D, DECAY_D, {"SWEEP", "SWP", 0, 24, KD_SEMI, LK}, SWTIME_D, {"FM", "FM", 0, 127, KD_PCT, LK},
                  NOISE_D, TONE_D, DRIVE_D},
    [TD_M_SYNTH] = {TUNE_D, {"SHAPE", "SHP", 0, 127, KD_SHAPE, LK}, {"DETUNE", "DET", 0, 127, KD_PCT, LK},
                    {"INTVL", "INT", -24, 24, KD_SEMI, LK}, {"SUB", "SUB", 0, 127, KD_PCT, LK}, NOISE_D, CHORD_D,
                    {"GLIDE", "GLD", 0, 127, KD_TGLIDE, LK}},
    [TD_M_FM] = {TUNE_D, {"RATIO", "RAT", 0, 15, KD_RATIO, LK}, {"INDEX", "IDX", 0, 127, KD_PCT, LK},
                 {"FDBK", "FB", 0, 127, KD_PCT, LK}, {"ENVDEC", "EDC", 0, 127, KD_TDEC, LK},
                 {"ENVAMT", "EAM", 0, 127, KD_PCT, LK}, {"FINE", "FIN", -64, 63, KD_BIP, LK}, CHORD_D},
    [TD_M_SAMPLE] = {TUNE_D, {"SLOT", "SLT", 0, TD_SAMPLE_SLOTS - 1, KD_SLOT, LK}, {"START", "STA", 0, 127, KD_PCT, LK},
                     {"END", "END", 0, 127, KD_PCT, LK}, {"SLICES", "SLC", 0, 3, KD_SLICES, LK},
                     {"REVERSE", "REV", 0, 1, KD_ONOFF, LK}, {"LOOP", "LOP", 0, 1, KD_ONOFF, LK},
                     {"GAIN", "GAN", -24, 12, KD_DB, LK}},
};
static const int16_t SRC_DEF[TD_MACHINES][8] = {
    [TD_M_KICK] = {0, 62, 30, 38, 45, 20, 96, 12},
    [TD_M_SNARE] = {0, 42, 85, 50, 55, 80, 70, 12},
    [TD_M_HAT] = {0, 34, 0, 80, 72, 20, 0, 0},
    [TD_M_CLAP] = {0, 46, 55, 4, 64, 40, 25, 10},
    [TD_M_TOM] = {0, 58, 9, 50, 18, 22, 90, 8},
    [TD_M_SYNTH] = {0, 85, 16, 0, 30, 0, 0, 0},
    [TD_M_FM] = {0, 3, 36, 0, 45, 60, 0, 0},
    [TD_M_SAMPLE] = {0, 0, 0, 127, 0, 0, 0, 0},
};
static const td_pdesc_t FLTR_PAGE[8] = {
    {"CUTOFF", "CUT", 0, 127, KD_CUT, LK},   {"RESO", "RES", 0, 127, KD_PCT, LK},
    {"TYPE", "TYP", 0, 2, KD_FTYPE, LK},     {"ENV", "ENV", -64, 63, KD_BIP, LK},
    {"ATTACK", "ATK", 0, 127, KD_TATK, LK},  {"DECAY", "DEC", 0, 127, KD_TDEC, LK},
    {"KEYTRK", "KEY", 0, 127, KD_PCT, LK},   {"DRIVE", "DRV", 0, 127, KD_PCT, LK},
};
static const int16_t FLTR_DEF_DRUM[8] = {127, 0, 0, 0, 0, 40, 0, 0};
static const int16_t FLTR_DEF_SYNTH[8] = {70, 30, 0, 24, 0, 45, 64, 0};
static const td_pdesc_t AMP_PAGE[8] = {
    {"ATTACK", "ATK", 0, 127, KD_TATK, LK},  {"HOLD", "HLD", 0, 127, KD_THOLD, LK},
    {"DECAY", "DEC", 0, 127, KD_TDEC, LK},   {"SUSTAIN", "SUS", 0, 127, KD_PCT, LK},
    {"RELEASE", "REL", 0, 127, KD_TREL, LK}, {"PAN", "PAN", -64, 63, KD_PAN, LK},
    {"LEVEL", "LVL", 0, 127, KD_INT, LK},    {"OVERDRIVE", "OVD", 0, 127, KD_PCT, LK},
};
static const int16_t AMP_DEF_DRUM[8] = {0, 0, 127, 127, 127, 0, 100, 0};
static const int16_t AMP_DEF_SYNTH[8] = {2, 0, 60, 90, 40, 0, 100, 0};
static const int16_t AMP_DEF_FM[8] = {0, 0, 60, 0, 50, 0, 100, 0};
static const td_pdesc_t FX_PAGE[8] = {
    {"DELAY", "DLY", 0, 127, KD_PCT, LK},  {"REVERB", "REV", 0, 127, KD_PCT, LK},
    {"CHORUS", "CHO", 0, 127, KD_PCT, LK}, {"BITS", "BIT", 0, 127, KD_BITS, LK},
    {"SRR", "SRR", 0, 127, KD_SRR, LK},    {"LOWCUT", "LCT", 0, 127, KD_LOWCUT, LK},
    {"DUCK", "DCK", 0, 127, KD_PCT, LK},   {"WIDTH", "WID", 0, 127, KD_PCT, LK},
};
static const td_pdesc_t LFO_PAGE[8] = {
    {"SPEED", "SPD", 0, 127, KD_INT, LK},  {"MULT", "MUL", 0, 11, KD_MULT, LK},
    {"WAVE", "WAV", 0, 4, KD_LWAVE, LK},   {"DEST", "DST", 0, 32, KD_LDEST, LK},
    {"DEPTH", "DEP", -64, 63, KD_BIP, LK}, {"FADE", "FAD", -64, 63, KD_BIP, LK},
    {"MODE", "MOD", 0, 3, KD_LMODE, LK},   {"PHASE", "PHS", 0, 127, KD_INT, LK},
};
static const int16_t LFO_DEF[8] = {32, 2, 1, 9, 0, 0, 0, 0};
static const td_pdesc_t MASTER_PAGE[8] = {
    {"DLY TIME", "DTM", 0, 11, KD_DIV, 0},  {"DLY FDBK", "DFB", 0, 127, KD_PCT, 0},
    {"DLY TONE", "DTN", 0, 127, KD_PCT, 0}, {"REV SIZE", "RSZ", 0, 127, KD_PCT, 0},
    {"REV DAMP", "RDM", 0, 127, KD_PCT, 0}, {"COMP", "CMP", 0, 127, KD_PCT, 0},
    {"FILTER", "FLT", -64, 63, KD_DJF, 0},  {"VOLUME", "VOL", 0, 127, KD_INT, 0},
};
static const int16_t MASTER_DEF[8] = {6, 55, 64, 80, 55, 35, 0, 100};

const float td_len_steps[17] = {0.125f, 0.25f, 0.5f, 0.75f, 1, 1.5f, 2, 3, 4, 6, 8, 12, 16, 24, 32, 64, -1};
static const char *const LEN_NAMES[17] = {"1/8", "1/4", "1/2", "3/4", "1", "1.5", "2", "3", "4", "6", "8", "12",
                                          "16", "24", "32", "64", "INF"};
const float td_rrate_steps[4] = {1.f, 2.f / 3.f, 0.5f, 0.25f};
static const char *const RRATE_NAMES[4] = {"1/16", "1/24", "1/32", "1/64"};
const float td_div_steps[12] = {0.5f, 2.f / 3.f, 1, 1.5f, 2, 8.f / 3.f, 3, 4, 6, 8, 12, 16};
static const char *const DIV_NAMES[12] = {"1/32", "1/16T", "1/16", "1/16D", "1/8", "1/4T", "3/16", "1/4", "1/4D",
                                          "1/2", "3/4", "1/1"};
const float td_perf_div_steps[TD_PERF_DIVS] = {0.5f, 1, 2, 4, 8, 16, 32, 64};
const float td_fm_ratio[16] = {0.5f, 1, 1.5f, 2, 2.5f, 3, 3.5f, 4, 4.5f, 5, 6, 7, 8, 9, 11, 14};
#define NO -128
const int8_t td_chord_iv[8][4] = {
    {0, NO, NO, NO}, {0, 3, 7, 10}, {0, 4, 7, 11}, {0, 3, 10, 14},
    {0, 5, 7, 12},   {0, 7, 12, NO}, {0, 7, 10, 15}, {0, 7, 10, 14},
};
static const char *const CHORD_NAMES[8] = {"OFF", "MIN7", "MAJ7", "MIN9", "SUS4", "FIFTH", "OPEN7", "OPEN9"};
static const char *const MULT_NAMES[12] = {"1", "2", "4", "8", "16", "32", "64", "128", "256", "512", "1k", "2k"};
static const char *const LWAVE_NAMES[5] = {"SIN", "TRI", "SAW", "SQR", "RND"};
static const char *const LMODE_NAMES[4] = {"FREE", "TRIG", "HOLD", "ONE"};
static const char *const FTYPE_NAMES[3] = {"LP", "BP", "HP"};
static const char *const SLICES_NAMES[4] = {"OFF", "4", "8", "16"};
static const char *const DEST_NAMES[33] = {
    "OFF", "SRC A", "SRC B", "SRC C", "SRC D", "SRC E", "SRC F", "SRC G", "SRC H",
    "CUTOFF", "RESO", "F.TYPE", "F.ENV", "F.ATK", "F.DEC", "KEYTRK", "F.DRIVE",
    "A.ATK", "HOLD", "A.DEC", "SUSTAIN", "RELEASE", "PAN", "LEVEL", "OVERDRV",
    "DELAY", "REVERB", "CHORUS", "BITS", "SRR", "LOWCUT", "DUCK", "WIDTH"};
const uint8_t td_scales[8][12] = {
    {1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0}, /* MINOR */
    {1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1}, /* MAJOR */
    {1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 1, 0}, /* DORIAN */
    {1, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0}, /* PHRYGIAN */
    {1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0}, /* MIN PENT */
    {1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0}, /* MAJ PENT */
    {1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 0, 1}, /* HARM MIN */
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, /* CHROMA */
};
static const char *const SCALE_NAMES[8] = {"MINOR", "MAJOR", "DORIAN", "PHRYGIAN", "MIN PENT", "MAJ PENT", "HARM MIN",
                                           "CHROMA"};
const uint8_t td_cond_ab[10][2] = {{1, 2}, {2, 2}, {1, 3}, {2, 3}, {3, 3}, {1, 4}, {2, 4}, {3, 4}, {4, 4}, {1, 8}};
static const char *const COND_NAMES[TD_CONDS] = {
    "ALWAYS", "FILL", "!FILL", "1ST", "!1ST", "12%", "25%", "37%", "50%", "62%", "75%", "87%",
    "1:2", "2:2", "1:3", "2:3", "3:3", "1:4", "2:4", "3:4", "4:4", "1:8"};

const td_pdesc_t *td_pdesc(int machine, int param)
{
    machine = clampi(machine, 0, TD_MACHINES - 1);
    int page = param / TD_SLOTS, slot = param % TD_SLOTS;
    switch (page) {
    case TD_PAGE_TRIG: return &TRIG_PAGE[slot];
    case TD_PAGE_SRC: return &SRC_PAGE[machine][slot];
    case TD_PAGE_FLTR: return &FLTR_PAGE[slot];
    case TD_PAGE_AMP: return &AMP_PAGE[slot];
    case TD_PAGE_FX: return &FX_PAGE[slot];
    default: return &LFO_PAGE[slot];
    }
}
int td_pdef(int machine, int param)
{
    machine = clampi(machine, 0, TD_MACHINES - 1);
    int page = param / TD_SLOTS, slot = param % TD_SLOTS;
    int melodic = machine == TD_M_SYNTH;
    switch (page) {
    case TD_PAGE_TRIG: return slot == 0 && machine == TD_M_SYNTH ? 48 : TRIG_DEF[slot];
    case TD_PAGE_SRC: return SRC_DEF[machine][slot];
    case TD_PAGE_FLTR: return (melodic ? FLTR_DEF_SYNTH : FLTR_DEF_DRUM)[slot];
    case TD_PAGE_AMP: return (machine == TD_M_SYNTH ? AMP_DEF_SYNTH : machine == TD_M_FM ? AMP_DEF_FM : AMP_DEF_DRUM)[slot];
    case TD_PAGE_FX: return 0;
    default: return LFO_DEF[slot];
    }
}
const td_pdesc_t *td_gdesc(int slot) { return &MASTER_PAGE[clampi(slot, 0, 7)]; }
int td_gdef(int slot) { return MASTER_DEF[clampi(slot, 0, 7)]; }

static uint8_t kind_fmt(int kind)
{
    switch (kind) {
    case KD_INT: return TD_FMT_INT;
    case KD_BIP: case KD_SEMI: case KD_DB: case KD_PAN: case KD_DJF: return TD_FMT_BIPOLAR;
    case KD_PCT: return TD_FMT_PERCENT;
    case KD_CUT: case KD_LOWCUT: return TD_FMT_HZ;
    case KD_TDEC: case KD_TATK: case KD_THOLD: case KD_TREL: case KD_TSWEEP: case KD_TGLIDE: return TD_FMT_MS;
    case KD_NOTE: return TD_FMT_NOTE;
    case KD_ONOFF: return TD_FMT_ONOFF;
    default: return TD_FMT_ENUM;
    }
}
static void fill_info(const td_pdesc_t *d, int def, td_param_info_t *out)
{
    memset(out, 0, sizeof *out);
    snprintf(out->name, sizeof out->name, "%s", d->name);
    snprintf(out->short_name, sizeof out->short_name, "%s", d->sname);
    out->min = d->min;
    out->max = d->max;
    out->def = (int16_t)def;
    out->fmt = kind_fmt(d->kind);
    out->lockable = d->lockable;
}
int td_param_info(int track, int param, td_param_info_t *out)
{
    if (!out || track < 0 || track >= TD_TRACKS || param < 0 || param >= TD_NPARAMS)
        return 0;
    td_lock();
    int m = E.inited ? E.pat[E.cur].machine[track] : TD_M_KICK;
    td_unlock();
    fill_info(td_pdesc(m, param), td_pdef(m, param), out);
    return 1;
}
int td_global_info(int slot, td_param_info_t *out)
{
    if (!out || slot < 0 || slot >= TD_SLOTS)
        return 0;
    fill_info(&MASTER_PAGE[slot], MASTER_DEF[slot], out);
    return 1;
}

/* the kind a name reads as (every page, every machine; the same name always has the same kind) */
static int kind_of(const char *name)
{
    static const td_pdesc_t *const pages[] = {TRIG_PAGE, FLTR_PAGE, AMP_PAGE, FX_PAGE, LFO_PAGE, MASTER_PAGE};
    for (unsigned p = 0; p < sizeof pages / sizeof pages[0]; p++)
        for (int s = 0; s < 8; s++)
            if (!strcmp(pages[p][s].name, name))
                return pages[p][s].kind;
    for (int m = 0; m < TD_MACHINES; m++)
        for (int s = 0; s < 8; s++)
            if (!strcmp(SRC_PAGE[m][s].name, name))
                return SRC_PAGE[m][s].kind;
    return -1;
}
static int fmt_hz(char *out, int n, float hz)
{
    if (hz < 1000.f)
        return snprintf(out, (size_t)n, "%d Hz", (int)(hz + 0.5f));
    if (hz < 10000.f)
        return snprintf(out, (size_t)n, "%.1f kHz", hz / 1000.f);
    return snprintf(out, (size_t)n, "%d kHz", (int)(hz / 1000.f + 0.5f));
}
static int fmt_time(char *out, int n, float s)
{
    if (s < 0.0095f)
        return snprintf(out, (size_t)n, "%.1f ms", s * 1000.f);
    if (s < 0.9995f)
        return snprintf(out, (size_t)n, "%d ms", (int)(s * 1000.f + 0.5f));
    if (s < 9.95f)
        return snprintf(out, (size_t)n, "%.1f s", s);
    return snprintf(out, (size_t)n, "%d s", (int)(s + 0.5f));
}
static const char *const NOTE_NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static int fmt_enum(char *out, int n, const char *const *names, int count, int v)
{
    return snprintf(out, (size_t)n, "%s", names[clampi(v, 0, count - 1)]);
}
static int fmt_kind(int kind, int v, const td_param_info_t *info, char *out, int n)
{
    switch (kind) {
    case KD_INT: return snprintf(out, (size_t)n, "%d", v);
    case KD_BIP: case KD_SEMI: return v ? snprintf(out, (size_t)n, "%+d", v) : snprintf(out, (size_t)n, "0");
    case KD_DB: return v ? snprintf(out, (size_t)n, "%+d dB", v) : snprintf(out, (size_t)n, "0 dB");
    case KD_PCT: return snprintf(out, (size_t)n, "%d %%", (int)(v * 100.f / 127.f + 0.5f));
    case KD_NOTE: v = clampi(v, 0, 127); return snprintf(out, (size_t)n, "%s%d", NOTE_NAMES[v % 12], v / 12 - 2);
    case KD_CUT: return fmt_hz(out, n, map_cut_hz((float)v));
    case KD_LOWCUT: return fmt_hz(out, n, map_lowcut_hz((float)v));
    case KD_TDEC: return fmt_time(out, n, map_tdec((float)v));
    case KD_TATK: return fmt_time(out, n, map_tatk((float)v));
    case KD_THOLD: return fmt_time(out, n, map_thold((float)v));
    case KD_TREL: return fmt_time(out, n, map_trel((float)v));
    case KD_TSWEEP: return fmt_time(out, n, map_tsweep((float)v));
    case KD_TGLIDE: return v ? fmt_time(out, n, map_tglide((float)v)) : snprintf(out, (size_t)n, "OFF");
    case KD_ONOFF: return snprintf(out, (size_t)n, "%s", v ? "ON" : "OFF");
    case KD_LEN: return fmt_enum(out, n, LEN_NAMES, 17, v);
    case KD_RRATE: return fmt_enum(out, n, RRATE_NAMES, 4, v);
    case KD_COND: return fmt_enum(out, n, COND_NAMES, TD_CONDS, v);
    case KD_FTYPE: return fmt_enum(out, n, FTYPE_NAMES, 3, v);
    case KD_SHAPE: {
        static const char *const S[4] = {"SIN", "TRI", "SAW", "SQR"};
        float s = clampi(v, 0, 127) * (3.f / 127.f);
        int i = (int)(s + 0.5f);
        if (fabsf(s - (float)i) < 0.12f)
            return snprintf(out, (size_t)n, "%s", S[i]);
        int a = (int)s;
        return snprintf(out, (size_t)n, "%s/%s", S[a], S[a + 1 > 3 ? 3 : a + 1]);
    }
    case KD_RATIO: {
        float r = td_fm_ratio[clampi(v, 0, 15)];
        return r == floorf(r) ? snprintf(out, (size_t)n, "x%d", (int)r) : snprintf(out, (size_t)n, "x%.1f", r);
    }
    case KD_SLOT: {
        v = clampi(v, 0, TD_SAMPLE_SLOTS - 1);
        char nm[16];
        td_lock();
        snprintf(nm, sizeof nm, "%s", td_slots[v].used ? td_slots[v].name : "");
        td_unlock();
        return nm[0] ? snprintf(out, (size_t)n, "%s", nm) : snprintf(out, (size_t)n, "S%02d --", v + 1);
    }
    case KD_CHORD: return fmt_enum(out, n, CHORD_NAMES, 8, v);
    case KD_SLICES: return fmt_enum(out, n, SLICES_NAMES, 4, v);
    case KD_BITS: return v ? snprintf(out, (size_t)n, "%d bit", (int)(16.f - v * (14.f / 127.f) + 0.5f)) : snprintf(out, (size_t)n, "OFF");
    case KD_SRR: return v ? snprintf(out, (size_t)n, "1/%d", (int)(1.f + v * (31.f / 127.f) + 0.5f)) : snprintf(out, (size_t)n, "OFF");
    case KD_PAN: return v == 0 ? snprintf(out, (size_t)n, "C") : snprintf(out, (size_t)n, "%c%d", v < 0 ? 'L' : 'R', v < 0 ? -v : v);
    case KD_MULT: return fmt_enum(out, n, MULT_NAMES, 12, v);
    case KD_LWAVE: return fmt_enum(out, n, LWAVE_NAMES, 5, v);
    case KD_LDEST: return fmt_enum(out, n, DEST_NAMES, 33, v);
    case KD_LMODE: return fmt_enum(out, n, LMODE_NAMES, 4, v);
    case KD_DIV: return fmt_enum(out, n, DIV_NAMES, 12, v);
    case KD_DJF: return v == 0 ? snprintf(out, (size_t)n, "OFF") : snprintf(out, (size_t)n, "%s %d", v < 0 ? "LP" : "HP", v < 0 ? -v : v);
    default: break;
    }
    (void)info;
    return snprintf(out, (size_t)n, "%d", v);
}
int td_format_value(const td_param_info_t *info, int value, char *out, int n)
{
    if (!out || n <= 0)
        return 0;
    out[0] = 0;
    if (!info)
        return snprintf(out, (size_t)n, "%d", value);
    char name[17];
    memcpy(name, info->name, 16);
    name[16] = 0;
    int kind = kind_of(name);
    if (kind < 0) {                                         /* not one of ours: by the generic format */
        switch (info->fmt) {
        case TD_FMT_BIPOLAR: kind = KD_BIP; break;
        case TD_FMT_PERCENT: {
            int span = info->max - info->min;
            return snprintf(out, (size_t)n, "%d %%", span ? (int)((value - info->min) * 100.f / span + 0.5f) : 0);
        }
        case TD_FMT_HZ: return fmt_hz(out, n, (float)value);
        case TD_FMT_MS: return fmt_time(out, n, value / 1000.f);
        case TD_FMT_NOTE: kind = KD_NOTE; break;
        case TD_FMT_ONOFF: kind = KD_ONOFF; break;
        default: kind = KD_INT; break;
        }
    }
    int r = fmt_kind(kind, value, info, out, n);
    return r < 0 ? 0 : (r >= n ? n - 1 : r);
}

const char *td_machine_name(int machine)
{
    static const char *const N[TD_MACHINES] = {"KICK", "SNARE", "HAT", "CLAP", "TOM", "SYNTH", "FM", "SAMPLE"};
    return machine >= 0 && machine < TD_MACHINES ? N[machine] : "";
}
const char *td_page_name(int page)
{
    static const char *const N[TD_PAGES] = {"TRIG", "SRC", "FLTR", "AMP", "FX", "LFO"};
    if (page == TD_PAGE_MASTER)
        return "MASTER";
    return page >= 0 && page < TD_PAGES ? N[page] : "";
}
const char *td_cond_name(int cond) { return cond >= 0 && cond < TD_CONDS ? COND_NAMES[cond] : ""; }

/* ---- what each parameter does, for the help line (by page and name; a machine's own first, then any machine's) */
typedef struct { int8_t page, machine; const char *name, *text; } help_t;
#define ANY -1
static const help_t HELP[] = {
    /* TRIG */
    {0, ANY, "NOTE", "The note the track plays. Hold a step and turn: that step's own note."},
    {0, ANY, "VELOCITY", "How hard the note is hit: louder, and brighter on most machines."},
    {0, ANY, "LENGTH", "How long the note is held, in steps (INF: until the next note)."},
    {0, ANY, "RETRIG", "Repeats the note inside its step: rolls and stutters. 0: off."},
    {0, ANY, "RTRG RATE", "How fast the repeats come: 1/16 up to 1/64."},
    {0, ANY, "RTRG VEL", "The repeats get louder (+) or fade away (-)."},
    {0, ANY, "COND", "When the step plays: ALWAYS; FILL (only while FILL is held); 1ST; a chance (12% to 87%); or A:B, the "
                     "Ath pass in every B (1:4 plays once every 4 bars)."},
    {0, ANY, "MICRO", "Moves the note a little early (-) or late (+), in 24ths of a step: the feel."},
    /* SRC: every machine */
    {1, ANY, "TUNE", "The pitch, up or down in semitones."},
    {1, ANY, "DECAY", "How long the sound rings out."},
    {1, ANY, "DRIVE", "Saturation: warmer and louder, then distorted."},
    {1, ANY, "TONE", "The sound's brightness."},
    {1, ANY, "NOISE", "Noise mixed into the sound."},
    {1, ANY, "CHORD", "Plays a chord from each note: MIN7, MAJ7, MIN9, SUS4..."},
    /* SRC: per machine */
    {1, TD_M_KICK, "SWEEP", "How far the pitch falls at the start: the kick's punch."},
    {1, TD_M_KICK, "SWTIME", "How quickly that pitch fall happens."},
    {1, TD_M_KICK, "CLICK", "The click at the start, which helps a kick cut through."},
    {1, TD_M_KICK, "HOLD", "Keeps the body at full level before it decays: a longer, boomier kick."},
    {1, TD_M_SNARE, "NOISE", "The snares: noise against the drum's body."},
    {1, TD_M_SNARE, "NDECAY", "How long the snares (the noise) ring."},
    {1, TD_M_SNARE, "SNAP", "The crack at the start."},
    {1, TD_M_SNARE, "BODY", "The drum's own tone under the snares."},
    {1, TD_M_HAT, "OPEN", "Closed to open: lets the hat ring."},
    {1, TD_M_HAT, "COLOR", "The metal's character, darker to brighter."},
    {1, TD_M_HAT, "BRIGHT", "How much top end."},
    {1, TD_M_HAT, "RESO", "A ringing resonance in the metal."},
    {1, TD_M_HAT, "SPREAD", "Spreads the metal's tones apart: wider, trashier."},
    {1, TD_M_CLAP, "SPREAD", "The time between the clap's hands."},
    {1, TD_M_CLAP, "BURSTS", "How many hands clap."},
    {1, TD_M_CLAP, "ROOM", "A short room around the clap."},
    {1, TD_M_CLAP, "RAND", "Random timing between the hands: looser."},
    {1, TD_M_TOM, "SWEEP", "How far the pitch falls at the start."},
    {1, TD_M_TOM, "SWTIME", "How quickly that pitch fall happens."},
    {1, TD_M_TOM, "FM", "A metallic FM tone in the drum."},
    {1, TD_M_SYNTH, "SHAPE", "The wave: sine, triangle, saw, square, and blends between them."},
    {1, TD_M_SYNTH, "DETUNE", "A second oscillator slightly out of tune: thicker and wider."},
    {1, TD_M_SYNTH, "INTVL", "The second oscillator's interval in semitones (7: a fifth, 12: an octave)."},
    {1, TD_M_SYNTH, "SUB", "A sine an octave below: weight."},
    {1, TD_M_SYNTH, "NOISE", "Noise mixed in: breath and air."},
    {1, TD_M_SYNTH, "GLIDE", "Slides from one note to the next."},
    {1, TD_M_FM, "RATIO", "The modulator's pitch against the note: 1 is warm, higher ratios are bells and metal."},
    {1, TD_M_FM, "INDEX", "How much modulation: brighter, then harsher."},
    {1, TD_M_FM, "FDBK", "The modulator feeds back on itself: grit, then noise."},
    {1, TD_M_FM, "ENVDEC", "How fast the modulation dies away: short makes a pluck or an electric piano's tine."},
    {1, TD_M_FM, "ENVAMT", "How much that envelope adds to the modulation."},
    {1, TD_M_FM, "FINE", "Fine-tunes the ratio: beating, bell-like detune."},
    {1, TD_M_SAMPLE, "SLOT", "Which sample plays (the sound packs fill the slots)."},
    {1, TD_M_SAMPLE, "START", "Where in the sample it starts."},
    {1, TD_M_SAMPLE, "END", "Where in the sample it stops."},
    {1, TD_M_SAMPLE, "SLICES", "Cuts the sample into 4, 8 or 16 slices: the note picks the slice."},
    {1, TD_M_SAMPLE, "REVERSE", "Plays the sample backwards."},
    {1, TD_M_SAMPLE, "LOOP", "Loops the sample while the note lasts."},
    {1, TD_M_SAMPLE, "GAIN", "The sample's level before the filter."},
    /* FLTR */
    {2, ANY, "CUTOFF", "The filter's cutoff. Low-pass: lower is darker. High-pass: higher is thinner."},
    {2, ANY, "RESO", "A peak at the cutoff, from smooth to squelchy. Heard only when the cutoff is inside the sound."},
    {2, ANY, "TYPE", "LP keeps the lows, BP the middle, HP the highs."},
    {2, ANY, "ENV", "How far each note opens (+) or closes (-) the filter: a pluck or a wah. At 0, ATTACK and DECAY do nothing."},
    {2, ANY, "ATTACK", "How fast the filter envelope opens (needs ENV)."},
    {2, ANY, "DECAY", "How fast the filter envelope closes again (needs ENV)."},
    {2, ANY, "KEYTRK", "Higher notes open the filter more, so they are not duller than low ones."},
    {2, ANY, "DRIVE", "Overdrives the filter: fatter, grittier."},
    /* AMP */
    {3, ANY, "ATTACK", "Each note's fade-in: 0 is a sharp start, higher for pads and swells."},
    {3, ANY, "HOLD", "Stays at full level before the decay starts."},
    {3, ANY, "DECAY", "How fast the level falls to SUSTAIN."},
    {3, ANY, "SUSTAIN", "The level held while the note lasts (0: the note dies away by itself)."},
    {3, ANY, "RELEASE", "The fade-out after the note ends."},
    {3, ANY, "PAN", "Left or right."},
    {3, ANY, "LEVEL", "The track's volume."},
    {3, ANY, "OVERDRIVE", "Distortion after the envelope: louder and dirtier."},
    /* FX */
    {4, ANY, "DELAY", "How much goes into the master delay (its time and echoes are on the MASTER page)."},
    {4, ANY, "REVERB", "How much goes into the master reverb (its size is on the MASTER page)."},
    {4, ANY, "CHORUS", "A chorus: wider and wobblier."},
    {4, ANY, "BITS", "Fewer bits: crunchy lo-fi."},
    {4, ANY, "SRR", "A lower sample rate: aliased, metallic lo-fi."},
    {4, ANY, "LOWCUT", "Cuts the lows: keeps hats, chords and stabs out of the kick's and bass's way."},
    {4, ANY, "DUCK", "Ducks the track under the kick (sidechain): house music's pumping."},
    {4, ANY, "WIDTH", "Stereo width."},
    /* LFO */
    {5, ANY, "SPEED", "How fast the LFO moves (MULT multiplies it, in time with the tempo)."},
    {5, ANY, "MULT", "Multiplies SPEED, in time with the tempo."},
    {5, ANY, "WAVE", "The LFO's shape: sine, triangle, saw, square or random."},
    {5, ANY, "DEST", "What the LFO moves: a SRC parameter, CUTOFF, PAN, DELAY... OFF: nothing."},
    {5, ANY, "DEPTH", "How far the LFO moves its destination, up (+) or down (-). At 0 it moves nothing."},
    {5, ANY, "FADE", "Fades the LFO in (+) or out (-) after each note."},
    {5, ANY, "MODE", "FREE runs on; TRIG restarts with each note; HOLD freezes at each note; ONE runs once per note."},
    {5, ANY, "PHASE", "Where in its wave the LFO starts."},
    /* MASTER (td_global_help) */
    {6, ANY, "DLY TIME", "The master delay's time, in steps of the tempo (3/16 is the classic dub echo)."},
    {6, ANY, "DLY FDBK", "How many echoes: higher repeats for longer."},
    {6, ANY, "DLY TONE", "Darker or brighter echoes."},
    {6, ANY, "REV SIZE", "The reverb's size, from a room to a hall."},
    {6, ANY, "REV DAMP", "Damps the reverb's highs: darker."},
    {6, ANY, "COMP", "A compressor on everything: glues the mix, louder and more even."},
    {6, ANY, "FILTER", "A DJ filter on everything: left is low-pass, right is high-pass, the middle is off."},
    {6, ANY, "VOLUME", "The master volume."},
};

static const char *help_for(int page, int machine, const char *name)
{
    const char *any = "";
    for (size_t i = 0; i < sizeof HELP / sizeof HELP[0]; i++) {
        const help_t *h = &HELP[i];
        if (h->page != page || strcmp(h->name, name) != 0) continue;
        if (h->machine == machine) return h->text;
        if (h->machine == ANY) any = h->text;
    }
    return any;
}
const char *td_param_help(int machine, int param)
{
    if (param < 0 || param >= TD_NPARAMS) return "";
    machine = clampi(machine, 0, TD_MACHINES - 1);
    return help_for(param / TD_SLOTS, machine, td_pdesc(machine, param)->name);
}
const char *td_global_help(int slot) { return help_for(6, ANY, td_gdesc(slot)->name); }
const char *td_perf_name(int perf)
{
    static const char *const N[TD_PERFS] = {"REPEAT", "STUTTER", "TAPESTOP", "DLYTHROW", "REVTHROW", "FILTER", "CRUSH",
                                            "DUBHOLD", "ECHO", "FLANGER", "PHASER", "TRANS", "AUTOPAN", "REVERSE",
                                            "HALF", "NOISE", "DISTORT", "SWEEP"};
    return perf >= 0 && perf < TD_PERFS ? N[perf] : "";
}
const char *td_scale_name(int scale) { return scale >= 0 && scale < 8 ? SCALE_NAMES[scale] : ""; }
