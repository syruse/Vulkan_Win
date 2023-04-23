#pragma once

#include <string_view>

namespace Constants {
#ifdef _WIN32
	static constexpr char DIR_SEPARATOR = '\\';
#else
	static constexpr char DIR_SEPARATOR = '/';
#endif
	static constexpr std::string_view TEXTURES_DIR{ "textures" };
	static constexpr std::string_view SHADERS_DIR{ "shaders" };
	static constexpr std::string_view MODEL_DIR = "models";
	static constexpr std::string_view PIPELINE_CACHE_FILE{ "pipeline_data.cache" };
}