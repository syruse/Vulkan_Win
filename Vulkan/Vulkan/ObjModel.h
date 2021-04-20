#pragma once

#include "I3DModel.h"

class ObjModel : public I3DModel
{
public:
    ObjModel() = default;

    virtual void draw(VkCommandBuffer cmdBuf, std::function<void(VkImageView imageView, VkSampler sampler)> descriptorUpdater) override;

private:
    virtual void load(std::string_view path) override;
};