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
#include "SC4VersionDetection.h"
#include "SC4WindowManager.h"
#include <Windows.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
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
static constexpr size_t kView3DWinSetScrollingVTableIndex = 33;
static constexpr size_t kView3DWinMinimizeUIVTableIndex = 55;
static constexpr float kMouseRotationSensitivity = 0.005f;
static constexpr UINT kPanIdleRedrawDelayMs = 1000;
static constexpr UINT kKeyboardPanTickMs = 33;
static constexpr float kKeyboardPanNativeSpeed = 0.25f;
static constexpr float kKeyboardPanBoostMultiplier = 3.0f;
static constexpr UINT kZoomIdleRedrawDelayMs = 1500;
static constexpr UINT kHighPeriodicRedrawDelayMs = 1000;
static constexpr UINT kExtremePeriodicRedrawDelayMs = 100;
static constexpr UINT kCameraDumpConfirmationDelayMs = 2500;
static constexpr UINT kNativeCameraBaselineDelayMs = 1000;
static constexpr UINT kNativeToolClearEscDelayMs = 50;
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
    WASDMovementEnabled,
};

// Cinematic Camera State
bool g_IsMiddleMouseDown = false;
bool g_IsRightMouseDown = false;
bool g_WASDKeyWDown = false;
bool g_WASDKeyADown = false;
bool g_WASDKeySDown = false;
bool g_WASDKeyDDown = false;
bool g_ArrowKeyUpDown = false;
bool g_ArrowKeyLeftDown = false;
bool g_ArrowKeyDownDown = false;
bool g_ArrowKeyRightDown = false;
bool g_IsApplyingKeyboardScrolling = false;
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
UINT_PTR g_NativeToolClearEscTimerID = 0;

VOID CALLBACK RedrawTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
VOID CALLBACK PeriodicRedrawTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
VOID CALLBACK KeyboardPanTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
VOID CALLBACK ClearCameraDumpConfirmationTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
VOID CALLBACK NativeCameraBaselineTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
VOID CALLBACK NativeToolClearEscTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
LRESULT CALLBACK KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam);
void StopHeldKeyboardMovement(bool scheduleRedraw);
bool IsRightClickScrollingActive();
bool FocusView3DForNativeInput(const char* context);
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

const char* GetView3DToolClearReasonName(View3DToolClearReason reason)
{
    switch (reason) {
    case View3DToolClearReason::ModernCameraEnabled:
        return "Modern camera enabled";
    case View3DToolClearReason::WASDMovementEnabled:
        return "WASD movement enabled";
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

    const bool removedControl = view3D->RemoveCurrentViewInputControl(true);
    const bool removedAllControls = view3D->RemoveAllViewInputControls(true);
    const bool stoppedScrolling = view3D->ScrollStop();
    const bool killedKeyboardScrolling = view3D->KillKeyboardScrolling();
    cISC4ViewInputControl* remainingControl = view3D->GetCurrentViewInputControl();
    Logger::GetInstance().WriteLine(
        LogLevel::Info,
        std::string(reasonName)
        + ": native view input reset. RemoveCurrentViewInputControl="
        + (removedControl ? "true" : "false")
        + " RemoveAllViewInputControls="
        + (removedAllControls ? "true" : "false")
        + " ScrollStop="
        + (stoppedScrolling ? "true" : "false")
        + " KillKeyboardScrolling="
        + (killedKeyboardScrolling ? "true" : "false")
        + " RemainingToolID:"
        + (remainingControl ? FormatWindowID(remainingControl->GetID()) : "none"));

    view3D->Release();
}

void KillNativeToolClearEscTimer()
{
    if (g_NativeToolClearEscTimerID != 0) {
        KillTimer(NULL, g_NativeToolClearEscTimerID);
        g_NativeToolClearEscTimerID = 0;
    }
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

bool HasHeldKeyboardMovementKey()
{
    return g_WASDKeyWDown || g_WASDKeyADown || g_WASDKeySDown || g_WASDKeyDDown
        || g_ArrowKeyUpDown || g_ArrowKeyLeftDown || g_ArrowKeyDownDown || g_ArrowKeyRightDown;
}

void ClearHeldKeyboardMovementKeys()
{
    g_WASDKeyWDown = false;
    g_WASDKeyADown = false;
    g_WASDKeySDown = false;
    g_WASDKeyDDown = false;
    g_ArrowKeyUpDown = false;
    g_ArrowKeyLeftDown = false;
    g_ArrowKeyDownDown = false;
    g_ArrowKeyRightDown = false;
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

VOID CALLBACK NativeToolClearEscTimerProc(HWND, UINT, UINT_PTR idEvent, DWORD)
{
    if (idEvent == g_NativeToolClearEscTimerID && g_NativeToolClearEscTimerID != 0) {
        KillNativeToolClearEscTimer();
        FocusView3DForNativeInput("Native view input clear Esc");
        SendVirtualKeyTap(VK_ESCAPE, "Esc");
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

	const char* source = g_IsApplyingKeyboardScrolling
		? "keyboard SetScrolling"
		: (IsRightClickScrollingActive() ? "right mouse SetScrolling" : "native SetScrolling");
	float adjustedX = x;
	bool blockedByBounds = false;
	if (g_IsCityLoaded && g_IsModernCamEnabled && scrolling) {
		g_CameraController.AdjustScrollForCityBounds(x, z, adjustedX, blockedByBounds, source);
		if (blockedByBounds) {
			const bool stopped = original(view3D, false, 0.0f, 0.0f);
			Logger::GetInstance().WriteLine(
				LogLevel::Verbose,
				std::string("View3D SetScrolling blocked at city bounds. Source:")
				+ source
				+ " X:" + std::to_string(x)
				+ " Z:" + std::to_string(z)
				+ " StopResult:" + (stopped ? "true" : "false"));
			return stopped;
		}
	}

	const bool result = original(view3D, scrolling, adjustedX, z);
	Logger::GetInstance().WriteLine(
		LogLevel::Verbose,
		std::string("View3D SetScrolling observed. Source:")
		+ (g_IsApplyingKeyboardScrolling ? "ModernCameraKeyboard" : "NativeOrOther")
		+ " Scrolling:" + (scrolling ? "true" : "false")
		+ " X:" + std::to_string(x)
		+ " AdjustedX:" + std::to_string(adjustedX)
		+ " Z:" + std::to_string(z)
		+ " Result:" + (result ? "true" : "false"));

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

void ResetInputState()
{
    KillRedrawTimers();
    StopHeldKeyboardMovement(true);
    KillCameraDumpConfirmationTimer();
    KillNativeCameraBaselineTimer();
    KillNativeToolClearEscTimer();
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
    StopHeldKeyboardMovement(true);
    KillCameraDumpConfirmationTimer();
    KillNativeCameraBaselineTimer();
    KillNativeToolClearEscTimer();

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
    StopHeldKeyboardMovement(true);

    if (!enabled) {
        ResetInputState();
    }
    else {
        g_CameraController.EnsureTargetNearCityCenterIfOutOfBounds("modern camera enabled");
        ClearNativeView3DToolState(View3DToolClearReason::ModernCameraEnabled);
        ClearNativeToolWithQueryThenEsc();
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
    StopHeldKeyboardMovement(true);
    if (g_IsModernCamEnabled && g_Settings.wasdMovement) {
        ClearNativeView3DToolState(View3DToolClearReason::WASDMovementEnabled);
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

bool IsRightClickScrollingActive()
{
    return g_IsRightMouseDown || ((GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);
}

bool IsVirtualKeyDown(int key)
{
    return (GetAsyncKeyState(key) & 0x8000) != 0;
}

bool IsCommandShortcutModifierDown()
{
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
        + " Key:" + std::to_string(static_cast<uint32_t>(key)));
}

bool IsTextEditWindow(cIGZWin* window)
{
    cRZAutoRefCount<cIGZWinTextEdit> textEdit;
    return window && window->QueryInterface(GZIID_cIGZWinTextEdit, textEdit.AsPPVoid());
}

bool IsModernCameraWindowID(uint32_t id)
{
    return id >= kModernCameraWindowIDMin && id <= kModernCameraWindowIDMax;
}

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
    Logger::GetInstance().WriteLine(
        focused ? LogLevel::Verbose : LogLevel::Warning,
        std::string(context ? context : "native input")
        + ": 3D view keyboard focus "
        + (focused ? "set." : "failed."));
    view3D->Release();
    return focused;
}

bool IsCameraKeyboardFocus()
{
    cIGZWinMgrPtr winMgr;
    if (!winMgr) {
        return true;
    }

    cIGZWin* focusedWindow = winMgr->GZGetFocus();
    if (!focusedWindow) {
        return true;
    }

    if (IsTextEditWindow(focusedWindow)) {
        Logger::GetInstance().WriteLine(
            LogLevel::Verbose,
            "WASD keyboard pass-through: focused window is a text edit. Focus:{"
            + DescribeGZWindow(focusedWindow)
            + "}");
        return false;
    }

    const uint32_t focusedID = focusedWindow->GetID();
    if (focusedID == kGZWin_SC4View3DWin || focusedID == kGZWin_SC4View3DSurface) {
        return true;
    }
    if (IsModernCameraWindowID(focusedID)) {
        if (g_WindowManager.HasVisibleWindow()) {
            Logger::GetInstance().WriteLine(
                LogLevel::Verbose,
                "WASD keyboard pass-through: focused window is an open Modern Camera UI control. Focus:{"
                + DescribeGZWindow(focusedWindow)
                + "}");
            return false;
        }

        Logger::GetInstance().WriteLine(
            LogLevel::Verbose,
            "WASD keyboard pass-through: focused window is a stale Modern Camera UI control. Focus:{"
            + DescribeGZWindow(focusedWindow)
            + "}");
        return false;
    }

    cISC4AppPtr app;
    if (!app) {
        return true;
    }

    cIGZWin* mainWindow = app->GetMainWindow();
    if (focusedWindow == mainWindow) {
        return true;
    }

    cIGZWin* parentWindow = mainWindow ? mainWindow->GetChildWindowFromID(kGZWin_WinSC4App) : nullptr;
    if (focusedWindow == parentWindow) {
        return true;
    }

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
        Logger::GetInstance().WriteLine(
            LogLevel::Verbose,
            "WASD keyboard pass-through: focused window is not the 3D view. Focus:{"
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

bool IsKeyboardMovementVirtualKey(WPARAM key)
{
    return IsWASDVirtualKey(key) || IsArrowVirtualKey(key);
}

bool IsWASDCharacter(WPARAM character)
{
    return character == 'W' || character == 'w'
        || character == 'A' || character == 'a'
        || character == 'S' || character == 's'
        || character == 'D' || character == 'd';
}

bool* GetKeyboardMovementKeyState(WPARAM key)
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
    case VK_UP:
        return &g_ArrowKeyUpDown;
    case VK_LEFT:
        return &g_ArrowKeyLeftDown;
    case VK_DOWN:
        return &g_ArrowKeyDownDown;
    case VK_RIGHT:
        return &g_ArrowKeyRightDown;
    default:
        return nullptr;
    }
}

bool ShouldCaptureKeyboardMovementKey(WPARAM key)
{
    return g_IsCityLoaded
        && g_IsModernCamEnabled
        && (IsArrowVirtualKey(key) || g_Settings.wasdMovement)
        && !IsCommandShortcutModifierDown()
        && IsCameraKeyboardFocus()
        && !IsRightClickScrollingActive();
}

bool ShouldCaptureHeldKeyboardMovement()
{
    return g_IsCityLoaded
        && g_IsModernCamEnabled
        && !IsCommandShortcutModifierDown()
        && IsCameraKeyboardFocus()
        && !IsRightClickScrollingActive()
        && (g_ArrowKeyUpDown
            || g_ArrowKeyLeftDown
            || g_ArrowKeyDownDown
            || g_ArrowKeyRightDown
            || (g_Settings.wasdMovement
                && (g_WASDKeyWDown || g_WASDKeyADown || g_WASDKeySDown || g_WASDKeyDDown)));
}

bool ApplyKeyboardMovement(const char* source, LogLevel successLogLevel)
{
    cISC4View3DWin* view3D = GetView3DWinForInput(source ? source : "keyboard movement");
    if (!view3D) {
        return false;
    }

    const float rightSteps = ((g_WASDKeyDDown || g_ArrowKeyRightDown) ? 1.0f : 0.0f)
        - ((g_WASDKeyADown || g_ArrowKeyLeftDown) ? 1.0f : 0.0f);
    const float forwardSteps = ((g_WASDKeyWDown || g_ArrowKeyUpDown) ? 1.0f : 0.0f)
        - ((g_WASDKeySDown || g_ArrowKeyDownDown) ? 1.0f : 0.0f);
    if (rightSteps == 0.0f && forwardSteps == 0.0f) {
        const bool stopped = view3D->ScrollStop();
        Logger::GetInstance().WriteLine(
            successLogLevel,
            "Keyboard movement stopped. Source:"
            + std::string(source ? source : "unknown")
            + " Result:" + (stopped ? "true" : "false"));
        view3D->Release();
        return stopped;
    }

    const float directionAngle = std::atan2(-forwardSteps, rightSteps);
    const float speedMultiplier = ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0)
        ? kKeyboardPanBoostMultiplier
        : 1.0f;
    const float scrollSpeed = kKeyboardPanNativeSpeed * speedMultiplier * g_Settings.panSensitivity;

    g_IsApplyingKeyboardScrolling = true;
    const bool nativeScrollResult = view3D->SetScrolling(true, directionAngle, scrollSpeed);
    g_IsApplyingKeyboardScrolling = false;

    Logger::GetInstance().WriteLine(
        successLogLevel,
        "Keyboard native movement updated. Source:"
        + std::string(source ? source : "unknown")
        + " RightSteps:" + std::to_string(rightSteps)
        + " ForwardSteps:" + std::to_string(forwardSteps)
        + " DirectionAngle:" + std::to_string(directionAngle)
        + " ScrollSpeed:" + std::to_string(scrollSpeed)
        + " Result:" + (nativeScrollResult ? "true" : "false"));

    view3D->Release();
    return nativeScrollResult;
}

void StopHeldKeyboardMovement(bool)
{
    const bool hadHeldKey = HasHeldKeyboardMovementKey();
    ClearHeldKeyboardMovementKeys();
    KillKeyboardPanTimer();
    if (hadHeldKey) {
        ApplyKeyboardMovement("keyboard capture stopped", LogLevel::Info);
    }
}

void HandleKeyboardMovementKeyState(WPARAM key, bool pressed)
{
    bool* keyState = GetKeyboardMovementKeyState(key);
    if (!keyState) {
        return;
    }

    if (*keyState != pressed) {
        *keyState = pressed;
        ApplyKeyboardMovement("keyboard key capture", LogLevel::Info);
        if (HasHeldKeyboardMovementKey()) {
            StartKeyboardPanTimer();
        }
        else {
            KillKeyboardPanTimer();
        }
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
        Logger::GetInstance().WriteLine(LogLevel::Info, "Registered low-level modern camera keyboard hook.");
    }
    else {
        Logger::GetInstance().WriteLine(
            LogLevel::Warning,
            "Failed to register low-level modern camera keyboard hook. GetLastError="
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
            "Failed to unregister low-level modern camera keyboard hook. GetLastError="
            + std::to_string(GetLastError()));
    }
    else {
        Logger::GetInstance().WriteLine(LogLevel::Info, "Unregistered low-level modern camera keyboard hook.");
    }
    g_KeyboardHook = NULL;
}

LRESULT CALLBACK KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION) {
        const KBDLLHOOKSTRUCT* keyboard = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        const bool isKeyDown = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
        const bool isKeyUp = wParam == WM_KEYUP || wParam == WM_SYSKEYUP;

        if (keyboard
            && (isKeyDown || isKeyUp)
            && IsKeyboardMovementVirtualKey(keyboard->vkCode)) {
            DWORD foregroundProcessID = 0;
            GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcessID);
            if (foregroundProcessID == GetCurrentProcessId()) {
                if (IsCommandShortcutModifierDown()) {
                    LogKeyboardShortcutPassThrough(keyboard->vkCode, "low-level keyboard hook");
                    StopHeldKeyboardMovement(true);
                    return CallNextHookEx(g_KeyboardHook, nCode, wParam, lParam);
                }
                if (ShouldCaptureKeyboardMovementKey(keyboard->vkCode)) {
                    HandleKeyboardMovementKeyState(keyboard->vkCode, isKeyDown);
                    return 1;
                }
                if (isKeyUp && HasHeldKeyboardMovementKey()) {
                    HandleKeyboardMovementKeyState(keyboard->vkCode, false);
                    return 1;
                }
                StopHeldKeyboardMovement(true);
            }
            else if (isKeyUp && HasHeldKeyboardMovementKey()) {
                HandleKeyboardMovementKeyState(keyboard->vkCode, false);
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
        if (!HasHeldKeyboardMovementKey() || !ShouldCaptureHeldKeyboardMovement()) {
            StopHeldKeyboardMovement(true);
            return;
        }

        ApplyKeyboardMovement("held keyboard movement tick", LogLevel::Verbose);
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

    const bool isKeyboardMovementKeyMessage = uMsg == WM_KEYDOWN
        || uMsg == WM_SYSKEYDOWN
        || uMsg == WM_KEYUP
        || uMsg == WM_SYSKEYUP;
    const bool isWASDCharMessage = uMsg == WM_CHAR || uMsg == WM_SYSCHAR;

    if (isKeyboardMovementKeyMessage && IsKeyboardMovementVirtualKey(wParam)) {
        if (IsCommandShortcutModifierDown()) {
            LogKeyboardShortcutPassThrough(wParam, "canvas WinProc filter");
            StopHeldKeyboardMovement(true);
            return 0;
        }
        if (ShouldCaptureKeyboardMovementKey(wParam)) {
            HandleKeyboardMovementKeyState(wParam, uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN);
            handled = true;
            return 0;
        }
        if ((uMsg == WM_KEYUP || uMsg == WM_SYSKEYUP) && HasHeldKeyboardMovementKey()) {
            HandleKeyboardMovementKeyState(wParam, false);
            handled = true;
            return 0;
        }
        StopHeldKeyboardMovement(true);
    }
    else if (isWASDCharMessage && IsWASDCharacter(wParam)) {
        if (ShouldCaptureKeyboardMovementKey(wParam)) {
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
        g_IsRightMouseDown = true;
        StopHeldKeyboardMovement(true);
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
            g_IsNativeUIHidden = false;
            ResetInputState();
            InstallMinimizeUIHook();
            InstallSetScrollingHook();
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
