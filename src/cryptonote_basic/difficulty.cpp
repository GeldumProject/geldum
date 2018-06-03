// Portions Copyright (c) 2012-2013, The Cryptonote developers
// Portions Copyright (c) 2014-2017, The Monero Project
// Portions Copyright (c) 2017, The Masari Project
// Portions Copyright (c) 2017-2018, The Geldum Project

// DERO uses modified difficuly algorithm based on following references.
// Many other references, wikis, research papers were studied in planning DERO difficulty algorithm. 
// Contact us to add any missing references and copyrights. 

// References on difficulty algorithhms below:
// https://github.com/zawy12/difficulty-algorithms/issues/17
// https://zawy1.blogspot.in/2017/11/best-difficulty-algorithms.html
// https://github.com/monero-project/monero/pull/2887
// https://github.com/zawy12/difficulty-algorithms/issues/1

// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Parts of this file are originally copyright (c) 2012-2013 The Cryptonote developers

#include <stdio.h>
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <limits>
#include <boost/thread.hpp>
#include <boost/utility/value_init.hpp>

#include "include_base_utils.h"
#include "common/int-util.h"
#include "crypto/hash.h"
#include "cryptonote_config.h"
#include "difficulty.h"

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "difficulty"

namespace cryptonote {


  using std::size_t;
  using std::uint64_t;
  using std::vector;

#if defined(__x86_64__)
  static inline void mul(uint64_t a, uint64_t b, uint64_t &low, uint64_t &high) {
    low = mul128(a, b, &high);
  }

#else

  static inline void mul(uint64_t a, uint64_t b, uint64_t &low, uint64_t &high) {
    // __int128 isn't part of the standard, so the previous function wasn't portable. mul128() in Windows is fine,
    // but this portable function should be used elsewhere. Credit for this function goes to latexi95.

    uint64_t aLow = a & 0xFFFFFFFF;
    uint64_t aHigh = a >> 32;
    uint64_t bLow = b & 0xFFFFFFFF;
    uint64_t bHigh = b >> 32;

    uint64_t res = aLow * bLow;
    uint64_t lowRes1 = res & 0xFFFFFFFF;
    uint64_t carry = res >> 32;

    res = aHigh * bLow + carry;
    uint64_t highResHigh1 = res >> 32;
    uint64_t highResLow1 = res & 0xFFFFFFFF;

    res = aLow * bHigh;
    uint64_t lowRes2 = res & 0xFFFFFFFF;
    carry = res >> 32;

    res = aHigh * bHigh + carry;
    uint64_t highResHigh2 = res >> 32;
    uint64_t highResLow2 = res & 0xFFFFFFFF;

    //Addition

    uint64_t r = highResLow1 + lowRes2;
    carry = r >> 32;
    low = (r << 32) | lowRes1;
    r = highResHigh1 + highResLow2 + carry;
    uint64_t d3 = r & 0xFFFFFFFF;
    carry = r >> 32;
    r = highResHigh2 + carry;
    high = d3 | (r << 32);
  }

#endif

  static inline bool cadd(uint64_t a, uint64_t b) {
    return a + b < a;
  }

  static inline bool cadc(uint64_t a, uint64_t b, bool c) {
    return a + b < a || (c && a + b == (uint64_t) -1);
  }

  bool check_hash(const crypto::hash &hash, difficulty_type difficulty) {
    uint64_t low, high, top, cur;
    // First check the highest word, this will most likely fail for a random hash.
    mul(swap64le(((const uint64_t *) &hash)[3]), difficulty, top, high);
    if (high != 0) {
      return false;
    }
    mul(swap64le(((const uint64_t *) &hash)[0]), difficulty, low, cur);
    mul(swap64le(((const uint64_t *) &hash)[1]), difficulty, low, high);
    bool carry = cadd(cur, low);
    cur = high;
    mul(swap64le(((const uint64_t *) &hash)[2]), difficulty, low, high);
    carry = cadc(cur, low, carry);
    carry = cadc(high, top, carry);
    return !carry;
  }






  /* this is from masari
  # Tom Harold (Degnr8) WT
  # Modified by Zawy to be a weighted-Weighted Harmonic Mean (WWHM)
  # No limits in rise or fall rate should be employed.
  # MTP should not be used.
  k = (N+1)/2  * T
  # original algorithm
  d=0, t=0, j=0
  for i = height - N+1 to height  # (N most recent blocks)
      # TS = timestamp
      solvetime = TS[i] - TS[i-1]
      solvetime = 10*T if solvetime > 10*T
      solvetime = -9*T if solvetime < -9*T
      j++
      t +=  solvetime * j
      d +=D[i] # sum the difficulties
  next i
  t=T*N/2 if t < T*N/2  # in case of startup weirdness, keep it reasonable
  next_D = d * k / t
  */
  difficulty_type next_difficulty(std::vector<std::uint64_t> timestamps, std::vector<difficulty_type> cumulative_difficulties, size_t target_seconds) {


    uint64_t difficulty;
    if (timestamps.size() > DIFFICULTY_BLOCKS_COUNT_V2)
    {
      timestamps.resize(DIFFICULTY_BLOCKS_COUNT_V2);
      cumulative_difficulties.resize(DIFFICULTY_BLOCKS_COUNT_V2);
    }

    size_t length = timestamps.size();
    assert(length == cumulative_difficulties.size());
    if (length <= 1) {
      return 1;
    }

    uint64_t weighted_timespans = 0;
    for (size_t i = 1; i < length; i++) {
      uint64_t timespan;
      if (timestamps[i - 1] >= timestamps[i]) {
        timespan = 1;
      } else {
        timespan = timestamps[i] - timestamps[i - 1];
      }
      if (timespan > 10 * target_seconds) {
        timespan = 10 * target_seconds;
      }
      weighted_timespans += i * timespan;
    }

    uint64_t minimum_timespan = target_seconds * length / 2;
    if (weighted_timespans < minimum_timespan) {
      weighted_timespans = minimum_timespan;
    }

    difficulty_type total_work = cumulative_difficulties.back() - cumulative_difficulties.front();
    assert(total_work > 0);

    uint64_t low, high;
    uint64_t target = (((length + 1) / 2) * target_seconds * 3)/2;
    mul(total_work, target, low, high);
    if (high != 0) {
      return 0;
    }
    difficulty =  low / weighted_timespans;


/* THIS PATCH will protect the blockchain from miners hijacking  with sudden abandoning 
   just as happenned with BTC after the hard fork 
   We further need to study the cumulative effects of 1000X hash attacks and other factors
*/
/*
     if ( timestamps.size() > 2) {
      uint64_t last_block_time=0;
      for(int64_t i = (timestamps.size()-1 ); i>=0;i--){
        if (last_block_time < timestamps[i] ) {
            last_block_time = timestamps[i]; // find the latest time stamp, 
        }
      }
      if( last_block_time != 0 &&  (time(NULL)-last_block_time) >= DIFFICULTY_MANIPULATION_THRESHOLD ) {
        uint64_t power =  ((time(NULL)-last_block_time) /DIFFICULTY_MANIPULATION_THRESHOLD );
        power--; 
       
        uint64_t manipulated_difficulty =  difficulty/ ( 2 << power ) ;


        if (manipulated_difficulty < 1000){
          manipulated_difficulty= 1000; // difficulty will not be less than this
        }


        LOG_PRINT_L1("Difficulty "<<difficulty<<" decreasing difficulty by  factor "<<( 2 << power)<<" (to avoid extremely hard patches) "
                                <<"current_difficulty "<<manipulated_difficulty<<ENDL);
      
        
    
       return manipulated_difficulty; // lets save the network from miners hijacking

    }
  }
*/
  return difficulty;


}



}

