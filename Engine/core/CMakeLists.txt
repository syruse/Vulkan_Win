cmake_minimum_required(VERSION 3.21)
set(APP_NAME VulkanGame)
project(${APP_NAME} VERSION 1.0 LANGUAGES CXX)

option(USE_PROFILING "compiled for profiling" ON)
option(USE_CUDA "Use CUDA" ON)
option(USE_FSR "Use FSR" OFF)
option(USE_DLSS "Use DLSS" ON)

set(CMAKE_CXX_STANDARD 20)
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

#-----------------------------BulletPhysics-------------------------------
#Force Bullet to build as static libraries
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries" FORCE)
set(BULLET2_BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries" FORCE)
#Disable unnecessary Bullet extras to speed up build
set(BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_BULLET2_DEMOS OFF CACHE BOOL "" FORCE)
set(BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
#Disable the deprecation warning for old CMake versions
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/../BulletPhysics ${CMAKE_BINARY_DIR}/BulletPhysics/build)
#turn it back on for your own code
set(CMAKE_WARN_DEPRECATED ON CACHE BOOL "" FORCE)
set(BULLET_PHYSICS_HEADER "${CMAKE_SOURCE_DIR}/../BulletPhysics/src")

if(WIN32)
    add_subdirectory(${CMAKE_SOURCE_DIR}/../DirectXTK12 ${CMAKE_BINARY_DIR}/DirectXTK12/build)
    set(CMAKE_CXX_COMPILER "msvc")
    link_directories($ENV{VULKAN_SDK}/lib)
	if(USE_CUDA)
	    find_package(CUDAToolkit 8.0 QUIET)
		if(NOT CUDAToolkit_FOUND)
            message(STATUS "CUDA not found")
			set(USE_CUDA OFF)
        else()
		    message(STATUS "CUDA found: ${CUDAToolkit_VERSION}")
		    enable_language("CUDA")
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
target_compile_definitions(imgui PRIVATE IMGUI_IMPL_VULKAN_NO_PROTOTYPES)
if(WIN32)
	target_compile_definitions(imgui PRIVATE VK_USE_PLATFORM_WIN32_KHR)
endif()

#Volk
set(VOLK_HEADER "${CMAKE_SOURCE_DIR}/../Volk")
set(VOLK_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../Volk/volk.c"
)

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

add_executable(${APP_NAME} ${HEADERS} ${SOURCES} ${VOLK_SRC})
target_include_directories(${APP_NAME} PRIVATE ${include_list} ${THIRD_PARTY_INCLUDE_DIR} ${IMGUI_HEADER} ${VOLK_HEADER} ${BULLET_PHYSICS_HEADER})
#enable debug mode for Vulkan by setting VK_DEBUG_ENABLED(deprecated)
#target_compile_definitions(${APP_NAME} PRIVATE VK_DEBUG_ENABLED=1)

if(CMAKE_BUILD_TYPE MATCHES Release)
   message("${CMAKE_CXX_FLAGS_RELEASE}")
else()
   message("${CMAKE_CXX_FLAGS_DEBUG}")
   if(WIN32 AND USE_PROFILING)
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
    target_link_libraries(${APP_NAME} "libxcb.so" "libSDL2.so") # no need "libvulkan.so.1" anymore
	#Volk resolves external symbols like vkCreateWin32SurfaceKHR since we don't use "libvulkan.so.1"
	target_compile_definitions(${APP_NAME} PRIVATE VK_USE_PLATFORM_WAYLAND_KHR)
else(WIN32)
    #Volk resolves external symbols like vkCreateWin32SurfaceKHR since we don't use "vulkan-1.lib"
    target_compile_definitions(${APP_NAME} PRIVATE VK_USE_PLATFORM_WIN32_KHR)
	if(USE_FSR)
		set(FSR3_HEADER "${CMAKE_SOURCE_DIR}/../FidelityFX-SDK-v1.1.4/ffx-ap/include" "${CMAKE_SOURCE_DIR}/../FidelityFX-SDK-v1.1.4/sdk/include")
		target_include_directories(${APP_NAME} PRIVATE ${FSR3_HEADER})
		target_compile_definitions(${APP_NAME} PRIVATE USE_FSR=1)
		target_link_libraries(${APP_NAME} "${CMAKE_SOURCE_DIR}/../FidelityFX-SDK-v1.1.4/ffx-ap/bin/amd_fidelityfx_vk.lib")
	
		add_custom_command(
			TARGET ${APP_NAME} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"${CMAKE_SOURCE_DIR}/../FidelityFX-SDK-v1.1.4/ffx-ap/bin/amd_fidelityfx_vk.dll"
				"${CMAKE_BINARY_DIR}/Bin/amd_fidelityfx_vk.dll"
			COMMENT "Copying FSR3 DLL: amd_fidelityfx_vk.dll"
		)
	endif()
	
	if(USE_DLSS)
		#NVIDIA DLSS
		set(STREAMLINE_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../streamline-sdk-v2.11.1")
		set(STREAMLINE_DLLS "sl.interposer.dll" "sl.common.dll" "sl.dlss.dll" "nvngx_dlss.dll")
		set(STREAMLINE_HEADER "${STREAMLINE_SDK_DIR}/include")
		set(STREAMLINE_CONFIG "${STREAMLINE_SDK_DIR}/sl.interposer.json")
		target_include_directories(${APP_NAME} PRIVATE ${STREAMLINE_HEADER})
		target_compile_definitions(${APP_NAME} PRIVATE USE_DLSS=1)

		#at the end of building we copy resources if they are changed
		foreach(dll IN LISTS STREAMLINE_DLLS)
			# Debug
			add_custom_command(
				TARGET ${APP_NAME} POST_BUILD
				COMMAND $<$<CONFIG:Debug>:${CMAKE_COMMAND}> -E copy_if_different
					"${STREAMLINE_SDK_DIR}/bin/x64/development/${dll}"
					"${CMAKE_BINARY_DIR}/Bin/${dll}"
				COMMENT "Copying Streamline Development DLL: ${dll}"
			)
		
			# non Debug
			add_custom_command(
				TARGET ${APP_NAME} POST_BUILD
				COMMAND $<$<NOT:$<CONFIG:Debug>>:${CMAKE_COMMAND}> -E copy_if_different
					"${STREAMLINE_SDK_DIR}/bin/x64/${dll}"
					"${CMAKE_BINARY_DIR}/Bin/${dll}"
				COMMENT "Copying Streamline Production DLL: ${dll}"
			)
		endforeach()
	endif()
	
    target_link_libraries(${APP_NAME} DirectXTK12) # no need "vulkan-1.lib" anymore (now volk is used)
	if(USE_CUDA)
		target_link_libraries(${APP_NAME} md5_anim_cuda)
		target_compile_definitions(${APP_NAME} PRIVATE USE_CUDA=1)
	endif()
	
	#disable strict C++ conformance checking
	#GLM templates conflict with the new template parsing rules in MSVC
	target_compile_options(${APP_NAME} PRIVATE /permissive)
	target_compile_definitions(${APP_NAME} PRIVATE 
        NOMINMAX 
    )
endif()

#GLM CONFIG FOR MAIN MODULE
target_compile_definitions(${APP_NAME} PRIVATE 
    GLM_ENABLE_EXPERIMENTAL
	GLM_FORCE_RADIANS
	GLM_FORCE_DEPTH_ZERO_TO_ONE  # coerce the perspective projection matrix to be in depth: [0.0 to 1.0]
)

target_link_libraries(${APP_NAME} 
    imgui
    BulletDynamics 
    BulletCollision 
    LinearMath
)

#our bin is built inside Bin dir regardless of build type
set_target_properties(${APP_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Bin"
	RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/Bin"
)

add_custom_command(
    TARGET ${APP_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/textures" "${CMAKE_BINARY_DIR}/Bin/textures"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/models" "${CMAKE_BINARY_DIR}/Bin/models"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/shaders" "${CMAKE_BINARY_DIR}/Bin/shaders"
    COMMENT "Copying assets to target directory"
)
