#pragma once

#include <filesystem>

namespace PluginPaths
{
	const std::filesystem::path& GetPluginDirectory();
	const std::filesystem::path& GetDataDirectory();
	std::filesystem::path GetSettingsPath();
	std::filesystem::path GetThemeManifestPath();
	std::filesystem::path GetLogPath();
	std::filesystem::path GetTestSettingsPath();
}
