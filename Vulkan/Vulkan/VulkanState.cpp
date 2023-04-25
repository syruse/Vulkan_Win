#include "VulkanState.h"

#ifdef _WIN32
#include "Win32Control.h"
/// already included 'windows.h' with own implementations of aligned_alloc...
#elif __linux__
#include "XCBControl.h"
#include <cstring> // memcpy
#include <stdlib.h> // aligned_alloc/free
#define _aligned_free free
#define _aligned_malloc aligned_alloc
#else            
/// other OS
#endif 

VulkanState::VulkanState(std::wstring_view appName, uint16_t width, uint16_t height) :
	_width(width),
	_height(height),
#ifdef _WIN32
	_core(std::make_unique<Win32Control>(appName, width, height))
#elif __linux__
	_core(std::make_unique<XCBControl>(appName, width, height))
#else            
	/// other OS
#endif 
{}
