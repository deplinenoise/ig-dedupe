
dedupe - OpenCL based optical disc layout tool
==============================================

The most common way to get good loading speed from optical media is to
duplicate files next to each other, so a seek is not necessary to find them. In
the most extreme scenario, every file is duplicated for every time it is
referenced.

While fully duplicating files in this way leads to the best possible load
times, it's often not practical because of media limitations. Therefore, it is
desired to find large chunks of commonly referenced assets that can be
de-duplicated, that is, moved to a common area which reduces the disc
footprint. Each such de-duplication move introduces one seek per bucket that
donated to the new bucket.

Dedupe aims to help with this problem by automatically selecting the
de-duplication moves without human intervention. It does so by finding the
largest (in bytes) set of items that are shared by the largest number of
buckets. These items are then de-duplicated to their own bucket and the
algorithm continues.

Usage
-----

* Prepare an input JSON file that lists the following things:
  - Items - These are file sizes.
  - Buckets - These are groups of indices into your items

* Run dedupe on your input. There are many options controlling its operation.

* Feed the output JSON file back into your disc build process.

Algorithm
---------

Dedupe works by executing brute force searches over the N-K combination space
between all the input buckets to find the next biggest benefit for
de-duplication. You select both N (the number of buckets) and K (the
combination count). For example, at N = 100 and K = 3, dedupe needs 161,700
computations to exhaust the search space. Going to K = 4 or 5 increases the
computational requirements rapidly. The highest K supported is 6, and that's
ridiculously slow on current hardware (hours to run.)

This operation is surrounded by a loop that lets this process run with the
initial bucket pool until it terminates. The bucket pool is then expanded with
all the newly split off buckets and the process is repeated, until the maximum
level count is reached.

Options
-------

Here are some of the most useful options:

* -k (number) -- Set the maximum combination count
* -levels (number) -- Caps the number of de-duplication levels to (height of the merge tree)
* -gain (float) -- Specify the minimum gain in MB required to be able to make a split
* -maxsplits (number)  -- max # of splits for a single bucket before it becomes ineligible
* -v -- Be verbose
* -v -v -- Be even more verbose

Tweak options controlling OpenCL kernel execution:

* -kicksize (number) -- Set the global work size for the OpenCL kernel
* -localsize (number) -- Set the local work group size for the OpenCL kernel
* -gpu 1/0 -- Allow GPU execution or not (default to 1)

For even more options see --help or try reading main.c/dedupe.c

Example Data
------------

Three example data files are provided:

* pair.json - Just two buckets, to verify program operation.
* triplet.json - Three buckets, also useful to verify program operation.
* game.json - Example data from an actual game-sized problem. This file has 86
  buckets (could be levels/regions/zones) and 12533 items (assets/things).

Future Improvements
-------------------

The host parts (combination generator and scoring) could be done in a
double-buffered fashion to avoid stalling the GPU. 

License
-------

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

Includes json-parser software:

Copyright (C) 2012 James McLaughlin et al.  All rights reserved.
https://github.com/udp/json-parser

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

1. Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
SUCH DAMAGE.

