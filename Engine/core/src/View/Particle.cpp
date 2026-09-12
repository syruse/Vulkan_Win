#include "Particle.h"
#include <assert.h>
#include <glm/gtx/transform.hpp>
#include <random>
#include "I3DModel.h"
#include "PipelineCreatorParticle.h"
#include "Utils.h"

Particle::~Particle() {
    for (size_t i = 0u; i < m_uboParticle.buffers.size(); ++i) {
        if (m_uboParticle.buffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_vkState._core.getDevice(), m_uboParticle.buffers[i], nullptr);
        }
    }
    for (size_t i = 0u; i < m_uboParticle.buffersMemory.size(); ++i) {
        if (m_uboParticle.buffersMemory[i] != VK_NULL_HANDLE) {
            vkFreeMemory(m_vkState._core.getDevice(), m_uboParticle.buffersMemory[i], nullptr);
        }
    }
    for (size_t i = 0u; i < m_instanceBuffers.size(); ++i) {
        if (m_instanceBuffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_vkState._core.getDevice(), m_instanceBuffers[i], nullptr);
        }
        if (m_instanceBuffersMemory[i] != VK_NULL_HANDLE) {
            vkFreeMemory(m_vkState._core.getDevice(), m_instanceBuffersMemory[i], nullptr);
        }
    }
}

Particle::Particle(const VulkanState& vulkanState, TextureFactory& textureFactory, std::string_view textureFileName,
                   PipelineCreatorParticle* pipelineCreatorTextured, uint32_t instancesAmount, float zFar,
                   const glm::vec3& scale) noexcept(true)
    : I3DModel(vulkanState, textureFactory, pipelineCreatorTextured),
      m_textureFileName(textureFileName),
      m_pipelineCreatorTextured(pipelineCreatorTextured),
      m_instanceCount(instancesAmount),
      m_zFar{zFar},
      m_maxScale{scale},
      m_minScale{scale},
    m_mode{ParticleMode::STATIC} {
    assert(zFar > 1.0f);
    pipelineCreatorTextured->increaseUsageCounter();
    m_instances.resize(m_instanceCount, Particle::Instance{});

    m_verticesPreparedFuture = std::async(std::launch::async, [this] {
        for (auto& vertex : m_vertices) {
            vertex.scaleMax = m_maxScale;
            vertex.scaleMin = m_maxScale;
        }

        std::random_device rd;
        std::mt19937 gen(rd());  // seed the generator
        int32_t limit = static_cast<int32_t>(m_zFar);
        std::uniform_int_distribution<> distr(-limit, limit);  // define the range
        for (std::size_t i = 0u; i < m_instances.size(); ++i) {
            auto& instance = m_instances[i];
            instance.pos.z = distr(gen);
            instance.pos.x = distr(gen);
            instance.pos.y = 0.0f;
        }
        return true;
    });
}

Particle::Particle(const VulkanState& vulkanState, TextureFactory& textureFactory,
                   std::string_view particleTextureFileName, std::string_view particleGradientTextureFileName,
                   PipelineCreatorParticle* pipelineCreatorTextured, uint32_t instancesAmount,
                   const glm::vec3& positionOrigin, const glm::vec3& accrleration, const glm::vec3& minScale,
                   const glm::vec3& maxScale, float lifeDurationMinMs, float lifeDurationMaxMs) noexcept(true)
    : I3DModel(vulkanState, textureFactory, pipelineCreatorTextured),
      m_textureFileName(particleTextureFileName),
      m_textureGradientFileName(particleGradientTextureFileName),
      m_pipelineCreatorTextured(pipelineCreatorTextured),
      m_instanceCount(instancesAmount),
      m_maxScale{maxScale},
      m_minScale{minScale},
    m_mode{ParticleMode::ANCHORED} {
        pipelineCreatorTextured->increaseUsageCounter();
        assert(lifeDurationMinMs > 0.0f && lifeDurationMinMs <= lifeDurationMaxMs);
        m_instances.resize(m_instanceCount, Particle::Instance{});
        m_uboParticle.params.dynamicPos = glm::vec4(positionOrigin, 1.0f);
        m_uboParticle.params.velocity = glm::vec4(accrleration, 0.0f);
        m_verticesPreparedFuture =
            std::async(std::launch::async, [this, positionOrigin, lifeDurationMinMs, lifeDurationMaxMs, accrleration] {
        for (auto& vertex : m_vertices) {
            vertex.scaleMax = m_maxScale;
            vertex.scaleMin = m_minScale;
            vertex.pos = positionOrigin;
        }

        std::random_device rd;
        std::mt19937 gen(rd());                                 // seed the generator
        std::uniform_real_distribution<float> distr(0.1, 1.0);  // define the range
        for (std::size_t i = 0u; i < m_instances.size(); ++i) {
            auto& instance = m_instances[i];
            float random = distr(gen);
            // matrix for spreading non linear way
            // glm::mat3 rotMat1 = glm::mat3(glm::rotate(glm::radians(5.0f * random), glm::vec3(0.0f, 0.0f, 1.0f)));
            // glm::mat3 rotMat2 = glm::mat3(glm::rotate(glm::radians(5.0f * random), glm::vec3(1.0f, 0.0f, 0.0f)));
            instance.acceleration = accrleration;  // *rotMat1* rotMat2;
            instance.lifeDuration = glm::mix(lifeDurationMinMs, lifeDurationMaxMs, random);
            instance.alphaK = distr(gen);
            instance.birthTimeMs = -random * instance.lifeDuration;
        }
        return true;
    });
}

void Particle::init(bool useTransferQueue) {
    auto p_devide = m_vkState._core.getDevice();
    assert(p_devide);
    assert(m_pipelineCreatorTextured);
    assert(!m_textureFileName.empty());

    m_uboParticle.params.mode = static_cast<int32_t>(m_mode);
    m_uboParticle.buffers.assign(m_vkState._swapchainImageCount, VK_NULL_HANDLE);
    m_uboParticle.buffersMemory.assign(m_vkState._swapchainImageCount, VK_NULL_HANDLE);
    VkDeviceSize uboBufSize = sizeof(UBOParticle::Params);
    void* data;
    for (size_t i = 0u; i < m_vkState._swapchainImageCount; ++i) {
        Utils::VulkanCreateBuffer(m_vkState._core.getDevice(), m_vkState._core.getPhysDevice(), uboBufSize,
                                  VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  m_uboParticle.buffers[i], m_uboParticle.buffersMemory[i]);

        vkMapMemory(m_vkState._core.getDevice(), m_uboParticle.buffersMemory[i], 0, uboBufSize, 0, &data);
        memcpy(data, &m_uboParticle.params, uboBufSize);
        vkUnmapMemory(m_vkState._core.getDevice(), m_uboParticle.buffersMemory[i]);
    }

    auto texture = m_textureFactory.create2DTexture(m_textureFileName, true, true, useTransferQueue).lock();

    if (m_verticesPreparedFuture.get()) {
        // STATIC: (general buffer: vertices + instances)
        //     binding 0 -> m_generalBuffer + offset 0
        //     binding 1 -> m_generalBuffer + m_verticesBufferOffset
        // 
        // GHOST / ANCHORED / GHOST_GPGPU:
        //     binding 0 -> m_generalBuffer
        //     binding 1 -> m_instanceBuffers[image]

        const VkDeviceSize vertexAtributesSize = sizeof(m_vertices[0]) * m_vertices.size();
        m_verticesBufferOffset = vertexAtributesSize;
        const VkDeviceSize instancesSize = sizeof(m_instances[0]) * m_instances.size();
        const VkDeviceSize bufferSize = vertexAtributesSize +
                        (m_mode == ParticleMode::STATIC ? instancesSize : 0u);

        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        Utils::VulkanCreateBuffer(p_devide, m_vkState._core.getPhysDevice(), bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer,
                                  stagingBufferMemory);

        void* data;
        vkMapMemory(p_devide, stagingBufferMemory, 0, bufferSize, 0, &data);
        memcpy(data, m_vertices.data(), (size_t)vertexAtributesSize);
        if (m_mode == ParticleMode::STATIC) {
            memcpy(static_cast<char*>(data) + m_verticesBufferOffset, m_instances.data(), instancesSize);
        }
        vkUnmapMemory(p_devide, stagingBufferMemory);

        Utils::VulkanCreateBuffer(
            p_devide, m_vkState._core.getPhysDevice(), bufferSize,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_generalBuffer, m_generalBufferMemory);

        const VkQueue queue = useTransferQueue ? m_vkState._transferQueue : m_vkState._queue;
        const VkCommandPool commandPool = useTransferQueue ? m_vkState._transferCmdBufPool : m_vkState._cmdBufPool;
        Utils::VulkanCopyBuffer(p_devide, queue, commandPool, stagingBuffer, m_generalBuffer, bufferSize);
        if (useTransferQueue && m_vkState._core.hasDedicatedTransferQueue()) {
            Utils::VulkanReleaseBufferOwnership(p_devide, queue, commandPool, m_generalBuffer,
                                                m_vkState._core.getTransferQueueFamily(), m_vkState._core.getQueueFamily());
            m_vkState.registerTransferBufferOwnership(m_generalBuffer);
        }

        vkDestroyBuffer(p_devide, stagingBuffer, nullptr);
        vkFreeMemory(p_devide, stagingBufferMemory, nullptr);

        const VkDeviceSize instanceBufferSize = sizeof(m_instances[0]) * m_instances.size();
        m_instanceBuffers.assign(m_vkState._swapchainImageCount, VK_NULL_HANDLE);
        m_instanceBuffersMemory.assign(m_vkState._swapchainImageCount, VK_NULL_HANDLE);
        const VkMemoryPropertyFlags instanceMemoryProperties =
            m_mode == ParticleMode::GHOST_GPGPU
                ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
                : VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for (uint32_t i = 0u; i < m_vkState._swapchainImageCount; ++i) {
            Utils::VulkanCreateBuffer(p_devide, m_vkState._core.getPhysDevice(), instanceBufferSize,
                                      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                      instanceMemoryProperties,
                                      m_instanceBuffers[i], m_instanceBuffersMemory[i]);
            if (m_mode == ParticleMode::GHOST_GPGPU) {
                VkBuffer instanceStagingBuffer = VK_NULL_HANDLE;
                VkDeviceMemory instanceStagingMemory = VK_NULL_HANDLE;
                Utils::VulkanCreateBuffer(
                    p_devide, m_vkState._core.getPhysDevice(), instanceBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    instanceStagingBuffer, instanceStagingMemory);
                vkMapMemory(p_devide, instanceStagingMemory, 0, instanceBufferSize, 0, &data);
                memcpy(data, m_instances.data(), instanceBufferSize);
                vkUnmapMemory(p_devide, instanceStagingMemory);
                Utils::VulkanCopyBuffer(p_devide, queue, commandPool, instanceStagingBuffer,
                                        m_instanceBuffers[i], instanceBufferSize);
                vkDestroyBuffer(p_devide, instanceStagingBuffer, nullptr);
                vkFreeMemory(p_devide, instanceStagingMemory, nullptr);
            } else {
                vkMapMemory(p_devide, m_instanceBuffersMemory[i], 0, instanceBufferSize, 0, &data);
                memcpy(data, m_instances.data(), instanceBufferSize);
                vkUnmapMemory(p_devide, m_instanceBuffersMemory[i]);
            }
        }
    }

    if (m_mode != ParticleMode::STATIC) {
        auto textureGradient =
            m_textureFactory.create2DTexture(m_textureGradientFileName, false, true, useTransferQueue).lock();
        mMaterialId = m_pipelineCreatorTextured->createDescriptor(
            texture, m_textureFactory.getTextureSampler(texture->mipLevels), textureGradient,
            m_textureFactory.getTextureSampler(textureGradient->mipLevels), &m_uboParticle, this);
    } else { // without gradient for bushes
        mMaterialId = m_pipelineCreatorTextured->createDescriptor(
            texture, m_textureFactory.getTextureSampler(texture->mipLevels), texture,
            m_textureFactory.getTextureSampler(0u), &m_uboParticle, this);
    }

    publishReadyAfterTransfer(useTransferQueue);
}

void Particle::draw(VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex, [[maybe_unused]] uint32_t dynamicOffset) const {
    assert(m_generalBuffer);
    assert(m_pipelineCreatorTextured);
    assert(m_pipelineCreatorTextured->getPipeline().get());

    vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineCreatorTextured->getPipeline().get()->pipeline);

    VkBuffer vertexBuffers[] = {m_generalBuffer, m_instanceBuffers[descriptorSetIndex]};
    static VkDeviceSize offsetsVertexAttributes[] = {0u};
    static VkDeviceSize offsetsInstances[] = {0u};
    vkCmdBindVertexBuffers(cmdBuf, 0, 1, vertexBuffers, offsetsVertexAttributes);
    if (m_mode == ParticleMode::STATIC) {
        const VkDeviceSize staticInstanceOffset = m_verticesBufferOffset;
        vkCmdBindVertexBuffers(cmdBuf, 1, 1, &m_generalBuffer, &staticInstanceOffset);
    } else {
        vkCmdBindVertexBuffers(cmdBuf, 1, 1, &vertexBuffers[1], offsetsInstances);
    }

    vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipelineCreatorTextured->getPipeline().get()->pipelineLayout, 0, 1,
                            m_pipelineCreatorTextured->getDescriptorSet(descriptorSetIndex, mMaterialId), 0, VK_NULL_HANDLE);
    /// Note: designed for VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP
    vkCmdDraw(cmdBuf, 4, m_instanceCount, 0, 0);
}

StaticParticle::StaticParticle(const VulkanState& vulkanState, TextureFactory& textureFactory,
                               std::string_view textureFileName, PipelineCreatorParticle* pipelineCreatorTextured,
                               uint32_t instancesAmount, float zFar, const glm::vec3& scale) noexcept(true)
    : Particle(vulkanState, textureFactory, textureFileName, pipelineCreatorTextured, instancesAmount, zFar, scale) {
}

void StaticParticle::update(uint32_t, float, const glm::vec4&, const glm::vec4&) {
}

AnchoredParticle::AnchoredParticle(const VulkanState& vulkanState, TextureFactory& textureFactory,
                                   std::string_view textureFileName, PipelineCreatorParticle* pipelineCreatorTextured,
                                   uint32_t instancesAmount, float zFar, const glm::vec3& scale) noexcept(true)
    : Particle(vulkanState, textureFactory, textureFileName, pipelineCreatorTextured, instancesAmount, zFar, scale) {
}

AnchoredParticle::AnchoredParticle(const VulkanState& vulkanState, TextureFactory& textureFactory,
                                   std::string_view particleTextureFileName, std::string_view particleGradientTextureFileName,
                                   PipelineCreatorParticle* pipelineCreatorTextured, uint32_t instancesAmount,
                                   const glm::vec3& positionOrigin, const glm::vec3& velocity, const glm::vec3& minScale,
                                   const glm::vec3& maxScale, float lifeDurationMinMs, float lifeDurationMaxMs) noexcept(true)
    : Particle(vulkanState, textureFactory, particleTextureFileName, particleGradientTextureFileName,
               pipelineCreatorTextured, instancesAmount, positionOrigin, velocity, minScale, maxScale, lifeDurationMinMs,
               lifeDurationMaxMs) {
}

void AnchoredParticle::update(uint32_t currentImage, float deltaMS, const glm::vec4& offsetPosition,
                              const glm::vec4& velocity) {
    static VkDeviceSize uboBufSize = sizeof(UBOParticle::Params);
    void* data;
    if (m_isFirstSmoothedEmitterUpdate) {
        m_smoothedEmitterVelocity = velocity;
        m_isFirstSmoothedEmitterUpdate = false;
    } else {
        const float smoothing = 1.0f - std::exp(-deltaMS / 180.0f);
        m_smoothedEmitterVelocity = glm::mix(m_smoothedEmitterVelocity, velocity, smoothing);
    }
    m_uboParticle.params.dynamicPos = offsetPosition;
    m_uboParticle.params.velocity = m_smoothedEmitterVelocity;
    vkMapMemory(m_vkState._core.getDevice(), m_uboParticle.buffersMemory[currentImage], 0, uboBufSize, 0, &data);
    memcpy(data, &m_uboParticle.params, uboBufSize);
    vkUnmapMemory(m_vkState._core.getDevice(), m_uboParticle.buffersMemory[currentImage]);

}

GhostParticle::GhostParticle(const VulkanState& vulkanState, TextureFactory& textureFactory,
                             std::string_view particleTextureFileName, std::string_view particleGradientTextureFileName,
                             PipelineCreatorParticle* pipelineCreatorTextured, uint32_t instancesAmount,
                             const glm::vec3& positionOrigin, const glm::vec3& velocity, const glm::vec3& minScale,
                             const glm::vec3& maxScale, float lifeDurationMinMs, float lifeDurationMaxMs) noexcept(true)
    : AnchoredParticle(vulkanState, textureFactory, particleTextureFileName, particleGradientTextureFileName,
                       pipelineCreatorTextured, instancesAmount, positionOrigin, velocity, minScale, maxScale,
                       lifeDurationMinMs, lifeDurationMaxMs) {
    m_mode = ParticleMode::GHOST;
}

void GhostParticle::update(uint32_t currentImage, float deltaMS, const glm::vec4& offsetPosition,
                           const glm::vec4& velocity) {
    if (m_isFirstSmoothedEmitterUpdate) {
        m_smoothedEmitterVelocity = velocity;
        m_isFirstSmoothedEmitterUpdate = false;
    } else {
        const float smoothing = 1.0f - std::exp(-deltaMS / 180.0f);
        m_smoothedEmitterVelocity = glm::mix(m_smoothedEmitterVelocity, velocity, smoothing);
    }

    const float elapsedTimeMs = m_vkState._pushConstant.windDirElapsedTimeMS.w;
    for (auto& instance : m_instances) {
        if (m_isFirstGhostUpdate || elapsedTimeMs - instance.birthTimeMs >= instance.lifeDuration) {
            instance.pos = glm::vec3(offsetPosition);
            instance.velocity = glm::vec3(m_smoothedEmitterVelocity);
            instance.birthTimeMs = elapsedTimeMs;
        }
    }
    m_isFirstGhostUpdate = false;

    const VkDeviceSize instancesSize = sizeof(m_instances[0]) * m_instances.size();
    void* data;
    vkMapMemory(m_vkState._core.getDevice(), m_instanceBuffersMemory[currentImage], 0, instancesSize, 0, &data);
    memcpy(data, m_instances.data(), instancesSize);
    vkUnmapMemory(m_vkState._core.getDevice(), m_instanceBuffersMemory[currentImage]);
}

GhostParticleGPGPU::GhostParticleGPGPU(const VulkanState& vulkanState, TextureFactory& textureFactory,
                                       std::string_view particleTextureFileName,
                                       std::string_view particleGradientTextureFileName,
                                       PipelineCreatorParticle* pipelineCreatorTextured, uint32_t instancesAmount,
                                       const glm::vec3& positionOrigin, const glm::vec3& velocity,
                                       const glm::vec3& minScale, const glm::vec3& maxScale,
                                       float lifeDurationMinMs, float lifeDurationMaxMs) noexcept(true)
    : Particle(vulkanState, textureFactory, particleTextureFileName, particleGradientTextureFileName,
               pipelineCreatorTextured, instancesAmount, positionOrigin, velocity, minScale, maxScale,
               lifeDurationMinMs, lifeDurationMaxMs) {
    m_mode = ParticleMode::GHOST_GPGPU;

    m_computeParams.position = glm::vec4(positionOrigin, 1.0f);
    m_computeParams.velocity = glm::vec4(velocity, 0.0f);
}

GhostParticleGPGPU::~GhostParticleGPGPU() {
    if (m_computePipeline != VK_NULL_HANDLE && m_vkState._core.getDevice() != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_vkState._core.getDevice(), m_computePipeline, nullptr);
    }
}

void GhostParticleGPGPU::init(bool useTransferQueue) {
    assert(m_pipelineCreatorTextured);
    assert(m_pipelineCreatorTextured->getPipeline());

    const VkPipelineLayout layout = m_pipelineCreatorTextured->getPipeline()->pipelineLayout;
    const VkShaderModule shader = Utils::VulkanCreateShaderModule(m_vkState._core.getDevice(), "comp_particle_gpgpu.spv");
    VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = shader;
    stage.pName = "main";
    VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipelineInfo.stage = stage;
    pipelineInfo.layout = layout;
    CHECK_VULKAN_ERROR("vkCreateComputePipelines (particle GPGPU) error %d\n",
                       vkCreateComputePipelines(m_vkState._core.getDevice(), VK_NULL_HANDLE, 1u,
                                                &pipelineInfo, nullptr, &m_computePipeline));
    vkDestroyShaderModule(m_vkState._core.getDevice(), shader, nullptr);

    Particle::init(useTransferQueue);
}

void GhostParticleGPGPU::update(uint32_t, float deltaMS, const glm::vec4& offsetPosition,
                                const glm::vec4& velocity) {
    m_computeParams.position = offsetPosition;
    m_computeParams.velocity = velocity;
    m_computeParams.time.x = m_vkState._pushConstant.windDirElapsedTimeMS.w;
    m_computeParams.time.y = deltaMS;
    m_computeParams.time.z = offsetPosition.w;
}

void GhostParticleGPGPU::recordCompute(VkCommandBuffer commandBuffer, uint32_t currentImage) const {
    if (!isReady() || m_computePipeline == VK_NULL_HANDLE || m_instanceBuffers.empty()) {
        return;
    }
    const auto* particlePipeline = static_cast<const PipelineCreatorParticle*>(m_pipelineCreatorTextured);
    const VkPipelineLayout layout = particlePipeline->getPipeline()->pipelineLayout;
    const VkDescriptorSet* descriptorSet = particlePipeline->getDescriptorSet(currentImage, mMaterialId);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0u, 1u, descriptorSet, 0u, nullptr);
    vkCmdPushConstants(commandBuffer, layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT |
                           VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
                       0u, sizeof(ComputeParams), &m_computeParams);
    // 60 particles -> 1 workgroup
    // 64 particles -> 1 workgroup
    // 65 particles -> 2 workgroups
    // 150 particles -> 3 workgroups
    // 300 particles -> 5 workgroups
    // (m_instanceCount + 63u) / 64u this is integer rounding up:
    vkCmdDispatch(commandBuffer, (m_instanceCount + 63u) / 64u, 1u, 1u);

    VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
    barrier.buffer = m_instanceBuffers[currentImage];
    barrier.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                         0u, 0u, nullptr, 1u, &barrier, 0u, nullptr);
}
