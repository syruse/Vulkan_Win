
#include "PipelineCreatorTextured.h"

void PipelineCreatorTextured::createPipeline()
{
    m_pipeLine = Pipeliner::getInstance().createPipeLine("vert.spv", "frag.spv", m_width, m_height,
    m_descriptorSetLayout, m_renderPass, m_core.getDevice(), 0u, m_pushConstantRange);
    assert(m_pipeLine);
}


