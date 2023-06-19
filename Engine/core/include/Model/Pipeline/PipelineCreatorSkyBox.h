#pragma once

#include "PipelineCreatorTextured.h"

class PipelineCreatorSkyBox : public PipelineCreatorTextured {
public:
    PipelineCreatorSkyBox(const VulkanState& vkState, std::string_view vertShader, std::string_view fragShader,
                          uint32_t subpass = 0u, VkPushConstantRange pushConstantRange = {0u, 0u, 0u})
        : PipelineCreatorTextured(vkState, 1U, vertShader, fragShader, subpass, pushConstantRange) {
    }

private:
    void createPipeline(VkRenderPass renderPass) override;
};
