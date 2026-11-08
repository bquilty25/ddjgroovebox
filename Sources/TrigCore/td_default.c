/* SPDX-License-Identifier: GPL-3.0-only */
/* TRIGDECK's default project: a few patterns to start from, 124 BPM, A minor (tempo and swing are the project's).
 *
 *   P01  HOUSE / TECHNO   the one TRIGDECK opens with (pat_house below)
 *   P02  its breakdown    no kick, no clap: for the mix in and out
 *   P03  DUB TECHNO       polymetric, after DUB-1's "4 AM in Berlin" (pat_dub)
 *   P04  ACID             a 303-style line with slides and accents over a 909-style beat (pat_acid)
 *   P05  ELECTRO          broken kicks, snare on 2 and 4, a square bass (pat_electro)
 *   P06..P16              empty, with P01's sounds
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
        }
    }
}

/* P01: house / techno, A minor. The four-on-the-floor beat, a rolling off-beat bass with accents and a slide, a
 * minor 9 chord stab into the delay, an FM pluck, a vocal blip every fourth bar. */
static void pat_house(void)
{
    four_on_the_floor();

    /* 5 SYNTH: the bass, off-beats and sixteenth pickups, ducked by the kick */
    prm(4, SRC(SY_SHAPE), 92); prm(4, SRC(SY_DETUNE), 6); prm(4, SRC(SY_SUB), 60); prm(4, SRC(SY_GLIDE), 20);
    prm(4, P_CUT, 52); prm(4, P_RES, 30); prm(4, P_FENV, 30); prm(4, P_FDEC, 30); prm(4, P_KEY, 64);
    prm(4, P_AATK, 0); prm(4, P_ADEC, 44); prm(4, P_ASUS, 40); prm(4, P_AREL, 14);
    prm(4, P_LEVEL, 92); prm(4, P_ODRV, 30); prm(4, P_DUCK, 70); prm(4, P_LEN, 3);
    trig(4, 2, 33, 104);                                       /* A1 */
    trig(4, 3, 45, 70);                                        /* A2 */
    trig(4, 6, 33, 100);
    trig(4, 7, 36, 74);                                        /* C2 */
    trig(4, 10, 33, 112)->accent = 1;
    trig(4, 11, 45, 72);
    trig(4, 14, 33, 100);
    trig(4, 15, 31, 80)->slide = 1;                            /* G1, sliding into the A */
    lock(4, 10, P_CUT, 70);

    /* 6 SYNTH: a minor 9 stab on the "and"s, into the delay */
    prm(5, SRC(SY_SHAPE), 86); prm(5, SRC(SY_DETUNE), 18); prm(5, SRC(SY_CHORD), 3);   /* MIN9 */
    prm(5, P_CUT, 74); prm(5, P_RES, 30); prm(5, P_FENV, 24); prm(5, P_FDEC, 26); prm(5, P_KEY, 32);
    prm(5, P_AATK, 0); prm(5, P_ADEC, 34); prm(5, P_ASUS, 0); prm(5, P_AREL, 18);
    prm(5, P_LEVEL, 104); prm(5, P_DLY, 70); prm(5, P_REV, 44); prm(5, P_CHO, 40); prm(5, P_WIDTH, 80);
    prm(5, P_DUCK, 40); prm(5, P_LOWCUT, 56); prm(5, P_LEN, 2);
    prm(5, P_LSPD, 12); prm(5, P_LMUL, 2); prm(5, P_LWAVE, 0); prm(5, P_LDEST, 9); prm(5, P_LDEPTH, 12);   /* CUTOFF */
    trig(5, 3, 57, 100);                                       /* Am9 */
    trig(5, 10, 57, 92);
    trig(5, 13, 62, 84)->cond = TD_COND_AB + 0;                /* 1:2: Dm9 */

    /* 7 FM: a pluck answering the stab */
    prm(6, SRC(FM_RATIO), 4); prm(6, SRC(FM_INDEX), 40); prm(6, SRC(FM_FDBK), 8); prm(6, SRC(FM_ENVDEC), 20);
    prm(6, SRC(FM_ENVAMT), 70);
    prm(6, P_CUT, 104); prm(6, P_ADEC, 30); prm(6, P_ASUS, 0); prm(6, P_AREL, 20);
    prm(6, P_LEVEL, 84); prm(6, P_PAN, 30); prm(6, P_DLY, 60); prm(6, P_REV, 40); prm(6, P_LOWCUT, 70);
    trig(6, 1, 76, 80);                                        /* E5 */
    trig(6, 9, 72, 70);                                        /* C5 */
    trig(6, 13, 69, 66)->cond = TD_COND_P1 + 2;                /* A4, 37 % */

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
        for (int s = 0; s < 16; s++) {
            if (!line[s][0]) continue;
            td_step_t *st = trig(4, s, line[s][0], line[s][1] ? 120 : 92);
            st->accent = line[s][1];
            st->slide = line[s][2];
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
    build(0, pat_house);
    E.pat[1] = E.pat[0];                                       /* P02: the breakdown (no kick, no clap) */
    memset(E.pat[1].step[0], 0, sizeof E.pat[1].step[0]);
    memset(E.pat[1].step[2], 0, sizeof E.pat[1].step[2]);
    build(2, pat_dub);
    build(3, pat_acid);
    build(4, pat_electro);
    for (int p = 5; p < TD_PATTERNS; p++) {                    /* empty, with P01's sounds */
        E.pat[p] = E.pat[0];
        memset(E.pat[p].step, 0, sizeof E.pat[p].step);
        memset(E.pat[p].lock, 0, sizeof E.pat[p].lock);
        for (int t = 0; t < TD_TRACKS; t++) E.pat[p].length[t] = 16;
    }

    E.bpm = 124.f;
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
    {"HOUSE", pat_house}, {"DUB", pat_dub}, {"ACID", pat_acid}, {"ELECTRO", pat_electro},
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
