#include "VulkanState.h"

VulkanState::VulkanState(std::string_view appName, uint16_t width, uint16_t height)
    : _width(width),
      _height(height),
#ifdef _WIN32
      _core(std::make_unique<Win32Control>(appName, width, height))
#elif __linux__
      _core(std::make_unique<XCBControl>(appName, width, height))
#else
/// other OS
#endif
{
}
