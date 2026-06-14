#include "VulkanState.h"

VulkanState::VulkanState(std::string_view appName, uint16_t windowWidth, uint16_t windowHeight, uint16_t offscreenWidth, uint16_t offscreenHeight)
    : _windowWidth(windowWidth),
      _windowHeight(windowHeight),
      _offscreenWidth(offscreenWidth == 0 ? windowWidth : offscreenWidth),
      _offscreenHeight(offscreenHeight == 0 ? windowHeight : offscreenHeight),
#ifdef _WIN32
      _core(std::make_unique<Win32Control>(appName, _windowWidth, _windowHeight))
#elif __linux__
      _core(std::make_unique<XCBControl>(appName, _windowWidth, _windowHeight))
#else
/// other OS
#endif
{
}
