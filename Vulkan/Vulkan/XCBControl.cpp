#ifdef __linux__

#include "XCBControl.h"
#include "vulkan/vulkan_xcb.h"
#include "vulkan/vk_sdk_platform.h"
#include "Utils.h"
#include <cassert>
#include <unistd.h>

static constexpr const wchar_t* WIN_CLASS_NAME = L"XCBControl";
static IControl::WindowQueueMSG _windowQueueMsg{};

XCBControl::~XCBControl()
{
    if (m_xcbWindow) {
        xcb_destroy_window(m_pXCBConn, m_xcbWindow);
    }

    if (m_pXCBConn) {
        xcb_disconnect(m_pXCBConn);
    }
}

std::string_view XCBControl::getVulkanWindowSurfaceExtension() const
{
    return VK_KHR_XCB_SURFACE_EXTENSION_NAME;
}

void XCBControl::init()
{
    m_pXCBConn = xcb_connect(NULL, NULL);

    int error = xcb_connection_has_error(m_pXCBConn);

    if (error) {
        OGLDEV_ERROR("Error opening xcb connection error %d\n", error);
        assert(0);
    }

    printf("XCB connection opened\n");

    const xcb_setup_t* pSetup = xcb_get_setup(m_pXCBConn);
    xcb_screen_iterator_t iter = xcb_setup_roots_iterator(pSetup);

    m_pXCBScreen = iter.data;

    printf("XCB screen %p\n", m_pXCBScreen);

    m_xcbWindow = xcb_generate_id(m_pXCBConn);

    xcb_create_window(m_pXCBConn,
        XCB_COPY_FROM_PARENT,
        m_xcbWindow,
        m_pXCBScreen->root,
        0,
        0,
        Width,
        Height,
        0,
        XCB_WINDOW_CLASS_INPUT_OUTPUT,
        m_pXCBScreen->root_visual,
        0,
        0);

    xcb_map_window(m_pXCBConn, m_xcbWindow);

    xcb_flush(m_pXCBConn);

    printf("Window %x created\n", m_xcbWindow);
}

VkSurfaceKHR XCBControl::createSurface(VkInstance& inst) const
{
    VkXcbSurfaceCreateInfoKHR surfaceCreateInfo = {};
    surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
    surfaceCreateInfo.connection = m_pXCBConn;
    surfaceCreateInfo.window = m_xcbWindow;

    VkSurfaceKHR surface;

    VkResult res = vkCreateXcbSurfaceKHR(inst, &surfaceCreateInfo, NULL, &surface);
    CHECK_VULKAN_ERROR("vkCreateXcbSurfaceKHR error %d\n", res);

    return surface;
}

IControl::WindowQueueMSG XCBControl::processWindowQueueMSGs()
{

}

#endif