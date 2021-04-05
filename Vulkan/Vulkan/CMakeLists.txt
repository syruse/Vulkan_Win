cmake_minimum_required(VERSION 3.0)

project(VulkanApp)

if(UNIX AND NOT APPLE)
    set(LINUX TRUE)
endif()

SET(MYINCLUDE_DIR "../")

INCLUDE_DIRECTORIES( ${MYINCLUDE_DIR} )


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -D_DEBUG")

file(GLOB VulkanApp_SRC
     "*.h"
     "*.cpp"
)

find_package(Vulkan REQUIRED)
#/usr/lib/x86_64-linux-gnu/libvulkan.so.1
#/usr/lib/x86_64-linux-gnu/libxcb.so

add_executable(VulkanApp ${VulkanApp_SRC})

if(LINUX)
    target_link_libraries(VulkanApp "libvulkan.so.1" "libxcb.so")
else(WIN32)
    message("set specific libs for linking!!!")
endif()