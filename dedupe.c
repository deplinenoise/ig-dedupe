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
#include "ocl_util.h"
#include "json.h"
#include "combgen.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#ifdef _MSC_VER
#include <malloc.h> /* for alloca */
#define snprintf _snprintf
#endif

/*--------------------------------------------------------------------------*/

const struct dedupe_options dedupe_defaults =
{
  1,          /* allow_gpu */
  "",         /* preferred_platform */
  "",         /* preferred_device */
  5.0,        /* min_gain_mb */
  512*1024,   /* min_bucket_size */
  65536,      /* kick_size */
  256,        /* work_size */
  4,          /* max_k */
  3,          /* max_levels */
  1024,       /* max_iterations */
  25,         /* max_bucket_splits */
  1,          /* merge_across_levels */
  1           /* verbosity */
};

/*--------------------------------------------------------------------------*/
#define ERRCHECK_GOTO(error_code, message, label) \
  do { \
    if ((error_code) != CL_SUCCESS) { \
      fprintf(stderr, "%s (%s)\n", message, OpenClErrorString(error_code)); \
      goto label; \
    } \
  } while (0)

/*--------------------------------------------------------------------------*/
enum
{
  MAX_COMBINATIONS    = 6,
  MAX_PASSES          = MAX_COMBINATIONS - 1 /* 2, 3, 4, ..., MAX_COMBINATIONS */
};

/*--------------------------------------------------------------------------*/
struct bucket_info
{
  char            *name;
  int32_t         level;
  int32_t         split_count;
  int32_t         *split_links;
  int32_t         ref_count;
  int64_t         ref_size;
};

/*--------------------------------------------------------------------------*/
struct comb_pass
{
  int             k;
  cl_program      program;
  cl_kernel       kernel;
};

/*--------------------------------------------------------------------------*/
struct dedupe_state
{
  /* Working state */
  uint32_t              item_count;
  uint32_t              item_count_padded;
  uint32_t              word_count;
  uint32_t*             item_sizes;
  uint32_t              bucket_count;
  uint32_t              bucket_capacity;
  uint32_t*             bucket_refs;
  struct bucket_info    *buckets;

  /* OpenCL state */
  cl_device_id        ocl_device;
  cl_platform_id      ocl_platform;
  cl_context          ocl_context;
  cl_command_queue    ocl_queue;

  /* OpenCL buffers */
  cl_mem              device_combinations;
  cl_mem              device_sizes;
  cl_mem              device_refs;
  cl_mem              device_output;

  /* CPU buffers, enough space to store K * kick_size combinations. */
  cl_int              *host_combinations;
  cl_uint             *host_scores;

  /* User options */
  struct dedupe_options options;

  /* Passes */
  int32_t             pass_count;
  struct comb_pass    passes[MAX_PASSES];
};

/*--------------------------------------------------------------------------*/

static void*
alloc_aligned(size_t size)
{
#if defined(__APPLE__)
  return malloc(size);
#elif defined(_MSC_VER)
  return _aligned_malloc(size, 16);
#else
#error implement me
#endif
}

/*--------------------------------------------------------------------------*/

static void
free_aligned(void* ptr)
{
#if defined(__APPLE__)
  free(ptr);
#elif defined(_MSC_VER)
  _aligned_free(ptr);
#else
#error implement me
#endif
}

/*--------------------------------------------------------------------------*/

static void*
realloc_aligned(void *ptr, size_t size)
{
#ifdef __APPLE__
  return realloc(ptr, size);
#elif defined(_MSC_VER)
  return _aligned_realloc(ptr, size, 16);
#else
#error implement me
#endif
}


/*--------------------------------------------------------------------------*/

static void
free_buffers(struct dedupe_state *state);

/*--------------------------------------------------------------------------*/

struct dedupe_state *
dedupe_init(const struct dedupe_options *options)
{
  cl_device_id device;
  cl_platform_id platform;
  cl_int error;
  struct dedupe_state *state = NULL;
  int i;

  cl_context_properties properties[] =
  {
    CL_CONTEXT_PLATFORM, 0, /* (cl_context_properties)platform, */
    0
  };

  if (0 != SelectOpenClDevice(
        &device,
        &platform,
        options->allow_gpu,
        options->preferred_platform,
        options->preferred_device))
    goto error;

  state = calloc(sizeof(struct dedupe_state), 1);

  if (!state)
    goto error;

  memcpy(&state->options, options, sizeof(struct dedupe_options));

  /* Fix up kick size and local group size to match hardware constraints */
  {
    size_t kick_size = state->options.kick_size;
    size_t local_size = state->options.local_size;
    size_t device_local_max = 1;

    if (local_size < 1)
      local_size = 1;

    if (CL_SUCCESS == clGetDeviceInfo(device, CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof device_local_max, &device_local_max, NULL))
    {
      if (local_size > device_local_max)
      {
        fprintf(stderr, "local workgroup size reduced from user-specified %d to %d (device cap)\n", (int) local_size, (int) device_local_max);
        local_size = (int) device_local_max;
      }
    }
    if (0 == state->options.allow_gpu && local_size != 1)
    {
      fprintf(stderr, "local workgroup size for CPU set to 1\n");
      local_size = 1;
    }

    if (local_size & (local_size-1))
    {
      fprintf(stderr, "local size %d is invalid, must be a power of two\n", (int) local_size);
      goto error;
    }

    if (kick_size & (kick_size-1))
    {
      fprintf(stderr, "kick size %d is invalid, must be a power of two\n", (int) kick_size);
      goto error;
    }

    printf("using kick size %d, local size %d\n", (int) kick_size, (int) local_size);

    state->options.local_size = (int) local_size;
    state->options.kick_size = (int) kick_size;
  }

  state->ocl_device = device;
  state->ocl_platform = platform;

  /* Note that nVidia's OpenCL requires the platform property */
  properties[1] = (cl_context_properties) platform;
  state->ocl_context = clCreateContext(properties, 1, &device, NULL, NULL, &error);
  ERRCHECK_GOTO(error, "couldn't create OpenCL context", error);

  state->ocl_queue = clCreateCommandQueue(state->ocl_context, device, 0, &error);
  ERRCHECK_GOTO(error, "couldn't create OpenCL command queue", error);

  if (state->options.max_k > MAX_COMBINATIONS)
  {
    fprintf(stderr, "warning: K of %d too high, limiting to %d\n",
        state->options.max_k, MAX_COMBINATIONS);
    state->options.max_k = MAX_COMBINATIONS;
  }

  state->pass_count = state->options.max_k - 1;

  /* Create compute kernels */
  for (i = 0; i < state->pass_count; ++i)
  {
    extern const char* kernel_src;
    char options[128];
    int comb_size = state->options.max_k - i;
    snprintf(options, sizeof options, "-DCOMB=%d", comb_size);

    state->passes[i].k = comb_size;

    printf("Compiling kernel %d/%d (K=%d)...\n",
        i + 1, state->pass_count, comb_size);

    if (NULL == (state->passes[i].program = BuildOpenClProgram(state->ocl_context, device, kernel_src, &error, options)))
      ERRCHECK_GOTO(error, "couldn't build OpenCL kernel", error);

    if (NULL == (state->passes[i].kernel = clCreateKernel(state->passes[i].program, "score_combinations", &error)))
      ERRCHECK_GOTO(error, "couldn't create OpenCL kernel", error);
  }

  return state;

error:
  if (state)
  {
    dedupe_destroy(state);
  }
  return NULL;
}

/*--------------------------------------------------------------------------*/

static void
dedupe_clear(struct dedupe_state *state)
{
  free_buffers(state);

  if (state->buckets)
  {
    uint32_t i, len;
    for (i = 0, len = state->bucket_count; i < len; ++i)
    {
      free(state->buckets[i].name);
      free(state->buckets[i].split_links);
    }

    free(state->buckets);
  }

  if (state->bucket_refs)
    free_aligned(state->bucket_refs);

  if (state->item_sizes)
    free_aligned(state->item_sizes);

  state->item_sizes = NULL;
  state->bucket_refs = NULL;
  state->buckets = NULL;
  state->item_count = 0;
  state->item_count_padded = 0;
  state->word_count = 0;
  state->bucket_count = 0;
  state->bucket_capacity = 0;
}

/*--------------------------------------------------------------------------*/

void
dedupe_destroy(struct dedupe_state* state)
{
  dedupe_clear(state);
  free(state);
}

/*--------------------------------------------------------------------------*/

static char *
load_file_data(const char* filename)
{
  FILE* f;
  size_t size;
  char* data;

  if (NULL == (f = fopen(filename, "rb")))
  {
    fprintf(stderr, "couldn't open %s for input\n", filename);
    return NULL;
  }

  fseek(f, 0, SEEK_END);
  size = (size_t) ftell(f);
  fseek(f, 0, SEEK_SET);

  if (NULL == (data = malloc(size + 1)))
  {
    fprintf(stderr, "out of memory allocating %d bytes\n", (int) size);
    fclose(f);
    return NULL;
  }

  if (1 != fread(data, size, 1, f))
  {
    fprintf(stderr, "couldn't read file data\n");
    free(data);
    fclose(f);
    return NULL;
  }

  data[size] = 0;

  return data;
}

/*--------------------------------------------------------------------------*/

static json_value *
parse_file(const char* filename)
{
  json_settings jsettings;
  json_value* j;
  char parse_error[512];
  char *data;
  int success;

  if (NULL == (data = load_file_data(filename)))
    return NULL;

  memset(&jsettings, 0, sizeof jsettings);

  success = NULL != (j = json_parse_ex(&jsettings, data, parse_error));

  free(data);

  if (!success)
  {
    fprintf(stderr, "couldn't parse %s: %s\n", filename, parse_error);
    return NULL;
  }
  else
  {
    return j;
  }
}

/*--------------------------------------------------------------------------*/

static json_value *
get_named_key(json_value *obj, const char *name, json_type type)
{
  unsigned int i, len;

  if (json_object != obj->type)
    return NULL;

  for (i = 0, len = obj->u.object.length; i < len; ++i)
  {
    if (0 == strcmp(obj->u.object.values[i].name, name))
    {
      json_value *v = obj->u.object.values[i].value;
      if (v->type == type)
        return v;
      else
        return NULL;
    }
  }

  return NULL;
}

/*--------------------------------------------------------------------------*/

#define ALIGNED_SIZE(size, alignment) \
  (((size) + ((alignment) - 1)) & (~(alignment - 1)))

/*--------------------------------------------------------------------------*/

static int size_location(int index)
{
  // Reference bits are shuffled accordingly:
  //   0   32  64  96
  //   1   33  65  97
  //   ...
  //   31  63  95 127
  //  128 160 192 224
  //  129 161 193 225
  //  ...
  //
  // therefore
  // bits 0-4 choose the local row inside the tile
  // bits 5-6 choose the column
  // bits 31-7 choose the vertical group

  int local_row = index & 31;
  int local_column = (index >> 5) & 3;
  int vertical_group = index & ~127;

  return (local_row << 2) | (local_column) | (vertical_group);
}

/*--------------------------------------------------------------------------*/

static int
fill_input(
   struct dedupe_state *state,
   json_value *items,
   json_value *buckets)
{
  uint32_t i, len;

  for (i = 0, len = state->item_count; i < len; ++i)
  {
    uint32_t location = size_location(i);

    json_value *item = items->u.array.values[i];

    if (json_integer != item->type)
      return 1;

    state->item_sizes[location] = (uint32_t) item->u.integer;
  }

  for (i = 0, len = state->bucket_count; i < len; ++i)
  {
    json_value *item = buckets->u.array.values[i];
    json_value *refs, *name;
    uint32_t ri, rlen;
    uint32_t *bit_base = state->bucket_refs + state->word_count * i;
    int32_t ref_count = 0;
    int64_t ref_size = 0;

    if (json_object != item->type)
      return 1;

    /* Allocate & copy bucket name */
    if (NULL == (name = get_named_key(item, "Name", json_string)))
      return 1;

    if (NULL == (state->buckets[i].name = malloc(name->u.string.length + 1)))
      return 1;

    memcpy(state->buckets[i].name, name->u.string.ptr,
        name->u.string.length + 1);

    /* Walk reference array and populate refs. */

    if (NULL == (refs = get_named_key(item, "Refs", json_array)))
      return 1;

    for (ri = 0, rlen = refs->u.array.length; ri < rlen; ++ri)
    {
      json_value *item = refs->u.array.values[ri];
      uint32_t ref_item;

      if (json_integer != item->type)
        return 1;

      ref_item = (uint32_t) item->u.integer;

      if (ref_item >= state->item_count)
      {
        fprintf(stderr, "item %u referenced from %s is out of bounds\n",
            ref_item, name->u.string.ptr);
        return 1;
      }

      ref_size += state->item_sizes[size_location(ref_item)];

      bit_base[ref_item >> 5] |= 1 << (ref_item & 31);
      ++ref_count;
    }

    /* Keep track of how many refs are in the bucket & how big it is */
    state->buckets[i].ref_count = ref_count;
    state->buckets[i].ref_size = ref_size;
  }

  return 0;
}

/*--------------------------------------------------------------------------*/

static int
assign_input(
   struct dedupe_state *state,
   json_value *items,
   json_value *buckets)
{
  size_t bucket_memory, size_memory, bucket_info_memory;

  state->item_count         = items->u.array.length;
  state->bucket_count       = buckets->u.array.length;
  state->bucket_capacity    = buckets->u.array.length + 64;
  state->item_count_padded  = ALIGNED_SIZE(state->item_count, 128);
  state->word_count         = state->item_count_padded / 32;

  /* Allocate memory. */
  bucket_memory = sizeof(uint32_t) * state->word_count * state->bucket_capacity;
  size_memory = sizeof(uint32_t) * state->item_count_padded;
  bucket_info_memory = sizeof(struct bucket_info) * state->bucket_capacity;

  state->item_sizes = alloc_aligned(size_memory);
  state->bucket_refs = alloc_aligned(bucket_memory);
  state->buckets = calloc(bucket_info_memory, 1);

  if (NULL == state->item_sizes ||
      NULL == state->bucket_refs ||
      NULL == state->buckets)
    goto error;

  memset(state->item_sizes, 0, size_memory);
  memset(state->bucket_refs, 0, bucket_memory);
  memset(state->buckets, 0, bucket_info_memory);

  if (0 != fill_input(state, items, buckets))
    goto error;

  return 0;

error:
  dedupe_clear(state);
  return 1;
}

/*--------------------------------------------------------------------------*/

int
dedupe_load_input(
    struct dedupe_state *state,
    const char *filename)
{
  int error = 1;
  json_value *j = NULL;
  json_value *items, *buckets;

  if (NULL == (j = parse_file(filename)))
    goto leave;

  if (NULL == (items = get_named_key(j, "Items", json_array)))
    goto bad_data;

  if (NULL == (buckets = get_named_key(j, "Buckets", json_array)))
    goto bad_data;

  error = assign_input(state, items, buckets);

  if (0 == error)
    goto leave;

bad_data:
  fprintf(stderr, "%s: Bad JSON structure\n", filename);

leave:
  if (j != NULL)
    json_value_free(j);

  return error;
}

/*--------------------------------------------------------------------------*/

static uint32_t *
alloc_bucket(struct dedupe_state *state, const char *name, struct bucket_info **new_bucket)
{
  uint32_t index = state->bucket_count;

  if (index == state->bucket_capacity)
  {
    uint32_t *refs;
    struct bucket_info *info;
    uint32_t capacity = state->bucket_capacity + 64;

    refs = realloc_aligned(state->bucket_refs, capacity * state->word_count * sizeof(uint32_t));
    info = realloc(state->buckets, capacity * sizeof(struct bucket_info));

    if (refs) state->bucket_refs = refs;
    if (info) state->buckets = info;

    if (!refs || !info) {
      return NULL;
    }

    state->bucket_capacity = capacity;
  }

  state->bucket_count = index + 1;
  assert(state->bucket_count <= state->bucket_capacity);

  *new_bucket = state->buckets + index;
  memset(*new_bucket, 0, sizeof(struct bucket_info));

  return state->bucket_refs + index * state->word_count;
}

/*--------------------------------------------------------------------------*/

static int deduplicate(struct dedupe_state *state, const cl_int* buckets, int bucket_count, int level) 
{
  int b;
  const uint32_t word_count = state->word_count;
  const size_t table_size = sizeof(uint32_t) * word_count;
  char name_buffer[64];
  uint32_t* scratch = (uint32_t*) alloca(table_size);
  uint32_t *output;
  struct bucket_info* new_bucket;
  size_t slen;
  uint32_t ref_count = 0;
  int64_t ref_size = 0;

  memset(scratch, 0xff, table_size);

  /* Allocate new bucket */
  if (NULL == (output = alloc_bucket(state, name_buffer, &new_bucket)))
    return 1;

  /* Set a name */
  snprintf(name_buffer, sizeof name_buffer, "dedupe%05u", state->bucket_count);
  slen = strlen(name_buffer) + 1;

  if (NULL == (new_bucket->name = malloc(slen)))
    return 1;

  memcpy(new_bucket->name, name_buffer, slen);

  /* Assign level */
  new_bucket->level = level + 1;
  new_bucket->split_count = 0;

  /* Compute common bitset of references */
  for (b = 0; b < bucket_count; ++b)
  {
    uint32_t i;
    const uint32_t* bucket_base = state->bucket_refs + buckets[b] * word_count;

    for (i = 0; i < word_count; ++i)
    {
      scratch[i] &= bucket_base[i];
    }
  }

  /* Compute the number of items being moved */
  {
    uint32_t i;
    for (i = 0; i < word_count; ++i)
    {
      int bit;
      uint32_t mask = 1;
      const uint32_t w = scratch[i];
      for (bit = 0; bit < 32; ++bit, mask <<= 1)
      {
        if (w & mask)
        {
          ref_size += state->item_sizes[size_location(i * 32 + bit)];
          ++ref_count;
        }
      }
    }
  }

  /* Clear ref bits for the items we dropped from these buckets, and bump their
   * split counts */
  for (b = 0; b < bucket_count; ++b)
  {
    uint32_t i;
    const int bucket_index = buckets[b];
    uint32_t* bucket_base = state->bucket_refs + bucket_index * word_count;
    struct bucket_info* bucket = state->buckets + bucket_index;
    int32_t* links = realloc(bucket->split_links, sizeof(int32_t) * (bucket->split_count + 1));

    if (!links)
      return 1;

    /* Append to links array going out from this bucket */
    bucket->ref_count -= ref_count;
    bucket->ref_size -= ref_size;
    bucket->split_links = links;
    links[bucket->split_count] = (int) (new_bucket - state->buckets);
    bucket->split_count += 1;

    for (i = 0; i < word_count; ++i)
    {
      bucket_base[i] ^= scratch[i];
    }
  }

  new_bucket->ref_count = ref_count;
  new_bucket->ref_size = ref_size;

  /* Copy ref bits to our new bucket */
  memcpy(output, scratch, table_size);

  return 0;
}

/*--------------------------------------------------------------------------*/

static void
free_buffers(struct dedupe_state *state)
{
  if (state->device_combinations) clReleaseMemObject(state->device_combinations);
  if (state->device_sizes) clReleaseMemObject(state->device_sizes);
  if (state->device_refs) clReleaseMemObject(state->device_refs);
  if (state->device_output) clReleaseMemObject(state->device_output);

  state->device_combinations = state->device_sizes = state->device_refs = state->device_output = NULL;

  free(state->host_combinations);
  free(state->host_scores);

  state->host_combinations = NULL;
  state->host_scores = NULL;
}

/*--------------------------------------------------------------------------*/

static int
setup_buffers(struct dedupe_state *state)
{
  int kick_size = state->options.kick_size;
  cl_context context = state->ocl_context;
  cl_int error;
  int i;

  /* Discard existing buffers */
  free_buffers(state);

  /* Allocate memory buffers.
   *
   * The algorithm needs the following buffers (for N combinations of K):
   *
   * combination buffer - space to hold input combination sequences (K * N * uint)
   * item size buffer - item sizes (32x4 group swizzled)
   * bucket bit buffer - bucket->item reference bits
   * output score buffer - stores results of compute buffer (N * uint)
   */

  state->device_combinations = clCreateBuffer(context, CL_MEM_READ_ONLY, kick_size * MAX_COMBINATIONS * sizeof(cl_uint), NULL, &error);
  ERRCHECK_GOTO(error, "couldn't allocate combination buffer", error);

  state->device_sizes = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
      state->item_count_padded * sizeof(cl_uint), state->item_sizes, &error);
  ERRCHECK_GOTO(error, "couldn't allocate item size buffer", error);

  state->device_refs = clCreateBuffer(context, CL_MEM_READ_ONLY,
      state->bucket_count * state->word_count * sizeof(cl_uint), NULL, &error);
  ERRCHECK_GOTO(error, "couldn't allocate ref bit buffer", error);

  state->device_output = clCreateBuffer(context, CL_MEM_WRITE_ONLY,
      kick_size * sizeof(cl_uint), NULL, &error);
  ERRCHECK_GOTO(error, "couldn't allocate output buffer", error);

  // Set up kernel parmeters. There never change so we just set them once.
  for (i = 0; i < state->pass_count; ++i)
  {
    cl_kernel k = state->passes[i].kernel;

    // Give the kernel the 128-wide word count.
    cl_int wordcount128 = state->word_count >> 2;

    clSetKernelArg(k, 0, sizeof(cl_mem), &state->device_combinations);
    clSetKernelArg(k, 1, sizeof(cl_mem), &state->device_sizes);
    clSetKernelArg(k, 2, sizeof(cl_mem), &state->device_refs);
    clSetKernelArg(k, 3, sizeof(cl_mem), &state->device_output);
    clSetKernelArg(k, 4, sizeof(cl_int), &wordcount128);
  }

  state->host_combinations = (cl_int*) malloc(kick_size * MAX_COMBINATIONS * sizeof(cl_int));
  state->host_scores = (cl_uint*) malloc(kick_size * sizeof(cl_uint));

  if (!state->host_combinations || !state->host_scores)
    goto error;

  return 0;

error:
  free_buffers(state);
  return 1;
}

/*--------------------------------------------------------------------------*/

static int
step_deduplication(
    struct dedupe_state *state,
    int pass_bucket_count,
    int bucket_count,
    const int32_t* in_buckets,
    cl_int best_combination[MAX_COMBINATIONS],
    uint64_t *best_score_out,
    int* best_k_out)
{
  uint64_t best_score = 0;
  int pass, pass_count;
  int best_K = 0;
  int kick_size = state->options.kick_size;
  cl_command_queue cq = state->ocl_queue;
  cl_int error;

  // Upload reference bit buffer as it changes between iterations.
  error = clEnqueueWriteBuffer(
      state->ocl_queue, state->device_refs, CL_FALSE, 0,
      pass_bucket_count * state->word_count * sizeof(cl_uint), state->bucket_refs, 0, NULL, NULL);

  ERRCHECK_GOTO(error, "couldn't write combination buffer", error);

  // Run each combination pass
  pass_count = state->pass_count;
  for (pass = 0; pass < pass_count; ++pass)
  {
    const int K = state->passes[pass].k;
    struct combgen gen;

    /* Make sure we have enough buckets to try this K */
    if (bucket_count < K)
      continue;

    combgen_init(&gen, bucket_count, K);

    // Generate runs of combinations.
    for (;;)
    {
      int x;
      int valid_combinations = combgen_iterate(&gen, state->host_combinations, kick_size, in_buckets);

      /* Pad global work size to an even multiple of the local work size. */
      size_t local_work_size = state->options.local_size;
      size_t global_work_size = (valid_combinations + local_work_size - 1) & ~(local_work_size - 1);

      if (0 == valid_combinations)
        break;

      /* Clear rest of buffer to -1 */
      memset(state->host_combinations + K * valid_combinations, 0xff, (kick_size - valid_combinations) * K * sizeof(cl_int));

      // Upload combination buffer (non-blocking)
      error = clEnqueueWriteBuffer(cq, state->device_combinations, CL_FALSE, 0, kick_size * K * sizeof(cl_int), state->host_combinations, 0, NULL, NULL);
      ERRCHECK_GOTO(error, "couldn't write combination buffer", error);

      // Execute the kernel.
      error = clEnqueueNDRangeKernel(cq, state->passes[pass].kernel, 1, NULL, &global_work_size, &local_work_size, 0, NULL, NULL);
      ERRCHECK_GOTO(error, "couldn't execute kernel", error);

      // Read the results back
      error = clEnqueueReadBuffer(cq, state->device_output, CL_TRUE, 0, kick_size * sizeof(cl_uint), state->host_scores, 0, NULL, NULL);
      ERRCHECK_GOTO(error, "couldn't read results back", error);

      // Select the best result, store that combination.
      for (x = 0; x < valid_combinations; ++x)
      {
        uint64_t savings = state->host_scores[x] * (K - 1);

        if (savings > best_score)
        {
          int i;
          best_K = K;
          best_score = savings;

          for (i = 0; i < K; ++i)
          {
            best_combination[i] = state->host_combinations[x * K + i];
          }
        }
      }
    }
  }

  if (state->options.verbosity > 0 && best_K > 0)
  {
    int i;

    printf("best score: kn-%4d/%2d - %9.2f MB (", bucket_count, best_K, best_score / (1024.0 * 1024.0));
    for (i = 0; i < best_K; ++i)
    {
      printf("%s%d", i > 0 ? "/" : "", best_combination[i]);
    }
    printf(")\n");
  }

  *best_k_out = best_K;
  *best_score_out = best_score;
  return 0;

error:
  return 1;
}

/*--------------------------------------------------------------------------*/

static int
find_eligible_buckets(struct dedupe_state* state, int32_t* eligible_buckets, int pass_buckets_count, int level)
{
  int i;
  int count = 0;
  const int max_splits = state->options.max_bucket_splits;
  const int merge_across_levels = state->options.merge_across_levels;
  const int64_t min_bucket_size = state->options.min_bucket_size;

  /* Consider buckets 0 .. pass_buckets_count */
  for (i = 0; i < pass_buckets_count; ++i)
  {
    struct bucket_info* info = state->buckets + i;

    /* Remove buckets that cannot be split any further */
    if (info->split_count >= max_splits)
      continue;

    /* Remove buckets smaller than the minimum size */
    if (info->ref_size <= min_bucket_size)
      continue;

    /* Remove buckets of the wrong level */
    if (!merge_across_levels && info->level != level)
      continue;

    /* OK; keep this bucket */
    eligible_buckets[count++] = i;
  }

  return count;
}

/*--------------------------------------------------------------------------*/

int
dedupe_run(struct dedupe_state *state)
{
  int level, max_levels;
  int iteration, max_iter;
  uint64_t min_gain = (uint64_t) (state->options.min_gain_mb * 1024 * 1024);

  max_levels = state->options.max_levels;

  for (level = 0; level < max_levels; ++level)
  {
    /* Keep track of how many buckets this level started out with. Each level
     * de-duplicates only from those buckets. */
    const int pass_bucket_count = state->bucket_count;

    /* Allocate space for indices of the buckets that are eligble to take part
     * in an iteration. We allocate the worst case (all buckets). */
    int32_t* eligible_buckets = malloc(sizeof(int32_t) * pass_bucket_count);
    if (eligible_buckets == NULL)
      return 1;

    printf("de-duplication running, level %d/%d - %d buckets...\n", level + 1, max_levels, pass_bucket_count);

    if (0 != setup_buffers(state))
    {
      free(eligible_buckets);
      return 1;
    }

    max_iter = state->options.max_iterations;
    for (iteration = 0; iteration < max_iter; ++iteration)
    {
      uint64_t score;
      int eligible_count;
      cl_int combination[MAX_COMBINATIONS];
      int k;

      eligible_count = find_eligible_buckets(state, eligible_buckets, pass_bucket_count, level);

      if (0 != step_deduplication(state, pass_bucket_count, eligible_count, eligible_buckets, combination, &score, &k))
      {
        free(eligible_buckets);
        return 1;
      }

      if (score < min_gain)
      {
        printf("aborting after %d iterations, gain lower than threshold\n", iteration + 1);
        break;
      }

      if (0 != deduplicate(state, combination, k, level))
      {
        free(eligible_buckets);
        return 1;
      }
    }

    free(eligible_buckets);
  }

  return 0;
}

/*--------------------------------------------------------------------------*/

int
dedupe_save_output(
    struct dedupe_state* state,
    const char* filename)
{
  uint32_t i, word;
  const uint32_t bucket_count = state->bucket_count;
  const uint32_t word_count = state->word_count;
  const uint32_t *refs;
  FILE* out = fopen(filename, "w");

  if (!out) {
    fprintf(stderr, "couldn't open %s for output\n", filename);
    return 1;
  }

  fprintf(out, "[\n");

  refs = state->bucket_refs;

  for (i = 0; i < bucket_count; ++i)
  {
    int print_count;
    struct bucket_info* bucket = state->buckets + i;
    const uint32_t link_count = bucket->split_count;

    fprintf(out, "  {\n");
    fprintf(out, "    \"Name\": \"%s\",\n", bucket->name);
    fprintf(out, "    \"Level\": %d,\n", bucket->level);
#ifndef _MSC_VER
    fprintf(out, "    \"SizeBytes\": %lld,\n", bucket->ref_size);
#else
    fprintf(out, "    \"SizeBytes\": %I64d,\n", bucket->ref_size);
#endif
    fprintf(out, "    \"SplitCount\": %d,\n", link_count);
    fprintf(out, "    \"SplitLinks\": [\n");

    {
      uint32_t link_iter;
      for (link_iter = 0; link_iter < link_count; ++link_iter)
      {
        fprintf(out, "          %u%s\n", bucket->split_links[link_iter], link_iter + 1 < link_count ? "," : "");
      }
    }

    fprintf(out, "    ],\n");

    fprintf(out, "    \"Refs\": [");

    print_count = 0;
    for (word = 0; word < word_count; ++word)
    {
      int bit;
      uint32_t mask = 1;
      const uint32_t w = *refs++;
      for (bit = 0; bit < 32; ++bit, mask <<= 1)
      {
        if (w & mask)
        {
          if (0 == (print_count & 0x7))
            fprintf(out, "\n       ");

          fprintf(out, "%s%d", print_count > 0 ? "," : "", word * 32 + bit);
          ++print_count;
        }
      }
    }

    fprintf(out, "\n    ]\n");
    fprintf(out, "  }%s\n", i + 1 < bucket_count ? "," : "");
  }

  fprintf(out, "]\n");
  fclose(out);
  return 0;
}

/*--------------------------------------------------------------------------*/

static double
compute_total_size(struct dedupe_state *state)
{
  uint64_t sum = 0;
  uint32_t i, count;

  for (i = 0, count = state->bucket_count; i < count; ++i)
  {
    sum += state->buckets[i].ref_size;
  }
  
  return sum / (1024.0 * 1024.0);
}

/*--------------------------------------------------------------------------*/

void
dedupe_print_summary(struct dedupe_state *state, const char* label)
{
  printf("De-duplication %s summary:\n", label);

  printf("  Number of buckets: %9d\n", state->bucket_count);
  printf("  Number of items:   %9d     (32-bit state words: %u)\n", state->item_count, state->word_count);

  printf("  Total data size:   %9.2f MB\n", compute_total_size(state));
}

/*--------------------------------------------------------------------------*/

static int seek_count(struct dedupe_state *state, int visited[], int bucket)
{
  int i;
  int sum = 0;
  const struct bucket_info* b = state->buckets + bucket;

  /* Already payed for this bucket */
  if (visited[bucket])
    return sum;

  /* Assume one seek to get to the bucket */
  sum = 1;

  for (i = 0; i < b->split_count; ++i)
  {
    visited[bucket] = 1;

    sum += seek_count(state, visited, (int) b->split_links[i]);
  }

  return sum;
}

/*--------------------------------------------------------------------------*/

void
dedupe_print_seek_summary(struct dedupe_state *state)
{
  uint32_t i;
  int* scratch = alloca(sizeof(int) * state->bucket_count);

  if (!scratch)
    return;

  printf("Seeks | Bucket\n");

  for (i = 0; i < state->bucket_count; ++i)
  {
    /* Break as soon as we see a non-level 0 (generated) bucket */
    if (state->buckets[i].level != 0)
      break;

    memset(scratch, 0, sizeof(int) * state->bucket_count);

    printf("%5d | %s\n", seek_count(state, scratch, i), state->buckets[i].name);
  }
}
