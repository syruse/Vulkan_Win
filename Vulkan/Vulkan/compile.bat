%VULKAN_SDK%/Bin/glslc.exe gPass.vert -o shaders/vert_gPass.spv
%VULKAN_SDK%/Bin/glslc.exe gPass.frag -o shaders/frag_gPass.spv

%VULKAN_SDK%/Bin/glslc.exe gLigtingSubpass.vert -o shaders/vert_gLigtingSubpass.spv
%VULKAN_SDK%/Bin/glslc.exe gLigtingSubpass.frag -o shaders/frag_gLigtingSubpass.spv

%VULKAN_SDK%/Bin/glslc.exe skybox.vert -o shaders/vert_skybox.spv
%VULKAN_SDK%/Bin/glslc.exe skybox.frag -o shaders/frag_skybox.spv

%VULKAN_SDK%/Bin/glslc.exe fxaa.vert -o shaders/vert_fxaa.spv
%VULKAN_SDK%/Bin/glslc.exe fxaa.frag -o shaders/frag_fxaa.spv
pause