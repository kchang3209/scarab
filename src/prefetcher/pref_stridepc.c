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
 * File         : pref_stride.c
 * Author       : HPS Research Group
 * Date         : 1/23/2005
 * Description  : Stride Prefetcher - Based on load's PC address
 ***************************************************************************************/

#include "debug/debug_macros.h"
#include "debug/debug_print.h"
#include "globals/global_defs.h"
#include "globals/global_types.h"
#include "globals/global_vars.h"

#include "globals/assert.h"
#include "globals/utils.h"
#include "op.h"

#include "core.param.h"
#include "debug/debug.param.h"
#include "general.param.h"
#include "libs/hash_lib.h"
#include "libs/list_lib.h"
#include "memory/memory.param.h"
#include "prefetcher//pref_stridepc.h"
#include "prefetcher//pref_stridepc.param.h"
#include "prefetcher/pref.param.h"
#include "prefetcher/pref_common.h"
#include "statistics.h"

/*
   stride prefetcher : Stride prefetcher based on the original stride work -
   Essentially use the load's PC to index into a table of prefetch entries
*/

/*
 * This prefetcher is implemented in a unified way where all cores share the same prefetcher instance.
 * As a result, the proc_id received as a parameter is ignored.
 */

/**************************************************************************************/
/* Macros */
#define DEBUG(args...) _DEBUG(DEBUG_PREF_STRIDEPC, ##args)

stridepc_prefetchers stridepc_prefetche_array;

int roundnum = 0;       // check with ROHIT, does global variable get initialized every time this code is executed?
int roundmax = 100;      // per BO paper
int scorethreshold = 3;
int best_offset;

int value_list[] = {1, 2, 3, 4, 5, 6, 8, 9, 10, 12, 15, 16, 18, 20, 24, 25, 27, 30, 32, 36, 40};
// , 45, 48, 50, 54, 60, 64, 72, 75, 80, 81, 90, 96, 100, 108, 120, 125, 128, 135,
// 144, 150, 160, 162, 180, 192, 200, 216, 225, 240, 243, 250, 256};

int offsetnum = sizeof(value_list)/sizeof(value_list[0]);

Offset_List offset_candidates[21];


void pref_stridepc_init(HWP* hwp) {

  if(!PREF_STRIDEPC_ON){
    return;
  }
  hwp->hwp_info->enabled = TRUE;
  //Dcache: L1
  //MLC: L2
  //LLC or L1: L3
  //init_offset_list();
  for (int i = 0; i < offsetnum; i++){
    offset_candidates[i].value = value_list[i];
    offset_candidates[i].tested = FALSE;  // reset candidate to 'not yet tested'
  }
  //if L2 prefetcher is on
  if(PREF_UMLC_ON){
    stridepc_prefetche_array.stridepc_hwp_core_umlc        = (Pref_StridePC*)malloc(sizeof(Pref_StridePC) * NUM_CORES);
    stridepc_prefetche_array.stridepc_hwp_core_umlc->type  = UMLC;
    init_stridepc(hwp, stridepc_prefetche_array.stridepc_hwp_core_umlc);
  }
  //if L3 prefetcher is on
  if(PREF_UL1_ON){
    stridepc_prefetche_array.stridepc_hwp_core_ul1        = (Pref_StridePC*)malloc(sizeof(Pref_StridePC) * NUM_CORES);
    stridepc_prefetche_array.stridepc_hwp_core_ul1->type  = UL1;
    init_stridepc(hwp, stridepc_prefetche_array.stridepc_hwp_core_ul1);
  }

}

void init_stridepc(HWP* hwp, Pref_StridePC* stridepc_hwp_core) {
  uns8 proc_id;

  for(proc_id = 0; proc_id < NUM_CORES; proc_id++) {
    stridepc_hwp_core[proc_id].hwp_info     = hwp->hwp_info;
    stridepc_hwp_core[proc_id].stride_table = (StridePC_Table_Entry*)calloc(
      PREF_STRIDEPC_TABLE_N, sizeof(StridePC_Table_Entry));
  }

}



void pref_stridepc_ul1_hit(uns8 proc_id, Addr lineAddr, Addr loadPC,
                           uns32 global_hist) {
  pref_stridepc_train(&stridepc_prefetche_array.stridepc_hwp_core_ul1[proc_id], proc_id, lineAddr, loadPC, TRUE);
}

void pref_stridepc_ul1_miss(uns8 proc_id, Addr lineAddr, Addr loadPC,
                            uns32 global_hist) {
  pref_stridepc_train(&stridepc_prefetche_array.stridepc_hwp_core_ul1[proc_id], proc_id, lineAddr, loadPC, FALSE);
}

void pref_stridepc_umlc_hit(uns8 proc_id, Addr lineAddr, Addr loadPC,
                           uns32 global_hist) {
  pref_stridepc_train(&stridepc_prefetche_array.stridepc_hwp_core_umlc[proc_id], proc_id, lineAddr, loadPC, TRUE);
}

void pref_stridepc_umlc_miss(uns8 proc_id, Addr lineAddr, Addr loadPC,
                            uns32 global_hist) {
  pref_stridepc_train(&stridepc_prefetche_array.stridepc_hwp_core_umlc[proc_id], proc_id, lineAddr, loadPC, FALSE);
}


//************************************************************************//
//*************************  BEST OFFSET TRAIN  **************************//
//************************************************************************//
// CSE220 Fall 2024
// Best Offset Prefetcher modification
void pref_stridepc_train(Pref_StridePC* stridepc_hwp, uns8 proc_id, Addr lineAddr, Addr loadPC,
                             Flag is_hit) {
  int ii;
  int idx = -1;
  Addr                  lineIndex = lineAddr >> LOG2(DCACHE_LINE_SIZE);
  StridePC_Table_Entry* entry     = NULL;
  int stride;
  if(loadPC == 0) {
    return;  // no point hashing on a null address
  }

  int current_offset = -1; 
  int current_offset_index = -1;

  //select current offset for testing
  for (int bocount = 0; bocount < offsetnum; bocount++){
    if(!offset_candidates[bocount].tested){
      current_offset = offset_candidates[bocount].value;
      current_offset_index = bocount;
      offset_candidates[bocount].tested = TRUE;
      break;
    }
  }

  //if all offsets tested, reset, roundnum++
  if(current_offset == -1){
    for (int i = 0; i < offsetnum; i++){
      offset_candidates[i].tested = FALSE;  // reset candidate to 'not yet tested'
    }
    roundnum += 1;
    if (roundnum == roundmax){
      //get_best_offset();
      int temp_score = -1;
      for (int i = 0; i < offsetnum; i++){
        //only update best_offset if score is higher than scorethreshold
        if (offset_candidates[i].score > temp_score){
          temp_score = offset_candidates[i].score;
          if (temp_score >= scorethreshold){
            best_offset = offset_candidates[i].value;
          }
        }
        offset_candidates[i].score  = 0;  // only reset candidate score at getting best offset because scores accumulate and they are not reset in every round
      }
      roundnum = 0;
    }
  } else {
    //else check if X-d is in table, if yes, score++
    // Trying Binary Search, however we realized List isn't sorted.
    // int low = 0 , high = PREF_STRIDEPC_TABLE_N - 1;
    // while (low <= high){
    //   int mid = low +(high-low)/2;
    //   if(stridepc_hwp->stride_table[mid].load_addr == (loadPC-current_offset) && stridepc_hwp->stride_table[ii].valid) {    
    //     offset_candidates[current_offset_index].score += 1;
    //   }
    //   else if (stridepc_hwp->stride_table[mid].load_addr < (loadPC-current_offset) && stridepc_hwp->stride_table[ii].valid){
    //     low = mid + 1;
    //   }
    //   else{
    //     high = mid - 1;
    //   }
    // }
    for(ii = 0; ii < PREF_STRIDEPC_TABLE_N; ii++) {
      if(stridepc_hwp->stride_table[ii].load_addr == (loadPC-current_offset) && stridepc_hwp->stride_table[ii].valid) {
        offset_candidates[current_offset_index].score += 1;
        break;
      }
    }
  }

  // Search for a matching entry
  for(ii = 0; ii < PREF_STRIDEPC_TABLE_N; ii++) {
    if(stridepc_hwp->stride_table[ii].load_addr == loadPC &&
       stridepc_hwp->stride_table[ii].valid) {
      idx = ii;
      break;
    }

  }

  // If no entry is found, allocate a new one
  if(idx == -1) {

    //ONLY TRAIN on miss
    if(is_hit) {  
      return;
    }

    // Find an unused or LRU entry
    for(ii = 0; ii < PREF_STRIDEPC_TABLE_N; ii++) {
      if(!stridepc_hwp->stride_table[ii].valid) {
        idx = ii;
        break;
      }
      if(idx == -1 || (stridepc_hwp->stride_table[idx].last_access <
                       stridepc_hwp->stride_table[ii].last_access)) {
        idx = ii;
      }
    }

    // Initialize the new entry
    stridepc_hwp->stride_table[idx].trained     = FALSE;
    stridepc_hwp->stride_table[idx].valid       = TRUE;
    stridepc_hwp->stride_table[idx].stride      = 0;    //BO has no stride, change it >stride
    stridepc_hwp->stride_table[idx].train_num   = 0;
    stridepc_hwp->stride_table[idx].pref_sent   = 0;
    stridepc_hwp->stride_table[idx].last_addr   = (PREF_STRIDEPC_USELOADADDR ?
                                                   lineAddr :
                                                   lineIndex);
    stridepc_hwp->stride_table[idx].load_addr   = loadPC;
    stridepc_hwp->stride_table[idx].last_access = cycle_count;
    // return;    // return commented out because we are not using stride logistic
  }

  // comment out candi
  entry              = &stridepc_hwp->stride_table[idx];
  entry->last_access = cycle_count;
  stride = (PREF_STRIDEPC_USELOADADDR ? (lineAddr - entry->last_addr) :
                                        (lineIndex - entry->last_addr));

  
  Addr pref_index;
  //Addr curr_idx = (PREF_STRIDEPC_USELOADADDR ? lineAddr : lineIndex);

  // if(  ((curr_idx >= entry->start_index) && (curr_idx <= entry->pref_last_index)) ||        //filter changed for BO
  //       ((curr_idx <= entry->start_index) && (curr_idx >= entry->pref_last_index))  ) {
  // all good. continue sending out prefetches
  for(ii = 0; ii < PREF_STRIDEPC_DEGREE; ii++) {
    
    //the original stridepc prefetch index
    //pref_index = entry->pref_last_index + entry->stride;

    //the BO prefetch index
    if (best_offset > 0){    // <64 because there are 64 lines in a page according to our setting
      pref_index = lineIndex + best_offset;
    

      ASSERT(proc_id,
            proc_id == (pref_index >> (58 - LOG2(DCACHE_LINE_SIZE))));

      if(stridepc_hwp->type == UMLC){ if(!pref_addto_umlc_req_queue(proc_id,
            (PREF_STRIDEPC_USELOADADDR ?
            (pref_index >> LOG2(DCACHE_LINE_SIZE)) :
                pref_index),
          stridepc_hwp->hwp_info->id)){
        break;}
      }else{
        if(!pref_addto_ul1req_queue(proc_id,
            (PREF_STRIDEPC_USELOADADDR ?
            (pref_index >> LOG2(DCACHE_LINE_SIZE)) :
                pref_index),
          stridepc_hwp->hwp_info->id))  // FIXME
      break;   }

                                            // q is full
      //entry->pref_last_index = pref_index;
    }
  }
  entry->last_addr = (PREF_STRIDEPC_USELOADADDR ? lineAddr : lineIndex);

}

//************************************************************************//
//***************************  STRIDEPC TRAIN  ***************************//
//************************************************************************//

// void pref_offsetpc_train(Pref_OffsetPC* offsetpc_hwp, uns8 proc_id, Addr lineAddr, Addr loadPC, Flag is_hit) {
//     int ii;
//     int idx = -1;

//     Addr lineIndex = lineAddr >> LOG2(DCACHE_LINE_SIZE);
//     OffsetPC_Table_Entry* entry = NULL;

//     if (loadPC == 0) {
//         return;  // No point hashing on a null address
//     }

//     // Search for an existing entry
//     for (ii = 0; ii < PREF_OFFSETPC_TABLE_N; ii++) {
//         if (offsetpc_hwp->offset_table[ii].load_addr == loadPC && offsetpc_hwp->offset_table[ii].valid) {
//             idx = ii;
//             break;
//         }
//     }

//     // Create a new entry if not found
//     if (idx == -1) {
//         for (ii = 0; ii < PREF_OFFSETPC_TABLE_N; ii++) {
//             if (!offsetpc_hwp->offset_table[ii].valid) {
//                 idx = ii;
//                 break;
//             }
//         }
//         offsetpc_hwp->offset_table[idx].load_addr = loadPC;
//         offsetpc_hwp->offset_table[idx].last_addr = lineAddr;
//         offsetpc_hwp->offset_table[idx].train_num = 0;
//         offsetpc_hwp->offset_table[idx].valid = TRUE;
//         offsetpc_hwp->offset_table[idx].trained = FALSE;
//         return;
//     }

//     entry = &offsetpc_hwp->offset_table[idx];

//     // Offset training logic
//     if (entry->train_num == 0) {
//         entry->offset = lineAddr - entry->last_addr;
//     } else {
//         Addr new_offset = lineAddr - entry->last_addr;
//         if (new_offset == entry->offset) {
//             entry->train_num++;
//         } else {
//             // Offset changed, retrain
//             entry->train_num = 1;
//             entry->offset = new_offset;
//         }
//     }

//     // Check if training is complete
//     if (entry->train_num >= PREF_OFFSETPC_TRAINNUM) {
//         entry->trained = TRUE;
//     }

//     // Issue prefetch if trained
//     if (entry->trained) {
//         Addr pref_index = entry->last_addr + entry->offset;
//         if (entry->pref_sent < PREF_OFFSETPC_DEGREE) {
//             // Issue prefetch request
//             pref_addto_req_queue(proc_id, pref_index, offsetpc_hwp->hwp_info->id);
//             entry->pref_sent++;
//             entry->pref_last_index = pref_index;
//         }
//     }
//     entry->last_addr = lineAddr;  // Update last accessed address
// }
