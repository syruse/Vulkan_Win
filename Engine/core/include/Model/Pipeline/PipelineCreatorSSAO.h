#pragma once

#include "PipelineCreatorQuad.h"
#include "TextureFactory.h"

class PipelineCreatorSSAO : public PipelineCreatorQuad {
    // SSAO semisphera kernel for sampling
    struct UBOSemiSpheraKernel {
        struct Params {
            glm::vec4 samples[64];
        };
        std::array<VkBuffer, VulkanState::MAX_FRAMES_IN_FLIGHT> buffers{};
        std::array<VkDeviceMemory, VulkanState::MAX_FRAMES_IN_FLIGHT> buffersMemory{};
        Params params;
    };
public:
    PipelineCreatorSSAO(const VulkanState& vkState, VkRenderPass& renderPass, std::string_view vertShader,
                        std::string_view fragShader, uint32_t subpass = 0u);
    ~PipelineCreatorSSAO();

    void createDescriptorPool() override;
    void recreateDescriptors() override;

private:
    void createPipeline() override;
    void createDescriptorSetLayout() override;

private:
    UBOSemiSpheraKernel m_ubo;
    TextureFactory::Texture m_noiseTexture;
    VkSampler mSampler{nullptr};
    bool mIsPoolRecreated{false};
};
