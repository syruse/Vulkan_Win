#pragma once

#include "I3DModel.h"

class ObjModel : public I3DModel
{
public:
    ObjModel() = default;

    virtual void draw(VkCommandBuffer cmdBuf) override;

private:
    virtual void load(std::string_view path) override;
};