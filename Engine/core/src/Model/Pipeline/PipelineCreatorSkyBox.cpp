
#include "PipelineCreatorSkyBox.h"
#include <assert.h>
#include "Skybox.h"

void PipelineCreatorSkyBox::createPipeline() {
    assert(m_descriptorSetLayout);
    assert(m_renderPass);
    assert(m_vkState._core.getDevice());

    auto& vertexInputInfo = Pipeliner::getInstance().getVertexInputInfo();
    constexpr auto bindingDescription = Skybox::Vertex::getBindingDescription();
    constexpr auto attributeDescriptions = Skybox::Vertex::getAttributeDescription();
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 1;
    vertexInputInfo.pVertexAttributeDescriptions = &attributeDescriptions;

    auto& raster = Pipeliner::getInstance().getRasterizationInfo();
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;

    auto& depthStencil = Pipeliner::getInstance().getDepthStencilInfo();
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;                   // don't want to write to depth buffer
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;  // skybox has 1.0 Z value for each edge and we need to make sure
                                                                // the skybox passes the depth tests

    auto& pipelineIACreateInfo = Pipeliner::getInstance().getInputAssemblyInfo();
    pipelineIACreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    m_pipeline = Pipeliner::getInstance().createPipeLine(m_vertShader, m_fragShader, m_vkState._width, m_vkState._height,
                                                         *m_descriptorSetLayout.get(), m_renderPass, m_vkState._core.getDevice(),
                                                         0u, m_pushConstantRange);
    assert(m_pipeline);
}
