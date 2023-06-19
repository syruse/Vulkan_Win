#pragma once

#include "I3DModel.h"
#include "PipelineCreatorBase.h"
#include "TextureFactory.h"

class PipelineCreatorTextured : public PipelineCreatorBase {
public:
    // each part of model has own descriptorSet since has unique ImageView binding
    class DescriptorSetData {
        friend PipelineCreatorTextured;

    private:
        DescriptorSetData(const VulkanState& vkState) : m_vkState(vkState) {
        }
        DescriptorSetData(DescriptorSetData&&) = delete;  // copy ctor gets removed as well

        static DescriptorSetData& instance(const VulkanState& vkState) {
            static DescriptorSetData descriptorSetData{vkState};
            return descriptorSetData;
        }

    private:
        const VulkanState& m_vkState;
        uint32_t m_materialId{0u};
        std::unordered_map<uint32_t, I3DModel::Material> m_descriptorSets{};
    };

    PipelineCreatorTextured(const VulkanState& vkState, uint32_t maxObjectsCount, std::string_view vertShader,
                            std::string_view fragShader, uint32_t subpass = 0u,
                            VkPushConstantRange pushConstantRange = {0u, 0u, 0u})
        : PipelineCreatorBase(vkState, vertShader, fragShader, subpass, pushConstantRange), m_maxObjectsCount(maxObjectsCount) {
    }

    void createDescriptorPool() override;
    void recreateDescriptors() override;
    const VkDescriptorSet* getDescriptorSet(uint32_t descriptorSetsIndex, uint32_t materialId = 0u) const override;

    uint32_t createDescriptor(std::weak_ptr<TextureFactory::Texture>, VkSampler);

private:
    void createDescriptorSetLayout() override;
    void createPipeline(VkRenderPass renderPass) override;

private:
    uint32_t m_maxObjectsCount = 1U;
};
