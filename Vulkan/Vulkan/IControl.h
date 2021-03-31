#pragma once

#include "vulkan/vulkan.h"
#include "string"

class IControl
{
public:

    struct WindowQueueMSG
    {
        bool isQuited = false;
        bool isResized = false;
        unsigned short width = 0;
        unsigned short height = 0;
    };

    constexpr IControl(std::wstring_view appName, size_t width, size_t height)
        : m_appName(appName)
        , m_width(width)
        , m_height(height)
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

    /// falls into NRVO
    virtual WindowQueueMSG processWindowQueueMSGs() { return WindowQueueMSG{}; }

protected:
    std::wstring_view m_appName;
    size_t m_width;
    size_t m_height;
};