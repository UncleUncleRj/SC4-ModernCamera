#pragma once

#include <filesystem>

namespace PluginPaths
{
	const std::filesystem::path& GetPluginDirectory();
	std::filesystem::path GetSettingsPath();
	std::filesystem::path GetLogPath();
}
