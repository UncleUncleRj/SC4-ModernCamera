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
    // Adds periodic redraws at a moderate interval - small performance impact.
    High,
    // Adds frequent periodic redraws for maximum responsiveness to camera movement - can cause major performance impact on some systems.
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

namespace PluginSettingText
{
    // Returns the persisted label for a camera mode.
    const char* ToString(CameraMode value);
    // Returns the persisted label for a redraw-aggression level.
    const char* ToString(RedrawAggression value);
    // Returns the persisted label for a diagnostics logging level.
    const char* ToString(DebugLogging value);
}

// Loads, validates, and saves user-configurable plugin settings.
class PluginSettings
{
public:
    // Sensitivity defaults and bounds shared by settings validation and UI sliders.
    static constexpr float kDefaultSensitivity = 1.0f;
    static constexpr float kMinimumSensitivity = 0.1f;
    static constexpr float kMaximumSensitivity = 3.0f;
    static constexpr float kSensitivityRange = kMaximumSensitivity - kMinimumSensitivity;
    static constexpr float kMinimumPanSensitivity = 0.25f;
    static constexpr float kMaximumPanSensitivity = 2.75f;
    static constexpr float kPanSensitivityRange = kMaximumPanSensitivity - kMinimumPanSensitivity;

    // Initializes this settings object with fresh-install defaults.
    PluginSettings();

    // Loads known settings from path; writes defaults when the file does not exist.
    bool Load(const std::filesystem::path& path);
    // Writes the current values to the path most recently supplied to Load.
    bool Save() const;
    // Restores supported fresh-install defaults without writing them to disk.
    void RestoreDefaults();

    // Returns whether the installed version has not acknowledged this plugin version.
    bool NeedsVersionNotice() const;
    // Stores the current plugin version as acknowledged and writes the settings file.
    bool AcknowledgeCurrentVersion();

    // Returns the path retained for future Save calls.
    const std::filesystem::path& GetPath() const;
    // Returns the installed-version marker from the last loaded settings document.
    const std::string& GetInstalledVersion() const;

    // Selects Modern Camera or native Classic camera behavior.
    CameraMode cameraMode;
    // Enables WASD keyboard panning while Modern Camera is active.
    bool wasdMovement;
    // Enables Q and E keyboard rotation while Modern Camera is active.
    bool qeRotation;
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
	// Enables the Ctrl+Alt+F8 camera diagnostics shortcut.
	bool cameraDumpHotkey;
	// Enables Modern-mode numbered bookmark hotkeys.
	bool bookmarksEnabled;
	// Shows the version greeting and changelog after a plugin update.
    bool showChangelogsOnUpdate;

private:
    // Source file used by Save and version-acknowledgement updates.
    std::filesystem::path path;
    // Version recorded by the most recently loaded settings document.
    std::string installedVersion;
};
