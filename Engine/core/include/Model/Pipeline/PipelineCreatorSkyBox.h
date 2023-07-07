#pragma once

#include "PipelineCreatorTextured.h"

class PipelineCreatorSkyBox : public PipelineCreatorTextured {
public:
    PipelineCreatorSkyBox(const VulkanState& vkState, VkRenderPass& renderPass, std::string_view vertShader,
                          std::string_view fragShader, uint32_t subpass = 0u,
                          VkPushConstantRange pushConstantRange = {0u, 0u, 0u})
        : PipelineCreatorTextured(vkState, renderPass, vertShader, fragShader, 1u, subpass, pushConstantRange) {
    }

private:
    void createPipeline() override;
};
