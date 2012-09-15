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

#ifndef DEDUPE_H
#define DEDUPE_H

#include "stdint_wrapper.h"

#ifdef __cplusplus
extern "C" {
#endif

struct dedupe_options
{
  int             allow_gpu;
  const char      *preferred_platform;
  const char      *preferred_device;
  double          min_gain_mb;
  int64_t         min_bucket_size;
  int             kick_size;
  int             local_size;
  int             max_k;
  int             max_levels;
  int             max_iterations;
  int             max_bucket_splits;
  int             merge_across_levels;
  int             verbosity;
};

extern const struct dedupe_options dedupe_defaults;

struct dedupe_state;

struct dedupe_state*
dedupe_init(const struct dedupe_options* options);

void
dedupe_destroy(struct dedupe_state* state);

int
dedupe_load_input(struct dedupe_state *state, const char *filename);

int
dedupe_run(struct dedupe_state *state);

void
dedupe_print_summary(struct dedupe_state *state, const char* label);

void
dedupe_print_seek_summary(struct dedupe_state *state);

int
dedupe_save_output(struct dedupe_state *state, const char *filename);

#ifdef __cplusplus
}
#endif

#endif
