#include "PipelineCreatorBase.h"
#include "Utils.h"

Pipeliner::pipeline_ptr PipelineCreatorBase::recreate(uint32_t width, uint32_t height, 
    VkRenderPass renderPass, VkDevice device)
{
    if(!m_descriptorSetLayout)
       createDescriptorSetLayout(device);

    ///make a reset if exists
    m_pipeline.reset();
    createPipeline(width, height, renderPass, device);
}

// default textured layout
void PipelineCreatorBase::createDescriptorSetLayout(VkDevice device)
{
    // UboViewProjection Binding Info
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.pImmutableSamplers = nullptr;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    // Model Binding Info
    VkDescriptorSetLayoutBinding modelLayoutBinding = {};
    modelLayoutBinding.binding = 1;
    modelLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    modelLayoutBinding.descriptorCount = 1;
    modelLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    modelLayoutBinding.pImmutableSamplers = nullptr;

    // Texture
    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 2;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.pImmutableSamplers = nullptr;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::vector<VkDescriptorSetLayoutBinding> layoutBindings = { uboLayoutBinding, modelLayoutBinding, samplerLayoutBinding };
    // Create Descriptor Set Layout with given bindings
    VkDescriptorSetLayoutCreateInfo layoutCreateInfo = {};
    layoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutCreateInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());;
    layoutCreateInfo.pBindings = layoutBindings.data();

    VkDescriptorSetLayout descriptorSetLayout;
    if (vkCreateDescriptorSetLayout(device, &layoutCreateInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        Utils::printLog(ERROR_PARAM, "failed to create descriptor set layout!");
    }

    m_descriptorSetLayout.reset(&descriptorSetLayout);
    m_descriptorSetLayout.get_deleter() = [device](VkDescriptorSetLayout* p)
    {
        vkDestroyDescriptorSetLayout(device, *p, nullptr);
    };
}


