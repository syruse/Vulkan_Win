#include "PipelineCreatorQuad.h"
#include <assert.h>
#include "Utils.h"

void PipelineCreatorQuad::createPipeline(uint32_t width, uint32_t height, 
        VkRenderPass renderPass, VkDevice device)
{
    assert(m_descriptorSetLayout);
    assert(renderPass);
    assert(device);

    auto& vertexInputInfo = Pipeliner::getInstance().getVertexInputInfo();
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.pVertexBindingDescriptions = nullptr;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;
    vertexInputInfo.pVertexAttributeDescriptions = nullptr;

    // Don't want to write to depth buffer
    auto& depthStencil = Pipeliner::getInstance().getDepthStencilInfo();
    depthStencil.depthWriteEnable = VK_FALSE;

    auto& pipelineIACreateInfo = Pipeliner::getInstance().getInputAssemblyInfo();
    pipelineIACreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; // as a simple set with two triangles for quad drawing

    m_pipeline = Pipeliner::getInstance().createPipeLine(m_vertShader, m_fragShader,
        width, height, *m_descriptorSetLayout.get(), renderPass, device, m_subpassAmount);

    assert(m_pipeline);
}

void PipelineCreatorQuad::createDescriptorSetLayout(VkDevice device)
{
    ///---------------------------------------------------------------------------///
    // CREATE INPUT ATTACHMENT IMAGE DESCRIPTOR SET LAYOUT
    // Colour Input Binding
    VkDescriptorSetLayoutBinding colourInputLayoutBinding = {};
    colourInputLayoutBinding.binding = 0;
    colourInputLayoutBinding.descriptorType = m_isDepthNeeded ? VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    colourInputLayoutBinding.descriptorCount = 1;
    colourInputLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Array of input attachment bindings
    std::vector<VkDescriptorSetLayoutBinding> inputBindings = { colourInputLayoutBinding };

    if (m_isDepthNeeded) {
        // Depth Input Binding
        VkDescriptorSetLayoutBinding depthInputLayoutBinding = {};
        depthInputLayoutBinding.binding = 1;
        depthInputLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        depthInputLayoutBinding.descriptorCount = 1;
        depthInputLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        inputBindings.push_back(depthInputLayoutBinding);
    }

    // Create a descriptor set layout for input attachments
    VkDescriptorSetLayoutCreateInfo inputLayoutCreateInfo = {};
    inputLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    inputLayoutCreateInfo.bindingCount = static_cast<uint32_t>(inputBindings.size());
    inputLayoutCreateInfo.pBindings = inputBindings.data();

    // Create Descriptor Set Layout
    m_descriptorSetLayout = std::make_unique<VkDescriptorSetLayout>();
    if (vkCreateDescriptorSetLayout(device, &inputLayoutCreateInfo, nullptr, m_descriptorSetLayout.get()) != VK_SUCCESS) {
        Utils::printLog(ERROR_PARAM, "failed to create descriptor set layout for second pass!");
    }
}
