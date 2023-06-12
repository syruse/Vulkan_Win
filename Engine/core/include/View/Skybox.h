#pragma once

#include <glm/glm.hpp>
#include "I3DModel.h"
#include "Pipeliner.h"
#include "TextureFactory.h"

class Skybox : public I3DModel {
public:
    struct Vertex {
        glm::vec3 pos;

        static constexpr VkVertexInputBindingDescription getBindingDescription() {
            VkVertexInputBindingDescription bindingDescription{};
            bindingDescription.binding = 0;
            bindingDescription.stride = sizeof(Vertex);
            bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            return bindingDescription;
        }
        static constexpr VkVertexInputAttributeDescription getAttributeDescription() {
            VkVertexInputAttributeDescription attributeDescription{};
            attributeDescription.binding = 0;
            attributeDescription.location = 0;
            attributeDescription.format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescription.offset = offsetof(Vertex, pos);
            return attributeDescription;
        }
    };

    Skybox(const VulkanState& vulkanState, TextureFactory& textureFactory,
           const std::array<std::string_view, 6>& textureFileNames,
           PipelineCreatorTextured* pipelineCreatorTextured) noexcept(true)
        : I3DModel(vulkanState, textureFactory, pipelineCreatorTextured), m_textureFileNames(textureFileNames) {
    }

    virtual void init() override;

    virtual void draw(VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex, uint32_t dynamicOffset) override;

private:
    std::array<std::string_view, 6> m_textureFileNames{};
    std::uint32_t m_realMaterialId{0U};
};