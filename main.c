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

#include "dedupe.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char* help_text = 
"dedupe - file deduplication utility for optical disc layout\n"
"Version 1.0\n"
"Copyright (c) 2012, Insomniac Games\n"
"All rights reserved.\n\n"
"usage:\n"
"   dedupe [options] input.json output.json\n"
"\n"
"options:\n"
"  -k number          -- max combination group size (K), between 2 and 6\n"
"  -levels number     -- number of de-duplication levels to attempt\n"
"  -gain float-number -- minimum acceptable gain for de-duplication, in megabytes\n"
"  -gpu 1/0           -- specify whether GPU execution is permissable\n"
"  -kicksize number   -- specify global work size per OpenCL kernel invocation\n"
"  -localsize number  -- specify local work size per OpenCL kernel invocation\n"
"  -dag 1/0           -- when 1, allow merging buckets from different levels\n"
"  -maxsplits number  -- max # of splits for a single bucket\n"
"  -minbucket float   -- minimum bucket size in MB to be considered for splitting\n"
"  -v                 -- increase verbosity (can specify multiple times)\n"
;

static void usage()
{
  fputs(help_text, stderr);
  exit(1);
}

int main(int argc, char* argv[])
{
  int i, result;
  const char *in_fn = NULL, *out_fn = NULL;

  struct dedupe_options options = dedupe_defaults;
  struct dedupe_state *d;

  for (i = 1; i < argc - 2; )
  {
    if (argv[i][0] != '-')
      break;

    if (0 == strcmp(argv[i] + 1, "k"))
    {
      options.max_k = atoi(argv[i+1]);
      i += 2;
    }
    else if (0 == strcmp(argv[i] + 1, "levels"))
    {
      options.max_levels = atoi(argv[i+1]);
      i += 2;
    }
    else if (0 == strcmp(argv[i] + 1, "gain"))
    {
      options.min_gain_mb = atof(argv[i+1]);
      i += 2;
    }
    else if (0 == strcmp(argv[i] + 1, "gpu"))
    {
      options.allow_gpu = atoi(argv[i+1]);
      i += 2;
    }
    else if (0 == strcmp(argv[i] + 1, "kicksize"))
    {
      options.kick_size = atoi(argv[i+1]);
      i += 2;
    }
    else if (0 == strcmp(argv[i] + 1, "localsize"))
    {
      options.local_size = atoi(argv[i+1]);
      i += 2;
    }
    else if (0 == strcmp(argv[i] + 1, "dag"))
    {
      options.merge_across_levels = atoi(argv[i+1]);
      i += 2;
    }
    else if (0 == strcmp(argv[i] + 1, "maxsplits"))
    {
      options.max_bucket_splits = atoi(argv[i+1]);
      i += 2;
    }
    else if (0 == strcmp(argv[i] + 1, "minbucket"))
    {
      options.min_bucket_size = (int64_t) (atof(argv[i+1]) * 1024 * 1024);
      i += 2;
    }
    else if (0 == strcmp(argv[i] + 1, "v"))
    {
      options.verbosity++;
      i += 1;
    }
    else
    {
      usage();
    }
  }

  if (i != argc - 2)
    usage();

  in_fn = argv[i];
  out_fn = argv[i+1];

  if (NULL == (d = dedupe_init(&options)))
    return 1;

  if (0 != dedupe_load_input(d, in_fn))
    return 1;

  dedupe_print_summary(d, "input");

  result = dedupe_run(d);

  if (0 == result)
  {
    dedupe_print_summary(d, "output");

    if (options.verbosity > 1)
      dedupe_print_seek_summary(d);

    result = dedupe_save_output(d, out_fn);
  }

  dedupe_destroy(d);

  return result;
}
