cmake_minimum_required(VERSION 3.21)
set(APP_NAME VulkanGame)
project(${APP_NAME} VERSION 1.0 LANGUAGES CXX)

if(UNIX AND NOT APPLE)
    set(LINUX TRUE)
    find_package(Vulkan REQUIRED)
    find_package(SDL REQUIRED)
endif()

if(WIN32)
    set(CMAKE_CXX_COMPILER "msvc")
    link_directories($ENV{SDL}/lib)
    link_directories($ENV{VULKAN_SDK}/lib)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -D_DEBUG")
set(CMAKE_SUPPRESS_REGENERATION true)

if( NOT CMAKE_BUILD_TYPE )
   set( CMAKE_BUILD_TYPE Release )
endif()

if(CMAKE_BUILD_TYPE MATCHES Release)
   message("${CMAKE_CXX_FLAGS_RELEASE}")
else()
   message("${CMAKE_CXX_FLAGS_DEBUG}")
endif()

file(GLOB_RECURSE HEADERS "${CMAKE_SOURCE_DIR}/include/*.h")
file(GLOB_RECURSE SOURCES "${CMAKE_SOURCE_DIR}/src/*.cpp")

file(GLOB_RECURSE include_list LIST_DIRECTORIES true "${CMAKE_SOURCE_DIR}/include")
#THIRD_PARTY_INCLUDE_DIR contains: glm; stb_image; tiny_obj_loader; vulkan; sdl
if(LINUX)
    set(THIRD_PARTY_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../" "${VULKAN_INCLUDE_DIR}/include" "${SDL_INCLUDE_DIR}/include")
else(WIN32)
    set(THIRD_PARTY_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../" "$ENV{VULKAN_SDK}/include" "$ENV{SDL}/include")
endif()
include_directories(${include_list} ${THIRD_PARTY_INCLUDE_DIR})

add_executable(${APP_NAME} ${HEADERS} ${SOURCES})

message("copying textures, shaders, models from ${CMAKE_CURRENT_SOURCE_DIR}/ to ${CMAKE_BINARY_DIR}/")
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/textures DESTINATION ${CMAKE_BINARY_DIR}/)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/models DESTINATION ${CMAKE_BINARY_DIR}/)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/shaders DESTINATION ${CMAKE_BINARY_DIR}/)
file(COPY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/pipeline_data.cache ${CMAKE_BINARY_DIR}/pipeline_data.cache ONLY_IF_DIFFERENT)

if(LINUX)
    target_link_libraries(${APP_NAME} "libvulkan.so.1" "libxcb.so")
else(WIN32)
    target_link_libraries(${APP_NAME} "vulkan-1.lib" "SDL2.lib" "SDL2main.lib")
endif()

install(TARGETS ${APP_NAME} DESTINATION ${CMAKE_BINARY_DIR}/Bin)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/textures DESTINATION ${CMAKE_BINARY_DIR}/Bin/)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/models DESTINATION ${CMAKE_BINARY_DIR}/Bin/)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shaders DESTINATION ${CMAKE_BINARY_DIR}/Bin/)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/pipeline_data.cache DESTINATION ${CMAKE_BINARY_DIR}/Bin/)