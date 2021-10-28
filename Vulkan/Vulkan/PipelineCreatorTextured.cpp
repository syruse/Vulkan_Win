
#include "PipelineCreatorTextured.h"
#include <assert.h>

void PipelineCreatorTextured::createPipeline(uint32_t width, uint32_t height, 
        VkRenderPass renderPass, VkDevice device)
{
    assert(m_descriptorSetLayout);
    assert(renderPass);
    assert(device);

    m_pipeline = Pipeliner::getInstance().createPipeLine(m_vertShader, m_fragShader, width, height,
    m_descriptorSetLayout, renderPass, device, m_subpassAmount, m_pushConstantRange);
    assert(m_pipeline);
}


