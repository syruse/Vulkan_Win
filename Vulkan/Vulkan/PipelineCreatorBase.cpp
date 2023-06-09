#include "PipelineCreatorBase.h"
#include "Utils.h"

void PipelineCreatorBase::recreate(VkRenderPass renderPass) {
    if (!m_descriptorSetLayout) {
        createDescriptorSetLayout();
    }

    /// make a reset if exists
    m_pipeline.reset();
    auto deleter = [device = m_vkState._core.getDevice()](VkDescriptorSetLayout* p) {
        assert(device);
        Utils::printLog(INFO_PARAM, "removal triggered");
        vkDestroyDescriptorSetLayout(device, *p, nullptr);
        delete p;
    };
    m_descriptorSetLayout.get_deleter() = deleter;

    createPipeline(renderPass);
}

void PipelineCreatorBase::destructDescriptorPool() {
    assert(m_vkState._core.getDevice());
    assert(m_descriptorPool);
    vkDestroyDescriptorPool(m_vkState._core.getDevice(), m_descriptorPool, nullptr);
}

// default textured layout
void PipelineCreatorBase::createDescriptorSetLayout() {
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

    std::vector<VkDescriptorSetLayoutBinding> layoutBindings = {uboLayoutBinding, modelLayoutBinding, samplerLayoutBinding};
    // Create Descriptor Set Layout with given bindings
    VkDescriptorSetLayoutCreateInfo layoutCreateInfo = {};
    layoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutCreateInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());
    layoutCreateInfo.pBindings = layoutBindings.data();

    m_descriptorSetLayout = std::make_unique<VkDescriptorSetLayout>();
    if (vkCreateDescriptorSetLayout(m_vkState._core.getDevice(), &layoutCreateInfo, nullptr, m_descriptorSetLayout.get()) !=
        VK_SUCCESS) {
        Utils::printLog(ERROR_PARAM, "failed to create descriptor set layout!");
    }
}
