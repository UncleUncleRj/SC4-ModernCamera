#include "cIGZCanvas.h"
#include "cIGZCanvasW32.h"
#include "cIGZGraphicSystem.h"
#include "cIGZMessage2Standard.h"
#include "cIGZMessageServer2.h"
#include "cIGZWin.h"
#include "cIGZWinMgr.h"
#include "cIGZWinProcFilterW32.h"
#include "cIGZWinTextEdit.h"
#include "cISC4App.h"
#include "cISC4View3DWin.h"
#include "cISC4ViewInputControl.h"
#include "cRZAutoRefCount.h"
#include "cRZMessage2COMDirector.h"
#include "GZServPtrs.h"
#include "Logger.h"
#include "PluginPaths.h"
#include "PluginSettings.h"
#include "PluginVersion.h"
#include "SC4CameraController.h"
#include "SC4GameConstants.h"
#include "SC4VersionDetection.h"
#include "SC4WindowManager.h"
#include <Windows.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <sstream>
#include <string>

static constexpr uint32_t kSC4MessagePostCityInit = 0x26D31EC1;
static constexpr uint32_t kSC4MessagePreCityShutdown = 0x26D31EC2;
static constexpr uint32_t kSC4MessagePreSave = 0x26C63343;
static constexpr uint32_t kDirectorID = 0x8C4B3A11;
static constexpr size_t kView3DWinSetScrollingVTableIndex = 33;
static constexpr size_t kView3DWinMinimizeUIVTableIndex = 55;
static constexpr LPARAM kPreviousKeyStateMask = static_cast<LPARAM>(1) << 30;
static constexpr float kMouseRotationSensitivity = 0.005f;
static constexpr UINT kPanIdleRedrawDelayMs = 1000;
static constexpr UINT kKeyboardPanTickMs = 33;
static constexpr float kKeyboardPanNativeSpeed = 0.25f;
static constexpr float kKeyboardPanBoostMultiplier = 2.0f;
static constexpr UINT kZoomIdleRedrawDelayMs = 1500;
static constexpr UINT kHighPeriodicRedrawDelayMs = 1000;
static constexpr UINT kExtremePeriodicRedrawDelayMs = 100;
static constexpr UINT kCameraDumpConfirmationDelayMs = 2500;
static constexpr UINT kNativeToolClearEscDelayMs = 50;
static constexpr UINT kModernCameraFocusRestoreDelayMs = 150;
static constexpr UINT kDumpCameraInfoKey = VK_F8;
static constexpr int32_t kNativeUICornerProbeMaxX = 260;
static constexpr int32_t kNativeUICornerProbeMaxBottomOffset = 360;
static constexpr uint32_t kModernCameraWindowIDMin = 0x3D0C0700;
static constexpr uint32_t kModernCameraWindowIDMax = 0x3D0C09FF;

// Global State
bool g_IsCityLoaded = false;
bool g_IsModernCamEnabled = true;
bool g_IsNativeUIHidden = false;
using MinimizeUIFunction = bool(__thiscall*)(cISC4View3DWin* view3D, bool minimize);
using SetScrollingFunction = bool(__thiscall*)(cISC4View3DWin* view3D, bool scrolling, float x, float z);
MinimizeUIFunction g_OriginalMinimizeUI = nullptr;
SetScrollingFunction g_OriginalSetScrolling = nullptr;
void** g_View3DWinVTable = nullptr;
void** g_View3DWinSetScrollingVTable = nullptr;
bool g_MinimizeUIHookInstalled = false;
bool g_SetScrollingHookInstalled = false;

enum class View3DToolClearReason : uint8_t
{
    ModernCameraEnabled,
    WASDMovementChanged,
    ClassicCameraEnabled,
};

enum class KeyOwner : uint8_t
{
    None,
    ModernCameraPan,
    ModernCameraRotation,
    ModernCameraClassicAngle,
};

struct TrackedKeyState
{
    bool down = false;
    KeyOwner owner = KeyOwner::None;
};

class KeyStateManager
{
public:
    bool IsTrackable(WPARAM key) const
    {
        return key < keyStates.size();
    }

    bool IsPhysicalDown(int key) const
    {
        return (GetAsyncKeyState(key) & 0x8000) != 0;
    }

    void Observe(WPARAM key, bool down)
    {
        if (!IsTrackable(key)) {
            return;
        }

        keyStates[key].down = down;
        switch (key) {
        case VK_LSHIFT:
        case VK_RSHIFT:
            keyStates[VK_SHIFT].down = down
                || IsPhysicalDown(VK_LSHIFT)
                || IsPhysicalDown(VK_RSHIFT);
            break;
        case VK_LCONTROL:
        case VK_RCONTROL:
            keyStates[VK_CONTROL].down = down
                || IsPhysicalDown(VK_LCONTROL)
                || IsPhysicalDown(VK_RCONTROL);
            break;
        case VK_LMENU:
        case VK_RMENU:
            keyStates[VK_MENU].down = down
                || IsPhysicalDown(VK_LMENU)
                || IsPhysicalDown(VK_RMENU);
            break;
        default:
            break;
        }
    }

    bool IsDown(int key) const
    {
        return key >= 0
            && key < static_cast<int>(keyStates.size())
            && keyStates[static_cast<size_t>(key)].down;
    }

    bool IsDownWithFallback(int key) const
    {
        return IsDown(key) || IsPhysicalDown(key);
    }

    void RefreshModifiers()
    {
        keyStates[VK_SHIFT].down = IsPhysicalDown(VK_SHIFT)
            || IsPhysicalDown(VK_LSHIFT)
            || IsPhysicalDown(VK_RSHIFT);
        keyStates[VK_CONTROL].down = IsPhysicalDown(VK_CONTROL)
            || IsPhysicalDown(VK_LCONTROL)
            || IsPhysicalDown(VK_RCONTROL);
        keyStates[VK_MENU].down = IsPhysicalDown(VK_MENU)
            || IsPhysicalDown(VK_LMENU)
            || IsPhysicalDown(VK_RMENU);
    }

    bool Claim(WPARAM key, KeyOwner owner)
    {
        if (!IsTrackable(key)) {
            return false;
        }

        TrackedKeyState& state = keyStates[key];
        if (state.owner != KeyOwner::None && state.owner != owner) {
            return false;
        }

        state.owner = owner;
        return true;
    }

    void Release(WPARAM key, KeyOwner owner)
    {
        if (!IsTrackable(key)) {
            return;
        }

        TrackedKeyState& state = keyStates[key];
        if (state.owner == owner) {
            state.owner = KeyOwner::None;
        }
    }

    bool IsOwnedBy(WPARAM key, KeyOwner owner) const
    {
        return IsTrackable(key) && keyStates[key].owner == owner;
    }

    KeyOwner GetOwner(WPARAM key) const
    {
        return IsTrackable(key) ? keyStates[key].owner : KeyOwner::None;
    }

    template <size_t Count>
    bool AnyDownOwnedBy(const std::array<WPARAM, Count>& keys, KeyOwner owner) const
    {
        for (WPARAM key : keys) {
            if (IsOwnedBy(key, owner) && IsDown(static_cast<int>(key))) {
                return true;
            }
        }

        return false;
    }

    template <size_t Count>
    bool AnyOwnedBy(const std::array<WPARAM, Count>& keys, KeyOwner owner) const
    {
        for (WPARAM key : keys) {
            if (IsOwnedBy(key, owner)) {
                return true;
            }
        }

        return false;
    }

    template <size_t Count>
    void ReleaseAll(const std::array<WPARAM, Count>& keys, KeyOwner owner)
    {
        for (WPARAM key : keys) {
            Release(key, owner);
        }
    }

    void Clear()
    {
        for (TrackedKeyState& state : keyStates) {
            state.down = false;
            state.owner = KeyOwner::None;
        }
    }

private:
    std::array<TrackedKeyState, 256> keyStates{};
};

// Cinematic Camera State
bool g_IsApplyingKeyboardPan = false;
KeyStateManager g_KeyStateManager;
static constexpr std::array<WPARAM, 4> kWASDKeyboardPanKeys = { 'W', 'A', 'S', 'D' };
static constexpr std::array<WPARAM, 4> kArrowKeyboardPanKeys = { VK_UP, VK_LEFT, VK_DOWN, VK_RIGHT };
static constexpr std::array<WPARAM, 3> kClassicAngleKeys = { VK_MENU, VK_LMENU, VK_RMENU };
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
POINT g_LastMousePos = { 0, 0 };
HWND g_CapturedMouseWindow = NULL;
bool g_RotationCursorHidden = false;
HWND g_CanvasWindow = NULL;
HHOOK g_KeyboardHook = NULL;
SC4CameraController g_CameraController;
PluginSettings g_Settings;
SC4WindowManager g_WindowManager;

UINT_PTR g_IdleTimerID = 0;
UINT_PTR g_PeriodicRedrawTimerID = 0;
UINT_PTR g_KeyboardPanTimerID = 0;
UINT_PTR g_CameraDumpConfirmationTimerID = 0;
UINT_PTR g_NativeToolClearEscTimerID = 0;
UINT_PTR g_ModernCameraFocusRestoreTimerID = 0;
bool g_View3DFocusRestoreRequiresModern = true;
const char* g_View3DFocusRestoreContext = "View3D focus restore";
std::function<void()> g_RegisterCanvasWinProcFilterCallback;
std::function<void()> g_UnregisterCanvasWinProcFilterCallback;

VOID CALLBACK RedrawTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
VOID CALLBACK PeriodicRedrawTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
VOID CALLBACK KeyboardPanTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
VOID CALLBACK ClearCameraDumpConfirmationTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
VOID CALLBACK NativeToolClearEscTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
VOID CALLBACK ModernCameraFocusRestoreTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
LRESULT CALLBACK KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam);
void InstallKeyboardHook();
void UninstallKeyboardHook();
void InstallSetScrollingHook();
void UninstallSetScrollingHook();
void RegisterCanvasWinProcFilterForModernCamera();
void UnregisterCanvasWinProcFilterForModernCamera();
void StopHeldKeyboardPan();
void StopClassicAngleHold(const char* source);
bool IsRightClickScrollingActive();
bool FocusView3DForNativeInput(const char* context);
void RestoreWorldFocusAfterSettingsClosed();
bool __fastcall HookedMinimizeUI(cISC4View3DWin* view3D, void* edx, bool minimize);
bool __fastcall HookedSetScrolling(cISC4View3DWin* view3D, void* edx, bool scrolling, float x, float z);

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

const char* GetKeyOwnerName(KeyOwner owner)
{
    switch (owner) {
    case KeyOwner::ModernCameraPan:
        return "ModernCameraPan";
    case KeyOwner::ModernCameraRotation:
        return "ModernCameraRotation";
    case KeyOwner::ModernCameraClassicAngle:
        return "ModernCameraClassicAngle";
    default:
        return "None";
    }
}

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

    cIGZWin* parentWindow = mainWindow->GetChildWindowFromID(SC4WindowID::MainApplication);
    if (!parentWindow) {
        Logger::GetInstance().WriteLine(
            LogLevel::Warning,
            std::string(context) + ": failed to get WinSC4App while handling native view input.");
        return nullptr;
    }

    cISC4View3DWin* view3D = nullptr;
    if (!parentWindow->GetChildAs(
        SC4WindowID::View3D,
        kGZIID_cISC4View3DWin,
        reinterpret_cast<void**>(&view3D)) || !view3D) {
        Logger::GetInstance().WriteLine(
            LogLevel::Warning,
            std::string(context) + ": failed to get cISC4View3DWin while handling native view input.");
        return nullptr;
    }

    return view3D;
}

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

    const bool removedControl = currentControl
        ? view3D->RemoveCurrentViewInputControl(true)
        : false;
    const bool stoppedScrolling = view3D->ScrollStop();
    const bool killedKeyboardScrolling = view3D->KillKeyboardScrolling();
    cISC4ViewInputControl* remainingControl = view3D->GetCurrentViewInputControl();
    Logger::GetInstance().WriteLine(
        LogLevel::Info,
        std::string(reasonName)
        + ": native view input reset. RemoveCurrentViewInputControl="
        + (removedControl ? "true" : "false")
        + " RemoveAllViewInputControls=skipped"
        + " ScrollStop="
        + (stoppedScrolling ? "true" : "false")
        + " KillKeyboardScrolling="
        + (killedKeyboardScrolling ? "true" : "false")
        + " RemainingToolID:"
        + (remainingControl ? FormatWindowID(remainingControl->GetID()) : "none"));

    view3D->Release();
}

bool CancelTimer(UINT_PTR& timerID)
{
    if (timerID == 0) {
        return false;
    }

    KillTimer(NULL, timerID);
    timerID = 0;
    return true;
}

void KillNativeToolClearEscTimer()
{
    CancelTimer(g_NativeToolClearEscTimerID);
}

void KillModernCameraFocusRestoreTimer()
{
    CancelTimer(g_ModernCameraFocusRestoreTimerID);
}

void ScheduleView3DFocusRestore(bool requireModern, const char* context)
{
    KillModernCameraFocusRestoreTimer();
    if (!g_IsCityLoaded || (requireModern && !g_IsModernCamEnabled)) {
        return;
    }

    g_View3DFocusRestoreRequiresModern = requireModern;
    g_View3DFocusRestoreContext = context ? context : "View3D focus restore";
    g_ModernCameraFocusRestoreTimerID = SetTimer(
        NULL,
        0,
        kModernCameraFocusRestoreDelayMs,
        ModernCameraFocusRestoreTimerProc);
    if (g_ModernCameraFocusRestoreTimerID == 0) {
        Logger::GetInstance().WriteLine(LogLevel::Warning, "Failed to start 3D view focus restore timer.");
    }
}

void ScheduleModernCameraFocusRestore()
{
    ScheduleView3DFocusRestore(true, "Modern camera focus restore");
}

bool SendVirtualKeyTap(WORD virtualKey, const char* label)
{
    INPUT inputs[2]{};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = virtualKey;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = virtualKey;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;

    const UINT sent = SendInput(2, inputs, sizeof(INPUT));
    Logger::GetInstance().WriteLine(
        sent == 2 ? LogLevel::Info : LogLevel::Warning,
        std::string("Native view input clear: ")
        + (label ? label : "key")
        + " tap "
        + (sent == 2 ? "sent." : "failed.")
        + " Sent:" + std::to_string(sent));
    return sent == 2;
}

void ScheduleNativeToolClearEsc()
{
    KillNativeToolClearEscTimer();
    g_NativeToolClearEscTimerID = SetTimer(NULL, 0, kNativeToolClearEscDelayMs, NativeToolClearEscTimerProc);
    if (g_NativeToolClearEscTimerID == 0) {
        Logger::GetInstance().WriteLine(LogLevel::Warning, "Failed to start native tool clear Esc timer.");
    }
}

void ClearNativeToolWithQueryThenEsc()
{
    Logger::GetInstance().WriteLine(LogLevel::Info, "Native view input clear: activating query tool before Esc clear.");
    FocusView3DForNativeInput("Native view input clear");
    if (SendVirtualKeyTap(VK_OEM_2, "query tool slash")) {
        ScheduleNativeToolClearEsc();
    }
    else {
        ScheduleView3DFocusRestore(
            g_IsModernCamEnabled,
            g_IsModernCamEnabled
                ? "Modern camera native tool clear focus restore"
                : "Classic camera native tool clear focus restore");
    }
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

    cIGZWin* parentWindow = mainWindow->GetChildWindowFromID(SC4WindowID::MainApplication);
    if (!parentWindow) {
        result.targetDescription = "no WinSC4App";
        return result;
    }

    cISC4View3DWin* view3D = nullptr;
    if (!parentWindow->GetChildAs(
        SC4WindowID::View3D,
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
    result.targetIsView3DSurface = targetWindow && targetWindow->GetID() == SC4WindowID::View3DSurface;
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

void KillIdleTimer()
{
    CancelTimer(g_IdleTimerID);
}

void KillPeriodicRedrawTimer()
{
    CancelTimer(g_PeriodicRedrawTimerID);
}

void KillRedrawTimers()
{
    KillIdleTimer();
    KillPeriodicRedrawTimer();
}

void KillKeyboardPanTimer()
{
    if (CancelTimer(g_KeyboardPanTimerID)) {
        Logger::GetInstance().WriteLine(LogLevel::Verbose, "Keyboard movement timer stopped.");
    }
}

bool IsTrackableKey(WPARAM key)
{
    return g_KeyStateManager.IsTrackable(key);
}

bool IsPhysicalKeyDown(int key)
{
    return g_KeyStateManager.IsPhysicalDown(key);
}

void ObserveKeyTransition(WPARAM key, bool down)
{
    g_KeyStateManager.Observe(key, down);
}

bool IsTrackedKeyDown(int key)
{
    return g_KeyStateManager.IsDown(key);
}

bool IsObservedOrPhysicalKeyDown(int key)
{
    return g_KeyStateManager.IsDownWithFallback(key);
}

void RefreshTrackedModifierStates()
{
    g_KeyStateManager.RefreshModifiers();
}

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

bool IsKeyOwnedBy(WPARAM key, KeyOwner owner)
{
    return g_KeyStateManager.IsOwnedBy(key, owner);
}

bool IsKeyboardPanKeyDown(WPARAM key)
{
    return IsKeyOwnedBy(key, KeyOwner::ModernCameraPan)
        && IsTrackedKeyDown(static_cast<int>(key));
}

bool HasHeldKeyboardPanKey()
{
    return g_KeyStateManager.AnyDownOwnedBy(kKeyboardPanKeys, KeyOwner::ModernCameraPan);
}

bool HasOwnedKeyboardPanKey()
{
    return g_KeyStateManager.AnyOwnedBy(kKeyboardPanKeys, KeyOwner::ModernCameraPan);
}

bool HasHeldWASDKeyboardPanKey()
{
    return g_KeyStateManager.AnyDownOwnedBy(kWASDKeyboardPanKeys, KeyOwner::ModernCameraPan);
}

bool HasHeldArrowKeyboardPanKey()
{
    return g_KeyStateManager.AnyDownOwnedBy(kArrowKeyboardPanKeys, KeyOwner::ModernCameraPan);
}

bool IsMiddleMouseRotationActive()
{
    return IsKeyOwnedBy(VK_MBUTTON, KeyOwner::ModernCameraRotation);
}

bool IsClassicAngleVirtualKey(WPARAM key)
{
    return key == VK_MENU || key == VK_LMENU || key == VK_RMENU;
}

bool HasHeldClassicAngleKey()
{
    return g_KeyStateManager.AnyDownOwnedBy(kClassicAngleKeys, KeyOwner::ModernCameraClassicAngle);
}

bool HasOwnedClassicAngleKey()
{
    return g_KeyStateManager.AnyOwnedBy(kClassicAngleKeys, KeyOwner::ModernCameraClassicAngle);
}

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

void ReleaseKeyboardPanKeyClaims()
{
    g_KeyStateManager.ReleaseAll(kKeyboardPanKeys, KeyOwner::ModernCameraPan);
}

void ReleaseClassicAngleKeyClaims()
{
    g_KeyStateManager.ReleaseAll(kClassicAngleKeys, KeyOwner::ModernCameraClassicAngle);
}

void ClearHeldKeyboardPanKeys()
{
    ReleaseKeyboardPanKeyClaims();
}

void ClearClassicAngleKeys()
{
    ReleaseClassicAngleKeyClaims();
}

void ClearMiddleMouseRotationClaim()
{
    ReleaseKey(VK_MBUTTON, KeyOwner::ModernCameraRotation);
}

void ClearTrackedKeyStates(const char* reason)
{
    g_KeyStateManager.Clear();

    Logger::GetInstance().WriteLine(
        LogLevel::Verbose,
        std::string("Keyboard state tracker cleared. Reason:")
        + (reason ? reason : "unknown"));
}

void StartKeyboardPanTimer()
{
    if (g_KeyboardPanTimerID != 0) {
        return;
    }

    g_KeyboardPanTimerID = SetTimer(NULL, 0, kKeyboardPanTickMs, KeyboardPanTimerProc);
    if (g_KeyboardPanTimerID == 0) {
        Logger::GetInstance().WriteLine(LogLevel::Warning, "Failed to start the keyboard movement timer.");
    }
    else {
        Logger::GetInstance().WriteLine(
            LogLevel::Verbose,
            "Keyboard movement timer started. TickMs:" + std::to_string(kKeyboardPanTickMs));
    }
}

void StartPeriodicRedrawTimer()
{
    KillPeriodicRedrawTimer();

    if (!g_IsCityLoaded || !g_IsModernCamEnabled || IsMiddleMouseRotationActive()) {
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
        + std::string(PluginSettingText::ToString(g_Settings.redrawAggression))
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
        + std::string(PluginSettingText::ToString(g_Settings.redrawAggression))
        + " delayMs=" + std::to_string(delayMs));
    g_IdleTimerID = SetTimer(NULL, 0, delayMs, RedrawTimerProc);
    if (g_IdleTimerID == 0) {
        Logger::GetInstance().WriteLine(LogLevel::Warning, "Failed to start the redraw timer.");
    }
}

void SetRotationCursorVisible(bool visible)
{
    const bool hidden = !visible;
    if (g_RotationCursorHidden == hidden) {
        return;
    }

    cIGZWinMgrPtr winMgr;
    if (!winMgr || !winMgr->GZShowCursor(visible)) {
        Logger::GetInstance().WriteLine(
            LogLevel::Warning,
            std::string("Failed to ") + (visible ? "show" : "hide")
            + " the cursor for middle mouse camera rotation.");
        return;
    }

    g_RotationCursorHidden = hidden;
}

void StopMiddleMouseRotation(const char* reason, bool scheduleRedraw)
{
    const bool wasActive = IsMiddleMouseRotationActive();
    if (g_CapturedMouseWindow != NULL && GetCapture() == g_CapturedMouseWindow) {
        ReleaseCapture();
    }

    ClearMiddleMouseRotationClaim();
    g_CapturedMouseWindow = NULL;
    SetRotationCursorVisible(true);

    if (!wasActive) {
        return;
    }

    g_CameraController.EndRotationGesture();
    Logger::GetInstance().WriteLine(
        LogLevel::Verbose,
        std::string("Middle mouse camera rotation stopped. Reason:")
        + (reason ? reason : "unknown"));

    if (scheduleRedraw) {
        Logger::GetInstance().WriteLine(LogLevel::Verbose, "Pan stopped: scheduling redraw sequence.");
        StartIdleTimer(kPanIdleRedrawDelayMs);
    }
}

void KillCameraDumpConfirmationTimer()
{
    CancelTimer(g_CameraDumpConfirmationTimerID);
}

void StartCameraDumpConfirmationTimer()
{
    KillCameraDumpConfirmationTimer();
    g_CameraDumpConfirmationTimerID = SetTimer(NULL, 0, kCameraDumpConfirmationDelayMs, ClearCameraDumpConfirmationTimerProc);
}

VOID CALLBACK NativeToolClearEscTimerProc(HWND, UINT, UINT_PTR idEvent, DWORD)
{
    if (idEvent == g_NativeToolClearEscTimerID && g_NativeToolClearEscTimerID != 0) {
        KillNativeToolClearEscTimer();
        FocusView3DForNativeInput("Native view input clear Esc");
        SendVirtualKeyTap(VK_ESCAPE, "Esc");
        ScheduleView3DFocusRestore(
            g_IsModernCamEnabled,
            g_IsModernCamEnabled
                ? "Modern camera native tool clear focus restore"
                : "Classic camera native tool clear focus restore");
    }
}

VOID CALLBACK ModernCameraFocusRestoreTimerProc(HWND, UINT, UINT_PTR idEvent, DWORD)
{
    if (idEvent == g_ModernCameraFocusRestoreTimerID && g_ModernCameraFocusRestoreTimerID != 0) {
        KillModernCameraFocusRestoreTimer();
        if (g_IsCityLoaded && (!g_View3DFocusRestoreRequiresModern || g_IsModernCamEnabled)) {
            FocusView3DForNativeInput(g_View3DFocusRestoreContext);
        }
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

bool IsNativeUICornerProbeClick(const POINT& point)
{
	cISC4AppPtr app;
	cIGZWin* mainWindow = app ? app->GetMainWindow() : nullptr;
	cIGZWin* parentWindow = mainWindow
		? mainWindow->GetChildWindowFromID(SC4WindowID::MainApplication)
		: nullptr;
	if (!parentWindow) {
		return false;
	}

	const int32_t bottomOffset = parentWindow->GetH() - point.y;
	return point.x >= 0
		&& point.x < kNativeUICornerProbeMaxX
		&& bottomOffset >= 0
		&& bottomOffset < kNativeUICornerProbeMaxBottomOffset;
}

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

cISC4View3DWin* GetView3DWinForNativeUIHook()
{
	cISC4AppPtr app;
	cIGZWin* mainWindow = app ? app->GetMainWindow() : nullptr;
	cIGZWin* parentWindow = mainWindow
		? mainWindow->GetChildWindowFromID(SC4WindowID::MainApplication)
		: nullptr;
	if (!parentWindow) {
		return nullptr;
	}

	cISC4View3DWin* view3D = nullptr;
	if (!parentWindow->GetChildAs(
		SC4WindowID::View3D,
		kGZIID_cISC4View3DWin,
		reinterpret_cast<void**>(&view3D))) {
		return nullptr;
	}

	return view3D;
}

std::string FormatPointerValue(const void* value)
{
	std::ostringstream stream;
	stream << "0x" << std::uppercase << std::hex << reinterpret_cast<uintptr_t>(value);
	return stream.str();
}

bool PatchVTableSlot(void** slot, void* replacement, void** originalOut)
{
	if (!slot || !replacement) {
		return false;
	}

	DWORD oldProtect = 0;
	if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
		return false;
	}

	if (originalOut) {
		*originalOut = *slot;
	}
	*slot = replacement;

	DWORD ignored = 0;
	VirtualProtect(slot, sizeof(void*), oldProtect, &ignored);
	return true;
}

void InstallMinimizeUIHook()
{
	if (g_MinimizeUIHookInstalled) {
		return;
	}

	cISC4View3DWin* view3D = GetView3DWinForNativeUIHook();
	if (!view3D) {
		Logger::GetInstance().WriteLine(LogLevel::Warning, "Native UI MinimizeUI hook: could not find cISC4View3DWin.");
		return;
	}

	void** vtable = *reinterpret_cast<void***>(view3D);
	void** slot = vtable ? &vtable[kView3DWinMinimizeUIVTableIndex] : nullptr;
	void* original = nullptr;

	if (PatchVTableSlot(slot, reinterpret_cast<void*>(&HookedMinimizeUI), &original)) {
		g_View3DWinVTable = vtable;
		g_OriginalMinimizeUI = reinterpret_cast<MinimizeUIFunction>(original);
		g_MinimizeUIHookInstalled = true;
		Logger::GetInstance().WriteLine(
			LogLevel::Info,
			std::string("Native UI MinimizeUI hook installed. VTable:")
			+ FormatPointerValue(vtable)
			+ " Slot:" + std::to_string(kView3DWinMinimizeUIVTableIndex)
			+ " Original:" + FormatPointerValue(original));
	}
	else {
		Logger::GetInstance().WriteLine(LogLevel::Error, "Native UI MinimizeUI hook: failed to patch vtable slot.");
	}

	view3D->Release();
}

void InstallSetScrollingHook()
{
	if (g_SetScrollingHookInstalled) {
		return;
	}

	cISC4View3DWin* view3D = GetView3DWinForNativeUIHook();
	if (!view3D) {
		Logger::GetInstance().WriteLine(LogLevel::Warning, "View3D SetScrolling hook: could not find cISC4View3DWin.");
		return;
	}

	void** vtable = *reinterpret_cast<void***>(view3D);
	void** slot = vtable ? &vtable[kView3DWinSetScrollingVTableIndex] : nullptr;
	void* original = nullptr;

	if (PatchVTableSlot(slot, reinterpret_cast<void*>(&HookedSetScrolling), &original)) {
		g_View3DWinSetScrollingVTable = vtable;
		g_OriginalSetScrolling = reinterpret_cast<SetScrollingFunction>(original);
		g_SetScrollingHookInstalled = true;
		Logger::GetInstance().WriteLine(
			LogLevel::Info,
			std::string("View3D SetScrolling hook installed. VTable:")
			+ FormatPointerValue(vtable)
			+ " Slot:" + std::to_string(kView3DWinSetScrollingVTableIndex)
			+ " Original:" + FormatPointerValue(original));
	}
	else {
		Logger::GetInstance().WriteLine(LogLevel::Error, "View3D SetScrolling hook: failed to patch vtable slot.");
	}

	view3D->Release();
}

void UninstallMinimizeUIHook()
{
	if (!g_MinimizeUIHookInstalled) {
		return;
	}

	void** slot = g_View3DWinVTable ? &g_View3DWinVTable[kView3DWinMinimizeUIVTableIndex] : nullptr;
	if (slot && *slot == reinterpret_cast<void*>(&HookedMinimizeUI)) {
		PatchVTableSlot(slot, reinterpret_cast<void*>(g_OriginalMinimizeUI), nullptr);
		Logger::GetInstance().WriteLine(LogLevel::Info, "Native UI MinimizeUI hook uninstalled.");
	}
	else {
		Logger::GetInstance().WriteLine(LogLevel::Warning, "Native UI MinimizeUI hook: slot changed before uninstall.");
	}

	g_OriginalMinimizeUI = nullptr;
	g_View3DWinVTable = nullptr;
	g_MinimizeUIHookInstalled = false;
}

void UninstallSetScrollingHook()
{
	if (!g_SetScrollingHookInstalled) {
		return;
	}

	void** slot = g_View3DWinSetScrollingVTable ? &g_View3DWinSetScrollingVTable[kView3DWinSetScrollingVTableIndex] : nullptr;
	if (slot && *slot == reinterpret_cast<void*>(&HookedSetScrolling)) {
		PatchVTableSlot(slot, reinterpret_cast<void*>(g_OriginalSetScrolling), nullptr);
		Logger::GetInstance().WriteLine(LogLevel::Info, "View3D SetScrolling hook uninstalled.");
	}
	else {
		Logger::GetInstance().WriteLine(LogLevel::Warning, "View3D SetScrolling hook: slot changed before uninstall.");
	}

	g_OriginalSetScrolling = nullptr;
	g_View3DWinSetScrollingVTable = nullptr;
	g_SetScrollingHookInstalled = false;
}

void RegisterCanvasWinProcFilterForModernCamera()
{
    if (g_RegisterCanvasWinProcFilterCallback) {
        g_RegisterCanvasWinProcFilterCallback();
    }
    else {
        Logger::GetInstance().WriteLine(LogLevel::Warning, "Canvas WinProc filter register callback unavailable.");
    }
}

void UnregisterCanvasWinProcFilterForModernCamera()
{
    if (g_UnregisterCanvasWinProcFilterCallback) {
        g_UnregisterCanvasWinProcFilterCallback();
    }
    else {
        Logger::GetInstance().WriteLine(LogLevel::Warning, "Canvas WinProc filter unregister callback unavailable.");
    }
}

bool __fastcall HookedMinimizeUI(cISC4View3DWin* view3D, void* edx, bool minimize)
{
	(void)edx;

	MinimizeUIFunction original = g_OriginalMinimizeUI;
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

bool __fastcall HookedSetScrolling(cISC4View3DWin* view3D, void* edx, bool scrolling, float x, float z)
{
	(void)edx;

	SetScrollingFunction original = g_OriginalSetScrolling;
	if (!original) {
		Logger::GetInstance().WriteLine(LogLevel::Error, "View3D SetScrolling hook: original function pointer was null.");
		return false;
	}

	const char* source = g_IsApplyingKeyboardPan
		? "keyboard SetScrolling"
		: (IsRightClickScrollingActive() ? "right mouse SetScrolling" : "native SetScrolling");
	float adjustedX = x;
	bool blockedByBounds = false;
	if (g_IsCityLoaded && g_IsModernCamEnabled && scrolling) {
		cISC43DRender* renderer = view3D ? view3D->GetRenderer() : nullptr;
		const bool boundsChecked = g_CameraController.AdjustScrollForCityBounds(
			renderer,
			x,
			z,
			adjustedX,
			blockedByBounds,
			source);
		if (!boundsChecked || blockedByBounds) {
			const bool stopped = original(view3D, false, 0.0f, 0.0f);
			const LogLevel logLevel = boundsChecked ? LogLevel::Verbose : LogLevel::Warning;
			Logger::GetInstance().WriteLineLazy(logLevel, [&]() {
				return std::string(boundsChecked
					? "View3D SetScrolling blocked at city bounds. Source:"
					: "View3D SetScrolling bounds preflight failed; stopping scroll. Source:")
					+ source
					+ " X:" + std::to_string(x)
					+ " Z:" + std::to_string(z)
					+ " StopResult:" + (stopped ? "true" : "false");
			});
			return stopped;
		}
	}

	const bool result = original(view3D, scrolling, adjustedX, z);
	Logger::GetInstance().WriteLineLazy(LogLevel::Verbose, [&]() {
		return std::string("View3D SetScrolling observed. Source:")
			+ (g_IsApplyingKeyboardPan ? "ModernCameraKeyboard" : "NativeOrOther")
			+ " Scrolling:" + (scrolling ? "true" : "false")
			+ " X:" + std::to_string(x)
			+ " AdjustedX:" + std::to_string(adjustedX)
			+ " Z:" + std::to_string(z)
			+ " Result:" + (result ? "true" : "false");
	});

	return result;
}

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
		+ " HookInstalled:" + (g_MinimizeUIHookInstalled ? "true" : "false"));
}

void ResetInputState(bool resetCamera = true)
{
    KillRedrawTimers();
    StopHeldKeyboardPan();
    StopClassicAngleHold("input reset");
    StopMiddleMouseRotation("input reset", false);
    ClearTrackedKeyStates("input reset");
    KillCameraDumpConfirmationTimer();
    KillNativeToolClearEscTimer();
    KillModernCameraFocusRestoreTimer();
    g_CameraController.ClearCameraDumpConfirmation();

    if (resetCamera) {
        g_CameraController.Reset();
    }
}

void ResetCameraToNativeView()
{
    Logger::GetInstance().WriteLine(LogLevel::Info, "Settings UI: reset camera requested.");
    KillRedrawTimers();
    StopHeldKeyboardPan();
    StopClassicAngleHold("camera reset");
    StopMiddleMouseRotation("camera reset", false);
    ClearTrackedKeyStates("camera reset");
    KillCameraDumpConfirmationTimer();
    KillNativeToolClearEscTimer();
    KillModernCameraFocusRestoreTimer();

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
    StopHeldKeyboardPan();
    StopClassicAngleHold("camera mode changed");

    if (!enabled) {
        UninstallKeyboardHook();
        UninstallSetScrollingHook();
        UnregisterCanvasWinProcFilterForModernCamera();
        const bool restoredNativeCamera = g_CameraController.RestoreNativeCameraState("classic camera enabled");
        if (restoredNativeCamera) {
            ResetInputState(false);
        }
        else {
            ResetCameraToNativeView();
        }
        ClearNativeView3DToolState(View3DToolClearReason::ClassicCameraEnabled);
        ClearNativeToolWithQueryThenEsc();
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
        ClearNativeToolWithQueryThenEsc();
        ScheduleModernCameraFocusRestore();
        TriggerCityRedraw();
        StartPeriodicRedrawTimer();
    }
}

void ApplyRedrawAggressionChange()
{
    Logger::GetInstance().WriteLine(
        LogLevel::Info,
        "Settings UI: redraw aggression changed to "
        + std::string(PluginSettingText::ToString(g_Settings.redrawAggression))
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
    StopHeldKeyboardPan();
    ClearTrackedKeyStates("input settings changed");
    if (g_IsModernCamEnabled) {
        ClearNativeView3DToolState(View3DToolClearReason::WASDMovementChanged);
        ClearNativeToolWithQueryThenEsc();
        FocusView3DForNativeInput("Input settings changed focus handoff");
        ScheduleModernCameraFocusRestore();
    }
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

void RestoreWorldFocusAfterSettingsClosed()
{
    Logger::GetInstance().WriteLine(LogLevel::Info, "Settings UI: closed; restoring city focus.");
    FocusView3DForNativeInput("Settings window closed focus handoff");
    ScheduleView3DFocusRestore(false, "Settings window closed focus restore");
}

bool IsRightClickScrollingActive()
{
    return IsObservedOrPhysicalKeyDown(VK_RBUTTON);
}

bool IsVirtualKeyDown(int key)
{
    return IsObservedOrPhysicalKeyDown(key);
}

bool IsManualCameraDumpHotkey(WPARAM key)
{
    RefreshTrackedModifierStates();
    return key == kDumpCameraInfoKey
        && IsVirtualKeyDown(VK_CONTROL)
        && IsVirtualKeyDown(VK_MENU);
}

bool IsCommandShortcutModifierDown()
{
    RefreshTrackedModifierStates();
    return IsVirtualKeyDown(VK_CONTROL)
        || IsVirtualKeyDown(VK_MENU)
        || IsVirtualKeyDown(VK_LWIN)
        || IsVirtualKeyDown(VK_RWIN);
}

void LogKeyboardShortcutPassThrough(WPARAM key, const char* source)
{
    Logger::GetInstance().WriteLine(
        LogLevel::Verbose,
        std::string("Keyboard movement pass-through: command shortcut modifier is down. Source:")
        + (source ? source : "unknown")
        + " Key:" + FormatVirtualKey(key)
        + " Ctrl:" + (IsVirtualKeyDown(VK_CONTROL) ? "true" : "false")
        + " Shift:" + (IsVirtualKeyDown(VK_SHIFT) ? "true" : "false")
        + " Alt:" + (IsVirtualKeyDown(VK_MENU) ? "true" : "false")
        + " LWin:" + (IsVirtualKeyDown(VK_LWIN) ? "true" : "false")
        + " RWin:" + (IsVirtualKeyDown(VK_RWIN) ? "true" : "false"));
}

bool IsTextEditWindow(cIGZWin* window)
{
    cRZAutoRefCount<cIGZWinTextEdit> textEdit;
    return window && window->QueryInterface(GZIID_cIGZWinTextEdit, textEdit.AsPPVoid());
}

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

bool IsModernCameraWindowID(uint32_t id)
{
    return id >= kModernCameraWindowIDMin && id <= kModernCameraWindowIDMax;
}

bool FocusView3DForNativeInput(const char* context)
{
    cISC4AppPtr app;
    cIGZWin* mainWindow = app ? app->GetMainWindow() : nullptr;
    cIGZWin* parentWindow = mainWindow
        ? mainWindow->GetChildWindowFromID(SC4WindowID::MainApplication)
        : nullptr;
    cISC4View3DWin* view3D = nullptr;
    if (!parentWindow
        || !parentWindow->GetChildAs(
            SC4WindowID::View3D,
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
    if (focusedID == SC4WindowID::View3D || focusedID == SC4WindowID::View3DSurface) {
        return true;
    }
    if (IsModernCameraWindowID(focusedID)) {
        if (g_WindowManager.HasVisibleWindow()) {
            Logger::GetInstance().WriteLine(
                LogLevel::Verbose,
                "Keyboard capture pass-through: focused window is an open Modern Camera UI control. Focus:{"
                + DescribeGZWindow(focusedWindow)
                + "}");
            return false;
        }

        Logger::GetInstance().WriteLine(
            LogLevel::Verbose,
            "Keyboard focus recovery: focused window is a stale Modern Camera UI control. Focus:{"
            + DescribeGZWindow(focusedWindow)
            + "}");
        return FocusView3DForNativeInput("Keyboard stale Modern Camera UI focus recovery");
    }

    cISC4AppPtr app;
    cIGZWin* mainWindow = app ? app->GetMainWindow() : nullptr;
    cIGZWin* parentWindow = mainWindow
        ? mainWindow->GetChildWindowFromID(SC4WindowID::MainApplication)
        : nullptr;
    cISC4View3DWin* view3D = nullptr;
    bool cameraFocus = false;
    if (parentWindow
        && parentWindow->GetChildAs(
            SC4WindowID::View3D,
            kGZIID_cISC4View3DWin,
            reinterpret_cast<void**>(&view3D))) {
        cIGZWin* viewWindow = view3D->AsIGZWin();
        cameraFocus = focusedWindow == viewWindow
            || (viewWindow && viewWindow->IsWinInChildChain(focusedWindow));
        view3D->Release();
    }

    if (!cameraFocus) {
        Logger::GetInstance().WriteLine(
            LogLevel::Verbose,
            "Keyboard capture pass-through: focused window is not the 3D view. Focus:{"
            + DescribeGZWindow(focusedWindow)
            + "}");
    }

    return cameraFocus;
}

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

bool IsKeyboardPanVirtualKey(WPARAM key)
{
    return IsWASDVirtualKey(key) || IsArrowVirtualKey(key);
}

bool IsCapturedKeyboardPanVirtualKey(WPARAM key)
{
    return IsKeyboardPanVirtualKey(key);
}

bool IsWASDCharacter(WPARAM character)
{
    return character == 'W' || character == 'w'
        || character == 'A' || character == 'a'
        || character == 'S' || character == 's'
        || character == 'D' || character == 'd';
}

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

bool IsEscapeInput(UINT message, WPARAM key)
{
    return (message == WM_KEYDOWN
        || message == WM_SYSKEYDOWN
        || message == WM_KEYUP
        || message == WM_SYSKEYUP)
        && key == VK_ESCAPE;
}

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
        ? kKeyboardPanBoostMultiplier
        : 1.0f;
    const float scrollSpeed = kKeyboardPanNativeSpeed * speedMultiplier * g_Settings.panSensitivity;

    g_IsApplyingKeyboardPan = true;
    const bool nativeScrollResult = view3D->SetScrolling(true, directionAngle, scrollSpeed);
    g_IsApplyingKeyboardPan = false;

    Logger::GetInstance().WriteLineLazy(successLogLevel, [&]() {
        return "Keyboard native movement updated. Source:"
            + std::string(source ? source : "unknown")
            + " RightSteps:" + std::to_string(rightSteps)
            + " ForwardSteps:" + std::to_string(forwardSteps)
            + " DirectionAngle:" + std::to_string(directionAngle)
            + " ScrollSpeed:" + std::to_string(scrollSpeed)
            + " Result:" + (nativeScrollResult ? "true" : "false");
    });

    view3D->Release();
    return nativeScrollResult;
}

void StopHeldKeyboardPan()
{
    const bool hadHeldKey = HasOwnedKeyboardPanKey();
    ClearHeldKeyboardPanKeys();
    KillKeyboardPanTimer();
    if (hadHeldKey) {
        ApplyKeyboardPan("keyboard capture stopped", LogLevel::Info);
    }
}

void StopClassicAngleHold(const char* source)
{
    const bool hadHeldKey = HasOwnedClassicAngleKey();
    ClearClassicAngleKeys();
    if (hadHeldKey) {
        g_CameraController.EndClassicAngleHold(source ? source : "classic angle hold stopped");
    }
}

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
        ApplyKeyboardPan("keyboard key capture", LogLevel::Info);
        if (HasHeldKeyboardPanKey()) {
            StartKeyboardPanTimer();
        }
        else {
            KillKeyboardPanTimer();
        }
    }

    if (!pressed) {
        ReleaseKey(key, KeyOwner::ModernCameraPan);
    }
}

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
        g_CameraController.BeginClassicAngleHold("Alt key pressed");
    }
    else if (wasActive && !isActive) {
        g_CameraController.EndClassicAngleHold("Alt key released");
    }

    if (!pressed) {
        ReleaseKey(key, KeyOwner::ModernCameraClassicAngle);
    }
}

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

LRESULT CALLBACK KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam)
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

                if (!g_IsModernCamEnabled) {
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
                    return CallNextHookEx(g_KeyboardHook, nCode, wParam, lParam);
                }

                if (IsClassicAngleVirtualKey(keyboard->vkCode)) {
                    const bool cameraFocus = IsCameraKeyboardFocus();
                    if (ShouldCaptureClassicAngleKeyInFocus(keyboard->vkCode, cameraFocus)) {
                        HandleClassicAngleKeyState(keyboard->vkCode, isKeyDown);
                        return 1;
                    }
                    if (isKeyUp && HasOwnedClassicAngleKey()) {
                        HandleClassicAngleKeyState(keyboard->vkCode, false);
                        if (cameraFocus) {
                            return 1;
                        }
                    }
                }
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
        CancelTimer(g_IdleTimerID);

        TriggerCityRedraw();
        StartPeriodicRedrawTimer();
    }
}

VOID CALLBACK PeriodicRedrawTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime) {
    if (idEvent == g_PeriodicRedrawTimerID && g_PeriodicRedrawTimerID != 0) {
        CancelTimer(g_PeriodicRedrawTimerID);

        if (!IsMiddleMouseRotationActive()) {
            Logger::GetInstance().WriteLine(LogLevel::Verbose, "Periodic camera redraw timer fired.");
            TriggerCityRedraw();
        }

        StartPeriodicRedrawTimer();
    }
}

VOID CALLBACK KeyboardPanTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime) {
    if (idEvent == g_KeyboardPanTimerID && g_KeyboardPanTimerID != 0) {
        if (!HasHeldKeyboardPanKey() || !ShouldCaptureHeldKeyboardPan()) {
            StopHeldKeyboardPan();
            return;
        }

        ApplyKeyboardPan("held keyboard movement tick", LogLevel::Verbose);
    }
}

VOID CALLBACK ClearCameraDumpConfirmationTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime) {
    if (idEvent == g_CameraDumpConfirmationTimerID && g_CameraDumpConfirmationTimerID != 0) {
        KillCameraDumpConfirmationTimer();
        g_CameraController.ClearCameraDumpConfirmation();
    }
}

void ResetTransientInputState(const char* reason)
{
    StopHeldKeyboardPan();
    StopClassicAngleHold(reason);
    StopMiddleMouseRotation(reason, false);
    ClearTrackedKeyStates(reason);
}

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

bool ResumesCameraInteraction(UINT message)
{
    return message == WM_LBUTTONDOWN
        || message == WM_RBUTTONDOWN
        || message == WM_MBUTTONDOWN
        || message == WM_MOUSEWHEEL
        || message == WM_KEYDOWN
        || message == WM_SYSKEYDOWN;
}

bool RouteMouseInputToVisiblePluginWindow(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam,
    bool& handled)
{
    if (!g_WindowManager.HasVisibleWindow()
        || !IsCanvasMouseMessage(message)
        || IsMiddleMouseRotationActive()) {
        return false;
    }

    // WM_MOUSEWHEEL supplies screen coordinates. Convert them to the canvas
    // coordinates used by the native SC4 window hierarchy.
    POINT cursor = MakePointFromLParam(lParam);
    if (message == WM_MOUSEWHEEL) {
        if (!ScreenToClient(window, &cursor)) {
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

bool HandleManualCameraDumpHotkey(WPARAM key, LPARAM lParam, bool& handled)
{
    const bool isRepeat = (lParam & kPreviousKeyStateMask) != 0;
    if (!IsManualCameraDumpHotkey(key) || isRepeat || !IsCameraKeyboardFocus()) {
        return false;
    }

    Logger::GetInstance().WriteLine(
        LogLevel::Info,
        "Canvas WinProc Filter: Ctrl+Alt+F8 pressed, dumping camera info.");
    g_CameraController.DumpCameraInfo("Ctrl+Alt+F8 hotkey");
    if (g_CameraController.ShowCameraDumpConfirmation()) {
        StartCameraDumpConfirmationTimer();
    }
    handled = true;
    return true;
}

bool HandleMiddleMouseButtonDown(HWND window, LPARAM lParam, bool& handled)
{
    if (!g_IsModernCamEnabled) {
        return false;
    }

    Logger& log = Logger::GetInstance();
    log.WriteLine(LogLevel::Verbose, "Canvas WinProc Filter: WM_MBUTTONDOWN (Middle Mouse Down)");
    if (!ClaimKey(VK_MBUTTON, KeyOwner::ModernCameraRotation)) {
        log.WriteLine(
            LogLevel::Verbose,
            "Middle mouse camera rotation ignored because MButton is owned by another feature.");
        return false;
    }

    g_LastMousePos = MakePointFromLParam(lParam);
    const bool beganRotation = g_CameraController.BeginRotationGesture();
    SetCapture(window);
    g_CapturedMouseWindow = window;
    SetRotationCursorVisible(false);
    KillRedrawTimers();
    log.WriteLine(
        LogLevel::Verbose,
        std::string("Middle mouse camera rotation started. BeginResult:")
        + (beganRotation ? "true" : "false")
        + " CaptureWindow:" + std::to_string(reinterpret_cast<intptr_t>(window)));
    handled = true;
    return true;
}

bool HandleMiddleMouseButtonUp(bool& handled)
{
    if (!g_IsModernCamEnabled) {
        return false;
    }

    Logger::GetInstance().WriteLine(
        LogLevel::Verbose,
        "Canvas WinProc Filter: WM_MBUTTONUP (Middle Mouse Up)");
    StopMiddleMouseRotation("middle mouse released", true);
    handled = true;
    return true;
}

bool HandleMiddleMouseMove(LPARAM lParam, bool& handled)
{
    if (!g_IsModernCamEnabled || !IsMiddleMouseRotationActive()) {
        return false;
    }

    const POINT mousePos = MakePointFromLParam(lParam);
    const int deltaX = mousePos.x - g_LastMousePos.x;
    const int deltaY = mousePos.y - g_LastMousePos.y;
    if (deltaX == 0 && deltaY == 0) {
        handled = true;
        return true;
    }

    const float yawDelta = static_cast<float>(deltaX)
        * kMouseRotationSensitivity
        * g_Settings.rotationSensitivity;
    float pitchDelta = static_cast<float>(deltaY)
        * kMouseRotationSensitivity
        * g_Settings.rotationSensitivity;
    if (g_Settings.invertVertical) {
        pitchDelta = -pitchDelta;
    }

    g_CameraController.ApplyDelta(pitchDelta, yawDelta, true);
    g_LastMousePos = mousePos;
    handled = true;
    return true;
}

void LogNativeWheelHitTest(const POINT& cursor, const NativeWheelHitTest& hitTest)
{
    Logger::GetInstance().WriteLine(
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
}

bool HandleCameraZoom(HWND window, WPARAM wParam, LPARAM lParam, bool& handled)
{
    if (!g_IsModernCamEnabled) {
        return false;
    }

    Logger& log = Logger::GetInstance();
    const short wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
    log.WriteLine(
        LogLevel::Verbose,
        "Canvas WinProc Filter: WM_MOUSEWHEEL (Delta: " + std::to_string(wheelDelta) + ")");

    POINT cursor = MakePointFromLParam(lParam);
    if (!ScreenToClient(window, &cursor)) {
        log.WriteLine(
            LogLevel::Warning,
            "Mouse wheel hit-test: failed to convert screen coordinates; using camera zoom.");
    }
    else {
        const NativeWheelHitTest hitTest = HitTestNativeWheelTarget(cursor.x, cursor.y);
        LogNativeWheelHitTest(cursor, hitTest);
        if (hitTest.resolved && !hitTest.overView3D) {
            log.WriteLine(
                LogLevel::Info,
                "Mouse wheel hit-test: passing wheel through to native SC4 UI.");
            return false;
        }
    }

    bool zoomChanged = false;
    const int32_t adjustedZoomDelta = static_cast<int32_t>(
        std::lround(static_cast<float>(wheelDelta) * g_Settings.zoomSensitivity));
    if (!g_CameraController.ZoomByWheel(adjustedZoomDelta, zoomChanged)) {
        return false;
    }

    if (zoomChanged) {
        log.WriteLine(LogLevel::Verbose, "Camera zoom changed: scheduling redraw sequence.");
        StartIdleTimer(kZoomIdleRedrawDelayMs);
    }
    handled = true;
    return true;
}

LRESULT HandleCanvasMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, bool& handled)
{
    if (hWnd && IsWindow(hWnd)) {
        g_CanvasWindow = hWnd;
    }

    if (!g_IsCityLoaded) {
        return 0;
    }

    if (uMsg == WM_KILLFOCUS) {
        ResetTransientInputState("canvas focus lost");
        return 0;
    }
    if (uMsg == WM_ACTIVATEAPP && wParam == FALSE) {
        ResetTransientInputState("application deactivated");
        return 0;
    }

    if (!g_IsModernCamEnabled) {
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
    }

    if (RouteMouseInputToVisiblePluginWindow(hWnd, uMsg, wParam, lParam, handled)) {
        return 0;
    }

    if (ResumesCameraInteraction(uMsg)
        && g_CameraController.IsSavePreviewNormalizationActive()) {
        g_CameraController.EndSavePreviewNormalization();
    }

    const bool isKeyboardPanKeyMessage = uMsg == WM_KEYDOWN
        || uMsg == WM_SYSKEYDOWN
        || uMsg == WM_KEYUP
        || uMsg == WM_SYSKEYUP;
    const bool isWASDCharMessage = uMsg == WM_CHAR || uMsg == WM_SYSCHAR;

    if (isKeyboardPanKeyMessage && IsClassicAngleVirtualKey(wParam)) {
        const bool cameraFocus = IsCameraKeyboardFocus();
        if (ShouldCaptureClassicAngleKeyInFocus(wParam, cameraFocus)) {
            HandleClassicAngleKeyState(wParam, uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN);
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
    else if (isKeyboardPanKeyMessage && IsCapturedKeyboardPanVirtualKey(wParam)) {
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
    else if (isWASDCharMessage && IsWASDCharacter(wParam)) {
        const bool cameraFocus = IsCameraKeyboardFocus();
        if (ShouldCaptureKeyboardPanKeyInFocus(wParam, cameraFocus)) {
            handled = true;
            return 0;
        }
    }

    switch (uMsg) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (HandleManualCameraDumpHotkey(wParam, lParam, handled)) {
            return 0;
        }
        break;
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
        break;
    }
    case WM_RBUTTONUP: {
        LogMouseButtonEvent("WM_RBUTTONUP (Right Mouse Up)", MakePointFromLParam(lParam));
        break;
    }
    case WM_MBUTTONDOWN:
        if (HandleMiddleMouseButtonDown(hWnd, lParam, handled)) {
            return 0;
        }
        break;
    case WM_MBUTTONUP:
        if (HandleMiddleMouseButtonUp(handled)) {
            return 0;
        }
        break;
    case WM_MOUSEMOVE:
        if (HandleMiddleMouseMove(lParam, handled)) {
            return 0;
        }
        break;
    case WM_MOUSEWHEEL:
        if (HandleCameraZoom(hWnd, wParam, lParam, handled)) {
            return 0;
        }
        break;
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
        g_RegisterCanvasWinProcFilterCallback = [this]() { RegisterCanvasWinProcFilter(); };
        g_UnregisterCanvasWinProcFilterCallback = [this]() { UnregisterCanvasWinProcFilter(); };
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
        return kDirectorID;
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
            RestoreWorldFocusAfterSettingsClosed,
        });

        ApplyDebugLoggingChange();

        Logger::GetInstance().WriteLine(
            LogLevel::Info,
            std::string("Modern Camera Enabled: ") + (g_IsModernCamEnabled ? "true" : "false")
            + (g_IsModernCamEnabled ? "" : " (Classic native camera mode)"));

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
            g_IsNativeUIHidden = false;
            ResetInputState();
            InstallMinimizeUIHook();
            if (g_IsModernCamEnabled) {
                RegisterCanvasWinProcFilter();
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
            g_CameraController.AbandonSavePreviewNormalization();
            g_IsCityLoaded = false;
            UninstallSetScrollingHook();
            UninstallMinimizeUIHook();
            ResetInputState();
            UninstallKeyboardHook();
            UnregisterCanvasWinProcFilter();
        }

        return true;
    }

    LRESULT FilterMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, bool& handled) override
    {
        return HandleCanvasMessage(hWnd, uMsg, wParam, lParam, handled);
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
