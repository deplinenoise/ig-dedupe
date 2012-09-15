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

#include "combgen.h"
#include <assert.h>

void combgen_init(struct combgen *c, int n, int k)
{
  int x;
  int64_t ncomb_num = 1;
  int64_t ncomb_div = 1;

  assert(k < COMB_MAX_K);

  c->n = n;
  c->k = k;
  
  /* Set up initial combination; 0, 1, 2, ... */
  for (x = 0; x < k; ++x)
    c->i[x] = x;

  /* Decrement the last index once so we can do pre-increment later. */
  c->i[k - 1] -= 1;

  /* Compute how many combinations we have in the stream (n!/k!) */
  for (x = 0; x < k; ++x)
    ncomb_num *= n - x;

  for (x = 0; x < k - 1; ++x)
    ncomb_div *= k - x;

  c->index = 0;
  c->count = ncomb_num / ncomb_div;
}

int combgen_iterate(struct combgen *c, int *output, int max, const int32_t* remapping)
{
  int64_t seq_left = c->count - c->index;
  int64_t output_count = max > seq_left ? seq_left : max;
  int remaining = (int) output_count;
  int x;
  const int n = c->n;
  const int k = c->k;

  while (remaining > 0)
  {
    int y = k - 1;
    int max = n - 1;
    int v = 0;

    /* Step the combination buffer */
    while (y >= 0)
    {
      v = c->i[y];

      if (v < max)
        break;

      --y;
      --max;
    }

    c->i[y] = v + 1;

    for (x = y + 1; x < k; ++x)
      c->i[x] = c->i[x-1] + 1;

    /* Copy one remapped combination to the output buffer */
    for (x = 0; x < k; ++x)
      *output++ = remapping[c->i[x]];

    --remaining;
  }

  c->index += output_count;

  assert(c->index <= c->count);

  return (int) output_count;
}

