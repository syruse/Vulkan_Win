#pragma once

#include <windows.h>
#include "vulkan/vulkan.h"
#include "IControl.h"
#include "string"

class Win32Control : public IControl
{
public:

    constexpr Win32Control(std::wstring_view appName, size_t width, size_t height)
        : IControl(appName, width, height)
        , m_hinstance(nullptr)
        , m_hwnd(0)
    {
    }

    virtual ~Win32Control();

    virtual void init() override;

    virtual VkSurfaceKHR createSurface(VkInstance& inst) const override;

    virtual std::string_view getVulkanWindowSurfaceExtension() const override;

    virtual WindowQueueMSG processWindowQueueMSGs() override;

private:
    HINSTANCE       m_hinstance;
    HWND            m_hwnd;
};

