#pragma once

#include <filesystem>

namespace PluginPaths
{
	// Returns the directory that contains this plugin DLL.
	const std::filesystem::path& GetPluginDirectory();
	// Returns the writable directory used for settings, logs, and theme data.
	const std::filesystem::path& GetDataDirectory();
	// Returns the persistent settings-file path.
	std::filesystem::path GetSettingsPath();
	// Returns the optional UI theme manifest path.
	std::filesystem::path GetThemeManifestPath();
	// Returns the active diagnostic-log path.
	std::filesystem::path GetLogPath();
	// Returns the control-laboratory diagnostic-state path.
	std::filesystem::path GetTestSettingsPath();
}
