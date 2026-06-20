#include "cIGZCanvasW32.h"
#include "cIGZGraphicSystem.h"
#include "cIGZMessage2Standard.h"
#include "cIGZMessageServer2.h"
#include "cIGZWinProcFilterW32.h"
#include "cRZMessage2COMDirector.h"
#include "GZServPtrs.h"
#include "Logger.h"
#include "SC4CameraController.h"
#include "SC4VersionDetection.h"
#include <Windows.h>
#include <windowsx.h>

#include <cstdlib>
#include <string>

static constexpr uint32_t kSC4MessagePostCityInit = 0x26D31EC1;
static constexpr uint32_t kSC4MessagePreCityShutdown = 0x26D31EC2;
static constexpr uint32_t kSC4MessagePreSave = 0x26C63343;
static constexpr float kMouseRotationSensitivity = 0.005f;
static constexpr UINT kPanIdleRedrawDelayMs = 1000;
static constexpr UINT kZoomIdleRedrawDelayMs = 1500;
static constexpr UINT kCameraDumpConfirmationDelayMs = 2500;
static constexpr UINT kNativeCameraBaselineDelayMs = 1000;
static constexpr UINT kDumpCameraInfoKey = VK_F8;

// Global State
bool g_IsCityLoaded = false;
bool g_IsModernCamEnabled = true;

// Cinematic Camera State
bool g_IsMiddleMouseDown = false;
POINT g_LastMousePos = { 0, 0 };
HWND g_CapturedMouseWindow = NULL;
SC4CameraController g_CameraController;

UINT_PTR g_IdleTimerID = 0;
UINT_PTR g_CameraDumpConfirmationTimerID = 0;
UINT_PTR g_NativeCameraBaselineTimerID = 0;

VOID CALLBACK RedrawTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
VOID CALLBACK ClearCameraDumpConfirmationTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
VOID CALLBACK NativeCameraBaselineTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);

void KillIdleTimer()
{
    if (g_IdleTimerID != 0) {
        KillTimer(NULL, g_IdleTimerID);
        g_IdleTimerID = 0;
    }
}

void StartIdleTimer(UINT delayMs)
{
    KillIdleTimer();
    g_IdleTimerID = SetTimer(NULL, 0, delayMs, RedrawTimerProc);
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

    if (!g_IsModernCamEnabled) {
        return;
    }

    log.WriteLine(LogLevel::Info, "Executing ForceFullRedraw()...");

    if (g_CameraController.ForceFullRedraw()) {
        log.WriteLine(LogLevel::Info, "ForceFullRedraw() Success.");
    }
    else {
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
        LogLevel::Info,
        std::string("Canvas WinProc Filter: ") + name + " at X:" + std::to_string(point.x) + " Y:" + std::to_string(point.y));
}

void ResetInputState()
{
    KillIdleTimer();
    KillCameraDumpConfirmationTimer();
    KillNativeCameraBaselineTimer();
    g_CameraController.ClearCameraDumpConfirmation();

    if (g_CapturedMouseWindow != NULL && GetCapture() == g_CapturedMouseWindow) {
        ReleaseCapture();
    }

    g_IsMiddleMouseDown = false;
    g_CapturedMouseWindow = NULL;
    g_CameraController.Reset();
}

std::string GetDefaultLogPath()
{
    char* userProfile = nullptr;
    size_t len = 0;
    _dupenv_s(&userProfile, &len, "USERPROFILE");

    std::string logPath = "SC4-3DMouseCam.log";
    if (userProfile) {
        logPath = std::string(userProfile) + "\\Documents\\SimCity 4\\Plugins\\SC4-3DMouseCam.log";
        free(userProfile);
    }

    return logPath;
}

bool CheckGameVersion()
{
    const uint16_t gameVersion = SC4VersionDetection::GetGameVersion();
    Logger::GetInstance().WriteLine(LogLevel::Info, "Detected SimCity 4 game version: " + std::to_string(gameVersion));

    if (!SC4VersionDetection::IsDigitalDistributionVersion()) {
        Logger::GetInstance().WriteLine(
            LogLevel::Error,
            "Unsupported Game Version. This plugin currently only supports the Steam/GOG/EA digital distribution build.");
    }

    return true;
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
        log.WriteLine(LogLevel::Info, "Camera Idle Timer Reached 0: Firing TriggerCityRedraw");
        KillIdleTimer();
        TriggerCityRedraw();
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

    const bool resumesCameraInteraction = uMsg == WM_LBUTTONDOWN
        || uMsg == WM_RBUTTONDOWN
        || uMsg == WM_MBUTTONDOWN
        || uMsg == WM_MOUSEWHEEL
        || uMsg == WM_KEYDOWN;
    if (resumesCameraInteraction && g_CameraController.IsSavePreviewNormalizationActive()) {
        g_CameraController.EndSavePreviewNormalization();
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

        log.WriteLine(LogLevel::Info, "Canvas WinProc Filter: WM_MBUTTONDOWN (Middle Mouse Down)");
        if (!g_CameraController.HasNativeCameraState()) {
            g_CameraController.DumpCameraInfo("immediately before first custom rotation");
        }
        g_IsMiddleMouseDown = true;
        g_LastMousePos = MakePointFromLParam(lParam);
		g_CameraController.BeginRotationGesture();
        SetCapture(hWnd);
        g_CapturedMouseWindow = hWnd;
        if (g_IdleTimerID != 0) {
            // log.WriteLine(LogLevel::Info, "Action Interrupted: Killing Idle Timer");
            KillIdleTimer();
        }
        handled = true;
        return 0;
    }
    case WM_MBUTTONUP: {
        if (!g_IsModernCamEnabled) {
            break;
        }

        log.WriteLine(LogLevel::Info, "Canvas WinProc Filter: WM_MBUTTONUP (Middle Mouse Up)");
        if (g_CapturedMouseWindow != NULL && GetCapture() == g_CapturedMouseWindow) {
            ReleaseCapture();
        }
        g_IsMiddleMouseDown = false;
		g_CameraController.EndRotationGesture();
        g_CapturedMouseWindow = NULL;
        log.WriteLine(LogLevel::Info, "Pan Stopped: Starting 1000ms Idle Timer");
        StartIdleTimer(kPanIdleRedrawDelayMs);
        handled = true;
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (g_IsModernCamEnabled && g_IsMiddleMouseDown) {
            POINT mousePos = MakePointFromLParam(lParam);
            int deltaX = mousePos.x - g_LastMousePos.x;
            int deltaY = mousePos.y - g_LastMousePos.y;

			float yawDelta = static_cast<float>(deltaX) * kMouseRotationSensitivity;
			const float pitchDelta = static_cast<float>(deltaY) * kMouseRotationSensitivity;

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
        log.WriteLine(LogLevel::Info, "Canvas WinProc Filter: WM_MOUSEWHEEL (Delta: " + std::to_string(zDelta) + ")");

        if (!g_CameraController.HasNativeCameraState()) {
            g_CameraController.DumpCameraInfo("immediately before first custom zoom");
        }

        bool zoomChanged = false;
        if (g_CameraController.ZoomByWheel(zDelta, zoomChanged)) {
            if (zoomChanged) {
                log.WriteLine(LogLevel::Info, "Camera Zoom: Starting/Resetting 1500ms Idle Timer");
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

class cSC4MouseCamDirector : public cRZMessage2COMDirector, public cIGZWinProcFilterW32
{
public:
    cSC4MouseCamDirector()
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
        Logger::GetInstance().Initialize(GetDefaultLogPath());
        Logger::GetInstance().WriteLine(LogLevel::Info, "Plugin Loaded. Waiting for city to load...");
        Logger::GetInstance().WriteLine(
            LogLevel::Info,
            std::string("Modern Camera Enabled: ") + (g_IsModernCamEnabled ? "true" : "false")
            + (g_IsModernCamEnabled ? "" : " (passive diagnostics mode)"));

        if (!CheckGameVersion()) {
            return true;
        }

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
            g_IsCityLoaded = true;
            ResetInputState();
            RegisterCanvasWinProcFilter();
            StartNativeCameraBaselineTimer();
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
            g_CameraController.DumpCameraInfo("pre-city-shutdown");
            g_CameraController.AbandonSavePreviewNormalization();
            g_IsCityLoaded = false;
            ResetInputState();
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
    static cSC4MouseCamDirector sDirector;
    return &sDirector;
}
