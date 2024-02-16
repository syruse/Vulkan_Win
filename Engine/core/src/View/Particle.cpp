#include "Particle.h"
#include <assert.h>
#include <glm/gtx/transform.hpp>
#include <random>
#include "I3DModel.h"
#include "PipelineCreatorParticle.h"
#include "Utils.h"

void Particle::init() {
    auto p_devide = m_vkState._core.getDevice();
    assert(p_devide);
    assert(m_pipelineCreatorTextured);
    assert(!m_textureFileName.empty());

    auto texture = m_textureFactory.create2DTexture(m_textureFileName).lock();

    if (texture) {
        auto ptr = static_cast<PipelineCreatorParticle*>(m_pipelineCreatorTextured);
        ptr->createDescriptor(texture, m_textureFactory.getTextureSampler(texture->mipLevels));
    }

    std::vector<Particle::Instance> instances(m_instancesAmount, Particle::Instance{});

    glm::mat3 rotMat = glm::mat3(glm::rotate(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
    std::random_device rd;
    std::mt19937 gen(rd());  // seed the generator
    uint32_t limit = static_cast<uint32_t>(m_zFar);
    std::uniform_int_distribution<> distr(-limit, limit);  // define the range
    for (std::size_t i = 0u; i < instances.size(); ++i) {
        auto& instance = instances[i];
        instance.pos.z = distr(gen);
        instance.pos.x = distr(gen);
        instance.pos.y = 0.0f;
        instance.scale = m_scale;
    }

    Utils::createGeneralBuffer(p_devide, m_vkState._core.getPhysDevice(), m_vkState._cmdBufPool, m_vkState._queue,
                               std::vector<uint32_t>{}, instances, m_verticesBufferOffset, m_generalBuffer,
                               m_generalBufferMemory);
}

void Particle::draw(VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex, [[maybe_unused]] uint32_t dynamicOffset) const {
    assert(m_generalBuffer);
    assert(m_pipelineCreatorTextured);
    assert(m_pipelineCreatorTextured->getPipeline().get());

    auto ptr = static_cast<PipelineCreatorParticle*>(m_pipelineCreatorTextured);

    vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, ptr->getPipeline().get()->pipeline);

    VkBuffer vertexBuffers[] = {m_generalBuffer};
    VkDeviceSize offsets[] = {m_verticesBufferOffset};
    vkCmdBindVertexBuffers(cmdBuf, 0, 1, vertexBuffers, offsets);

    vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, ptr->getPipeline().get()->pipelineLayout, 0, 1,
                            ptr->getDescriptorSet(descriptorSetIndex), 0, VK_NULL_HANDLE);
    /// Note: designed for VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP
    vkCmdDraw(cmdBuf, 4, m_instancesAmount, 0, 0);
}
