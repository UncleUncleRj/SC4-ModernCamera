#include "cIGZCanvasW32.h"
#include "cIGZGraphicSystem.h"
#include "cIGZMessage2Standard.h"
#include "cIGZMessageServer2.h"
#include "cIGZWin.h"
#include "cIGZWinMgr.h"
#include "cIGZWinProcFilterW32.h"
#include "cISC4App.h"
#include "cISC4View3DWin.h"
#include "cRZMessage2COMDirector.h"
#include "GZServPtrs.h"
#include "Logger.h"
#include "PluginPaths.h"
#include "PluginSettings.h"
#include "PluginVersion.h"
#include "SC4CameraController.h"
#include "SC4VersionDetection.h"
#include "SC4WindowManager.h"
#include <Windows.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <sstream>
#include <string>

static constexpr uint32_t kSC4MessagePostCityInit = 0x26D31EC1;
static constexpr uint32_t kSC4MessagePreCityShutdown = 0x26D31EC2;
static constexpr uint32_t kSC4MessagePreSave = 0x26C63343;
static constexpr uint32_t kGZWin_WinSC4App = 0x6104489A;
static constexpr uint32_t kGZWin_SC4View3DWin = 0x9A47B417;
static constexpr uint32_t kGZWin_SC4View3DSurface = 0x6A5E44B6;
static constexpr float kMouseRotationSensitivity = 0.005f;
static constexpr UINT kPanIdleRedrawDelayMs = 1000;
static constexpr UINT kKeyboardPanIdleRedrawDelayMs = 1000;
static constexpr UINT kKeyboardPanTickMs = 33;
static constexpr float kKeyboardPanStepsPerTick = 0.25f;
static constexpr float kKeyboardPanBoostMultiplier = 3.0f;
static constexpr UINT kZoomIdleRedrawDelayMs = 1500;
static constexpr UINT kHighPeriodicRedrawDelayMs = 1000;
static constexpr UINT kExtremePeriodicRedrawDelayMs = 100;
static constexpr UINT kCameraDumpConfirmationDelayMs = 2500;
static constexpr UINT kNativeCameraBaselineDelayMs = 1000;
static constexpr UINT kDumpCameraInfoKey = VK_F8;

// Global State
bool g_IsCityLoaded = false;
bool g_IsModernCamEnabled = true;

// Cinematic Camera State
bool g_IsMiddleMouseDown = false;
bool g_IsRightMouseDown = false;
bool g_WASDKeyWDown = false;
bool g_WASDKeyADown = false;
bool g_WASDKeySDown = false;
bool g_WASDKeyDDown = false;
bool g_WASDPanMovedSinceKeyDown = false;
POINT g_LastMousePos = { 0, 0 };
HWND g_CapturedMouseWindow = NULL;
HHOOK g_KeyboardHook = NULL;
SC4CameraController g_CameraController;
PluginSettings g_Settings;
SC4WindowManager g_WindowManager;

UINT_PTR g_IdleTimerID = 0;
UINT_PTR g_PeriodicRedrawTimerID = 0;
UINT_PTR g_KeyboardPanTimerID = 0;
UINT_PTR g_CameraDumpConfirmationTimerID = 0;
UINT_PTR g_NativeCameraBaselineTimerID = 0;

VOID CALLBACK RedrawTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
VOID CALLBACK PeriodicRedrawTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
VOID CALLBACK KeyboardPanTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
VOID CALLBACK ClearCameraDumpConfirmationTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
VOID CALLBACK NativeCameraBaselineTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
LRESULT CALLBACK KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam);
void StopHeldWASDMovement(bool scheduleRedraw);

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

std::string FormatWindowID(uint32_t value)
{
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << value;
    return stream.str();
}

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

bool IsUsefulHitTestTarget(cIGZWin* candidate, cIGZWin* mainWindow, cIGZWin* parentWindow)
{
    return candidate && candidate != mainWindow && candidate != parentWindow;
}

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
    result.targetDescription = DescribeGZWindow(targetWindow);
    result.globalTargetDescription = DescribeGZWindow(globalTarget);
    result.mainTargetDescription = DescribeGZWindow(mainTarget);
    result.parentTargetDescription = DescribeGZWindow(parentTarget);
    result.viewDescription = DescribeGZWindow(viewWindow);

    view3D->Release();
    return result;
}

const char* GetRedrawAggressionName()
{
    switch (g_Settings.redrawAggression) {
    case RedrawAggression::Classic:
        return "Classic";
    case RedrawAggression::High:
        return "High";
    case RedrawAggression::Extreme:
        return "Extreme";
    default:
        return "Normal";
    }
}

void KillIdleTimer()
{
    if (g_IdleTimerID != 0) {
        KillTimer(NULL, g_IdleTimerID);
        g_IdleTimerID = 0;
    }

}

void KillPeriodicRedrawTimer()
{
    if (g_PeriodicRedrawTimerID != 0) {
        KillTimer(NULL, g_PeriodicRedrawTimerID);
        g_PeriodicRedrawTimerID = 0;
    }
}

void KillRedrawTimers()
{
    KillIdleTimer();
    KillPeriodicRedrawTimer();
}

void KillKeyboardPanTimer()
{
    if (g_KeyboardPanTimerID != 0) {
        KillTimer(NULL, g_KeyboardPanTimerID);
        g_KeyboardPanTimerID = 0;
    }
}

bool HasHeldWASDKey()
{
    return g_WASDKeyWDown || g_WASDKeyADown || g_WASDKeySDown || g_WASDKeyDDown;
}

void ClearHeldWASDKeys()
{
    g_WASDKeyWDown = false;
    g_WASDKeyADown = false;
    g_WASDKeySDown = false;
    g_WASDKeyDDown = false;
    g_WASDPanMovedSinceKeyDown = false;
    KillKeyboardPanTimer();
}

void StartPeriodicRedrawTimer()
{
    KillPeriodicRedrawTimer();

    if (!g_IsCityLoaded || !g_IsModernCamEnabled || g_IsMiddleMouseDown) {
        return;
    }

    UINT delayMs = 0;
    if (g_Settings.redrawAggression == RedrawAggression::High) {
        delayMs = kHighPeriodicRedrawDelayMs;
    }
    else if (g_Settings.redrawAggression == RedrawAggression::Extreme) {
        delayMs = kExtremePeriodicRedrawDelayMs;
    }

    if (delayMs == 0) {
        return;
    }

    Logger::GetInstance().WriteLine(
        LogLevel::Info,
        "Starting periodic redraw timer: aggression="
        + std::string(GetRedrawAggressionName())
        + " delayMs=" + std::to_string(delayMs));
    g_PeriodicRedrawTimerID = SetTimer(NULL, 0, delayMs, PeriodicRedrawTimerProc);
    if (g_PeriodicRedrawTimerID == 0) {
        Logger::GetInstance().WriteLine(LogLevel::Warning, "Failed to start the periodic redraw timer.");
    }
}

void StartIdleTimer(UINT delayMs)
{
    KillRedrawTimers();

    if (!g_IsCityLoaded || !g_IsModernCamEnabled || g_Settings.redrawAggression == RedrawAggression::Classic) {
        return;
    }

    if (g_Settings.redrawAggression == RedrawAggression::High) {
        delayMs = std::max<UINT>(100, delayMs / 4);
    }
    else if (g_Settings.redrawAggression == RedrawAggression::Extreme) {
        delayMs = std::max<UINT>(25, delayMs / 20);
    }

    Logger::GetInstance().WriteLine(
        LogLevel::Info,
        "Starting idle redraw timer: aggression="
        + std::string(GetRedrawAggressionName())
        + " delayMs=" + std::to_string(delayMs));
    g_IdleTimerID = SetTimer(NULL, 0, delayMs, RedrawTimerProc);
    if (g_IdleTimerID == 0) {
        Logger::GetInstance().WriteLine(LogLevel::Warning, "Failed to start the redraw timer.");
    }
}

void KillCameraDumpConfirmationTimer()
{
    if (g_CameraDumpConfirmationTimerID != 0) {
        KillTimer(NULL, g_CameraDumpConfirmationTimerID);
        g_CameraDumpConfirmationTimerID = 0;
    }
}

void StartCameraDumpConfirmationTimer()
{
    KillCameraDumpConfirmationTimer();
    g_CameraDumpConfirmationTimerID = SetTimer(NULL, 0, kCameraDumpConfirmationDelayMs, ClearCameraDumpConfirmationTimerProc);
}

void KillNativeCameraBaselineTimer()
{
    if (g_NativeCameraBaselineTimerID != 0) {
        KillTimer(NULL, g_NativeCameraBaselineTimerID);
        g_NativeCameraBaselineTimerID = 0;
    }
}

void StartNativeCameraBaselineTimer()
{
    KillNativeCameraBaselineTimer();
    g_NativeCameraBaselineTimerID = SetTimer(NULL, 0, kNativeCameraBaselineDelayMs, NativeCameraBaselineTimerProc);
    if (g_NativeCameraBaselineTimerID == 0) {
        Logger::GetInstance().WriteLine(LogLevel::Warning, "Failed to start native camera baseline timer.");
    }
}

void TriggerCityRedraw() {
    Logger& log = Logger::GetInstance();

    if (!g_IsCityLoaded || !g_IsModernCamEnabled) {
        return;
    }

    log.WriteLine(LogLevel::Info, "Executing forced redraw.");

    if (!g_CameraController.ForceFullRedraw()) {
        log.WriteLine(LogLevel::Warning, "ForceFullRedraw() failed.");
    }
}

POINT MakePointFromLParam(LPARAM lParam)
{
    POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    return point;
}

void LogMouseButtonEvent(const char* name, const POINT& point)
{
    Logger::GetInstance().WriteLine(
        LogLevel::Verbose,
        std::string("Canvas WinProc Filter: ") + name + " at X:" + std::to_string(point.x) + " Y:" + std::to_string(point.y));
}

void ResetInputState()
{
    KillRedrawTimers();
    ClearHeldWASDKeys();
    KillCameraDumpConfirmationTimer();
    KillNativeCameraBaselineTimer();
    g_CameraController.ClearCameraDumpConfirmation();

    if (g_CapturedMouseWindow != NULL && GetCapture() == g_CapturedMouseWindow) {
        ReleaseCapture();
    }

    g_IsMiddleMouseDown = false;
    g_IsRightMouseDown = false;
    g_CapturedMouseWindow = NULL;
    g_CameraController.Reset();
}

void ResetCameraToNativeView()
{
    Logger::GetInstance().WriteLine(LogLevel::Info, "Settings UI: reset camera requested.");
    KillRedrawTimers();
    ClearHeldWASDKeys();
    KillCameraDumpConfirmationTimer();
    KillNativeCameraBaselineTimer();

    if (g_CapturedMouseWindow != NULL && GetCapture() == g_CapturedMouseWindow) {
        ReleaseCapture();
    }

    g_IsMiddleMouseDown = false;
    g_IsRightMouseDown = false;
    g_CapturedMouseWindow = NULL;

    if (g_CameraController.IsSavePreviewNormalizationActive()) {
        g_CameraController.AbandonSavePreviewNormalization();
    }
    if (g_CameraController.BeginSavePreviewNormalization()) {
        g_CameraController.AbandonSavePreviewNormalization();
    }
    g_CameraController.Reset();
    StartPeriodicRedrawTimer();
}

void ApplyModernCameraEnabled(bool enabled)
{
    if (g_IsModernCamEnabled == enabled) {
        return;
    }

    Logger::GetInstance().WriteLine(
        LogLevel::Info,
        std::string("Settings UI: Modern Camera Enabled changed to ") + (enabled ? "true" : "false"));
    g_IsModernCamEnabled = enabled;
    ClearHeldWASDKeys();

    if (!enabled) {
        ResetInputState();
    }
    else {
        TriggerCityRedraw();
        StartPeriodicRedrawTimer();
    }
}

void ApplyRedrawAggressionChange()
{
    Logger::GetInstance().WriteLine(
        LogLevel::Info,
        "Settings UI: redraw aggression changed to "
        + std::string(GetRedrawAggressionName())
        + "; restarting redraw timers.");
    KillRedrawTimers();
    if (g_IsModernCamEnabled && g_Settings.redrawAggression != RedrawAggression::Classic) {
        TriggerCityRedraw();
    }
    StartPeriodicRedrawTimer();
}

void ApplyInputSettingsChange()
{
    Logger::GetInstance().WriteLine(LogLevel::Info, "Settings UI: input settings changed; clearing held input state.");
    StopHeldWASDMovement(true);
}

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

bool IsRightClickScrollingActive()
{
    return g_IsRightMouseDown || ((GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);
}

bool IsWASDVirtualKey(WPARAM key, float& rightSteps, float& forwardSteps)
{
    rightSteps = 0.0f;
    forwardSteps = 0.0f;

    switch (key) {
    case 'W':
        forwardSteps = 1.0f;
        return true;
    case 'S':
        forwardSteps = -1.0f;
        return true;
    case 'A':
        rightSteps = -1.0f;
        return true;
    case 'D':
        rightSteps = 1.0f;
        return true;
    default:
        return false;
    }
}

bool IsWASDCharacter(WPARAM character)
{
    return character == 'W' || character == 'w'
        || character == 'A' || character == 'a'
        || character == 'S' || character == 's'
        || character == 'D' || character == 'd';
}

bool* GetWASDKeyState(WPARAM key)
{
    switch (key) {
    case 'W':
        return &g_WASDKeyWDown;
    case 'A':
        return &g_WASDKeyADown;
    case 'S':
        return &g_WASDKeySDown;
    case 'D':
        return &g_WASDKeyDDown;
    default:
        return nullptr;
    }
}

bool ShouldCaptureWASDKeys()
{
    return g_IsCityLoaded
        && g_IsModernCamEnabled
        && g_Settings.wasdMovement
        && !IsRightClickScrollingActive();
}

void StopHeldWASDMovement(bool scheduleRedraw)
{
    const bool moved = g_WASDPanMovedSinceKeyDown;
    ClearHeldWASDKeys();
    if (scheduleRedraw && moved) {
        StartIdleTimer(kKeyboardPanIdleRedrawDelayMs);
    }
}

bool PanCameraFromWASD(float rightSteps, float forwardSteps, const char* source)
{
    Logger& log = Logger::GetInstance();
    if (!g_CameraController.HasNativeCameraState()) {
        g_CameraController.DumpCameraInfo(source);
    }
    if (g_CameraController.PanByKeyboard(rightSteps, forwardSteps)) {
        log.WriteLine(
            LogLevel::Info,
            "WASD camera pan handled by "
            + std::string(source ? source : "unknown source")
            + ": rightSteps=" + std::to_string(rightSteps)
            + " forwardSteps=" + std::to_string(forwardSteps));
        return true;
    }

    log.WriteLine(
        LogLevel::Warning,
        "WASD camera pan failed in "
        + std::string(source ? source : "unknown source")
        + "; key input was still consumed.");
    return false;
}

bool PanCameraFromHeldWASD()
{
    if (!ShouldCaptureWASDKeys()) {
        StopHeldWASDMovement(true);
        return false;
    }

    float rightSteps = (g_WASDKeyDDown ? 1.0f : 0.0f) - (g_WASDKeyADown ? 1.0f : 0.0f);
    float forwardSteps = (g_WASDKeyWDown ? 1.0f : 0.0f) - (g_WASDKeySDown ? 1.0f : 0.0f);
    if (rightSteps == 0.0f && forwardSteps == 0.0f) {
        StopHeldWASDMovement(true);
        return false;
    }

    const float length = std::sqrt((rightSteps * rightSteps) + (forwardSteps * forwardSteps));
    if (length > 0.0f) {
        const float speedMultiplier = ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0)
            ? kKeyboardPanBoostMultiplier
            : 1.0f;
        const float stepsPerTick = kKeyboardPanStepsPerTick * speedMultiplier;
        rightSteps = (rightSteps / length) * stepsPerTick;
        forwardSteps = (forwardSteps / length) * stepsPerTick;
    }

    if (PanCameraFromWASD(rightSteps, forwardSteps, "held WASD movement tick")) {
        g_WASDPanMovedSinceKeyDown = true;
        return true;
    }
    return false;
}

void StartKeyboardPanTimer()
{
    if (g_KeyboardPanTimerID != 0) {
        return;
    }

    g_KeyboardPanTimerID = SetTimer(NULL, 0, kKeyboardPanTickMs, KeyboardPanTimerProc);
    if (g_KeyboardPanTimerID == 0) {
        Logger::GetInstance().WriteLine(LogLevel::Warning, "Failed to start the WASD movement timer.");
    }
}

void HandleWASDKeyState(WPARAM key, bool pressed)
{
    bool* keyState = GetWASDKeyState(key);
    if (!keyState) {
        return;
    }

    const bool wasAnyKeyHeld = HasHeldWASDKey();
    *keyState = pressed;

    if (pressed) {
        if (!wasAnyKeyHeld) {
            g_WASDPanMovedSinceKeyDown = false;
            PanCameraFromHeldWASD();
        }
        StartKeyboardPanTimer();
    }
    else if (!HasHeldWASDKey()) {
        StopHeldWASDMovement(true);
    }
}

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
        Logger::GetInstance().WriteLine(LogLevel::Info, "Registered low-level WASD keyboard hook.");
    }
    else {
        Logger::GetInstance().WriteLine(
            LogLevel::Warning,
            "Failed to register low-level WASD keyboard hook. GetLastError="
            + std::to_string(GetLastError()));
    }
}

void UninstallKeyboardHook()
{
    if (g_KeyboardHook == NULL) {
        return;
    }

    if (!UnhookWindowsHookEx(g_KeyboardHook)) {
        Logger::GetInstance().WriteLine(
            LogLevel::Warning,
            "Failed to unregister low-level WASD keyboard hook. GetLastError="
            + std::to_string(GetLastError()));
    }
    else {
        Logger::GetInstance().WriteLine(LogLevel::Info, "Unregistered low-level WASD keyboard hook.");
    }
    g_KeyboardHook = NULL;
}

LRESULT CALLBACK KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION) {
        const KBDLLHOOKSTRUCT* keyboard = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        const bool isKeyDown = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
        const bool isKeyUp = wParam == WM_KEYUP || wParam == WM_SYSKEYUP;

        float rightSteps = 0.0f;
        float forwardSteps = 0.0f;
        if (keyboard
            && (isKeyDown || isKeyUp)
            && IsWASDVirtualKey(keyboard->vkCode, rightSteps, forwardSteps)) {
            DWORD foregroundProcessID = 0;
            GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcessID);
            if (foregroundProcessID == GetCurrentProcessId()) {
                if (ShouldCaptureWASDKeys()) {
                    HandleWASDKeyState(keyboard->vkCode, isKeyDown);
                    return 1;
                }
                if (isKeyUp && HasHeldWASDKey()) {
                    HandleWASDKeyState(keyboard->vkCode, false);
                    return 1;
                }
                StopHeldWASDMovement(true);
            }
        }
    }

    return CallNextHookEx(g_KeyboardHook, nCode, wParam, lParam);
}

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

bool RegisterNotifications(cIGZMessageTarget2* target)
{
    cIGZMessageServer2Ptr pMsgServ;
    if (pMsgServ) {
        const bool addedPostCityInit = pMsgServ->AddNotification(target, kSC4MessagePostCityInit);
        const bool addedPreCityShutdown = pMsgServ->AddNotification(target, kSC4MessagePreCityShutdown);
        const bool addedPreSave = pMsgServ->AddNotification(target, kSC4MessagePreSave);

        return addedPostCityInit && addedPreCityShutdown && addedPreSave;
    }

    return false;
}

VOID CALLBACK RedrawTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime) {
    Logger& log = Logger::GetInstance();
    if (idEvent == g_IdleTimerID && g_IdleTimerID != 0) {
        log.WriteLine(LogLevel::Verbose, "Camera redraw timer fired.");
        KillTimer(NULL, g_IdleTimerID);
        g_IdleTimerID = 0;

        TriggerCityRedraw();
        StartPeriodicRedrawTimer();
    }
}

VOID CALLBACK PeriodicRedrawTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime) {
    if (idEvent == g_PeriodicRedrawTimerID && g_PeriodicRedrawTimerID != 0) {
        KillTimer(NULL, g_PeriodicRedrawTimerID);
        g_PeriodicRedrawTimerID = 0;

        if (!g_IsMiddleMouseDown) {
            Logger::GetInstance().WriteLine(LogLevel::Verbose, "Periodic camera redraw timer fired.");
            TriggerCityRedraw();
        }

        StartPeriodicRedrawTimer();
    }
}

VOID CALLBACK KeyboardPanTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime) {
    if (idEvent == g_KeyboardPanTimerID && g_KeyboardPanTimerID != 0) {
        if (!HasHeldWASDKey()) {
            StopHeldWASDMovement(true);
            return;
        }

        PanCameraFromHeldWASD();
    }
}

VOID CALLBACK ClearCameraDumpConfirmationTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime) {
    if (idEvent == g_CameraDumpConfirmationTimerID && g_CameraDumpConfirmationTimerID != 0) {
        KillCameraDumpConfirmationTimer();
        g_CameraController.ClearCameraDumpConfirmation();
    }
}

VOID CALLBACK NativeCameraBaselineTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime) {
    if (idEvent == g_NativeCameraBaselineTimerID && g_NativeCameraBaselineTimerID != 0) {
        KillNativeCameraBaselineTimer();
        if (g_IsCityLoaded) {
            g_CameraController.DumpCameraInfo("post-city-init native baseline (1000ms delayed)");
        }
    }
}

LRESULT HandleCanvasMouseMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, bool& handled)
{
    if (!g_IsCityLoaded) {
        return 0;
    }

    Logger& log = Logger::GetInstance();

    const bool isMouseMessage = uMsg == WM_LBUTTONDOWN
        || uMsg == WM_LBUTTONUP
        || uMsg == WM_RBUTTONDOWN
        || uMsg == WM_RBUTTONUP
        || uMsg == WM_MBUTTONDOWN
        || uMsg == WM_MBUTTONUP
        || uMsg == WM_MOUSEMOVE
        || uMsg == WM_MOUSEWHEEL;
    if (g_WindowManager.HasVisibleWindow() && isMouseMessage && !g_IsMiddleMouseDown) {
        // WM_MOUSEWHEEL supplies screen coordinates. Convert them to the
        // canvas coordinates used by the native SC4 window hierarchy.
        POINT cursor = MakePointFromLParam(lParam);
        if (uMsg == WM_MOUSEWHEEL) {
            if (!ScreenToClient(hWnd, &cursor)) {
                return 0;
            }
            if (g_WindowManager.HandleMouseWheel(
                GET_WHEEL_DELTA_WPARAM(wParam), cursor.x, cursor.y,
                reinterpret_cast<intptr_t>(hWnd))) {
                handled = true;
                return 0;
            }
        }

        if (g_WindowManager.IsPointOverVisibleWindow(cursor.x, cursor.y)) {
            return 0;
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

    const bool isWASDKeyMessage = uMsg == WM_KEYDOWN
        || uMsg == WM_SYSKEYDOWN
        || uMsg == WM_KEYUP
        || uMsg == WM_SYSKEYUP;
    const bool isWASDCharMessage = uMsg == WM_CHAR || uMsg == WM_SYSCHAR;

    float rightSteps = 0.0f;
    float forwardSteps = 0.0f;
    if (isWASDKeyMessage && IsWASDVirtualKey(wParam, rightSteps, forwardSteps)) {
        if (ShouldCaptureWASDKeys()) {
            HandleWASDKeyState(wParam, uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN);
            handled = true;
            return 0;
        }
        if ((uMsg == WM_KEYUP || uMsg == WM_SYSKEYUP) && HasHeldWASDKey()) {
            HandleWASDKeyState(wParam, false);
            handled = true;
            return 0;
        }
        StopHeldWASDMovement(true);
    }
    else if (isWASDCharMessage && IsWASDCharacter(wParam)) {
        if (ShouldCaptureWASDKeys()) {
            handled = true;
            return 0;
        }
    }

    switch (uMsg) {
    case WM_KEYDOWN: {
        const bool isRepeat = (lParam & (1 << 30)) != 0;

        if (wParam == kDumpCameraInfoKey && !isRepeat) {
            log.WriteLine(LogLevel::Info, "Canvas WinProc Filter: F8 pressed, dumping camera info.");
            g_CameraController.DumpCameraInfo("F8 hotkey");
            if (g_CameraController.ShowCameraDumpConfirmation()) {
                StartCameraDumpConfirmationTimer();
            }
            handled = true;
            return 0;
        }

        break;
    }
    case WM_LBUTTONDOWN: {
        LogMouseButtonEvent("WM_LBUTTONDOWN (Left Mouse Down)", MakePointFromLParam(lParam));
        break;
    }
    case WM_LBUTTONUP: {
        LogMouseButtonEvent("WM_LBUTTONUP (Left Mouse Up)", MakePointFromLParam(lParam));
        break;
    }
    case WM_RBUTTONDOWN: {
        LogMouseButtonEvent("WM_RBUTTONDOWN (Right Mouse Down)", MakePointFromLParam(lParam));
        g_IsRightMouseDown = true;
        StopHeldWASDMovement(true);
        break;
    }
    case WM_RBUTTONUP: {
        LogMouseButtonEvent("WM_RBUTTONUP (Right Mouse Up)", MakePointFromLParam(lParam));
        g_IsRightMouseDown = false;
        break;
    }
    case WM_MBUTTONDOWN: {
        if (!g_IsModernCamEnabled) {
            break;
        }

        log.WriteLine(LogLevel::Verbose, "Canvas WinProc Filter: WM_MBUTTONDOWN (Middle Mouse Down)");
        if (!g_CameraController.HasNativeCameraState()) {
            g_CameraController.DumpCameraInfo("immediately before first custom rotation");
        }
        g_IsMiddleMouseDown = true;
        g_LastMousePos = MakePointFromLParam(lParam);
		g_CameraController.BeginRotationGesture();
        SetCapture(hWnd);
        g_CapturedMouseWindow = hWnd;
        KillRedrawTimers();
        handled = true;
        return 0;
    }
    case WM_MBUTTONUP: {
        if (!g_IsModernCamEnabled) {
            break;
        }

        log.WriteLine(LogLevel::Verbose, "Canvas WinProc Filter: WM_MBUTTONUP (Middle Mouse Up)");
        if (g_CapturedMouseWindow != NULL && GetCapture() == g_CapturedMouseWindow) {
            ReleaseCapture();
        }
        g_IsMiddleMouseDown = false;
		g_CameraController.EndRotationGesture();
        g_CapturedMouseWindow = NULL;
        log.WriteLine(LogLevel::Verbose, "Pan stopped: scheduling redraw sequence.");
        StartIdleTimer(kPanIdleRedrawDelayMs);
        handled = true;
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (g_IsModernCamEnabled && g_IsMiddleMouseDown) {
            POINT mousePos = MakePointFromLParam(lParam);
            int deltaX = mousePos.x - g_LastMousePos.x;
            int deltaY = mousePos.y - g_LastMousePos.y;

			float yawDelta = static_cast<float>(deltaX) * kMouseRotationSensitivity * g_Settings.rotationSensitivity;
			float pitchDelta = static_cast<float>(deltaY) * kMouseRotationSensitivity * g_Settings.rotationSensitivity;
			if (g_Settings.invertVertical) {
				pitchDelta = -pitchDelta;
			}

			g_CameraController.ApplyDelta(pitchDelta, yawDelta, true);

            g_LastMousePos = mousePos;
            handled = true;
            return 0;
        }
        break;
    }
    case WM_MOUSEWHEEL: {
        if (!g_IsModernCamEnabled) {
            break;
        }

        short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        log.WriteLine(LogLevel::Verbose, "Canvas WinProc Filter: WM_MOUSEWHEEL (Delta: " + std::to_string(zDelta) + ")");

        POINT cursor = MakePointFromLParam(lParam);
        if (!ScreenToClient(hWnd, &cursor)) {
            log.WriteLine(LogLevel::Warning, "Mouse wheel hit-test: failed to convert screen coordinates; using camera zoom.");
        }
        else {
            NativeWheelHitTest hitTest = HitTestNativeWheelTarget(cursor.x, cursor.y);
            log.WriteLine(
                LogLevel::Info,
                std::string("Mouse wheel hit-test: X:") + std::to_string(cursor.x)
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
                + " View3D:{" + hitTest.viewDescription + "}");

            if (hitTest.resolved && !hitTest.overView3D) {
                log.WriteLine(LogLevel::Info, "Mouse wheel hit-test: passing wheel through to native SC4 UI.");
                break;
            }
        }

        if (!g_CameraController.HasNativeCameraState()) {
            g_CameraController.DumpCameraInfo("immediately before first custom zoom");
        }

        bool zoomChanged = false;
        const int32_t adjustedZoomDelta = static_cast<int32_t>(
            std::lround(static_cast<float>(zDelta) * g_Settings.zoomSensitivity));
        if (g_CameraController.ZoomByWheel(adjustedZoomDelta, zoomChanged)) {
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

class cSC4ModernCameraDirector : public cRZMessage2COMDirector, public cIGZWinProcFilterW32
{
public:
    cSC4ModernCameraDirector()
        : mpCanvasW32(nullptr)
    {
        AddRef();
    }

    // Resolve COM multiple inheritance ambiguity
    uint32_t AddRef() override { return cRZMessage2COMDirector::AddRef(); }
    uint32_t Release() override { return cRZMessage2COMDirector::Release(); }

    bool QueryInterface(uint32_t riid, void** ppvObj) override
    {
        return cRZMessage2COMDirector::QueryInterface(riid, ppvObj);
    }

    uint32_t GetDirectorID() const override
    {
        return 0x8C4B3A11;
    }

    bool OnStart(cIGZCOM* pCOM) override
    {
        try {
            Logger::GetInstance().Initialize(PluginPaths::GetLogPath().string());
        }
        catch (const std::exception&) {
            std::filesystem::create_directories("SC4-ModernCamera");
            Logger::GetInstance().Initialize("SC4-ModernCamera/SC4-ModernCamera.log");
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

        g_IsModernCamEnabled = g_Settings.cameraMode == CameraMode::Modern;
        g_WindowManager.SetCallbacks(SC4WindowManagerCallbacks{
            ApplyModernCameraEnabled,
            ApplyRedrawAggressionChange,
            ResetCameraToNativeView,
            ApplyInputSettingsChange,
            ApplyDebugLoggingChange,
        });

        ApplyDebugLoggingChange();

        Logger::GetInstance().WriteLine(
            LogLevel::Info,
            std::string("Modern Camera Enabled: ") + (g_IsModernCamEnabled ? "true" : "false")
            + (g_IsModernCamEnabled ? "" : " (passive diagnostics mode)"));

        if (!RegisterNotifications(this)) {
            Logger::GetInstance().WriteLine(LogLevel::Error, "Failed to subscribe to the required notifications.");
        }

        return true;
    }

    bool DoMessage(cIGZMessage2* pMsg) override
    {
        uint32_t msgType = pMsg->GetType();

        if (msgType == kSC4MessagePostCityInit) {
            Logger::GetInstance().WriteLine(LogLevel::Info, "City Loaded! Activating input handlers...");
            g_WindowManager.OnCityLoaded(g_Settings);
            g_IsCityLoaded = true;
            ResetInputState();
            RegisterCanvasWinProcFilter();
            InstallKeyboardHook();
            StartNativeCameraBaselineTimer();
            StartPeriodicRedrawTimer();
        }
        else if (msgType == kSC4MessagePreSave) {
            Logger::GetInstance().WriteLine(LogLevel::Info, "Pre-save notification received.");
            if (g_IsModernCamEnabled && g_IsCityLoaded
                && !g_CameraController.BeginSavePreviewNormalization()
                && !g_CameraController.IsSavePreviewNormalizationActive()) {
                Logger::GetInstance().WriteLine(LogLevel::Warning, "Save Preview: failed to normalize camera at pre-save.");
            }
        }
        else if (msgType == kSC4MessagePreCityShutdown) {
            Logger::GetInstance().WriteLine(LogLevel::Info, "City Shutting Down. Deactivating input handlers...");
            g_WindowManager.OnCityShutdown();
            g_CameraController.DumpCameraInfo("pre-city-shutdown");
            g_CameraController.AbandonSavePreviewNormalization();
            g_IsCityLoaded = false;
            ResetInputState();
            UninstallKeyboardHook();
            UnregisterCanvasWinProcFilter();
        }

        return true;
    }

    LRESULT FilterMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, bool& handled) override
    {
        return HandleCanvasMouseMessage(hWnd, uMsg, wParam, lParam, handled);
    }

private:
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
};

cRZCOMDllDirector* RZGetCOMDllDirector() {
    static cSC4ModernCameraDirector sDirector;
    return &sDirector;
}
