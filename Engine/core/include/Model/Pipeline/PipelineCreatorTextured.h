#pragma once

#include "I3DModel.h"
#include "PipelineCreatorBase.h"
#include "TextureFactory.h"

class PipelineCreatorTextured : public PipelineCreatorBase {
public:
    PipelineCreatorTextured(const VulkanState& vkState, VkRenderPass& renderPass, std::string_view vertShader,
                            std::string_view fragShader, uint32_t subpass = 0u,
                            VkPushConstantRange pushConstantRange = {0u, 0u, 0u}, bool expectsArrayTexture = true)
        : PipelineCreatorTextured(vkState, renderPass, vertShader, fragShader, std::string_view{}, std::string_view{}, subpass,
                                  pushConstantRange, expectsArrayTexture) {
    }

    PipelineCreatorTextured(const VulkanState& vkState, VkRenderPass& renderPass, std::string_view vertShader,
                            std::string_view fragShader, std::string_view tessCtrlShader, std::string_view tessEvalShader,
                            uint32_t subpass = 0u, VkPushConstantRange pushConstantRange = {0u, 0u, 0u},
                            bool expectsArrayTexture = true)
        : PipelineCreatorBase(vkState, renderPass, vertShader, fragShader, subpass, pushConstantRange),
          m_tessCtrlShader(tessCtrlShader),
          m_tessEvalShader(tessEvalShader),
          m_expectsArrayTexture(expectsArrayTexture) {
        m_isTessellated = !m_tessCtrlShader.empty() && !m_tessEvalShader.empty();
    }

    void createDescriptorPool() override;
    void recreateDescriptors() override;
    const VkDescriptorSet* getDescriptorSet(uint32_t descriptorSetsIndex, uint32_t materialId = 0u) const override;

    virtual uint32_t createDescriptor(std::weak_ptr<TextureFactory::Texture>, VkSampler);

    /// it must be called for every 3d model instancing to know how big pool needed
    void increaseUsageCounter() {
        ++m_maxObjectsCount;
    }

    // Whether this pipeline's fragment shader binds materials as sampler2DArray (true) or plain sampler2D (false).
    bool expectsArrayTexture() const {
        return m_expectsArrayTexture;
    }

private:
    void createDescriptorSetLayout() override;
    void createPipeline() override;
    uint32_t createDescriptorWithId(std::weak_ptr<TextureFactory::Texture>, VkSampler, uint32_t materialId);

protected:
    uint32_t m_maxObjectsCount{0u};
    uint32_t m_curMaterialId{0u};
    std::string_view m_tessCtrlShader{};
    std::string_view m_tessEvalShader{};
    bool m_isTessellated{false};
    bool m_expectsArrayTexture{true};
    std::unordered_map<uint32_t, I3DModel::Material> m_descriptorSets{};
};
