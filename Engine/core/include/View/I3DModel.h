#pragma once

#include "TextureFactory.h"
#include "VulkanState.h"
#include "VertexData.h"

#include <array>
#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>
#include <map>
#include <string>

class PipelineCreatorBase;
class PipelineCreatorTextured;
class PipelineCreatorFootprint;
class I3DModel {
public:
    struct SubObject {
        std::uint32_t realMaterialId;
        std::size_t indexOffset;
        std::size_t indexAmount;
        std::uint32_t realMaterialFootprintId;
    };

    struct Material {
        std::weak_ptr<TextureFactory::Texture> texture;
        VkSampler sampler;
        VkDescriptorSetLayout descriptorSetLayout;
        std::array<VkDescriptorSet, VulkanState::MAX_FRAMES_IN_FLIGHT> descriptorSets{};
    };

    struct Vertex : VertexData {

        bool operator==(const Vertex& other) const {
            return pos == other.pos && normal == other.normal && texCoord == other.texCoord;
        }

        static const VkVertexInputBindingDescription& getBindingDescription() {
            static VkVertexInputBindingDescription bindingDescription{};

            bindingDescription.binding = 0;
            bindingDescription.stride = sizeof(Vertex);
            bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            return bindingDescription;
        }

        static const std::array<VkVertexInputAttributeDescription, 5>& getAttributeDescriptions() {
            static std::array<VkVertexInputAttributeDescription, 5> attributeDescriptions{};

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

            attributeDescriptions[3].binding = 0;
            attributeDescriptions[3].location = 3;
            attributeDescriptions[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            attributeDescriptions[3].offset = offsetof(Vertex, tangent);

            attributeDescriptions[4].binding = 0;
            attributeDescriptions[4].location = 4;
            attributeDescriptions[4].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[4].offset = offsetof(Vertex, bitangent);

            return attributeDescriptions;
        }
    };

    I3DModel(const VulkanState& vulkanState, TextureFactory& textureFactory, PipelineCreatorTextured* pipelineCreatorTextured,
             PipelineCreatorFootprint* pipelineCreatorFootprint, float vertexMagnitudeMultiplier = 1.0f) noexcept(true);

    I3DModel(const VulkanState& vulkanState, TextureFactory& textureFactory, PipelineCreatorTextured* pipelineCreatorTextured,
             float vertexMagnitudeMultiplier = 1.0f) noexcept(true)
        : I3DModel(vulkanState, textureFactory, pipelineCreatorTextured, nullptr, vertexMagnitudeMultiplier) {
    }

    virtual ~I3DModel() {
        std::ignore = vkDeviceWaitIdle(m_vkState._core.getDevice());
        vkDestroyBuffer(m_vkState._core.getDevice(), m_generalBuffer, nullptr);
        vkFreeMemory(m_vkState._core.getDevice(), m_generalBufferMemory, nullptr);
    }

    virtual void init() = 0;
    virtual void draw(VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex = 0U, uint32_t dynamicOffset = 0U) const = 0;
    virtual void drawWithCustomPipeline(PipelineCreatorBase* pipelineCreator, VkCommandBuffer cmdBuf,
                                        uint32_t descriptorSetIndex = 0U, uint32_t dynamicOffset = 0U) const {
    }
    virtual void drawFootprints(VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex = 0U, uint32_t dynamicOffset = 0U) const {
    }

    virtual float radius() const {
        return 0.0f;
    }

    virtual void update(float deltaTimeMS, int animationID = 0u, bool onGPU = true) {
        // actual for animated models
    }

protected:
    const VulkanState& m_vkState;
    TextureFactory& m_textureFactory;
    float m_vertexMagnitudeMultiplier{1.0f};
    PipelineCreatorTextured* m_pipelineCreatorTextured{nullptr};
    PipelineCreatorFootprint* m_pipelineCreatorFootprint{nullptr};
    VulkanState::Model m_modelMtrx{glm::mat4(1.0f)};
    VkDeviceSize m_verticesBufferOffset{0U};
    VkBuffer m_generalBuffer{nullptr};
    VkDeviceMemory m_generalBufferMemory{nullptr};
};

namespace std {
template <>
struct hash<I3DModel::Vertex> {
    size_t operator()(I3DModel::Vertex const& vertex) const {
        return ((hash<glm::vec3>()(vertex.pos) ^ (hash<glm::vec3>()(vertex.normal) << 1)) >> 1) ^
               (hash<glm::vec2>()(vertex.texCoord) << 1);
    }
};
}  // namespace std
