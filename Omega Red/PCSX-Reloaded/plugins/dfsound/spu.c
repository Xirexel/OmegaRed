/***************************************************************************
                            spu.c  -  description
                             -------------------
    begin                : Wed May 15 2002
    copyright            : (C) 2002 by Pete Bernert
    email                : BlackDove@addcom.de
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version. See also the license.txt file for *
 *   additional informations.                                              *
 *                                                                         *
 ***************************************************************************/

//*************************************************************************//
// History of changes:
//
// 2003/04/07 - Eric
// - adjusted cubic interpolation algorithm
//
// 2003/03/16 - Eric
// - added cubic interpolation
//
// 2003/03/01 - linuzappz
// - libraryName changes using ALSA
//
// 2003/02/28 - Pete
// - added option for type of interpolation
// - adjusted spu irqs again (Thousant Arms, Valkyrie Profile)
// - added MONO support for MSWindows DirectSound
//
// 2003/02/20 - kode54
// - amended interpolation code, goto GOON could skip initialization of gpos and cause segfault
//
// 2003/02/19 - kode54
// - moved SPU IRQ handler and changed sample flag processing
//
// 2003/02/18 - kode54
// - moved ADSR calculation outside of the sample decode loop, somehow I doubt that
//   ADSR timing is relative to the frequency at which a sample is played... I guess
//   this remains to be seen, and I don't know whether ADSR is applied to noise channels...
//
// 2003/02/09 - kode54
// - one-shot samples now process the end block before stopping
// - in light of removing fmod hack, now processing ADSR on frequency channel as well
//
// 2003/02/08 - kode54
// - replaced easy interpolation with gaussian
// - removed fmod averaging hack
// - changed .sinc to be updated from .iRawPitch, no idea why it wasn't done this way already (<- Pete: because I sometimes fail to see the obvious, haharhar :)
//
// 2003/02/08 - linuzappz
// - small bugfix for one usleep that was 1 instead of 1000
// - added iDisStereo for no stereo (Linux)
//
// 2003/01/22 - Pete
// - added easy interpolation & small noise adjustments
//
// 2003/01/19 - Pete
// - added Neill's reverb
//
// 2003/01/12 - Pete
// - added recording window handlers
//
// 2003/01/06 - Pete
// - added Neill's ADSR timings
//
// 2002/12/28 - Pete
// - adjusted spu irq handling, fmod handling and loop handling
//
// 2002/08/14 - Pete
// - added extra reverb
//
// 2002/06/08 - linuzappz
// - SPUupdate changed for SPUasync
//
// 2002/05/15 - Pete
// - generic cleanup for the Peops release
//
//*************************************************************************//

#include "stdafx.h"

#define _IN_SPU

#include "externals.h"
#include "cfg.h"
#include "dsoundoss.h"
#include "regs.h"
#include "debug.h"
#include "record.h"
#include "resource.h"

////////////////////////////////////////////////////////////////////////
// spu version infos/name
////////////////////////////////////////////////////////////////////////

const unsigned char version = 1;
const unsigned char revision = 0;
const unsigned char build = 0;
#ifdef _WINDOWS
static char *libraryName = "P.E.Op.S. DSound Audio Driver";
#else
#ifndef USEALSA
static char *libraryName = "OSS Sound";
#else
static char *libraryName = "ALSA Sound";
#endif
#endif
static char *libraryInfo = "P.E.Op.S. OSS Driver V1.7\nCoded by Pete Bernert and the P.E.Op.S. team\n";

////////////////////////////////////////////////////////////////////////
// globals
////////////////////////////////////////////////////////////////////////

// psx buffer / addresses

unsigned short regArea[10000];
unsigned short spuMem[256 * 1024];
unsigned char *spuMemC;
unsigned char *pSpuIrq = 0;
unsigned char *pSpuBuffer;

// user settings

int iUseXA = 1;
int iVolume = 3;
int iXAPitch = 1;
int iUseTimer = 2;
int iSPUIRQWait = 1;
int iDebugMode = 0;
int iRecordMode = 0;
int iUseReverb = 2;
int iUseInterpolation = 1;
int iDisStereo = 0;


int iLockCount = -1;

void clearAndBlock();

void unblock();

// MAIN infos struct for each channel

SPUCHAN s_chan[MAXCHAN + 1]; // channel + 1 infos (1 is security for fmod handling)
REVERBInfo rvb;

unsigned long dwNoiseVal = 1; // global noise generator
int iWatchDog = 0;

unsigned short spuCtrl = 0; // some vars to store psx reg infos
unsigned short spuStat = 0;
unsigned short spuIrq = 0;
unsigned long spuAddr = 0xffffffff; // address into spu mem
int bEndThread = 0;                 // thread handlers
int bThreadEnded = 0;
int bSpuInit = 0;
int bSPUIsOpen = 0;

#ifdef _WINDOWS
HWND hWMain = 0; // window handle
HWND hWDebug = 0;
HWND hWRecord = 0;
static HANDLE hMainThread;
#else
// 2003/06/07 - Pete
#ifndef NOTHREADLIB
static pthread_t thread = -1; // thread id (linux)
#endif
#endif

unsigned long dwNewChannel = 0; // flags for faster testing, if new channel starts

void(CALLBACK *irqCallback)(void) = 0; // func of main emu, called on spu irq
void(CALLBACK *cddavCallback)(unsigned short, unsigned short) = 0;

// certain globals (were local before, but with the new timeproc I need em global)

static const int f[5][2] = {{0, 0},
                            {60, 0},
                            {115, -52},
                            {98, -55},
                            {122, -60}};
int SSumR[NSSIZE];
int SSumL[NSSIZE];
int iCycle = 0;
short *pS;

static int lastch = -1;      // last channel processed on spu irq in timer mode
static int lastns = 0;       // last ns pos
static int iSecureStart = 0; // secure start counter

////////////////////////////////////////////////////////////////////////
// CODE AREA
////////////////////////////////////////////////////////////////////////

// dirty inline func includes

#include "reverb.c"
#include "adsr.c"

////////////////////////////////////////////////////////////////////////
// helpers for simple interpolation

//
// easy interpolation on upsampling, no special filter, just "Pete's common sense" tm
//
// instead of having n equal sample values in a row like:
//       ____
//           |____
//
// we compare the current delta change with the next delta change.
//
// if curr_delta is positive,
//
//  - and next delta is smaller (or changing direction):
//         \.
//          -__
//
//  - and next delta significant (at least twice) bigger:
//         --_
//            \.
//
//  - and next delta is nearly same:
//          \.
//           \.
//
//
// if curr_delta is negative,
//
//  - and next delta is smaller (or changing direction):
//          _--
//         /
//
//  - and next delta significant (at least twice) bigger:
//            /
//         __-
//
//  - and next delta is nearly same:
//           /
//          /
//


INLINE void InterpolateUp(int ch)
{
    if (s_chan[ch].SB[32] == 1) // flag == 1? calc step and set flag... and don't change the value in this pass
    {
        const int id1 = s_chan[ch].SB[30] - s_chan[ch].SB[29]; // curr delta to next val
        const int id2 = s_chan[ch].SB[31] - s_chan[ch].SB[30]; // and next delta to next-next val :)

        s_chan[ch].SB[32] = 0;

        if (id1 > 0) // curr delta positive
        {
            if (id2 < id1) {
                s_chan[ch].SB[28] = id1;
                s_chan[ch].SB[32] = 2;
            } else if (id2 < (id1 << 1))
                s_chan[ch].SB[28] = (id1 * s_chan[ch].sinc) / 0x10000L;
            else
                s_chan[ch].SB[28] = (id1 * s_chan[ch].sinc) / 0x20000L;
        } else // curr delta negative
        {
            if (id2 > id1) {
                s_chan[ch].SB[28] = id1;
                s_chan[ch].SB[32] = 2;
            } else if (id2 > (id1 << 1))
                s_chan[ch].SB[28] = (id1 * s_chan[ch].sinc) / 0x10000L;
            else
                s_chan[ch].SB[28] = (id1 * s_chan[ch].sinc) / 0x20000L;
        }
    } else if (s_chan[ch].SB[32] == 2) // flag 1: calc step and set flag... and don't change the value in this pass
    {
        s_chan[ch].SB[32] = 0;

        s_chan[ch].SB[28] = (s_chan[ch].SB[28] * s_chan[ch].sinc) / 0x20000L;
        if (s_chan[ch].sinc <= 0x8000)
            s_chan[ch].SB[29] = s_chan[ch].SB[30] - (s_chan[ch].SB[28] * ((0x10000 / s_chan[ch].sinc) - 1));
        else
            s_chan[ch].SB[29] += s_chan[ch].SB[28];
    } else // no flags? add bigger val (if possible), calc smaller step, set flag1
        s_chan[ch].SB[29] += s_chan[ch].SB[28];
}

//
// even easier interpolation on downsampling, also no special filter, again just "Pete's common sense" tm
//

INLINE void InterpolateDown(int ch)
{
    if (s_chan[ch].sinc >= 0x20000L) // we would skip at least one val?
    {
        s_chan[ch].SB[29] += (s_chan[ch].SB[30] - s_chan[ch].SB[29]) / 2;     // add easy weight
        if (s_chan[ch].sinc >= 0x30000L)                                      // we would skip even more vals?
            s_chan[ch].SB[29] += (s_chan[ch].SB[31] - s_chan[ch].SB[30]) / 2; // add additional next weight
    }
}

////////////////////////////////////////////////////////////////////////
// helpers for gauss interpolation

#define gval0 (((short *)(&s_chan[ch].SB[29]))[gpos])
#define gval(x) (((short *)(&s_chan[ch].SB[29]))[(gpos + x) & 3])

#include "gauss_i.h"

////////////////////////////////////////////////////////////////////////

#include "xa.c"

////////////////////////////////////////////////////////////////////////
// START SOUND... called by main thread to setup a new sound on a channel
////////////////////////////////////////////////////////////////////////

INLINE void StartSound(int ch)
{
    StartADSR(ch);
    StartREVERB(ch);

    s_chan[ch].pCurr = s_chan[ch].pStart; // set sample start

    s_chan[ch].s_1 = 0; // init mixing vars
    s_chan[ch].s_2 = 0;
    s_chan[ch].iSBPos = 28;

    s_chan[ch].bNew = 0; // init channel flags
    s_chan[ch].bStop = 0;
    s_chan[ch].bOn = 1;

    s_chan[ch].SB[29] = 0; // init our interpolation helpers
    s_chan[ch].SB[30] = 0;

    if (iUseInterpolation >= 2) // gauss interpolation?
    {
        s_chan[ch].spos = 0x30000L;
        s_chan[ch].SB[28] = 0;
    } // -> start with more decoding
    else {
        s_chan[ch].spos = 0x10000L;
        s_chan[ch].SB[31] = 0;
    } // -> no/simple interpolation starts with one 48000 decoding

    dwNewChannel &= ~(1 << ch); // clear new channel bit
}

////////////////////////////////////////////////////////////////////////
// MAIN SPU FUNCTION
// here is the main job handler... thread, timer or direct func call
// basically the whole sound processing is done in this fat func!
////////////////////////////////////////////////////////////////////////

// 5 ms waiting phase, if buffer is full and no new sound has to get started
// .. can be made smaller (smallest val: 1 ms), but bigger waits give
// better performance

#define PAUSE_W 5
#define PAUSE_L 5000

////////////////////////////////////////////////////////////////////////

#ifdef _WINDOWS
static VOID CALLBACK MAINProc(UINT nTimerId, UINT msg, DWORD dwUser, DWORD dwParam1, DWORD dwParam2)
#else
static void *MAINThread(void *arg)
#endif
{
    int s_1, s_2, fa, ns, voldiv = iVolume;
    unsigned char *start;
    unsigned int nSample;
    int ch, predict_nr, shift_factor, flags, d, s;
    int gpos, bIRQReturn = 0;

    while (!bEndThread) // until we are shutting down
    {
        //--------------------------------------------------//
        // ok, at the beginning we are looking if there is
        // enuff free place in the dsound/oss buffer to
        // fill in new data, or if there is a new channel to start.
        // if not, we wait (thread) or return (timer/spuasync)
        // until enuff free place is available/a new channel gets
        // started

        if (dwNewChannel)   // new channel should start immedately?
        {                   // (at least one bit 0 ... MAXCHANNEL is set?)
            iSecureStart++; // -> set iSecure
            if (iSecureStart > 5)
                iSecureStart = 0; //    (if it is set 5 times - that means on 5 tries a new samples has been started - in a row, we will reset it, to give the sound update a chance)
        } else
            iSecureStart = 0; // 0: no new channel should start

        while (!iSecureStart && !bEndThread &&       // no new start? no thread end?
               (SoundGetBytesBuffered() > TESTSIZE)) // and still enuff data in sound buffer?
        {
            iSecureStart = 0; // reset secure

#ifdef _WINDOWS
            if (iUseTimer) // no-thread mode?
            {
                if (iUseTimer == 1) // -> ok, timer mode 1: setup a oneshot timer of x ms to wait
                    timeSetEvent(PAUSE_W, 1, MAINProc, 0, TIME_ONESHOT);
                return; // -> and done this time (timer mode 1 or 2)
            }
            // win thread mode:
            Sleep(PAUSE_W); // sleep for x ms (win)
#else
            if (iUseTimer)
                return 0;    // linux no-thread mode? bye
            usleep(PAUSE_L); // else sleep for x ms (linux)
#endif

            if (dwNewChannel)
                iSecureStart = 1; // if a new channel kicks in (or, of course, sound buffer runs low), we will leave the loop
        }

        //--------------------------------------------------// continue from irq handling in timer mode?

        if (lastch >= 0) // will be -1 if no continue is pending
        {
            ch = lastch;
            ns = lastns;
            lastch = -1; // -> setup all kind of vars to continue
            goto GOON;   // -> directly jump to the continue point
        }

        //--------------------------------------------------//
        //- main channel loop                              -//
        //--------------------------------------------------//
        {
            for (ch = 0; ch < MAXCHAN; ch++) // loop em all... we will collect 1 ms of sound of each playing channel
            {
                if (s_chan[ch].bNew)
                    StartSound(ch); // start new sound
                if (!s_chan[ch].bOn)
                    continue; // channel not playing? next

                if (s_chan[ch].iActFreq != s_chan[ch].iUsedFreq) // new psx frequency?
                {
                    s_chan[ch].iUsedFreq = s_chan[ch].iActFreq; // -> take it and calc steps
                    s_chan[ch].sinc = s_chan[ch].iRawPitch << 4;
                    if (!s_chan[ch].sinc)
                        s_chan[ch].sinc = 1;
                    if (iUseInterpolation == 1)
                        s_chan[ch].SB[32] = 1; // -> freq change in simle imterpolation mode: set flag
                }

                ns = 0;
                while (ns < NSSIZE) // loop until 1 ms of data is reached
                {
                    while (s_chan[ch].spos >= 0x10000L) {
                        if (s_chan[ch].iSBPos == 28) // 28 reached?
                        {
                            start = s_chan[ch].pCurr; // set up the current pos

                            if (start == (unsigned char *)-1) // special "stop" sign
                            {
                                s_chan[ch].bOn = 0; // -> turn everything off
                                s_chan[ch].ADSRX.lVolume = 0;
                                s_chan[ch].ADSRX.EnvelopeVol = 0;
                                goto ENDX; // -> and done for this channel
                            }

                            s_chan[ch].iSBPos = 0;

                            //////////////////////////////////////////// spu irq handler here? mmm... do it later

                            s_1 = s_chan[ch].s_1;
                            s_2 = s_chan[ch].s_2;

                            predict_nr = (int)*start;
                            start++;
                            shift_factor = predict_nr & 0xf;
                            predict_nr >>= 4;
                            flags = (int)*start;
                            start++;

                            // -------------------------------------- //

                            for (nSample = 0; nSample < 28; start++) {
                                d = (int)*start;
                                s = ((d & 0xf) << 12);
                                if (s & 0x8000)
                                    s |= 0xffff0000;

                                fa = (s >> shift_factor);
                                fa = fa + ((s_1 * f[predict_nr][0]) >> 6) + ((s_2 * f[predict_nr][1]) >> 6);
                                s_2 = s_1;
                                s_1 = fa;
                                s = ((d & 0xf0) << 8);

                                s_chan[ch].SB[nSample++] = fa;

                                if (s & 0x8000)
                                    s |= 0xffff0000;
                                fa = (s >> shift_factor);
                                fa = fa + ((s_1 * f[predict_nr][0]) >> 6) + ((s_2 * f[predict_nr][1]) >> 6);
                                s_2 = s_1;
                                s_1 = fa;

                                s_chan[ch].SB[nSample++] = fa;
                            }

                            //////////////////////////////////////////// irq check

                            if (irqCallback && (spuCtrl & 0x40)) // some callback and irq active?
                            {
                                if ((pSpuIrq > start - 16 && // irq address reached?
                                     pSpuIrq <= start) ||
                                    ((flags & 1) && // special: irq on looping addr, when stop/loop flag is set
                                     (pSpuIrq > s_chan[ch].pLoop - 16 &&
                                      pSpuIrq <= s_chan[ch].pLoop))) {
                                    s_chan[ch].iIrqDone = 1; // -> debug flag
                                    irqCallback();           // -> call main emu

                                    if (iSPUIRQWait) // -> option: wait after irq for main emu
                                    {
                                        DWORD dwWatchTime;

                                        if (iUseTimer == 2) // -> special timer mode... give main emu the control
                                        {
                                            bIRQReturn = 1;
                                        } else {
                                            dwWatchTime = timeGetTime_spu() + 2500;
                                            iWatchDog = 1; // -> should we do some mutex stuff? ahh, naaa

                                            while (iWatchDog && !bEndThread &&
                                                   timeGetTime_spu() < dwWatchTime)
#ifdef _WINDOWS
                                            {
                                                if (iLockCount != -1)
                                                    if (iLockCount > 0) {
                                                        --iLockCount;
                                                        unblock();
                                                    } else {
                                                        clearAndBlock();
                                                    }

                                                Sleep(1);
											}												
#else
                                                usleep(1000L);
#endif
                                        }
                                    }
                                }
                            }

                            //////////////////////////////////////////// flag handler

                            if ((flags & 4) && (!s_chan[ch].bIgnoreLoop))
                                s_chan[ch].pLoop = start - 16; // loop adress

                            if (flags & 1) // 1: stop/loop
                            {
                                // We play this block out first...
                                //if(!(flags&2))                          // 1+2: do loop... otherwise: stop
                                if (flags != 3 || s_chan[ch].pLoop == NULL) // PETE: if we don't check exactly for 3, loop hang ups will happen (DQ4, for example)
                                {                                           // and checking if pLoop is set avoids crashes, yeah
                                    start = (unsigned char *)-1;
                                } else {
                                    start = s_chan[ch].pLoop;
                                }
                            }

                            s_chan[ch].pCurr = start; // store values for next cycle
                            s_chan[ch].s_1 = s_1;
                            s_chan[ch].s_2 = s_2;

                            ////////////////////////////////////////////

                            if (bIRQReturn) // special return for "spu irq - wait for cpu action"
                            {
                                bIRQReturn = 0;
                                lastch = ch;
                                lastns = ns;
#ifdef _WINDOWS
                                return;
#else
                                return 0;
#endif
                            }

                            ////////////////////////////////////////////

                        GOON:;
                        }

                        fa = s_chan[ch].SB[s_chan[ch].iSBPos++]; // get sample data

                        if ((spuCtrl & 0x4000) == 0)
                            fa = 0; // muted?
                        else        // else adjust
                        {
                            if (fa > 32767L)
                                fa = 32767L;
                            if (fa < -32767L)
                                fa = -32767L;
                        }

                        if (iUseInterpolation >= 2) // gauss/cubic interpolation
                        {
                            gpos = s_chan[ch].SB[28];
                            gval0 = fa;
                            gpos = (gpos + 1) & 3;
                            s_chan[ch].SB[28] = gpos;
                        } else if (iUseInterpolation == 1) // simple interpolation
                        {
                            s_chan[ch].SB[28] = 0;
                            s_chan[ch].SB[29] = s_chan[ch].SB[30]; // -> helpers for simple linear interpolation: delay real val for two slots, and calc the two deltas, for a 'look at the future behaviour'
                            s_chan[ch].SB[30] = s_chan[ch].SB[31];
                            s_chan[ch].SB[31] = fa;
                            s_chan[ch].SB[32] = 1; // -> flag: calc new interolation
                        } else
                            s_chan[ch].SB[29] = fa; // no interpolation

                        s_chan[ch].spos -= 0x10000L;
                    }

                    ////////////////////////////////////////////////
                    // noise handler... just produces some noise data
                    // surely wrong... and no noise frequency (spuCtrl&0x3f00) will be used...
                    // and sometimes the noise will be used as fmod modulation... pfff

                    if (s_chan[ch].bNoise) {
                        if ((dwNoiseVal <<= 1) & 0x80000000L) {
                            dwNoiseVal ^= 0x0040001L;
                            fa = ((dwNoiseVal >> 2) & 0x7fff);
                            fa = -fa;
                        } else
                            fa = (dwNoiseVal >> 2) & 0x7fff;

                        // mmm... depending on the noise freq we allow bigger/smaller changes to the previous val
                        fa = s_chan[ch].iOldNoise + ((fa - s_chan[ch].iOldNoise) / ((0x001f - ((spuCtrl & 0x3f00) >> 9)) + 1));
                        if (fa > 32767L)
                            fa = 32767L;
                        if (fa < -32767L)
                            fa = -32767L;
                        s_chan[ch].iOldNoise = fa;

                        if (iUseInterpolation < 2)  // no gauss/cubic interpolation?
                            s_chan[ch].SB[29] = fa; // -> store noise val in "current sample" slot
                    }                               //----------------------------------------
                    else                            // NO NOISE (NORMAL SAMPLE DATA) HERE
                    {                               //------------------------------------------//
                        if (iUseInterpolation == 3) // cubic interpolation
                        {
                            long xd;
                            xd = ((s_chan[ch].spos) >> 1) + 1;
                            gpos = s_chan[ch].SB[28];

                            fa = gval(3) - 3 * gval(2) + 3 * gval(1) - gval0;
                            fa *= (xd - (2 << 15)) / 6;
                            fa >>= 15;
                            fa += gval(2) - gval(1) - gval(1) + gval0;
                            fa *= (xd - (1 << 15)) >> 1;
                            fa >>= 15;
                            fa += gval(1) - gval0;
                            fa *= xd;
                            fa >>= 15;
                            fa = fa + gval0;
                        }
                        //------------------------------------------//
                        else if (iUseInterpolation == 2) // gauss interpolation
                        {
                            int vl, vr;
                            vl = (s_chan[ch].spos >> 6) & ~3;
                            gpos = s_chan[ch].SB[28];
                            vr = (gauss[vl] * gval0) & ~2047;
                            vr += (gauss[vl + 1] * gval(1)) & ~2047;
                            vr += (gauss[vl + 2] * gval(2)) & ~2047;
                            vr += (gauss[vl + 3] * gval(3)) & ~2047;
                            fa = vr >> 11;
                            /*
             vr=(gauss[vl]*gval0)>>9;
             vr+=(gauss[vl+1]*gval(1))>>9;
             vr+=(gauss[vl+2]*gval(2))>>9;
             vr+=(gauss[vl+3]*gval(3))>>9;
             fa = vr>>2;
*/
                        }
                        //------------------------------------------//
                        else if (iUseInterpolation == 1) // simple interpolation
                        {
                            if (s_chan[ch].sinc < 0x10000L) // -> upsampling?
                                InterpolateUp(ch);          // --> interpolate up
                            else
                                InterpolateDown(ch); // --> else down
                            fa = s_chan[ch].SB[29];
                        }
                        //------------------------------------------//
                        else
                            fa = s_chan[ch].SB[29]; // no interpolation
                    }

                    s_chan[ch].sval = (MixADSR(ch) * fa) / 1023; // add adsr

                    if (s_chan[ch].bFMod == 2) // fmod freq channel
                    {
                        int NP = s_chan[ch + 1].iRawPitch;

                        NP = ((32768L + s_chan[ch].sval) * NP) / 32768L;

                        if (NP > 0x3fff)
                            NP = 0x3fff;
                        if (NP < 0x1)
                            NP = 0x1;

                        // mmmm... if I do this, all is screwed
                        //           s_chan[ch+1].iRawPitch=NP;

                        NP = (48000L * NP) / (4096L); // calc frequency

                        s_chan[ch + 1].iActFreq = NP;
                        s_chan[ch + 1].iUsedFreq = NP;
                        s_chan[ch + 1].sinc = (((NP / 10) << 16) / 4800);
                        if (!s_chan[ch + 1].sinc)
                            s_chan[ch + 1].sinc = 1;
                        if (iUseInterpolation == 1) // freq change in sipmle interpolation mode
                            s_chan[ch + 1].SB[32] = 1;

                        // mmmm... set up freq decoding positions?
                        //           s_chan[ch+1].iSBPos=28;
                        //           s_chan[ch+1].spos=0x10000L;
                    } else {
                        //////////////////////////////////////////////
                        // ok, left/right sound volume (psx volume goes from 0 ... 0x3fff)

                        if (s_chan[ch].iMute)
                            s_chan[ch].sval = 0; // debug mute
                        else {
                            SSumL[ns] += (s_chan[ch].sval * s_chan[ch].iLeftVolume) / 0x4000L;
                            SSumR[ns] += (s_chan[ch].sval * s_chan[ch].iRightVolume) / 0x4000L;
                        }

                        //////////////////////////////////////////////
                        // now let us store sound data for reverb

                        if (s_chan[ch].bRVBActive)
                            StoreREVERB(ch, ns);
                    }

                    ////////////////////////////////////////////////
                    // ok, go on until 1 ms data of this channel is collected

                    ns++;
                    s_chan[ch].spos += s_chan[ch].sinc;
                }
            ENDX:;
            }
        }

        //---------------------------------------------------//
        //- here we have another 1 ms of sound data
        //---------------------------------------------------//
        // mix XA infos (if any)

        if (XAPlay != XAFeed || XARepeat)
            MixXA();

        ///////////////////////////////////////////////////////
        // mix all channels (including reverb) into one buffer

        if (iDisStereo) // no stereo?
        {
            int dl, dr;
            for (ns = 0; ns < NSSIZE; ns++) {
                SSumL[ns] += MixREVERBLeft(ns);

                dl = SSumL[ns] / voldiv;
                SSumL[ns] = 0;
                if (dl < -32767)
                    dl = -32767;
                if (dl > 32767)
                    dl = 32767;

                SSumR[ns] += MixREVERBRight();

                dr = SSumR[ns] / voldiv;
                SSumR[ns] = 0;
                if (dr < -32767)
                    dr = -32767;
                if (dr > 32767)
                    dr = 32767;
                *pS++ = (dl + dr) / 2;
            }
        } else // stereo:
            for (ns = 0; ns < NSSIZE; ns++) {
                SSumL[ns] += MixREVERBLeft(ns);

                d = SSumL[ns] / voldiv;
                SSumL[ns] = 0;
                if (d < -32767)
                    d = -32767;
                if (d > 32767)
                    d = 32767;
                *pS++ = d;

                SSumR[ns] += MixREVERBRight();

                d = SSumR[ns] / voldiv;
                SSumR[ns] = 0;
                if (d < -32767)
                    d = -32767;
                if (d > 32767)
                    d = 32767;
                *pS++ = d;
            }

        InitREVERB();

        //////////////////////////////////////////////////////
        // feed the sound
        // wanna have around 1/60 sec (16.666 ms) updates

        if (iCycle++ > 8) //temp fix: lowered for low latency alsa configs
        {
            SoundFeedStreamData((unsigned char *)pSpuBuffer,
                                ((unsigned char *)pS) -
                                    ((unsigned char *)pSpuBuffer));
            pS = (short *)pSpuBuffer;
            iCycle = 0;
        }
    }

    // end of big main loop...

    bThreadEnded = 1;

#ifndef _WINDOWS
    return 0;
#endif
}

////////////////////////////////////////////////////////////////////////
// WINDOWS THREAD... simply calls the timer func and stays forever :)
////////////////////////////////////////////////////////////////////////

#ifdef _WINDOWS

DWORD WINAPI MAINThreadEx(LPVOID lpParameter)
{
    MAINProc(0, 0, 0, 0, 0);
    return 0;
}

#endif

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////
// SPU ASYNC... even newer epsxe func
//  1 time every 'cycle' cycles... harhar
////////////////////////////////////////////////////////////////////////

void CALLBACK SPUasync(unsigned long cycle)
{
    iLockCount = 200;//20;
    iWatchDog = 0; // clear the watchdog

#ifdef _WINDOWS
    if (iDebugMode == 2) {
        if (IsWindow(hWDebug))
            DestroyWindow(hWDebug);
        hWDebug = 0;
        iDebugMode = 0;
    }
    if (iRecordMode == 2) {
        if (IsWindow(hWRecord))
            DestroyWindow(hWRecord);
        hWRecord = 0;
        iRecordMode = 0;
    }
#endif

    if (iUseTimer == 2) // special mode, only used in Linux by this spu (or if you enable the experimental Windows mode)
    {
        if (!bSpuInit)
            return; // -> no init, no call

#ifdef _WINDOWS
        MAINProc(0, 0, 0, 0, 0); // -> experimental win mode... not really tested... don't like the drawbacks
#else
        MAINThread(0); // -> linux high-compat mode
#endif
    }
}

////////////////////////////////////////////////////////////////////////
// SPU UPDATE... new epsxe func
//  1 time every 32 hsync lines
//  (312/32)x50 in pal
//  (262/32)x60 in ntsc
////////////////////////////////////////////////////////////////////////

#ifndef _WINDOWS

// since epsxe 1.5.2 (linux) uses SPUupdate, not SPUasync, I will
// leave that func in the linux port, until epsxe linux is using
// the async function as well

void CALLBACK SPUupdate(void)
{
    SPUasync(0);
}

#endif

////////////////////////////////////////////////////////////////////////
// XA AUDIO
////////////////////////////////////////////////////////////////////////

void CALLBACK SPUplayADPCMchannel(xa_decode_t *xap)
{
    if (!iUseXA)
        return; // no XA? bye
    if (!xap)
        return;
    if (!xap->freq)
        return; // no xa freq ? bye

    FeedXA(xap); // call main XA feeder
}

////////////////////////////////////////////////////////////////////////
// INIT/EXIT STUFF
////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////
// SPUINIT: this func will be called first by the main emu
////////////////////////////////////////////////////////////////////////

long CALLBACK SPUinit(void)
{
    spuMemC = (unsigned char *)spuMem; // just small setup
    memset((void *)s_chan, 0, MAXCHAN * sizeof(SPUCHAN));
    memset((void *)&rvb, 0, sizeof(REVERBInfo));
    InitADSR();
    return 0;
}

////////////////////////////////////////////////////////////////////////
// SETUPTIMER: init of certain buffers and threads/timers
////////////////////////////////////////////////////////////////////////

void SetupTimer(void)
{
    memset(SSumR, 0, NSSIZE * sizeof(int)); // init some mixing buffers
    memset(SSumL, 0, NSSIZE * sizeof(int));
    pS = (short *)pSpuBuffer; // setup soundbuffer pointer

    bEndThread = 0; // init thread vars
    bThreadEnded = 0;
    bSpuInit = 1; // flag: we are inited

#ifdef _WINDOWS

    if (iUseTimer == 1) // windows: use timer
    {
        timeBeginPeriod(1);
        timeSetEvent(1, 1, MAINProc, 0, TIME_ONESHOT);
    } else if (iUseTimer == 0) // windows: use thread
    {
        //_beginthread(MAINThread,0,NULL);
        DWORD dw;
        hMainThread = CreateThread(NULL, 0, MAINThreadEx, 0, 0, &dw);
        SetThreadPriority(hMainThread,
                          //THREAD_PRIORITY_TIME_CRITICAL);
                          THREAD_PRIORITY_HIGHEST);
    }

#else

#ifndef NOTHREADLIB
    if (!iUseTimer)    // linux: use thread
    {
        pthread_create(&thread, NULL, MAINThread, NULL);
    }
#endif

#endif
}

////////////////////////////////////////////////////////////////////////
// REMOVETIMER: kill threads/timers
////////////////////////////////////////////////////////////////////////

void RemoveTimer(void)
{
    bEndThread = 1; // raise flag to end thread

#ifdef _WINDOWS

    if (iUseTimer != 2) // windows thread?
    {
        while (!bThreadEnded) {
            Sleep(5L);
        } // -> wait till thread has ended
        Sleep(5L);
    }
    if (iUseTimer == 1)
        timeEndPeriod(1); // windows timer? stop it

#else

#ifndef NOTHREADLIB
    if (!iUseTimer) // linux tread?
    {
        int i = 0;
        while (!bThreadEnded && i < 2000) {
            usleep(1000L);
            i++;
        } // -> wait until thread has ended
        if (thread != -1) {
            pthread_cancel(thread);
            thread = -1;
        } // -> cancel thread anyway
    }
#endif

#endif

    bThreadEnded = 0; // no more spu is running
    bSpuInit = 0;
}

////////////////////////////////////////////////////////////////////////
// SETUPSTREAMS: init most of the spu buffers
////////////////////////////////////////////////////////////////////////

void SetupStreams(void)
{
    int i;

    pSpuBuffer = (unsigned char *)malloc(32768); // alloc mixing buffer

    if (iUseReverb == 1)
        i = 88200 * 2;
    else
        i = NSSIZE * 2;

    sRVBStart = (int *)malloc(i * 4); // alloc reverb buffer
    memset(sRVBStart, 0, i * 4);
    sRVBEnd = sRVBStart + i;
    sRVBPlay = sRVBStart;

    XAStart = // alloc xa buffer
        (unsigned long *)malloc(48000 * 4);
    XAPlay = XAStart;
    XAFeed = XAStart;
    XAEnd = XAStart + 48000;

    for (i = 0; i < MAXCHAN; i++) // loop sound channels
    {
        // we don't use mutex sync... not needed, would only
        // slow us down:
        //   s_chan[i].hMutex=CreateMutex(NULL,FALSE,NULL);
        s_chan[i].ADSRX.SustainLevel = 1024; // -> init sustain
        s_chan[i].iMute = 0;
        s_chan[i].iIrqDone = 0;
        s_chan[i].pLoop = spuMemC;
        s_chan[i].pStart = spuMemC;
        s_chan[i].pCurr = spuMemC;
    }
}

////////////////////////////////////////////////////////////////////////
// REMOVESTREAMS: free most buffer
////////////////////////////////////////////////////////////////////////

void RemoveStreams(void)
{
    free(pSpuBuffer); // free mixing buffer
    pSpuBuffer = NULL;
    free(sRVBStart); // free reverb buffer
    sRVBStart = 0;
    free(XAStart); // free XA buffer
    XAStart = 0;

    /*
 int i;
 for(i=0;i<MAXCHAN;i++)
  {
   WaitForSingleObject(s_chan[i].hMutex,2000);
   ReleaseMutex(s_chan[i].hMutex);
   if(s_chan[i].hMutex)    
    {CloseHandle(s_chan[i].hMutex);s_chan[i].hMutex=0;}
  }
*/
}


////////////////////////////////////////////////////////////////////////
// SPUOPEN: called by main emu after init
////////////////////////////////////////////////////////////////////////

#ifdef _WINDOWS
long CALLBACK SPUopen(HWND hW)
#else
long SPUopen(void)
#endif
{
    if (bSPUIsOpen)
        return 0; // security for some stupid main emus

    iUseXA = 1; // just small setup
    iVolume = 3;
    iReverbOff = -1;
    spuIrq = 0;
    spuAddr = 0xffffffff;
    bEndThread = 0;
    bThreadEnded = 0;
    spuMemC = (unsigned char *)spuMem;
    memset((void *)s_chan, 0, (MAXCHAN + 1) * sizeof(SPUCHAN));
    pSpuIrq = 0;
    iSPUIRQWait = 1;

#ifdef _WINDOWS
    LastWrite = 0xffffffff;
    LastPlay = 0; // init some play vars
    if (!IsWindow(hW))
        hW = GetActiveWindow();
    hWMain = hW; // store hwnd
#endif

    ReadConfig(); // read user stuff

    SetupSound(); // setup midas (before init!)

    SetupStreams(); // prepare streaming

    SetupTimer(); // timer for feeding data

    bSPUIsOpen = 1;

#ifdef _WINDOWS
    if (iDebugMode) // windows debug dialog
    {
        hWDebug = CreateDialog(hInst, MAKEINTRESOURCE(IDD_DEBUG),
                               NULL, (DLGPROC)DebugDlgProc);
        SetWindowPos(hWDebug, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
        UpdateWindow(hWDebug);
        SetFocus(hWMain);
    }
#endif

    return PSE_SPU_ERR_SUCCESS;
}

////////////////////////////////////////////////////////////////////////

#ifndef _WINDOWS
void SPUsetConfigFile(char *pCfg)
{
    pConfigFile = pCfg;
}
#endif

////////////////////////////////////////////////////////////////////////
// SPUCLOSE: called before shutdown
////////////////////////////////////////////////////////////////////////

long CALLBACK SPUclose(void)
{
    if (!bSPUIsOpen)
        return 0; // some security

    bSPUIsOpen = 0; // no more open

#ifdef _WINDOWS
    if (IsWindow(hWDebug))
        DestroyWindow(hWDebug);
    hWDebug = 0;
    if (IsWindow(hWRecord))
        DestroyWindow(hWRecord);
    hWRecord = 0;
#endif

    RemoveTimer(); // no more feeding

    RemoveSound(); // no more sound handling

    RemoveStreams(); // no more streaming

    return 0;
}

////////////////////////////////////////////////////////////////////////
// SPUSHUTDOWN: called by main emu on final exit
////////////////////////////////////////////////////////////////////////

long CALLBACK SPUshutdown(void)
{
    return 0;
}

////////////////////////////////////////////////////////////////////////
// SPUTEST: we don't test, we are always fine ;)
////////////////////////////////////////////////////////////////////////

long CALLBACK SPUtest(void)
{
    return 0;
}

////////////////////////////////////////////////////////////////////////
// SPUCONFIGURE: call config dialog
////////////////////////////////////////////////////////////////////////

long CALLBACK SPUconfigure(void)
{
#ifdef _WINDOWS
    DialogBox(hInst, MAKEINTRESOURCE(IDD_CFGDLG),
              GetActiveWindow(), (DLGPROC)DSoundDlgProc);
#else
    StartCfgTool("CFG");
#endif
    return 0;
}

////////////////////////////////////////////////////////////////////////
// SPUABOUT: show about window
////////////////////////////////////////////////////////////////////////

void CALLBACK SPUabout(void)
{
#ifdef _WINDOWS
    DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUT),
              GetActiveWindow(), (DLGPROC)AboutDlgProc);
#else
    StartCfgTool("ABOUT");
#endif
}

////////////////////////////////////////////////////////////////////////
// SETUP CALLBACKS
// this functions will be called once,
// passes a callback that should be called on SPU-IRQ/cdda volume change
////////////////////////////////////////////////////////////////////////

void CALLBACK SPUregisterCallback(void(CALLBACK *callback)(void))
{
    irqCallback = callback;
}

void CALLBACK SPUregisterCDDAVolume(void(CALLBACK *CDDAVcallback)(unsigned short, unsigned short))
{
    cddavCallback = CDDAVcallback;
}

////////////////////////////////////////////////////////////////////////
// COMMON PLUGIN INFO FUNCS
////////////////////////////////////////////////////////////////////////

char *CALLBACK PSEgetLibName(void)
{
    return libraryName;
}

unsigned long CALLBACK PSEgetLibType(void)
{
    return PSE_LT_SPU;
}

unsigned long CALLBACK PSEgetLibVersion(void)
{
    return version << 16 | revision << 8 | build;
}

char *SPUgetLibInfos(void)
{
    return libraryInfo;
}

////////////////////////////////////////////////////////////////////////
