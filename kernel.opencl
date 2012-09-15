/*
Copyright (c) 2012, Insomniac Games
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.

Redistributions in binary form must reproduce the above copyright notice, this
list of conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

kernel void score_combinations (
    global const uint*            combinations,
    global const uint4*           item_sizes,
    global const uint4*           bucket_bits,
    global uint*                  result_buffer,
    int                           word_count128)
{
  const int index = get_global_id(0);
  const int comb_index = index * COMB;

  const int r0 = combinations[comb_index + 0];
  const int r1 = combinations[comb_index + 1];

#if COMB > 2
  const int r2 = combinations[comb_index + 2];
#endif
#if COMB > 3
  const int r3 = combinations[comb_index + 3];
#endif
#if COMB > 4
  const int r4 = combinations[comb_index + 4];
#endif
#if COMB > 5
  const int r5 = combinations[comb_index + 5];
#endif

  if (r0 < 0 || r1 < 0
#if COMB > 2
      || r2 < 0
#endif
#if COMB > 3
      || r3 < 0
#endif
#if COMB > 4
      || r4 < 0
#endif
#if COMB > 5
      || r5 < 0
#endif
     )
  {
    result_buffer[index] = 0;
    return;
  }

  const global uint4* b0 = bucket_bits + r0 * word_count128;
  const global uint4* b1 = bucket_bits + r1 * word_count128;
#if COMB > 2
  const global uint4* b2 = bucket_bits + r2 * word_count128;
#endif
#if COMB > 3
  const global uint4* b3 = bucket_bits + r3 * word_count128;
#endif
#if COMB > 4
  const global uint4* b4 = bucket_bits + r4 * word_count128;
#endif
#if COMB > 5
  const global uint4* b5 = bucket_bits + r5 * word_count128;
#endif

  const uint4 zero = 0;

  uint4 score = { 0, 0, 0, 0 };

  for (int i = 0; i < word_count128; ++i)
  {
    // Combine the bucket ref bits to yield 4 x 32-bit asset-sharing bits
    const uint4 v = b0[i] & b1[i]
#if COMB > 2
      & b2[i]
#endif
#if COMB > 3
      & b3[i]
#endif
#if COMB > 4
      & b4[i]
#endif
#if COMB > 5
      & b5[i]
#endif
      ;

    // Iterate the 32 bits of each lane while running over the file size array.
    //
    // The size array must be 32-tile shuffled on the host to support the
    // parallel iteration order:
    //
    // [size of asset 0] [size of asset 32] [size of asset 64] [size of asset 96]
    // [size of asset 1] [size of asset 33] [size of asset 65] [size of asset 97]
    // ... and so on ...

    // This mask will move from bit 0 to bit 31 in the loop.
    //uint4 mask = { 1, 1, 1, 1 };
    uint mask = 1;

#pragma unroll
    for (int bit = 0; bit < 32; ++bit)
    {
      const uint4 sizes = *item_sizes++;

      const uint4 masked_sizes = (mask & v) == zero ? zero : sizes;

      score += masked_sizes;
      mask <<= 1;
    }
  }

  result_buffer[index] = score.x + score.y + score.z + score.w;
}
