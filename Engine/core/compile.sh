#!/bin/sh
echo "shaders compiling"
glslc shadersSRC/gPass.vert -o shaders/vert_gPass.spv
glslc shadersSRC/gPass.frag -o shaders/frag_gPass.spv

glslc shadersSRC/gLigtingSubpass.vert -o shaders/vert_gLigtingSubpass.spv
glslc shadersSRC/gLigtingSubpass.frag -o shaders/frag_gLigtingSubpass.spv

glslc shadersSRC/skybox.vert -o shaders/vert_skybox.spv
glslc shadersSRC/skybox.frag -o shaders/frag_skybox.spv

glslc shadersSRC/fxaa.vert -o shaders/vert_fxaa.spv
glslc shadersSRC/fxaa.frag -o shaders/frag_fxaa.spv

glslc shadersSRC/shadowMap.vert -o shaders/vert_shadowMap.spv
glslc shadersSRC/shadowMap.frag -o shaders/frag_shadowMap.spv

glslc shadersSRC/terrain.vert -o shaders/vert_terrain.spv
glslc shadersSRC/terrain.frag -o shaders/frag_terrain.spv

glslc shadersSRC/particle.vert -o shaders/vert_particle.spv
glslc shadersSRC/particle.frag -o shaders/frag_particle.spv
pause
