#ifdef _WIN32

#include "Win32Control.h"
#include "Utils.h"

#include <cassert>
#include <vulkan/vulkan_win32.h>
#include <imgui/backends/imgui_impl_win32.h>
#include <imgui/backends/imgui_impl_vulkan.h>
#include <imgui/imgui.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);

static constexpr const char* WIN_CLASS_NAME = "Win32Control";
static IControl::WindowQueueMSG _windowQueueMsg{};

Win32Control::~Win32Control() {
    DestroyWindow(m_hwnd);
}

std::string_view Win32Control::getVulkanWindowSurfaceExtension() const {
    return VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
}

void Win32Control::init() {
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

    SetProcessDPIAware();  // take dpi into account for window size

    m_hwnd = CreateWindowEx(0,
                            WIN_CLASS_NAME,  // class name
                            m_appName.data(), style, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, NULL,
                            NULL, m_hinstance, NULL);

    if (m_hwnd == 0) {
        DWORD error = GetLastError();
        Utils::printLog(ERROR_PARAM, "CreateWindowEx error %d", error);
    }

    mKeyboard = std::make_unique<DirectX::Keyboard>();
    mMouse = std::make_unique<DirectX::Mouse>();
    mMouse->SetWindow(m_hwnd);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;   // Enable Gamepad Controls
    io.WantCaptureMouse = true;
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(m_hwnd);

    ShowWindow(m_hwnd, SW_SHOW);
}

void Win32Control::imGuiNewFrame() const {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

VkSurfaceKHR Win32Control::createSurface(VkInstance& inst) const {
    VkWin32SurfaceCreateInfoKHR surfaceCreateInfo = {};
    surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surfaceCreateInfo.hinstance = m_hinstance;
    surfaceCreateInfo.hwnd = m_hwnd;

    VkSurfaceKHR surface;

    VkResult res = vkCreateWin32SurfaceKHR(inst, &surfaceCreateInfo, NULL, &surface);
    CHECK_VULKAN_ERROR("vkCreateWin32SurfaceKHR error %d\n", res);

    return surface;
}

IControl::WindowQueueMSG Win32Control::processWindowQueueMSGs() {
    MSG msg;

    /**
    GetMessage does not return until a message matching the filter criteria is placed in the queue, whereas
    PeekMessage returns immediately regardless of whether a message is in the queue.
    Remove any messages that may be in the queue of cpecified type like WM_QUIT
    */
    if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        /* handle or dispatch messages */
        if (msg.message == WM_QUIT) {
            _windowQueueMsg.isQuited = true;
        } else {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    static DirectX::Keyboard::KeyboardStateTracker tracker;
    auto kb = mKeyboard->GetState();
    tracker.Update(kb);

    static bool escPressed = false;

    _windowQueueMsg.buttonFlag = 0;
    if (tracker.pressed.Escape) {
        escPressed = !escPressed;
    }
    if (kb.W || kb.Up) {
        _windowQueueMsg.buttonFlag |= WindowQueueMSG::UP;
    }
    if (kb.A || kb.Left) {
        _windowQueueMsg.buttonFlag |= WindowQueueMSG::LEFT;
    }
    if (kb.S || kb.Down) {
        _windowQueueMsg.buttonFlag |= WindowQueueMSG::DONW;
    }
    if (kb.D || kb.Right) {
        _windowQueueMsg.buttonFlag |= WindowQueueMSG::RIGHT;
    }

    _windowQueueMsg.mouseX = mMouse->GetState().x;
    _windowQueueMsg.mouseY = mMouse->GetState().y;

    // if menu invoked
    _windowQueueMsg.hmiRenderData = nullptr; 
    if (escPressed) {
        imGuiNewFrame();
        _windowQueueMsg.hmiStates = &mUi.updateAndDraw();
        _windowQueueMsg.hmiRenderData = ImGui::GetDrawData();
    }

    IControl::WindowQueueMSG windowQueueMsg(_windowQueueMsg);
    _windowQueueMsg.reset();

    return windowQueueMsg;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE:
            break;
        case WM_SIZE: {
            _windowQueueMsg.width = LOWORD(lParam);
            _windowQueueMsg.height = HIWORD(lParam);
            _windowQueueMsg.isResized = true;
            break;
        }
        case WM_CLOSE:
            PostQuitMessage(0);
            break;
        case WM_ACTIVATEAPP: {
            DirectX::Keyboard::ProcessMessage(uMsg, wParam, lParam);
            DirectX::Mouse::ProcessMessage(uMsg, wParam, lParam);
            break;        
        }
        case WM_ACTIVATE:
        case WM_INPUT:
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
            ImGui_ImplWin32_WndProcHandler(hwnd, uMsg, wParam, lParam);
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MOUSEWHEEL:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
        case WM_MOUSEHOVER:
            DirectX::Mouse::ProcessMessage(uMsg, wParam, lParam);
            break;
        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP:
            DirectX::Keyboard::ProcessMessage(uMsg, wParam, lParam);
            break;
        case WM_MOUSEACTIVATE:
            // "click activating" the window to regain focus we don'g react on this, just focusing
            return MA_ACTIVATEANDEAT;
        default: {
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
        }
    }
    return 0;
}

#endif