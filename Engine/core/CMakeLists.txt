cmake_minimum_required(VERSION 3.21)
set(APP_NAME VulkanGame)
project(${APP_NAME} VERSION 1.0 LANGUAGES CXX)

option(USE_CUDA "Use CUDA" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -D_DEBUG")
set(CMAKE_SUPPRESS_REGENERATION true)

#You can pick the build type in VS
#if( NOT CMAKE_BUILD_TYPE )
#   set( CMAKE_BUILD_TYPE Release )
#endif()

if(UNIX AND NOT APPLE)
    set(LINUX TRUE)
    find_package(Vulkan REQUIRED)
    find_package(SDL2 REQUIRED)
endif()

if(WIN32)
    add_subdirectory(${CMAKE_SOURCE_DIR}/../DirectXTK12 ${CMAKE_BINARY_DIR}/DirectXTK12/build)
    set(CMAKE_CXX_COMPILER "msvc")
    link_directories($ENV{VULKAN_SDK}/lib)
	if(USE_CUDA)
		find_package(CUDA 8.0)
		if(NOT CUDA_FOUND)
            message(STATUS "CUDA not found")
			set(USE_CUDA OFF)
        else()
		    enable_language("CUDA")
		    find_package(CUDAToolkit)
	        set(CMAKE_CUDA_STANDARD 17)
			set(CMAKE_CUDA_STANDARD_REQUIRED ON)
			file(GLOB_RECURSE cuda_include_list LIST_DIRECTORIES true "${CMAKE_SOURCE_DIR}/include")
		    file(GLOB_RECURSE CUDA_HEADERS "${CMAKE_SOURCE_DIR}/include/*.cuh")
		    file(GLOB_RECURSE CUDA_SOURCES "${CMAKE_SOURCE_DIR}/src/*.cu")
            add_library(md5_anim_cuda STATIC ${CUDA_SOURCES} ${CUDA_HEADERS})
			set_target_properties(md5_anim_cuda PROPERTIES
				CUDA_FLAGS "-O3"
			)
			target_link_libraries(md5_anim_cuda CUDA::cudart_static)
			set_target_properties(md5_anim_cuda PROPERTIES CUDA_SEPARABLE_COMPILATION ON)
			set_target_properties(md5_anim_cuda PROPERTIES CUDA_RESOLVE_DEVICE_SYMBOLS ON)
			target_include_directories(md5_anim_cuda PRIVATE "${CMAKE_SOURCE_DIR}/../" ${cuda_include_list})
        endif()
	endif()
endif()


set(IMGUI_HEADER "${CMAKE_SOURCE_DIR}/../imgui")
set(IMGUI_SRC
"${CMAKE_CURRENT_SOURCE_DIR}/../imgui/imgui.cpp"
"${CMAKE_CURRENT_SOURCE_DIR}/../imgui/imgui_tables.cpp"
"${CMAKE_CURRENT_SOURCE_DIR}/../imgui/imgui_draw.cpp"
"${CMAKE_CURRENT_SOURCE_DIR}/../imgui/imgui_widgets.cpp"
"${CMAKE_CURRENT_SOURCE_DIR}/../imgui/backends/imgui_impl_vulkan.cpp"
"${CMAKE_CURRENT_SOURCE_DIR}/../imgui/backends/imgui_impl_win32.cpp"
)
add_library(imgui STATIC ${IMGUI_SRC})

file(GLOB_RECURSE HEADERS "${CMAKE_SOURCE_DIR}/include/*.h")
file(GLOB_RECURSE SOURCES "${CMAKE_SOURCE_DIR}/src/*.cpp")

file(GLOB_RECURSE include_list LIST_DIRECTORIES true "${CMAKE_SOURCE_DIR}/include")
#THIRD_PARTY_INCLUDE_DIR contains: glm; stb_image; tiny_obj_loader; vulkan; directInput/sdl
if(LINUX)
    set(THIRD_PARTY_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../" "${VULKAN_INCLUDE_DIR}/include" "${SDL_INCLUDE_DIR}/include")
    target_include_directories(imgui PRIVATE ${IMGUI_HEADER} "${VULKAN_INCLUDE_DIR}/include")
else(WIN32)
    set(THIRD_PARTY_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../" "$ENV{VULKAN_SDK}/include" "${CMAKE_SOURCE_DIR}/../DirectXTK12/Inc")
    target_include_directories(imgui PRIVATE ${IMGUI_HEADER} "$ENV{VULKAN_SDK}/include")
endif()

add_executable(${APP_NAME} ${HEADERS} ${SOURCES})
target_include_directories(${APP_NAME} PRIVATE ${include_list} ${THIRD_PARTY_INCLUDE_DIR} ${IMGUI_HEADER})

if(CMAKE_BUILD_TYPE MATCHES Release)
   message("${CMAKE_CXX_FLAGS_RELEASE}")
else()
   message("${CMAKE_CXX_FLAGS_DEBUG}")
   if(WIN32)
	  SET_TARGET_PROPERTIES(${APP_NAME} PROPERTIES LINK_FLAGS "/PROFILE")
	  if(USE_CUDA)
		SET_TARGET_PROPERTIES(md5_anim_cuda PROPERTIES LINK_FLAGS "/PROFILE")
		target_compile_options(md5_anim_cuda PRIVATE "-G") # Enable debug information
		endif()
   endif()
endif()

message("copying textures, shaders, models from ${CMAKE_CURRENT_SOURCE_DIR}/ to ${CMAKE_BINARY_DIR}/")
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/textures DESTINATION ${CMAKE_BINARY_DIR}/)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/models DESTINATION ${CMAKE_BINARY_DIR}/)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/shaders DESTINATION ${CMAKE_BINARY_DIR}/)

if(LINUX)
    target_link_libraries(${APP_NAME} "libvulkan.so.1" "libxcb.so" "libSDL2.so")
else(WIN32)
    target_link_libraries(${APP_NAME} "vulkan-1.lib" DirectXTK12)
	if(USE_CUDA)
		target_link_libraries(${APP_NAME} md5_anim_cuda)
		target_compile_definitions(${APP_NAME} PRIVATE USE_CUDA=1)
	endif()
endif()
target_link_libraries(${APP_NAME} imgui)

install(TARGETS ${APP_NAME} DESTINATION ${CMAKE_BINARY_DIR}/Bin)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/textures DESTINATION ${CMAKE_BINARY_DIR}/Bin/)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/models DESTINATION ${CMAKE_BINARY_DIR}/Bin/)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shaders DESTINATION ${CMAKE_BINARY_DIR}/Bin/)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/pipeline_data.cache DESTINATION ${CMAKE_BINARY_DIR}/Bin/)
