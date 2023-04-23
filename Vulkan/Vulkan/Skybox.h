#pragma once

#include "Pipeliner.h"
#include "TextureFactory.h"
#include "I3DModel.h"
#include <glm/glm.hpp>

class Skybox : public I3DModel
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

    Skybox(const std::array<std::string_view, 6>& textureFileNames, PipelineCreatorBase* pipelineCreatorBase) noexcept(true)
        : I3DModel(pipelineCreatorBase)
        , m_textureFileNames(textureFileNames)
    {}

    virtual void init(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool cmdBufPool, VkQueue queue,
                  const std::function<uint16_t(std::weak_ptr<TextureFactory::Texture> texture, VkSampler sampler, 
                                         VkDescriptorSetLayout descriptorSetLayout)>& descriptorCreator) override;

    virtual void draw(VkCommandBuffer cmdBuf, std::function<void(uint16_t materialId, VkPipelineLayout pipelineLayout)> descriptorBinding) override;

private:
    std::array<std::string_view, 6> m_textureFileNames{};
    std::uint32_t m_realMaterialId{ 0U };
};