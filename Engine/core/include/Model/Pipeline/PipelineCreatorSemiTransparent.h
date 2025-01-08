#pragma once

#include "Particle.h"
#include "PipelineCreatorTextured.h"

class PipelineCreatorSemiTransparent : public PipelineCreatorTextured {
public:
    PipelineCreatorSemiTransparent(const VulkanState& vkState, VkRenderPass& renderPass, std::string_view vertShader,
                                   std::string_view fragShader, uint32_t subpass = 0u,
                                   VkPushConstantRange pushConstantRange = {0u, 0u, 0u})
        : PipelineCreatorTextured(vkState, renderPass, vertShader, fragShader, 1u, subpass, pushConstantRange) {
    }

    uint32_t createDescriptor(std::weak_ptr<TextureFactory::Texture>, VkSampler) override;

    void createDescriptorPool() override;

private:
    void createPipeline() override;
    void createDescriptorSetLayout() override;
};
