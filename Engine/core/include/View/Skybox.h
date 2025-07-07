#pragma once

#include <glm/glm.hpp>
#include "I3DModel.h"
#include "Pipeliner.h"
#include "TextureFactory.h"

class Skybox : public I3DModel {
public:
    struct Vertex {
        glm::vec3 pos;

        static const std::array<VkVertexInputBindingDescription, 2u>& getBindingDescriptions() {
            static std::array<VkVertexInputBindingDescription, 2u> bindingDescriptions{};
            bindingDescriptions[0].binding = 0;
            bindingDescriptions[0].stride = sizeof(Vertex);
            bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            bindingDescriptions[1].binding = 1;
            bindingDescriptions[1].stride = sizeof(Instance);
            bindingDescriptions[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

            return bindingDescriptions;
        }
        static const std::array<VkVertexInputAttributeDescription, 3>& getAttributeDescription() {
            static std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};
            attributeDescriptions[0].binding = 0;
            attributeDescriptions[0].location = 0;
            attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[0].offset = offsetof(Vertex, pos);

            attributeDescriptions[1].binding = 1;
            attributeDescriptions[1].location = 5;
            attributeDescriptions[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            attributeDescriptions[1].offset = offsetof(Instance, posShift);

            attributeDescriptions[2].binding = 1;
            attributeDescriptions[2].location = 6;
            attributeDescriptions[2].format = VK_FORMAT_R32_SFLOAT;
            attributeDescriptions[2].offset = offsetof(Instance, scale);

            return attributeDescriptions;
        }
    };

    Skybox(const VulkanState& vulkanState, TextureFactory& textureFactory,
           const std::array<std::string_view, 6>& textureFileNames,
           PipelineCreatorTextured* pipelineCreatorTextured) noexcept(true)
        : I3DModel(vulkanState, textureFactory, pipelineCreatorTextured), m_textureFileNames(textureFileNames) {
    }

    void init() override;
    void draw(VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex, uint32_t dynamicOffset) const override;

private:
    std::array<std::string_view, 6> m_textureFileNames{};
    std::uint32_t m_realMaterialId{0U};
};
