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

if( NOT CMAKE_BUILD_TYPE )
   set( CMAKE_BUILD_TYPE Release )
endif()

if(CMAKE_BUILD_TYPE MATCHES Release)
   message("${CMAKE_CXX_FLAGS_RELEASE}")
else()
   message("${CMAKE_CXX_FLAGS_DEBUG}")
endif()

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

message("copying textures, shaders, models from ${CMAKE_CURRENT_SOURCE_DIR}/ to ${CMAKE_BINARY_DIR}/")
execute_process(COMMAND cp -r ${CMAKE_CURRENT_SOURCE_DIR}/textures ${CMAKE_BINARY_DIR}/)
execute_process(COMMAND cp -r ${CMAKE_CURRENT_SOURCE_DIR}/models ${CMAKE_BINARY_DIR}/)
execute_process(COMMAND cp -r ${CMAKE_CURRENT_SOURCE_DIR}/shaders ${CMAKE_BINARY_DIR}/)