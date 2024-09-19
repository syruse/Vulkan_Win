%VULKAN_SDK%/Bin/glslc.exe shadersSRC/gPass.vert -o shaders/vert_gPass.spv
%VULKAN_SDK%/Bin/glslc.exe shadersSRC/gPass.frag -o shaders/frag_gPass.spv

%VULKAN_SDK%/Bin/glslc.exe shadersSRC/gLigtingSubpass.vert -o shaders/vert_gLigtingSubpass.spv
%VULKAN_SDK%/Bin/glslc.exe shadersSRC/gLigtingSubpass.frag -o shaders/frag_gLigtingSubpass.spv

%VULKAN_SDK%/Bin/glslc.exe shadersSRC/skybox.vert -o shaders/vert_skybox.spv
%VULKAN_SDK%/Bin/glslc.exe shadersSRC/skybox.frag -o shaders/frag_skybox.spv

%VULKAN_SDK%/Bin/glslc.exe shadersSRC/fxaa.vert -o shaders/vert_fxaa.spv
%VULKAN_SDK%/Bin/glslc.exe shadersSRC/fxaa.frag -o shaders/frag_fxaa.spv

%VULKAN_SDK%/Bin/glslc.exe shadersSRC/shadowMap.vert -o shaders/vert_shadowMap.spv
%VULKAN_SDK%/Bin/glslc.exe shadersSRC/shadowMap.frag -o shaders/frag_shadowMap.spv

%VULKAN_SDK%/Bin/glslc.exe shadersSRC/terrain.vert -o shaders/vert_terrain.spv
%VULKAN_SDK%/Bin/glslc.exe shadersSRC/terrain.frag -o shaders/frag_terrain.spv
%VULKAN_SDK%/Bin/glslc.exe shadersSRC/terrain.tese -o shaders/tessEval_terrain.spv
%VULKAN_SDK%/Bin/glslc.exe shadersSRC/terrain.tesc -o shaders/tessCtrl_terrain.spv

%VULKAN_SDK%/Bin/glslc.exe shadersSRC/particle.vert -o shaders/vert_particle.spv
%VULKAN_SDK%/Bin/glslc.exe shadersSRC/particle.frag -o shaders/frag_particle.spv

%VULKAN_SDK%/Bin/glslc.exe shadersSRC/gaussXBlur.vert -o shaders/vert_gaussXBlur.spv
%VULKAN_SDK%/Bin/glslc.exe shadersSRC/gaussXBlur.frag -o shaders/frag_gaussXBlur.spv

%VULKAN_SDK%/Bin/glslc.exe shadersSRC/gaussYBlur.vert -o shaders/vert_gaussYBlur.spv
%VULKAN_SDK%/Bin/glslc.exe shadersSRC/gaussYBlur.frag -o shaders/frag_gaussYBlur.spv

%VULKAN_SDK%/Bin/glslc.exe shadersSRC/bloom.vert -o shaders/vert_bloom.spv
%VULKAN_SDK%/Bin/glslc.exe shadersSRC/bloom.frag -o shaders/frag_bloom.spv

%VULKAN_SDK%/Bin/glslc.exe shadersSRC/depthWriter.vert -o shaders/vert_depthWriter.spv
%VULKAN_SDK%/Bin/glslc.exe shadersSRC/depthWriter.frag -o shaders/frag_depthWriter.spv

%VULKAN_SDK%/Bin/glslc.exe shadersSRC/ssao.vert -o shaders/vert_ssao.spv
%VULKAN_SDK%/Bin/glslc.exe shadersSRC/ssao.frag -o shaders/frag_ssao.spv

%VULKAN_SDK%/Bin/glslc.exe shadersSRC/footprint.vert -o shaders/vert_footPrint.spv
%VULKAN_SDK%/Bin/glslc.exe shadersSRC/footprint.frag -o shaders/frag_footPrint.spv

%VULKAN_SDK%/Bin/glslc.exe shadersSRC/ssaoBlur.vert -o shaders/vert_ssaoBlur.spv
%VULKAN_SDK%/Bin/glslc.exe shadersSRC/ssaoBlur.frag -o shaders/frag_ssaoBlur.spv
pause