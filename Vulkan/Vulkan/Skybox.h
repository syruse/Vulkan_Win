#pragma once

#include "Pipeliner.h"

class Skybox
{
public:
    Skybox() = default;

    void init(std::string_view vertShader, std::string_view fragShader, uint32_t width, uint32_t height, 
    VkDescriptorSetLayout descriptorSetLayout, VkRenderPass renderPass, VkDevice device);

private:

    Pipeliner::pipeline_ptr m_pipeLine = { nullptr, nullptr };
};