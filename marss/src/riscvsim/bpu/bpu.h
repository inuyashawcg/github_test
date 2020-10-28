/**
 * Branch Prediction Unit
 *
 * MARSS-RISCV : Micro-Architectural System Simulator for RISC-V
 *
 * Copyright (c) 2017-2020 Gaurav Kothari {gkothar1@binghamton.edu}
 * State University of New York at Binghamton
 *
 * Copyright (c) 2018-2019 Parikshit Sarnaik {psarnai1@binghamton.edu}
 * State University of New York at Binghamton
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#ifndef _BRANCH_PRED_UNIT_H_
#define _BRANCH_PRED_UNIT_H_

#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../riscv_sim_typedefs.h"
#include "../utils/sim_params.h"
#include "../utils/sim_stats.h"
#include "adaptive_predictor.h"
#include "bht.h"
#include "btb.h"
#include "ras.h"

typedef struct BPUResponsePkt
{
    int btb_probe_status;
    int ap_probe_status;
    int bpu_probe_status;
    BtbEntry *btb_entry;
} BPUResponsePkt;

typedef struct BranchPredUnit
{
    BranchTargetBuffer *btb;
    Bht *bht;
    Ras *ras;
    AdaptivePredictor *ap;
    SimStats *stats;

    /* Predictor type: bimodal or adaptive */
    int bpu_type;
} BranchPredUnit;

BranchPredUnit *bpu_init(const SimParams *p, SimStats *s);
target_ulong bpu_get_target(BranchPredUnit *u, target_ulong pc,
                            BtbEntry *btb_entry);
void bpu_probe(BranchPredUnit *u, target_ulong pc, BPUResponsePkt *p, int priv);
void bpu_add(BranchPredUnit *u, target_ulong pc, int type, BPUResponsePkt *p,
             int priv, int fret);
void bpu_update(BranchPredUnit *u, target_ulong pc, target_ulong target,
                int pred, int type, BPUResponsePkt *p, int priv);
void bpu_flush(BranchPredUnit *u);
void bpu_free(BranchPredUnit **u);
#endif
