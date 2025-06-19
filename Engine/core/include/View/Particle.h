#pragma once

#include <future>
#include <glm/glm.hpp>
#include "I3DModel.h"
#include "Pipeliner.h"
#include "TextureFactory.h"
#include "VulkanState.h"

class PipelineCreatorParticle;

class Particle : public I3DModel {
public:
    enum class ParticleMode {
        DEFAULT = 0,      // regular particles effect(parallel to user face)
        ZPLANE_SPREADING  // spreading along z-plane(which perpendicular to z-plane)
    };
    struct alignas(16) Origin {
        glm::vec3 pos{0.0f};
        glm::vec3 scaleMin{1.0f};
        glm::vec3 scaleMax{1.0f};
    };
    struct alignas(16) Instance {
        glm::vec3 pos{0.0f};
        glm::vec3 acceleration{0.0f};
        float lifeDuration{1.0f};  // ms allocated for life of particle
        float speedK{1.0f};
        float alphaK{1.0f};
    };

    struct UBOParticle {
        struct Params {
            alignas(16) glm::vec4 dynamicPos{0.0f};  // some offset for DEFAULT only particle system for tracking some object
            alignas(16) glm::vec4 velocity{0.0f};
            alignas(16) int32_t mode{static_cast<int32_t>(ParticleMode::DEFAULT)};
        };
        std::array<VkBuffer, VulkanState::MAX_FRAMES_IN_FLIGHT> buffers{};
        std::array<VkDeviceMemory, VulkanState::MAX_FRAMES_IN_FLIGHT> buffersMemory{};
        Params params;
    };

    static const std::array<VkVertexInputBindingDescription, 2u>& getBindingDescription() {
        static std::array<VkVertexInputBindingDescription, 2u> bindingDescriptions;
        bindingDescriptions[0].binding = 0;
        bindingDescriptions[0].stride = sizeof(Origin);
        bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        bindingDescriptions[1].binding = 1;
        bindingDescriptions[1].stride = sizeof(Instance);
        bindingDescriptions[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
        return bindingDescriptions;
    }

    static const std::array<VkVertexInputAttributeDescription, 8u>& getAttributeDescription() {
        static std::array<VkVertexInputAttributeDescription, 8u> attributeDescriptions{};
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Origin, pos);
        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(Origin, scaleMin);
        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[2].offset = offsetof(Origin, scaleMax);

        attributeDescriptions[3].binding = 1;
        attributeDescriptions[3].location = 3;
        attributeDescriptions[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[3].offset = offsetof(Instance, pos);
        attributeDescriptions[4].binding = 1;
        attributeDescriptions[4].location = 4;
        attributeDescriptions[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[4].offset = offsetof(Instance, acceleration);
        attributeDescriptions[5].binding = 1;
        attributeDescriptions[5].location = 5;
        attributeDescriptions[5].format = VK_FORMAT_R32_SFLOAT;
        attributeDescriptions[5].offset = offsetof(Instance, lifeDuration);
        attributeDescriptions[6].binding = 1;
        attributeDescriptions[6].location = 6;
        attributeDescriptions[6].format = VK_FORMAT_R32_SFLOAT;
        attributeDescriptions[6].offset = offsetof(Instance, speedK);
        attributeDescriptions[7].binding = 1;
        attributeDescriptions[7].location = 7;
        attributeDescriptions[7].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[7].offset = offsetof(Instance, alphaK);
        return attributeDescriptions;
    }

    ~Particle();

    // for filling z plane with particles (bushes, wind cloud etc) which perpendicular to z-plane
    Particle(const VulkanState& vulkanState, TextureFactory& textureFactory, std::string_view textureFileName,
             PipelineCreatorParticle* pipelineCreator, uint32_t instancesAmount, float zFar = 0.0f,
             const glm::vec3& scale = glm::vec3(1.0f)) noexcept(true);

    // for effects like fire, smoke etc, parallel to user face
    Particle(const VulkanState& vulkanState, TextureFactory& textureFactory, std::string_view particleTextureFileName,
             std::string_view particleGradientTextureFileName, PipelineCreatorParticle* pipelineCreator, uint32_t instancesAmount,
             const glm::vec3& positionOrigin = glm::vec3(0.0f), const glm::vec3& velocity = glm::vec3(0.0f),
             const glm::vec3& minScale = glm::vec3(1.0f), const glm::vec3& maxScale = glm::vec3(1.0f)) noexcept(true);

    void update(uint32_t currentImage, float deltaMS = 0.0f, const glm::vec4& offsetPosition = glm::vec4(0.0f),
                const glm::vec4& velocity = glm::vec4(0.0f));
    void init() override;
    void draw(VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex, [[maybe_unused]] uint32_t dynamicOffset = 0u) const override;

private:
    uint32_t mMaterialId{0u};
    std::string_view m_textureFileName{};
    std::string_view m_textureGradientFileName{};
    PipelineCreatorParticle* m_pipelineCreatorTextured{nullptr};
    uint32_t m_instanceCount{0u};
    std::array<Origin, 4u> m_vertices{};
    std::vector<Particle::Instance> m_instances{};
    float m_zFar{1.0f};
    glm::vec3 m_maxScale{1.0f};
    glm::vec3 m_minScale{1.0f};
    std::future<bool> m_verticesPreparedFuture{};
    ParticleMode m_mode{ParticleMode::DEFAULT};
    UBOParticle m_uboParticle{};
};
