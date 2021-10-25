
#include "Skybox.h"
#include "Utils.h"
#include "I3DModel.h"
#include <assert.h>

///Note: designed for VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
const std::vector<Skybox::Vertex> _vertices =
{
    // front
    {{-1.0, 0.0, 1.0}},
    {{1.0, 0.0, 1.0}},
    {{1.0, 1.0, 1.0}},
    {{-1.0, 1.0, 1.0}},
    // back
    {{-1.0, 0.0, -1.0}},
    {{1.0, 0.0, -1.0}},
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
    6, 5, 4,
    4, 7, 6,
    // left
    4, 0, 3,
    3, 7, 4,
    // bottom
    6, 7, 3,
    3, 2, 6,
    // top
    5, 4, 0,
    0, 1, 5
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

    auto& raster = Pipeliner::getInstance().getRasterizationInfo();
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;

    // Don't want to write to depth buffer
    auto& depthStencil = Pipeliner::getInstance().getDepthStencilInfo();
    depthStencil.depthTestEnable = VK_FALSE;
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

void Skybox::draw(VkCommandBuffer cmdBuf, std::function<void(uint16_t materialId,
    VkPipelineLayout pipelineLayout)> descriptorBinding)
{
    assert(descriptorBinding);
    assert(m_generalBuffer);

    vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeLine->pipeline);

    VkBuffer vertexBuffers[] = { m_generalBuffer };
    VkDeviceSize offsets[] = { m_verticesBufferOffset };
    vkCmdBindVertexBuffers(cmdBuf, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmdBuf, m_generalBuffer, 0, VK_INDEX_TYPE_UINT32);

    descriptorBinding(m_realMaterialId, m_pipeLine->pipelineLayout);
    vkCmdDrawIndexed(cmdBuf, static_cast<uint32_t>(_indices.size()), 1U, 0U, 0, 0U);
}