#include "Terrain.h"
#include <assert.h>
#include "I3DModel.h"
#include "PipelineCreatorTextured.h"
#include "Utils.h"

/// Note: designed for VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
std::vector<I3DModel::Vertex> _vertices = {{{-1.0, 0.0, 1.0}, {0.0, 1.0, 0.0}, {0.0, 1.0}},
                                           {{1.0, 0.0, 1.0}, {0.0, 1.0, 0.0}, {1.0, 1.0}},
                                           {{1.0, 0.0, -1.0}, {0.0, 1.0, 0.0}, {1.0, 0.0}},
                                           {{-1.0, 0.0, -1.0}, {0.0, 1.0, 0.0}, {0.0, 0.0}}};

const std::vector<uint32_t> _indices = {0, 1, 2, 2, 3, 0};

void Terrain::init() {
    auto p_devide = m_vkState._core.getDevice();
    assert(p_devide);
    assert(m_pipelineCreatorTextured);
    assert(!m_textureFileName.empty());

    if (auto texture = m_textureFactory.create2DTexture(m_textureFileName).lock()) {
        m_realMaterialId =
            m_pipelineCreatorTextured->createDescriptor(texture, m_textureFactory.getTextureSampler(texture->mipLevels));

        float factor = static_cast<float>(m_vertexMagnitudeMultiplier);
        for (auto& vertex : _vertices) {
            vertex.pos = factor * vertex.pos;
            // repetition factor can be calculated more precisely
            vertex.texCoord = 50.0f * (factor / texture->width) * vertex.texCoord;
        }
    }

    Utils::createGeneralBuffer(p_devide, m_vkState._core.getPhysDevice(), m_vkState._cmdBufPool, m_vkState._queue, _indices,
                               _vertices, m_verticesBufferOffset, m_generalBuffer, m_generalBufferMemory);
}

void Terrain::draw(VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex, uint32_t dynamicOffset) const {
    assert(m_generalBuffer);
    assert(m_pipelineCreatorTextured);
    assert(m_pipelineCreatorTextured->getPipeline().get());

    vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineCreatorTextured->getPipeline().get()->pipeline);

    VkBuffer vertexBuffers[] = {m_generalBuffer};
    VkDeviceSize offsets[] = {m_verticesBufferOffset};
    vkCmdBindVertexBuffers(cmdBuf, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmdBuf, m_generalBuffer, 0, VK_INDEX_TYPE_UINT32);

    vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipelineCreatorTextured->getPipeline().get()->pipelineLayout, 0, 1,
                            m_pipelineCreatorTextured->getDescriptorSet(descriptorSetIndex, m_realMaterialId), 1, &dynamicOffset);
    vkCmdDrawIndexed(cmdBuf, static_cast<uint32_t>(_indices.size()), 1U, 0U, 0, 0U);
}
