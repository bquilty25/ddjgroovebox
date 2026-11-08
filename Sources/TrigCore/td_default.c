/* SPDX-License-Identifier: GPL-3.0-only */
/* TRIGDECK's default project: a few patterns to start from, 124 BPM, A minor (tempo and swing are the project's).
 *
 * Classic grooves, written for TRIGDECK (the genres' common vocabulary, not any machine's factory content), four of
 * each, each pattern with its own tempo (td_default_project's SET):
 *   house   P01 DEEP 122 (opens with it), P02 TECH 126, P03 JACKIN 124, P04 AFRO 122
 *   techno  P05 TECHNO 130, P06 ACID 132, P07 DUB 122, P08 MELODIC 124
 *   garage  P09 2-STEP 132, P10 SPEED 132, P11 4X4 132, P12 BASSLINE 138
 *   d&b     P13 LIQUID 174, P14 NEURO 174, P15 JUNGLE 165, P16 ROLLER 172
 * The other kits (MINIMAL, FILTER, HARD, PROG, DISCO, BREAKS, ELECTRO) are sound packs only.
 *
 * Dub techno (P03), polymetric:
 *   deck A  1 KICK    deep, four on the floor, a 1:4 ghost; it ducks the sub and the stab (DUCK)
 *           2 HAT     closed off-beats with 50 % ghost sixteenths, an opening lock on the last one, delay and reverb
 *           3 CLAP    sparse: beat 4, a 37 % beat 2, a retrigged roll on FILL; drowned in reverb
 *           4 HAT     a shaker (noise only), 12 steps against the 16
 *   deck B  5 SYNTH   sub bass on the off-beats, sine-ish, ducked by the kick
 *           6 SYNTH   CHORD MIN7 stab, short and filtered, heavy tape delay, 10 steps; an LFO breathes its filter
 *           7 FM      a "ping" loop over 7 steps (bell ratio, short index envelope), delay and reverb
 *           8 SAMPLE  the built-in vinyl CRACKLE, looped, quiet
 *   scene A (crossfader left): filters closed, reverb sends up (a reverb throw)
 *   scene B (crossfader right): drive, open filters, resonance, more delay
 * Pattern 2 is a breakdown of pattern 1 (no kick, no clap); patterns 3..16 hold the same sounds and no steps. */
#include "td_internal.h"

static const uint8_t DEFAULT_MACHINES[TD_TRACKS] = {TD_M_KICK, TD_M_HAT, TD_M_CLAP, TD_M_HAT,
                                                    TD_M_SYNTH, TD_M_SYNTH, TD_M_FM, TD_M_SAMPLE};

void td_set_machine_x(td_pattern_t *p, int t, int machine)
{
    p->machine[t] = (uint8_t)machine;
    for (int q = TD_SLOTS; q < TD_PARAM(TD_PAGE_FX, 0); q++)   /* SRC, FLTR, AMP: the machine's own defaults */
        p->param[t][q] = (int16_t)td_pdef(machine, q);
    p->param[t][P_NOTE] = (int16_t)td_pdef(machine, P_NOTE);
}
void td_pattern_init_kit(td_pattern_t *p)
{
    memset(p, 0, sizeof *p);
    for (int t = 0; t < TD_TRACKS; t++) {
        p->machine[t] = DEFAULT_MACHINES[t];
        p->length[t] = 16;
        for (int q = 0; q < TD_NPARAMS; q++)
            p->param[t][q] = (int16_t)td_pdef(p->machine[t], q);
    }
}

static td_pattern_t *P;
static void prm(int t, int p, int v) { P->param[t][p] = (int16_t)v; }
static td_step_t *trig(int t, int s, int note, int vel)
{
    td_step_t *st = &P->step[t][s];
    st->trig = 1;
    st->note = (uint8_t)note;
    st->vel = (uint8_t)vel;
    st->rrate = (uint8_t)P->param[t][P_RRATE];
    return st;
}
static void lock(int t, int s, int p, int v)
{
    td_step_t *st = &P->step[t][s];
    if (st->nlocks < TD_LPS) {
        P->lock[t][s][st->nlocks].param = (uint8_t)p;
        P->lock[t][s][st->nlocks].value = (int16_t)v;
        st->nlocks++;
    }
}
static void scene(int sc, int t, int p, int v)
{
    if (P->nscene[sc] < TD_SCENE_MAX) {
        td_scene_ent_t *e = &P->scene[sc][P->nscene[sc]++];
        e->track = (uint8_t)t;
        e->param = (uint8_t)p;
        e->value = (int16_t)v;
    }
}
#define SRC(i) (P_SRC + (i))

static void pat_dub(void)
{
    /* 1 KICK */
    prm(0, SRC(KK_TUNE), -3); prm(0, SRC(KK_DECAY), 64); prm(0, SRC(KK_SWEEP), 26); prm(0, SRC(KK_STIME), 34);
    prm(0, SRC(KK_CLICK), 34); prm(0, SRC(KK_DRIVE), 24); prm(0, SRC(KK_TONE), 84); prm(0, SRC(KK_HOLD), 16);
    prm(0, P_LEVEL, 104); prm(0, P_REV, 10);
    for (int s = 0; s < 16; s += 4) trig(0, s, 60, 112);
    trig(0, 11, 60, 72)->cond = TD_COND_AB + 5;                /* 1:4: a ghost kick every fourth bar */
    trig(0, 15, 60, 64)->cond = TD_COND_FILL;

    /* 2 HAT: closed off-beats */
    prm(1, SRC(HH_TUNE), 2); prm(1, SRC(HH_DECAY), 30); prm(1, SRC(HH_COLOR), 86); prm(1, SRC(HH_BRIGHT), 84);
    prm(1, SRC(HH_RESO), 30); prm(1, SRC(HH_SPREAD), 12);
    prm(1, P_LEVEL, 112); prm(1, P_PAN, 16); prm(1, P_DLY, 34); prm(1, P_REV, 30); prm(1, P_LOWCUT, 70);
    prm(1, P_LSPD, 16); prm(1, P_LMUL, 2); prm(1, P_LWAVE, 0); prm(1, P_LDEST, 5); prm(1, P_LDEPTH, 12);   /* BRIGHT */
    for (int s = 2; s < 16; s += 4) trig(1, s, 60, 100);
    for (int s = 3; s < 16; s += 4) trig(1, s, 60, 38)->cond = TD_COND_P1 + 3;   /* 50 % ghosts */
    lock(1, 14, SRC(HH_OPEN), 64);                             /* the last off-beat opens */
    lock(1, 14, P_DLY, 70);

    /* 3 CLAP: sparse, drowned in reverb */
    prm(2, SRC(CP_DECAY), 40); prm(2, SRC(CP_SPREAD), 60); prm(2, SRC(CP_BURSTS), 4); prm(2, SRC(CP_TONE), 70);
    prm(2, SRC(CP_ROOM), 50);
    prm(2, P_LEVEL, 96); prm(2, P_PAN, -12); prm(2, P_WIDTH, 50); prm(2, P_REV, 92); prm(2, P_DLY, 40); prm(2, P_LOWCUT, 60);
    trig(2, 12, 60, 96);
    trig(2, 4, 60, 70)->cond = TD_COND_P1 + 2;                 /* 37 % */
    {
        td_step_t *st = trig(2, 14, 60, 80);
        st->cond = TD_COND_FILL;
        st->retrig = 3;
        st->rrate = 2;
    }

    /* 4 HAT as a shaker: 12 steps */
    P->length[3] = 12;
    prm(3, SRC(HH_TUNE), 6); prm(3, SRC(HH_DECAY), 22); prm(3, SRC(HH_COLOR), 0); prm(3, SRC(HH_BRIGHT), 100);
    prm(3, SRC(HH_RESO), 12);
    prm(3, P_LEVEL, 80); prm(3, P_PAN, -28); prm(3, P_WIDTH, 70); prm(3, P_REV, 26); prm(3, P_LOWCUT, 80);
    {
        static const uint8_t v[12] = {84, 34, 58, 34, 80, 34, 60, 30, 78, 36, 56, 40};
        for (int s = 0; s < 12; s++) {
            td_step_t *st = trig(3, s, 60, v[s]);
            if (v[s] < 40) st->cond = TD_COND_P1 + 5;          /* 75 % */
        }
    }

    /* 5 SYNTH: the sub bass, ducked by the kick */
    prm(4, SRC(SY_SHAPE), 30); prm(4, SRC(SY_DETUNE), 0); prm(4, SRC(SY_SUB), 0); prm(4, SRC(SY_GLIDE), 18);
    prm(4, P_CUT, 58); prm(4, P_RES, 8); prm(4, P_FENV, 10); prm(4, P_FDEC, 40); prm(4, P_KEY, 64);
    prm(4, P_AATK, 3); prm(4, P_ADEC, 70); prm(4, P_ASUS, 100); prm(4, P_AREL, 28);
    prm(4, P_LEVEL, 80); prm(4, P_ODRV, 22); prm(4, P_DUCK, 80); prm(4, P_LEN, 5);
    for (int s = 2; s < 16; s += 4) trig(4, s, 33, 100);       /* A0 (55 Hz) on the off-beats */
    trig(4, 13, 36, 84)->cond = TD_COND_AB + 0;                /* 1:2: a C1 pickup */
    lock(4, 14, P_CUT, 66);

    /* 6 SYNTH: the chord stab (MIN7), 10 steps, into the tape delay */
    P->length[5] = 10;
    prm(5, SRC(SY_SHAPE), 85); prm(5, SRC(SY_DETUNE), 22); prm(5, SRC(SY_CHORD), 1);
    prm(5, P_CUT, 60); prm(5, P_RES, 42); prm(5, P_FENV, 30); prm(5, P_FDEC, 30); prm(5, P_KEY, 32);
    prm(5, P_AATK, 0); prm(5, P_ADEC, 40); prm(5, P_ASUS, 0); prm(5, P_AREL, 20);
    prm(5, P_LEVEL, 120); prm(5, P_DLY, 96); prm(5, P_REV, 60); prm(5, P_CHO, 46); prm(5, P_WIDTH, 80);
    prm(5, P_DUCK, 30); prm(5, P_LOWCUT, 50); prm(5, P_LEN, 2);
    prm(5, P_LSPD, 16); prm(5, P_LMUL, 2); prm(5, P_LWAVE, 1); prm(5, P_LDEST, 9); prm(5, P_LDEPTH, 14);   /* CUTOFF */
    trig(5, 0, 57, 100);                                       /* A2: A minor 7 */
    trig(5, 6, 57, 86);
    lock(5, 6, P_CUT, 62);
    trig(5, 3, 57, 64)->cond = TD_COND_P1 + 2;

    /* 7 FM: a ping loop over 7 steps */
    P->length[6] = 7;
    prm(6, SRC(FM_RATIO), 6); prm(6, SRC(FM_INDEX), 26); prm(6, SRC(FM_FDBK), 12); prm(6, SRC(FM_ENVDEC), 26);
    prm(6, SRC(FM_ENVAMT), 66);
    prm(6, P_CUT, 112); prm(6, P_ADEC, 44); prm(6, P_ASUS, 0); prm(6, P_AREL, 30);
    prm(6, P_LEVEL, 97); prm(6, P_PAN, 36); prm(6, P_DLY, 84); prm(6, P_REV, 52); prm(6, P_LOWCUT, 64);
    trig(6, 0, 76, 88);                                        /* E5 */
    trig(6, 3, 69, 72);                                        /* A4 */
    trig(6, 5, 72, 66)->cond = TD_COND_P1 + 5;                 /* C5, 75 % */
    lock(6, 3, SRC(FM_RATIO), 3);

    /* 8 SAMPLE: vinyl crackle */
    prm(7, SRC(SM_SLOT), 4); prm(7, SRC(SM_LOOP), 1);
    prm(7, P_AREL, 22); prm(7, P_LEN, 16);
    prm(7, P_LEVEL, 80); prm(7, P_WIDTH, 70); prm(7, P_LOWCUT, 50);
    prm(7, P_CUT, 104);
    trig(7, 0, 60, 100);

    /* scene A: closed and washed out */
    scene(0, 4, P_CUT, 34); scene(0, 5, P_CUT, 22); scene(0, 5, P_REV, 127); scene(0, 6, P_CUT, 70);
    scene(0, 6, P_REV, 118); scene(0, 1, P_REV, 100); scene(0, 2, P_REV, 127); scene(0, 0, P_LOWCUT, 50);
    scene(0, 3, P_LEVEL, 30);
    /* scene B: driven and open */
    scene(1, 5, P_CUT, 92); scene(1, 5, P_RES, 72); scene(1, 5, P_ODRV, 64); scene(1, 4, P_ODRV, 54);
    scene(1, 0, SRC(KK_DRIVE), 76); scene(1, 6, SRC(FM_INDEX), 92); scene(1, 1, SRC(HH_BRIGHT), 122);
    scene(1, 5, P_DLY, 127);

}

/* ---- the beat house and techno share: a 909-style kick, an open hat on the off-beats, a clap on 2 and 4, closed
 * sixteenths with ghosts (tracks 1-4) */
static void four_on_the_floor(void)
{
    /* 1 KICK: short, punchy, a click on top */
    prm(0, SRC(KK_TUNE), 0); prm(0, SRC(KK_DECAY), 50); prm(0, SRC(KK_SWEEP), 36); prm(0, SRC(KK_STIME), 22);
    prm(0, SRC(KK_CLICK), 56); prm(0, SRC(KK_DRIVE), 34); prm(0, SRC(KK_TONE), 92); prm(0, SRC(KK_HOLD), 8);
    prm(0, P_LEVEL, 110); prm(0, P_REV, 4);
    for (int s = 0; s < 16; s += 4) trig(0, s, 60, 118);
    trig(0, 14, 60, 92)->cond = TD_COND_FILL;                 /* the fill: kicks into the next bar */
    trig(0, 15, 60, 84)->cond = TD_COND_FILL;

    /* 2 HAT: the open hat on the off-beats */
    prm(1, SRC(HH_TUNE), 4); prm(1, SRC(HH_DECAY), 54); prm(1, SRC(HH_OPEN), 82); prm(1, SRC(HH_COLOR), 72);
    prm(1, SRC(HH_BRIGHT), 92); prm(1, SRC(HH_RESO), 20); prm(1, SRC(HH_SPREAD), 18);
    prm(1, P_LEVEL, 98); prm(1, P_PAN, 10); prm(1, P_DLY, 12); prm(1, P_REV, 16); prm(1, P_LOWCUT, 78);
    for (int s = 2; s < 16; s += 4) trig(1, s, 60, 100);
    lock(1, 14, SRC(HH_OPEN), 112);                            /* the last one rings longer */

    /* 3 CLAP on 2 and 4, a 1:4 ghost, a roll on FILL */
    prm(2, SRC(CP_DECAY), 46); prm(2, SRC(CP_SPREAD), 52); prm(2, SRC(CP_BURSTS), 3); prm(2, SRC(CP_TONE), 78);
    prm(2, SRC(CP_ROOM), 42);
    prm(2, P_LEVEL, 100); prm(2, P_PAN, -6); prm(2, P_WIDTH, 40); prm(2, P_REV, 46); prm(2, P_LOWCUT, 62);
    trig(2, 4, 60, 108);
    trig(2, 12, 60, 108);
    trig(2, 7, 60, 58)->cond = TD_COND_AB + 5;                 /* 1:4 */
    {
        td_step_t *st = trig(2, 15, 60, 84);
        st->cond = TD_COND_FILL;
        st->retrig = 3;
        st->rrate = 2;
    }

    /* 4 HAT: closed sixteenths, accented off-beats, 75 % ghosts */
    prm(3, SRC(HH_TUNE), 0); prm(3, SRC(HH_DECAY), 14); prm(3, SRC(HH_OPEN), 0); prm(3, SRC(HH_COLOR), 60);
    prm(3, SRC(HH_BRIGHT), 98); prm(3, SRC(HH_RESO), 10); prm(3, SRC(HH_SPREAD), 6);
    prm(3, P_LEVEL, 88); prm(3, P_PAN, -18); prm(3, P_REV, 10); prm(3, P_LOWCUT, 86);
    {
        static const uint8_t v[16] = {70, 40, 96, 44, 72, 40, 98, 46, 70, 42, 96, 44, 72, 40, 100, 52};
        for (int s = 0; s < 16; s++) {
            td_step_t *st = trig(3, s, 60, v[s]);
            if (v[s] < 50) st->cond = TD_COND_P1 + 5;          /* 75 % */
            if (s % 2) st->micro = 3;                           /* the off-sixteenths a little late: a shuffle */
        }
    }
}

/* a 4-bar progression for the melodic tracks: roots (bass octave), chord roots, chord types (td_chord_iv) */
typedef struct { int bass[4], chord[4], type[4]; } prog_t;
static const prog_t PROG_DEEP = {{33, 38, 29, 28}, {57, 62, 53, 52}, {3, 3, 2, 1}};   /* Am9 Dm9 Fmaj7 Em7 */
static const prog_t PROG_DISCO = {{33, 38, 31, 36}, {57, 62, 55, 60}, {1, 1, 2, 2}};  /* Am7 Dm7 Gmaj7 Cmaj7 */

/* P01: deep house, A minor, 4 bars. The four-on-the-floor beat; a syncopated bass on the progression's roots (Am9,
 * Dm9, Fmaj7, Em7) with octave ghosts and a slide into each bar; chord stabs on the "and"s into the delay; an FM
 * pluck answering them; a vocal blip every fourth bar. */
static void pat_house(void)
{
    four_on_the_floor();
    const prog_t *pg = &PROG_DEEP;

    /* 5 SYNTH: the bass, 4 bars */
    prm(4, SRC(SY_SHAPE), 92); prm(4, SRC(SY_DETUNE), 6); prm(4, SRC(SY_SUB), 60); prm(4, SRC(SY_GLIDE), 20);
    prm(4, P_CUT, 52); prm(4, P_RES, 30); prm(4, P_FENV, 30); prm(4, P_FDEC, 30); prm(4, P_KEY, 64);
    prm(4, P_AATK, 0); prm(4, P_ADEC, 44); prm(4, P_ASUS, 40); prm(4, P_AREL, 14);
    prm(4, P_LEVEL, 92); prm(4, P_ODRV, 30); prm(4, P_DUCK, 70); prm(4, P_LEN, 3);
    P->length[4] = P->length[5] = P->length[6] = 64;
    for (int b = 0; b < 4; b++) {
        int r = pg->bass[b], s = b * 16;
        trig(4, s + 3, r, 104);
        trig(4, s + 6, r + 12, 66);                            /* octave ghost */
        trig(4, s + 10, r, 110)->accent = 1;
        trig(4, s + 11, r + 12, 62)->cond = TD_COND_P1 + 4;    /* 62 % */
        trig(4, s + 14, r + 7, 92);                            /* the fifth */
        td_step_t *st = trig(4, s + 15, r + 5, 76);            /* a step towards the next root, sliding */
        st->slide = 1;
        lock(4, s + 10, P_CUT, 72);
    }

    /* 6 SYNTH: the chords, stabbed on the "and"s, into the delay (each stab locks its chord type) */
    prm(5, SRC(SY_SHAPE), 86); prm(5, SRC(SY_DETUNE), 18); prm(5, SRC(SY_CHORD), 3);   /* MIN9 */
    prm(5, P_CUT, 74); prm(5, P_RES, 30); prm(5, P_FENV, 24); prm(5, P_FDEC, 26); prm(5, P_KEY, 32);
    prm(5, P_AATK, 0); prm(5, P_ADEC, 34); prm(5, P_ASUS, 0); prm(5, P_AREL, 18);
    prm(5, P_LEVEL, 104); prm(5, P_DLY, 70); prm(5, P_REV, 44); prm(5, P_CHO, 40); prm(5, P_WIDTH, 80);
    prm(5, P_DUCK, 40); prm(5, P_LOWCUT, 56); prm(5, P_LEN, 2);
    prm(5, P_LSPD, 12); prm(5, P_LMUL, 2); prm(5, P_LWAVE, 0); prm(5, P_LDEST, 9); prm(5, P_LDEPTH, 12);   /* CUTOFF */
    for (int b = 0; b < 4; b++) {
        int s = b * 16, c = pg->chord[b];
        trig(5, s + 2, c, 100); lock(5, s + 2, SRC(SY_CHORD), pg->type[b]);
        trig(5, s + 7, c, 84); lock(5, s + 7, SRC(SY_CHORD), pg->type[b]);
        trig(5, s + 10, c, 92); lock(5, s + 10, SRC(SY_CHORD), pg->type[b]);
        if (b == 3) { trig(5, s + 14, c + 3, 80)->cond = TD_COND_AB + 0; lock(5, s + 14, SRC(SY_CHORD), 2); }   /* 1:2: Gmaj7, back to Am */
    }

    /* 7 FM: a pluck melody over the chords, answering the stabs */
    prm(6, SRC(FM_RATIO), 4); prm(6, SRC(FM_INDEX), 40); prm(6, SRC(FM_FDBK), 8); prm(6, SRC(FM_ENVDEC), 20);
    prm(6, SRC(FM_ENVAMT), 70);
    prm(6, P_CUT, 104); prm(6, P_ADEC, 30); prm(6, P_ASUS, 0); prm(6, P_AREL, 20);
    prm(6, P_LEVEL, 84); prm(6, P_PAN, 30); prm(6, P_DLY, 60); prm(6, P_REV, 40); prm(6, P_LOWCUT, 70);
    {
        /* E5 C5 | F5 D5 | E5 C5 A4 | B4 G4 */
        static const int mel[][3] = {{1, 76, 82}, {9, 72, 70}, {17, 77, 80}, {25, 74, 70}, {33, 76, 82}, {41, 72, 70},
                                     {45, 69, 60}, {49, 71, 80}, {57, 67, 72}, {61, 69, 58}};
        for (int i = 0; i < (int)(sizeof mel / sizeof mel[0]); i++) trig(6, mel[i][0], mel[i][1], mel[i][2]);
        P->step[6][45].cond = TD_COND_P1 + 3;                  /* 50 % */
        P->step[6][61].cond = TD_COND_P1 + 3;
    }

    /* 8 SAMPLE: the built-in vocal blip, every fourth bar */
    prm(7, SRC(SM_SLOT), 1);
    prm(7, P_LEVEL, 92); prm(7, P_DLY, 70); prm(7, P_REV, 50); prm(7, P_LOWCUT, 60); prm(7, P_PAN, -20);
    trig(7, 7, 60, 100)->cond = TD_COND_AB + 7;                /* 3:4 */

    /* scene A: a filtered breakdown; scene B: driven and open */
    scene(0, 4, P_CUT, 26); scene(0, 5, P_CUT, 30); scene(0, 5, P_REV, 110); scene(0, 2, P_REV, 100);
    scene(0, 3, P_LEVEL, 40); scene(0, 0, P_LOWCUT, 60); scene(0, 6, P_REV, 100);
    scene(1, 5, P_CUT, 104); scene(1, 5, P_RES, 70); scene(1, 4, P_ODRV, 70); scene(1, 4, P_CUT, 76);
    scene(1, 0, SRC(KK_DRIVE), 80); scene(1, 1, SRC(HH_BRIGHT), 124); scene(1, 5, P_DLY, 110);
}

/* P04: acid. The four-on-the-floor beat under a 303-style line: square-ish, resonant, accents and slides */
static void pat_acid(void)
{
    four_on_the_floor();

    prm(4, SRC(SY_SHAPE), 104); prm(4, SRC(SY_DETUNE), 0); prm(4, SRC(SY_SUB), 0); prm(4, SRC(SY_GLIDE), 26);
    prm(4, P_CUT, 36); prm(4, P_RES, 104); prm(4, P_FENV, 44); prm(4, P_FDEC, 38); prm(4, P_KEY, 40); prm(4, P_FDRV, 40);
    prm(4, P_AATK, 0); prm(4, P_ADEC, 50); prm(4, P_ASUS, 40); prm(4, P_AREL, 10);
    prm(4, P_LEVEL, 98); prm(4, P_ODRV, 46); prm(4, P_DLY, 30); prm(4, P_DUCK, 40); prm(4, P_LEN, 3);
    prm(4, P_LSPD, 6); prm(4, P_LMUL, 2); prm(4, P_LWAVE, 0); prm(4, P_LDEST, 9); prm(4, P_LDEPTH, 18);   /* CUTOFF */
    {
        /* note, accent, slide; 0 = rest */
        static const uint8_t line[16][3] = {
            {33, 0, 0}, {45, 1, 0}, {0, 0, 0}, {33, 0, 0}, {36, 0, 1}, {38, 0, 0}, {33, 0, 0}, {0, 0, 0},
            {45, 1, 0}, {31, 0, 0}, {33, 0, 1}, {36, 0, 0}, {40, 1, 0}, {0, 0, 0}, {33, 0, 0}, {43, 0, 1},
        };
        /* the second bar answers: up to the C and the E, an octave jump, back down by slides */
        static const uint8_t answer[16][3] = {
            {33, 0, 0}, {33, 0, 1}, {36, 1, 0}, {0, 0, 0}, {40, 0, 1}, {45, 1, 0}, {43, 0, 0}, {40, 0, 1},
            {0, 0, 0}, {48, 1, 0}, {45, 0, 1}, {43, 0, 0}, {40, 1, 0}, {38, 0, 1}, {36, 0, 0}, {31, 0, 1},
        };
        P->length[4] = 32;
        for (int s = 0; s < 32; s++) {
            const uint8_t *n = s < 16 ? line[s] : answer[s - 16];
            if (!n[0]) continue;
            td_step_t *st = trig(4, s, n[0], n[1] ? 120 : 92);
            st->accent = n[1];
            st->slide = n[2];
            lock(4, s, P_CUT, 30 + s);                         /* the filter opens over the two bars */
        }
    }
    /* 8 SAMPLE: the riser, every eighth bar */
    prm(7, SRC(SM_SLOT), 3);
    prm(7, P_LEVEL, 80); prm(7, P_REV, 40);
    trig(7, 0, 60, 100)->cond = TD_COND_AB + 9;                /* 1:8 */

    scene(0, 4, P_CUT, 18); scene(0, 4, P_RES, 80); scene(0, 2, P_REV, 100);
    scene(1, 4, P_CUT, 90); scene(1, 4, P_RES, 122); scene(1, 4, P_ODRV, 100); scene(1, 4, P_DLY, 90);
    scene(1, 0, SRC(KK_DRIVE), 80);
}

/* P02: techno. A harder kick, the open hat as a ride, sixteenth hats; a rolling sub on the off-sixteenths (the
 * "rumble"), a minor stab on a 24-step loop against the bar, a 12-step tom pattern, a riser every 8 bars. */
static void pat_techno(void)
{
    four_on_the_floor();
    prm(0, SRC(KK_DECAY), 64); prm(0, SRC(KK_CLICK), 66); prm(0, SRC(KK_DRIVE), 52); prm(0, SRC(KK_TUNE), -2);
    prm(0, P_REV, 14);                                         /* (a little room: the rumble's tail) */
    prm(1, SRC(HH_DECAY), 64); prm(1, SRC(HH_COLOR), 96); prm(1, SRC(HH_RESO), 40);   /* a ride */
    for (int s = 2; s < 16; s += 4) P->step[1][s].micro = 0;
    trig(1, 15, 60, 70)->cond = TD_COND_P1 + 3;
    /* 3 CLAP: only on 2 and 4, drier; a snare roll as the fill */
    prm(2, SRC(CP_ROOM), 20); prm(2, P_REV, 30);
    P->step[2][7].trig = 0;

    /* 5 SYNTH: the rumble: a short sine on the three off-sixteenths of each beat, ducked under the kick */
    prm(4, SRC(SY_SHAPE), 10); prm(4, SRC(SY_SUB), 30);
    prm(4, P_CUT, 46); prm(4, P_RES, 20); prm(4, P_ADEC, 22); prm(4, P_ASUS, 0); prm(4, P_AREL, 8);
    prm(4, P_LEVEL, 100); prm(4, P_DUCK, 90); prm(4, P_REV, 30); prm(4, P_ODRV, 20);
    for (int s = 0; s < 16; s++)
        if (s % 4) trig(4, s, 33, s % 4 == 2 ? 96 : 70);       /* A1 */
    /* 6 SYNTH: a minor stab, 24 steps long: it lands somewhere else in every bar */
    prm(5, SRC(SY_SHAPE), 92); prm(5, SRC(SY_DETUNE), 14); prm(5, SRC(SY_CHORD), 1);
    prm(5, P_CUT, 60); prm(5, P_RES, 50); prm(5, P_FENV, 30); prm(5, P_ADEC, 24); prm(5, P_ASUS, 0); prm(5, P_AREL, 20);
    prm(5, P_LEVEL, 96); prm(5, P_DLY, 80); prm(5, P_REV, 60); prm(5, P_DUCK, 50);
    P->length[5] = 24;
    trig(5, 0, 57, 100); trig(5, 10, 57, 84); trig(5, 17, 60, 80)->cond = TD_COND_P1 + 4;
    /* 7 TOM: 12 steps, three hits, tuned */
    td_set_machine_x(P, 6, TD_M_TOM);
    prm(6, SRC(TM_TUNE), -2); prm(6, SRC(TM_DECAY), 40); prm(6, SRC(TM_SWEEP), 8); prm(6, SRC(TM_NOISE), 20);
    prm(6, P_LEVEL, 84); prm(6, P_PAN, -24); prm(6, P_DLY, 30);
    P->length[6] = 12;
    trig(6, 0, 57, 90); trig(6, 3, 60, 70); trig(6, 7, 55, 80);
    /* 8 SAMPLE: the riser, every 8 bars */
    prm(7, SRC(SM_SLOT), 3); prm(7, P_LEVEL, 80); prm(7, P_REV, 50);
    trig(7, 0, 60, 100)->cond = TD_COND_AB + 9;                /* 1:8 */

    scene(0, 0, P_LOWCUT, 70); scene(0, 4, P_LEVEL, 30); scene(0, 5, P_REV, 120); scene(0, 3, P_LEVEL, 50);
    scene(1, 0, SRC(KK_DRIVE), 100); scene(1, 5, P_CUT, 100); scene(1, 5, P_RES, 90); scene(1, 1, SRC(HH_DECAY), 100);
}

/* P06: UK garage, 2-step. The kick skips (1 and the "and" of 3), the snare on 2 and 4 with ghosts, heavily shuffled
 * hats, a sliding sub, organ-like minor 7 stabs on the off-beats, 2 bars. */
static void pat_garage(void)
{
    td_set_machine_x(P, 2, TD_M_SNARE);
    prm(0, SRC(KK_DECAY), 70); prm(0, SRC(KK_SWEEP), 26); prm(0, SRC(KK_CLICK), 40); prm(0, SRC(KK_TONE), 80);
    prm(0, P_LEVEL, 112);
    trig(0, 0, 60, 120); trig(0, 10, 60, 112);
    trig(0, 13, 60, 80)->cond = TD_COND_AB + 1;                /* 2:2 */
    /* 2 HAT open: off-beats */
    prm(1, SRC(HH_DECAY), 40); prm(1, SRC(HH_OPEN), 70); prm(1, SRC(HH_BRIGHT), 96); prm(1, P_LEVEL, 82); prm(1, P_LOWCUT, 84);
    trig(1, 2, 60, 90); trig(1, 10, 60, 84);
    /* 3 SNARE: 2 and 4, ghosts */
    prm(2, SRC(SN_TUNE), 10); prm(2, SRC(SN_DECAY), 30); prm(2, SRC(SN_NOISE), 84); prm(2, SRC(SN_SNAP), 96);
    prm(2, P_LEVEL, 104); prm(2, P_REV, 40);
    trig(2, 4, 60, 112); trig(2, 12, 60, 112);
    trig(2, 7, 60, 50); trig(2, 15, 60, 46)->cond = TD_COND_P1 + 4;
    /* 4 HAT closed: sixteenths, every second one late: the 2-step shuffle */
    prm(3, SRC(HH_DECAY), 10); prm(3, SRC(HH_BRIGHT), 104); prm(3, P_LEVEL, 80); prm(3, P_LOWCUT, 90); prm(3, P_PAN, -16);
    for (int s = 0; s < 16; s++) {
        td_step_t *st = trig(3, s, 60, s % 2 ? 56 : (s % 4 ? 80 : 96));
        if (s % 2) st->micro = 8;
    }
    /* 5 SYNTH: the sub, sliding, 2 bars (A then D) */
    prm(4, SRC(SY_SHAPE), 6); prm(4, SRC(SY_GLIDE), 46); prm(4, P_CUT, 70); prm(4, P_ADEC, 60); prm(4, P_ASUS, 60);
    prm(4, P_AREL, 20); prm(4, P_LEVEL, 108); prm(4, P_DUCK, 50);
    P->length[4] = 32;
    static const int sub[][3] = {{0, 33, 0}, {3, 33, 1}, {6, 45, 0}, {11, 40, 1}, {14, 36, 0},
                                 {16, 38, 0}, {19, 38, 1}, {22, 50, 0}, {27, 41, 1}, {30, 40, 1}};
    for (int i = 0; i < 10; i++) trig(4, sub[i][0], sub[i][1], 100)->slide = (uint8_t)sub[i][2];
    /* 6 SYNTH: organ-like minor 7 stabs (square, detuned, short) on the off-beats, 2 bars */
    prm(5, SRC(SY_SHAPE), 120); prm(5, SRC(SY_DETUNE), 24); prm(5, SRC(SY_CHORD), 1);
    prm(5, P_CUT, 84); prm(5, P_ADEC, 26); prm(5, P_ASUS, 0); prm(5, P_AREL, 12); prm(5, P_LEVEL, 92);
    prm(5, P_CHO, 60); prm(5, P_REV, 30);
    P->length[5] = 32;
    trig(5, 3, 57, 96); trig(5, 6, 57, 80); trig(5, 11, 57, 90);
    trig(5, 19, 62, 96); trig(5, 22, 62, 80); trig(5, 27, 62, 90); trig(5, 30, 60, 70)->cond = TD_COND_AB + 0;
    /* 7 FM: a chopped vocal-like pluck */
    prm(6, SRC(FM_RATIO), 3); prm(6, SRC(FM_INDEX), 50); prm(6, SRC(FM_ENVDEC), 14); prm(6, SRC(FM_ENVAMT), 90);
    prm(6, P_ADEC, 22); prm(6, P_ASUS, 0); prm(6, P_LEVEL, 80); prm(6, P_DLY, 50); prm(6, P_PAN, 26);
    trig(6, 5, 69, 80); trig(6, 13, 72, 70)->cond = TD_COND_P1 + 4;
    /* 8 SAMPLE: the vocal, every other bar */
    prm(7, SRC(SM_SLOT), 1); prm(7, P_LEVEL, 88); prm(7, P_DLY, 60); prm(7, P_REV, 40);
    trig(7, 9, 60, 100)->cond = TD_COND_AB + 1;                /* 2:2 */

    scene(0, 4, P_CUT, 30); scene(0, 5, P_REV, 110); scene(0, 0, P_LEVEL, 60);
    scene(1, 5, P_CUT, 110); scene(1, 4, P_ODRV, 60); scene(1, 3, P_LEVEL, 100);
}

/* P07: disco. Four on the floor, claps, open hats on the off-beats; the octave bass on eighths over Am7, Dm7, Gmaj7,
 * Cmaj7; chord stabs on the off-beats; an FM brass hit; 4 bars. */
static void pat_disco(void)
{
    four_on_the_floor();
    const prog_t *pg = &PROG_DISCO;
    prm(0, SRC(KK_DECAY), 50); prm(0, SRC(KK_CLICK), 30); prm(0, SRC(KK_DRIVE), 10); prm(0, SRC(KK_TONE), 70);
    /* 5 SYNTH: octave bass, eighths */
    prm(4, SRC(SY_SHAPE), 70); prm(4, SRC(SY_SUB), 30); prm(4, P_CUT, 64); prm(4, P_RES, 20); prm(4, P_FENV, 20);
    prm(4, P_ADEC, 30); prm(4, P_ASUS, 20); prm(4, P_AREL, 10); prm(4, P_LEVEL, 96); prm(4, P_DUCK, 30);
    P->length[4] = P->length[5] = 64;
    for (int b = 0; b < 4; b++)
        for (int e = 0; e < 8; e++) trig(4, b * 16 + e * 2, pg->bass[b] + (e % 2 ? 12 : 0), e % 2 ? 80 : 104);
    /* 6 SYNTH: off-beat stabs, the chord of the bar */
    prm(5, SRC(SY_SHAPE), 88); prm(5, SRC(SY_DETUNE), 20); prm(5, P_CUT, 86); prm(5, P_ADEC, 20); prm(5, P_ASUS, 0);
    prm(5, P_AREL, 16); prm(5, P_LEVEL, 92); prm(5, P_CHO, 50); prm(5, P_REV, 40);
    for (int b = 0; b < 4; b++)
        for (int s = 2; s < 16; s += 4) { trig(5, b * 16 + s, pg->chord[b], s == 2 ? 96 : 82); lock(5, b * 16 + s, SRC(SY_CHORD), pg->type[b]); }
    /* 7 FM: a brass hit on the last off-beat of every second bar */
    prm(6, SRC(FM_RATIO), 1); prm(6, SRC(FM_INDEX), 70); prm(6, SRC(FM_FDBK), 30); prm(6, SRC(FM_ENVDEC), 40);
    prm(6, SRC(FM_ENVAMT), 60); prm(6, SRC(FM_CHORD), 5); prm(6, P_AATK, 6); prm(6, P_ADEC, 50); prm(6, P_ASUS, 30);
    prm(6, P_LEVEL, 86); prm(6, P_REV, 50);
    trig(6, 14, 69, 96)->cond = TD_COND_AB + 1;                /* 2:2 */
    /* 8 SAMPLE: the vocal */
    prm(7, SRC(SM_SLOT), 1); prm(7, P_LEVEL, 88); prm(7, P_DLY, 70);
    trig(7, 7, 60, 100)->cond = TD_COND_AB + 7;                /* 3:4 */

    scene(0, 4, P_CUT, 30); scene(0, 5, P_CUT, 40); scene(0, 2, P_REV, 100);
    scene(1, 5, P_CUT, 110); scene(1, 4, P_CUT, 90); scene(1, 6, SRC(FM_INDEX), 110);
}

/* P08: breaks. A syncopated kick, the snare on 2 and 4 with ghost notes, swung eighth hats, an open hat on the last
 * "and", a bass answering the kick, a pad. */
static void pat_breaks(void)
{
    td_set_machine_x(P, 2, TD_M_SNARE);
    prm(0, SRC(KK_DECAY), 56); prm(0, SRC(KK_CLICK), 50); prm(0, SRC(KK_DRIVE), 30); prm(0, P_LEVEL, 110);
    trig(0, 0, 60, 120); trig(0, 3, 60, 90); trig(0, 10, 60, 110);
    trig(0, 11, 60, 84)->cond = TD_COND_AB + 1;                /* 2:2 */
    trig(0, 6, 60, 76)->cond = TD_COND_P1 + 3;
    /* 2 HAT open: the last "and" */
    prm(1, SRC(HH_DECAY), 50); prm(1, SRC(HH_OPEN), 80); prm(1, P_LEVEL, 84); prm(1, P_LOWCUT, 80);
    trig(1, 14, 60, 90);
    /* 3 SNARE: 2 and 4, ghosts around them */
    prm(2, SRC(SN_TUNE), 6); prm(2, SRC(SN_DECAY), 40); prm(2, SRC(SN_NOISE), 76); prm(2, SRC(SN_SNAP), 80);
    prm(2, SRC(SN_BODY), 70); prm(2, P_LEVEL, 106); prm(2, P_REV, 30);
    trig(2, 4, 60, 116); trig(2, 12, 60, 116);
    trig(2, 7, 60, 44); trig(2, 9, 60, 52); trig(2, 15, 60, 48)->cond = TD_COND_P1 + 4;
    /* 4 HAT closed: eighths, swung */
    prm(3, SRC(HH_DECAY), 14); prm(3, P_LEVEL, 84); prm(3, P_LOWCUT, 86);
    for (int s = 0; s < 16; s += 2) { td_step_t *st = trig(3, s, 60, s % 4 ? 70 : 96); if (s % 4) st->micro = 6; }
    /* 5 SYNTH: the bass, answering the kick */
    prm(4, SRC(SY_SHAPE), 80); prm(4, SRC(SY_SUB), 50); prm(4, P_CUT, 56); prm(4, P_RES, 40); prm(4, P_FENV, 30);
    prm(4, P_ADEC, 36); prm(4, P_ASUS, 20); prm(4, P_LEVEL, 98); prm(4, P_DUCK, 40);
    trig(4, 0, 33, 110); trig(4, 3, 33, 90); trig(4, 8, 36, 96); trig(4, 10, 38, 100); trig(4, 14, 31, 84)->slide = 1;
    /* 6 SYNTH: a pad, one chord a bar, alternating */
    prm(5, SRC(SY_SHAPE), 60); prm(5, SRC(SY_DETUNE), 40); prm(5, SRC(SY_CHORD), 3);
    prm(5, P_AATK, 50); prm(5, P_ASUS, 100); prm(5, P_AREL, 70); prm(5, P_CUT, 54); prm(5, P_REV, 80); prm(5, P_LEVEL, 76);
    prm(5, P_LEN, 12);
    P->length[5] = 32;
    trig(5, 0, 57, 80); trig(5, 16, 62, 80);
    /* 7 FM: a stab */
    prm(6, SRC(FM_RATIO), 7); prm(6, SRC(FM_INDEX), 60); prm(6, SRC(FM_ENVDEC), 16); prm(6, SRC(FM_ENVAMT), 90);
    prm(6, P_ADEC, 20); prm(6, P_ASUS, 0); prm(6, P_LEVEL, 76); prm(6, P_DLY, 60);
    trig(6, 6, 72, 80)->cond = TD_COND_P1 + 4;
    /* 8 SAMPLE: the vocal */
    prm(7, SRC(SM_SLOT), 1); prm(7, P_LEVEL, 86); prm(7, P_DLY, 60);
    trig(7, 13, 60, 100)->cond = TD_COND_AB + 5;               /* 1:4 */

    scene(0, 0, P_LOWCUT, 60); scene(0, 5, P_REV, 120); scene(0, 2, P_REV, 90);
    scene(1, 2, SRC(SN_DRIVE), 80); scene(1, 4, P_CUT, 90); scene(1, 0, SRC(KK_DRIVE), 70);
}

/* ---- more four on the floor (P09 .. P16) */

static const prog_t PROG_MELO = {{33, 29, 36, 31}, {57, 53, 60, 55}, {1, 2, 2, 4}};   /* Am7 Fmaj7 Cmaj7 Gsus4 */

/* an arpeggio over a 4-bar progression: every sixteenth, up the chord's tones (td_chord_iv), an octave above */
static void arp(int t, const prog_t *pg, int vel)
{
    static const int8_t iv[8][4] = {{0, 12, 7, 12}, {0, 3, 7, 10}, {0, 4, 7, 11}, {0, 3, 10, 14},
                                    {0, 5, 7, 12}, {0, 7, 12, 7}, {0, 7, 10, 15}, {0, 7, 10, 14}};
    P->length[t] = 64;
    for (int b = 0; b < 4; b++)
        for (int s = 0; s < 16; s++) {
            int ty = pg->type[b] & 7;
            int n = pg->chord[b] + 12 + iv[ty][s % 4] + (s / 8) * 12 * (s % 4 == 3);
            trig(t, b * 16 + s, n, s % 4 == 0 ? vel : vel - 24);
        }
}
static void bass4(int t, const prog_t *pg, const int *offs, int n, int vel)   /* the same rhythm on each bar's root */
{
    P->length[t] = 64;
    for (int b = 0; b < 4; b++)
        for (int i = 0; i < n; i++) trig(t, b * 16 + offs[i], pg->bass[b], i == 0 ? vel : vel - 18);
}
static void pad4(int t, const prog_t *pg, int vel)                     /* one held chord a bar */
{
    P->length[t] = 64;
    for (int b = 0; b < 4; b++) { trig(t, b * 16, pg->chord[b], vel); lock(t, b * 16, SRC(SY_CHORD), pg->type[b]); }
}
static void synth_pad(int t)
{
    prm(t, SRC(SY_SHAPE), 64); prm(t, SRC(SY_DETUNE), 44); prm(t, P_AATK, 60); prm(t, P_ASUS, 100); prm(t, P_AREL, 80);
    prm(t, P_CUT, 52); prm(t, P_REV, 80); prm(t, P_CHO, 50); prm(t, P_LEVEL, 76); prm(t, P_LEN, 12); prm(t, P_DUCK, 60);
}
static void synth_sub(int t)
{
    prm(t, SRC(SY_SHAPE), 8); prm(t, SRC(SY_SUB), 40); prm(t, P_CUT, 54); prm(t, P_RES, 10); prm(t, P_ADEC, 30);
    prm(t, P_ASUS, 10); prm(t, P_AREL, 10); prm(t, P_LEVEL, 104); prm(t, P_DUCK, 80); prm(t, P_LEN, 2);
}

/* P09: tech house. A rolling sixteenth bass, a syncopated tom and rim groove, one note stabbed */
static void pat_tech(void)
{
    four_on_the_floor();
    prm(0, SRC(KK_DECAY), 58); prm(0, SRC(KK_CLICK), 60); prm(0, SRC(KK_DRIVE), 40);
    synth_sub(4); prm(4, SRC(SY_SHAPE), 40); prm(4, P_RES, 40); prm(4, P_FENV, 30); prm(4, P_FDEC, 20);
    for (int s = 0; s < 16; s++) if (s % 4) trig(4, s, s % 4 == 2 ? 33 : 45, s % 4 == 2 ? 100 : 66);   /* A1, A2 ghosts */
    lock(4, 14, P_CUT, 80);
    prm(5, SRC(SY_SHAPE), 100); prm(5, P_CUT, 70); prm(5, P_RES, 60); prm(5, P_ADEC, 18); prm(5, P_ASUS, 0);
    prm(5, P_DLY, 70); prm(5, P_LEVEL, 86);
    trig(5, 7, 69, 90); trig(5, 15, 69, 70)->cond = TD_COND_P1 + 3;
    td_set_machine_x(P, 6, TD_M_TOM);
    prm(6, SRC(TM_TUNE), 4); prm(6, SRC(TM_DECAY), 30); prm(6, SRC(TM_NOISE), 30); prm(6, P_LEVEL, 82); prm(6, P_PAN, 24);
    trig(6, 3, 60, 90); trig(6, 6, 64, 70); trig(6, 11, 60, 80); trig(6, 13, 67, 66)->cond = TD_COND_AB + 0;
    td_set_machine_x(P, 7, TD_M_SNARE);
    prm(7, SRC(SN_TUNE), 24); prm(7, SRC(SN_DECAY), 10); prm(7, SRC(SN_NOISE), 20); prm(7, SRC(SN_SNAP), 100);
    prm(7, P_LEVEL, 76); prm(7, P_PAN, -24);
    trig(7, 5, 60, 80); trig(7, 9, 60, 60); trig(7, 14, 60, 70)->cond = TD_COND_P1 + 4;
    scene(0, 4, P_CUT, 30); scene(0, 0, P_LOWCUT, 60);
    scene(1, 4, P_CUT, 90); scene(1, 4, P_RES, 90); scene(1, 5, P_DLY, 110);
}

/* P10: minimal. A clicky kick, hats left to chance, a rim on 7 steps and a tom on 5, a deep sub */
static void pat_minimal(void)
{
    four_on_the_floor();
    prm(0, SRC(KK_DECAY), 40); prm(0, SRC(KK_CLICK), 90); prm(0, SRC(KK_TONE), 110); prm(0, SRC(KK_SWEEP), 20);
    for (int s = 0; s < 16; s++) if (P->step[3][s].trig) P->step[3][s].cond = TD_COND_P1 + (s % 4 == 2 ? 6 : 3);
    P->step[2][12].trig = 0; P->step[2][15].trig = 0;          /* the clap only on 2, sparse */
    synth_sub(4);
    trig(4, 2, 33, 100); trig(4, 11, 33, 80); trig(4, 13, 36, 70)->cond = TD_COND_AB + 0;
    prm(5, SRC(SY_SHAPE), 30); prm(5, P_CUT, 60); prm(5, P_ADEC, 14); prm(5, P_ASUS, 0); prm(5, P_DLY, 90);
    prm(5, P_LEVEL, 74);
    trig(5, 10, 76, 80)->cond = TD_COND_P1 + 2;
    td_set_machine_x(P, 6, TD_M_TOM);
    prm(6, SRC(TM_TUNE), -6); prm(6, SRC(TM_DECAY), 24); prm(6, P_LEVEL, 76); prm(6, P_PAN, 20);
    P->length[6] = 5; trig(6, 2, 60, 80);
    td_set_machine_x(P, 7, TD_M_SNARE);
    prm(7, SRC(SN_TUNE), 24); prm(7, SRC(SN_DECAY), 8); prm(7, SRC(SN_NOISE), 10); prm(7, SRC(SN_SNAP), 110);
    prm(7, P_LEVEL, 72); prm(7, P_PAN, -20);
    P->length[7] = 7; trig(7, 3, 60, 80);
    scene(0, 0, P_LOWCUT, 70); scene(0, 5, P_REV, 110);
    scene(1, 5, P_DLY, 120); scene(1, 6, SRC(TM_DECAY), 80);
}

/* P11: melodic techno. A sixteenth arpeggio over Am7 Fmaj7 Cmaj7 Gsus4, an off-beat sub, a pad */
static void pat_melodic(void)
{
    four_on_the_floor();
    synth_sub(4);
    static const int off[] = {2, 6, 10, 14};
    bass4(4, &PROG_MELO, off, 4, 100);
    prm(5, SRC(SY_SHAPE), 96); prm(5, SRC(SY_DETUNE), 10); prm(5, P_CUT, 58); prm(5, P_RES, 50); prm(5, P_FENV, 24);
    prm(5, P_ADEC, 18); prm(5, P_ASUS, 0); prm(5, P_AREL, 14); prm(5, P_DLY, 50); prm(5, P_REV, 40); prm(5, P_LEVEL, 80);
    prm(5, P_LSPD, 6); prm(5, P_LMUL, 1); prm(5, P_LWAVE, 1); prm(5, P_LDEST, 9); prm(5, P_LDEPTH, 30);   /* (a slow sweep) */
    arp(5, &PROG_MELO, 96);
    td_set_machine_x(P, 6, TD_M_SYNTH); synth_pad(6);
    pad4(6, &PROG_MELO, 80);
    prm(7, SRC(SM_SLOT), 3); prm(7, P_LEVEL, 76); prm(7, P_REV, 60);
    trig(7, 0, 60, 100)->cond = TD_COND_AB + 9;                /* the riser, 1:8 */
    scene(0, 5, P_CUT, 26); scene(0, 0, P_LOWCUT, 80); scene(0, 6, P_REV, 127);
    scene(1, 5, P_CUT, 100); scene(1, 5, P_RES, 90); scene(1, 5, P_DLY, 100);
}

/* P12: filter house. Disco chords pumping under the kick, an LFO sweeping their filter, an octave bass */
static void pat_filter(void)
{
    four_on_the_floor();
    prm(4, SRC(SY_SHAPE), 70); prm(4, SRC(SY_SUB), 30); prm(4, P_CUT, 60); prm(4, P_ADEC, 26); prm(4, P_ASUS, 20);
    prm(4, P_LEVEL, 96); prm(4, P_DUCK, 50);
    P->length[4] = 64;
    for (int b = 0; b < 4; b++)
        for (int e = 0; e < 8; e++) trig(4, b * 16 + e * 2 + 1, PROG_DISCO.bass[b] + (e % 2 ? 12 : 0), e % 2 ? 76 : 100);
    prm(5, SRC(SY_SHAPE), 90); prm(5, SRC(SY_DETUNE), 26); prm(5, P_CUT, 50); prm(5, P_RES, 50); prm(5, P_ASUS, 100);
    prm(5, P_ADEC, 60); prm(5, P_AREL, 30); prm(5, P_LEVEL, 92); prm(5, P_DUCK, 127); prm(5, P_CHO, 60); prm(5, P_LEN, 11);
    prm(5, P_LSPD, 4); prm(5, P_LMUL, 1); prm(5, P_LWAVE, 0); prm(5, P_LDEST, 9); prm(5, P_LDEPTH, 40);   /* the sweep */
    pad4(5, &PROG_DISCO, 96);
    prm(7, SRC(SM_SLOT), 1); prm(7, P_LEVEL, 86); prm(7, P_DLY, 70);
    trig(7, 7, 60, 100)->cond = TD_COND_AB + 1;
    scene(0, 5, P_CUT, 20); scene(0, 4, P_CUT, 30);
    scene(1, 5, P_CUT, 110); scene(1, 5, P_RES, 90);
}

/* P13: jackin' house. Claps on 2 and 4 with pickups, a heavy shuffle, electric-piano stabs, a bouncing bass */
static void pat_jackin(void)
{
    four_on_the_floor();
    for (int s = 1; s < 16; s += 2) P->step[3][s].micro = 8;   /* a heavy shuffle */
    trig(2, 11, 60, 70); trig(2, 14, 60, 64)->cond = TD_COND_P1 + 4;
    prm(4, SRC(SY_SHAPE), 80); prm(4, SRC(SY_SUB), 50); prm(4, P_CUT, 56); prm(4, P_RES, 30); prm(4, P_ADEC, 28);
    prm(4, P_ASUS, 10); prm(4, P_LEVEL, 100); prm(4, P_DUCK, 50);
    trig(4, 2, 33, 100); trig(4, 5, 45, 70); trig(4, 8, 33, 84); trig(4, 10, 36, 96); trig(4, 13, 38, 80);
    td_set_machine_x(P, 5, TD_M_FM);                           /* the electric piano, minor 7 */
    prm(5, SRC(FM_RATIO), 1); prm(5, SRC(FM_INDEX), 34); prm(5, SRC(FM_FDBK), 6); prm(5, SRC(FM_ENVDEC), 24);
    prm(5, SRC(FM_ENVAMT), 80); prm(5, SRC(FM_CHORD), 1); prm(5, P_ADEC, 44); prm(5, P_ASUS, 0); prm(5, P_LEVEL, 90);
    prm(5, P_CHO, 50); prm(5, P_REV, 30);
    trig(5, 3, 57, 96); trig(5, 6, 57, 80); trig(5, 11, 62, 90); trig(5, 14, 60, 70)->cond = TD_COND_AB + 0;
    prm(7, SRC(SM_SLOT), 1); prm(7, P_LEVEL, 90);
    trig(7, 4, 60, 100)->cond = TD_COND_AB + 5;                /* 1:4 */
    scene(0, 5, P_REV, 110); scene(0, 0, P_LOWCUT, 60);
    scene(1, 5, SRC(FM_INDEX), 90); scene(1, 4, P_CUT, 90);
}

/* P14: afro house. A 3-3-2 tom pattern, a sixteenth shaker, marimba-like FM plucks, sparse chords */
static void pat_afro(void)
{
    four_on_the_floor();
    prm(3, SRC(HH_COLOR), 100); prm(3, SRC(HH_BRIGHT), 70); prm(3, SRC(HH_DECAY), 20);   /* a shaker */
    P->step[2][7].trig = 0;
    td_set_machine_x(P, 6, TD_M_TOM);
    prm(6, SRC(TM_TUNE), 2); prm(6, SRC(TM_DECAY), 50); prm(6, SRC(TM_SWEEP), 4); prm(6, P_LEVEL, 88); prm(6, P_PAN, 18);
    trig(6, 0, 62, 96); trig(6, 3, 57, 80); trig(6, 6, 62, 90); trig(6, 10, 55, 76); trig(6, 13, 57, 84);
    synth_sub(4);
    trig(4, 0, 33, 96); trig(4, 6, 33, 84); trig(4, 10, 36, 90); trig(4, 13, 31, 76);
    td_set_machine_x(P, 5, TD_M_FM);                           /* the marimba: ratio 4, a fast index */
    prm(5, SRC(FM_RATIO), 7); prm(5, SRC(FM_INDEX), 40); prm(5, SRC(FM_ENVDEC), 10); prm(5, SRC(FM_ENVAMT), 100);
    prm(5, P_ADEC, 22); prm(5, P_ASUS, 0); prm(5, P_LEVEL, 84); prm(5, P_REV, 40); prm(5, P_DLY, 30);
    P->length[5] = 32;
    static const int mar[][2] = {{0, 69}, {3, 72}, {6, 76}, {8, 74}, {11, 72}, {14, 69}, {16, 67}, {19, 69}, {22, 72},
                                 {26, 74}, {29, 72}};
    for (int i = 0; i < 11; i++) trig(5, mar[i][0], mar[i][1], 80 + (i % 3) * 8);
    td_set_machine_x(P, 7, TD_M_SYNTH); synth_pad(7);
    P->length[7] = 32; trig(7, 0, 57, 70); lock(7, 0, SRC(SY_CHORD), 1); trig(7, 16, 53, 70); lock(7, 16, SRC(SY_CHORD), 2);
    scene(0, 6, P_LEVEL, 30); scene(0, 0, P_LOWCUT, 60); scene(0, 7, P_REV, 127);
    scene(1, 5, SRC(FM_INDEX), 90); scene(1, 6, P_DLY, 80);
}

/* P15: hard techno. A distorted kick, rumble, off-beat hats, a reverb clap, an acid edge */
static void pat_hard(void)
{
    pat_techno();
    prm(0, SRC(KK_DRIVE), 110); prm(0, SRC(KK_CLICK), 90); prm(0, SRC(KK_DECAY), 60); prm(0, P_ODRV, 60);
    prm(2, P_REV, 90); prm(2, SRC(CP_DRIVE), 60);
    prm(4, P_ODRV, 70); prm(4, P_DUCK, 110);
    td_set_machine_x(P, 6, TD_M_SYNTH);                        /* the acid edge instead of the tom */
    prm(6, SRC(SY_SHAPE), 110); prm(6, SRC(SY_GLIDE), 30); prm(6, P_CUT, 40); prm(6, P_RES, 110); prm(6, P_FENV, 50);
    prm(6, P_ADEC, 30); prm(6, P_ASUS, 10); prm(6, P_ODRV, 80); prm(6, P_LEVEL, 76); prm(6, P_DLY, 40);
    memset(P->step[6], 0, sizeof P->step[6]);
    P->length[6] = 16;
    trig(6, 2, 45, 100)->accent = 1; trig(6, 3, 45, 80)->slide = 1; trig(6, 7, 48, 90); trig(6, 11, 45, 100)->accent = 1;
    trig(6, 14, 52, 80)->slide = 1;
    scene(1, 6, P_CUT, 100);
}

/* P16: progressive. A galloping bass, a pluck arpeggio over 4 bars, a pad */
static void pat_prog(void)
{
    four_on_the_floor();
    synth_sub(4); prm(4, SRC(SY_SHAPE), 50); prm(4, P_RES, 30);
    static const int gal[] = {2, 3, 6, 7, 10, 11, 14, 15};
    bass4(4, &PROG_DEEP, gal, 8, 100);
    td_set_machine_x(P, 5, TD_M_FM);
    prm(5, SRC(FM_RATIO), 3); prm(5, SRC(FM_INDEX), 40); prm(5, SRC(FM_ENVDEC), 16); prm(5, SRC(FM_ENVAMT), 80);
    prm(5, P_ADEC, 20); prm(5, P_ASUS, 0); prm(5, P_LEVEL, 76); prm(5, P_DLY, 60); prm(5, P_REV, 50);
    arp(5, &PROG_DEEP, 90);
    for (int s = 0; s < 64; s++) if (s % 4 == 1) P->step[5][s].cond = TD_COND_P1 + 5;   /* (75 %: it breathes) */
    td_set_machine_x(P, 6, TD_M_SYNTH); synth_pad(6);
    pad4(6, &PROG_DEEP, 76);
    prm(7, SRC(SM_SLOT), 3); prm(7, P_LEVEL, 76); prm(7, P_REV, 60);
    trig(7, 0, 60, 100)->cond = TD_COND_AB + 9;
    scene(0, 5, P_LEVEL, 30); scene(0, 0, P_LOWCUT, 70); scene(0, 6, P_CUT, 30);
    scene(1, 5, SRC(FM_INDEX), 80); scene(1, 6, P_CUT, 90);
}

/* ---- garage and drum & bass (with their own tempos) */

static void fm_ep(int t, int chord)                            /* an FM electric piano (ratio 1, the tine's index dying) */
{
    td_set_machine_x(P, t, TD_M_FM);
    prm(t, SRC(FM_RATIO), 1); prm(t, SRC(FM_INDEX), 34); prm(t, SRC(FM_FDBK), 6); prm(t, SRC(FM_ENVDEC), 24);
    prm(t, SRC(FM_ENVAMT), 80); prm(t, SRC(FM_CHORD), chord); prm(t, P_ADEC, 60); prm(t, P_ASUS, 30); prm(t, P_AREL, 40);
    prm(t, P_LEVEL, 88); prm(t, P_CHO, 50); prm(t, P_REV, 40);
}
static void organ(int t)                                       /* a garage organ: square, detuned, short, chorused */
{
    prm(t, SRC(SY_SHAPE), 124); prm(t, SRC(SY_DETUNE), 20); prm(t, SRC(SY_SUB), 40); prm(t, P_CUT, 80); prm(t, P_RES, 10);
    prm(t, P_ADEC, 30); prm(t, P_ASUS, 20); prm(t, P_AREL, 12); prm(t, P_CHO, 60); prm(t, P_LEVEL, 92);
}
static void shuffle_hats(int t, int late)                      /* closed sixteenths, every second one late */
{
    prm(t, SRC(HH_DECAY), 10); prm(t, SRC(HH_BRIGHT), 104); prm(t, P_LEVEL, 80); prm(t, P_LOWCUT, 90); prm(t, P_PAN, -16);
    for (int s = 0; s < 16; s++) {
        td_step_t *st = trig(t, s, 60, s % 2 ? 56 : (s % 4 ? 80 : 96));
        if (s % 2) st->micro = (int8_t)late;
    }
}

/* P10: speed garage. Four on the floor, shuffled hats, the "wheezy" reese bass on the off-beats dropping by slides,
 * organ stabs, the vocal */
static void pat_speed(void)
{
    four_on_the_floor();
    memset(P->step[3], 0, sizeof P->step[3]);
    shuffle_hats(3, 9);
    prm(4, SRC(SY_SHAPE), 92); prm(4, SRC(SY_DETUNE), 70); prm(4, SRC(SY_SUB), 60); prm(4, SRC(SY_GLIDE), 40);
    prm(4, P_CUT, 50); prm(4, P_RES, 40); prm(4, P_ADEC, 80); prm(4, P_ASUS, 70); prm(4, P_ODRV, 40); prm(4, P_LEVEL, 100);
    prm(4, P_DUCK, 60); prm(4, P_LSPD, 40); prm(4, P_LMUL, 2); prm(4, P_LDEST, 9); prm(4, P_LDEPTH, 24);
    P->length[4] = 32;
    static const int wz[][3] = {{2, 33, 0}, {6, 33, 0}, {7, 31, 1}, {10, 33, 0}, {14, 36, 1}, {15, 33, 1},
                                {18, 38, 0}, {22, 38, 0}, {23, 36, 1}, {26, 33, 0}, {30, 31, 1}, {31, 33, 1}};
    for (int i = 0; i < 12; i++) trig(4, wz[i][0], wz[i][1], 104)->slide = (uint8_t)wz[i][2];
    organ(5); prm(5, SRC(SY_CHORD), 1);
    trig(5, 3, 57, 90); trig(5, 11, 57, 84); trig(5, 14, 60, 70)->cond = TD_COND_AB + 0;
    prm(7, SRC(SM_SLOT), 1); prm(7, P_LEVEL, 90); prm(7, P_DLY, 60);
    trig(7, 6, 60, 100)->cond = TD_COND_AB + 1;
    scene(0, 4, P_CUT, 26); scene(0, 0, P_LOWCUT, 60);
    scene(1, 4, P_CUT, 90); scene(1, 4, P_ODRV, 90); scene(1, 5, P_CUT, 110);
}

/* P11: 4x4 garage. Four on the floor with a swung shuffle and a rim, an organ bass jumping octaves, electric-piano
 * minor 7 chords, the vocal */
static void pat_4x4(void)
{
    four_on_the_floor();
    memset(P->step[3], 0, sizeof P->step[3]);
    shuffle_hats(3, 7);
    prm(0, SRC(KK_DECAY), 66); prm(0, SRC(KK_CLICK), 40);
    organ(4); prm(4, SRC(SY_SHAPE), 110); prm(4, P_CUT, 56); prm(4, P_DUCK, 40);
    P->length[4] = 32;
    static const int ob[][2] = {{0, 33}, {3, 45}, {6, 33}, {10, 45}, {13, 33}, {16, 38}, {19, 50}, {22, 38}, {26, 50}, {29, 36}};
    for (int i = 0; i < 10; i++) trig(4, ob[i][0], ob[i][1], ob[i][1] > 40 ? 76 : 104);
    fm_ep(5, 1);
    P->length[5] = 32;
    trig(5, 2, 57, 96); trig(5, 7, 57, 80); trig(5, 10, 57, 88); trig(5, 18, 62, 96); trig(5, 23, 62, 80);
    trig(5, 26, 60, 88)->cond = TD_COND_AB + 0;
    td_set_machine_x(P, 6, TD_M_SNARE);                         /* a rim */
    prm(6, SRC(SN_TUNE), 24); prm(6, SRC(SN_DECAY), 8); prm(6, SRC(SN_NOISE), 16); prm(6, SRC(SN_SNAP), 110);
    prm(6, P_LEVEL, 74); prm(6, P_PAN, 22);
    trig(6, 3, 60, 70); trig(6, 7, 60, 60); trig(6, 13, 60, 64)->cond = TD_COND_P1 + 4;
    prm(7, SRC(SM_SLOT), 1); prm(7, P_LEVEL, 88); prm(7, P_DLY, 70);
    trig(7, 12, 60, 100)->cond = TD_COND_AB + 5;
    scene(0, 5, P_REV, 110); scene(0, 4, P_CUT, 30);
    scene(1, 5, SRC(FM_INDEX), 80); scene(1, 4, P_CUT, 100);
}

/* P12: bassline. Four on the floor, fast; a wobbling bass jumping octaves with slides, organ stabs */
static void pat_bassline(void)
{
    four_on_the_floor();
    memset(P->step[3], 0, sizeof P->step[3]);
    shuffle_hats(3, 6);
    prm(4, SRC(SY_SHAPE), 100); prm(4, SRC(SY_DETUNE), 30); prm(4, SRC(SY_SUB), 70); prm(4, SRC(SY_GLIDE), 30);
    prm(4, P_CUT, 44); prm(4, P_RES, 60); prm(4, P_ADEC, 60); prm(4, P_ASUS, 60); prm(4, P_ODRV, 60); prm(4, P_LEVEL, 98);
    prm(4, P_DUCK, 70); prm(4, P_LSPD, 64); prm(4, P_LMUL, 3); prm(4, P_LWAVE, 1); prm(4, P_LDEST, 9); prm(4, P_LDEPTH, 40);
    static const int bl[][3] = {{1, 33, 0}, {3, 45, 1}, {5, 33, 0}, {7, 40, 1}, {9, 33, 0}, {11, 45, 1}, {13, 43, 0}, {15, 40, 1}};
    for (int i = 0; i < 8; i++) trig(4, bl[i][0], bl[i][1], 104)->slide = (uint8_t)bl[i][2];
    organ(5); prm(5, SRC(SY_CHORD), 1);
    trig(5, 2, 57, 90); trig(5, 10, 57, 84);
    prm(7, SRC(SM_SLOT), 1); prm(7, P_LEVEL, 88); prm(7, P_DLY, 60);
    trig(7, 6, 60, 100)->cond = TD_COND_AB + 5;
    scene(0, 4, P_CUT, 24); scene(0, 4, P_LDEPTH, 0);
    scene(1, 4, P_CUT, 80); scene(1, 4, P_LDEPTH, 63);
}

/* the drum & bass two-step: the kick on 1 and the "and" of 3, the snare on 2 and 4, ghosts, over 2 bars */
static void dnb_beat(int ghosts)
{
    td_set_machine_x(P, 2, TD_M_SNARE);
    prm(0, SRC(KK_DECAY), 40); prm(0, SRC(KK_CLICK), 70); prm(0, SRC(KK_SWEEP), 30); prm(0, SRC(KK_DRIVE), 30);
    prm(0, P_LEVEL, 112);
    P->length[0] = P->length[2] = 32;
    trig(0, 0, 60, 120); trig(0, 10, 60, 110); trig(0, 16, 60, 120); trig(0, 26, 60, 104);
    trig(0, 27, 60, 84)->cond = TD_COND_AB + 1;
    prm(2, SRC(SN_TUNE), 12); prm(2, SRC(SN_DECAY), 34); prm(2, SRC(SN_NOISE), 84); prm(2, SRC(SN_SNAP), 96);
    prm(2, SRC(SN_BODY), 60); prm(2, P_LEVEL, 108); prm(2, P_REV, 24);
    for (int b = 0; b < 2; b++) { trig(2, b * 16 + 4, 60, 116); trig(2, b * 16 + 12, 60, 116); }
    if (ghosts) { trig(2, 7, 60, 44); trig(2, 15, 60, 40)->cond = TD_COND_P1 + 4; trig(2, 25, 60, 46); trig(2, 30, 60, 50); }
    /* 2 HAT: an open hat, sparse; 4 HAT: eighths with sixteenth ghosts */
    prm(1, SRC(HH_DECAY), 40); prm(1, SRC(HH_OPEN), 70); prm(1, P_LEVEL, 76); prm(1, P_LOWCUT, 90);
    trig(1, 14, 60, 80); trig(1, 30, 60, 70)->cond = TD_COND_P1 + 3;
    prm(3, SRC(HH_DECAY), 12); prm(3, SRC(HH_BRIGHT), 110); prm(3, P_LEVEL, 78); prm(3, P_LOWCUT, 94); prm(3, P_PAN, -14);
    for (int s = 0; s < 16; s++) {
        td_step_t *st = trig(3, s, 60, s % 2 ? 46 : (s % 4 ? 72 : 90));
        if (s % 2) st->cond = TD_COND_P1 + 4;
    }
}

/* P13: liquid. The two-step, a long sub on the roots (Am7 Fmaj7 Cmaj7 Gsus4), electric-piano chords, a pad */
static void pat_liquid(void)
{
    dnb_beat(1);
    synth_sub(4); prm(4, SRC(SY_GLIDE), 50); prm(4, P_ADEC, 90); prm(4, P_ASUS, 90); prm(4, P_AREL, 40); prm(4, P_LEN, 12);
    prm(4, P_DUCK, 30);
    static const int r1[] = {0, 10};
    bass4(4, &PROG_MELO, r1, 2, 104);
    fm_ep(5, 0);
    P->length[5] = 64;
    for (int b = 0; b < 4; b++)                                /* the chord on 1, again on the "and" of 2 */
        for (int k = 0; k < 2; k++) {
            int s = b * 16 + (k ? 7 : 0);
            trig(5, s, PROG_MELO.chord[b], k ? 70 : 90);
            lock(5, s, SRC(FM_CHORD), PROG_MELO.type[b]);
        }
    td_set_machine_x(P, 6, TD_M_SYNTH); synth_pad(6); prm(6, P_LEVEL, 64);
    pad4(6, &PROG_MELO, 70);
    prm(7, SRC(SM_SLOT), 1); prm(7, P_LEVEL, 80); prm(7, P_DLY, 80); prm(7, P_REV, 70);
    trig(7, 20, 60, 100)->cond = TD_COND_AB + 5;
    scene(0, 0, P_LEVEL, 60); scene(0, 2, P_REV, 110); scene(0, 6, P_LEVEL, 100);
    scene(1, 5, SRC(FM_INDEX), 80); scene(1, 4, P_ODRV, 60);
}

/* P14: neuro. A tight two-step, a reese bass (detuned saws, the filter moving) in a sixteenth rhythm, metallic stabs */
static void pat_neuro(void)
{
    dnb_beat(0);
    prm(0, SRC(KK_DRIVE), 70); prm(2, SRC(SN_DRIVE), 50);
    prm(4, SRC(SY_SHAPE), 90); prm(4, SRC(SY_DETUNE), 90); prm(4, SRC(SY_SUB), 60); prm(4, P_CUT, 40); prm(4, P_RES, 60);
    prm(4, P_FENV, 30); prm(4, P_ADEC, 40); prm(4, P_ASUS, 60); prm(4, P_ODRV, 90); prm(4, P_LEVEL, 96); prm(4, P_DUCK, 50);
    prm(4, P_LSPD, 50); prm(4, P_LMUL, 2); prm(4, P_LWAVE, 2); prm(4, P_LDEST, 9); prm(4, P_LDEPTH, 30);
    P->length[4] = 32;
    static const int rz[][2] = {{2, 33}, {3, 33}, {6, 36}, {8, 33}, {13, 31}, {18, 33}, {19, 33}, {22, 38}, {24, 36}, {29, 40}};
    for (int i = 0; i < 10; i++) { trig(4, rz[i][0], rz[i][1], 100); lock(4, rz[i][0], P_CUT, 30 + (i * 7) % 50); }
    td_set_machine_x(P, 6, TD_M_FM);                           /* metallic stabs */
    prm(6, SRC(FM_RATIO), 14); prm(6, SRC(FM_INDEX), 90); prm(6, SRC(FM_FDBK), 60); prm(6, SRC(FM_ENVDEC), 10);
    prm(6, SRC(FM_ENVAMT), 100); prm(6, P_ADEC, 16); prm(6, P_ASUS, 0); prm(6, P_LEVEL, 72); prm(6, P_DLY, 40);
    trig(6, 11, 57, 90); trig(6, 15, 64, 70)->cond = TD_COND_AB + 0;
    prm(7, SRC(SM_SLOT), 3); prm(7, P_LEVEL, 70);
    trig(7, 0, 60, 100)->cond = TD_COND_AB + 9;
    scene(0, 4, P_CUT, 20); scene(0, 4, P_LDEPTH, 0);
    scene(1, 4, P_CUT, 90); scene(1, 4, P_RES, 100); scene(1, 4, P_LDEPTH, 63);
}

/* P15: jungle. A chopped break (kicks and snares moving, ghosts everywhere, a ride), a deep sine sub, a pad */
static void pat_jungle(void)
{
    dnb_beat(1);
    memset(P->step[0], 0, sizeof P->step[0]);
    memset(P->step[2], 0, sizeof P->step[2]);
    static const int kk[] = {0, 2, 10, 16, 18, 24, 27};
    for (int i = 0; i < 7; i++) trig(0, kk[i], 60, i % 3 ? 100 : 120);
    static const int sn[][2] = {{4, 116}, {7, 50}, {9, 60}, {12, 116}, {15, 54}, {20, 116}, {22, 48}, {25, 60}, {28, 116}, {30, 70}, {31, 90}};
    for (int i = 0; i < 11; i++) trig(2, sn[i][0], 60, sn[i][1]);
    P->step[2][31].retrig = 2;
    prm(1, SRC(HH_COLOR), 96); prm(1, SRC(HH_DECAY), 70); prm(1, SRC(HH_OPEN), 100);   /* a ride */
    memset(P->step[1], 0, sizeof P->step[1]);
    for (int s = 0; s < 32; s += 4) trig(1, s + 2, 60, 70);
    synth_sub(4); prm(4, SRC(SY_SHAPE), 0); prm(4, SRC(SY_GLIDE), 70); prm(4, P_ADEC, 100); prm(4, P_ASUS, 100);
    prm(4, P_LEN, 12); prm(4, P_DUCK, 20);
    P->length[4] = 64;
    trig(4, 0, 33, 110); trig(4, 24, 31, 100)->slide = 1; trig(4, 32, 29, 110); trig(4, 56, 28, 100)->slide = 1;
    td_set_machine_x(P, 6, TD_M_SYNTH); synth_pad(6); pad4(6, &PROG_DEEP, 70);
    prm(7, SRC(SM_SLOT), 1); prm(7, P_LEVEL, 86); prm(7, P_DLY, 70); prm(7, P_REV, 60);
    trig(7, 14, 60, 100)->cond = TD_COND_AB + 1;
    scene(0, 0, P_LOWCUT, 70); scene(0, 2, P_REV, 110);
    scene(1, 2, SRC(SN_DRIVE), 90); scene(1, 4, P_ODRV, 70);
}

/* P16: roller. A minimal two-step, rolling sixteenth hats, a sub rolling on eighths, a tom groove, a stab */
static void pat_roller(void)
{
    dnb_beat(1);
    for (int s = 0; s < 16; s++) P->step[3][s].cond = s % 2 ? TD_COND_P1 + 5 : 0;
    synth_sub(4); prm(4, SRC(SY_SHAPE), 20);
    P->length[4] = 32;
    static const int ro[][2] = {{0, 33}, {3, 33}, {6, 33}, {8, 36}, {11, 33}, {14, 31}, {16, 33}, {19, 33}, {22, 38}, {24, 36}, {27, 33}, {30, 31}};
    for (int i = 0; i < 12; i++) trig(4, ro[i][0], ro[i][1], i % 3 ? 86 : 104);
    td_set_machine_x(P, 6, TD_M_TOM);
    prm(6, SRC(TM_TUNE), 6); prm(6, SRC(TM_DECAY), 30); prm(6, SRC(TM_NOISE), 20); prm(6, P_LEVEL, 76); prm(6, P_PAN, 22);
    trig(6, 6, 60, 80); trig(6, 13, 64, 70);
    prm(5, SRC(SY_SHAPE), 96); prm(5, SRC(SY_CHORD), 1); prm(5, P_CUT, 64); prm(5, P_ADEC, 20); prm(5, P_ASUS, 0);
    prm(5, P_DLY, 80); prm(5, P_LEVEL, 76);
    trig(5, 18, 57, 90)->cond = TD_COND_AB + 1;
    scene(0, 4, P_CUT, 30); scene(0, 0, P_LOWCUT, 60);
    scene(1, 4, P_ODRV, 80); scene(1, 5, P_DLY, 110);
}

/* P05: electro. Broken kicks, a snare on 2 and 4, sixteenth hats, a square bass, an FM zap */
static void pat_electro(void)
{
    td_set_machine_x(P, 2, TD_M_SNARE);
    /* 1 KICK: deeper, longer */
    prm(0, SRC(KK_TUNE), -4); prm(0, SRC(KK_DECAY), 78); prm(0, SRC(KK_SWEEP), 30); prm(0, SRC(KK_STIME), 40);
    prm(0, SRC(KK_CLICK), 30); prm(0, SRC(KK_DRIVE), 20); prm(0, SRC(KK_TONE), 80);
    prm(0, P_LEVEL, 112);
    trig(0, 0, 60, 120); trig(0, 6, 60, 104); trig(0, 10, 60, 112); trig(0, 11, 60, 90);
    trig(0, 14, 60, 80)->cond = TD_COND_AB + 0;                /* 1:2 */
    /* 2 HAT: closed sixteenths */
    prm(1, SRC(HH_DECAY), 12); prm(1, SRC(HH_OPEN), 0); prm(1, SRC(HH_COLOR), 30); prm(1, SRC(HH_BRIGHT), 100);
    prm(1, P_LEVEL, 86); prm(1, P_PAN, 14); prm(1, P_LOWCUT, 86);
    for (int s = 0; s < 16; s++) trig(1, s, 60, s % 2 ? 50 : 90);
    /* 3 SNARE on 2 and 4, a ghost */
    prm(2, SRC(SN_TUNE), 4); prm(2, SRC(SN_DECAY), 44); prm(2, SRC(SN_NOISE), 80); prm(2, SRC(SN_SNAP), 90);
    prm(2, SRC(SN_BODY), 60);
    prm(2, P_LEVEL, 104); prm(2, P_REV, 36); prm(2, P_LOWCUT, 50);
    trig(2, 4, 60, 110); trig(2, 12, 60, 110);
    trig(2, 15, 60, 54)->cond = TD_COND_P1 + 3;               /* 50 % */
    /* 4 HAT: one open hat */
    prm(3, SRC(HH_DECAY), 60); prm(3, SRC(HH_OPEN), 100); prm(3, P_LEVEL, 80); prm(3, P_LOWCUT, 80);
    trig(3, 14, 60, 90);
    /* 5 SYNTH: a square bass */
    prm(4, SRC(SY_SHAPE), 127); prm(4, SRC(SY_SUB), 40);
    prm(4, P_CUT, 50); prm(4, P_RES, 30); prm(4, P_FENV, 30); prm(4, P_FDEC, 30);
    prm(4, P_ADEC, 40); prm(4, P_ASUS, 30); prm(4, P_AREL, 10); prm(4, P_LEVEL, 92); prm(4, P_DUCK, 30); prm(4, P_LEN, 2);
    trig(4, 0, 33, 110); trig(4, 3, 33, 90); trig(4, 6, 36, 100); trig(4, 10, 33, 104); trig(4, 11, 40, 90);
    /* 7 FM: a zap */
    prm(6, SRC(FM_RATIO), 12); prm(6, SRC(FM_INDEX), 90); prm(6, SRC(FM_ENVDEC), 10); prm(6, SRC(FM_ENVAMT), 100);
    prm(6, P_ADEC, 20); prm(6, P_ASUS, 0); prm(6, P_LEVEL, 76); prm(6, P_DLY, 50); prm(6, P_PAN, -24);
    trig(6, 7, 72, 90); trig(6, 15, 79, 80)->cond = TD_COND_AB + 0;
    /* 8 SAMPLE: the vocal blip */
    prm(7, SRC(SM_SLOT), 1); prm(7, P_LEVEL, 86); prm(7, P_DLY, 50);
    trig(7, 12, 60, 100)->cond = TD_COND_AB + 1;               /* 2:2 */

    scene(0, 4, P_CUT, 24); scene(0, 2, P_REV, 110); scene(0, 1, P_LEVEL, 50);
    scene(1, 4, P_CUT, 90); scene(1, 4, P_RES, 90); scene(1, 6, SRC(FM_INDEX), 127); scene(1, 0, SRC(KK_DRIVE), 70);
}

static void build(int p, void (*fill)(void))
{
    P = &E.pat[p];
    td_pattern_init_kit(P);
    fill();
}

void td_default_project(void)
{
    /* four of each, each at its tempo: house (left deck pads 1-4), techno (5-8), garage (right 1-4), drum & bass (5-8) */
    static const struct { void (*fill)(void); int bpm; } SET[TD_PATTERNS] = {
        {pat_house, 122}, {pat_tech, 126}, {pat_jackin, 124}, {pat_afro, 122},
        {pat_techno, 130}, {pat_acid, 132}, {pat_dub, 122}, {pat_melodic, 124},
        {pat_garage, 132}, {pat_speed, 132}, {pat_4x4, 132}, {pat_bassline, 138},
        {pat_liquid, 174}, {pat_neuro, 174}, {pat_jungle, 165}, {pat_roller, 172},
    };
    for (int p = 0; p < TD_PATTERNS; p++) {
        build(p, SET[p].fill);
        E.pat[p].bpm10 = (uint16_t)(SET[p].bpm * 10);
    }

    E.bpm = 122.f;
    E.swing = 54;
    E.gparam[G_DTIME] = 6;                                     /* 3/16 */
    E.gparam[G_DFB] = 66;
    E.gparam[G_DTONE] = 56;
    E.gparam[G_RSIZE] = 86;
    E.gparam[G_RDAMP] = 62;
    E.gparam[G_COMP] = 40;
    E.gparam[G_FILTER] = 0;
    E.gparam[G_VOLUME] = 100;
    E.cur = 0;
}

/* ---- sound packs: kits on a fixed voice map (after the Syntakt's machine designs: a pitched body with sweep and
 * hold, a transient, noise with its own decay; hats from metallic noise; an electric piano from two-operator FM whose
 * modulation dies before the note) */

static void kit_808(void)
{
    /* 1 KICK: the long sine boom, little sweep, almost no click */
    prm(0, SRC(KK_TUNE), -5); prm(0, SRC(KK_DECAY), 112); prm(0, SRC(KK_SWEEP), 12); prm(0, SRC(KK_STIME), 30);
    prm(0, SRC(KK_CLICK), 8); prm(0, SRC(KK_DRIVE), 4); prm(0, SRC(KK_TONE), 56); prm(0, SRC(KK_HOLD), 24);
    prm(0, P_LEVEL, 112);
    /* 2 HAT, open: metallic */
    prm(1, SRC(HH_DECAY), 70); prm(1, SRC(HH_OPEN), 92); prm(1, SRC(HH_COLOR), 36); prm(1, SRC(HH_BRIGHT), 70);
    prm(1, SRC(HH_RESO), 34); prm(1, SRC(HH_SPREAD), 8); prm(1, P_LEVEL, 92); prm(1, P_LOWCUT, 80); prm(1, P_PAN, 10);
    /* 3 SNARE: two tones and noise */
    td_set_machine_x(P, 2, TD_M_SNARE);
    prm(2, SRC(SN_TUNE), 6); prm(2, SRC(SN_DECAY), 40); prm(2, SRC(SN_NOISE), 70); prm(2, SRC(SN_NDECAY), 46);
    prm(2, SRC(SN_SNAP), 46); prm(2, SRC(SN_BODY), 84); prm(2, SRC(SN_TONE), 50); prm(2, P_LEVEL, 100); prm(2, P_REV, 28);
    /* 4 HAT, closed */
    prm(3, SRC(HH_DECAY), 16); prm(3, SRC(HH_OPEN), 0); prm(3, SRC(HH_COLOR), 36); prm(3, SRC(HH_BRIGHT), 76);
    prm(3, SRC(HH_RESO), 24); prm(3, P_LEVEL, 88); prm(3, P_LOWCUT, 84); prm(3, P_PAN, -14);
    /* 5 SYNTH: the sub bass, sine, gliding */
    prm(4, SRC(SY_SHAPE), 0); prm(4, SRC(SY_SUB), 0); prm(4, SRC(SY_GLIDE), 50);
    prm(4, P_CUT, 96); prm(4, P_RES, 0); prm(4, P_FENV, 0); prm(4, P_AATK, 0); prm(4, P_ADEC, 92); prm(4, P_ASUS, 0);
    prm(4, P_AREL, 40); prm(4, P_LEVEL, 112); prm(4, P_ODRV, 10);
    /* 6 SYNTH: a triangle major 7 chord */
    prm(5, SRC(SY_SHAPE), 42); prm(5, SRC(SY_DETUNE), 10); prm(5, SRC(SY_CHORD), 2);
    prm(5, P_CUT, 72); prm(5, P_ADEC, 52); prm(5, P_ASUS, 0); prm(5, P_AREL, 30); prm(5, P_DLY, 40); prm(5, P_REV, 50);
    /* 7 TOM */
    td_set_machine_x(P, 6, TD_M_TOM);
    prm(6, SRC(TM_TUNE), -4); prm(6, SRC(TM_DECAY), 72); prm(6, SRC(TM_SWEEP), 6); prm(6, SRC(TM_STIME), 40);
    prm(6, SRC(TM_NOISE), 6); prm(6, SRC(TM_TONE), 60); prm(6, P_LEVEL, 96); prm(6, P_PAN, 20);
    /* 8 FM: the cowbell (two inharmonic tones, short) */
    td_set_machine_x(P, 7, TD_M_FM);
    prm(7, SRC(FM_RATIO), 2); prm(7, SRC(FM_INDEX), 30); prm(7, SRC(FM_FDBK), 0); prm(7, SRC(FM_ENVDEC), 20);
    prm(7, SRC(FM_ENVAMT), 30); prm(7, P_ADEC, 26); prm(7, P_ASUS, 0); prm(7, P_AREL, 10); prm(7, P_LEVEL, 80);
    prm(7, P_LOWCUT, 70);
    scene(0, 4, P_CUT, 40); scene(0, 5, P_REV, 100);
    scene(1, 0, SRC(KK_DRIVE), 70); scene(1, 4, P_ODRV, 60);
}

static void kit_909(void)
{
    /* 1 KICK: punch, click, drive */
    prm(0, SRC(KK_TUNE), 0); prm(0, SRC(KK_DECAY), 54); prm(0, SRC(KK_SWEEP), 34); prm(0, SRC(KK_STIME), 20);
    prm(0, SRC(KK_CLICK), 72); prm(0, SRC(KK_DRIVE), 46); prm(0, SRC(KK_TONE), 96); prm(0, SRC(KK_HOLD), 6);
    prm(0, P_LEVEL, 110);
    /* 2 HAT, open: bright noise */
    prm(1, SRC(HH_DECAY), 56); prm(1, SRC(HH_OPEN), 86); prm(1, SRC(HH_COLOR), 86); prm(1, SRC(HH_BRIGHT), 100);
    prm(1, SRC(HH_RESO), 10); prm(1, SRC(HH_SPREAD), 24); prm(1, P_LEVEL, 92); prm(1, P_LOWCUT, 80); prm(1, P_PAN, 10);
    /* 3 CLAP */
    prm(2, SRC(CP_DECAY), 50); prm(2, SRC(CP_SPREAD), 50); prm(2, SRC(CP_BURSTS), 4); prm(2, SRC(CP_TONE), 80);
    prm(2, SRC(CP_ROOM), 50); prm(2, SRC(CP_RAND), 30); prm(2, P_LEVEL, 100); prm(2, P_REV, 40); prm(2, P_LOWCUT, 60);
    /* 4 HAT, closed */
    prm(3, SRC(HH_DECAY), 12); prm(3, SRC(HH_OPEN), 0); prm(3, SRC(HH_COLOR), 86); prm(3, SRC(HH_BRIGHT), 104);
    prm(3, P_LEVEL, 86); prm(3, P_LOWCUT, 86); prm(3, P_PAN, -14);
    /* 5 SYNTH: a saw bass */
    prm(4, SRC(SY_SHAPE), 86); prm(4, SRC(SY_SUB), 50);
    prm(4, P_CUT, 50); prm(4, P_RES, 50); prm(4, P_FENV, 30); prm(4, P_FDEC, 30); prm(4, P_ADEC, 40); prm(4, P_ASUS, 30);
    prm(4, P_AREL, 12); prm(4, P_ODRV, 20); prm(4, P_DUCK, 60); prm(4, P_LEVEL, 96);
    /* 6 SYNTH: a minor 7 stab into the delay */
    prm(5, SRC(SY_SHAPE), 90); prm(5, SRC(SY_DETUNE), 20); prm(5, SRC(SY_CHORD), 1);
    prm(5, P_CUT, 80); prm(5, P_ADEC, 34); prm(5, P_ASUS, 0); prm(5, P_AREL, 18); prm(5, P_DLY, 60); prm(5, P_REV, 40);
    /* 7 SNARE: band-passed noise, snap */
    td_set_machine_x(P, 6, TD_M_SNARE);
    prm(6, SRC(SN_TUNE), 8); prm(6, SRC(SN_DECAY), 34); prm(6, SRC(SN_NOISE), 96); prm(6, SRC(SN_NDECAY), 50);
    prm(6, SRC(SN_SNAP), 86); prm(6, SRC(SN_BODY), 50); prm(6, SRC(SN_TONE), 86); prm(6, SRC(SN_DRIVE), 20);
    prm(6, P_LEVEL, 100); prm(6, P_REV, 30);
    /* 8 SAMPLE: the vocal blip */
    prm(7, SRC(SM_SLOT), 1); prm(7, P_LEVEL, 90); prm(7, P_DLY, 60); prm(7, P_REV, 50);
    scene(0, 4, P_CUT, 24); scene(0, 2, P_REV, 110);
    scene(1, 4, P_CUT, 96); scene(1, 4, P_RES, 90); scene(1, 0, SRC(KK_DRIVE), 90);
}

static void kit_keys(void)
{
    /* 1 KICK: soft */
    prm(0, SRC(KK_DECAY), 60); prm(0, SRC(KK_SWEEP), 20); prm(0, SRC(KK_CLICK), 14); prm(0, SRC(KK_DRIVE), 0);
    prm(0, SRC(KK_TONE), 50); prm(0, P_LEVEL, 104);
    /* 2 HAT, open: soft */
    prm(1, SRC(HH_DECAY), 40); prm(1, SRC(HH_OPEN), 50); prm(1, SRC(HH_BRIGHT), 60); prm(1, P_LEVEL, 80); prm(1, P_LOWCUT, 80);
    /* 3 SNARE: a rim */
    td_set_machine_x(P, 2, TD_M_SNARE);
    prm(2, SRC(SN_TUNE), 20); prm(2, SRC(SN_DECAY), 14); prm(2, SRC(SN_NOISE), 30); prm(2, SRC(SN_NDECAY), 14);
    prm(2, SRC(SN_SNAP), 90); prm(2, SRC(SN_BODY), 40); prm(2, P_LEVEL, 90); prm(2, P_REV, 40);
    /* 4 HAT, closed: soft */
    prm(3, SRC(HH_DECAY), 10); prm(3, SRC(HH_BRIGHT), 60); prm(3, P_LEVEL, 78); prm(3, P_LOWCUT, 84);
    /* 5 SYNTH: a round bass */
    prm(4, SRC(SY_SHAPE), 20); prm(4, SRC(SY_SUB), 60); prm(4, P_CUT, 60); prm(4, P_RES, 10); prm(4, P_ADEC, 70);
    prm(4, P_ASUS, 50); prm(4, P_AREL, 20); prm(4, P_LEVEL, 100);
    /* 6 FM: the electric piano (ratio 1, the index dying fast: the tine) */
    td_set_machine_x(P, 5, TD_M_FM);
    prm(5, SRC(FM_RATIO), 1); prm(5, SRC(FM_INDEX), 34); prm(5, SRC(FM_FDBK), 6); prm(5, SRC(FM_ENVDEC), 26);
    prm(5, SRC(FM_ENVAMT), 80); prm(5, P_AATK, 0); prm(5, P_ADEC, 80); prm(5, P_ASUS, 40); prm(5, P_AREL, 50);
    prm(5, P_CHO, 50); prm(5, P_REV, 40); prm(5, P_LEVEL, 100); prm(5, P_LSPD, 30); prm(5, P_LWAVE, 0);
    prm(5, P_LDEST, 22); prm(5, P_LDEPTH, 16);                 /* (a slow auto-pan, the suitcase's tremolo) */
    /* 7 FM: the same piano, minor 7 chords, into the delay */
    td_set_machine_x(P, 6, TD_M_FM);
    prm(6, SRC(FM_RATIO), 1); prm(6, SRC(FM_INDEX), 30); prm(6, SRC(FM_FDBK), 6); prm(6, SRC(FM_ENVDEC), 26);
    prm(6, SRC(FM_ENVAMT), 80); prm(6, SRC(FM_CHORD), 1); prm(6, P_ADEC, 80); prm(6, P_ASUS, 40); prm(6, P_AREL, 50);
    prm(6, P_CHO, 50); prm(6, P_DLY, 30); prm(6, P_REV, 50); prm(6, P_LEVEL, 92);
    /* 8 SYNTH: a slow open-9 pad */
    td_set_machine_x(P, 7, TD_M_SYNTH);
    prm(7, SRC(SY_SHAPE), 60); prm(7, SRC(SY_DETUNE), 40); prm(7, SRC(SY_CHORD), 7);
    prm(7, P_AATK, 60); prm(7, P_ASUS, 100); prm(7, P_AREL, 80); prm(7, P_CUT, 50); prm(7, P_REV, 80); prm(7, P_CHO, 60);
    prm(7, P_LEVEL, 80);
    scene(0, 5, P_CUT, 40); scene(0, 7, P_CUT, 30);
    scene(1, 5, SRC(FM_INDEX), 80); scene(1, 6, P_DLY, 90);
}

static const struct { const char *name; void (*fill)(void); } PACKS[] = {
    {"808", kit_808}, {"909", kit_909}, {"KEYS", kit_keys},
    {"DEEP", pat_house}, {"TECH", pat_tech}, {"JACKIN", pat_jackin}, {"AFRO", pat_afro},
    {"TECHNO", pat_techno}, {"ACID", pat_acid}, {"DUB", pat_dub}, {"MELODIC", pat_melodic},
    {"2-STEP", pat_garage}, {"SPEED", pat_speed}, {"4X4", pat_4x4}, {"BASSLINE", pat_bassline},
    {"LIQUID", pat_liquid}, {"NEURO", pat_neuro}, {"JUNGLE", pat_jungle}, {"ROLLER", pat_roller},
    {"MINIMAL", pat_minimal}, {"FILTER", pat_filter}, {"HARD", pat_hard}, {"PROG", pat_prog},
    {"DISCO", pat_disco}, {"BREAKS", pat_breaks}, {"ELECTRO", pat_electro},
};
#define NPACKS ((int)(sizeof PACKS / sizeof PACKS[0]))

int td_pack_count(void) { return NPACKS; }
const char *td_pack_name(int pack) { return pack >= 0 && pack < NPACKS ? PACKS[pack].name : ""; }

/* the kit of a pattern: its machines, every page but TRIG, its scenes */
typedef struct {
    uint8_t machine[TD_TRACKS];
    int16_t param[TD_TRACKS][TD_NPARAMS];
    uint16_t nscene[2];
    td_scene_ent_t scene[2][TD_SCENE_MAX];
} td_kit_t;
static td_kit_t g_undo;
static int g_undo_ok = 0;
static td_pattern_t g_scratch;

static void kit_get(const td_pattern_t *p, td_kit_t *k)
{
    memcpy(k->machine, p->machine, sizeof k->machine);
    memcpy(k->param, p->param, sizeof k->param);
    memcpy(k->nscene, p->nscene, sizeof k->nscene);
    memcpy(k->scene, p->scene, sizeof k->scene);
}
static void kit_put(td_pattern_t *p, const td_kit_t *k)
{
    for (int t = 0; t < TD_TRACKS; t++) {
        if (p->machine[t] != k->machine[t])
            for (int i = 0; i < TD_VOICES; i++) td_voice_fade(&E.trk[t].v[i], 5.f);
        p->machine[t] = k->machine[t];
        for (int q = TD_SLOTS; q < TD_NPARAMS; q++) p->param[t][q] = k->param[t][q];   /* (the TRIG page stays) */
        E.trk[t].lk_mask = 0;
    }
    memcpy(p->nscene, k->nscene, sizeof p->nscene);
    memcpy(p->scene, k->scene, sizeof p->scene);
}

void td_pack_load(int pack)
{
    if (pack < 0 || pack >= NPACKS) return;
    static td_kit_t k;
    P = &g_scratch;                                            /* (built aside, outside the lock) */
    td_pattern_init_kit(P);
    PACKS[pack].fill();
    kit_get(P, &k);
    td_lock();
    if (E.inited) {
        kit_get(&E.pat[E.cur], &g_undo);
        g_undo_ok = 1;
        kit_put(&E.pat[E.cur], &k);
    }
    td_unlock();
}

int td_pack_revert(void)
{
    int r = -1;
    td_lock();
    if (E.inited && g_undo_ok) {
        static td_kit_t now;
        kit_get(&E.pat[E.cur], &now);
        kit_put(&E.pat[E.cur], &g_undo);
        g_undo = now;                                          /* (revert again: back to the pack) */
        r = 0;
    }
    td_unlock();
    return r;
}
