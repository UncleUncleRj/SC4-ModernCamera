#include "PluginSettings.h"

#include "Logger.h"
#include "PluginVersion.h"

#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>
#include <system_error>

namespace
{
	// Reads an entire text file and returns an empty string when it cannot be opened.
	std::string ReadTextFile(const std::filesystem::path& path)
	{
		std::ifstream stream(path, std::ios::in | std::ios::binary);
		if (!stream)
		{
			return {};
		}

		std::ostringstream text;
		text << stream.rdbuf();
		return text.str();
	}

	// Extracts one quoted string property from the settings JSON document.
	bool TryReadString(const std::string& json, const char* key, std::string& value)
	{
		const std::regex expression(std::string("\\\"") + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
		std::smatch match;
		if (!std::regex_search(json, match, expression))
		{
			return false;
		}

		value = match[1].str();
		return true;
	}

	// Extracts one Boolean property from the settings JSON document.
	bool TryReadBool(const std::string& json, const char* key, bool& value)
	{
		const std::regex expression(std::string("\\\"") + key + "\\\"\\s*:\\s*(true|false)");
		std::smatch match;
		if (!std::regex_search(json, match, expression))
		{
			return false;
		}

		value = match[1].str() == "true";
		return true;
	}

	// Extracts one finite decimal property from the settings JSON document.
	bool TryReadFloat(const std::string& json, const char* key, float& value)
	{
		const std::regex expression(std::string("\\\"") + key + "\\\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?)");
		std::smatch match;
		if (!std::regex_search(json, match, expression))
		{
			return false;
		}

		try
		{
			value = std::stof(match[1].str());
			return true;
		}
		catch (...)
		{
			return false;
		}
	}
}

// Converts a camera mode to its persisted representation.
const char* PluginSettingText::ToString(CameraMode value)
{
	return value == CameraMode::Classic ? "Classic" : "Modern";
}

// Converts a redraw-aggression level to its persisted representation.
const char* PluginSettingText::ToString(RedrawAggression value)
{
	switch (value)
	{
	case RedrawAggression::Classic: return "Classic";
	case RedrawAggression::High: return "High";
	case RedrawAggression::Extreme: return "Extreme";
	default: return "Normal";
	}
}

// Converts a diagnostics logging level to its persisted representation.
const char* PluginSettingText::ToString(DebugLogging value)
{
	switch (value)
	{
	case DebugLogging::Off: return "Off";
	case DebugLogging::Verbose: return "Verbose";
	default: return "Normal";
	}
}

// Initializes settings with the same values used for a fresh install.
PluginSettings::PluginSettings()
{
	RestoreDefaults();
}

// Restores fresh-install values without writing a settings file.
void PluginSettings::RestoreDefaults()
{
	cameraMode = CameraMode::Modern;
	wasdMovement = true;
	qeRotation = true;
	rotationSensitivity = kDefaultSensitivity;
	zoomSensitivity = kDefaultSensitivity;
	panSensitivity = kDefaultSensitivity;
	invertVertical = false;
	redrawAggression = RedrawAggression::Normal;
	debugLogging = DebugLogging::Normal;
	cameraDumpHotkey = true;
	showChangelogsOnUpdate = true;
}

// Loads known fields while retaining defaults for missing or invalid values.
bool PluginSettings::Load(const std::filesystem::path& settingsPath)
{
	path = settingsPath;
	RestoreDefaults();
	installedVersion.clear();

	if (!std::filesystem::exists(path))
	{
		Logger::GetInstance().WriteLine(LogLevel::Info, "Settings file not found; using defaults.");
		return Save();
	}

	const std::string json = ReadTextFile(path);
	if (json.empty())
	{
		Logger::GetInstance().WriteLine(LogLevel::Warning, "Settings file is empty or unreadable; using defaults.");
		return false;
	}

	TryReadString(json, "installedVersion", installedVersion);

	std::string value;
	if (TryReadString(json, "mode", value))
	{
		cameraMode = value == "Classic" ? CameraMode::Classic : CameraMode::Modern;
	}

	TryReadBool(json, "wasdMovement", wasdMovement);
	TryReadBool(json, "qeRotation", qeRotation);
	TryReadFloat(json, "rotationSensitivity", rotationSensitivity);
	TryReadFloat(json, "zoomSensitivity", zoomSensitivity);
	TryReadFloat(json, "panSensitivity", panSensitivity);
	TryReadBool(json, "invertVertical", invertVertical);

	if (TryReadString(json, "redrawAggression", value))
	{
		if (value == "Classic" || value == "Off" || value == "None") redrawAggression = RedrawAggression::Classic;
		else if (value == "High") redrawAggression = RedrawAggression::High;
		else if (value == "Extreme") redrawAggression = RedrawAggression::Extreme;
		else if (value == "Aggressive") redrawAggression = RedrawAggression::High;
		else redrawAggression = RedrawAggression::Normal;
	}

	if (TryReadString(json, "logging", value))
	{
		if (value == "Off") debugLogging = DebugLogging::Off;
		else if (value == "Verbose") debugLogging = DebugLogging::Verbose;
		else debugLogging = DebugLogging::Normal;
	}
	TryReadBool(json, "cameraDumpHotkey", cameraDumpHotkey);
	TryReadBool(json, "showChangelogsOnUpdate", showChangelogsOnUpdate);

	rotationSensitivity = std::clamp(rotationSensitivity, kMinimumSensitivity, kMaximumSensitivity);
	zoomSensitivity = std::clamp(zoomSensitivity, kMinimumSensitivity, kMaximumSensitivity);
	panSensitivity = std::clamp(panSensitivity, kMinimumPanSensitivity, kMaximumPanSensitivity);

	Logger::GetInstance().WriteLine(LogLevel::Info, "Loaded settings from " + path.string());
	return true;
}

// Serializes the current settings to the path retained by Load.
bool PluginSettings::Save() const
{
	if (path.empty())
	{
		return false;
	}

	const std::filesystem::path temporaryPath = path.wstring() + L".tmp";
	std::ofstream stream(temporaryPath, std::ios::out | std::ios::binary | std::ios::trunc);
	if (!stream)
	{
		Logger::GetInstance().WriteLine(LogLevel::Error, "Failed to open the temporary settings file for writing.");
		return false;
	}

	stream
		<< "{\n"
		<< "  \"installedVersion\": \"" << installedVersion << "\",\n"
		<< "  \"camera\": {\n"
		<< "    \"mode\": \"" << PluginSettingText::ToString(cameraMode) << "\",\n"
		<< "    \"wasdMovement\": " << (wasdMovement ? "true" : "false") << ",\n"
		<< "    \"qeRotation\": " << (qeRotation ? "true" : "false") << ",\n"
		<< "    \"rotationSensitivity\": " << rotationSensitivity << ",\n"
		<< "    \"zoomSensitivity\": " << zoomSensitivity << ",\n"
		<< "    \"panSensitivity\": " << panSensitivity << ",\n"
		<< "    \"invertVertical\": " << (invertVertical ? "true" : "false") << "\n"
		<< "  },\n"
		<< "  \"rendering\": {\n"
		<< "    \"redrawAggression\": \"" << PluginSettingText::ToString(redrawAggression) << "\"\n"
		<< "  },\n"
		<< "  \"diagnostics\": {\n"
		<< "    \"logging\": \"" << PluginSettingText::ToString(debugLogging) << "\",\n"
		<< "    \"cameraDumpHotkey\": " << (cameraDumpHotkey ? "true" : "false") << "\n"
		<< "  },\n"
		<< "  \"ui\": {\n"
		<< "    \"showChangelogsOnUpdate\": " << (showChangelogsOnUpdate ? "true" : "false") << "\n"
		<< "  }\n"
		<< "}\n";

	stream.close();
	if (!stream)
	{
		Logger::GetInstance().WriteLine(LogLevel::Error, "Failed while writing the settings file.");
		return false;
	}

	std::error_code error;
	std::filesystem::rename(temporaryPath, path, error);
	if (error)
	{
		std::filesystem::remove(path, error);
		error.clear();
		std::filesystem::rename(temporaryPath, path, error);
	}

	if (error)
	{
		Logger::GetInstance().WriteLine(LogLevel::Error, "Failed to replace the settings file: " + error.message());
		return false;
	}

	return true;
}

// Returns whether the stored version differs from the current plugin version.
bool PluginSettings::NeedsVersionNotice() const
{
	return installedVersion != PluginVersion::String;
}

// Records the current plugin version and saves the updated settings document.
bool PluginSettings::AcknowledgeCurrentVersion()
{
	installedVersion = PluginVersion::String;
	return Save();
}

// Returns the path retained for future Save calls.
const std::filesystem::path& PluginSettings::GetPath() const
{
	return path;
}

// Returns the version marker retained from the loaded settings document.
const std::string& PluginSettings::GetInstalledVersion() const
{
	return installedVersion;
}
