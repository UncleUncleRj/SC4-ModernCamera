#pragma once

#include <filesystem>
#include <string>

enum class CameraMode
{
	// Enables Modern Camera input and free-camera behavior.
	Modern,
	// Leaves SimCity 4's native camera behavior active.
	Classic
};

enum class RedrawAggression
{
	// Uses native redraw timing without Modern Camera redraw scheduling.
	Classic,
	// Schedules redraws after input becomes idle.
	Normal,
	// Adds periodic redraws at a moderate interval.
	High,
	// Adds frequent periodic redraws for maximum responsiveness.
	Extreme
};

enum class DebugLogging
{
	// Disables all plugin log output after startup.
	Off,
	// Records lifecycle events, settings changes, and warnings.
	Normal,
	// Includes input and camera diagnostics intended for troubleshooting.
	Verbose
};

// Loads, validates, and saves the user-configurable plugin settings.
class PluginSettings
{
public:
	// Initializes the settings object with the supported default values.
	PluginSettings();

	// Loads known settings from path; when absent, writes defaults and returns that write result.
	bool Load(const std::filesystem::path& path);
	// Writes the current settings to the path most recently supplied to Load.
	bool Save() const;
	// Restores supported fresh-install defaults without writing them to disk.
	void RestoreDefaults();

	// Returns whether the installed version has not acknowledged this plugin version.
	bool NeedsVersionNotice() const;
	// Stores the current plugin version as acknowledged and writes the settings file.
	bool AcknowledgeCurrentVersion();

	// Returns the path most recently supplied to Load.
	const std::filesystem::path& GetPath() const;
	// Returns the version string currently retained by this settings object.
	const std::string& GetInstalledVersion() const;

	// Selects Modern Camera or native Classic camera behavior.
	CameraMode cameraMode;
	// Enables WASD keyboard panning when Modern Camera is active.
	bool wasdMovement;
	// Multiplies mouse-driven yaw and pitch changes.
	float rotationSensitivity;
	// Multiplies wheel-driven zoom changes.
	float zoomSensitivity;
	// Multiplies keyboard panning speed.
	float panSensitivity;
	// Reverses the sign of vertical mouse rotation.
	bool invertVertical;
	// Controls idle and periodic redraw scheduling.
	RedrawAggression redrawAggression;
	// Controls the amount of diagnostic log output.
	DebugLogging debugLogging;

private:
	// Source file used by Save and version-acknowledgement updates.
	std::filesystem::path path;
	// Version recorded by the most recently loaded settings document.
	std::string installedVersion;
};
