#include "cIGZCanvas.h"
#include "cIGZCanvasW32.h"
#include "cIGZGraphicSystem.h"
#include "cIGZMessage2Standard.h"
#include "cIGZMessageServer2.h"
#include "cIGZString.h"
#include "cIGZSystemService.h"
#include "cIGZWin.h"
#include "cIGZWinMgr.h"
#include "cIGZWinProcFilterW32.h"
#include "cIGZWinTextEdit.h"
#include "cISC4App.h"
#include "cISC4Nation.h"
#include "cISC4RegionalCity.h"
#include "cISC4View3DWin.h"
#include "cISC4ViewInputControl.h"
#include "cRZAutoRefCount.h"
#include "cRZMessage2COMDirector.h"
#include "SC4String.h"
#include "GZServPtrs.h"
#include "Hooks/VTableHook.h"
#include "CameraBookmarkStore.h"
#include "Input/KeyStateManager.h"
#include "Logger.h"
#include "PluginPaths.h"
#include "PluginSettings.h"
#include "PluginVersion.h"
#include "Runtime/TimerManager.h"
#include "SC4CameraController.h"
#include "SC4VersionDetection.h"
#include "SC4WindowManager.h"
#include <Windows.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cctype>
#include <exception>
#include <filesystem>
#include <functional>
#include <optional>
#include <sstream>
#include <string>

// Game lifecycle message identifiers
// Reverse-engineered SimCity 4 message IDs for post-city initialization,
// pre-city shutdown, pre-save, post-save, and occupant insertion and removal.
// The plugin subscribes to these fixed IDs; it does not assign their values.
static constexpr uint32_t kSC4MessagePostCityInit = 0x26D31EC1;
static constexpr uint32_t kSC4MessagePreCityShutdown = 0x26D31EC2;
static constexpr uint32_t kSC4MessagePreSave = 0x26C63343;
static constexpr uint32_t kSC4MessagePostSave = 0x26C63345;
static constexpr uint32_t kSC4MessageInsertOccupant = 0x99EF1142;
static constexpr uint32_t kSC4MessageRemoveOccupant = 0x99EF1143;

// Reverse-engineered GZ window IDs used to resolve the SC4 application and 3D
// view, and to recognize input targeting the full 3D surface.
static constexpr uint32_t kGZWin_WinSC4App = 0x6104489A;
static constexpr uint32_t kGZWin_SC4View3DWin = 0x9A47B417;
static constexpr uint32_t kGZWin_SC4View3DSurface = 0x6A5E44B6;

// View input control ID of the U-Drive-It vehicle tool, set by its constructor at
// 0x565557 and the value cISC4AutomataManager::IsDrivingMode tests at 0x506940.
static constexpr uint32_t kViewInputControlUDriveIt = 0x2B709676;

// cISC4View3DWin vtable slots used to install the SetScrolling and MinimizeUI
// hooks. They are valid only for the supported game executable and GZCOM layout.
static constexpr size_t kView3DWinSetScrollingVTableIndex = 33;
static constexpr size_t kView3DWinMinimizeUIVTableIndex = 55;

// Multiplier applied to mouse movement before the user rotation-sensitivity setting.
static constexpr float kMouseRotationSensitivity = 0.005f;

// F8 key in the optional Ctrl+Alt+F8 camera diagnostics shortcut.
static constexpr UINT kDumpCameraInfoKey = VK_F8;

// Bounds of the lower-left diagnostic click area for native-UI toggle activity.
static constexpr int32_t kNativeUICornerProbeMaxX = 260;
static constexpr int32_t kNativeUICornerProbeMaxBottomOffset = 360;

// Input and redraw constants. Names ending in Ms are durations in milliseconds.
static constexpr UINT kPanIdleRedrawDelayMs = 1000; // Delay before redrawing after rotation ends.
static constexpr UINT kCameraFrameFallbackMs = 33; // Camera frame interval if the framework tick is unavailable.
static constexpr float kKeyboardPanNativeSpeed = 0.25f; // Base speed before settings and Shift boost.
// Angular speed applied while Q or E is held for keyboard rotation.
static constexpr float kKeyboardRotationRadiansPerSecond = SC4CameraController::kPi / 4.0f;
static constexpr UINT kZoomIdleRedrawDelayMs = 1500; // Delay before redrawing after mouse-wheel zoom.
static constexpr UINT kCameraDumpConfirmationDelayMs = 2500; // Lifetime of the dump confirmation.
static constexpr UINT kBookmarkConfirmationDelayMs = 2500; // Lifetime of the bookmark confirmation.
static constexpr UINT kModernCameraFocusRestoreDelayMs = 150; // Delay before restoring 3D-view focus.

// Plugin-owned identifier of the camera frame service on the framework tick list.
static constexpr uint32_t kCameraTickServiceID = 0x3D0C4D10;
// Tick priorities run high to low, so this follows message delivery (5,000,000)
// and precedes the draw system (1,500,000).
static constexpr int32_t kCameraTickPriority = 2000000;
// Longest frame time applied to held-key rotation, limiting the jump after a stall.
static constexpr float kMaximumCameraFrameSeconds = 0.25f;
// Moving the hidden cursor beyond this fraction of the view from its center recenters it.
static constexpr int kRotationCursorRecenterDivisor = 4;

// Inclusive ID range recognized as Modern Camera UI by IsModernCameraWindowID.
static constexpr uint32_t kModernCameraWindowIDMin = 0x3D0C0700;
static constexpr uint32_t kModernCameraWindowIDMax = 0x3D0C09FF;

// Global State
// True between the post-city-init and pre-city-shutdown notifications.
bool g_IsCityLoaded = false;
// True when Modern Camera input and camera behavior are active.
bool g_IsModernCamEnabled = true;
// True while a U-Drive-It drive has temporarily handed Modern Camera input to the game.
bool g_IsUDriveItActive = false;
// True when SimCity 4's native UI has been minimized.
bool g_IsNativeUIHidden = false;
// Native MinimizeUI method signature captured by the vtable hook.
using MinimizeUIFunction = bool(__thiscall*)(cISC4View3DWin* view3D, bool minimize);
// Native SetScrolling method signature captured by the vtable hook.
// The game's function returns nothing: 0x7F64F0 exits with the angle's float bits in EAX.
using SetScrollingFunction = void(__thiscall*)(cISC4View3DWin* view3D, bool scrolling, float x, float z);
// Owns the native UI-minimize vtable replacement.
VTableHook g_MinimizeUIHook;
// Owns the native scrolling vtable replacement.
VTableHook g_SetScrollingHook;

enum class View3DToolClearReason : uint8_t
{
    // Modern Camera has become active and must clear a native tool.
    ModernCameraEnabled,
    // The WASD setting changed and native movement state must be reset.
    WASDMovementChanged,
    // Classic camera has become active and must receive native input cleanly.
    ClassicCameraEnabled,
};

// Reads the current physical state for a virtual key through Win32.
bool ReadPhysicalKeyState(int key)
{
    return (GetAsyncKeyState(key) & 0x8000) != 0;
}

/// <summary>
/// Logs an exception stopped at a native callback boundary.
/// </summary>
/// <param name="callback">Name of the callback that failed.</param>
/// <param name="detail">Description of the failure.</param>
void LogNativeCallbackFailure(const char* callback, const char* detail) noexcept
{
    try {
        Logger::GetInstance().WriteLine(LogLevel::Error, std::string(callback) + " failed: " + detail);
    }
    catch (...) {
        // Logging is the last resort, and nothing may escape to the native caller.
    }
}

/// <summary>
/// Runs a callback that SC4 or Windows invoked without letting a C++ exception reach
/// the caller. SC4's message server and tick loop catch nothing, so an escaping
/// exception would skip other services or end the process.
/// </summary>
/// <param name="callback">Name logged if the callback fails.</param>
/// <param name="function">Callback body.</param>
template <typename Function>
void RunGuardedNativeCallback(const char* callback, Function&& function) noexcept
{
    try {
        function();
    }
    catch (const std::exception& exception) {
        LogNativeCallbackFailure(callback, exception.what());
    }
    catch (...) {
        LogNativeCallbackFailure(callback, "unknown exception");
    }
}

// Cinematic Camera State
// Prevents the scrolling hook from treating keyboard pan as unrelated native input.
bool g_IsApplyingKeyboardPan = false;
// Tracks observed keys and their Modern Camera ownership.
KeyStateManager g_KeyStateManager(ReadPhysicalKeyState);
// WASD virtual keys eligible for panning when the corresponding setting is enabled.
static constexpr std::array<WPARAM, 4> kWASDKeyboardPanKeys = { 'W', 'A', 'S', 'D' };
// Arrow-key virtual keys eligible for Modern Camera panning.
static constexpr std::array<WPARAM, 4> kArrowKeyboardPanKeys = { VK_UP, VK_LEFT, VK_DOWN, VK_RIGHT };
// Alt virtual keys that activate the temporary Classic-angle state.
static constexpr std::array<WPARAM, 3> kClassicAngleKeys = { VK_MENU, VK_LMENU, VK_RMENU };
// Complete movement-key set used to query and release Modern Camera pan ownership.
static constexpr std::array<WPARAM, 8> kKeyboardPanKeys = {
    'W',
    'A',
    'S',
    'D',
    VK_UP,
    VK_LEFT,
    VK_DOWN,
    VK_RIGHT,
};
// Q and E keys that drive continuous Modern Camera yaw rotation.
static constexpr std::array<WPARAM, 2> kKeyboardRotationKeys = { 'Q', 'E' };
// Last canvas mouse position used to compute rotation deltas.
POINT g_LastMousePos = { 0, 0 };
// Client position where the current middle-mouse gesture began.
POINT g_RotationCursorOrigin = { 0, 0 };
// True after the current middle-mouse gesture has moved the hidden cursor.
bool g_RotationCursorWarped = false;
// Mouse yaw accumulated by canvas messages until the next camera frame.
float g_PendingMouseYawDelta = 0.0f;
// Mouse pitch accumulated by canvas messages until the next camera frame.
float g_PendingMousePitchDelta = 0.0f;
// True from the post-save message until the next camera frame restores the free camera.
bool g_SavePreviewRestorePending = false;
// True while the framework tick delivers camera frames.
bool g_CameraTickRegistered = false;
// Time of the previous camera frame.
std::chrono::steady_clock::time_point g_LastCameraFrameTime = std::chrono::steady_clock::now();
// Window that owns mouse capture during a Modern Camera rotation gesture.
HWND g_CapturedMouseWindow = NULL;
// True after Modern Camera hides the cursor for a middle-mouse rotation gesture.
bool g_RotationCursorHidden = false;
// True while Q or E owns the controller's shared rotation gesture.
bool g_KeyboardRotationGestureActive = false;
// Last valid SC4 canvas window observed by the WinProc filter.
HWND g_CanvasWindow = NULL;
// Low-level hook that observes keyboard state while Modern Camera is active.
HHOOK g_KeyboardHook = NULL;
// Owns Modern Camera state and direct native camera operations.
SC4CameraController g_CameraController;
// Owns persistent, per-city bookmark records and its asynchronous file writer.
CameraBookmarkStore g_CameraBookmarks;
// Persisted settings currently applied to the plugin.
PluginSettings g_Settings;
// Owns plugin-created SC4 windows and UI callbacks.
SC4WindowManager g_WindowManager;

// Owns all Win32 timers started by the Modern Camera runtime.
TimerManager g_TimerManager;
// Limits deferred focus restoration to Modern Camera when true.
bool g_View3DFocusRestoreRequiresModern = true;
// Diagnostic reason logged when deferred 3D-view focus is restored.
const char* g_View3DFocusRestoreContext = "View3D focus restore";
// Director callback that attaches the canvas WinProc filter.
std::function<void()> g_RegisterCanvasWinProcFilterCallback;
// Director callback that detaches the canvas WinProc filter.
std::function<void()> g_UnregisterCanvasWinProcFilterCallback;

// Executes a delayed city redraw.
VOID CALLBACK RedrawTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
// Runs camera frames from a timer when the framework tick is unavailable.
VOID CALLBACK CameraFrameFallbackTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
// Clears the temporary camera-dump confirmation state.
VOID CALLBACK ClearCameraDumpConfirmationTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
// Clears the temporary bookmark confirmation state.
VOID CALLBACK ClearCameraBookmarkConfirmationTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
// Shows the bookmark confirmation for a short interval.
void StartCameraBookmarkConfirmationTimer();
// Returns focus to the 3D view after a UI transition.
VOID CALLBACK ModernCameraFocusRestoreTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
// Observes global keyboard input and captures eligible camera controls.
LRESULT CALLBACK KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam);
// Installs the low-level keyboard state observer.
void InstallKeyboardHook();
// Removes the low-level keyboard state observer.
void UninstallKeyboardHook();
// Installs the native scrolling bounds hook.
void InstallSetScrollingHook();
// Restores the original native scrolling handler.
void UninstallSetScrollingHook();
// Registers the director's canvas Windows-message filter.
void RegisterCanvasWinProcFilterForModernCamera();
// Unregisters the director's canvas Windows-message filter.
void UnregisterCanvasWinProcFilterForModernCamera();
// Stops camera movement from previously captured keyboard keys.
void StopHeldKeyboardPan();
// Stops held Q/E rotation and releases its camera-input ownership.
void StopHeldKeyboardRotation(const char* reason);
// Ends a temporary Alt-key classic-angle hold.
void StopClassicAngleHold(const char* source);
// Returns whether native right-click scrolling is active.
bool IsRightClickScrollingActive();
// Focuses the 3D view before handing it native input.
bool FocusView3DForNativeInput(const char* context);
// Restores city focus after the settings window closes.
void RestoreWorldFocusAfterSettingsClosed();
// Captures the active full camera view into one zero-based bookmark slot.
bool SaveCameraBookmark(uint32_t slot);
// Restores one zero-based bookmark slot with city-bounds correction.
bool RecallCameraBookmark(uint32_t slot);
// Returns a stable per-region/per-city identity for persistence lookup.
bool GetActiveBookmarkCityKey(std::string& cityKey);
// Activates the persistent slot set for the currently loaded region and city.
bool ActivateCurrentBookmarkCity();
// Mirrors native UI minimization to Modern Camera UI state.
bool __fastcall HookedMinimizeUI(cISC4View3DWin* view3D, void* edx, bool minimize);
// Limits native scrolling to the city bounds before forwarding it.
void __fastcall HookedSetScrolling(cISC4View3DWin* view3D, void* edx, bool scrolling, float x, float z);

// Result of locating the native window under a wheel-input point.
struct NativeWheelHitTest
{
    bool resolved = false;
    bool overView3D = false;
    bool targetIsView3D = false;
    bool targetIsView3DSurface = false;
    bool insideView3DRect = false;
    std::string targetSource;
    std::string targetDescription;
    std::string globalTargetDescription;
    std::string mainTargetDescription;
    std::string parentTargetDescription;
    std::string viewDescription;
};

// Formats a window identifier as uppercase hexadecimal for diagnostics.
std::string FormatWindowID(uint32_t value)
{
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << value;
    return stream.str();
}

// Resolves a stable bookmark key from SC4's public nation and regional-city
// services. The active region directory is valid before the city has ever
// been saved, unlike the optional city-save path.
bool GetActiveBookmarkCityKey(std::string& cityKey)
{
	cityKey.clear();
	cISC4AppPtr app;
	if (!app)
	{
		return false;
	}
	cISC4RegionalCity* regionalCity = app->GetRegionalCity();
	if (!regionalCity)
	{
		return false;
	}
	cISC4Nation* nation = app->GetNation();
	SC4String* regionDirectory = nation ? nation->GetRegionDirectory() : nullptr;
	const char* regionText = regionDirectory ? regionDirectory->ToChar() : nullptr;
	if (!regionText || !*regionText)
	{
		return false;
	}

	std::string normalizedPath = regionText;
	std::transform(
		normalizedPath.begin(),
		normalizedPath.end(),
		normalizedPath.begin(),
		[](unsigned char value) { return static_cast<char>(std::tolower(value)); });
	cityKey = std::to_string(regionalCity->GetCitySerialNumber()) + "|" + normalizedPath;
	return true;
}

// Activates the slot set associated with the active region and city. This is
// also called from the deferred save callback in case city initialization
// completed before the regional path was available.
bool ActivateCurrentBookmarkCity()
{
	std::string cityKey;
	if (!GetActiveBookmarkCityKey(cityKey))
	{
		Logger::GetInstance().WriteLine(LogLevel::Warning, "Camera Bookmarks: active region/city identity was unavailable.");
		return false;
	}
	if (!g_CameraBookmarks.ActivateCity(cityKey))
	{
		Logger::GetInstance().WriteLine(LogLevel::Warning, "Camera Bookmarks: failed to activate persistent storage for the active city.");
		return false;
	}
	return true;
}

/// <summary>
/// Shows the outcome of a bookmark command unless a Modern Camera window is open.
/// </summary>
/// <param name="notice">Outcome to report to the player.</param>
/// <param name="slot">Zero-based bookmark slot. Ignored for storage failures.</param>
void ShowCameraBookmarkNotice(CameraBookmarkNotice notice, uint32_t slot)
{
	if (g_WindowManager.HasVisibleWindow())
	{
		return;
	}

	// SetCursorText can display the acknowledgement while returning false,
	// so its return value must not prevent the cleanup timer from starting.
	g_CameraController.ShowCameraBookmarkNotice(notice, slot);
	StartCameraBookmarkConfirmationTimer();
}

// Captures the active camera state and hands only serialized bookmark bytes to
// the store's background writer. No image or renderer-buffer capture is used.
bool SaveCameraBookmark(uint32_t slot)
{
	if (!g_IsCityLoaded || !g_IsModernCamEnabled || slot >= CameraBookmarkStore::kSlotCount)
	{
		return false;
	}
	if (!ActivateCurrentBookmarkCity())
	{
		ShowCameraBookmarkNotice(CameraBookmarkNotice::SaveFailed, slot);
		return false;
	}

	// A bookmark must store a real view, not a point on the way to one.
	g_CameraController.FinishClassicAngleTransition();

	SC4NativeCameraState view;
	if (!g_CameraController.CaptureCurrentCameraState(view, "bookmark save"))
	{
		Logger::GetInstance().WriteLine(LogLevel::Warning, "Camera Bookmark: failed to capture the current camera state.");
		ShowCameraBookmarkNotice(CameraBookmarkNotice::SaveFailed, slot);
		return false;
	}
	const bool saved = g_CameraBookmarks.Save(slot, view);
	Logger::GetInstance().WriteLine(
		saved ? LogLevel::Info : LogLevel::Warning,
		std::string("Camera Bookmark: ") + (saved ? "saved slot " : "failed to save slot ")
		+ std::to_string(slot + 1) + ".");
	ShowCameraBookmarkNotice(saved ? CameraBookmarkNotice::Saved : CameraBookmarkNotice::SaveFailed, slot);
	return saved;
}

// Restores only the copy held by the store; bounds correction therefore never
// rewrites the saved view when city dimensions have changed.
bool RecallCameraBookmark(uint32_t slot)
{
	if (!g_IsCityLoaded || !g_IsModernCamEnabled || slot >= CameraBookmarkStore::kSlotCount)
	{
		return false;
	}
	const std::optional<CameraBookmark> bookmark = g_CameraBookmarks.Get(slot);
	if (!bookmark)
	{
		ShowCameraBookmarkNotice(CameraBookmarkNotice::Empty, slot);
		return false;
	}
	const bool recalled = g_CameraController.RestoreCameraState(bookmark->view, "bookmark recall");
	ShowCameraBookmarkNotice(recalled ? CameraBookmarkNotice::Recalled : CameraBookmarkNotice::RecallFailed, slot);
	return recalled;
}

// Returns the diagnostic name for a key-state owner.
const char* GetKeyOwnerName(KeyOwner owner)
{
    switch (owner) {
    case KeyOwner::ModernCameraPan:
        return "ModernCameraPan";
    case KeyOwner::ModernCameraRotation:
        return "ModernCameraRotation";
    case KeyOwner::ModernCameraClassicRotation:
        return "ModernCameraClassicRotation";
    case KeyOwner::ModernCameraClassicAngle:
        return "ModernCameraClassicAngle";
    default:
        return "None";
    }
}

// Formats a virtual key with a readable name and numeric value.
std::string FormatVirtualKey(WPARAM key)
{
    switch (key) {
    case VK_CONTROL:
        return "Ctrl(" + std::to_string(static_cast<uint32_t>(key)) + ")";
    case VK_SHIFT:
        return "Shift(" + std::to_string(static_cast<uint32_t>(key)) + ")";
    case VK_MENU:
        return "Alt(" + std::to_string(static_cast<uint32_t>(key)) + ")";
    case VK_LMENU:
        return "LeftAlt(" + std::to_string(static_cast<uint32_t>(key)) + ")";
    case VK_RMENU:
        return "RightAlt(" + std::to_string(static_cast<uint32_t>(key)) + ")";
    case VK_LBUTTON:
        return "LButton(" + std::to_string(static_cast<uint32_t>(key)) + ")";
    case VK_RBUTTON:
        return "RButton(" + std::to_string(static_cast<uint32_t>(key)) + ")";
    case VK_MBUTTON:
        return "MButton(" + std::to_string(static_cast<uint32_t>(key)) + ")";
    case VK_RETURN:
        return "Enter(" + std::to_string(static_cast<uint32_t>(key)) + ")";
    case VK_ESCAPE:
        return "Escape(" + std::to_string(static_cast<uint32_t>(key)) + ")";
    case VK_UP:
        return "Up(" + std::to_string(static_cast<uint32_t>(key)) + ")";
    case VK_DOWN:
        return "Down(" + std::to_string(static_cast<uint32_t>(key)) + ")";
    case VK_LEFT:
        return "Left(" + std::to_string(static_cast<uint32_t>(key)) + ")";
    case VK_RIGHT:
        return "Right(" + std::to_string(static_cast<uint32_t>(key)) + ")";
    default:
        break;
    }

    if (key >= 'A' && key <= 'Z') {
        std::string text;
        text.push_back(static_cast<char>(key));
        text += "(" + std::to_string(static_cast<uint32_t>(key)) + ")";
        return text;
    }

    return "VK(" + std::to_string(static_cast<uint32_t>(key)) + ")";
}

// Resolves and caches the native canvas window handle.
HWND ResolveCanvasWindowHandle()
{
    if (g_CanvasWindow && IsWindow(g_CanvasWindow)) {
        return g_CanvasWindow;
    }

    cIGZGraphicSystemPtr pGraphicSystem;
    if (!pGraphicSystem) {
        return NULL;
    }

    cIGZCanvasW32* canvasW32 = nullptr;
    if (!pGraphicSystem->GetCanvas(GZIID_cIGZCanvasW32, reinterpret_cast<void**>(&canvasW32)) || !canvasW32) {
        return NULL;
    }

    HWND resolved = NULL;
    cIGZCanvas* canvas = canvasW32->AsIGZCanvas();
    const intptr_t identifier = canvas ? canvas->GetSystemCanvasIdentifier() : 0;
    if (identifier != 0) {
        HWND direct = reinterpret_cast<HWND>(identifier);
        if (IsWindow(direct)) {
            resolved = direct;
        }
        else {
            HWND* indirect = reinterpret_cast<HWND*>(identifier);
            if (indirect && IsWindow(*indirect)) {
                resolved = *indirect;
            }
        }
    }

    canvasW32->Release();
    if (resolved) {
        g_CanvasWindow = resolved;
    }
    return resolved;
}

// Activates and focuses the native canvas before sending native input.
bool FocusCanvasWindowForNativeInput(const char* context)
{
    HWND canvasWindow = ResolveCanvasWindowHandle();
    if (!canvasWindow) {
        Logger::GetInstance().WriteLine(
            LogLevel::Warning,
            std::string(context ? context : "native input")
            + ": failed to resolve canvas HWND for native input focus.");
        return false;
    }

    HWND previousFocus = GetFocus();
    HWND previousActive = GetActiveWindow();
    HWND rootWindow = GetAncestor(canvasWindow, GA_ROOT);
    if (!rootWindow) {
        rootWindow = canvasWindow;
    }

    HWND activeResult = SetActiveWindow(rootWindow);
    SetForegroundWindow(rootWindow);
    HWND focusResult = SetFocus(canvasWindow);
    const bool active = GetActiveWindow() == rootWindow || activeResult == rootWindow;
    const bool focused = GetFocus() == canvasWindow || focusResult == canvasWindow;

    Logger::GetInstance().WriteLine(
        focused ? LogLevel::Verbose : LogLevel::Warning,
        std::string(context ? context : "native input")
        + ": canvas HWND activation Active="
        + (active ? "true" : "false")
        + " Focus="
        + (focused ? "true" : "false")
        + " RootHWND:" + std::to_string(reinterpret_cast<intptr_t>(rootWindow))
        + " CanvasHWND:" + std::to_string(reinterpret_cast<intptr_t>(canvasWindow))
        + " PreviousActive:" + std::to_string(reinterpret_cast<intptr_t>(previousActive))
        + " PreviousFocus:" + std::to_string(reinterpret_cast<intptr_t>(previousFocus)));

    return focused;
}

// Creates a diagnostic description of a GZ window.
std::string DescribeGZWindow(cIGZWin* window)
{
    if (!window) {
        return "none";
    }

    std::ostringstream stream;
    stream << "ID:" << FormatWindowID(window->GetID())
        << " Instance:" << FormatWindowID(window->GetInstanceID())
        << " Area:[" << window->GetL() << "," << window->GetT()
        << "," << window->GetR() << "," << window->GetB() << "]"
        << " Visible:" << (window->IsVisible() ? "true" : "false")
        << " Enabled:" << (window->IsEnabled() ? "true" : "false");
    return stream.str();
}

// Retrieves the 3D view window used for native camera input.
cISC4View3DWin* GetView3DWinForInput(const char* context)
{
    cISC4AppPtr app;
    if (!app) {
        Logger::GetInstance().WriteLine(
            LogLevel::Warning,
            std::string(context) + ": failed to get cISC4App while handling native view input.");
        return nullptr;
    }

    cIGZWin* mainWindow = app->GetMainWindow();
    if (!mainWindow) {
        Logger::GetInstance().WriteLine(
            LogLevel::Warning,
            std::string(context) + ": failed to get main window while handling native view input.");
        return nullptr;
    }

    cIGZWin* parentWindow = mainWindow->GetChildWindowFromID(kGZWin_WinSC4App);
    if (!parentWindow) {
        Logger::GetInstance().WriteLine(
            LogLevel::Warning,
            std::string(context) + ": failed to get WinSC4App while handling native view input.");
        return nullptr;
    }

    cISC4View3DWin* view3D = nullptr;
    if (!parentWindow->GetChildAs(
        kGZWin_SC4View3DWin,
        kGZIID_cISC4View3DWin,
        reinterpret_cast<void**>(&view3D)) || !view3D) {
        Logger::GetInstance().WriteLine(
            LogLevel::Warning,
            std::string(context) + ": failed to get cISC4View3DWin while handling native view input.");
        return nullptr;
    }

    return view3D;
}

/// <summary>
/// Returns whether Modern Camera owns player input, which it gives up in Classic mode
/// and for the length of a U-Drive-It drive.
/// </summary>
/// <returns>True when Modern Camera may capture keyboard and mouse input.</returns>
bool IsModernInputActive()
{
    return g_IsModernCamEnabled && !g_IsUDriveItActive;
}

// Returns a diagnostic label for a native view-tool reset.
const char* GetView3DToolClearReasonName(View3DToolClearReason reason)
{
    switch (reason) {
    case View3DToolClearReason::ModernCameraEnabled:
        return "Modern camera enabled";
    case View3DToolClearReason::WASDMovementChanged:
        return "WASD movement changed";
    case View3DToolClearReason::ClassicCameraEnabled:
        return "Classic camera enabled";
    default:
        return "input mode changed";
    }
}

// Clears the active native view tool and its inherited scrolling state.
void ClearNativeView3DToolState(View3DToolClearReason reason)
{
    if (!g_IsCityLoaded) {
        return;
    }

    const char* reasonName = GetView3DToolClearReasonName(reason);
    cISC4View3DWin* view3D = GetView3DWinForInput(reasonName);
    if (!view3D) {
        return;
    }

    cISC4ViewInputControl* currentControl = view3D->GetCurrentViewInputControl();
    // The vehicle is driven through this tool, so removing it would end the drive.
    if (currentControl && currentControl->GetID() == kViewInputControlUDriveIt) {
        Logger::GetInstance().WriteLine(
            LogLevel::Info,
            std::string(reasonName) + ": keeping the U-Drive-It tool for the current drive.");
        view3D->Release();
        return;
    }

    if (currentControl) {
        Logger::GetInstance().WriteLine(
            LogLevel::Info,
            std::string(reasonName)
            + ": clearing current native view tool ID:"
            + FormatWindowID(currentControl->GetID()));
    }
    else {
        Logger::GetInstance().WriteLine(
            LogLevel::Verbose,
            std::string(reasonName) + ": no current native view tool to clear.");
    }

    // This is the call SC4's own Cancel command makes. Passing true would remove
    // the tools without resetting the cursor, leaving the old tool's cursor showing.
    view3D->RemoveAllViewInputControls(false);
    const bool stoppedScrolling = view3D->ScrollStop();
    const bool killedKeyboardScrolling = view3D->KillKeyboardScrolling();
    cISC4ViewInputControl* remainingControl = view3D->GetCurrentViewInputControl();
    Logger::GetInstance().WriteLine(
        LogLevel::Info,
        std::string(reasonName)
        + ": native view input reset. ScrollStop="
        + (stoppedScrolling ? "true" : "false")
        + " KillKeyboardScrolling="
        + (killedKeyboardScrolling ? "true" : "false")
        + " RemainingToolID:"
        + (remainingControl ? FormatWindowID(remainingControl->GetID()) : "none"));

    view3D->Release();
}

// Stops the pending 3D-view focus restoration timer.
void KillModernCameraFocusRestoreTimer()
{
    g_TimerManager.Stop(RuntimeTimer::ModernCameraFocusRestore);
}

// Schedules focus to return to the 3D view after a UI transition.
void ScheduleView3DFocusRestore(bool requireModern, const char* context)
{
    KillModernCameraFocusRestoreTimer();
    if (!g_IsCityLoaded || (requireModern && !g_IsModernCamEnabled)) {
        return;
    }

    g_View3DFocusRestoreRequiresModern = requireModern;
    g_View3DFocusRestoreContext = context ? context : "View3D focus restore";
    if (!g_TimerManager.Start(
        RuntimeTimer::ModernCameraFocusRestore,
        kModernCameraFocusRestoreDelayMs,
        ModernCameraFocusRestoreTimerProc)) {
        Logger::GetInstance().WriteLine(LogLevel::Warning, "Failed to start 3D view focus restore timer.");
    }
}

// Schedules a Modern Camera-only 3D-view focus restoration.
void ScheduleModernCameraFocusRestore()
{
    ScheduleView3DFocusRestore(true, "Modern camera focus restore");
}

// Returns whether a hit-test candidate is more specific than container windows.
bool IsUsefulHitTestTarget(cIGZWin* candidate, cIGZWin* mainWindow, cIGZWin* parentWindow)
{
    return candidate && candidate != mainWindow && candidate != parentWindow;
}

// Determines whether a mouse-wheel event targets the 3D view or native UI.
NativeWheelHitTest HitTestNativeWheelTarget(int32_t parentX, int32_t parentY)
{
    NativeWheelHitTest result;

    cISC4AppPtr app;
    if (!app) {
        result.targetDescription = "no cISC4App";
        return result;
    }

    cIGZWin* mainWindow = app->GetMainWindow();
    if (!mainWindow) {
        result.targetDescription = "no main window";
        return result;
    }

    cIGZWin* parentWindow = mainWindow->GetChildWindowFromID(kGZWin_WinSC4App);
    if (!parentWindow) {
        result.targetDescription = "no WinSC4App";
        return result;
    }

    cISC4View3DWin* view3D = nullptr;
    if (!parentWindow->GetChildAs(
        kGZWin_SC4View3DWin,
        kGZIID_cISC4View3DWin,
        reinterpret_cast<void**>(&view3D))) {
        result.targetDescription = "no SC4View3DWin";
        return result;
    }

    cIGZWin* viewWindow = view3D->AsIGZWin();
    cIGZWin* globalTarget = nullptr;
    cIGZWinMgrPtr winMgr;
    if (winMgr) {
        globalTarget = winMgr->GetWindowFromPoint(parentX, parentY);
    }

    cIGZWin* mainTarget = mainWindow->GetWindowFromPoint(parentX, parentY);
    cIGZWin* parentTarget = parentWindow->GetWindowFromPoint(parentX, parentY);
    cIGZWin* targetWindow = nullptr;

    if (IsUsefulHitTestTarget(globalTarget, mainWindow, parentWindow)) {
        targetWindow = globalTarget;
        result.targetSource = "global";
    }
    else if (IsUsefulHitTestTarget(mainTarget, mainWindow, parentWindow)) {
        targetWindow = mainTarget;
        result.targetSource = "main";
    }
    else if (parentTarget) {
        targetWindow = parentTarget;
        result.targetSource = "parent";
    }
    else if (globalTarget) {
        targetWindow = globalTarget;
        result.targetSource = "global-fallback";
    }
    else if (mainTarget) {
        targetWindow = mainTarget;
        result.targetSource = "main-fallback";
    }
    else {
        result.targetSource = "none";
    }

    result.targetIsView3D = targetWindow && viewWindow && targetWindow == viewWindow;
    result.targetIsView3DSurface = targetWindow && targetWindow->GetID() == kGZWin_SC4View3DSurface;
    result.insideView3DRect = viewWindow
        && viewWindow->IsPointInWindowParentCoordinates(parentX, parentY);

    result.resolved = viewWindow != nullptr;
    result.overView3D = result.targetIsView3D
        || result.targetIsView3DSurface
        || (!targetWindow && result.insideView3DRect);
    if (Logger::GetInstance().IsEnabled(LogLevel::Verbose)) {
        result.targetDescription = DescribeGZWindow(targetWindow);
        result.globalTargetDescription = DescribeGZWindow(globalTarget);
        result.mainTargetDescription = DescribeGZWindow(mainTarget);
        result.parentTargetDescription = DescribeGZWindow(parentTarget);
        result.viewDescription = DescribeGZWindow(viewWindow);
    }

    view3D->Release();
    return result;
}

// Stops the one-shot idle redraw timer.
void KillIdleTimer()
{
    g_TimerManager.Stop(RuntimeTimer::IdleRedraw);
}

// Returns whether the key can be tracked by the shared key-state manager.
bool IsTrackableKey(WPARAM key)
{
    return g_KeyStateManager.IsTrackable(key);
}

// Reads a key's current physical state through the key-state manager.
bool IsPhysicalKeyDown(int key)
{
    return g_KeyStateManager.IsPhysicalDown(key);
}

// Records a key or mouse-button transition in the shared state manager.
void ObserveKeyTransition(WPARAM key, bool down)
{
    g_KeyStateManager.Observe(key, down);
}

// Returns the recorded state for a virtual key.
bool IsTrackedKeyDown(int key)
{
    return g_KeyStateManager.IsDown(key);
}

// Returns a key state, using the physical state when tracking missed an event.
bool IsObservedOrPhysicalKeyDown(int key)
{
    return g_KeyStateManager.IsDownWithFallback(key);
}

// Refreshes tracked modifier keys from the operating system.
void RefreshTrackedModifierStates()
{
    g_KeyStateManager.RefreshModifiers();
}

// Claims a key for a camera feature and records the ownership transition.
bool ClaimKey(WPARAM key, KeyOwner owner)
{
    const KeyOwner previousOwner = g_KeyStateManager.GetOwner(key);
    const bool claimed = g_KeyStateManager.Claim(key, owner);
    if (claimed && previousOwner != owner) {
        Logger::GetInstance().WriteLine(
            LogLevel::Verbose,
            "Key State: claimed key. Key:" + FormatVirtualKey(key)
            + " Owner:" + GetKeyOwnerName(owner));
    }
    else if (!claimed) {
        Logger::GetInstance().WriteLine(
            LogLevel::Verbose,
            "Key State: claim denied. Key:" + FormatVirtualKey(key)
            + " RequestedOwner:" + GetKeyOwnerName(owner)
            + " CurrentOwner:" + GetKeyOwnerName(previousOwner));
    }
    return claimed;
}

// Releases a camera feature's key claim and records the transition.
void ReleaseKey(WPARAM key, KeyOwner owner)
{
    const KeyOwner previousOwner = g_KeyStateManager.GetOwner(key);
    g_KeyStateManager.Release(key, owner);
    if (previousOwner == owner) {
        Logger::GetInstance().WriteLine(
            LogLevel::Verbose,
            "Key State: released key. Key:" + FormatVirtualKey(key)
            + " Owner:" + GetKeyOwnerName(owner));
    }
}

// Returns whether a camera feature owns the supplied key.
bool IsKeyOwnedBy(WPARAM key, KeyOwner owner)
{
    return g_KeyStateManager.IsOwnedBy(key, owner);
}

// Returns whether Modern Camera owns and currently holds a pan key.
bool IsKeyboardPanKeyDown(WPARAM key)
{
    return IsKeyOwnedBy(key, KeyOwner::ModernCameraPan)
        && IsTrackedKeyDown(static_cast<int>(key));
}

// Returns whether Q or E can request keyboard-controlled camera rotation.
bool IsKeyboardRotationVirtualKey(WPARAM key)
{
    return key == 'Q' || key == 'E';
}

// Returns whether Modern Camera owns and currently holds a Q/E rotation key.
bool IsKeyboardRotationKeyDown(WPARAM key)
{
    return IsKeyOwnedBy(key, KeyOwner::ModernCameraRotation)
        && IsTrackedKeyDown(static_cast<int>(key));
}

// Returns whether a claimed keyboard-pan key is still held.
bool HasHeldKeyboardPanKey()
{
    return g_KeyStateManager.AnyDownOwnedBy(kKeyboardPanKeys, KeyOwner::ModernCameraPan);
}

// Returns whether Modern Camera owns any keyboard-pan key.
bool HasOwnedKeyboardPanKey()
{
    return g_KeyStateManager.AnyOwnedBy(kKeyboardPanKeys, KeyOwner::ModernCameraPan);
}

// Returns whether a claimed WASD movement key is held.
bool HasHeldWASDKeyboardPanKey()
{
    return g_KeyStateManager.AnyDownOwnedBy(kWASDKeyboardPanKeys, KeyOwner::ModernCameraPan);
}

// Returns whether a claimed arrow-key movement key is held.
bool HasHeldArrowKeyboardPanKey()
{
    return g_KeyStateManager.AnyDownOwnedBy(kArrowKeyboardPanKeys, KeyOwner::ModernCameraPan);
}

// Returns whether a continuous Q/E rotation key is still held.
bool HasHeldKeyboardRotationKey()
{
    return g_KeyStateManager.AnyDownOwnedBy(kKeyboardRotationKeys, KeyOwner::ModernCameraRotation);
}

// Returns whether Modern Camera owns Q or E for continuous rotation or an Alt step.
bool HasOwnedKeyboardRotationKey()
{
    return g_KeyStateManager.AnyOwnedBy(kKeyboardRotationKeys, KeyOwner::ModernCameraRotation)
        || g_KeyStateManager.AnyOwnedBy(kKeyboardRotationKeys, KeyOwner::ModernCameraClassicRotation);
}

// Returns whether Modern Camera currently owns middle-mouse rotation.
bool IsMiddleMouseRotationActive()
{
    return IsKeyOwnedBy(VK_MBUTTON, KeyOwner::ModernCameraRotation);
}

// Returns whether a key represents the generic, left, or right Alt virtual key.
bool IsClassicAngleVirtualKey(WPARAM key)
{
    return key == VK_MENU || key == VK_LMENU || key == VK_RMENU;
}

// Returns whether a claimed Alt key is held for classic-angle mode.
bool HasHeldClassicAngleKey()
{
    return g_KeyStateManager.AnyDownOwnedBy(kClassicAngleKeys, KeyOwner::ModernCameraClassicAngle);
}

// Returns whether Modern Camera owns an Alt key for classic-angle mode.
bool HasOwnedClassicAngleKey()
{
    return g_KeyStateManager.AnyOwnedBy(kClassicAngleKeys, KeyOwner::ModernCameraClassicAngle);
}

// Decides whether an Alt key should control classic-angle mode.
bool ShouldCaptureClassicAngleKeyInFocus(WPARAM key, bool cameraFocus)
{
    RefreshTrackedModifierStates();
    return g_IsCityLoaded
        && g_IsModernCamEnabled
        && IsClassicAngleVirtualKey(key)
        && cameraFocus
        && !IsObservedOrPhysicalKeyDown(VK_CONTROL)
        && !IsObservedOrPhysicalKeyDown(VK_LWIN)
        && !IsObservedOrPhysicalKeyDown(VK_RWIN);
}

// Releases every keyboard-pan claim held by Modern Camera.
void ReleaseKeyboardPanKeyClaims()
{
    g_KeyStateManager.ReleaseAll(kKeyboardPanKeys, KeyOwner::ModernCameraPan);
}

// Releases all Q/E claims used for continuous rotation or Alt+Q/E classic steps.
void ReleaseKeyboardRotationKeyClaims()
{
    g_KeyStateManager.ReleaseAll(kKeyboardRotationKeys, KeyOwner::ModernCameraRotation);
    g_KeyStateManager.ReleaseAll(kKeyboardRotationKeys, KeyOwner::ModernCameraClassicRotation);
}

// Releases every classic-angle Alt-key claim held by Modern Camera.
void ReleaseClassicAngleKeyClaims()
{
    g_KeyStateManager.ReleaseAll(kClassicAngleKeys, KeyOwner::ModernCameraClassicAngle);
}

// Clears Modern Camera ownership of keyboard-pan keys.
void ClearHeldKeyboardPanKeys()
{
    ReleaseKeyboardPanKeyClaims();
}

// Clears Modern Camera ownership of Q/E rotation keys.
void ClearHeldKeyboardRotationKeys()
{
    ReleaseKeyboardRotationKeyClaims();
}

// Clears Modern Camera ownership of classic-angle keys.
void ClearClassicAngleKeys()
{
    ReleaseClassicAngleKeyClaims();
}

// Releases Modern Camera ownership of the middle mouse button.
void ClearMiddleMouseRotationClaim()
{
    ReleaseKey(VK_MBUTTON, KeyOwner::ModernCameraRotation);
}

// Clears all tracked input state and logs the reason.
void ClearTrackedKeyStates(const char* reason)
{
    g_KeyStateManager.Clear();

    Logger::GetInstance().WriteLine(
        LogLevel::Verbose,
        std::string("Keyboard state tracker cleared. Reason:")
        + (reason ? reason : "unknown"));
}

// Starts a delayed redraw after camera interaction becomes idle. Classic mode leaves redraws to the game.
void StartIdleTimer(UINT delayMs)
{
    KillIdleTimer();

    if (!g_IsCityLoaded || !g_IsModernCamEnabled) {
        return;
    }

    Logger::GetInstance().WriteLineLazy(LogLevel::Verbose, [&]() {
        return "Starting idle redraw timer: delayMs=" + std::to_string(delayMs);
    });
    if (!g_TimerManager.Start(RuntimeTimer::IdleRedraw, delayMs, RedrawTimerProc)) {
        Logger::GetInstance().WriteLine(LogLevel::Warning, "Failed to start the redraw timer.");
    }
}

// Shows or hides the native cursor while middle-mouse camera rotation owns it.
void SetRotationCursorVisible(bool visible)
{
    const bool hidden = !visible;
    if (g_RotationCursorHidden == hidden) {
        return;
    }

    // GZShowCursor returns the game's new hide count, not success, and that count is
    // zero on a normal show, so only a missing window manager counts as a failure.
    cIGZWinMgrPtr winMgr;
    if (!winMgr) {
        Logger::GetInstance().WriteLine(
            LogLevel::Warning,
            std::string("Failed to ") + (visible ? "show" : "hide")
            + " the cursor for middle mouse camera rotation.");
        return;
    }

    winMgr->GZShowCursor(visible);
    g_RotationCursorHidden = hidden;
}

/// <summary>
/// Applies the mouse rotation accumulated since the previous camera frame.
/// </summary>
void ApplyPendingMouseRotation()
{
    if (g_PendingMouseYawDelta == 0.0f && g_PendingMousePitchDelta == 0.0f) {
        return;
    }

    const float yawDelta = g_PendingMouseYawDelta;
    const float pitchDelta = g_PendingMousePitchDelta;
    g_PendingMouseYawDelta = 0.0f;
    g_PendingMousePitchDelta = 0.0f;
    g_CameraController.ApplyDelta(pitchDelta, yawDelta, true);
}

/// <summary>
/// Moves the hidden cursor back to the view center before it can stop at a screen edge.
/// </summary>
/// <param name="window">Canvas window whose client coordinates the cursor position uses.</param>
void RecenterRotationCursor(HWND window)
{
    RECT client{};
    if (!GetClientRect(window, &client)) {
        return;
    }

    const POINT center = { (client.left + client.right) / 2, (client.top + client.bottom) / 2 };
    const int limitX = (client.right - client.left) / kRotationCursorRecenterDivisor;
    const int limitY = (client.bottom - client.top) / kRotationCursorRecenterDivisor;
    if (std::abs(g_LastMousePos.x - center.x) <= limitX && std::abs(g_LastMousePos.y - center.y) <= limitY) {
        return;
    }

    POINT screenCenter = center;
    if (ClientToScreen(window, &screenCenter) && SetCursorPos(screenCenter.x, screenCenter.y)) {
        // The move this generates arrives with a zero delta from the new position.
        g_LastMousePos = center;
        g_RotationCursorWarped = true;
    }
}

// Ends middle-mouse rotation, releases capture, and optionally schedules redraw.
void StopMiddleMouseRotation(const char* reason, bool scheduleRedraw)
{
    const bool wasActive = IsMiddleMouseRotationActive();
    const HWND capturedWindow = g_CapturedMouseWindow;
    const bool ownsCapture = capturedWindow != NULL && GetCapture() == capturedWindow;

    // ReleaseCapture re-enters the canvas filter with WM_CAPTURECHANGED, which must
    // see the gesture as already finished.
    ClearMiddleMouseRotationClaim();
    g_CapturedMouseWindow = NULL;
    if (ownsCapture && scheduleRedraw && g_RotationCursorWarped) {
        POINT screenOrigin = g_RotationCursorOrigin;
        if (ClientToScreen(capturedWindow, &screenOrigin)) {
            SetCursorPos(screenOrigin.x, screenOrigin.y);
        }
    }
    g_RotationCursorWarped = false;
    if (ownsCapture) {
        ReleaseCapture();
    }
    SetRotationCursorVisible(true);

    if (!wasActive) {
        return;
    }

    if (scheduleRedraw) {
        ApplyPendingMouseRotation();
    }
    g_PendingMouseYawDelta = 0.0f;
    g_PendingMousePitchDelta = 0.0f;

    if (!g_KeyboardRotationGestureActive) {
        g_CameraController.EndRotationGesture();
    }
    Logger::GetInstance().WriteLine(
        LogLevel::Verbose,
        std::string("Middle mouse camera rotation stopped. Reason:")
        + (reason ? reason : "unknown"));

    if (scheduleRedraw) {
        Logger::GetInstance().WriteLine(LogLevel::Verbose, "Pan stopped: scheduling redraw sequence.");
        StartIdleTimer(kPanIdleRedrawDelayMs);
    }
}

// Stops the temporary camera-dump confirmation timer.
void KillCameraDumpConfirmationTimer()
{
    g_TimerManager.Stop(RuntimeTimer::CameraDumpConfirmation);
}

// Starts the timer that clears the camera-dump confirmation.
void StartCameraDumpConfirmationTimer()
{
    KillCameraDumpConfirmationTimer();
    g_TimerManager.Start(
        RuntimeTimer::CameraDumpConfirmation,
        kCameraDumpConfirmationDelayMs,
        ClearCameraDumpConfirmationTimerProc);
}

// Stops the temporary bookmark confirmation timer.
void KillCameraBookmarkConfirmationTimer()
{
    g_TimerManager.Stop(RuntimeTimer::BookmarkConfirmation);
}

// Starts the timer that clears the bookmark confirmation.
void StartCameraBookmarkConfirmationTimer()
{
    KillCameraBookmarkConfirmationTimer();
    g_TimerManager.Start(
        RuntimeTimer::BookmarkConfirmation,
        kBookmarkConfirmationDelayMs,
        ClearCameraBookmarkConfirmationTimerProc);
}

// Returns focus to the 3D view when the scheduled restoration fires.
VOID CALLBACK ModernCameraFocusRestoreTimerProc(HWND, UINT, UINT_PTR idEvent, DWORD)
{
    if (g_TimerManager.IsCurrent(RuntimeTimer::ModernCameraFocusRestore, idEvent)) {
        KillModernCameraFocusRestoreTimer();
        if (g_IsCityLoaded && (!g_View3DFocusRestoreRequiresModern || g_IsModernCamEnabled)) {
            FocusView3DForNativeInput(g_View3DFocusRestoreContext);
        }
    }
}

// Requests a full city redraw while Modern Camera is active.
void TriggerCityRedraw() {
    Logger& log = Logger::GetInstance();

    if (!g_IsCityLoaded || !g_IsModernCamEnabled) {
        return;
    }

    log.WriteLine(LogLevel::Verbose, "Executing forced redraw.");

    if (!g_CameraController.ForceFullRedraw()) {
        log.WriteLine(LogLevel::Warning, "ForceFullRedraw() failed.");
    }
}

// Converts mouse-message coordinates into a POINT.
POINT MakePointFromLParam(LPARAM lParam)
{
    POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    return point;
}

// Logs a canvas mouse-button event and its coordinates.
void LogMouseButtonEvent(const char* name, const POINT& point)
{
    Logger::GetInstance().WriteLine(
        LogLevel::Verbose,
        std::string("Canvas WinProc Filter: ") + name + " at X:" + std::to_string(point.x) + " Y:" + std::to_string(point.y));
}

// Returns whether a click falls in the native-UI toggle probe area.
bool IsNativeUICornerProbeClick(const POINT& point)
{
    cISC4AppPtr app;
    cIGZWin* mainWindow = app ? app->GetMainWindow() : nullptr;
    cIGZWin* parentWindow = mainWindow ? mainWindow->GetChildWindowFromID(kGZWin_WinSC4App) : nullptr;
    if (!parentWindow) {
        return false;
    }

    const int32_t bottomOffset = parentWindow->GetH() - point.y;
    return point.x >= 0
        && point.x < kNativeUICornerProbeMaxX
        && bottomOffset >= 0
        && bottomOffset < kNativeUICornerProbeMaxBottomOffset;
}

// Mirrors native-UI visibility to the Modern Camera menu button.
void SyncMenuButtonToNativeUI(bool nativeUILooksVisible, const char* reason)
{
    g_IsNativeUIHidden = !nativeUILooksVisible;
    g_WindowManager.SetMenuButtonVisible(nativeUILooksVisible);
    Logger::GetInstance().WriteLine(
        LogLevel::Info,
        std::string("Menu Button UI: ") + reason
        + " NativeUI:" + (nativeUILooksVisible ? "UIVisible" : "UIHidden")
        + " CameraButton:" + (nativeUILooksVisible ? "showing" : "hiding"));
}

// Retrieves the 3D view used to install native UI hooks.
cISC4View3DWin* GetView3DWinForNativeUIHook()
{
    cISC4AppPtr app;
    cIGZWin* mainWindow = app ? app->GetMainWindow() : nullptr;
    cIGZWin* parentWindow = mainWindow ? mainWindow->GetChildWindowFromID(kGZWin_WinSC4App) : nullptr;
    if (!parentWindow) {
        return nullptr;
    }

    cISC4View3DWin* view3D = nullptr;
    if (!parentWindow->GetChildAs(
        kGZWin_SC4View3DWin,
        kGZIID_cISC4View3DWin,
        reinterpret_cast<void**>(&view3D))) {
        return nullptr;
    }

    return view3D;
}

// Formats a pointer value as hexadecimal for hook diagnostics.
std::string FormatPointerValue(const void* value)
{
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << reinterpret_cast<uintptr_t>(value);
    return stream.str();
}

// Installs the hook that tracks native UI minimization.
void InstallMinimizeUIHook()
{
    if (g_MinimizeUIHook.IsInstalled()) {
        return;
    }

    cISC4View3DWin* view3D = GetView3DWinForNativeUIHook();
    if (!view3D) {
        Logger::GetInstance().WriteLine(LogLevel::Warning, "Native UI MinimizeUI hook: could not find cISC4View3DWin.");
        return;
    }

    void** vtable = *reinterpret_cast<void***>(view3D);
    if (g_MinimizeUIHook.Install(
        vtable,
        kView3DWinMinimizeUIVTableIndex,
        reinterpret_cast<void*>(&HookedMinimizeUI))) {
        Logger::GetInstance().WriteLine(
            LogLevel::Info,
            std::string("Native UI MinimizeUI hook installed. VTable:")
            + FormatPointerValue(vtable)
            + " Slot:" + std::to_string(kView3DWinMinimizeUIVTableIndex)
            + " Original:" + FormatPointerValue(g_MinimizeUIHook.GetOriginal()));
    }
    else {
        Logger::GetInstance().WriteLine(LogLevel::Error, "Native UI MinimizeUI hook: failed to patch vtable slot.");
    }

    view3D->Release();
}

// Installs the hook that constrains native scrolling to city bounds.
void InstallSetScrollingHook()
{
    if (g_SetScrollingHook.IsInstalled()) {
        return;
    }

    cISC4View3DWin* view3D = GetView3DWinForNativeUIHook();
    if (!view3D) {
        Logger::GetInstance().WriteLine(LogLevel::Warning, "View3D SetScrolling hook: could not find cISC4View3DWin.");
        return;
    }

    void** vtable = *reinterpret_cast<void***>(view3D);
    if (g_SetScrollingHook.Install(
        vtable,
        kView3DWinSetScrollingVTableIndex,
        reinterpret_cast<void*>(&HookedSetScrolling))) {
        Logger::GetInstance().WriteLine(
            LogLevel::Info,
            std::string("View3D SetScrolling hook installed. VTable:")
            + FormatPointerValue(vtable)
            + " Slot:" + std::to_string(kView3DWinSetScrollingVTableIndex)
            + " Original:" + FormatPointerValue(g_SetScrollingHook.GetOriginal()));
    }
    else {
        Logger::GetInstance().WriteLine(LogLevel::Error, "View3D SetScrolling hook: failed to patch vtable slot.");
    }

    view3D->Release();
}

// Restores the original native UI minimization vtable entry.
void UninstallMinimizeUIHook()
{
    if (!g_MinimizeUIHook.IsInstalled()) {
        return;
    }

    if (g_MinimizeUIHook.Uninstall()) {
        Logger::GetInstance().WriteLine(LogLevel::Info, "Native UI MinimizeUI hook uninstalled.");
    }
    else {
        Logger::GetInstance().WriteLine(LogLevel::Warning, "Native UI MinimizeUI hook: slot changed before uninstall.");
    }
}

// Restores the original native scrolling vtable entry.
void UninstallSetScrollingHook()
{
    if (!g_SetScrollingHook.IsInstalled()) {
        return;
    }

    if (g_SetScrollingHook.Uninstall()) {
        Logger::GetInstance().WriteLine(LogLevel::Info, "View3D SetScrolling hook uninstalled.");
    }
    else {
        Logger::GetInstance().WriteLine(LogLevel::Warning, "View3D SetScrolling hook: slot changed before uninstall.");
    }
}

// Invokes the director callback that registers the canvas message filter.
void RegisterCanvasWinProcFilterForModernCamera()
{
    if (g_RegisterCanvasWinProcFilterCallback) {
        g_RegisterCanvasWinProcFilterCallback();
    }
    else {
        Logger::GetInstance().WriteLine(LogLevel::Warning, "Canvas WinProc filter register callback unavailable.");
    }
}

// Invokes the director callback that unregisters the canvas message filter.
void UnregisterCanvasWinProcFilterForModernCamera()
{
    if (g_UnregisterCanvasWinProcFilterCallback) {
        g_UnregisterCanvasWinProcFilterCallback();
    }
    else {
        Logger::GetInstance().WriteLine(LogLevel::Warning, "Canvas WinProc filter unregister callback unavailable.");
    }
}

// Mirrors native UI minimization changes to the Modern Camera menu button.
bool __fastcall HookedMinimizeUI(cISC4View3DWin* view3D, void* edx, bool minimize)
{
    (void)edx;

    MinimizeUIFunction original = reinterpret_cast<MinimizeUIFunction>(g_MinimizeUIHook.GetOriginal());
    if (!original) {
        Logger::GetInstance().WriteLine(LogLevel::Error, "Native UI MinimizeUI hook: original function pointer was null.");
        return false;
    }

    const bool previousHidden = g_IsNativeUIHidden;
    const bool result = original(view3D, minimize);

    if (g_IsCityLoaded) {
        SyncMenuButtonToNativeUI(
            !minimize,
            minimize
            ? "native MinimizeUI(true) observed; hiding camera settings button."
            : "native MinimizeUI(false) observed; showing camera settings button.");
    }
    else {
        g_IsNativeUIHidden = minimize;
    }

    Logger::GetInstance().WriteLine(
        LogLevel::Info,
        std::string("Native UI MinimizeUI hook: Minimize:")
        + (minimize ? "true" : "false")
        + " PreviousTrackedState:" + (previousHidden ? "UIHidden" : "UIVisible")
        + " NewTrackedState:" + (g_IsNativeUIHidden ? "UIHidden" : "UIVisible")
        + " StateChanged:" + (previousHidden != g_IsNativeUIHidden ? "true" : "false")
        + " Result:" + (result ? "true" : "false"));

    return result;
}

// Enforces city bounds before forwarding native camera scrolling.
void __fastcall HookedSetScrolling(cISC4View3DWin* view3D, void* edx, bool scrolling, float x, float z)
{
    (void)edx;

    SetScrollingFunction original = reinterpret_cast<SetScrollingFunction>(g_SetScrollingHook.GetOriginal());
    if (!original) {
        Logger::GetInstance().WriteLine(LogLevel::Error, "View3D SetScrolling hook: original function pointer was null.");
        return;
    }

    const char* source = g_IsApplyingKeyboardPan
        ? "keyboard SetScrolling"
        : (IsRightClickScrollingActive() ? "right mouse SetScrolling" : "native SetScrolling");
    float adjustedX = x;
    bool blockedByBounds = false;
    if (g_IsCityLoaded && IsModernInputActive() && scrolling) {
        auto* renderer = view3D ? view3D->GetRenderer() : nullptr;
        const bool boundsChecked = g_CameraController.AdjustScrollForCityBounds(
            renderer,
            x,
            z,
            adjustedX,
            blockedByBounds,
            source);
        if (!boundsChecked || blockedByBounds) {
            original(view3D, false, 0.0f, 0.0f);
            const LogLevel logLevel = boundsChecked ? LogLevel::Verbose : LogLevel::Warning;
            Logger::GetInstance().WriteLineLazy(logLevel, [&]() {
                return std::string(boundsChecked
                    ? "View3D SetScrolling blocked at city bounds. Source:"
                    : "View3D SetScrolling bounds preflight failed; stopping scroll. Source:")
                + source
                + " X:" + std::to_string(x)
                + " Z:" + std::to_string(z);
            });
            return;
        }
    }

    original(view3D, scrolling, adjustedX, z);
    Logger::GetInstance().WriteLineLazy(LogLevel::Verbose, [&]() {
        return std::string("View3D SetScrolling observed. Source:")
        + (g_IsApplyingKeyboardPan ? "ModernCameraKeyboard" : "NativeOrOther")
        + " Scrolling:" + (scrolling ? "true" : "false")
        + " X:" + std::to_string(x)
        + " AdjustedX:" + std::to_string(adjustedX)
        + " Z:" + std::to_string(z);
    });
}

// Records native-UI toggle-zone clicks while the hook observes the result.
void HandleNativeUICornerClick(const POINT& point)
{
    if (!IsNativeUICornerProbeClick(point)) {
        return;
    }

    Logger::GetInstance().WriteLine(
        LogLevel::Info,
        std::string("Native UI trigger zone click observed; waiting for native MinimizeUI(bool) state change. ")
        + "ClickX:" + std::to_string(point.x)
        + " ClickY:" + std::to_string(point.y)
        + " CurrentTrackedState:" + (g_IsNativeUIHidden ? "UIHidden" : "UIVisible")
        + " HookInstalled:" + (g_MinimizeUIHook.IsInstalled() ? "true" : "false"));
}

// Stops active input, timers, and optional camera state during a reset.
void ResetInputState(bool resetCamera = true)
{
    KillIdleTimer();
    StopHeldKeyboardPan();
    StopHeldKeyboardRotation("input reset");
    StopClassicAngleHold("input reset");
    StopMiddleMouseRotation("input reset", false);
    ClearTrackedKeyStates("input reset");
    KillCameraDumpConfirmationTimer();
	KillCameraBookmarkConfirmationTimer();
    KillModernCameraFocusRestoreTimer();
    g_CameraController.ClearCameraDumpConfirmation();
	g_CameraController.ClearCameraBookmarkConfirmation();

    if (resetCamera) {
        g_CameraController.Reset();
    }
}

// Resets Modern Camera and returns the view to its native state.
void ResetCameraToNativeView()
{
    Logger::GetInstance().WriteLine(LogLevel::Info, "Settings UI: reset camera requested.");
    KillIdleTimer();
    StopHeldKeyboardPan();
    StopHeldKeyboardRotation("camera reset");
    StopClassicAngleHold("camera reset");
    StopMiddleMouseRotation("camera reset", false);
    ClearTrackedKeyStates("camera reset");
    KillCameraDumpConfirmationTimer();
	KillCameraBookmarkConfirmationTimer();
    KillModernCameraFocusRestoreTimer();

    if (g_CameraController.IsSavePreviewNormalizationActive()) {
        g_CameraController.AbandonSavePreviewNormalization();
    }
    if (g_CameraController.BeginSavePreviewNormalization()) {
        g_CameraController.AbandonSavePreviewNormalization();
    }
    g_CameraController.Reset();
}

// Switches between Modern Camera and the native Classic camera mode.
void ApplyModernCameraEnabled(bool enabled)
{
    if (g_IsModernCamEnabled == enabled) {
        return;
    }

    Logger::GetInstance().WriteLine(
        LogLevel::Info,
        std::string("Settings UI: Modern Camera Enabled changed to ") + (enabled ? "true" : "false"));
    g_IsModernCamEnabled = enabled;
    StopHeldKeyboardPan();
    StopHeldKeyboardRotation("camera mode changed");
    StopClassicAngleHold("camera mode changed");

    if (!enabled) {
        UninstallKeyboardHook();
        UninstallSetScrollingHook();
        const bool restoredNativeCamera = g_CameraController.RestoreNativeCameraState("classic camera enabled");
        if (restoredNativeCamera) {
            ResetInputState(false);
        }
        else {
            ResetCameraToNativeView();
        }
        ClearNativeView3DToolState(View3DToolClearReason::ClassicCameraEnabled);
        ScheduleView3DFocusRestore(false, "Classic camera enabled focus restore");
        Logger::GetInstance().WriteLine(
            LogLevel::Info,
            "Classic camera enabled; Modern input hooks are disabled and native SC4 input continues.");
    }
    else {
        g_CameraController.CaptureNativeCameraState("modern camera enabled");
        RegisterCanvasWinProcFilterForModernCamera();
        InstallSetScrollingHook();
        InstallKeyboardHook();
        g_CameraController.EnsureTargetNearCityCenterIfOutOfBounds("modern camera enabled");
        ClearNativeView3DToolState(View3DToolClearReason::ModernCameraEnabled);
        ScheduleModernCameraFocusRestore();
        TriggerCityRedraw();
    }
}

// Clears held input and refreshes native input after settings change.
void ApplyInputSettingsChange()
{
    Logger::GetInstance().WriteLine(LogLevel::Info, "Settings UI: input settings changed; clearing held input state.");
    StopHeldKeyboardPan();
    StopHeldKeyboardRotation("input settings changed");
    ClearTrackedKeyStates("input settings changed");
    if (g_IsModernCamEnabled) {
        ClearNativeView3DToolState(View3DToolClearReason::WASDMovementChanged);
        FocusView3DForNativeInput("Input settings changed focus handoff");
        ScheduleModernCameraFocusRestore();
    }
}

// Maps the selected debug setting to the logger's active verbosity.
void ApplyDebugLoggingChange()
{
    LogVerbosity verbosity = LogVerbosity::Normal;
    const char* name = "Normal";
    switch (g_Settings.debugLogging) {
    case DebugLogging::Off:
        verbosity = LogVerbosity::Off;
        name = "Off";
        break;
    case DebugLogging::Verbose:
        verbosity = LogVerbosity::Verbose;
        name = "Verbose";
        break;
    default:
        break;
    }

    Logger::GetInstance().WriteLine(
        LogLevel::Info,
        std::string("Settings UI: debug logging setting changed to ") + name + ".");
    Logger::GetInstance().SetVerbosity(verbosity);
    Logger::GetInstance().WriteLine(LogLevel::Info, "Settings UI: debug logging is active.");
}

// Returns keyboard and mouse focus to the city after closing settings.
void RestoreWorldFocusAfterSettingsClosed()
{
    Logger::GetInstance().WriteLine(LogLevel::Info, "Settings UI: closed; restoring city focus.");
    FocusView3DForNativeInput("Settings window closed focus handoff");
    ScheduleView3DFocusRestore(false, "Settings window closed focus restore");
}

// Returns whether the right mouse button is currently held for native scrolling.
bool IsRightClickScrollingActive()
{
    return IsObservedOrPhysicalKeyDown(VK_RBUTTON);
}

// Returns a virtual key's observed state with a physical-state fallback.
bool IsVirtualKeyDown(int key)
{
    return IsObservedOrPhysicalKeyDown(key);
}

// Returns whether the key completes the Ctrl+Alt+F8 camera-dump shortcut.
bool IsManualCameraDumpHotkey(WPARAM key)
{
    RefreshTrackedModifierStates();
    return key == kDumpCameraInfoKey
        && IsVirtualKeyDown(VK_CONTROL)
        && IsVirtualKeyDown(VK_MENU);
}

// Returns whether a native command-shortcut modifier is currently held. Windows
// sees a held Alt, but an Alt owned by the classic-angle hold is a camera action.
bool IsCommandShortcutModifierDown()
{
    RefreshTrackedModifierStates();
    return IsVirtualKeyDown(VK_CONTROL)
        || (IsVirtualKeyDown(VK_MENU) && !HasOwnedClassicAngleKey())
        || IsVirtualKeyDown(VK_LWIN)
        || IsVirtualKeyDown(VK_RWIN);
}

// Logs movement-key pass-through while a native shortcut modifier is held.
void LogKeyboardShortcutPassThrough(WPARAM key, const char* source)
{
    Logger::GetInstance().WriteLineLazy(LogLevel::Verbose, [&]() {
        return std::string("Keyboard movement pass-through: command shortcut modifier is down. Source:")
            + (source ? source : "unknown")
            + " Key:" + FormatVirtualKey(key)
            + " Ctrl:" + (IsVirtualKeyDown(VK_CONTROL) ? "true" : "false")
            + " Shift:" + (IsVirtualKeyDown(VK_SHIFT) ? "true" : "false")
            + " Alt:" + (IsVirtualKeyDown(VK_MENU) ? "true" : "false")
            + " LWin:" + (IsVirtualKeyDown(VK_LWIN) ? "true" : "false")
            + " RWin:" + (IsVirtualKeyDown(VK_RWIN) ? "true" : "false");
    });
}

// Returns whether a GZ window implements the text-edit interface.
bool IsTextEditWindow(cIGZWin* window)
{
    cRZAutoRefCount<cIGZWinTextEdit> textEdit;
    return window && window->QueryInterface(GZIID_cIGZWinTextEdit, textEdit.AsPPVoid());
}

// Returns whether the focused GZ window is a text-edit control.
bool IsTextEditFocused(cIGZWin** focusedOut = nullptr)
{
    if (focusedOut) {
        *focusedOut = nullptr;
    }

    cIGZWinMgrPtr winMgr;
    if (!winMgr) {
        return false;
    }

    cIGZWin* focusedWindow = winMgr->GZGetFocus();
    if (focusedOut) {
        *focusedOut = focusedWindow;
    }

    return IsTextEditWindow(focusedWindow);
}

// Returns whether a window identifier belongs to Modern Camera UI.
bool IsModernCameraWindowID(uint32_t id)
{
    return id >= kModernCameraWindowIDMin && id <= kModernCameraWindowIDMax;
}

// Focuses the city 3D view and canvas for native camera input.
bool FocusView3DForNativeInput(const char* context)
{
    cISC4AppPtr app;
    cIGZWin* mainWindow = app ? app->GetMainWindow() : nullptr;
    cIGZWin* parentWindow = mainWindow ? mainWindow->GetChildWindowFromID(kGZWin_WinSC4App) : nullptr;
    cISC4View3DWin* view3D = nullptr;
    if (!parentWindow
        || !parentWindow->GetChildAs(
            kGZWin_SC4View3DWin,
            kGZIID_cISC4View3DWin,
            reinterpret_cast<void**>(&view3D))
        || !view3D) {
        Logger::GetInstance().WriteLine(
            LogLevel::Warning,
            std::string(context ? context : "native input")
            + ": failed to find 3D view for keyboard focus.");
        return false;
    }

    cIGZWin* viewWindow = view3D->AsIGZWin();
    cIGZWinMgrPtr winMgr;
    const bool focused = winMgr && viewWindow && winMgr->GZSetFocus(viewWindow);
    const bool currentMouseSet = winMgr && viewWindow && winMgr->SetCurrentMouseWin(viewWindow);
    const bool canvasFocused = FocusCanvasWindowForNativeInput(context);
    Logger::GetInstance().WriteLine(
        (focused && currentMouseSet && canvasFocused) ? LogLevel::Verbose : LogLevel::Warning,
        std::string(context ? context : "native input")
        + ": 3D view activation Focus="
        + (focused ? "true" : "false")
        + " CurrentMouse="
        + (currentMouseSet ? "true" : "false")
        + " CanvasFocus="
        + (canvasFocused ? "true" : "false"));
    view3D->Release();
    return focused && currentMouseSet && canvasFocused;
}

// Returns whether keyboard focus is in the city 3D view hierarchy.
bool IsCameraKeyboardFocus()
{
    cIGZWinMgrPtr winMgr;
    cIGZWin* focusedWindow = winMgr ? winMgr->GZGetFocus() : nullptr;
    if (!focusedWindow) {
        Logger::GetInstance().WriteLine(
            LogLevel::Verbose,
            "Keyboard capture pass-through: no focused window.");
        return false;
    }

    const uint32_t focusedID = focusedWindow->GetID();
    if (focusedID == kGZWin_SC4View3DWin || focusedID == kGZWin_SC4View3DSurface) {
        return true;
    }
    if (IsModernCameraWindowID(focusedID)) {
        if (g_WindowManager.HasVisibleWindow()) {
            Logger::GetInstance().WriteLineLazy(LogLevel::Verbose, [&]() {
                return "Keyboard capture pass-through: focused window is an open Modern Camera UI control. Focus:{"
                    + DescribeGZWindow(focusedWindow)
                    + "}";
            });
            return false;
        }

        Logger::GetInstance().WriteLineLazy(LogLevel::Verbose, [&]() {
            return "Keyboard focus recovery: focused window is a stale Modern Camera UI control. Focus:{"
                + DescribeGZWindow(focusedWindow)
                + "}";
        });
        return FocusView3DForNativeInput("Keyboard stale Modern Camera UI focus recovery");
    }

    cISC4AppPtr app;
    cIGZWin* mainWindow = app ? app->GetMainWindow() : nullptr;
    cIGZWin* parentWindow = mainWindow ? mainWindow->GetChildWindowFromID(kGZWin_WinSC4App) : nullptr;
    cISC4View3DWin* view3D = nullptr;
    bool cameraFocus = false;
    if (parentWindow
        && parentWindow->GetChildAs(
            kGZWin_SC4View3DWin,
            kGZIID_cISC4View3DWin,
            reinterpret_cast<void**>(&view3D))) {
        cIGZWin* viewWindow = view3D->AsIGZWin();
        cameraFocus = focusedWindow == viewWindow
            || (viewWindow && viewWindow->IsWinInChildChain(focusedWindow));
        view3D->Release();
    }

    if (!cameraFocus) {
        Logger::GetInstance().WriteLineLazy(LogLevel::Verbose, [&]() {
            return "Keyboard capture pass-through: focused window is not the 3D view. Focus:{"
                + DescribeGZWindow(focusedWindow)
                + "}";
        });
    }

	return cameraFocus;
}

// Maps the top-row numeric keys used by the eight fixed bookmark slots.
bool GetBookmarkHotkeySlot(WPARAM key, uint32_t& slot)
{
	if (key < '1' || key > '8')
	{
		return false;
	}
	slot = static_cast<uint32_t>(key - '1');
	return true;
}

// Invokes numbered bookmark commands only in the explicitly supported focus
// context. Ctrl saves; unmodified number keys recall. All other modifiers are
// left to SC4 so they preserve native command behavior.
bool HandleBookmarkHotkey(WPARAM key, bool isRepeat)
{
	uint32_t slot = 0;
	if (!GetBookmarkHotkeySlot(key, slot)
		|| !g_IsCityLoaded
		|| !g_IsModernCamEnabled
		|| !g_Settings.bookmarksEnabled
		|| !IsCameraKeyboardFocus())
	{
		return false;
	}

	RefreshTrackedModifierStates();
	const bool control = IsVirtualKeyDown(VK_CONTROL);
	const bool shift = IsVirtualKeyDown(VK_SHIFT);
	const bool alt = IsVirtualKeyDown(VK_MENU);
	const bool windows = IsVirtualKeyDown(VK_LWIN) || IsVirtualKeyDown(VK_RWIN);
	if (alt || windows || shift)
	{
		return false;
	}

	if (!isRepeat)
	{
		if (control)
		{
			SaveCameraBookmark(slot);
		}
		else
		{
			RecallCameraBookmark(slot);
		}
	}
	return true;
}

// Consumes the character counterpart of a handled recall or Ctrl-save so SC4
// never receives a trailing numeric character after the keydown command.
bool ShouldConsumeBookmarkCharacter(WPARAM character)
{
	uint32_t slot = 0;
	if (!GetBookmarkHotkeySlot(character, slot)
		|| !g_IsCityLoaded
		|| !g_IsModernCamEnabled
		|| !g_Settings.bookmarksEnabled
		|| !IsCameraKeyboardFocus())
	{
		return false;
	}
	RefreshTrackedModifierStates();
	return !IsVirtualKeyDown(VK_SHIFT)
		&& !IsVirtualKeyDown(VK_MENU)
		&& !IsVirtualKeyDown(VK_LWIN)
		&& !IsVirtualKeyDown(VK_RWIN);
}

// Returns whether a virtual key is one of the WASD movement keys.
bool IsWASDVirtualKey(WPARAM key)
{
    switch (key) {
    case 'W':
    case 'S':
    case 'A':
    case 'D':
        return true;
    default:
        return false;
    }
}

// Returns whether a virtual key is one of the arrow movement keys.
bool IsArrowVirtualKey(WPARAM key)
{
    switch (key) {
    case VK_UP:
    case VK_DOWN:
    case VK_LEFT:
    case VK_RIGHT:
        return true;
    default:
        return false;
    }
}

// Returns whether a virtual key can request keyboard camera movement.
bool IsKeyboardPanVirtualKey(WPARAM key)
{
    return IsWASDVirtualKey(key) || IsArrowVirtualKey(key);
}

// Returns whether a keyboard movement key is handled by the capture path.
bool IsCapturedKeyboardPanVirtualKey(WPARAM key)
{
    return IsKeyboardPanVirtualKey(key);
}

// Returns whether a character message contains a WASD movement character.
bool IsWASDCharacter(WPARAM character)
{
    return character == 'W' || character == 'w'
        || character == 'A' || character == 'a'
        || character == 'S' || character == 's'
        || character == 'D' || character == 'd';
}

// Returns whether a character message contains a Q/E rotation character.
bool IsQERotationCharacter(WPARAM character)
{
    return character == 'Q' || character == 'q'
        || character == 'E' || character == 'e';
}

// Returns whether a keyboard or character message represents Enter.
bool IsEnterInput(UINT message, WPARAM key)
{
    if (message == WM_KEYDOWN
        || message == WM_SYSKEYDOWN
        || message == WM_KEYUP
        || message == WM_SYSKEYUP) {
        return key == VK_RETURN;
    }

    return (message == WM_CHAR || message == WM_SYSCHAR) && key == '\r';
}

// Returns whether a keyboard message represents Escape.
bool IsEscapeInput(UINT message, WPARAM key)
{
    return (message == WM_KEYDOWN
        || message == WM_SYSKEYDOWN
        || message == WM_KEYUP
        || message == WM_SYSKEYUP)
        && key == VK_ESCAPE;
}

// Logs Enter pass-through for a focused text-entry control.
void LogTextInputPassThrough(UINT message, WPARAM key, cIGZWin* focusedWindow, const char* source)
{
    if (!IsEnterInput(message, key)) {
        return;
    }

    Logger::GetInstance().WriteLine(
        LogLevel::Verbose,
        std::string("Keyboard text input pass-through: Enter key. Source:")
        + (source ? source : "unknown")
        + " Message:" + std::to_string(message)
        + " Focus:{" + DescribeGZWindow(focusedWindow) + "}");
}

// Logs Escape pass-through and the window that received it.
void LogEscapePassThrough(UINT message, cIGZWin* focusedWindow, const char* source)
{
    Logger::GetInstance().WriteLine(
        LogLevel::Verbose,
        std::string("Keyboard pass-through: Escape key. Source:")
        + (source ? source : "unknown")
        + " Message:" + std::to_string(message)
        + " Focus:{" + DescribeGZWindow(focusedWindow) + "}"
        + " ModernCameraEnabled:" + (g_IsModernCamEnabled ? "true" : "false"));
}

// Decides whether a movement key should be captured for Modern Camera.
bool ShouldCaptureKeyboardPanKeyInFocus(WPARAM key, bool cameraFocus)
{
    return g_IsCityLoaded
        && g_IsModernCamEnabled
        && (IsArrowVirtualKey(key) || (g_Settings.wasdMovement && IsWASDVirtualKey(key)))
        && IsCapturedKeyboardPanVirtualKey(key)
        && !IsCommandShortcutModifierDown()
        && cameraFocus
        && !IsRightClickScrollingActive();
}

// Decides whether held movement keys may continue moving the camera.
bool ShouldCaptureHeldKeyboardPan()
{
    return g_IsCityLoaded
        && g_IsModernCamEnabled
        && !IsCommandShortcutModifierDown()
        && IsCameraKeyboardFocus()
        && !IsRightClickScrollingActive()
        && (HasHeldArrowKeyboardPanKey()
            || (g_Settings.wasdMovement && HasHeldWASDKeyboardPanKey()));
}

// Decides whether Q or E should control the camera while the 3D view has focus.
bool ShouldCaptureKeyboardRotationKeyInFocus(WPARAM key, bool cameraFocus)
{
    if (!g_IsCityLoaded
        || !g_IsModernCamEnabled
        || !g_Settings.qeRotation
        || !IsKeyboardRotationVirtualKey(key)
        || !cameraFocus
        || IsRightClickScrollingActive()) {
        return false;
    }

    RefreshTrackedModifierStates();
    if (IsVirtualKeyDown(VK_CONTROL)
        || IsVirtualKeyDown(VK_LWIN)
        || IsVirtualKeyDown(VK_RWIN)) {
        return false;
    }

    const bool classicAngleHoldActive = HasHeldClassicAngleKey()
        && g_CameraController.IsClassicAngleHoldActive();
    if (classicAngleHoldActive) {
        return true;
    }

    return !IsVirtualKeyDown(VK_MENU);
}

// Decides whether held Q/E keys may continue rotating the camera.
bool ShouldCaptureHeldKeyboardRotation()
{
    RefreshTrackedModifierStates();
    return g_IsCityLoaded
        && g_IsModernCamEnabled
        && g_Settings.qeRotation
        && !IsVirtualKeyDown(VK_CONTROL)
        && !IsVirtualKeyDown(VK_MENU)
        && !IsVirtualKeyDown(VK_LWIN)
        && !IsVirtualKeyDown(VK_RWIN)
        && IsCameraKeyboardFocus()
        && !IsRightClickScrollingActive()
        && HasHeldKeyboardRotationKey();
}

// Applies one frame of Q/E yaw to the Modern Camera controller.
bool ApplyKeyboardRotation(const char* source, float elapsedSeconds)
{
    float direction = (IsKeyboardRotationKeyDown('Q') ? 1.0f : 0.0f)
        - (IsKeyboardRotationKeyDown('E') ? 1.0f : 0.0f);
    if (g_Settings.invertQE) {
        direction = -direction;
    }
    if (direction == 0.0f) {
        return true;
    }

    const float yawDelta = direction
        * kKeyboardRotationRadiansPerSecond
        * elapsedSeconds
        * g_Settings.qeRotationSpeed;
    const bool applied = g_CameraController.ApplyDelta(0.0f, yawDelta, true);
    Logger::GetInstance().WriteLineLazy(LogLevel::Verbose, [&]() {
        return "Keyboard Q/E rotation tick. Source:"
            + std::string(source ? source : "unknown")
            + " Direction:" + std::to_string(direction)
            + " YawDelta:" + std::to_string(yawDelta)
            + " Result:" + (applied ? "true" : "false");
    });
    return applied;
}

// Converts held movement keys into a native 3D-view scrolling request.
bool ApplyKeyboardPan(const char* source, LogLevel successLogLevel)
{
    cISC4View3DWin* view3D = GetView3DWinForInput(source ? source : "keyboard movement");
    if (!view3D) {
        return false;
    }

    const float rightSteps = ((IsKeyboardPanKeyDown('D') || IsKeyboardPanKeyDown(VK_RIGHT)) ? 1.0f : 0.0f)
        - ((IsKeyboardPanKeyDown('A') || IsKeyboardPanKeyDown(VK_LEFT)) ? 1.0f : 0.0f);
    const float forwardSteps = ((IsKeyboardPanKeyDown('W') || IsKeyboardPanKeyDown(VK_UP)) ? 1.0f : 0.0f)
        - ((IsKeyboardPanKeyDown('S') || IsKeyboardPanKeyDown(VK_DOWN)) ? 1.0f : 0.0f);
    if (rightSteps == 0.0f && forwardSteps == 0.0f) {
        const bool stopped = view3D->ScrollStop();
        Logger::GetInstance().WriteLineLazy(successLogLevel, [&]() {
            return "Keyboard movement stopped. Source:"
            + std::string(source ? source : "unknown")
            + " Result:" + (stopped ? "true" : "false");
        });
        view3D->Release();
        return stopped;
    }

    const float directionAngle = std::atan2(-forwardSteps, rightSteps);
    RefreshTrackedModifierStates();
    const float speedMultiplier = IsVirtualKeyDown(VK_SHIFT)
        ? g_Settings.shiftPanBoost
        : 1.0f;
    // Native speed follows the zoom stage alone, so without this the on-screen speed
    // would rise with magnification across a stage, then drop at the next stage.
    const float scrollSpeed = kKeyboardPanNativeSpeed
        * speedMultiplier
        * g_Settings.panSensitivity
        * g_CameraController.GetPanSpeedZoomScale();

    g_IsApplyingKeyboardPan = true;
    view3D->SetScrolling(true, directionAngle, scrollSpeed);
    g_IsApplyingKeyboardPan = false;

    Logger::GetInstance().WriteLineLazy(successLogLevel, [&]() {
        return "Keyboard native movement updated. Source:"
        + std::string(source ? source : "unknown")
        + " RightSteps:" + std::to_string(rightSteps)
        + " ForwardSteps:" + std::to_string(forwardSteps)
        + " DirectionAngle:" + std::to_string(directionAngle)
        + " ScrollSpeed:" + std::to_string(scrollSpeed);
    });

    view3D->Release();
    return true;
}

// Releases held movement keys and stops their periodic camera motion.
void StopHeldKeyboardPan()
{
    const bool hadHeldKey = HasOwnedKeyboardPanKey();
    ClearHeldKeyboardPanKeys();
    if (hadHeldKey) {
        ApplyKeyboardPan("keyboard capture stopped", LogLevel::Verbose);
    }
}

// Releases held Q/E rotation and ends its gesture when middle-mouse rotation is idle.
void StopHeldKeyboardRotation(const char* reason)
{
    const bool hadHeldKey = HasOwnedKeyboardRotationKey();
    ClearHeldKeyboardRotationKeys();
    if (g_KeyboardRotationGestureActive) {
        g_KeyboardRotationGestureActive = false;
        if (!IsMiddleMouseRotationActive()) {
            g_CameraController.EndRotationGesture();
        }
    }
    if (hadHeldKey) {
        Logger::GetInstance().WriteLine(
            LogLevel::Verbose,
            std::string("Keyboard Q/E rotation stopped. Reason:")
            + (reason ? reason : "unknown"));
    }
}

// Releases the Alt-key classic-angle hold and returns to the saved Modern view at once.
// Frames may not run after this, because the callers are losing focus or changing state.
void StopClassicAngleHold(const char* source)
{
    const bool hadHeldKey = HasOwnedClassicAngleKey();
    ClearClassicAngleKeys();
    if (hadHeldKey) {
        g_CameraController.EndClassicAngleHold(source ? source : "classic angle hold stopped", false);
    }
    g_CameraController.FinishClassicAngleTransition();
}

// Updates movement-key ownership and applies the resulting camera motion.
void HandleKeyboardPanKeyState(WPARAM key, bool pressed)
{
    if (!IsKeyboardPanVirtualKey(key)) {
        return;
    }

    const bool wasActive = pressed
        ? IsKeyboardPanKeyDown(key)
        : IsKeyOwnedBy(key, KeyOwner::ModernCameraPan);

    if (pressed && !ClaimKey(key, KeyOwner::ModernCameraPan)) {
        Logger::GetInstance().WriteLine(
            LogLevel::Verbose,
            "Keyboard movement ignored because key is owned by another feature. Key:"
            + std::to_string(static_cast<uint32_t>(key)));
        return;
    }
    if (!pressed
        && IsTrackableKey(key)
        && g_KeyStateManager.GetOwner(key) != KeyOwner::None
        && !IsKeyOwnedBy(key, KeyOwner::ModernCameraPan)) {
        return;
    }

    ObserveKeyTransition(key, pressed);
    const bool isActive = IsKeyboardPanKeyDown(key);

    if (wasActive != isActive) {
        Logger::GetInstance().WriteLine(
            LogLevel::Verbose,
            std::string("Keyboard movement key state changed. Key:") + FormatVirtualKey(key)
            + " Pressed:" + (pressed ? "true" : "false")
            + " WasActive:" + (wasActive ? "true" : "false")
            + " IsActive:" + (isActive ? "true" : "false")
            + " AnyHeld:" + (HasHeldKeyboardPanKey() ? "true" : "false"));
        ApplyKeyboardPan("keyboard key capture", LogLevel::Verbose);
    }

    if (!pressed) {
        ReleaseKey(key, KeyOwner::ModernCameraPan);
    }
}

// Updates Q/E ownership and performs a continuous rotation or one Classic-angle step.
void HandleKeyboardRotationKeyState(WPARAM key, bool pressed)
{
    if (!IsKeyboardRotationVirtualKey(key)) {
        return;
    }

    const bool classicStep = HasHeldClassicAngleKey()
        && g_CameraController.IsClassicAngleHoldActive();
    const KeyOwner owner = classicStep
        ? KeyOwner::ModernCameraClassicRotation
        : KeyOwner::ModernCameraRotation;
    const KeyOwner previousOwner = g_KeyStateManager.GetOwner(key);
    const bool wasOwned = previousOwner == KeyOwner::ModernCameraRotation
        || previousOwner == KeyOwner::ModernCameraClassicRotation;
    if (pressed && !ClaimKey(key, owner)) {
        return;
    }
    if (!pressed
        && IsTrackableKey(key)
        && g_KeyStateManager.GetOwner(key) != KeyOwner::None
        && !wasOwned) {
        return;
    }

    ObserveKeyTransition(key, pressed);

    if (pressed && classicStep) {
        if (!wasOwned) {
            g_CameraController.RotateClassicStep((key == 'Q') != g_Settings.invertQE, key == 'Q' ? "Alt+Q" : "Alt+E");
        }
    }
    else if (pressed) {
        if (!g_KeyboardRotationGestureActive) {
            g_KeyboardRotationGestureActive = g_CameraController.BeginRotationGesture();
        }
    }

    if (!pressed) {
        ReleaseKey(key, previousOwner);
        if (!HasHeldKeyboardRotationKey() && g_KeyboardRotationGestureActive) {
            g_KeyboardRotationGestureActive = false;
            if (!IsMiddleMouseRotationActive()) {
                g_CameraController.EndRotationGesture();
            }
        }
    }
}

// Starts or ends the temporary Alt-key classic-angle camera mode.
void HandleClassicAngleKeyState(WPARAM key, bool pressed)
{
    if (!IsClassicAngleVirtualKey(key)) {
        return;
    }

    const bool wasActive = HasOwnedClassicAngleKey();
    if (pressed && !ClaimKey(key, KeyOwner::ModernCameraClassicAngle)) {
        Logger::GetInstance().WriteLine(
            LogLevel::Verbose,
            "Classic angle hold ignored because key is owned by another feature. Key:"
            + FormatVirtualKey(key));
        return;
    }
    if (!pressed
        && IsTrackableKey(key)
        && g_KeyStateManager.GetOwner(key) != KeyOwner::None
        && !IsKeyOwnedBy(key, KeyOwner::ModernCameraClassicAngle)) {
        return;
    }

    ObserveKeyTransition(key, pressed);
    const bool isActive = HasHeldClassicAngleKey();

    if (!wasActive && isActive) {
        StopHeldKeyboardPan();
        StopHeldKeyboardRotation("Alt key pressed");
        // A Q or E held through the press keeps auto-repeating. Owning it as a finished
        // step stops the next repeat from turning the camera a quarter turn on its own.
        for (const WPARAM rotationKey : kKeyboardRotationKeys) {
            if (IsTrackedKeyDown(static_cast<int>(rotationKey))) {
                ClaimKey(rotationKey, KeyOwner::ModernCameraClassicRotation);
            }
        }
        g_CameraController.BeginClassicAngleHold("Alt key pressed");
    }
    else if (wasActive && !isActive) {
        if (g_Settings.altCameraBungie) {
            g_CameraController.EndClassicAngleHold("Alt key released", true);
        }
        else {
            g_CameraController.DropClassicAngleHold("Alt key released");
        }
    }

    if (!pressed) {
        ReleaseKey(key, KeyOwner::ModernCameraClassicAngle);
    }
}

// Records keyboard and mouse transitions seen by the canvas filter.
void ObserveCanvasInputTransition(UINT message, WPARAM key)
{
    switch (message) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        ObserveKeyTransition(key, true);
        break;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        ObserveKeyTransition(key, false);
        break;
    case WM_LBUTTONDOWN:
        ObserveKeyTransition(VK_LBUTTON, true);
        break;
    case WM_LBUTTONUP:
        ObserveKeyTransition(VK_LBUTTON, false);
        break;
    case WM_RBUTTONDOWN:
        ObserveKeyTransition(VK_RBUTTON, true);
        break;
    case WM_RBUTTONUP:
        ObserveKeyTransition(VK_RBUTTON, false);
        break;
    case WM_MBUTTONDOWN:
        ObserveKeyTransition(VK_MBUTTON, true);
        break;
    case WM_MBUTTONUP:
        ObserveKeyTransition(VK_MBUTTON, false);
        break;
    default:
        break;
    }
}

// Installs the low-level hook used to observe keyboard state globally.
void InstallKeyboardHook()
{
    if (g_KeyboardHook != NULL) {
        return;
    }

    HMODULE moduleHandle = NULL;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&KeyboardHookProc),
        &moduleHandle);

    g_KeyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHookProc, moduleHandle, 0);
    if (g_KeyboardHook != NULL) {
        Logger::GetInstance().WriteLine(LogLevel::Info, "Registered low-level keyboard state observer.");
    }
    else {
        Logger::GetInstance().WriteLine(
            LogLevel::Warning,
            "Failed to register low-level keyboard state observer. GetLastError="
            + std::to_string(GetLastError()));
    }
}

// Removes the low-level keyboard state observer.
void UninstallKeyboardHook()
{
    if (g_KeyboardHook == NULL) {
        return;
    }

    if (!UnhookWindowsHookEx(g_KeyboardHook)) {
        Logger::GetInstance().WriteLine(
            LogLevel::Warning,
            "Failed to unregister low-level keyboard state observer. GetLastError="
            + std::to_string(GetLastError()));
    }
    else {
        Logger::GetInstance().WriteLine(LogLevel::Info, "Unregistered low-level keyboard state observer.");
    }
    g_KeyboardHook = NULL;
}

/// <summary>
/// Observes one low-level keyboard event and captures eligible camera controls.
/// </summary>
/// <param name="nCode">Hook code supplied by Windows.</param>
/// <param name="wParam">Keyboard message identifier.</param>
/// <param name="lParam">Pointer to the low-level keyboard event.</param>
/// <returns>1 to consume the key; otherwise the result of the next hook.</returns>
LRESULT HandleKeyboardHookEvent(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION) {
        const KBDLLHOOKSTRUCT* keyboard = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        const bool isKeyDown = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
        const bool isKeyUp = wParam == WM_KEYUP || wParam == WM_SYSKEYUP;

        if (keyboard && (isKeyDown || isKeyUp)) {
            DWORD foregroundProcessID = 0;
            GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcessID);
            if (foregroundProcessID == GetCurrentProcessId()) {
                ObserveKeyTransition(keyboard->vkCode, isKeyDown);

                if (!IsModernInputActive()) {
                    return CallNextHookEx(g_KeyboardHook, nCode, wParam, lParam);
                }

                cIGZWin* focusedWindow = nullptr;
                if (IsTextEditFocused(&focusedWindow)) {
                    LogTextInputPassThrough(
                        static_cast<UINT>(wParam),
                        keyboard->vkCode,
                        focusedWindow,
                        "low-level keyboard hook");
                    if (IsKeyboardPanVirtualKey(keyboard->vkCode) && HasOwnedKeyboardPanKey()) {
                        StopHeldKeyboardPan();
                    }
                    if (IsKeyboardRotationVirtualKey(keyboard->vkCode) && HasOwnedKeyboardRotationKey()) {
                        StopHeldKeyboardRotation("text input focus");
                    }
                    return CallNextHookEx(g_KeyboardHook, nCode, wParam, lParam);
                }
                if (keyboard->vkCode == VK_ESCAPE) {
                    if (isKeyDown && !g_WindowManager.HasVisibleWindow()) {
                        FocusView3DForNativeInput("Escape key pass-through focus handoff");
                    }
                    LogEscapePassThrough(
                        static_cast<UINT>(wParam),
                        focusedWindow,
                        "low-level keyboard hook");
                    if (HasOwnedKeyboardPanKey()) {
                        StopHeldKeyboardPan();
                    }
                    if (HasOwnedKeyboardRotationKey()) {
                        StopHeldKeyboardRotation("Escape key");
                    }
                    return CallNextHookEx(g_KeyboardHook, nCode, wParam, lParam);
                }

                // Alt continues to Windows so Alt+Tab and Alt+F4 keep working; hiding it
                // here would turn those chords into plain Tab and F4. The canvas filter
                // consumes the resulting messages so a plain Alt press does nothing else.
                if (IsClassicAngleVirtualKey(keyboard->vkCode)) {
                    const bool cameraFocus = IsCameraKeyboardFocus();
                    if (ShouldCaptureClassicAngleKeyInFocus(keyboard->vkCode, cameraFocus)) {
                        HandleClassicAngleKeyState(keyboard->vkCode, isKeyDown);
                    }
                    else if (isKeyUp && HasOwnedClassicAngleKey()) {
                        HandleClassicAngleKeyState(keyboard->vkCode, false);
                    }
                    return CallNextHookEx(g_KeyboardHook, nCode, wParam, lParam);
                }
            }
        }

        if (keyboard
            && (isKeyDown || isKeyUp)
            && IsKeyboardRotationVirtualKey(keyboard->vkCode)) {
            DWORD foregroundProcessID = 0;
            GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcessID);
            if (foregroundProcessID == GetCurrentProcessId()) {
                const bool cameraFocus = IsCameraKeyboardFocus();
                if (ShouldCaptureKeyboardRotationKeyInFocus(keyboard->vkCode, cameraFocus)) {
                    HandleKeyboardRotationKeyState(keyboard->vkCode, isKeyDown);
                    return 1;
                }
                if (isKeyUp && HasOwnedKeyboardRotationKey()) {
                    HandleKeyboardRotationKeyState(keyboard->vkCode, false);
                    if (cameraFocus) {
                        return 1;
                    }
                    return CallNextHookEx(g_KeyboardHook, nCode, wParam, lParam);
                }
                StopHeldKeyboardRotation("keyboard capture interrupted");
            }
            else if (isKeyUp && HasOwnedKeyboardRotationKey()) {
                ObserveKeyTransition(keyboard->vkCode, false);
                HandleKeyboardRotationKeyState(keyboard->vkCode, false);
            }
        }

        if (keyboard
            && (isKeyDown || isKeyUp)
            && IsCapturedKeyboardPanVirtualKey(keyboard->vkCode)) {
            DWORD foregroundProcessID = 0;
            GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcessID);
            if (foregroundProcessID == GetCurrentProcessId()) {
                if (IsCommandShortcutModifierDown()) {
                    LogKeyboardShortcutPassThrough(keyboard->vkCode, "low-level keyboard hook");
                    if (HasOwnedKeyboardPanKey()) {
                        StopHeldKeyboardPan();
                    }
                    return CallNextHookEx(g_KeyboardHook, nCode, wParam, lParam);
                }

                const bool cameraFocus = IsCameraKeyboardFocus();
                if (ShouldCaptureKeyboardPanKeyInFocus(keyboard->vkCode, cameraFocus)) {
                    HandleKeyboardPanKeyState(keyboard->vkCode, isKeyDown);
                    return 1;
                }
                if (isKeyUp && HasOwnedKeyboardPanKey()) {
                    HandleKeyboardPanKeyState(keyboard->vkCode, false);
                    if (cameraFocus) {
                        return 1;
                    }
                    return CallNextHookEx(g_KeyboardHook, nCode, wParam, lParam);
                }
                StopHeldKeyboardPan();
            }
            else if (isKeyUp && HasOwnedKeyboardPanKey()) {
                ObserveKeyTransition(keyboard->vkCode, false);
                HandleKeyboardPanKeyState(keyboard->vkCode, false);
            }
        }
    }

    return CallNextHookEx(g_KeyboardHook, nCode, wParam, lParam);
}

// Observes global keyboard input and captures eligible camera controls.
LRESULT CALLBACK KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    LRESULT result = 0;
    bool completed = false;
    RunGuardedNativeCallback("Keyboard hook", [&]() {
        result = HandleKeyboardHookEvent(nCode, wParam, lParam);
        completed = true;
    });
    return completed ? result : CallNextHookEx(g_KeyboardHook, nCode, wParam, lParam);
}

// Verifies that the running SimCity 4 build is supported.
bool CheckGameVersion()
{
    const uint16_t gameVersion = SC4VersionDetection::GetGameVersion();
    Logger::GetInstance().WriteLine(LogLevel::Info, "Detected SimCity 4 game version: " + std::to_string(gameVersion));

    const bool supportedVersion = SC4VersionDetection::IsDigitalDistributionVersion();
    if (!supportedVersion) {
        Logger::GetInstance().WriteLine(
            LogLevel::Error,
            "Unsupported Game Version. This plugin currently only supports the Steam/GOG/EA digital distribution build.");
    }
    else {
        Logger::GetInstance().WriteLine(LogLevel::Info, "Plugin installed.");
    }

    return supportedVersion;
}

// Subscribes the director to city lifecycle, save, and occupant notifications.
bool RegisterNotifications(cIGZMessageTarget2* target)
{
    cIGZMessageServer2Ptr pMsgServ;
    if (pMsgServ) {
        const bool addedPostCityInit = pMsgServ->AddNotification(target, kSC4MessagePostCityInit);
        const bool addedPreCityShutdown = pMsgServ->AddNotification(target, kSC4MessagePreCityShutdown);
        const bool addedPreSave = pMsgServ->AddNotification(target, kSC4MessagePreSave);
        const bool addedPostSave = pMsgServ->AddNotification(target, kSC4MessagePostSave);
        const bool addedInsertOccupant = pMsgServ->AddNotification(target, kSC4MessageInsertOccupant);
        const bool addedRemoveOccupant = pMsgServ->AddNotification(target, kSC4MessageRemoveOccupant);

        return addedPostCityInit && addedPreCityShutdown && addedPreSave && addedPostSave
            && addedInsertOccupant && addedRemoveOccupant;
    }

    return false;
}

// Executes the delayed redraw.
VOID CALLBACK RedrawTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime) {
    Logger& log = Logger::GetInstance();
    if (g_TimerManager.IsCurrent(RuntimeTimer::IdleRedraw, idEvent)) {
        log.WriteLine(LogLevel::Verbose, "Camera redraw timer fired.");
        g_TimerManager.Stop(RuntimeTimer::IdleRedraw);

        TriggerCityRedraw();
    }
}

/// <summary>
/// Returns whether the U-Drive-It tool is the current native view tool.
/// </summary>
/// <returns>True while the player is driving a vehicle.</returns>
bool IsUDriveItToolActive()
{
    cISC4View3DWin* view3D = GetView3DWinForInput("U-Drive-It check");
    if (!view3D) {
        return false;
    }

    cISC4ViewInputControl* control = view3D->GetCurrentViewInputControl();
    const bool active = control && control->GetID() == kViewInputControlUDriveIt;
    view3D->Release();
    return active;
}

/// <summary>
/// Hands Modern Camera input to the game when a U-Drive-It drive starts and takes it
/// back when the drive ends. The camera mode, the view, and the saved settings are
/// left alone, so the player returns to Modern controls exactly as they were.
/// </summary>
void UpdateUDriveItInputHandoff()
{
    const bool driving = g_IsModernCamEnabled && IsUDriveItToolActive();
    if (driving == g_IsUDriveItActive) {
        return;
    }

    g_IsUDriveItActive = driving;
    if (driving) {
        StopHeldKeyboardPan();
        StopHeldKeyboardRotation("U-Drive-It started");
        StopClassicAngleHold("U-Drive-It started");
        StopMiddleMouseRotation("U-Drive-It started", false);
        Logger::GetInstance().WriteLine(
            LogLevel::Info,
            "U-Drive-It started; Modern Camera input is passed to the game until the drive ends.");
        return;
    }

    // A movement key held as the drive ended can start native keyboard scrolling before
    // this frame, and Modern Camera would then consume the release that stops it.
    cISC4View3DWin* view3D = GetView3DWinForInput("U-Drive-It ended");
    if (view3D) {
        view3D->ScrollStop();
        view3D->KillKeyboardScrolling();
        view3D->Release();
    }
    Logger::GetInstance().WriteLine(LogLevel::Info, "U-Drive-It ended; Modern Camera input resumed.");
}

/// <summary>
/// Runs one camera frame: applies held input and accumulated mouse rotation, then
/// completes the requested native updates. Running this once before the draw system
/// limits a frame to one render-grid rebuild however many input messages arrived.
/// </summary>
void RunCameraFrame()
{
    const auto now = std::chrono::steady_clock::now();
    const float elapsedSeconds = (std::min)(
        std::chrono::duration<float>(now - g_LastCameraFrameTime).count(),
        kMaximumCameraFrameSeconds);
    g_LastCameraFrameTime = now;

    if (!g_IsCityLoaded) {
        return;
    }

    // Waiting for this frame guarantees the save has finished, and a save-and-exit
    // that reaches city shutdown first keeps the native tables.
    if (g_SavePreviewRestorePending) {
        g_SavePreviewRestorePending = false;
        g_CameraController.EndSavePreviewNormalization();
    }

    UpdateUDriveItInputHandoff();

    if (g_IsModernCamEnabled) {
        if (HasHeldKeyboardPanKey() && ShouldCaptureHeldKeyboardPan()) {
            ApplyKeyboardPan("held keyboard movement frame", LogLevel::Verbose);
        }
        else if (HasOwnedKeyboardPanKey()) {
            StopHeldKeyboardPan();
        }

        // Rotation waits while the camera holds the native angles for a save. An Alt+Q/E
        // classic step also owns its key, but it is one step and has nothing to continue.
        if (!g_CameraController.IsSavePreviewNormalizationActive()) {
            if (HasHeldKeyboardRotationKey() && ShouldCaptureHeldKeyboardRotation()) {
                ApplyKeyboardRotation("held Q/E rotation frame", elapsedSeconds);
            }
            else if (g_KeyStateManager.AnyOwnedBy(kKeyboardRotationKeys, KeyOwner::ModernCameraRotation)) {
                StopHeldKeyboardRotation("held Q/E capture interrupted");
            }

            ApplyPendingMouseRotation();
            g_CameraController.AdvanceClassicAngleTransition(elapsedSeconds);
        }

        if (g_CameraBookmarks.ConsumeWriteFailure()) {
            ShowCameraBookmarkNotice(CameraBookmarkNotice::StorageFailed, 0);
        }
    }

    g_CameraController.FlushPendingViewUpdates();
}

// Runs camera frames from a timer when the framework tick is unavailable.
VOID CALLBACK CameraFrameFallbackTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime) {
    if (g_TimerManager.IsCurrent(RuntimeTimer::CameraFrameFallback, idEvent)) {
        RunGuardedNativeCallback("Camera frame timer", RunCameraFrame);
    }
}

// Clears the camera-dump confirmation when its timer expires.
VOID CALLBACK ClearCameraDumpConfirmationTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime) {
    if (g_TimerManager.IsCurrent(RuntimeTimer::CameraDumpConfirmation, idEvent)) {
        KillCameraDumpConfirmationTimer();
        g_CameraController.ClearCameraDumpConfirmation();
    }
}

// Clears the bookmark confirmation when its timer expires.
VOID CALLBACK ClearCameraBookmarkConfirmationTimerProc(HWND, UINT, UINT_PTR idEvent, DWORD)
{
    if (g_TimerManager.IsCurrent(RuntimeTimer::BookmarkConfirmation, idEvent)) {
        KillCameraBookmarkConfirmationTimer();
        g_CameraController.ClearCameraBookmarkConfirmation();
    }
}

// Returns whether a canvas message carries pointer input that may target plugin UI.
bool IsCanvasMouseMessage(UINT message)
{
    return message == WM_LBUTTONDOWN
        || message == WM_LBUTTONUP
        || message == WM_RBUTTONDOWN
        || message == WM_RBUTTONUP
        || message == WM_MBUTTONDOWN
        || message == WM_MBUTTONUP
        || message == WM_MOUSEMOVE
        || message == WM_MOUSEWHEEL;
}

// Gives visible plugin windows first access to pointer input in either camera mode.
bool RouteMouseInputToVisiblePluginWindow(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam,
    bool& handled)
{
    if (!IsCanvasMouseMessage(message) || IsMiddleMouseRotationActive()) {
        return false;
    }

    // WM_MOUSEWHEEL supplies screen coordinates. Convert them to the canvas
    // coordinates used by the native SC4 window hierarchy.
    POINT cursor = MakePointFromLParam(lParam);
    if (message == WM_MOUSEWHEEL) {
        if (!window || !ScreenToClient(window, &cursor)) {
            return true;
        }
        if (g_WindowManager.HandleMouseWheel(
            GET_WHEEL_DELTA_WPARAM(wParam),
            cursor.x,
            cursor.y,
            reinterpret_cast<intptr_t>(window))) {
            handled = true;
            return true;
        }
    }

    return g_WindowManager.IsPointOverVisibleWindow(cursor.x, cursor.y);
}

// Routes canvas input between Modern Camera controls and native SC4 UI.
LRESULT HandleCanvasMouseMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, bool& handled)
{
    if (hWnd && IsWindow(hWnd)) {
        g_CanvasWindow = hWnd;
    }

    if (!g_IsCityLoaded) {
        return 0;
    }

    Logger& log = Logger::GetInstance();

    if (uMsg == WM_KILLFOCUS) {
        StopHeldKeyboardPan();
        StopHeldKeyboardRotation("canvas focus lost");
        StopClassicAngleHold("canvas focus lost");
        StopMiddleMouseRotation("canvas focus lost", false);
        ClearTrackedKeyStates("canvas focus lost");
        return 0;
    }
    if (uMsg == WM_ACTIVATEAPP && wParam == FALSE) {
        StopHeldKeyboardPan();
        StopHeldKeyboardRotation("application deactivated");
        StopClassicAngleHold("application deactivated");
        StopMiddleMouseRotation("application deactivated", false);
        ClearTrackedKeyStates("application deactivated");
        return 0;
    }
    if (uMsg == WM_CAPTURECHANGED) {
        // SC4 drops the capture when a left or right button comes up, even with the
        // middle button held, so the next move takes it back. If another window took
        // the mouse or the button is up, the release may never reach this filter.
        const HWND newCapture = reinterpret_cast<HWND>(lParam);
        if (IsMiddleMouseRotationActive()
            && newCapture != g_CapturedMouseWindow
            && (newCapture != NULL || !IsPhysicalKeyDown(VK_MBUTTON))) {
            StopMiddleMouseRotation("mouse capture lost", true);
        }
        return 0;
    }

    if (RouteMouseInputToVisiblePluginWindow(hWnd, uMsg, wParam, lParam, handled)) {
        return 0;
    }

    if (!IsModernInputActive()) {
        return 0;
    }

    ObserveCanvasInputTransition(uMsg, wParam);

    if (IsEnterInput(uMsg, wParam)) {
        cIGZWin* focusedTextEdit = nullptr;
        if (IsTextEditFocused(&focusedTextEdit)) {
            LogTextInputPassThrough(uMsg, wParam, focusedTextEdit, "canvas WinProc filter (unhandled)");
        }
    }
    if (IsEscapeInput(uMsg, wParam)) {
        cIGZWin* focusedWindow = nullptr;
        IsTextEditFocused(&focusedWindow);
        LogEscapePassThrough(uMsg, focusedWindow, "canvas WinProc filter");
        if (HasOwnedKeyboardPanKey()) {
            StopHeldKeyboardPan();
        }
        if (HasOwnedKeyboardRotationKey()) {
            StopHeldKeyboardRotation("Escape key");
        }
    }

    const bool resumesCameraInteraction = uMsg == WM_LBUTTONDOWN
        || uMsg == WM_RBUTTONDOWN
        || uMsg == WM_MBUTTONDOWN
        || uMsg == WM_MOUSEWHEEL
        || uMsg == WM_KEYDOWN
        || uMsg == WM_SYSKEYDOWN;
    if (resumesCameraInteraction && g_CameraController.IsSavePreviewNormalizationActive()) {
        g_CameraController.EndSavePreviewNormalization();
    }

    const bool isKeyboardInputMessage = uMsg == WM_KEYDOWN
        || uMsg == WM_SYSKEYDOWN
        || uMsg == WM_KEYUP
        || uMsg == WM_SYSKEYUP;
    const bool isKeyboardCharacterMessage = uMsg == WM_CHAR || uMsg == WM_SYSCHAR;

    if (isKeyboardInputMessage && IsClassicAngleVirtualKey(wParam)) {
        const bool cameraFocus = IsCameraKeyboardFocus();
        if (ShouldCaptureClassicAngleKeyInFocus(wParam, cameraFocus)) {
            // The low-level hook normally owns the hold, and consuming this message is
            // what stops the default window procedure from opening the system menu. The
            // hold is only managed here if the hook is gone: Windows removes a hook
            // silently after repeated timeouts.
            const bool pressed = uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN;
            const bool hookMissedPress = pressed && !HasOwnedClassicAngleKey() && IsPhysicalKeyDown(VK_MENU);
            const bool heldWithoutHook = !pressed && IsKeyOwnedBy(wParam, KeyOwner::ModernCameraClassicAngle);
            if (hookMissedPress || heldWithoutHook) {
                HandleClassicAngleKeyState(wParam, pressed);
            }
            handled = true;
            return 0;
        }
        if ((uMsg == WM_KEYUP || uMsg == WM_SYSKEYUP) && HasOwnedClassicAngleKey()) {
            HandleClassicAngleKeyState(wParam, false);
            if (cameraFocus) {
                handled = true;
            }
            return 0;
        }
    }
    else if (isKeyboardInputMessage && IsKeyboardRotationVirtualKey(wParam)) {
        const bool cameraFocus = IsCameraKeyboardFocus();
        if (ShouldCaptureKeyboardRotationKeyInFocus(wParam, cameraFocus)) {
            HandleKeyboardRotationKeyState(wParam, uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN);
            handled = true;
            return 0;
        }
        if ((uMsg == WM_KEYUP || uMsg == WM_SYSKEYUP) && HasOwnedKeyboardRotationKey()) {
            HandleKeyboardRotationKeyState(wParam, false);
            if (cameraFocus) {
                handled = true;
            }
            return 0;
        }
        StopHeldKeyboardRotation("canvas keyboard capture interrupted");
    }
    else if (isKeyboardInputMessage && IsCapturedKeyboardPanVirtualKey(wParam)) {
        if (IsCommandShortcutModifierDown()) {
            LogKeyboardShortcutPassThrough(wParam, "canvas WinProc filter");
            if (HasOwnedKeyboardPanKey()) {
                StopHeldKeyboardPan();
            }
        }
        else {
            const bool cameraFocus = IsCameraKeyboardFocus();
            if (ShouldCaptureKeyboardPanKeyInFocus(wParam, cameraFocus)) {
                HandleKeyboardPanKeyState(wParam, uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN);
                handled = true;
                return 0;
            }
            if ((uMsg == WM_KEYUP || uMsg == WM_SYSKEYUP) && HasOwnedKeyboardPanKey()) {
                HandleKeyboardPanKeyState(wParam, false);
                if (cameraFocus) {
                    handled = true;
                }
                return 0;
            }
            StopHeldKeyboardPan();
        }
    }
    else if (isKeyboardCharacterMessage && IsWASDCharacter(wParam)) {
        const bool cameraFocus = IsCameraKeyboardFocus();
        if (ShouldCaptureKeyboardPanKeyInFocus(wParam, cameraFocus)) {
            handled = true;
            return 0;
        }
    }
    else if (isKeyboardCharacterMessage && IsQERotationCharacter(wParam)) {
        const bool cameraFocus = IsCameraKeyboardFocus();
        const WPARAM rotationKey = wParam == 'q' ? 'Q' : (wParam == 'e' ? 'E' : wParam);
        if (ShouldCaptureKeyboardRotationKeyInFocus(rotationKey, cameraFocus)) {
            handled = true;
            return 0;
        }
    }
    else if (isKeyboardCharacterMessage && ShouldConsumeBookmarkCharacter(wParam)) {
        handled = true;
        return 0;
    }

    switch (uMsg) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        const bool isRepeat = (lParam & (1 << 30)) != 0;

        if (HandleBookmarkHotkey(wParam, isRepeat)) {
            handled = true;
            return 0;
        }

        if (IsManualCameraDumpHotkey(wParam)
            && !isRepeat
            && IsCameraKeyboardFocus()) {
            log.WriteLine(LogLevel::Info, "Canvas WinProc Filter: Ctrl+Alt+F8 pressed, dumping camera info.");
            g_CameraController.DumpCameraInfo("Ctrl+Alt+F8 hotkey");
            if (g_CameraController.ShowCameraDumpConfirmation()) {
                StartCameraDumpConfirmationTimer();
            }
            handled = true;
            return 0;
        }

        break;
    }
    case WM_LBUTTONDOWN: {
        const POINT cursor = MakePointFromLParam(lParam);
        LogMouseButtonEvent("WM_LBUTTONDOWN (Left Mouse Down)", cursor);
        break;
    }
    case WM_LBUTTONUP: {
        const POINT cursor = MakePointFromLParam(lParam);
        LogMouseButtonEvent("WM_LBUTTONUP (Left Mouse Up)", cursor);
        HandleNativeUICornerClick(cursor);
        break;
    }
    case WM_RBUTTONDOWN: {
        LogMouseButtonEvent("WM_RBUTTONDOWN (Right Mouse Down)", MakePointFromLParam(lParam));
        StopHeldKeyboardPan();
        StopHeldKeyboardRotation("right mouse scrolling started");
        break;
    }
    case WM_RBUTTONUP: {
        LogMouseButtonEvent("WM_RBUTTONUP (Right Mouse Up)", MakePointFromLParam(lParam));
        break;
    }
    case WM_MBUTTONDOWN: {
        if (!g_IsModernCamEnabled) {
            break;
        }

        log.WriteLine(LogLevel::Verbose, "Canvas WinProc Filter: WM_MBUTTONDOWN (Middle Mouse Down)");
        if (!hWnd) {
            log.WriteLine(LogLevel::Warning, "Middle mouse camera rotation ignored because the canvas window is unavailable.");
            break;
        }
        if (!ClaimKey(VK_MBUTTON, KeyOwner::ModernCameraRotation)) {
            log.WriteLine(LogLevel::Verbose, "Middle mouse camera rotation ignored because MButton is owned by another feature.");
            break;
        }
        g_LastMousePos = MakePointFromLParam(lParam);
        g_RotationCursorOrigin = g_LastMousePos;
        g_RotationCursorWarped = false;
        g_PendingMouseYawDelta = 0.0f;
        g_PendingMousePitchDelta = 0.0f;
        const bool beganRotation = g_CameraController.BeginRotationGesture();
        g_CapturedMouseWindow = hWnd;
        SetCapture(hWnd);
        SetRotationCursorVisible(false);
        KillIdleTimer();
        log.WriteLine(
            LogLevel::Verbose,
            std::string("Middle mouse camera rotation started. BeginResult:")
            + (beganRotation ? "true" : "false")
            + " CaptureWindow:" + std::to_string(reinterpret_cast<intptr_t>(hWnd)));
        handled = true;
        return 0;
    }
    case WM_MBUTTONUP: {
        if (!g_IsModernCamEnabled) {
            break;
        }

        log.WriteLine(LogLevel::Verbose, "Canvas WinProc Filter: WM_MBUTTONUP (Middle Mouse Up)");
        StopMiddleMouseRotation("middle mouse released", true);
        handled = true;
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (g_IsModernCamEnabled && IsMiddleMouseRotationActive()) {
            // The release was missed if the button is already up on this move.
            if ((wParam & MK_MBUTTON) == 0 && !IsPhysicalKeyDown(VK_MBUTTON)) {
                StopMiddleMouseRotation("middle mouse release not observed", true);
                break;
            }
            if (GetCapture() != g_CapturedMouseWindow) {
                SetCapture(g_CapturedMouseWindow);
            }

            const POINT mousePos = MakePointFromLParam(lParam);
            const int deltaX = mousePos.x - g_LastMousePos.x;
            const int deltaY = mousePos.y - g_LastMousePos.y;

            if (deltaX == 0 && deltaY == 0) {
                handled = true;
                return 0;
            }

            float yawDelta = static_cast<float>(deltaX) * kMouseRotationSensitivity * g_Settings.rotationSensitivity;
            float pitchDelta = static_cast<float>(deltaY) * kMouseRotationSensitivity * g_Settings.rotationSensitivity;
            if (g_Settings.invertVertical) {
                pitchDelta = -pitchDelta;
            }

            // Applied on the next camera frame. A rotation step can rebuild the whole
            // render grid, and handling each move here could keep the message drain
            // busy long enough to starve drawing.
            g_PendingMouseYawDelta += yawDelta;
            g_PendingMousePitchDelta += pitchDelta;

            g_LastMousePos = mousePos;
            RecenterRotationCursor(hWnd);
            handled = true;
            return 0;
        }
        break;
    }
    case WM_MOUSEWHEEL: {
        if (!g_IsModernCamEnabled) {
            break;
        }
        // Stepped zoom leaves the wheel to SC4. The Alt hold keeps its own Classic steps.
        if (!g_Settings.smoothWheelZoom && !g_CameraController.IsClassicAngleHoldActive()) {
            break;
        }

        short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        log.WriteLineLazy(LogLevel::Verbose, [&]() {
            return "Canvas WinProc Filter: WM_MOUSEWHEEL (Delta: " + std::to_string(zDelta) + ")";
        });

        POINT cursor = MakePointFromLParam(lParam);
        if (!hWnd) {
            log.WriteLine(LogLevel::Warning, "Mouse wheel hit-test: canvas window is unavailable; using camera zoom.");
        }
        else if (!ScreenToClient(hWnd, &cursor)) {
            log.WriteLine(LogLevel::Warning, "Mouse wheel hit-test: failed to convert screen coordinates; using camera zoom.");
        }
        else {
            NativeWheelHitTest hitTest = HitTestNativeWheelTarget(cursor.x, cursor.y);
            log.WriteLineLazy(LogLevel::Verbose, [&]() {
                return std::string("Mouse wheel hit-test: X:") + std::to_string(cursor.x)
                    + " Y:" + std::to_string(cursor.y)
                    + " Resolved:" + (hitTest.resolved ? "true" : "false")
                    + " OverView3D:" + (hitTest.overView3D ? "true" : "false")
                    + " TargetIsView3D:" + (hitTest.targetIsView3D ? "true" : "false")
                    + " TargetIsView3DSurface:" + (hitTest.targetIsView3DSurface ? "true" : "false")
                    + " InsideView3DRect:" + (hitTest.insideView3DRect ? "true" : "false")
                    + " TargetSource:" + hitTest.targetSource
                    + " Target:{" + hitTest.targetDescription + "}"
                    + " GlobalTarget:{" + hitTest.globalTargetDescription + "}"
                    + " MainTarget:{" + hitTest.mainTargetDescription + "}"
                    + " ParentTarget:{" + hitTest.parentTargetDescription + "}"
                    + " View3D:{" + hitTest.viewDescription + "}";
            });

            if (hitTest.resolved && !hitTest.overView3D) {
                log.WriteLine(LogLevel::Verbose, "Mouse wheel hit-test: passing wheel through to native SC4 UI.");
                break;
            }
        }

        // Kept fractional: rounding could turn a small high-resolution wheel delta
        // into zero, which would hand the wheel to SC4's own stepped zoom.
        // The Alt hold moves between Classic zoom levels, one for each notch of the wheel.
        bool zoomChanged = false;
        const float wheelDelta = static_cast<float>(zDelta) * (g_Settings.invertWheelZoom ? -1.0f : 1.0f);
        const float adjustedZoomDelta = wheelDelta * g_Settings.zoomSensitivity;
        const bool zoomHandled = g_CameraController.IsClassicAngleHoldActive()
            ? g_CameraController.StepClassicZoom(wheelDelta / static_cast<float>(WHEEL_DELTA), zoomChanged)
            : g_CameraController.ZoomByWheel(adjustedZoomDelta, zoomChanged);
        if (zoomHandled) {
            if (zoomChanged) {
                log.WriteLine(LogLevel::Verbose, "Camera zoom changed: scheduling redraw sequence.");
                StartIdleTimer(kZoomIdleRedrawDelayMs);
            }
            handled = true;
            return 0;
        }

        break;
    }
    }

    return 0;
}

/// <summary>
/// Delivers one camera frame per framework tick. The object lives for the whole process
/// and exists only on the tick list, so it needs no service registration.
/// </summary>
class cSC4ModernCameraTickService : public cIGZSystemService
{
public:
    /// <summary>Resolves the service and base COM interfaces.</summary>
    bool QueryInterface(uint32_t riid, void** ppvObj) override
    {
        if (riid == GZIID_cIGZSystemService) {
            *ppvObj = static_cast<cIGZSystemService*>(this);
            AddRef();
            return true;
        }
        if (riid == GZIID_cIGZUnknown) {
            *ppvObj = static_cast<cIGZUnknown*>(this);
            AddRef();
            return true;
        }

        *ppvObj = nullptr;
        return false;
    }

    /// <summary>Counts a reference. The static object is never deleted.</summary>
    uint32_t AddRef() override { return ++m_RefCount; }

    /// <summary>Releases a reference. The static object is never deleted.</summary>
    uint32_t Release() override { return m_RefCount > 0 ? --m_RefCount : 0; }

    /// <summary>Returns the identifier the tick list uses to detect duplicates.</summary>
    uint32_t GetServiceID() override { return m_ServiceID; }

    /// <summary>Replaces the service identifier.</summary>
    cIGZSystemService* SetServiceID(uint32_t serviceID) override
    {
        m_ServiceID = serviceID;
        return this;
    }

    /// <summary>Returns the service priority.</summary>
    int32_t GetServicePriority() override { return kCameraTickPriority; }

    /// <summary>Returns the stored running state.</summary>
    bool IsServiceRunning() override { return m_Running; }

    /// <summary>Stores the running state.</summary>
    cIGZSystemService* SetServiceRunning(bool running) override
    {
        m_Running = running;
        return this;
    }

    /// <summary>Requires no framework initialization.</summary>
    bool Init() override { return true; }

    /// <summary>Requires no framework shutdown.</summary>
    bool Shutdown() override { return true; }

    /// <summary>Runs one camera frame.</summary>
    bool OnTick(uint32_t) override
    {
        RunGuardedNativeCallback("Camera frame", RunCameraFrame);
        return true;
    }

    /// <summary>Does nothing; the camera only moves during active ticks.</summary>
    bool OnIdle(uint32_t) override { return true; }

    /// <summary>Returns the tick order relative to message delivery and drawing.</summary>
    int32_t GetServiceTickPriority() override { return kCameraTickPriority; }

private:
    // Current reference count held by the framework's tick list.
    uint32_t m_RefCount = 0;
    // Identifier compared by the framework's tick list.
    uint32_t m_ServiceID = kCameraTickServiceID;
    // Running state stored for the service interface.
    bool m_Running = true;
};

// Process-lifetime camera frame service.
cSC4ModernCameraTickService g_CameraTickService;

// Plugin entry point that receives SC4 lifecycle messages and canvas input.
class cSC4ModernCameraDirector : public cRZMessage2COMDirector, public cIGZWinProcFilterW32
{
public:
    // Initializes the COM director and exposes canvas-filter callbacks.
    cSC4ModernCameraDirector()
        : mpCanvasW32(nullptr),
          m_FrameworkHookRegistered(false)
    {
        AddRef();
        g_RegisterCanvasWinProcFilterCallback = [this]() { RegisterCanvasWinProcFilter(); };
        g_UnregisterCanvasWinProcFilterCallback = [this]() { UnregisterCanvasWinProcFilter(); };
    }

    // Resolve COM multiple inheritance ambiguity
    // Resolves the inherited COM reference-count method.
    uint32_t AddRef() override { return cRZMessage2COMDirector::AddRef(); }

    // Resolves the inherited COM reference-release method.
    uint32_t Release() override { return cRZMessage2COMDirector::Release(); }

    // Resolves COM interfaces through the message-director base class.
    bool QueryInterface(uint32_t riid, void** ppvObj) override
    {
        return cRZMessage2COMDirector::QueryInterface(riid, ppvObj);
    }

    // Returns this plugin's fixed COM director identifier.
    uint32_t GetDirectorID() const override
    {
        return 0x8C4B3A11;
    }

    // Initializes the plugin and subscribes to game notifications on a supported build.
    bool OnStart(cIGZCOM* pCOM) override
    {
        try {
            Logger::GetInstance().Initialize(PluginPaths::GetLogPath().string());
        }
        catch (const std::exception&) {
            RunGuardedNativeCallback("Fallback log setup", []() {
                std::error_code ignored;
                std::filesystem::create_directories("SC4-ModernCamera", ignored);
                Logger::GetInstance().Initialize("SC4-ModernCamera/SC4-ModernCamera.log");
            });
        }

        if (!CheckGameVersion()) {
            Logger::GetInstance().WriteLine(
                LogLevel::Error,
                "Plugin startup disabled because this SimCity 4 build is unsupported.");
            return true;
        }

        Logger::GetInstance().WriteLine(
            LogLevel::Info,
            std::string("Plugin v") + PluginVersion::String + " loaded. Waiting for city to load...");

        try {
            g_Settings.Load(PluginPaths::GetSettingsPath());
        }
        catch (const std::exception& exception) {
            Logger::GetInstance().WriteLine(
                LogLevel::Error,
                std::string("Failed to initialize settings: ") + exception.what());
        }

        try {
            if (!g_CameraBookmarks.Initialize(PluginPaths::GetBookmarksPath())) {
                Logger::GetInstance().WriteLine(LogLevel::Error, "Camera Bookmarks: failed to initialize persistent storage.");
            }
        }
        catch (const std::exception& exception) {
            Logger::GetInstance().WriteLine(
                LogLevel::Error,
                std::string("Camera Bookmarks: failed to initialize persistent storage: ") + exception.what());
        }

        g_IsModernCamEnabled = g_Settings.cameraMode == CameraMode::Modern;
        g_WindowManager.SetCallbacks(SC4WindowManagerCallbacks{
            ApplyModernCameraEnabled,
            ResetCameraToNativeView,
            ApplyInputSettingsChange,
            ApplyDebugLoggingChange,
            RestoreWorldFocusAfterSettingsClosed,
            });

        ApplyDebugLoggingChange();

        if (!mpFrameWork) {
            Logger::GetInstance().WriteLine(
                LogLevel::Error,
                "Framework lifecycle hook is unavailable because cIGZFrameWork is null.");
        }
        else if (!m_FrameworkHookRegistered) {
            m_FrameworkHookRegistered = mpFrameWork->AddHook(static_cast<cIGZFrameWorkHooks*>(this));
            Logger::GetInstance().WriteLine(
                m_FrameworkHookRegistered ? LogLevel::Info : LogLevel::Error,
                m_FrameworkHookRegistered
                    ? "Registered framework lifecycle hook."
                    : "Failed to register framework lifecycle hook.");

            g_CameraTickRegistered = mpFrameWork->AddToTick(&g_CameraTickService);
            Logger::GetInstance().WriteLine(
                g_CameraTickRegistered ? LogLevel::Info : LogLevel::Warning,
                g_CameraTickRegistered
                    ? "Registered camera frame tick."
                    : "Failed to register camera frame tick; using a timer while a city is loaded.");
        }

        if (m_FrameworkHookRegistered) {
            Logger& logger = Logger::GetInstance();
            logger.WriteLine(LogLevel::Info, "Starting asynchronous logger.");
            if (logger.StartAsync()) {
                logger.WriteLine(LogLevel::Info, "Asynchronous logger started.");
            }
            else {
                logger.WriteLine(LogLevel::Error, "Failed to start asynchronous logger; continuing with synchronous logging.");
            }
        }

        Logger::GetInstance().WriteLine(
            LogLevel::Info,
            std::string("Modern Camera Enabled: ") + (g_IsModernCamEnabled ? "true" : "false")
            + (g_IsModernCamEnabled ? "" : " (Classic native camera mode)"));

        if (!RegisterNotifications(this)) {
            Logger::GetInstance().WriteLine(LogLevel::Error, "Failed to subscribe to the required notifications.");
        }

        return true;
    }

    // Prevents the DLL from unloading while the asynchronous logger can still execute plugin code.
    bool CanUnloadNow() override
    {
        return Logger::GetInstance().CanUnloadNow()
            && cRZMessage2COMDirector::CanUnloadNow();
    }

    // Stops and drains the asynchronous logger before application services are torn down.
    bool PreAppShutdown() override
    {
        Logger& logger = Logger::GetInstance();
        logger.WriteLine(LogLevel::Info, "Framework lifecycle: PreAppShutdown. Draining camera bookmarks and asynchronous logger.");
        if (g_CameraTickRegistered && mpFrameWork) {
            mpFrameWork->RemoveFromTick(&g_CameraTickService);
            g_CameraTickRegistered = false;
        }
        g_CameraController.GetCityIcons().Clear();
        g_WindowManager.ShutdownReleaseNotes();
        g_CameraBookmarks.Shutdown();
        logger.Shutdown();
        return true;
    }

    // Stops plugin writers before an abortive framework shutdown can unload code.
    bool AbortiveQuit() override
    {
        g_WindowManager.ShutdownReleaseNotes();
        g_CameraBookmarks.Shutdown();
        Logger::GetInstance().RequestAbortiveStop();
        return true;
    }

    // Handles city lifecycle and save notifications from SimCity 4.
    bool DoMessage(cIGZMessage2* pMsg) override
    {
        RunGuardedNativeCallback("Director message", [&]() { HandleMessage(pMsg); });
        return true;
    }

    // Delegates canvas Windows messages to the Modern Camera input router.
    LRESULT FilterMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, bool& handled) override
    {
        LRESULT result = 0;
        RunGuardedNativeCallback("Canvas input", [&]() {
            result = HandleCanvasMouseMessage(hWnd, uMsg, wParam, lParam, handled);
        });
        return result;
    }

private:
    /// <summary>
    /// Applies one city lifecycle, save, or occupant notification.
    /// </summary>
    /// <param name="pMsg">Message delivered by the SimCity 4 message server.</param>
    void HandleMessage(cIGZMessage2* pMsg)
    {
        uint32_t msgType = pMsg->GetType();

        if (msgType == kSC4MessageInsertOccupant || msgType == kSC4MessageRemoveOccupant) {
            // Occupants that arrive while a city loads are collected when it finishes.
            if (g_IsCityLoaded) {
                auto* standardMsg = static_cast<cIGZMessage2Standard*>(pMsg);
                auto* occupant = static_cast<cISC4Occupant*>(standardMsg->GetVoid1());
                const auto occupantType = static_cast<uint32_t>(standardMsg->GetData3());
                if (msgType == kSC4MessageInsertOccupant) {
                    g_CameraController.GetCityIcons().OnOccupantInserted(occupant, occupantType);
                }
                else {
                    g_CameraController.GetCityIcons().OnOccupantRemoved(occupant, occupantType);
                }
            }
        }
        else if (msgType == kSC4MessagePostCityInit) {
            Logger::GetInstance().WriteLine(LogLevel::Info, "City Loaded! Activating input handlers...");
            if (!ActivateCurrentBookmarkCity()) {
                Logger::GetInstance().WriteLine(
                    LogLevel::Warning,
                    "Camera Bookmarks: active city could not be activated; bookmark controls remain empty for this city.");
            }
            g_WindowManager.OnCityLoaded(g_Settings);
            g_IsCityLoaded = true;
            g_CameraController.GetCityIcons().TrackExistingIcons();
            g_IsNativeUIHidden = false;
            g_SavePreviewRestorePending = false;
            ResetInputState();
            if (!g_CameraTickRegistered) {
                g_TimerManager.Start(
                    RuntimeTimer::CameraFrameFallback,
                    kCameraFrameFallbackMs,
                    CameraFrameFallbackTimerProc);
            }
            InstallMinimizeUIHook();
            RegisterCanvasWinProcFilter();
            if (g_IsModernCamEnabled) {
                InstallKeyboardHook();
                g_CameraController.CaptureNativeCameraState("modern camera city load");
                InstallSetScrollingHook();
            }
            else {
                Logger::GetInstance().WriteLine(
                    LogLevel::Info,
                    "Classic camera active; Modern input hooks are disabled and native SC4 input continues.");
                FocusView3DForNativeInput("Classic camera startup focus handoff");
                ScheduleView3DFocusRestore(false, "Classic camera startup focus restore");
            }
        }
        else if (msgType == kSC4MessagePreSave) {
            Logger::GetInstance().WriteLine(LogLevel::Info, "Pre-save notification received.");
            if (g_IsModernCamEnabled && g_IsCityLoaded
                && !g_CameraController.BeginSavePreviewNormalization()
                && !g_CameraController.IsSavePreviewNormalizationActive()) {
                Logger::GetInstance().WriteLine(LogLevel::Warning, "Save Preview: failed to normalize camera at pre-save.");
            }
        }
        else if (msgType == kSC4MessagePostSave) {
            // The save preview is finished, but the file is still being written.
            g_SavePreviewRestorePending = g_IsCityLoaded && g_CameraController.IsSavePreviewNormalizationActive();
        }
        else if (msgType == kSC4MessagePreCityShutdown) {
            Logger::GetInstance().WriteLine(LogLevel::Info, "City Shutting Down. Deactivating input handlers...");
            g_WindowManager.OnCityShutdown();
			g_CameraBookmarks.DeactivateCity();
            g_SavePreviewRestorePending = false;
            g_CameraController.AbandonSavePreviewNormalization();
            g_CameraController.GetCityIcons().Clear();
            g_IsCityLoaded = false;
            g_IsUDriveItActive = false;
            g_TimerManager.Stop(RuntimeTimer::CameraFrameFallback);
            UninstallSetScrollingHook();
            UninstallMinimizeUIHook();
            ResetInputState();
            UninstallKeyboardHook();
            UnregisterCanvasWinProcFilter();
        }
    }

    // Attaches this director as a filter on the Windows canvas procedure.
    void RegisterCanvasWinProcFilter()
    {
        if (mpCanvasW32) {
            return;
        }

        cIGZGraphicSystemPtr pGraphicSystem;
        if (!pGraphicSystem) {
            Logger::GetInstance().WriteLine(LogLevel::Error, "Failed to get cIGZGraphicSystem for canvas WinProc filter.");
            return;
        }

        cIGZCanvasW32* canvasW32 = nullptr;
        if (!pGraphicSystem->GetCanvas(GZIID_cIGZCanvasW32, reinterpret_cast<void**>(&canvasW32)) || !canvasW32) {
            Logger::GetInstance().WriteLine(LogLevel::Error, "Failed to get cIGZCanvasW32 for canvas WinProc filter.");
            return;
        }

        if (canvasW32->AddWinProcFilter(this, true)) {
            mpCanvasW32 = canvasW32;
            Logger::GetInstance().WriteLine(LogLevel::Info, "Registered canvas WinProc filter.");
        }
        else {
            Logger::GetInstance().WriteLine(LogLevel::Error, "Failed to register canvas WinProc filter.");
            canvasW32->Release();
        }
    }

    // Detaches this director from the Windows canvas procedure.
    void UnregisterCanvasWinProcFilter()
    {
        if (!mpCanvasW32) {
            return;
        }

        mpCanvasW32->AddWinProcFilter(this, false);
        mpCanvasW32->Release();
        mpCanvasW32 = nullptr;
        Logger::GetInstance().WriteLine(LogLevel::Info, "Unregistered canvas WinProc filter.");
    }

    cIGZCanvasW32* mpCanvasW32;
    // Records whether this director is subscribed to framework lifecycle callbacks.
    bool m_FrameworkHookRegistered;
};

// Returns the singleton COM director exported by this plugin DLL.
cRZCOMDllDirector* RZGetCOMDllDirector() {
    // Function-local instance retained for the DLL's lifetime.
    static cSC4ModernCameraDirector sDirector;
    return &sDirector;
}
