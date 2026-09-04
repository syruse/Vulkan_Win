#include "CubeModel.h"

#include "PipelineCreatorTextured.h"
#include "Utils.h"

#include <cassert>
#include <cstring>

void CubeModel::buildMesh(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) const {
    vertices.clear();
    indices.clear();

    // Half extent controls actual cube size in world space.
    // We keep a per-face vertex layout (24 vertices total) so each face has stable UVs/normals.
    const glm::vec3 h = m_halfExtents;

    struct FaceData {
        glm::vec3 n;
        glm::vec3 p0;
        glm::vec3 p1;
        glm::vec3 p2;
        glm::vec3 p3;
    };

    const std::array<FaceData, 6> faces = {
        FaceData{glm::vec3(0, 0, 1), glm::vec3(-h.x, -h.y, h.z), glm::vec3(h.x, -h.y, h.z), glm::vec3(h.x, h.y, h.z), glm::vec3(-h.x, h.y, h.z)},
        FaceData{glm::vec3(0, 0, -1), glm::vec3(h.x, -h.y, -h.z), glm::vec3(-h.x, -h.y, -h.z), glm::vec3(-h.x, h.y, -h.z), glm::vec3(h.x, h.y, -h.z)},
        FaceData{glm::vec3(1, 0, 0), glm::vec3(h.x, -h.y, h.z), glm::vec3(h.x, -h.y, -h.z), glm::vec3(h.x, h.y, -h.z), glm::vec3(h.x, h.y, h.z)},
        FaceData{glm::vec3(-1, 0, 0), glm::vec3(-h.x, -h.y, -h.z), glm::vec3(-h.x, -h.y, h.z), glm::vec3(-h.x, h.y, h.z), glm::vec3(-h.x, h.y, -h.z)},
        FaceData{glm::vec3(0, 1, 0), glm::vec3(-h.x, h.y, h.z), glm::vec3(h.x, h.y, h.z), glm::vec3(h.x, h.y, -h.z), glm::vec3(-h.x, h.y, -h.z)},
        FaceData{glm::vec3(0, -1, 0), glm::vec3(-h.x, -h.y, -h.z), glm::vec3(h.x, -h.y, -h.z), glm::vec3(h.x, -h.y, h.z), glm::vec3(-h.x, -h.y, h.z)},
    };

    for (const auto& f : faces) {
        const uint32_t base = static_cast<uint32_t>(vertices.size());
        const float textureTileSize = 2.0f * h.y;
        const float uRepeat = glm::length(f.p1 - f.p0) / textureTileSize;
        const float vRepeat = glm::length(f.p2 - f.p1) / textureTileSize;
        Vertex v0{};
        v0.pos = f.p0;
        v0.normal = f.n;
        v0.texCoord = glm::vec2(0, vRepeat);
        Vertex v1{};
        v1.pos = f.p1;
        v1.normal = f.n;
        v1.texCoord = glm::vec2(uRepeat, vRepeat);
        Vertex v2{};
        v2.pos = f.p2;
        v2.normal = f.n;
        v2.texCoord = glm::vec2(uRepeat, 0);
        Vertex v3{};
        v3.pos = f.p3;
        v3.normal = f.n;
        v3.texCoord = glm::vec2(0, 0);

        vertices.push_back(v0);
        vertices.push_back(v1);
        vertices.push_back(v2);
        vertices.push_back(v3);

        // Two triangles per face.
        indices.push_back(base + 0u);
        indices.push_back(base + 1u);
        indices.push_back(base + 2u);
        indices.push_back(base + 0u);
        indices.push_back(base + 2u);
        indices.push_back(base + 3u);
    }
}

void CubeModel::init(bool useTransferQueue) {
    auto pDevice = m_vkState._core.getDevice();
    assert(pDevice);
    assert(m_pipelineCreatorTextured);

    const VkQueue queue = useTransferQueue ? m_vkState._transferQueue : m_vkState._queue;
    const VkCommandPool cmdBufPool = useTransferQueue ? m_vkState._transferCmdBufPool : m_vkState._cmdBufPool;

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    buildMesh(vertices, indices);

    // Approximate bounding sphere for culling/gameplay checks (sqrt(3) * halfExtent).
    m_radius = glm::length(m_halfExtents);
    m_indicesCount = static_cast<uint32_t>(indices.size());

    // View type must match the bound pipeline's shader (sampler2DArray vs sampler2D).
    auto texture = m_textureFactory.create2DArrayTexture({m_textureName}, true, true,
                                                         m_pipelineCreatorTextured->expectsArrayTexture(), useTransferQueue);
    if (texture.expired()) {
        Utils::printLog(ERROR_PARAM, "CubeModel: failed to create texture %s", m_textureName.c_str());
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

    // Per-swapchain-image instance buffers are used the same way as in ObjModel/SphereModel.
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

void CubeModel::update(float, int, bool, uint32_t currentImage, const glm::mat4& viewProj, float z_far,
                       const glm::vec3& camPos) {
    // Common instance culling + upload path.
    sortInstances(currentImage, viewProj, camPos, z_far);
    updateBuffers(currentImage);
}

void CubeModel::updateBuffers(uint32_t currentImage) {
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

void CubeModel::draw(VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex, uint32_t dynamicOffset) const {
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

    // Cube uses one textured material id supplied during init().
    vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineCreatorTextured->getPipeline()->pipelineLayout,
                            0, 1, m_pipelineCreatorTextured->getDescriptorSet(descriptorSetIndex, m_materialDescriptorId),
                            1, &dynamicOffset);

    vkCmdDrawIndexed(cmdBuf, m_indicesCount, static_cast<uint32_t>(m_activeInstances.size()), 0, 0, 0);
}

void CubeModel::drawWithCustomPipeline(PipelineCreatorBase* pipelineCreator, VkCommandBuffer cmdBuf,
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
