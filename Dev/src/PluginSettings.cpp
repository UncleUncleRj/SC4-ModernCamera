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

	const char* ToString(CameraMode value)
	{
		return value == CameraMode::Classic ? "Classic" : "Modern";
	}

	const char* ToString(RedrawAggression value)
	{
		switch (value)
		{
		case RedrawAggression::Off: return "Off";
		case RedrawAggression::High: return "High";
		case RedrawAggression::Aggressive: return "Aggressive";
		default: return "Normal";
		}
	}

	const char* ToString(DebugLogging value)
	{
		switch (value)
		{
		case DebugLogging::Off: return "Off";
		case DebugLogging::Verbose: return "Verbose";
		default: return "Normal";
		}
	}
}

PluginSettings::PluginSettings()
{
	RestoreDefaults();
}

void PluginSettings::RestoreDefaults()
{
	cameraMode = CameraMode::Modern;
	wasdMovement = true;
	rotationSensitivity = 1.0f;
	zoomSensitivity = 1.0f;
	invertVertical = false;
	redrawAggression = RedrawAggression::Normal;
	debugLogging = DebugLogging::Normal;
}

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
	TryReadFloat(json, "rotationSensitivity", rotationSensitivity);
	TryReadFloat(json, "zoomSensitivity", zoomSensitivity);
	TryReadBool(json, "invertVertical", invertVertical);

	if (TryReadString(json, "redrawAggression", value))
	{
		if (value == "Off") redrawAggression = RedrawAggression::Off;
		else if (value == "High") redrawAggression = RedrawAggression::High;
		else if (value == "Aggressive") redrawAggression = RedrawAggression::Aggressive;
		else redrawAggression = RedrawAggression::Normal;
	}

	if (TryReadString(json, "logging", value))
	{
		if (value == "Off") debugLogging = DebugLogging::Off;
		else if (value == "Verbose") debugLogging = DebugLogging::Verbose;
		else debugLogging = DebugLogging::Normal;
	}

	rotationSensitivity = std::clamp(rotationSensitivity, 0.1f, 3.0f);
	zoomSensitivity = std::clamp(zoomSensitivity, 0.1f, 3.0f);

	Logger::GetInstance().WriteLine(LogLevel::Info, "Loaded settings from " + path.string());
	return true;
}

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
		<< "    \"mode\": \"" << ToString(cameraMode) << "\",\n"
		<< "    \"wasdMovement\": " << (wasdMovement ? "true" : "false") << ",\n"
		<< "    \"rotationSensitivity\": " << rotationSensitivity << ",\n"
		<< "    \"zoomSensitivity\": " << zoomSensitivity << ",\n"
		<< "    \"invertVertical\": " << (invertVertical ? "true" : "false") << "\n"
		<< "  },\n"
		<< "  \"rendering\": {\n"
		<< "    \"redrawAggression\": \"" << ToString(redrawAggression) << "\"\n"
		<< "  },\n"
		<< "  \"diagnostics\": {\n"
		<< "    \"logging\": \"" << ToString(debugLogging) << "\"\n"
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

bool PluginSettings::NeedsVersionNotice() const
{
	return installedVersion != PluginVersion::String;
}

bool PluginSettings::AcknowledgeCurrentVersion()
{
	installedVersion = PluginVersion::String;
	return Save();
}

const std::filesystem::path& PluginSettings::GetPath() const
{
	return path;
}

const std::string& PluginSettings::GetInstalledVersion() const
{
	return installedVersion;
}
