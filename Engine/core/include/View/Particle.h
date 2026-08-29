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
        STATIC = 0,
        ANCHORED = 1,
        GHOST = 2,
    };
    struct alignas(16) Origin {
        glm::vec3 pos{0.0f};
        glm::vec3 scaleMin{1.0f};
        glm::vec3 scaleMax{1.0f};
    };
    struct alignas(16) Instance {
        glm::vec3 pos{0.0f};  // world-space position at particle birth
        glm::vec3 velocity{0.0f};
        glm::vec3 acceleration{0.0f};
        float lifeDuration{1.0f};  // ms allocated for life of particle
        float alphaK{1.0f};
        float birthTimeMs{0.0f};
    };

    struct UBOParticle {
        struct Params {
            alignas(16) glm::vec4 dynamicPos{0.0f};
            alignas(16) glm::vec4 velocity{0.0f};
            alignas(16) int32_t mode{static_cast<int32_t>(ParticleMode::STATIC)};
        };
        std::vector<VkBuffer> buffers{};
        std::vector<VkDeviceMemory> buffersMemory{};
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

    static const std::array<VkVertexInputAttributeDescription, 9u>& getAttributeDescription() {
        static std::array<VkVertexInputAttributeDescription, 9u> attributeDescriptions{};
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
        attributeDescriptions[4].offset = offsetof(Instance, velocity);
        attributeDescriptions[5].binding = 1;
        attributeDescriptions[5].location = 5;
        attributeDescriptions[5].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[5].offset = offsetof(Instance, acceleration);
        attributeDescriptions[6].binding = 1;
        attributeDescriptions[6].location = 6;
        attributeDescriptions[6].format = VK_FORMAT_R32_SFLOAT;
        attributeDescriptions[6].offset = offsetof(Instance, lifeDuration);
        attributeDescriptions[7].binding = 1;
        attributeDescriptions[7].location = 7;
        attributeDescriptions[7].format = VK_FORMAT_R32_SFLOAT;
        attributeDescriptions[7].offset = offsetof(Instance, alphaK);
        attributeDescriptions[8].binding = 1;
        attributeDescriptions[8].location = 8;
        attributeDescriptions[8].format = VK_FORMAT_R32_SFLOAT;
        attributeDescriptions[8].offset = offsetof(Instance, birthTimeMs);
        return attributeDescriptions;
    }

    virtual ~Particle();

    virtual void update(uint32_t currentImage, float deltaMS = 0.0f, const glm::vec4& offsetPosition = glm::vec4(0.0f),
                        const glm::vec4& velocity = glm::vec4(0.0f)) = 0;

protected:

    // for filling z plane with particles (bushes, wind cloud etc) which perpendicular to z-plane
    Particle(const VulkanState& vulkanState, TextureFactory& textureFactory, std::string_view textureFileName,
             PipelineCreatorParticle* pipelineCreator, uint32_t instancesAmount, float zFar = 0.0f,
             const glm::vec3& scale = glm::vec3(1.0f)) noexcept(true);

    // for effects like fire, smoke etc, parallel to user face
    Particle(const VulkanState& vulkanState, TextureFactory& textureFactory, std::string_view particleTextureFileName,
             std::string_view particleGradientTextureFileName, PipelineCreatorParticle* pipelineCreator,
             uint32_t instancesAmount, const glm::vec3& positionOrigin = glm::vec3(0.0f), const glm::vec3& accrleration = glm::vec3(0.0f),
             const glm::vec3& minScale = glm::vec3(1.0f), const glm::vec3& maxScale = glm::vec3(1.0f), float lifeDurationMinMs = 2000.0f,
             float lifeDurationMaxMs = 3000.0f) noexcept(true);

public:
    void init() override;
    void draw(VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex, [[maybe_unused]] uint32_t dynamicOffset = 0u) const override;

protected:
    uint32_t mMaterialId{0u};
    std::string_view m_textureFileName{};
    std::string_view m_textureGradientFileName{};
    PipelineCreatorParticle* m_pipelineCreatorTextured{nullptr};
    uint32_t m_instanceCount{0u};
    std::array<Origin, 4u> m_vertices{};
    std::vector<Particle::Instance> m_instances{};
    std::vector<VkBuffer> m_instanceBuffers{};
    std::vector<VkDeviceMemory> m_instanceBuffersMemory{};
    float m_zFar{1.0f};
    glm::vec3 m_maxScale{1.0f};
    glm::vec3 m_minScale{1.0f};
    std::future<bool> m_verticesPreparedFuture{};
    ParticleMode m_mode{ParticleMode::STATIC};
    UBOParticle m_uboParticle{};
    glm::vec4 m_smoothedEmitterVelocity{0.0f};
    bool m_isFirstSmoothedEmitterUpdate{true};
};

class StaticParticle final : public Particle {
public:
    StaticParticle(const VulkanState& vulkanState, TextureFactory& textureFactory, std::string_view textureFileName,
                   PipelineCreatorParticle* pipelineCreator, uint32_t instancesAmount, float zFar = 0.0f,
                   const glm::vec3& scale = glm::vec3(1.0f)) noexcept(true);

    void update(uint32_t currentImage, float deltaMS, const glm::vec4& offsetPosition,
                const glm::vec4& velocity) override;
};

class AnchoredParticle : public Particle {
public:
    AnchoredParticle(const VulkanState& vulkanState, TextureFactory& textureFactory, std::string_view textureFileName,
                     PipelineCreatorParticle* pipelineCreator, uint32_t instancesAmount, float zFar = 0.0f,
                     const glm::vec3& scale = glm::vec3(1.0f)) noexcept(true);

    AnchoredParticle(const VulkanState& vulkanState, TextureFactory& textureFactory, std::string_view particleTextureFileName,
                     std::string_view particleGradientTextureFileName, PipelineCreatorParticle* pipelineCreator,
                     uint32_t instancesAmount, const glm::vec3& positionOrigin = glm::vec3(0.0f),
                     const glm::vec3& velocity = glm::vec3(0.0f), const glm::vec3& minScale = glm::vec3(1.0f),
                     const glm::vec3& maxScale = glm::vec3(1.0f), float lifeDurationMinMs = 2000.0f,
                     float lifeDurationMaxMs = 3000.0f) noexcept(true);

    void update(uint32_t currentImage, float deltaMS, const glm::vec4& offsetPosition,
                const glm::vec4& velocity) override;
};

class GhostParticle final : public AnchoredParticle {
public:
    GhostParticle(const VulkanState& vulkanState, TextureFactory& textureFactory, std::string_view particleTextureFileName,
                  std::string_view particleGradientTextureFileName, PipelineCreatorParticle* pipelineCreator,
                  uint32_t instancesAmount, const glm::vec3& positionOrigin = glm::vec3(0.0f),
                  const glm::vec3& velocity = glm::vec3(0.0f), const glm::vec3& minScale = glm::vec3(1.0f),
                  const glm::vec3& maxScale = glm::vec3(1.0f), float lifeDurationMinMs = 2000.0f,
                  float lifeDurationMaxMs = 3000.0f) noexcept(true);

    void update(uint32_t currentImage, float deltaMS, const glm::vec4& offsetPosition,
                const glm::vec4& velocity) override;

private:
    bool m_isFirstGhostUpdate{true};
};
