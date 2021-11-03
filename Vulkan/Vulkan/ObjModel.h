#pragma once

#include "I3DModel.h"

class ObjModel : public I3DModel
{
public:

    ObjModel(std::string_view path, PipelineCreatorBase* pipelineCreatorBase) noexcept(true)
        : I3DModel(pipelineCreatorBase)
        , m_path(path)
    {}

    virtual void init(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool cmdBufPool, VkQueue queue,
        const std::function<uint16_t(std::weak_ptr<TextureFactory::Texture> texture, VkSampler sampler,
            VkDescriptorSetLayout descriptorSetLayout)>& descriptorCreator) override;

    virtual void draw(VkCommandBuffer cmdBuf, std::function<void(uint16_t materialId, VkPipelineLayout pipelineLayout)> descriptorBinding) override;

private:
    void load(TextureFactory* pTextureFactory, 
             std::function<uint16_t(std::weak_ptr<TextureFactory::Texture> texture, VkSampler sampler,
                 VkDescriptorSetLayout descriptorSetLayout)> descriptorCreator,
             std::vector<Vertex> &vertices, std::vector<uint32_t> &indices);

    std::string m_path;
    std::vector<std::vector<SubObject>> m_SubObjects{};
};