#ifdef _WIN32

#include "Win32Control.h"
#include "vulkan/vulkan_win32.h"
#include "vulkan/vk_sdk_platform.h"
#include "Utils.h"
#include <cassert>

LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);

static constexpr const wchar_t* WIN_CLASS_NAME = L"Win32Control";
static IControl::WindowQueueMSG _windowQueueMsg{};

Win32Control::~Win32Control()
{
    DestroyWindow(m_hwnd);
}

std::string_view Win32Control::getVulkanWindowSurfaceExtension() const
{
    return VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
}

void Win32Control::init()
{
    m_hinstance = GetModuleHandle(NULL);
    assert(m_hinstance);

    WNDCLASSEX wndcls = {};

    wndcls.cbSize = sizeof(wndcls);
    wndcls.lpfnWndProc = WindowProc;
    wndcls.hInstance = m_hinstance;
    wndcls.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wndcls.lpszClassName = WIN_CLASS_NAME;

    if (!RegisterClassEx(&wndcls)) {
        DWORD error = GetLastError();
        Utils::printLog(ERROR_PARAM, "RegisterClassEx error %d", error);
    }

    RECT rect;
    rect.left = 50;
    rect.top = 50;
    rect.right = m_width + rect.left;
    rect.bottom = m_height + rect.top;

    UINT style = WS_OVERLAPPEDWINDOW;

    /// Note: before creation of window we need to modify the rect
    /// because real produced rect will be a little less due to invisible borders
    /// let's adjust the rect
    AdjustWindowRectEx(&rect, style, 0, 0);

    m_hwnd = CreateWindowEx(0,
        WIN_CLASS_NAME,                        // class name
        m_appName.data(),
        style,
        rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
        NULL,
        NULL,
        m_hinstance,
        NULL);

    if (m_hwnd == 0) {
        DWORD error = GetLastError();
        Utils::printLog(ERROR_PARAM, "CreateWindowEx error %d", error);
    }

    ShowWindow(m_hwnd, SW_SHOW);
}

VkSurfaceKHR Win32Control::createSurface(VkInstance& inst) const
{
    VkWin32SurfaceCreateInfoKHR surfaceCreateInfo = {};
    surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surfaceCreateInfo.hinstance = m_hinstance;
    surfaceCreateInfo.hwnd = m_hwnd;

    VkSurfaceKHR surface;

    VkResult res = vkCreateWin32SurfaceKHR(inst, &surfaceCreateInfo, NULL, &surface);
    CHECK_VULKAN_ERROR("vkCreateXcbSurfaceKHR error %d\n", res);

    return surface;
}

IControl::WindowQueueMSG Win32Control::processWindowQueueMSGs()
{
    MSG msg;

   /**
   GetMessage does not return until a message matching the filter criteria is placed in the queue, whereas
   PeekMessage returns immediately regardless of whether a message is in the queue.
   Remove any messages that may be in the queue of cpecified type like WM_QUIT
   */
   if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
   {
       /* handle or dispatch messages */
       if (msg.message == WM_QUIT)
       {
           _windowQueueMsg.isQuited = true;
       }
       else
       {
           TranslateMessage(&msg);
           DispatchMessage(&msg);
       }
   }

   IControl::WindowQueueMSG windowQueueMsg(_windowQueueMsg);
   _windowQueueMsg.reset();

   return windowQueueMsg;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CLOSE:
        PostQuitMessage(0);
        break;

    case WM_DESTROY:
        return 0;

    case WM_SIZE:
    {
        _windowQueueMsg.width = LOWORD(lParam);
        _windowQueueMsg.height = HIWORD(lParam);
        _windowQueueMsg.isResized = true;
        break;
    }

    case WM_KEYDOWN:
    {
        switch (wParam)
        {
        case VK_ESCAPE:
            PostQuitMessage(0);
            break;
        }
    }
    break;

    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    return 0;
}


#endif