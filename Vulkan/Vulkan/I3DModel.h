#pragma once

#include <array>
#include <string>
#include <glm/glm.hpp>
#include "vulkan/vulkan.h"
#include "glm/gtx/hash.hpp"
#include "TextureFactory.h"

class I3DModel
{
public:

    static std::string MODEL_DIR;

    struct Material
    {
        uint16_t id = 0u;
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

    virtual ~I3DModel();

    virtual void init(std::string_view path, VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool cmdBufPool, VkQueue queue, 
                      std::function<uint16_t(VkImageView imageView, VkSampler sampler)> descriptorCreator) final;
    virtual void draw(VkCommandBuffer cmdBuf, std::function<void(uint16_t materialId)> descriptorBinding) = 0;

private:
    virtual void load(std::string_view path) = 0; 
    virtual void createVertexBuffer(VkCommandPool cmdBufPool, VkQueue queue) final;
    virtual void createIndexBuffer(VkCommandPool cmdBufPool, VkQueue queue) final;

protected:
    uint16_t m_materialId = -1;
    std::size_t m_indecesAmount = 0u;
    std::vector<Vertex> m_vertices{};
    std::vector<uint32_t> m_indices{};
    VkBuffer m_vertexBuffer = nullptr;
    VkDeviceMemory m_vertexBufferMemory = nullptr;
    VkBuffer m_indexBuffer = nullptr;
    VkDeviceMemory m_indexBufferMemory = nullptr;
    VkDevice m_device = nullptr;
    VkPhysicalDevice m_physicalDevice = nullptr;
    std::shared_ptr<TextureFactory::Texture> m_texture = nullptr;
    DynamicUniformBufferObject m_modelMtrx = { glm::mat4(1.0f) };
    TextureFactory* mp_textureFactory = nullptr;
    std::function<uint16_t(VkImageView imageView, VkSampler sampler)> m_descriptorCreator = nullptr;
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