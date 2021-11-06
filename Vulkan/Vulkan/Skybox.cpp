
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

void Skybox::init(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool cmdBufPool, VkQueue queue,
                  const std::function<uint16_t(std::weak_ptr<TextureFactory::Texture> texture, VkSampler sampler, 
                                         VkDescriptorSetLayout descriptorSetLayout)>& descriptorCreator)
{
    assert(device);
    assert(physicalDevice);
    assert(cmdBufPool);
    assert(queue);
    assert(descriptorCreator);
    assert(m_pipelineCreatorBase);
    assert(!m_textureFileNames.empty()); 

    m_device = device;
    m_physicalDevice = physicalDevice;

    TextureFactory* pTextureFactory = &TextureFactory::init(device, physicalDevice, cmdBufPool, queue);
    assert(pTextureFactory);

    auto texture = pTextureFactory->createCubeTexture(m_textureFileNames);
    if (!texture.expired())
    {
        m_realMaterialId = descriptorCreator(texture, pTextureFactory->getTextureSampler(texture.lock()->mipLevels),
                                             *m_pipelineCreatorBase->getDescriptorSetLayout().get());
    }

    Utils::createGeneralBuffer(device, physicalDevice, cmdBufPool, queue, _indices, _vertices,
        m_verticesBufferOffset, m_generalBuffer, m_generalBufferMemory);
}

void Skybox::draw(VkCommandBuffer cmdBuf, std::function<void(uint16_t materialId, VkPipelineLayout pipelineLayout)> descriptorBinding)
{
    assert(descriptorBinding);
    assert(m_generalBuffer);
    assert(m_pipelineCreatorBase);
    assert(m_pipelineCreatorBase->getPipeline().get());

    vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineCreatorBase->getPipeline().get()->pipeline);

    VkBuffer vertexBuffers[] = { m_generalBuffer };
    VkDeviceSize offsets[] = { m_verticesBufferOffset };
    vkCmdBindVertexBuffers(cmdBuf, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmdBuf, m_generalBuffer, 0, VK_INDEX_TYPE_UINT32);

    descriptorBinding(m_realMaterialId, m_pipelineCreatorBase->getPipeline().get()->pipelineLayout);
    vkCmdDrawIndexed(cmdBuf, static_cast<uint32_t>(_indices.size()), 1U, 0U, 0, 0U);
}