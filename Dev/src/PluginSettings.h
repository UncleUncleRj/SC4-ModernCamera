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

namespace PluginSettingText
{
    // Returns the persisted label for a camera mode.
    const char* ToString(CameraMode value);
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
    static constexpr float kDefaultShiftPanBoost = 2.0f;
    static constexpr float kMinimumShiftPanBoost = 1.0f;
    static constexpr float kMaximumShiftPanBoost = 5.0f;
    static constexpr float kShiftPanBoostRange = kMaximumShiftPanBoost - kMinimumShiftPanBoost;

    // Initializes this settings object with fresh-install defaults.
    PluginSettings();

    // Loads known settings from path; writes defaults when the file does not exist.
    bool Load(const std::filesystem::path& path);
    // Writes the current values to the path most recently supplied to Load.
    bool Save() const;
    // Restores supported fresh-install defaults without writing them to disk.
    void RestoreDefaults();

    // Returns the path retained for future Save calls.
    const std::filesystem::path& GetPath() const;

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
    // True returns to the saved Modern view when Alt is released; false stays on the view Alt let go of.
    bool altCameraBungie;
    // Multiplies the Q and E keyboard rotation speed.
    float qeRotationSpeed;
    // Multiplies keyboard panning speed while Shift is held.
    float shiftPanBoost;
    // Swaps the directions Q and E turn the camera.
    bool invertQE;
    // True zooms smoothly with the wheel; false leaves the wheel to SC4's stepped zoom.
    bool smoothWheelZoom;
    // Reverses the direction of wheel zoom handled by this plugin.
    bool invertWheelZoom;
    // Writes every diagnostic record, including input traces, when true; writes nothing after
    // startup when false, the default.
    bool writeLogs;
	// Enables Modern-mode numbered bookmark hotkeys.
	bool bookmarksEnabled;
	// Announces a newer GitHub release in the news ticker.
    bool notifyUpdatesInTicker;

private:
    // Source file used by Save.
    std::filesystem::path path;
};
