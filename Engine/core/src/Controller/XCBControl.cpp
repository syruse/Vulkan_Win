#ifdef __linux__

#include "XCBControl.h"

#include <SDL2/SDL_vulkan.h>

#include <cassert>
#include <cstring>

#include <imgui/backends/imgui_impl_sdl2.h>
#include <imgui/backends/imgui_impl_vulkan.h>
#include <imgui/imgui.h>

#include "Utils.h"

XCBControl::~XCBControl() {
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    if (m_window) {
        SDL_DestroyWindow(m_window);
    }

    SDL_Quit();
}

std::string_view XCBControl::getVulkanWindowSurfaceExtension() const {
    const char* videoDriver = SDL_GetCurrentVideoDriver();
    if (!videoDriver) {
        return "";
    }

    if (std::strcmp(videoDriver, "x11") == 0) {
        return "VK_KHR_xlib_surface";
    }
    if (std::strcmp(videoDriver, "wayland") == 0) {
        return "VK_KHR_wayland_surface";
    }

    Utils::printLog(ERROR_PARAM, "Unsupported SDL video driver for Vulkan surface extension ", videoDriver);
    return "";
}

void XCBControl::init() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        Utils::printLog(ERROR_PARAM, "SDL_Init error ", SDL_GetError());
        return;
    }

    m_window = SDL_CreateWindow(m_appName.data(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, m_width, m_height,
                                SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);
    if (!m_window) {
        Utils::printLog(ERROR_PARAM, "SDL_CreateWindow error ", SDL_GetError());
        return;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.WantCaptureMouse = true;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForVulkan(m_window);

    Utils::printLog(INFO_PARAM, "SDL window created");
}

void XCBControl::imGuiNewFrame(VkCommandBuffer command_buffer) {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    m_windowQueueMsg.hmiStates = &mUi.updateAndDraw();
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command_buffer);
}

VkSurfaceKHR XCBControl::createSurface(VkInstance& inst) const {
    VkSurfaceKHR surface = VK_NULL_HANDLE;

    if (SDL_Vulkan_CreateSurface(m_window, inst, &surface) != SDL_TRUE) {
        Utils::printLog(ERROR_PARAM, "SDL_Vulkan_CreateSurface error ", SDL_GetError());
        return VK_NULL_HANDLE;
    }

    return surface;
}

IControl::WindowQueueMSG XCBControl::processWindowQueueMSGs() {
    m_windowQueueMsg.reset();
    m_windowQueueMsg.hmiRenderData = m_isUiVisible;

    SDL_Event event;
    while (SDL_PollEvent(&event) != 0) {
        ImGui_ImplSDL2_ProcessEvent(&event);

        switch (event.type) {
            case SDL_QUIT:
                m_windowQueueMsg.isQuited = true;
                break;
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    m_windowQueueMsg.isResized = true;
                    m_windowQueueMsg.width = static_cast<uint32_t>(event.window.data1);
                    m_windowQueueMsg.height = static_cast<uint32_t>(event.window.data2);
                }
                break;
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_ESCAPE && event.key.repeat == 0) {
                    m_isUiVisible = !m_isUiVisible;
                    m_windowQueueMsg.hmiRenderData = m_isUiVisible;
                }
                break;
            default:
                break;
        }
    }

    const uint8_t* keyboardState = SDL_GetKeyboardState(nullptr);
    m_windowQueueMsg.buttonFlag = 0;
    if (keyboardState[SDL_SCANCODE_W] || keyboardState[SDL_SCANCODE_UP]) {
        m_windowQueueMsg.buttonFlag |= WindowQueueMSG::UP;
    }
    if (keyboardState[SDL_SCANCODE_A] || keyboardState[SDL_SCANCODE_LEFT]) {
        m_windowQueueMsg.buttonFlag |= WindowQueueMSG::LEFT;
    }
    if (keyboardState[SDL_SCANCODE_S] || keyboardState[SDL_SCANCODE_DOWN]) {
        m_windowQueueMsg.buttonFlag |= WindowQueueMSG::DONW;
    }
    if (keyboardState[SDL_SCANCODE_D] || keyboardState[SDL_SCANCODE_RIGHT]) {
        m_windowQueueMsg.buttonFlag |= WindowQueueMSG::RIGHT;
    }

    int mouseX = 0;
    int mouseY = 0;
    SDL_GetMouseState(&mouseX, &mouseY);
    m_windowQueueMsg.mouseX = static_cast<uint32_t>(mouseX);
    m_windowQueueMsg.mouseY = static_cast<uint32_t>(mouseY);

    return m_windowQueueMsg;
}

#endif