#pragma once

#include "Pipeliner.h"
#include "TextureFactory.h"
#include <glm/glm.hpp>

class Skybox
{
public:

    struct Vertex
    {
        glm::vec3 pos;

        static constexpr VkVertexInputBindingDescription getBindingDescription()
        {
            VkVertexInputBindingDescription bindingDescription{};
            bindingDescription.binding = 0;
            bindingDescription.stride = sizeof(Vertex);
            bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            return bindingDescription;
        }
        static constexpr VkVertexInputAttributeDescription getAttributeDescription()
        {
            VkVertexInputAttributeDescription attributeDescription{};
            attributeDescription.binding = 0;
            attributeDescription.location = 0;
            attributeDescription.format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescription.offset = offsetof(Vertex, pos);
            return attributeDescription;
        }
    };

    Skybox() = default;

    void init(std::string_view vertShader, std::string_view fragShader, uint32_t width, uint32_t height,
        VkDescriptorSetLayout descriptorSetLayout, VkRenderPass renderPass, VkDevice device,
        VkPhysicalDevice physicalDevice, VkCommandPool cmdBufPool, VkQueue queue,
        const std::array<std::string_view, 6>& textureFileNames,
        std::function<uint16_t(std::weak_ptr<TextureFactory::Texture>, VkSampler)> descriptorCreator);

private:

    Pipeliner::pipeline_ptr m_pipeLine = { nullptr, nullptr };
    std::uint32_t m_realMaterialId = 0u;
    VkDeviceSize m_verticesBufferOffset = 0;
    VkBuffer m_generalBuffer = nullptr;
    VkDeviceMemory m_generalBufferMemory = nullptr;
};