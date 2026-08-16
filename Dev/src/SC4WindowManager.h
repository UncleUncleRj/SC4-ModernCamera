#pragma once

#include "cIGZWinProc.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class cGZMessage;
class cIGZWin;
class PluginSettings;
enum class CameraMode;

// Runtime actions invoked when a plugin window changes camera settings or focus.
struct SC4WindowManagerCallbacks
{
	// Applies a requested Modern or Classic camera mode change.
	std::function<void(bool modernCameraEnabled)> OnCameraModeChanged;
	// Restarts redraw scheduling after its setting changes.
	std::function<void()> OnRedrawAggressionChanged;
	// Resets Modern Camera to its native view state.
	std::function<void()> OnResetCameraRequested;
	// Clears held input after an input setting changes.
	std::function<void()> OnInputSettingsChanged;
	// Applies the selected diagnostic logging verbosity.
	std::function<void()> OnDebugLoggingChanged;
	// Returns keyboard and mouse focus to the city after settings close.
	std::function<void()> OnSettingsWindowClosed;
};

// Opaque handle used to identify a dynamically created basic window.
using SC4WindowHandle = uint32_t;
// Sentinel returned when a managed window could not be created.
inline constexpr SC4WindowHandle InvalidSC4WindowHandle = 0;

enum class SC4WindowButton : uint8_t
{
	// Shows only the title-bar close button.
	XOnly = 0,
	// Shows an OK confirmation button.
	OK = 1,
	// Shows a Close button.
	Close = 2,
	// Shows an Accept button.
	Accept = 3,
};

enum class SC4WindowTemplate : uint8_t
{
	// Creates the interactive native-control laboratory window.
	ControlLaboratory = 0,
};

enum class DeferredWindowOpen : uint8_t
{
	// No deferred window request is pending.
	None = 0,
	// Reopens the advanced settings window after the current transition.
	AdvancedSettings,
	// Opens the changelog window after the current transition.
	Changelog,
};

// Text, dimensions, and controls for a code-created basic window.
struct SC4BasicWindowOptions
{
	// Requested window width in parent-window pixels.
	int32_t width = 420;
	// Requested window height in parent-window pixels.
	int32_t height = 240;
	// Title displayed in the native window caption.
	std::string title;
	// Body text displayed in the native window.
	std::string text;
	// Confirmation control to create with the window.
	SC4WindowButton button = SC4WindowButton::XOnly;
};

// Wraps a simple code-created SC4 window and its native procedure interface.
class BasicManagedWindow final : public cIGZWinProc
{
public:
	// Creates an empty managed native window.
	BasicManagedWindow();
	// Releases any native window resources still owned by this object.
	~BasicManagedWindow();

	// Resolves the native window-procedure interface.
	bool QueryInterface(uint32_t riid, void** ppvObj) override;
	// Increments the COM-style reference count.
	uint32_t AddRef() override;
	// Decrements the COM-style reference count without deleting this owner.
	uint32_t Release() override;
	// Handles high-level GZ messages sent to the managed window.
	bool DoWinProcMessage(cIGZWin* pWin, cGZMessage& msg) override;
	// Handles low-level GZ window messages sent to the managed window.
	bool DoWinMsg(cIGZWin* pWin, uint32_t messageID, uint32_t data1, uint32_t data2, uint32_t data3) override;

	// Builds the native window described by options and attaches this procedure.
	bool Create(const SC4BasicWindowOptions& options);
	// Destroys the native window and releases its retained interfaces.
	void Destroy();
	// Sends the native close request to the root window.
	void Close();
	// Returns whether the root window exists and is visible.
	bool IsVisible() const;
	// Returns whether a parent-window coordinate falls inside the root window.
	bool ContainsPoint(int32_t parentX, int32_t parentY) const;

private:
	// COM-style reference count required by the GZ window procedure interface.
	uint32_t refCount;
	// Parent window that owns rootWindow while this managed window is open.
	cIGZWin* parentWindow;
	// Root native window created for this managed window.
	cIGZWin* rootWindow;
};

// Base wrapper for windows instantiated from the Modern Camera UI DAT.
class BakedManagedWindow : public cIGZWinProc
{
public:
	enum class Placement : uint8_t
	{
		// Centers the baked window in its parent.
		Center,
		// Places the baked window next to the camera menu button.
		TopRightButton,
	};

	// Configures a baked SC4 UI resource and its standard close controls.
	BakedManagedWindow(
		const char* logName,
		uint32_t resourceInstanceID,
		uint32_t rootCLSID,
		uint32_t closeXButtonID,
		uint32_t okButtonID,
		Placement placement = Placement::Center);
	// Releases native interfaces retained by the baked window.
	~BakedManagedWindow();

	// Resolves the native window-procedure interface.
	bool QueryInterface(uint32_t riid, void** ppvObj) override;
	// Increments the COM-style reference count.
	uint32_t AddRef() override;
	// Decrements the COM-style reference count without deleting this owner.
	uint32_t Release() override;
	// Handles high-level GZ messages sent to the baked window.
	bool DoWinProcMessage(cIGZWin* pWin, cGZMessage& msg) override;
	// Handles low-level GZ window messages sent to the baked window.
	bool DoWinMsg(cIGZWin* pWin, uint32_t messageID, uint32_t data1, uint32_t data2, uint32_t data3) override;

	// Loads the baked resource, attaches this procedure, and makes it visible.
	bool Create();
	// Destroys the baked resource and releases retained interfaces.
	void Destroy();
	// Sends the native close request to the baked root window.
	void Close();
	// Moves the baked root window above its siblings.
	void BringToFront();
	// Moves the baked root window behind its siblings.
	void SendToBack();
	// Changes baked root-window visibility without destroying it.
	void SetVisible(bool visible);
	// Returns whether the baked root window exists and is visible.
	bool IsVisible() const;
	// Returns whether a parent-window coordinate falls inside the baked root window.
	bool ContainsPoint(int32_t parentX, int32_t parentY) const;

protected:
	// Initializes subclass-specific controls after resource creation.
	virtual void OnCreated();
	// Handles a GZ command before standard baked-window handling.
	virtual bool OnCommandMessage(cGZMessage& msg);
	// Handles one button ID and returns true when it was consumed.
	virtual bool OnButtonClick(uint32_t controlID);
	// Notifies subclasses after the baked root window closes.
	virtual void OnClosed();
	// Returns the current baked root window without transferring ownership.
	cIGZWin* GetRootWindow() const;

private:
	// Diagnostic label used in window-management logs.
	const char* logName;
	// Instance ID of the baked UI resource to load.
	uint32_t resourceInstanceID;
	// Class ID expected for the baked root window.
	uint32_t rootCLSID;
	// Control ID for the standard title-bar close button.
	uint32_t closeXButtonID;
	// Control ID for the standard confirmation button, or zero when absent.
	uint32_t okButtonID;
	// Screen position requested when the baked resource is opened.
	Placement placement;
	// COM-style reference count required by the GZ window procedure interface.
	uint32_t refCount;
	// Parent window that owns rootWindow while the resource is open.
	cIGZWin* parentWindow;
	// Root window instantiated from the baked resource.
	cIGZWin* rootWindow;
};

// Baked settings dialog that mirrors and updates PluginSettings.
class SettingsWindow final : public BakedManagedWindow
{
public:
	// Creates the baked settings window with its SC4 resource identifiers.
	SettingsWindow();
	// Supplies the window manager that owns this settings window.
	void SetWindowManager(class SC4WindowManager* manager);
	// Supplies the settings object mirrored by the controls.
	void SetSettings(PluginSettings* settings);
	// Supplies runtime callbacks invoked after settings changes.
	void SetCallbacks(const SC4WindowManagerCallbacks* callbacks);
	// Copies the current settings values into visible controls.
	void RefreshFromSettings();
	// Saves a delayed slider or toggle change when its debounce timer fires.
	void OnDelayedSettingsSaveTimer();

protected:
	// Initializes control state after the baked settings resource is created.
	void OnCreated() override;
	// Handles GZ command messages emitted by settings controls.
	bool OnCommandMessage(cGZMessage& msg) override;
	// Handles button presses emitted by settings controls.
	bool OnButtonClick(uint32_t controlID) override;
	// Flushes pending settings and restores city focus after the window closes.
	void OnClosed() override;

private:
	// Synchronizes every visible control with the supplied settings object.
	void SyncControlsFromSettings();
	// Converts a changed sensitivity slider position into its matching setting value.
	bool ApplySensitivitySliderChange(uint32_t controlID);
	// Applies a camera mode selected by the primary settings window.
	void ApplyCameraMode(CameraMode mode);
	// Selects the matching on or off control for a boolean value.
	void ApplyBooleanPair(uint32_t onID, uint32_t offID, bool value);
	// Selects one control from a mutually exclusive control group.
	void ApplyChoice(uint32_t selectedID, const uint32_t* ids, size_t count);
	// Saves now or defers the current settings change, then refreshes visible controls.
	void ApplySettingsChange(const char* reason, bool saveImmediately = true);
	// Writes the current settings when a deferred save is pending.
	void FlushPendingSettingsSave();
	// Defers one settings write to coalesce rapid control changes.
	void ScheduleSettingsSave(const char* reason);
	// Maps cursor position over a slider to a clamped setting value.
	float ReadSliderValueFromCursor(uint32_t sliderID, float fallback, float minimum, float range) const;

	// Owner that coordinates settings windows with city lifecycle state.
	SC4WindowManager* manager;
	// Mutable settings object updated by controls in this window.
	PluginSettings* settings;
	// Runtime callbacks supplied by the owning window manager.
	const SC4WindowManagerCallbacks* callbacks;
	// Win32 timer ID used to debounce settings writes. Zero means inactive.
	uintptr_t delayedSaveTimerID;
	// Indicates that delayedSaveReason should be written when the timer fires.
	bool delayedSavePending;
	// Prevents programmatic control updates from being treated as user changes.
	bool syncingControls;
	// Diagnostic reason retained for the delayed settings write.
	std::string delayedSaveReason;
};

// Floating baked button that opens and closes the settings dialog.
class MenuButtonWindow final : public BakedManagedWindow
{
public:
	// Creates the baked camera menu-button window.
	MenuButtonWindow();
	// Supplies the manager used to open and close settings.
	void SetWindowManager(class SC4WindowManager* manager);
	// Supplies the settings object that determines the current camera-mode presentation.
	void SetSettings(PluginSettings* settings);
	// Updates the menu-button state to match settings visibility.
	void SetSettingsOpen(bool open);
	// Selects the Modern or Classic resource that represents the current camera mode.
	void RefreshMode();
	// Applies the menu button's hover cursor for its current region.
	void RefreshCursor();
	// Updates the native cursor when the supplied coordinate lies over the menu button.
	bool UpdateCursorIfHovered(int32_t parentX, int32_t parentY);

protected:
	// Synchronizes the mode resource when the baked menu button is created.
	void OnCreated() override;
	// Opens or closes settings when the menu button is pressed.
	bool OnButtonClick(uint32_t controlID) override;

private:
	// Updates the visible Modern or Classic button's native cursor directly.
	void UpdateActiveButtonCursor();
	// Manager that owns settings-window visibility and callbacks.
	SC4WindowManager* manager;
	// Settings object used to decide which mode artwork is visible.
	PluginSettings* settings;
};

// Baked welcome dialog that routes users to plugin information and settings.
class GreetingWindow final : public BakedManagedWindow
{
public:
	// Creates the baked greeting window.
	GreetingWindow();
	// Supplies the manager that opens the selected greeting destination.
	void SetWindowManager(class SC4WindowManager* manager);
	// Supplies the settings object changed by the greeting's update-notice control.
	void SetSettings(PluginSettings* settings);
	// Synchronizes greeting controls with the supplied settings.
	void RefreshFromSettings();

protected:
	// Synchronizes greeting controls after its baked resource is created.
	void OnCreated() override;
	// Handles greeting buttons such as changelog and settings.
	bool OnButtonClick(uint32_t controlID) override;

private:
	// Manager that owns the target baked windows.
	SC4WindowManager* manager;
	// Settings object used by the update-changelog preference control.
	PluginSettings* settings;
};

// Baked reference window describing Modern Camera controls.
class ControlsWindow final : public BakedManagedWindow
{
public:
	// Creates the baked controls-reference window.
	ControlsWindow();
};

// Baked dialog for settings not shown in the primary settings window.
class AdvancedSettingsWindow final : public BakedManagedWindow
{
public:
	// Creates the baked advanced-settings window.
	AdvancedSettingsWindow();
	// Supplies the manager used for advanced settings transitions.
	void SetWindowManager(class SC4WindowManager* manager);
	// Supplies the settings object mirrored by advanced controls.
	void SetSettings(PluginSettings* settings);
	// Supplies callbacks used after an advanced setting changes.
	void SetCallbacks(const SC4WindowManagerCallbacks* callbacks);
	// Copies current advanced settings into visible controls.
	void RefreshFromSettings();

protected:
	// Initializes advanced controls after the baked resource is created.
	void OnCreated() override;
	// Handles advanced-settings button presses.
	bool OnButtonClick(uint32_t controlID) override;

private:
	// Synchronizes advanced controls with their backing settings values.
	void SyncControlsFromSettings();

	// Manager coordinating settings-window state during advanced transitions.
	SC4WindowManager* manager;
	// Settings object read and changed by advanced controls.
	PluginSettings* settings;
	// Runtime callbacks used to apply advanced setting changes.
	const SC4WindowManagerCallbacks* callbacks;
};

// Experimental native-control workspace used to inspect GZ UI behavior.
class ControlLaboratoryWindow final : public cIGZWinProc
{
public:
	// Creates an empty experimental native-control laboratory.
	ControlLaboratoryWindow();
	// Releases native controls and interfaces retained by the laboratory.
	~ControlLaboratoryWindow();

	// Resolves the native window-procedure interface.
	bool QueryInterface(uint32_t riid, void** ppvObj) override;
	// Increments the COM-style reference count.
	uint32_t AddRef() override;
	// Decrements the COM-style reference count without deleting this owner.
	uint32_t Release() override;
	// Handles high-level GZ messages sent to laboratory controls.
	bool DoWinProcMessage(cIGZWin* pWin, cGZMessage& msg) override;
	// Handles low-level GZ window messages sent to laboratory controls.
	bool DoWinMsg(cIGZWin* pWin, uint32_t messageID, uint32_t data1, uint32_t data2, uint32_t data3) override;

	// Builds the native-control laboratory and attaches its window procedure.
	bool Create();
	// Destroys the laboratory and releases each retained native interface.
	void Destroy();
	// Returns whether the laboratory root window exists and is visible.
	bool IsVisible() const;
	// Returns whether a parent-window coordinate falls inside the laboratory.
	bool ContainsPoint(int32_t parentX, int32_t parentY) const;
	// Scrolls laboratory content when the pointer is over the laboratory.
	bool HandleMouseWheel(int32_t wheelDelta, int32_t parentX, int32_t parentY, intptr_t nativeWindowHandle);

private:
	// Retained native control and its unscrolled laboratory layout.
	struct ChildControl
	{
		// Native child window placed in the laboratory.
		cIGZWin* window;
		// Resource or generated identifier assigned to the child control.
		uint32_t id;
		// Unscrolled left position in parent-window pixels.
		int32_t baseLeft;
		// Unscrolled top position in parent-window pixels.
		int32_t baseTop;
		// Control width in parent-window pixels.
		int32_t width;
		// Control height in parent-window pixels.
		int32_t height;
		// Keeps the control fixed while the rest of the laboratory scrolls.
		bool fixed;
		// Human-readable label used in event diagnostics.
		std::string name;
	};

	// Latest diagnostic message data recorded for one laboratory control.
	struct EventState
	{
		// Number of messages observed for the control.
		uint32_t count = 0;
		// Most recent GZ message type received by the control.
		uint32_t messageType = 0;
		// First data value from the most recent GZ message.
		uint32_t data1 = 0;
		// Third data value from the most recent GZ message.
		uint32_t data3 = 0;
		// Last observed selection state for the control.
		bool selected = false;
		// Last observed caption for the control.
		std::string caption;
	};

	// Creates and lays out the laboratory's native controls.
	bool BuildControls();
	// Changes the content scroll offset by the requested number of pixels.
	void ScrollBy(int32_t delta);
	// Activates the native scrollbar arrow represented by direction.
	void ActivateScrollbarArrow(int32_t direction, intptr_t nativeWindowHandle);
	// Applies scrollOffset to every scrollable laboratory control.
	void ApplyScrollPosition();
	// Records the latest event details for one laboratory control.
	void RecordEvent(const cGZMessage& msg);
	// Persists laboratory event diagnostics for the current test session.
	void SaveTestState() const;
	// Returns recorded layout data for a child ID, or null when it is unknown.
	const ChildControl* FindControl(uint32_t id) const;

	// COM-style reference count required by the GZ window procedure interface.
	uint32_t refCount;
	// Parent window that owns rootWindow while the laboratory is open.
	cIGZWin* parentWindow;
	// Root native window that contains the laboratory controls.
	cIGZWin* rootWindow;
	// Current vertical content offset in parent-window pixels.
	int32_t scrollOffset;
	// Wheel delta retained until it reaches one native scroll notch.
	int32_t wheelDeltaRemainder;
	// Last scrollbar-arrow direction requested through the native handle.
	int32_t wheelScrollbarDirection;
	// Prevents the paired native scrollbar notification from scrolling twice.
	bool wheelScrollbarNotificationHandled;
	// Retained native controls and their unscrolled layout metadata.
	std::vector<ChildControl> controls;
	// Most recent message state indexed by laboratory control ID.
	std::unordered_map<uint32_t, EventState> eventStates;
};

// Owns every plugin-created SC4 window and is the single integration point
// for city lifecycle, popup dialogs, and window-specific input routing.
class SC4WindowManager
{
public:
	// Replaces callbacks used to apply window-originated runtime changes.
	void SetCallbacks(SC4WindowManagerCallbacks callbacks);
	// Initializes windows and settings references after a city is loaded.
	void OnCityLoaded(PluginSettings& settings);
	// Closes every plugin window and releases city-specific state.
	void OnCityShutdown();
	// Shows the version notice when its deferred timer fires.
	void OnVersionNoticeTimer();
	// Reopens the settings window when its deferred timer fires.
	void OnSettingsReopenTimer();
	// Opens the queued baked window when its deferred timer fires.
	void OnDeferredWindowOpenTimer();

	// Shows the version notice when the settings require acknowledgement.
	bool ShowVersionNoticeIfNeeded(PluginSettings& settings);
	// Invokes the native notification dialog with the supplied caption and message.
	bool ShowNotification(const char* caption, const char* message) const;
	// Creates a default basic managed window and returns its handle.
	SC4WindowHandle CreateManagedWindow();
	// Creates a basic managed window from caller-supplied options.
	SC4WindowHandle CreateManagedWindow(const SC4BasicWindowOptions& options);
	// Creates one supported managed window template.
	SC4WindowHandle CreateManagedWindow(SC4WindowTemplate windowTemplate);
	// Closes and removes the basic managed window identified by handle.
	bool CloseWindow(SC4WindowHandle handle);

	// Opens the experimental native-control laboratory.
	bool ShowControlLaboratory();
	// Opens the baked greeting window.
	bool ShowGreetingWindow();
	// Opens the baked controls-reference window.
	bool ShowControlsWindow();
	// Opens the baked advanced-settings window.
	bool ShowAdvancedSettingsWindow();
	// Opens the baked settings window.
	bool ShowSettingsWindow();
	// Opens settings when closed or closes it when already visible.
	bool ToggleSettingsWindow();
	// Switches between Modern and Classic camera modes from the floating menu button.
	void ToggleCameraMode();
	// Shows or hides the floating camera menu button.
	void SetMenuButtonVisible(bool visible);
	// Synchronizes dependent windows and invokes the settings-close callback.
	void OnSettingsWindowClosed();
	// Copies current settings into every visible settings window.
	void RefreshSettingsWindows();
	// Defers reopening settings until the current UI transition completes.
	void ScheduleSettingsWindowReopen();
	// Defers opening one baked window until the current UI transition completes.
	void ScheduleDeferredWindowOpen(DeferredWindowOpen window);
	// Closes all baked, laboratory, and basic managed windows.
	void CloseAllWindows();
	// Returns whether any plugin-owned window is visible.
	bool HasVisibleWindow() const;
	// Returns whether a parent-window coordinate is over a visible plugin window.
	bool IsPointOverVisibleWindow(int32_t parentX, int32_t parentY);
	// Routes wheel input to the visible plugin window under the pointer.
	bool HandleMouseWheel(
		int32_t wheelDelta, int32_t parentX, int32_t parentY, intptr_t nativeWindowHandle);

private:
	// Handle and owning wrapper for a dynamically created basic window.
	struct BasicWindowEntry
	{
		// Handle returned to callers for this dynamically created window.
		SC4WindowHandle handle;
		// Owning pointer to the dynamically created native window wrapper.
		std::unique_ptr<BasicManagedWindow> window;
	};

	// Experimental native-control laboratory instance.
	ControlLaboratoryWindow controlLaboratory;
	// Baked greeting window instance.
	GreetingWindow greetingWindow;
	// Baked controls-reference window instance.
	ControlsWindow controlsWindow;
	// Baked advanced-settings window instance.
	AdvancedSettingsWindow advancedSettingsWindow;
	// Baked primary settings window instance.
	SettingsWindow settingsWindow;
	// Floating menu-button window instance.
	MenuButtonWindow menuButtonWindow;
	// Dynamically created basic managed windows.
	std::vector<BasicWindowEntry> basicWindows;
	// Runtime callbacks applied after window-originated state changes.
	SC4WindowManagerCallbacks callbacks;
	// Settings object retained while a deferred version notice is pending.
	PluginSettings* pendingSettings = nullptr;
	// Next nonzero handle allocated to a basic managed window.
	SC4WindowHandle nextWindowHandle = 100;
	// Settings object retained until the version-notice timer fires.
	PluginSettings* pendingVersionNoticeSettings = nullptr;
	// One-shot timer ID for deferred version notices. Zero means inactive.
	uintptr_t versionNoticeTimerID = 0;
	// One-shot timer ID for deferred settings reopen. Zero means inactive.
	uintptr_t settingsReopenTimerID = 0;
	// One-shot timer ID for deferred baked-window opening. Zero means inactive.
	uintptr_t deferredWindowOpenTimerID = 0;
	// Baked window request retained until deferredWindowOpenTimerID fires.
	DeferredWindowOpen pendingDeferredWindowOpen = DeferredWindowOpen::None;
};
