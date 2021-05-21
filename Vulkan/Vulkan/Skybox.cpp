
#include "Skybox.h"
#include "Utils.h"
#include "I3DModel.h"
#include <assert.h>

///Note: designed for VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
const std::vector<Skybox::Vertex> _vertices =
{
    // front
    {{-1.0, -1.0, 1.0}},
    {{1.0, -1.0, 1.0}},
    {{1.0, 1.0, 1.0}},
    {{-1.0, 1.0, 1.0}},
    // back
    {{-1.0, -1.0, -1.0}},
    {{1.0, -1.0, -1.0}},
    {{1.0, 1.0, -1.0}},
    {{-1.0, 1.0, -1.0}}
};

const std::vector<uint32_t> _indices =
{
    // front
    0, 1, 2,
    2, 3, 0,
    // right
    1, 5, 6,
    6, 2, 1,
    // back
    7, 6, 5,
    5, 4, 7,
    // left
    4, 0, 3,
    3, 7, 4,
    // bottom
    4, 5, 1,
    1, 0, 4,
    // top
    3, 2, 6,
    6, 7, 3
};

void Skybox::init(std::string_view vertShader, std::string_view fragShader, uint32_t width, uint32_t height, 
                  VkDescriptorSetLayout descriptorSetLayout, VkRenderPass renderPass, VkDevice device,
                  VkPhysicalDevice physicalDevice, VkCommandPool cmdBufPool, VkQueue queue,
                  const std::array<std::string_view, 6>& textureFileNames,
                  std::function<uint16_t(std::weak_ptr<TextureFactory::Texture>, VkSampler)> descriptorCreator)
{
    assert(descriptorCreator);
    
    auto& vertexInputInfo = Pipeliner::getInstance().getVertexInputInfo();
    constexpr auto bindingDescription = Vertex::getBindingDescription();
    constexpr auto attributeDescriptions = Vertex::getAttributeDescription();
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 1;
    vertexInputInfo.pVertexAttributeDescriptions = &attributeDescriptions;

    // Don't want to write to depth buffer
    auto& depthStencil = Pipeliner::getInstance().getDepthStencilInfo();
    depthStencil.depthWriteEnable = VK_FALSE;

    auto& pipelineIACreateInfo = Pipeliner::getInstance().getInputAssemblyInfo();
    pipelineIACreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    
    m_pipeLine = Pipeliner::getInstance().createPipeLine(vertShader, fragShader, width, height,
        descriptorSetLayout, renderPass, device);
    assert(m_pipeLine);

    TextureFactory* pTextureFactory = &TextureFactory::init(device, physicalDevice, cmdBufPool, queue);
    assert(pTextureFactory);

    auto texture = pTextureFactory->createCubeTexture(textureFileNames);
    if (!texture.expired())
    {
        m_realMaterialId = descriptorCreator(texture, pTextureFactory->getTextureSampler(texture.lock()->mipLevels));
    }

    Utils::createGeneralBuffer(device, physicalDevice, cmdBufPool, queue, _indices, _vertices,
        m_verticesBufferOffset, m_generalBuffer, m_generalBufferMemory);
}