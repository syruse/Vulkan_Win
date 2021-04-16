#pragma once

#ifdef __linux__

#include "IControl.h"
#include "vulkan/vulkan.h"
#include <xcb/xcb.h>

class XCBControl : public IControl
{
public:

    static constexpr uint32_t MASK = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_RESIZE_REDIRECT | XCB_EVENT_MASK_STRUCTURE_NOTIFY;

    constexpr XCBControl(std::wstring_view appName, size_t width, size_t height)
        : IControl(appName, width, height)
        , m_pXCBConn(nullptr)
        , m_pXCBScreen(nullptr)
    {
    }

    virtual ~XCBControl();

    virtual void init() override;

    virtual VkSurfaceKHR createSurface(VkInstance& inst) const override;

    virtual std::string_view getVulkanWindowSurfaceExtension() const override;

    virtual WindowQueueMSG processWindowQueueMSGs() override;

private:
    xcb_connection_t* m_pXCBConn;
    xcb_screen_t*     m_pXCBScreen;
    xcb_window_t      m_xcbWindow{};
    IControl::WindowQueueMSG m_windowQueueMsg{};
};


#endif
