#include "Particle.h"
#include <assert.h>
#include <glm/gtx/transform.hpp>
#include <random>
#include "I3DModel.h"
#include "PipelineCreatorParticle.h"
#include "Utils.h"

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
      m_mode{ParticleMode::ZPLANE_SPREADING} {
    assert(zFar > 1.0f);
    pipelineCreatorTextured->increaseUsageCounter();
    m_instances.resize(m_instanceCount, Particle::Instance{});

    m_verticesPreparedFuture = std::async(std::launch::async, [this] {
        for (auto& vertex : m_vertices) {
            vertex.mode = static_cast<int32_t>(m_mode);
            vertex.scaleMax = m_maxScale;
            vertex.scaleMin = m_maxScale;
        }

        std::random_device rd;
        std::mt19937 gen(rd());  // seed the generator
        uint32_t limit = static_cast<uint32_t>(m_zFar);
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

Particle::Particle(const VulkanState& vulkanState, TextureFactory& textureFactory, std::string_view particleTextureFileName,
                   std::string_view particleGradientTextureFileName, PipelineCreatorParticle* pipelineCreatorTextured,
                   uint32_t instancesAmount, const glm::vec3& positionOrigin, const glm::vec3& velocity,
                   const glm::vec3& minScale, const glm::vec3& maxScale) noexcept(true)
    : I3DModel(vulkanState, textureFactory, pipelineCreatorTextured),
      m_textureFileName(particleTextureFileName),
      m_textureGradientFileName(particleGradientTextureFileName),
      m_pipelineCreatorTextured(pipelineCreatorTextured),
      m_instanceCount(instancesAmount),
      m_maxScale{maxScale},
      m_minScale{minScale},
      m_mode{ParticleMode::DEFAULT} {
    pipelineCreatorTextured->increaseUsageCounter();
    m_instances.resize(m_instanceCount, Particle::Instance{});

    m_verticesPreparedFuture = std::async(std::launch::async, [this, positionOrigin, vel = glm::normalize(velocity)] {
        for (auto& vertex : m_vertices) {
            vertex.mode = static_cast<int32_t>(m_mode);
            vertex.scaleMax = m_maxScale;
            vertex.scaleMin = m_minScale;
            vertex.velocity = vel;
            vertex.pos = positionOrigin;
        }

        glm::vec3 up{0.0f, 1.0f, 0.0f};
        std::random_device rd;
        std::mt19937 gen(rd());                                 // seed the generator
        std::uniform_real_distribution<float> distr(0.1, 1.0);  // define the range
        for (std::size_t i = 0u; i < m_instances.size(); ++i) {
            auto& instance = m_instances[i];
            float random = distr(gen);
            // matrix for spreading non linear way
            //glm::mat3 rotMat1 = glm::mat3(glm::rotate(glm::radians(5.0f * random), glm::vec3(0.0f, 0.0f, 1.0f)));
            //glm::mat3 rotMat2 = glm::mat3(glm::rotate(glm::radians(5.0f * random), glm::vec3(1.0f, 0.0f, 0.0f)));
            instance.acceleration = up;// *rotMat1* rotMat2;
            instance.lifeDuration = distr(gen) * 3000000.0f;  // max is 3s
            instance.speedK = glm::mix(60, 100, distr(gen));
            instance.alphaK = distr(gen);
        }
        return true;
    });
}

void Particle::init() {
    auto p_devide = m_vkState._core.getDevice();
    assert(p_devide);
    assert(m_pipelineCreatorTextured);
    assert(!m_textureFileName.empty());

    auto texture = m_textureFactory.create2DTexture(m_textureFileName).lock();
    if (m_mode == ParticleMode::DEFAULT) {
        auto textureGradient =
            m_textureFactory.create2DTexture(m_textureGradientFileName, false, true).lock();  // without mip levels
        mMaterialId = m_pipelineCreatorTextured->createDescriptor(texture, m_textureFactory.getTextureSampler(texture->mipLevels),
                                                                  textureGradient,
                                                                  m_textureFactory.getTextureSampler(textureGradient->mipLevels));
    } else {  // without gradient for bushes ...
        mMaterialId = m_pipelineCreatorTextured->createDescriptor(texture, m_textureFactory.getTextureSampler(texture->mipLevels),
                                                                  texture, m_textureFactory.getTextureSampler(0u));
    }

    if (m_verticesPreparedFuture.get()) {
        const VkDeviceSize vertexAtributesSize = sizeof(m_vertices[0]) * m_vertices.size();
        m_verticesBufferOffset = vertexAtributesSize;
        const VkDeviceSize instancesSize = sizeof(m_instances[0]) * m_instances.size();
        const VkDeviceSize bufferSize = instancesSize + vertexAtributesSize;

        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        Utils::VulkanCreateBuffer(p_devide, m_vkState._core.getPhysDevice(), bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer,
                                  stagingBufferMemory);

        void* data;
        vkMapMemory(p_devide, stagingBufferMemory, 0, bufferSize, 0, &data);
        memcpy(data, m_vertices.data(), (size_t)vertexAtributesSize);
        memcpy(static_cast<char*>(data) + m_verticesBufferOffset, m_instances.data(), instancesSize);
        vkUnmapMemory(p_devide, stagingBufferMemory);

        Utils::VulkanCreateBuffer(
            p_devide, m_vkState._core.getPhysDevice(), bufferSize,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_generalBuffer, m_generalBufferMemory);

        Utils::VulkanCopyBuffer(p_devide, m_vkState._queue, m_vkState._cmdBufPool, stagingBuffer, m_generalBuffer, bufferSize);

        vkDestroyBuffer(p_devide, stagingBuffer, nullptr);
        vkFreeMemory(p_devide, stagingBufferMemory, nullptr);
    }
}

void Particle::draw(VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex, [[maybe_unused]] uint32_t dynamicOffset) const {
    assert(m_generalBuffer);
    assert(m_pipelineCreatorTextured);
    assert(m_pipelineCreatorTextured->getPipeline().get());

    vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineCreatorTextured->getPipeline().get()->pipeline);

    VkBuffer vertexBuffers[] = {m_generalBuffer};
    static VkDeviceSize offsetsVertexAttributes[] = {0u};
    static VkDeviceSize offsetsInstances[] = {m_verticesBufferOffset};
    vkCmdBindVertexBuffers(cmdBuf, 0, 1, vertexBuffers, offsetsVertexAttributes);
    vkCmdBindVertexBuffers(cmdBuf, 1, 1, vertexBuffers, offsetsInstances);

    vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipelineCreatorTextured->getPipeline().get()->pipelineLayout, 0, 1,
                            m_pipelineCreatorTextured->getDescriptorSet(descriptorSetIndex, mMaterialId), 0, VK_NULL_HANDLE);
    /// Note: designed for VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP
    vkCmdDraw(cmdBuf, 4, m_instanceCount, 0, 0);
}
