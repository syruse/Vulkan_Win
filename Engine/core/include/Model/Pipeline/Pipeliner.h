#pragma once

#include <vulkan/vulkan.h>
#include <functional>
#include <memory>
#include <string>

class Pipeliner {
public:
    struct PipeLine {
        VkShaderModule vsModule = nullptr;
        VkShaderModule fsModule = nullptr;
        VkShaderModule tsCtrlModule = nullptr;
        VkShaderModule tsEvalModule = nullptr;
        VkPipeline pipeline = nullptr;
        VkPipelineLayout pipelineLayout = nullptr;
    };

    using pipeline_ptr = std::unique_ptr<Pipeliner::PipeLine, std::function<void(Pipeliner::PipeLine* p)>>;

    static constexpr uint8_t MAX_COLOR_ATTACHMENTS = 4u;  // TO DO make it flexible

private:
    Pipeliner();

    friend void deletePipeLine(PipeLine* p);

    bool createCache();

public:
    static Pipeliner& getInstance() {
        static Pipeliner pipeliner;
        return pipeliner;
    }

    bool saveCache();

    /// you can customize states of pipeline by get desired and change before invoking createPipeLine
    pipeline_ptr createPipeLine(std::string_view vertShader, std::string_view fragShader, uint32_t width, uint32_t height,
                                VkDescriptorSetLayout descriptorSetLayout, VkRenderPass renderPass, VkDevice device,
                                uint32_t subpass = 0u, VkPushConstantRange pushConstantRange = {0u, 0u, 0u});

    pipeline_ptr createPipeLine(std::string_view vertShader, std::string_view fragShader, std::string_view tessCtrlShader,
                                std::string_view tessEvalShader, uint32_t width, uint32_t height,
                                VkDescriptorSetLayout descriptorSetLayout, VkRenderPass renderPass, VkDevice device,
                                uint32_t subpass = 0u, VkPushConstantRange pushConstantRange = {0u, 0u, 0u});

    /// get and customize states your way (just before invoking createPipeLine)

    inline VkPipelineVertexInputStateCreateInfo& getVertexInputInfo() {
        return m_vertexInputInfo;
    };

    inline VkPipelineInputAssemblyStateCreateInfo& getInputAssemblyInfo() {
        return m_pipelineIACreateInfo;
    };

    inline VkPipelineRasterizationStateCreateInfo& getRasterizationInfo() {
        return m_rastCreateInfo;
    };

    inline VkPipelineMultisampleStateCreateInfo& getMultisampleInfo() {
        return m_pipelineMSCreateInfo;
    };

    inline VkPipelineDepthStencilStateCreateInfo& getDepthStencilInfo() {
        return m_depthStencil;
    };

    inline VkPipelineColorBlendStateCreateInfo& getColorBlendInfo() {
        return m_blendCreateInfo;
    };

    inline VkPipelineTessellationStateCreateInfo& getTessInfo() {
        return m_tessInfo;
    };

private:
    VkDevice m_device{nullptr};
    VkPipelineCache m_pipeline_cache{nullptr};

    VkPipelineShaderStageCreateInfo m_shaderStageCreateInfo[4]{};
    VkPipelineVertexInputStateCreateInfo m_vertexInputInfo{};
    VkPipelineInputAssemblyStateCreateInfo m_pipelineIACreateInfo{};
    VkViewport m_vp{};
    VkRect2D m_scissor{};
    VkPipelineViewportStateCreateInfo m_vpCreateInfo{};
    VkPipelineRasterizationStateCreateInfo m_rastCreateInfo{};
    VkPipelineMultisampleStateCreateInfo m_pipelineMSCreateInfo{};
    VkPipelineColorBlendStateCreateInfo m_blendCreateInfo{};
    VkPipelineDepthStencilStateCreateInfo m_depthStencil{};
    VkPipelineTessellationStateCreateInfo m_tessInfo{};

    /// persistent default configuration
    inline static VkPipelineVertexInputStateCreateInfo _vertexInputInfo{};
    inline static VkPipelineInputAssemblyStateCreateInfo _pipelineIACreateInfo{};
    inline static VkPipelineRasterizationStateCreateInfo _rastCreateInfo{};
    inline static VkPipelineMultisampleStateCreateInfo _pipelineMSCreateInfo{};
    inline static VkPipelineColorBlendStateCreateInfo _blendCreateInfo{};
    inline static VkPipelineDepthStencilStateCreateInfo _depthStencil{};
    inline static VkPipelineTessellationStateCreateInfo _tessInfo{};
};