#pragma once

#include "vulkan/vulkan.h"
#include "string"

class IControl
{
public:

    struct WindowQueueMSG
    {
        bool isQuited = false;
    };

    constexpr IControl(std::wstring_view appName, size_t width, size_t height)
        : m_appName(appName)
        , m_width(width)
        , m_height(height)
        , m_windowQueueMSG()
    {}

    virtual ~IControl() = default;

    virtual void init() = 0;

    virtual VkSurfaceKHR createSurface(VkInstance& inst) const = 0;

    inline std::wstring_view getAppName() const 
    { 
        return m_appName;
    }

    virtual std::string_view getVulkanWindowSurfaceExtension() const
    {
        return "";
    }

    virtual const WindowQueueMSG& processWindowQueueMSGs() { return m_windowQueueMSG; }

protected:
    std::wstring_view m_appName;
    size_t m_width;
    size_t m_height;
    IControl::WindowQueueMSG m_windowQueueMSG;
};