:: "no optimization". This level generates the most debuggable code.
SET NoOptimizationFlag=-O0
:: default optimization level for better performance
SET PerfOptimizationFlag=-O

SET OptimizationFlag=%NoOptimizationFlag%


%VULKAN_SDK%/Bin/glslc.exe %OptimizationFlag% shadersSRC/gPass.vert -o shaders/vert_gPass.spv
%VULKAN_SDK%/Bin/glslc.exe %OptimizationFlag% shadersSRC/gPass.frag -o shaders/frag_gPass.spv

%VULKAN_SDK%/Bin/glslc.exe %OptimizationFlag% shadersSRC/gLigtingSubpass.vert -o shaders/vert_gLigtingSubpass.spv
%VULKAN_SDK%/Bin/glslc.exe %OptimizationFlag% shadersSRC/gLigtingSubpass.frag -o shaders/frag_gLigtingSubpass.spv

%VULKAN_SDK%/Bin/glslc.exe %OptimizationFlag% shadersSRC/skybox.vert -o shaders/vert_skybox.spv
%VULKAN_SDK%/Bin/glslc.exe %OptimizationFlag% shadersSRC/skybox.frag -o shaders/frag_skybox.spv

%VULKAN_SDK%/Bin/glslc.exe %OptimizationFlag% shadersSRC/fxaa.vert -o shaders/vert_fxaa.spv
%VULKAN_SDK%/Bin/glslc.exe %OptimizationFlag% shadersSRC/fxaa.frag -o shaders/frag_fxaa.spv

%VULKAN_SDK%/Bin/glslc.exe %OptimizationFlag% shadersSRC/shadowMap.vert -o shaders/vert_shadowMap.spv
%VULKAN_SDK%/Bin/glslc.exe %OptimizationFlag% shadersSRC/shadowMap.frag -o shaders/frag_shadowMap.spv

%VULKAN_SDK%/Bin/glslc.exe %OptimizationFlag% shadersSRC/terrain.vert -o shaders/vert_terrain.spv
%VULKAN_SDK%/Bin/glslc.exe %OptimizationFlag% shadersSRC/terrain.frag -o shaders/frag_terrain.spv
%VULKAN_SDK%/Bin/glslc.exe %OptimizationFlag% shadersSRC/terrain.tese -o shaders/tessEval_terrain.spv
%VULKAN_SDK%/Bin/glslc.exe %OptimizationFlag% shadersSRC/terrain.tesc -o shaders/tessCtrl_terrain.spv

%VULKAN_SDK%/Bin/glslc.exe %OptimizationFlag% shadersSRC/particle.vert -o shaders/vert_particle.spv
%VULKAN_SDK%/Bin/glslc.exe %OptimizationFlag% shadersSRC/particle.frag -o shaders/frag_particle.spv

%VULKAN_SDK%/Bin/glslc.exe %OptimizationFlag% shadersSRC/gaussXBlur.vert -o shaders/vert_gaussXBlur.spv
%VULKAN_SDK%/Bin/glslc.exe %OptimizationFlag% shadersSRC/gaussXBlur.frag -o shaders/frag_gaussXBlur.spv

%VULKAN_SDK%/Bin/glslc.exe %OptimizationFlag% shadersSRC/gaussYBlur.vert -o shaders/vert_gaussYBlur.spv
%VULKAN_SDK%/Bin/glslc.exe %OptimizationFlag% shadersSRC/gaussYBlur.frag -o shaders/frag_gaussYBlur.spv

%VULKAN_SDK%/Bin/glslc.exe %OptimizationFlag% shadersSRC/bloom.vert -o shaders/vert_bloom.spv
%VULKAN_SDK%/Bin/glslc.exe %OptimizationFlag% shadersSRC/bloom.frag -o shaders/frag_bloom.spv

%VULKAN_SDK%/Bin/glslc.exe %OptimizationFlag% shadersSRC/depthWriter.vert -o shaders/vert_depthWriter.spv
%VULKAN_SDK%/Bin/glslc.exe %OptimizationFlag% shadersSRC/depthWriter.frag -o shaders/frag_depthWriter.spv

%VULKAN_SDK%/Bin/glslc.exe %OptimizationFlag% shadersSRC/ssao.vert -o shaders/vert_ssao.spv
%VULKAN_SDK%/Bin/glslc.exe %OptimizationFlag% shadersSRC/ssao.frag -o shaders/frag_ssao.spv

%VULKAN_SDK%/Bin/glslc.exe %OptimizationFlag% shadersSRC/footprint.vert -o shaders/vert_footPrint.spv
%VULKAN_SDK%/Bin/glslc.exe %OptimizationFlag% shadersSRC/footprint.frag -o shaders/frag_footPrint.spv

%VULKAN_SDK%/Bin/glslc.exe %OptimizationFlag% shadersSRC/ssaoBlur.vert -o shaders/vert_ssaoBlur.spv
%VULKAN_SDK%/Bin/glslc.exe %OptimizationFlag% shadersSRC/ssaoBlur.frag -o shaders/frag_ssaoBlur.spv

%VULKAN_SDK%/Bin/glslc.exe %OptimizationFlag% shadersSRC/semi_transparent.vert -o shaders/vert_semi_transparent.spv
%VULKAN_SDK%/Bin/glslc.exe %OptimizationFlag% shadersSRC/semi_transparent.frag -o shaders/frag_semi_transparent.spv
pause