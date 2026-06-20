#pragma once

#include <filesystem>
#include <string>

enum class CameraMode
{
	Modern,
	Classic
};

enum class RedrawAggression
{
	Off,
	Normal,
	High,
	Aggressive
};

enum class DebugLogging
{
	Off,
	Normal,
	Verbose
};

class PluginSettings
{
public:
	PluginSettings();

	bool Load(const std::filesystem::path& path);
	bool Save() const;
	void RestoreDefaults();

	bool NeedsVersionNotice() const;
	bool AcknowledgeCurrentVersion();

	const std::filesystem::path& GetPath() const;
	const std::string& GetInstalledVersion() const;

	CameraMode cameraMode;
	bool wasdMovement;
	float rotationSensitivity;
	float zoomSensitivity;
	bool invertVertical;
	RedrawAggression redrawAggression;
	DebugLogging debugLogging;

private:
	std::filesystem::path path;
	std::string installedVersion;
};
