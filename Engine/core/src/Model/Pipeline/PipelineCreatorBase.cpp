#include "PipelineCreatorBase.h"
#include "Utils.h"

void PipelineCreatorBase::recreate() {
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

    createPipeline();
}

void PipelineCreatorBase::destroyDescriptorPool() {
    assert(m_vkState._core.getDevice());
    assert(m_descriptorPool);
    vkDestroyDescriptorPool(m_vkState._core.getDevice(), m_descriptorPool, nullptr);
    m_descriptorPool = nullptr;
}
