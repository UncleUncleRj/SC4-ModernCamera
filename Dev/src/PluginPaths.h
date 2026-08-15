#pragma once

#include <filesystem>

namespace PluginPaths
{
	// Returns the directory containing this plugin's DLL.
	const std::filesystem::path& GetPluginDirectory();
	// Returns the directory used for plugin settings and diagnostic files.
	const std::filesystem::path& GetDataDirectory();
	// Returns the settings-file path in the plugin data directory.
	std::filesystem::path GetSettingsPath();
	// Returns the diagnostic log file path.
	std::filesystem::path GetLogPath();
	// Returns the test UI settings-file path.
	std::filesystem::path GetTestSettingsPath();
}
