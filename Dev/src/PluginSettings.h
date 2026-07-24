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
	Classic,
	Normal,
	High,
	Extreme
};

enum class DebugLogging
{
	Off,
	Normal,
	Verbose
};

namespace PluginSettingText
{
	const char* ToString(CameraMode value);
	const char* ToString(RedrawAggression value);
	const char* ToString(DebugLogging value);
}

class PluginSettings
{
public:
	static constexpr float kDefaultSensitivity = 1.0f;
	static constexpr float kMinimumSensitivity = 0.1f;
	static constexpr float kMaximumSensitivity = 3.0f;
	static constexpr float kSensitivityRange = kMaximumSensitivity - kMinimumSensitivity;
	static constexpr float kMinimumPanSensitivity = 0.25f;
	static constexpr float kMaximumPanSensitivity = 2.75f;
	static constexpr float kPanSensitivityRange = kMaximumPanSensitivity - kMinimumPanSensitivity;

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
	float panSensitivity;
	bool invertVertical;
	RedrawAggression redrawAggression;
	DebugLogging debugLogging;

private:
	std::filesystem::path path;
	std::string installedVersion;
};
