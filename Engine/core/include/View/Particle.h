#pragma once

#include <glm/glm.hpp>
#include "I3DModel.h"
#include "Pipeliner.h"
#include "TextureFactory.h"

class Particle : public I3DModel {
public:
    struct Instance {
        glm::vec3 pos{0.0f};
        glm::vec3 scale{1.0f};
    };

    static const VkVertexInputBindingDescription& getBindingDescription() {
        static VkVertexInputBindingDescription bindingDescription;
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Instance);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
        return bindingDescription;
    }

    static const std::array<VkVertexInputAttributeDescription, 2u>& getAttributeDescription() {
        static std::array<VkVertexInputAttributeDescription, 2u> attributeDescriptions{};
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Instance, pos);

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(Instance, scale);
        return attributeDescriptions;
    }

    // for filling z plane with particles (bushes etc)
    Particle(const VulkanState& vulkanState, TextureFactory& textureFactory, std::string_view textureFileName,
             PipelineCreatorTextured* pipelineCreatorTextured, uint32_t instancesAmount, float zFar = 1.0f,
             const glm::vec3& scale = glm::vec3(1.0f)) noexcept(true)
        : I3DModel(vulkanState, textureFactory, pipelineCreatorTextured),
          m_textureFileName(textureFileName),
          m_instancesAmount{instancesAmount},
          m_zFar{zFar},
          m_scale{scale} {
    }

    void init() override;
    void draw(VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex, [[maybe_unused]] uint32_t dynamicOffset) const override;

private:
    std::string_view m_textureFileName{};
    uint32_t m_instancesAmount{1u};
    float m_zFar{1.0f};
    glm::vec3 m_scale{1.0f};
};
