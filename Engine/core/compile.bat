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
pause