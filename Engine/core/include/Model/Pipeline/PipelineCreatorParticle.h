#pragma once

#include "I3DModel.h"
#include "PipelineCreatorTextured.h"

class PipelineCreatorParticle : public PipelineCreatorTextured {
public:
    struct Material {
        std::weak_ptr<TextureFactory::Texture> textureParticle;
        VkSampler samplerParticle;
        std::weak_ptr<TextureFactory::Texture> textureGradient;
        VkSampler samplerGradient;
        VkDescriptorSetLayout descriptorSetLayout;
        std::array<VkDescriptorSet, VulkanState::MAX_FRAMES_IN_FLIGHT> descriptorSets{};
    };

    PipelineCreatorParticle(const VulkanState& vkState, VkRenderPass& renderPass, std::string_view vertShader,
                            std::string_view fragShader, uint32_t subpass = 0u,
                            VkPushConstantRange pushConstantRange = {0u, 0u, 0u})
        : PipelineCreatorTextured(vkState, renderPass, vertShader, fragShader, 1u, subpass, pushConstantRange) {
    }

    uint32_t createDescriptor(std::weak_ptr<TextureFactory::Texture> particleTexture, VkSampler particleSampler,
                              std::weak_ptr<TextureFactory::Texture> gradientTexture, VkSampler gradientSampler);

    void createDescriptorPool() override;
    void recreateDescriptors() override;

    const VkDescriptorSet* getDescriptorSet(uint32_t descriptorSetsIndex, uint32_t materialId = 0u) const override;

    // TODO can be wrapped into some registration function to avoid forgetting
    void increaseUsageCounter() {
        ++m_maxObjectsCount;
    }

private:
    void createPipeline() override;
    void createDescriptorSetLayout() override;

private:
    std::unordered_map<uint32_t, Material> m_descriptorSets{};
    uint32_t m_curMaterialId{0u};
    uint32_t m_maxObjectsCount{0u};
};
