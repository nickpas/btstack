/*
 * Copyright (C) 2016 BlueKitchen GmbH
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holders nor the names of
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 * 4. Any redistribution, use, or modification is done solely for
 *    personal benefit and not for any commercial purpose or for
 *    monetary gain.
 *
 * THIS SOFTWARE IS PROVIDED BY BLUEKITCHEN GMBH AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL MATTHIAS
 * RINGWALD OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Please inquire about commercial licensing options at 
 * contact@bluekitchen-gmbh.com
 *
 */

/*
 * btstack_sbc_plc.c
 *
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <math.h>

#include "btstack_cvsd_plc.h"

static float rcos[CVSD_OLAL] = {
    0.99148655,0.96623611,0.92510857,0.86950446,
    0.80131732,0.72286918,0.63683150,0.54613418, 
    0.45386582,0.36316850,0.27713082,0.19868268, 
    0.13049554,0.07489143,0.03376389,0.00851345};

static float CrossCorrelation(int8_t *x, int8_t *y){
    float num = 0;
    float den = 0;
    float x2 = 0;
    float y2 = 0;
    int   m;
    for (m=0;m<CVSD_M;m++){
        num+=((float)x[m])*y[m];
        x2+=((float)x[m])*x[m];
        y2+=((float)y[m])*y[m];
    }
    den = (float)sqrt(x2*y2);
    return num/den;
}

static int PatternMatch(int8_t *y){
    float maxCn = -999999.0;  /* large negative number */
    int   bestmatch = 0;
    float Cn;
    int   n;
    for (n=0;n<CVSD_N;n++){
        Cn = CrossCorrelation(&y[CVSD_LHIST-CVSD_M] /* x */, &y[n]); 
        if (Cn>maxCn){
            bestmatch=n;
            maxCn = Cn; 
        }
    }
    return bestmatch;
}

static float AmplitudeMatch(int8_t *y, int8_t bestmatch) {
    int   i;
    float sumx = 0;
    float sumy = 0.000001f;
    float sf;
    
    for (i=0;i<CVSD_FS;i++){
        sumx += abs(y[CVSD_LHIST-CVSD_FS+i]);
        sumy += abs(y[bestmatch+i]);
    }
    sf = sumx/sumy;
    /* This is not in the paper, but limit the scaling factor to something reasonable to avoid creating artifacts */
    if (sf<0.75f) sf=0.75f;
    if (sf>1.2f) sf=1.2f;
    return sf;
}

static int8_t crop_to_int8(float val){
    float croped_val = 0;
    if (val > 127.0)  croped_val= 127.0;
    if (val < -128.0) croped_val=-128.0; 
    return (int8_t) croped_val;
}

void btstack_cvsd_plc_init(btstack_cvsd_plc_state_t *plc_state){
    plc_state->nbf = 0;
    plc_state->bestlag = 0;
    memset(plc_state->hist, 0, sizeof(plc_state->hist));
}

void btstack_cvsd_plc_bad_frame(btstack_cvsd_plc_state_t *plc_state, int8_t *out){
    float val;
    float sf = 1;
    int   i = 0;

    plc_state->nbf++;

    if (plc_state->nbf==1){
        /* Perform pattern matching to find where to replicate */
        plc_state->bestlag = PatternMatch(plc_state->hist);

        /* the replication begins after the template match */
        plc_state->bestlag += CVSD_M; 
        
        /* Compute Scale Factor to Match Amplitude of Substitution Packet to that of Preceding Packet */
        sf = AmplitudeMatch(plc_state->hist, plc_state->bestlag);

        for (i=0;i<CVSD_OLAL;i++){
            float left = plc_state->hist[CVSD_LHIST-1];
            float right = sf*plc_state->hist[plc_state->bestlag+i];
            val = left * rcos[i] + right *rcos[CVSD_OLAL-1-i];
            plc_state->hist[CVSD_LHIST+i] = crop_to_int8(val);
        }
       
        for (i=CVSD_OLAL;i<CVSD_FS;i++){
            val = sf*plc_state->hist[plc_state->bestlag+i]; 
            plc_state->hist[CVSD_LHIST+i] = crop_to_int8(val);
        }
        
        for (i=CVSD_FS;i<CVSD_FS+CVSD_OLAL;i++){
            float left  = sf*plc_state->hist[plc_state->bestlag+i];
            float right = plc_state->hist[plc_state->bestlag+i];
            val = left * rcos[i-CVSD_FS] + right *rcos[CVSD_OLAL+CVSD_FS-1-i];
            plc_state->hist[CVSD_LHIST+i] = crop_to_int8(val);
        }

        for (i=CVSD_FS+CVSD_OLAL;i<CVSD_FS+CVSD_OLAL+CVSD_RT;i++){
            plc_state->hist[CVSD_LHIST+i] = plc_state->hist[plc_state->bestlag+i];
        }

    } else {
        for (i=0;i<CVSD_FS+CVSD_RT+CVSD_OLAL;i++){
            plc_state->hist[CVSD_LHIST+i] = plc_state->hist[plc_state->bestlag+i];
        }
    }

    for (i=0;i<CVSD_FS;i++){
        out[i] = plc_state->hist[CVSD_LHIST+i];
    }

    /* shift the history buffer */
    for (i=0;i<CVSD_LHIST+CVSD_RT+CVSD_OLAL;i++){
        plc_state->hist[i] = plc_state->hist[i+CVSD_FS];
    }
}

void btstack_cvsd_plc_good_frame(btstack_cvsd_plc_state_t *plc_state, int8_t *in, int8_t *out){
    float val;
    int i = 0;
    if (plc_state->nbf>0){
        for (i=0;i<CVSD_RT;i++){
            out[i] = plc_state->hist[CVSD_LHIST+i];
        }
            
        for (i=CVSD_RT;i<CVSD_RT+CVSD_OLAL;i++){
            float left  = plc_state->hist[CVSD_LHIST+i];
            float right = in[i];
            val = left * rcos[i-CVSD_RT] + right *rcos[CVSD_OLAL+CVSD_RT-1-i];
            out[i] = crop_to_int8(val);
        }
    }

    for (;i<CVSD_FS;i++){
        out[i] = in[i];
    }
    
    /*Copy the output to the history buffer */
    for (i=0;i<CVSD_FS;i++){
        plc_state->hist[CVSD_LHIST+i] = out[i];
    }

    /* shift the history buffer */
    for (i=0;i<CVSD_LHIST;i++){
        plc_state->hist[i] = plc_state->hist[i+CVSD_FS];
    }
    
    plc_state->nbf=0;
}

