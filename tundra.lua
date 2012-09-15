--[[
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
]]--

local common = {
	Env = {
		CCOPTS = {
			{ "/W4", "/WX", "/wd4127", "/wd4100", "/wd4324"; Config = "*-msvc-*" },
			{ "-Wall", "-Werror"; Config = { "*-gcc-*", "*-clang-*", "*-crosswin32" } },
			{ "-g"; Config = { "*-gcc-debug", "*-clang-debug", "*-gcc-production", "*-clang-production" } },
			{ "-O2"; Config = { "*-gcc-production", "*-clang-production", "*-crosswin32-production" } },
			{ "-O3"; Config = { "*-gcc-release", "*-clang-release", "*-crosswin32-release" } },
			{ "/O2"; Config = "*-msvc-production" },
			{ "/Ox"; Config = "*-msvc-release" },
		},
		CPPDEFS = {
			{ "_CRT_SECURE_NO_WARNINGS"; Config = "*-msvc-*"  },
			{ "NDEBUG"; Config = "*-*-release"  },
		},
    GENERATE_PDB = "1",
	}
}

Build {
	Configs = {
		Config {
			Name = "generic-gcc",
			DefaultOnHost = "linux",
			Tools = { "gcc" },
      Inherit = common,
		},
		Config {
			Name = "macosx-gcc",
			DefaultOnHost = "macosx",
			Tools = { "gcc-osx" },
      Inherit = common,
		},
		Config {
			Name = "win64-msvc",
			DefaultOnHost = "windows",
			Tools = {
				{ "msvc-winsdk"; TargetArch = "x64", VcVersion = "10.0" }
			},
      Env = {
        -- This is hardcoded to build with the AMD APP SDK in a special
        -- location. Change as necessary.
        AMD_APP_SDK = "x:\\sdk\\amd_app_sdk\\2.7",
        CPPPATH = "$(AMD_APP_SDK)\\include",
        LIBPATH = "$(AMD_APP_SDK)\\lib\\x86_64",
      },
      Inherit = common,
		},
	},
  Passes = {
    CodeGeneration = { BuildOrder = 1, Name = "Code Generation" },
  },
	Units = function()

		-- A rule to pack a source file into a C string.
		DefRule {
			Name               = "EmbedKernel",
			Pass               = "CodeGeneration",
			ConfigInvariant    = true,
			Command            = "perl embedfile.pl $(<) $(@) $(SYMBOL)",
			ImplicitInputs     = { "embedfile.pl" },
			Blueprint = {
				Input = { Type = "string", Required = true },
				Output = { Type = "string", Required = true },
				Symbol = { Type = "string", Required = true },
			},
			Setup = function (env, data)
        env:set("SYMBOL", data.Symbol)
				return {
					InputFiles = { data.Input },
					OutputFiles = { "$(OBJECTROOT)/" .. data.Output },
				}
			end,
		}

		local dedupe = Program {
			Name = "dedupe",
			Sources = {
        "main.c",
        "ocl_util.c",
        "combgen.c",
        "dedupe.c",
        "json.c",
        EmbedKernel { Input = "kernel.opencl", Output = "kernel.c", Symbol = "kernel_src" },
      },
      Libs = {
        { "OpenCL.lib"; Config = "win64-msvc-*" },
      },
      Frameworks = { "OpenCL" },
		}

		Default(dedupe)
	end,
}
