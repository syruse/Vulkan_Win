#pragma once

#include "Pipeliner.h"

class Skybox
{
public:
    Skybox() = default;

    void init(std::string_view vertShader, std::string_view fragShader, uint32_t width, uint32_t height, 
              VkDescriptorSetLayout descriptorSetLayout, VkRenderPass renderPass,
              VkPhysicalDevice physicalDevice, VkCommandPool cmdBufPool, VkQueue queue,
              const std::array<std::string_view, 6>& textureFileNames,
              std::function<uint16_t(std::weak_ptr<TextureFactory::Texture>, VkSampler)> descriptorCreator);

private:

    Pipeliner::pipeline_ptr m_pipeLine = { nullptr, nullptr };
    std::uint32_t m_realMaterialId = 0u;;
};