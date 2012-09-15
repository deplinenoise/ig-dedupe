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

#include "ocl_util.h"

#include <stdio.h>
#include <string.h>

struct OpenCLDevice
{
  cl_platform_id platform;
  cl_device_id device;
};

int SelectOpenClDevice(
    cl_device_id        *out_device,
    cl_platform_id      *out_platform,
    int                 allow_gpu,
    const char          *preferred_platform,
    const char          *preferred_device)
{
  enum
  {
    kMaxDevices = 8,
    kMaxPlatforms = 8,
  };

  cl_int error;
  cl_platform_id platforms[kMaxPlatforms];
  cl_uint platform_count;
  cl_uint i;

  int gpu_device_count = 0;
  int cpu_device_count = 0;

  struct OpenCLDevice gpu_devices[kMaxDevices];
  struct OpenCLDevice cpu_devices[kMaxDevices];

  // Fetch the Platform and Device IDs; we only want one.
  error = clGetPlatformIDs(kMaxPlatforms, platforms, &platform_count);

  if (CL_SUCCESS != error)
    fprintf(stderr, "couldn't get platform ids: %s\n", OpenClErrorString(error));

  for (i = 0; i < platform_count; ++i)
  {
    cl_uint di;
    cl_uint device_count;
    cl_device_id devices[kMaxDevices];
    char platform_name[256];
    char platform_vendor[256];

    error = clGetPlatformInfo(platforms[i], CL_PLATFORM_VENDOR, sizeof(platform_vendor), platform_vendor, NULL);
    if (CL_SUCCESS != error) {
      fprintf(stderr, "warning: couldn't get platform vendor: %s\n", OpenClErrorString(error));
      continue;
    }

    error = clGetPlatformInfo(platforms[i], CL_PLATFORM_NAME, sizeof(platform_name), platform_name, NULL);
    if (CL_SUCCESS != error) {
      fprintf(stderr, "warning: couldn't get platform name: %s\n", OpenClErrorString(error));
      continue;
    }

    if (preferred_platform && preferred_platform[0])
    {
      if (0 != strcmp(platform_name, preferred_platform))
      {
        continue;
      }
    }

    error = clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_GPU | CL_DEVICE_TYPE_CPU, kMaxDevices, devices, &device_count);

    if (CL_SUCCESS != error) {
      fprintf(stderr, "warning: couldn't get device ids: %s\n", OpenClErrorString(error));
      continue;
    }

    printf("Found OpenCL platform: %s (%s) - %d devices\n", platform_name, platform_vendor, (int) device_count);

    for (di = 0; di < device_count; ++di)
    {
      char device_name[256];
      cl_device_type device_type;
      cl_ulong const_buffer_max;
      cl_ulong work_group_size_max;

      error = clGetDeviceInfo(devices[di], CL_DEVICE_TYPE, sizeof device_type, &device_type, NULL);
      if (CL_SUCCESS != error) {
        fprintf(stderr, "warning: couldn't get device type: %s\n", OpenClErrorString(error));
        continue;
      }

      error = clGetDeviceInfo(devices[di], CL_DEVICE_NAME, sizeof device_name, device_name, NULL);
      if (CL_SUCCESS != error) {
        fprintf(stderr, "warning: couldn't get device name: %s\n", OpenClErrorString(error));
        continue;
      }

      if (preferred_device && preferred_device[0])
      {
        if (0 != strcmp(device_name, preferred_device))
        {
          continue;
        }
      }

      printf(" - %s: %s\n", device_type & CL_DEVICE_TYPE_GPU ? "GPU" : "CPU", device_name);

      clGetDeviceInfo(devices[di], CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE, sizeof const_buffer_max, &const_buffer_max, NULL);
      clGetDeviceInfo(devices[di], CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof work_group_size_max, &work_group_size_max, NULL);

      printf("   - Max constant buffer size: %d\n", (int) const_buffer_max);
      printf("   - Max work-group size:      %d\n", (int) work_group_size_max);

      if (device_type & CL_DEVICE_TYPE_GPU)
      {
        if (gpu_device_count < kMaxDevices)
        {
          int index = gpu_device_count++;
          gpu_devices[index].device = devices[di];
          gpu_devices[index].platform = platforms[i];
        }
      }
      else
      {
        if (cpu_device_count < kMaxDevices)
        {
          int index = cpu_device_count++;
          cpu_devices[index].device = devices[di];
          cpu_devices[index].platform = platforms[i];
        }
      }
    }
  }

  printf("Found %d GPU device%s, %d CPU device%s\n",
    gpu_device_count, gpu_device_count > 1 ? "s" : "",
    cpu_device_count, cpu_device_count > 1 ? "s" : "");

  if (allow_gpu && gpu_device_count > 0)
  {
    *out_device = gpu_devices[0].device;
    *out_platform = gpu_devices[0].platform;
    return 0;
  }
  else if (cpu_device_count > 0)
  {
    *out_device = cpu_devices[0].device;
    *out_platform = cpu_devices[0].platform;
    return 0;
  }
  else
    return 1;
}

cl_program BuildOpenClProgram(
    cl_context context,
    cl_device_id device,
    const char* source,
    cl_int* error,
    const char* options)
{
  cl_program prog = clCreateProgramWithSource(context, 1, &source, NULL, error);

  if (CL_SUCCESS != *error) {
    fprintf(stderr, "Failed to create program: %s\n", OpenClErrorString(*error));
    return NULL;
  }

  // and compile it (after this we could extract the compiled version)
  *error = clBuildProgram(prog, 0, NULL, options, NULL, NULL);

  if (CL_SUCCESS != *error)
  {
    char build_log[14000];

    if (CL_SUCCESS == clGetProgramBuildInfo(prog, device, CL_PROGRAM_BUILD_LOG, sizeof build_log, build_log, NULL))
      fprintf(stderr, "Failed to build program\n\n%s\n", build_log);
    else
      fprintf(stderr, "Failed to build program: %s\n", OpenClErrorString(*error));

    return NULL;
  }

  return prog;
}

const char* OpenClErrorString(cl_int error)
{
  switch (error) {
    case CL_SUCCESS:                            return "Success!";
    case CL_DEVICE_NOT_FOUND:                   return "Device not found.";
    case CL_DEVICE_NOT_AVAILABLE:               return "Device not available";
    case CL_COMPILER_NOT_AVAILABLE:             return "Compiler not available";
    case CL_MEM_OBJECT_ALLOCATION_FAILURE:      return "Memory object allocation failure";
    case CL_OUT_OF_RESOURCES:                   return "Out of resources";
    case CL_OUT_OF_HOST_MEMORY:                 return "Out of host memory";
    case CL_PROFILING_INFO_NOT_AVAILABLE:       return "Profiling information not available";
    case CL_MEM_COPY_OVERLAP:                   return "Memory copy overlap";
    case CL_IMAGE_FORMAT_MISMATCH:              return "Image format mismatch";
    case CL_IMAGE_FORMAT_NOT_SUPPORTED:         return "Image format not supported";
    case CL_BUILD_PROGRAM_FAILURE:              return "Program build failure";
    case CL_MAP_FAILURE:                        return "Map failure";
    case CL_INVALID_VALUE:                      return "Invalid value";
    case CL_INVALID_DEVICE_TYPE:                return "Invalid device type";
    case CL_INVALID_PLATFORM:                   return "Invalid platform";
    case CL_INVALID_DEVICE:                     return "Invalid device";
    case CL_INVALID_CONTEXT:                    return "Invalid context";
    case CL_INVALID_QUEUE_PROPERTIES:           return "Invalid queue properties";
    case CL_INVALID_COMMAND_QUEUE:              return "Invalid command queue";
    case CL_INVALID_HOST_PTR:                   return "Invalid host pointer";
    case CL_INVALID_MEM_OBJECT:                 return "Invalid memory object";
    case CL_INVALID_IMAGE_FORMAT_DESCRIPTOR:    return "Invalid image format descriptor";
    case CL_INVALID_IMAGE_SIZE:                 return "Invalid image size";
    case CL_INVALID_SAMPLER:                    return "Invalid sampler";
    case CL_INVALID_BINARY:                     return "Invalid binary";
    case CL_INVALID_BUILD_OPTIONS:              return "Invalid build options";
    case CL_INVALID_PROGRAM:                    return "Invalid program";
    case CL_INVALID_PROGRAM_EXECUTABLE:         return "Invalid program executable";
    case CL_INVALID_KERNEL_NAME:                return "Invalid kernel name";
    case CL_INVALID_KERNEL_DEFINITION:          return "Invalid kernel definition";
    case CL_INVALID_KERNEL:                     return "Invalid kernel";
    case CL_INVALID_ARG_INDEX:                  return "Invalid argument index";
    case CL_INVALID_ARG_VALUE:                  return "Invalid argument value";
    case CL_INVALID_ARG_SIZE:                   return "Invalid argument size";
    case CL_INVALID_KERNEL_ARGS:                return "Invalid kernel arguments";
    case CL_INVALID_WORK_DIMENSION:             return "Invalid work dimension";
    case CL_INVALID_WORK_GROUP_SIZE:            return "Invalid work group size";
    case CL_INVALID_WORK_ITEM_SIZE:             return "Invalid work item size";
    case CL_INVALID_GLOBAL_OFFSET:              return "Invalid global offset";
    case CL_INVALID_EVENT_WAIT_LIST:            return "Invalid event wait list";
    case CL_INVALID_EVENT:                      return "Invalid event";
    case CL_INVALID_OPERATION:                  return "Invalid operation";
    case CL_INVALID_GL_OBJECT:                  return "Invalid OpenGL object";
    case CL_INVALID_BUFFER_SIZE:                return "Invalid buffer size";
    case CL_INVALID_MIP_LEVEL:                  return "Invalid mip-map level";
    default: return "Unknown";
  }
}

