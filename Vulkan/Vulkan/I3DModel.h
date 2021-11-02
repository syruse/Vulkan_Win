#pragma once

#include <array>
#include <string>
#include <glm/glm.hpp>
#include "vulkan/vulkan.h"
#include "glm/gtx/hash.hpp"
#include "TextureFactory.h"
#include "PipelineCreatorBase.h"
#include <map>

class I3DModel
{
public:

    static constexpr std::string_view MODEL_DIR = "models";

    struct Material
    {
        std::weak_ptr<TextureFactory::Texture> texture;
        VkSampler sampler;
        VkDescriptorSetLayout descriptorSetLayout;
        std::vector<VkDescriptorSet> descriptorSets{}; /// separate set for each swapchain image
    };

    struct DynamicUniformBufferObject
    {
        alignas(16) glm::mat4 model;
    };

    struct Vertex
    {
        glm::vec3 pos;
        glm::vec3 normal;
        glm::vec2 texCoord;

        bool operator==(const Vertex &other) const
        {
            return pos == other.pos && normal == other.normal && texCoord == other.texCoord;
        }

        static VkVertexInputBindingDescription getBindingDescription()
        {
            static VkVertexInputBindingDescription bindingDescription{};

            bindingDescription.binding = 0;
            bindingDescription.stride = sizeof(Vertex);
            bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            return bindingDescription;
        }

        static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions()
        {

            static std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};

            attributeDescriptions[0].binding = 0;
            attributeDescriptions[0].location = 0;
            attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[0].offset = offsetof(Vertex, pos);

            attributeDescriptions[1].binding = 0;
            attributeDescriptions[1].location = 1;
            attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[1].offset = offsetof(Vertex, normal);

            attributeDescriptions[2].binding = 0;
            attributeDescriptions[2].location = 2;
            attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
            attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

            return attributeDescriptions;
        }
    };

    constexpr I3DModel(PipelineCreatorBase* pipelineCreatorBase):
        m_pipelineCreatorBase(pipelineCreatorBase)
    {
    }

    virtual ~I3DModel() 
    {
        vkDeviceWaitIdle(m_device);
        vkDestroyBuffer(m_device, m_generalBuffer, nullptr);
        vkFreeMemory(m_device, m_generalBufferMemory, nullptr);
    }

    virtual void init(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool cmdBufPool, VkQueue queue, 
                      const std::function<uint16_t(std::weak_ptr<TextureFactory::Texture> texture, VkSampler sampler,
                          VkDescriptorSetLayout descriptorSetLayout)>& descriptorCreator) = 0;
    virtual void draw(VkCommandBuffer cmdBuf, std::function<void(uint16_t materialId, VkPipelineLayout pipelineLayout)> descriptorBinding) = 0;

protected:
    PipelineCreatorBase* m_pipelineCreatorBase = nullptr;
    VkDevice m_device = nullptr;
    VkPhysicalDevice m_physicalDevice = nullptr;
    DynamicUniformBufferObject m_modelMtrx = { glm::mat4(1.0f) };
    VkDeviceSize m_verticesBufferOffset = 0;
    VkBuffer m_generalBuffer = nullptr;
    VkDeviceMemory m_generalBufferMemory = nullptr;
};

namespace std
{
    template <>
    struct hash<I3DModel::Vertex>
    {
        size_t operator()(I3DModel::Vertex const &vertex) const
        {
            return ((hash<glm::vec3>()(vertex.pos) ^ (hash<glm::vec3>()(vertex.normal) << 1)) >> 1) ^ (hash<glm::vec2>()(vertex.texCoord) << 1);
        }
    };
}