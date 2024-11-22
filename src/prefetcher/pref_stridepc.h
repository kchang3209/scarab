/* Copyright 2020 HPS/SAFARI Research Groups
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/***************************************************************************************
 * File         : pref_stridepc.h
 * Author       : HPS Research Group
 * Date         : 1/23/2005
 * Description  : Stride Prefetcher - Based on load's PC address
 ***************************************************************************************/
#ifndef __PREF_STRIDEPC_H__
#define __PREF_STRIDEPC_H__

#include "pref_common.h"

/*
typedef struct StridePC_Table_Entry_Struct {
  Flag trained;
  Flag valid;

  Addr last_addr;
  Addr load_addr;
  Addr start_index;
  Addr pref_last_index;
  int  stride;

  Counter train_num;
  Counter pref_sent;
  Counter last_access;  // for lru
} StridePC_Table_Entry;
*/
#define PREF_OFFSETPC_TRAINNUM  3  // Number of consistent offsets to consider the entry trained
#define PREF_OFFSETPC_DEGREE    4  // Number of prefetches to issue per trained entry

// Final Lab
typedef struct OffsetPC_Table_Entry_Struct{
    Addr load_addr;         // Load address for which prefetching is being done
    Addr offset;            // Offset used for prefetching
    Flag valid;             // Whether this table entry is valid
    Flag trained;           // Whether this entry has been trained
    int train_num;          // Number of training instances for this entry
    Addr last_addr;         // The last accessed address
    Addr pref_last_index;   // Last predicted address
    int pref_sent;          // Counter for how many prefetches have been sent
    // Other fields as necessary
} OffsetPC_Table_Entry;

//Final Lab modification
typedef struct Pref_OffsetPC_Struct {
  HWP_Info*             hwp_info;
  OffsetPC_Table_Entry* offset_table;   
  CacheLevel        type;
} Pref_OffsetPC;

typedef struct{
  Pref_OffsetPC* offsetpc_hwp_core_ul1;
  Pref_OffsetPC* offsetpc_hwp_core_umlc;
} offsetpc_prefetchers;

/*************************************************************/
/* HWP Interface */
void pref_stridepc_init(HWP* hwp);

void pref_stridepc_ul1_miss(uns8 proc_id, Addr lineAddr, Addr loadPC,
                            uns32 global_hist);
void pref_stridepc_ul1_hit(uns8 proc_id, Addr lineAddr, Addr loadPC,
                           uns32 global_hist);
void pref_stridepc_umlc_miss(uns8 proc_id, Addr lineAddr, Addr loadPC,
                            uns32 global_hist);
void pref_stridepc_umlc_hit(uns8 proc_id, Addr lineAddr, Addr loadPC,
                           uns32 global_hist);


/*************************************************************/
/* Internal Function */
void init_offsetpc(HWP* hwp, Pref_OffsetPC* offsetpc_hwp_core);
void pref_offsetpc_train(Pref_OffsetPC* offsetpc_hwp, uns8 proc_id, Addr lineAddr, Addr loadPC,
                             Flag is_hit);

      
/*************************************************************/
/* Misc functions */

#endif /*  __PREF_STRIDEPC_H__*/
