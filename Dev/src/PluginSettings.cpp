#include "PluginSettings.h"

#include "Logger.h"

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
	altCameraBungie = true;
	qeRotationSpeed = kDefaultSensitivity;
	shiftPanBoost = kDefaultShiftPanBoost;
	invertQE = false;
	smoothWheelZoom = true;
	invertWheelZoom = false;
	// Off until a player turns logs on to report a problem.
	writeLogs = false;
	bookmarksEnabled = true;
	notifyUpdatesInTicker = true;
}

// Loads known fields while retaining defaults for missing or invalid values.
bool PluginSettings::Load(const std::filesystem::path& settingsPath)
{
	path = settingsPath;
	RestoreDefaults();

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
	const bool hasAltCameraBungie = TryReadBool(json, "altCameraBungie", altCameraBungie);

	const bool hasQERotationSpeed = TryReadFloat(json, "qeRotationSpeed", qeRotationSpeed);
	TryReadFloat(json, "shiftPanBoost", shiftPanBoost);
	TryReadBool(json, "invertQE", invertQE);
	TryReadBool(json, "smoothWheelZoom", smoothWheelZoom);
	TryReadBool(json, "invertWheelZoom", invertWheelZoom);

	const bool hasWriteLogs = TryReadBool(json, "writeLogs", writeLogs);
	// Files from before Write Logs stored a level. Normal was its default and follows the new
	// default; only Verbose was a deliberate choice to write logs.
	if (!hasWriteLogs && TryReadString(json, "logging", value))
	{
		writeLogs = value == "Verbose";
	}
	TryReadBool(json, "bookmarksEnabled", bookmarksEnabled);
	// Someone who turned off the old update popup also doesn't want the ticker message.
	TryReadBool(json, "showChangelogsOnUpdate", notifyUpdatesInTicker);
	TryReadBool(json, "notifyUpdatesInTicker", notifyUpdatesInTicker);

	rotationSensitivity = std::clamp(rotationSensitivity, kMinimumSensitivity, kMaximumSensitivity);
	zoomSensitivity = std::clamp(zoomSensitivity, kMinimumSensitivity, kMaximumSensitivity);
	panSensitivity = std::clamp(panSensitivity, kMinimumPanSensitivity, kMaximumPanSensitivity);
	// Q/E speed used to follow rotation sensitivity, so older files keep the speed they had.
	if (!hasQERotationSpeed)
	{
		qeRotationSpeed = rotationSensitivity;
	}
	qeRotationSpeed = std::clamp(qeRotationSpeed, kMinimumSensitivity, kMaximumSensitivity);
	shiftPanBoost = std::clamp(shiftPanBoost, kMinimumShiftPanBoost, kMaximumShiftPanBoost);

	Logger::GetInstance().WriteLine(LogLevel::Info, "Loaded settings from " + path.string());

	// Settings files from before these options get them written at their starting values,
	// and an old logging level is replaced by writeLogs.
	if (!hasAltCameraBungie || !hasQERotationSpeed || !hasWriteLogs)
	{
		Save();
	}
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
		<< "  \"camera\": {\n"
		<< "    \"mode\": \"" << PluginSettingText::ToString(cameraMode) << "\",\n"
		<< "    \"wasdMovement\": " << (wasdMovement ? "true" : "false") << ",\n"
		<< "    \"qeRotation\": " << (qeRotation ? "true" : "false") << ",\n"
		<< "    \"rotationSensitivity\": " << rotationSensitivity << ",\n"
		<< "    \"zoomSensitivity\": " << zoomSensitivity << ",\n"
		<< "    \"panSensitivity\": " << panSensitivity << ",\n"
		<< "    \"invertVertical\": " << (invertVertical ? "true" : "false") << ",\n"
		<< "    \"altCameraBungie\": " << (altCameraBungie ? "true" : "false") << ",\n"
		<< "    \"qeRotationSpeed\": " << qeRotationSpeed << ",\n"
		<< "    \"shiftPanBoost\": " << shiftPanBoost << ",\n"
		<< "    \"invertQE\": " << (invertQE ? "true" : "false") << ",\n"
		<< "    \"smoothWheelZoom\": " << (smoothWheelZoom ? "true" : "false") << ",\n"
		<< "    \"invertWheelZoom\": " << (invertWheelZoom ? "true" : "false") << "\n"
		<< "  },\n"
		<< "  \"diagnostics\": {\n"
		<< "    \"writeLogs\": " << (writeLogs ? "true" : "false") << "\n"
		<< "  },\n"
		<< "  \"bookmarks\": {\n"
		<< "    \"bookmarksEnabled\": " << (bookmarksEnabled ? "true" : "false") << "\n"
		<< "  },\n"
		<< "  \"ui\": {\n"
		<< "    \"notifyUpdatesInTicker\": " << (notifyUpdatesInTicker ? "true" : "false") << "\n"
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

// Returns the path retained for future Save calls.
const std::filesystem::path& PluginSettings::GetPath() const
{
	return path;
}
