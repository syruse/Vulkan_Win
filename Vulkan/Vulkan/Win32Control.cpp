#include "Win32Control.h"
#include "Utils.h"
#include <cassert>

LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);

static constexpr const wchar_t* WIN_CLASS_NAME = L"Win32Control";

Win32Control::~Win32Control()
{
    DestroyWindow(m_hwnd);
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
        ERROR("RegisterClassEx error %d", error);
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
        ERROR("CreateWindowEx error %d", error);
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

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CLOSE:
        PostQuitMessage(0);
        break;

    case WM_DESTROY:
        return 0;

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
