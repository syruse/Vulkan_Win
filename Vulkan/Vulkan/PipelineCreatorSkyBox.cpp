
#include "PipelineCreatorSkyBox.h"
#include <assert.h>
#include "Skybox.h"

void PipelineCreatorSkyBox::createPipeline(uint32_t width, uint32_t height, 
        VkRenderPass renderPass, VkDevice device)
{
    assert(m_descriptorSetLayout);
    assert(renderPass);
    assert(device);

    auto& vertexInputInfo = Pipeliner::getInstance().getVertexInputInfo();
    constexpr auto bindingDescription = Skybox::Vertex::getBindingDescription();
    constexpr auto attributeDescriptions = Skybox::Vertex::getAttributeDescription();
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 1;
    vertexInputInfo.pVertexAttributeDescriptions = &attributeDescriptions;

    auto& raster = Pipeliner::getInstance().getRasterizationInfo();
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;

    // Don't want to write to depth buffer
    auto& depthStencil = Pipeliner::getInstance().getDepthStencilInfo();
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    auto& pipelineIACreateInfo = Pipeliner::getInstance().getInputAssemblyInfo();
    pipelineIACreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    
    m_pipeline = Pipeliner::getInstance().createPipeLine(m_vertShader, m_fragShader, width, height,
        *m_descriptorSetLayout.get(), renderPass, device);
    assert(m_pipeline);
}


