#include "Terrain.h"
#include <assert.h>
#include "I3DModel.h"
#include "PipelineCreatorTextured.h"
#include "Utils.h"

constexpr uint32_t TERRAIN_TILES = 16u;  /// Note: How many tiles being used for terrain for each axis

/// Note: designed for VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST

// WARN: not used due to tesselation which requires more tiles than one
//std::vector<I3DModel::Vertex> _vertices = {{{-1.0, 0.0, 1.0}, {0.0, 1.0, 0.0}, {0.0, 1.0}},
//                                           {{1.0, 0.0, 1.0}, {0.0, 1.0, 0.0}, {1.0, 1.0}},
//                                           {{1.0, 0.0, -1.0}, {0.0, 1.0, 0.0}, {1.0, 0.0}},
//                                           {{-1.0, 0.0, -1.0}, {0.0, 1.0, 0.0}, {0.0, 0.0}}};
//
//const std::vector<uint32_t> _indices = {0, 1, 2, 2, 3, 0};

void Terrain::init() {
    auto p_devide = m_vkState._core.getDevice();
    assert(p_devide);
    assert(m_pipelineCreatorTextured);
    assert(!m_textureFileName1.empty() && !m_textureFileName2.empty() && !m_noiseTextureFileName.empty());

    auto texture = m_textureFactory.create2DArrayTexture({m_noiseTextureFileName.data(), m_textureFileName1.data(), 
                                                          m_textureFileName2.data()}).lock();

    uint32_t ACTUAL_TERRAIN_TILES = TERRAIN_TILES + 1u; // includes the first point as well: TERRAIN_TILES = 2 => 0.0 <-> 0.5 <-> 1.0
    m_vertices.reserve(ACTUAL_TERRAIN_TILES * ACTUAL_TERRAIN_TILES);
    const float stepTexCoords = 1.0f / static_cast<float>(TERRAIN_TILES); // [0.0 : 1.0]
    const float stepVertCoords = 1.0f - std::abs(stepTexCoords * 2.0f - 1.0f); // [-1.0 : 1.0]
    constexpr float epsilon = std::numeric_limits<float>::epsilon();
    I3DModel::Vertex vert;
    vert.normal = {0.0, 1.0, 0.0};
    for (float zAxis = 1.0f; zAxis > -(1.0f + epsilon); zAxis -= stepVertCoords) {
        for (float xAxis = -1.0f; xAxis < (1.0f + epsilon); xAxis += stepVertCoords) {
            vert.pos = {xAxis, 0.0f, zAxis};
            vert.texCoord = {(xAxis * 0.5f + 0.5f), (zAxis * 0.5f + 0.5f)};
            m_vertices.push_back(vert);
        }
    }

    m_indices.reserve(TERRAIN_TILES * TERRAIN_TILES * 6u);
    for (uint32_t i = 0u; i < ACTUAL_TERRAIN_TILES; ++i) {
        for (uint32_t j = 0u; j < ACTUAL_TERRAIN_TILES; ++j) {
            if (i == ACTUAL_TERRAIN_TILES - 1u || j == ACTUAL_TERRAIN_TILES - 1u) {
                continue;
            }
            uint32_t curIndx = ACTUAL_TERRAIN_TILES * i + j;
            m_indices.push_back(curIndx);
            m_indices.push_back(curIndx + 1u);
            uint32_t _3dindx = curIndx + 1u + ACTUAL_TERRAIN_TILES;
            m_indices.push_back(_3dindx);

            m_indices.push_back(_3dindx);
            m_indices.push_back(_3dindx - 1u);
            m_indices.push_back(curIndx);
        }
    }

    if (texture) {
        m_realMaterialId = m_pipelineCreatorTextured->createDescriptor(texture, m_textureFactory.getTextureSampler(texture->mipLevels));

        float factor = static_cast<float>(m_vertexMagnitudeMultiplier);
        for (auto& vertex : m_vertices) {
            vertex.pos = factor * vertex.pos;
            // repetition factor can be calculated more precisely
            vertex.tangent = glm::vec4(45.0f * (factor / texture->width) * vertex.texCoord, 0.0f, 0.0f);
        }
    }

    // Note: we must have at least one instance to draw
    if (m_instances.empty()) {
        m_instances.push_back({glm::vec3(0.0f), 1.0f});
    }

    Utils::createGeneral3in1Buffer(p_devide, m_vkState._core.getPhysDevice(), m_vkState._cmdBufPool, m_vkState._queue, m_indices,
                                   m_vertices, m_instances, m_verticesBufferOffset, m_instancesBufferOffset, m_generalBuffer,
                                   m_generalBufferMemory);
}

void Terrain::draw(VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex, uint32_t dynamicOffset) const {
    assert(m_generalBuffer);
    assert(m_pipelineCreatorTextured);
    assert(m_pipelineCreatorTextured->getPipeline().get());

    if (m_pipelineCreatorTextured->isPushContantActive()) {
        vkCmdPushConstants(cmdBuf, m_pipelineCreatorTextured->getPipeline()->pipelineLayout,
                           VulkanState::PUSH_CONSTANT_STAGE_FLAGS,
                           0, sizeof(VulkanState::PushConstant), &m_vkState._pushConstant);
    }

    vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineCreatorTextured->getPipeline().get()->pipeline);

    VkBuffer vertexBuffers[] = {m_generalBuffer, m_generalBuffer};
    VkDeviceSize offsets[] = {m_verticesBufferOffset, m_instancesBufferOffset};
    vkCmdBindVertexBuffers(cmdBuf, 0, 2, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmdBuf, m_generalBuffer, 0, VK_INDEX_TYPE_UINT32);

    vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipelineCreatorTextured->getPipeline().get()->pipelineLayout, 0, 1,
                            m_pipelineCreatorTextured->getDescriptorSet(descriptorSetIndex, m_realMaterialId), 1, &dynamicOffset);
    vkCmdDrawIndexed(cmdBuf, static_cast<uint32_t>(m_indices.size()), 1U, 0U, 0, 0U);
}

void Terrain::drawWithCustomPipeline(PipelineCreatorBase* pipelineCreator, VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex,
                                      uint32_t dynamicOffset) const {
    assert(m_generalBuffer);
    assert(pipelineCreator);
    assert(pipelineCreator->getPipeline().get());

    if (pipelineCreator->isPushContantActive()) {
        vkCmdPushConstants(cmdBuf, pipelineCreator->getPipeline()->pipelineLayout, VulkanState::PUSH_CONSTANT_STAGE_FLAGS, 0,
                           sizeof(VulkanState::PushConstant),
                           &m_vkState._pushConstant);
    }
    
    vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineCreator->getPipeline().get()->pipeline);
    
    VkBuffer vertexBuffers[] = {m_generalBuffer, m_generalBuffer};
    VkDeviceSize offsets[] = {m_verticesBufferOffset, m_instancesBufferOffset};
    vkCmdBindVertexBuffers(cmdBuf, 0, 2, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmdBuf, m_generalBuffer, 0, VK_INDEX_TYPE_UINT32);
    
    vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineCreator->getPipeline().get()->pipelineLayout, 0, 1,
                            pipelineCreator->getDescriptorSet(descriptorSetIndex, m_realMaterialId), 1, &dynamicOffset);
    vkCmdDrawIndexed(cmdBuf, static_cast<uint32_t>(m_indices.size()), 1U, 0U, 0, 0U);
}
