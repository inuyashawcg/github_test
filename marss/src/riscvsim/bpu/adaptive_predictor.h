/**
 * Adaptive Predictor
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
#ifndef _ADAPTIVE_PREDICTOR_H_
#define _ADAPTIVE_PREDICTOR_H_

#include "../riscv_sim_typedefs.h"
#include "../utils/sim_params.h"

/* Adaptive Predictor Level 1: Global History Table (GHT) Entry */
typedef struct GHTEntry
{
    target_ulong pc; /* Virtual address of the branch */
    uint32_t ghr;    /* History register */
} GHTEntry;

/* Adaptive Predictor Level 2: Pattern History Table (PHT) Entry */
typedef struct PHTEntry
{
    target_ulong pc; /* Virtual address of the branch */
    int *ctr;        /* Array of 2-bit saturating counters */
} PHTEntry;

typedef struct AdaptivePredictor
{
    GHTEntry *ght;           /* GHT */
    PHTEntry *pht;           /* PHT */
    int ght_size;            /* Number of entries in GHT */
    int pht_size;            /* Number of entries in PHT */
    uint32_t ght_index_bits; /* Number of lowest bits of PC required to index
                                into GHT */
    uint32_t pht_index_bits; /* Number of lowest bits of PC required to index
                                into PHT */

    /* Number of bits in history register present in GHT entry */
    uint32_t hreg_bits;

    /* Type of adaptive predictor scheme used (GAg, GAp, PAg, PAp), based on
     * ght_size and pht_size*/
    int type;

    /* Type of aliasing function (and, xor or none) to apply for GAg based
     * schemes */
    int alias_func_type;
    uint32_t (*pfn_ap_aliasing_func)(const struct AdaptivePredictor *a,
                                     uint32_t hr, target_ulong pc);
} AdaptivePredictor;

AdaptivePredictor *adaptive_predictor_init(const SimParams *p);
void adaptive_predictor_free(AdaptivePredictor **a);
void adaptive_predictor_add(AdaptivePredictor *a, target_ulong pc);
void adaptive_predictor_update(AdaptivePredictor *a, target_ulong pc, int pred);
void adaptive_predictor_flush(AdaptivePredictor *a);
int adaptive_predictor_probe(const AdaptivePredictor *a, target_ulong pc);
int adaptive_predictor_get_prediction(const AdaptivePredictor *a,
                                      target_ulong pc);
#endif
