#include "SphereModel.h"

#include "PipelineCreatorTextured.h"
#include "Utils.h"

#include <cassert>
#include <cmath>
#include <cstring>

void SphereModel::buildMesh(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) const {
    vertices.clear();
    indices.clear();

    // Build a classic UV sphere:
    // - stacks split the sphere by latitude (from top to bottom)
    // - slices split it by longitude (around Y axis)
    // This gives predictable UVs for any regular 2D texture.
    const float pi = 3.1415926535f;
    for (uint32_t stack = 0; stack <= m_stacks; ++stack) {
        const float v = static_cast<float>(stack) / static_cast<float>(m_stacks);
        const float phi = v * pi;
        const float y = std::cos(phi);
        const float r = std::sin(phi);

        for (uint32_t slice = 0; slice <= m_slices; ++slice) {
            const float u = static_cast<float>(slice) / static_cast<float>(m_slices);
            const float theta = u * 2.0f * pi;
            const float x = r * std::cos(theta);
            const float z = r * std::sin(theta);

            Vertex vtx{};
            // Position and normal are generated from the same unit sphere direction.
            // Position is scaled by gameplay-defined projectile radius.
            vtx.pos = glm::vec3(x, y, z) * m_vertexMagnitudeMultiplier;
            vtx.normal = glm::normalize(glm::vec3(x, y, z));
            // Flip V so textures appear upright with the current shader conventions.
            vtx.texCoord = glm::vec2(u, 1.0f - v);
            // Tangent space is not required for this projectile right now.
            vtx.tangent = glm::vec4(0.0f);
            vtx.bitangent = glm::vec3(0.0f);
            vertices.push_back(vtx);
        }
    }

    // Stitch quads between neighboring rings and split each quad into 2 triangles.
    const uint32_t row = m_slices + 1u;
    for (uint32_t stack = 0; stack < m_stacks; ++stack) {
        for (uint32_t slice = 0; slice < m_slices; ++slice) {
            const uint32_t i0 = stack * row + slice;
            const uint32_t i1 = i0 + row;
            const uint32_t i2 = i0 + 1u;
            const uint32_t i3 = i1 + 1u;

            indices.push_back(i0);
            indices.push_back(i1);
            indices.push_back(i2);

            indices.push_back(i2);
            indices.push_back(i1);
            indices.push_back(i3);
        }
    }
}

void SphereModel::init(bool useTransferQueue) {
    auto pDevice = m_vkState._core.getDevice();
    assert(pDevice);
    assert(m_pipelineCreatorTextured);

    const VkQueue queue = useTransferQueue ? m_vkState._transferQueue : m_vkState._queue;
    const VkCommandPool cmdBufPool = useTransferQueue ? m_vkState._transferCmdBufPool : m_vkState._cmdBufPool;

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    buildMesh(vertices, indices);

    // Radius is used by gameplay/culling, so keep it aligned with actual sphere scale.
    m_radius = m_vertexMagnitudeMultiplier;
    m_indicesCount = static_cast<uint32_t>(indices.size());

    // View type must match the bound pipeline's shader (sampler2DArray vs sampler2D).
    auto texture = m_textureFactory.create2DArrayTexture({m_textureName}, true, true,
                                                         m_pipelineCreatorTextured->expectsArrayTexture(), useTransferQueue);
    if (texture.expired()) {
        Utils::printLog(ERROR_PARAM, "SphereModel: failed to create texture %s", m_textureName.c_str());
    } else {
        m_materialDescriptorId = m_pipelineCreatorTextured->createDescriptor(
            texture, m_textureFactory.getTextureSampler(texture.lock()->mipLevels));
    }

    Utils::createGeneralBuffer(pDevice, m_vkState._core.getPhysDevice(), cmdBufPool, queue, indices,
                               vertices, m_verticesBufferOffset, m_generalBuffer, m_generalBufferMemory);
    if (useTransferQueue && m_vkState._core.getTransferQueueFamily() != m_vkState._core.getQueueFamily()) {
        Utils::VulkanReleaseBufferOwnership(pDevice, queue, cmdBufPool, m_generalBuffer,
                                            m_vkState._core.getTransferQueueFamily(), m_vkState._core.getQueueFamily());
        m_vkState.registerTransferBufferOwnership(m_generalBuffer);
    }

    // Keep one host-visible instance buffer per swapchain image to avoid write hazards
    // when multiple frames are in flight.
    m_activeInstances = m_instances;
    m_instancesBuffer.assign(m_vkState._swapchainImageCount, VK_NULL_HANDLE);
    m_instancesBufferMemory.assign(m_vkState._swapchainImageCount, VK_NULL_HANDLE);

    const VkDeviceSize instancesSize = sizeof(m_instances[0]) * m_instances.size();
    for (size_t i = 0u; i < m_vkState._swapchainImageCount; ++i) {
        Utils::VulkanCreateBuffer(pDevice, m_vkState._core.getPhysDevice(), instancesSize,
                                  VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  m_instancesBuffer[i], m_instancesBufferMemory[i]);
        void* data = nullptr;
        vkMapMemory(pDevice, m_instancesBufferMemory[i], 0, instancesSize, 0, &data);
        memcpy(data, m_instances.data(), instancesSize);
        vkUnmapMemory(pDevice, m_instancesBufferMemory[i]);
    }

    publishReadyAfterTransfer(useTransferQueue);
}

void SphereModel::update(float, int, bool, uint32_t currentImage, const glm::mat4& viewProj, float z_far,
                         const glm::vec3& camPos) {
    // Reuse common frustum/LOD path from I3DModel and then upload current instances.
    sortInstances(currentImage, viewProj, camPos, z_far);
    updateBuffers(currentImage);
}

void SphereModel::updateBuffers(uint32_t currentImage) {
    auto pDevice = m_vkState._core.getDevice();
    assert(pDevice);
    if (m_activeInstances.empty()) {
        return;
    }

    const VkDeviceSize instancesSize = sizeof(m_activeInstances[0]) * m_activeInstances.size();
    void* data = nullptr;
    vkMapMemory(pDevice, m_instancesBufferMemory[currentImage], 0, instancesSize, 0, &data);
    memcpy(data, m_activeInstances.data(), instancesSize);
    vkUnmapMemory(pDevice, m_instancesBufferMemory[currentImage]);
}

void SphereModel::draw(VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex, uint32_t dynamicOffset) const {
    assert(m_generalBuffer);
    assert(m_pipelineCreatorTextured && m_pipelineCreatorTextured->getPipeline());

    if (m_pipelineCreatorTextured->isPushContantActive()) {
        vkCmdPushConstants(cmdBuf, m_pipelineCreatorTextured->getPipeline()->pipelineLayout,
                           VulkanState::PUSH_CONSTANT_STAGE_FLAGS, 0, sizeof(VulkanState::PushConstant),
                           &m_vkState._pushConstant);
    }

    vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineCreatorTextured->getPipeline()->pipeline);

    VkBuffer vertexBuffers[] = {m_generalBuffer, m_instancesBuffer[descriptorSetIndex]};
    VkDeviceSize offsets[] = {m_verticesBufferOffset, 0u};
    vkCmdBindVertexBuffers(cmdBuf, 0, 2, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmdBuf, m_generalBuffer, 0, VK_INDEX_TYPE_UINT32);

    // Sphere uses the textured pipeline and its own material descriptor.
    vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineCreatorTextured->getPipeline()->pipelineLayout,
                            0, 1, m_pipelineCreatorTextured->getDescriptorSet(descriptorSetIndex, m_materialDescriptorId),
                            1, &dynamicOffset);

    vkCmdDrawIndexed(cmdBuf, m_indicesCount, static_cast<uint32_t>(m_activeInstances.size()), 0, 0, 0);
}

void SphereModel::drawWithCustomPipeline(PipelineCreatorBase* pipelineCreator, VkCommandBuffer cmdBuf,
                                         uint32_t descriptorSetIndex, uint32_t dynamicOffset) const {
    assert(m_generalBuffer);
    assert(pipelineCreator && pipelineCreator->getPipeline());

    if (pipelineCreator->isPushContantActive()) {
        vkCmdPushConstants(cmdBuf, pipelineCreator->getPipeline()->pipelineLayout, VulkanState::PUSH_CONSTANT_STAGE_FLAGS, 0,
                           sizeof(VulkanState::PushConstant), &m_vkState._pushConstant);
    }

    vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineCreator->getPipeline()->pipeline);

    VkBuffer vertexBuffers[] = {m_generalBuffer, m_instancesBuffer[descriptorSetIndex]};
    VkDeviceSize offsets[] = {m_verticesBufferOffset, 0u};
    vkCmdBindVertexBuffers(cmdBuf, 0, 2, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmdBuf, m_generalBuffer, 0, VK_INDEX_TYPE_UINT32);

    vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineCreator->getPipeline()->pipelineLayout, 0, 1,
                            pipelineCreator->getDescriptorSet(descriptorSetIndex), 1, &dynamicOffset);

    vkCmdDrawIndexed(cmdBuf, m_indicesCount, static_cast<uint32_t>(m_activeInstances.size()), 0, 0, 0);
}
