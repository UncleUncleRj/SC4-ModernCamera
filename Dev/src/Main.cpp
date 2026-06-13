#include <Windows.h>
#include "cRZCOMDllDirector.h"
#include "Logger.h"
#include "cIGZMessageServer2.h"
#include "cIGZMessageTarget2.h"
#include "cIGZMessage2Standard.h"
#include "GZServPtrs.h"
#include "SC4VersionDetection.h"
#include "cISC4App.h"
#include "cIGZWin.h"
#include "cISC4View3DWin.h"
#include "cISC43DRender.h"

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <string>

// SC4 Native Structs for Memory Patching
class cS3DCamera {
public:
    void* vtable;
    intptr_t unknown[0x8D];
};

class cSC4CameraControl {
public:
    void* vtable;
    intptr_t unknown[0x45];
    float_t yaw;
    float_t pitch;
};

static constexpr uint32_t kSC4MessagePostCityInit = 0x26D31EC1;
static constexpr uint32_t kSC4MessagePreCityShutdown = 0x26D31EC2;
static constexpr uint32_t kGZWin_WinSC4App = 0x6104489a;
static constexpr uint32_t kGZWin_SC4View3DWin = 0x9a47b417;
static constexpr uint32_t kCameraUpdateMode = 2;
static constexpr float kDefaultPitch = 0.52359879f; // Default SC4 Zoom 1 pitch
static constexpr float kDefaultYaw = -0.39269909f;  // Default SC4 yaw
static constexpr float kMinPitch = 0.261f;
static constexpr float kMaxPitch = 1.483f;
static constexpr float kMouseRotationSensitivity = 0.005f;
static constexpr float kWheelPitchStep = 0.08f;
static constexpr UINT kPanIdleRedrawDelayMs = 1000;
static constexpr UINT kZoomIdleRedrawDelayMs = 1500;
static constexpr uint32_t kGZIID_cIGZMessageTarget2 = 0x090fa124;

// Global State
bool g_IsCityLoaded = false;
HHOOK g_MouseHook = NULL;
HHOOK g_KeyboardHook = NULL;
bool g_KeyState[256] = { false };

// Memory addresses from sc4-3d-camera-dll for Pitch and Yaw.
static constexpr uintptr_t kPitchAddress1 = 0xabcfd8;
static constexpr uintptr_t kPitchAddress2 = 0xabaccc;
static constexpr uintptr_t kYawAddress0 = 0x7ccb0a;
static constexpr uintptr_t kYawAddress1 = 0xabcfc4;
static constexpr uintptr_t kYawAddress2 = 0xabacb8;

// Cinematic Camera State
bool g_IsMiddleMouseDown = false;
POINT g_LastMousePos = { 0, 0 };
float g_CurrentPitch = kDefaultPitch;
float g_CurrentYaw = kDefaultYaw;

UINT_PTR g_IdleTimerID = 0;

typedef bool(__thiscall* pfn_cSC4CameraControl_UpdateCameraPosition)(cSC4CameraControl* pThis, uint32_t updateMode);
static pfn_cSC4CameraControl_UpdateCameraPosition UpdateCameraPosition = reinterpret_cast<pfn_cSC4CameraControl_UpdateCameraPosition>(0x7ccf80);

LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam);
VOID CALLBACK RedrawTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);

bool IsCurrentProcessForeground()
{
    DWORD foregroundProcessId = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcessId);

    return foregroundProcessId == GetCurrentProcessId();
}

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

template <typename Function>
bool WithRenderer(Function&& function)
{
    cISC4AppPtr pSC4App;
    if (!pSC4App) {
        return false;
    }

    cIGZWin* mainWindow = pSC4App->GetMainWindow();
    if (!mainWindow) {
        return false;
    }

    cIGZWin* pParentWin = mainWindow->GetChildWindowFromID(kGZWin_WinSC4App);
    if (!pParentWin) {
        return false;
    }

    cISC4View3DWin* pView3D = nullptr;
    if (!pParentWin->GetChildAs(kGZWin_SC4View3DWin, kGZIID_cISC4View3DWin, reinterpret_cast<void**>(&pView3D))) {
        return false;
    }

    bool result = false;
    cISC43DRender* renderer = pView3D->GetRenderer();
    if (renderer) {
        result = function(renderer);
    }

    pView3D->Release();
    return result;
}

bool UpdateCameraPositionFromRenderer(Logger& log, bool syncYaw)
{
    return WithRenderer([&](cISC43DRender* renderer) {
        cSC4CameraControl* pCamControl = renderer->GetCameraControl();

        if (!pCamControl) {
            log.WriteLine(LogLevel::Error, "Failed to get cSC4CameraControl from Renderer!");
            return false;
        }

        if (syncYaw) {
            // Sync the struct's internal state so the engine builds correct matrices.
            pCamControl->yaw = g_CurrentYaw;
        }

        UpdateCameraPosition(pCamControl, kCameraUpdateMode);
        return true;
    });
}

void OverwriteMemoryFloat(uintptr_t address, float newValue) {
    DWORD oldProtect;
    if (VirtualProtect(reinterpret_cast<void*>(address), sizeof(float), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        *reinterpret_cast<float*>(address) = newValue;
        VirtualProtect(reinterpret_cast<void*>(address), sizeof(float), oldProtect, &oldProtect);
    } else {
        char hexAddr[32];
        sprintf_s(hexAddr, sizeof(hexAddr), "0x%X", address);
        Logger::GetInstance().WriteLine(LogLevel::Error, std::string("VirtualProtect FAILED at ") + hexAddr);
    }
}

void UpdateCameraPitchYaw(float pitchDelta, float yawDelta) {
    g_CurrentPitch += pitchDelta;
    g_CurrentYaw += yawDelta;

    // Clamp pitch between ~15 degrees and ~85 degrees so we don't flip the camera upside down.
    g_CurrentPitch = std::clamp(g_CurrentPitch, kMinPitch, kMaxPitch);

    // Overwrite the game's internal camera floats
    for (int i = 0; i < 5; i++) {
        OverwriteMemoryFloat(kPitchAddress1 + i * 4, g_CurrentPitch);
        OverwriteMemoryFloat(kPitchAddress2 + i * 4, g_CurrentPitch);
    }
    
    OverwriteMemoryFloat(kYawAddress0, g_CurrentYaw);
    for (int i = 0; i < 5; i++) {
        OverwriteMemoryFloat(kYawAddress1 + i * 4, g_CurrentYaw);
        OverwriteMemoryFloat(kYawAddress2 + i * 4, g_CurrentYaw);
    }
}

void TriggerCityRedraw() {
    Logger::GetInstance().WriteLine(LogLevel::Info, "Executing ForceFullRedraw()...");

    WithRenderer([](cISC43DRender* renderer) {
        renderer->ForceFullRedraw();
        Logger::GetInstance().WriteLine(LogLevel::Info, "ForceFullRedraw() Success.");
        return true;
    });
}

void LogMouseButtonEvent(const char* name, const POINT& point)
{
    Logger::GetInstance().WriteLine(
        LogLevel::Info,
        std::string("Mouse Hook: ") + name + " at X:" + std::to_string(point.x) + " Y:" + std::to_string(point.y));
}

void NormalizeModifierKey(DWORD& vkCode)
{
    if (vkCode == VK_LSHIFT || vkCode == VK_RSHIFT) {
        vkCode = VK_SHIFT;
    }
    else if (vkCode == VK_LCONTROL || vkCode == VK_RCONTROL) {
        vkCode = VK_CONTROL;
    }
    else if (vkCode == VK_LMENU || vkCode == VK_RMENU) {
        vkCode = VK_MENU;
    }
}

bool IsKeyDownMessage(WPARAM wParam)
{
    return wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
}

bool IsKeyUpMessage(WPARAM wParam)
{
    return wParam == WM_KEYUP || wParam == WM_SYSKEYUP;
}

void UninstallHooks()
{
    if (g_MouseHook) {
        UnhookWindowsHookEx(g_MouseHook);
        g_MouseHook = NULL;
    }

    if (g_KeyboardHook) {
        UnhookWindowsHookEx(g_KeyboardHook);
        g_KeyboardHook = NULL;
    }
}

void InstallHooks()
{
    if (!g_MouseHook) {
        g_MouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc, GetModuleHandle(NULL), 0);
    }

    if (!g_KeyboardHook) {
        g_KeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardHookProc, GetModuleHandle(NULL), 0);
    }
}

void ResetInputState()
{
    memset(g_KeyState, 0, sizeof(g_KeyState));
    g_IsMiddleMouseDown = false;
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
            "Unsupported SimCity 4 version. SC4-3DMouseCam requires version 641, the Steam/GOG digital distribution build. The plugin will not activate.");

        MessageBoxA(
            NULL,
            "SC4-3DMouseCam requires SimCity 4 version 1.1.641, the Steam/GOG digital distribution build.\n\n"
            "This plugin uses version-specific camera memory addresses and will not activate on this executable.",
            "SC4-3DMouseCam: Unsupported Game Version",
            MB_OK | MB_ICONEXCLAMATION);

        return false;
    }

    return true;
}

bool RegisterNotifications(cIGZMessageTarget2* target)
{
    cIGZMessageServer2Ptr pMsgServ;
    if (pMsgServ) {
        const bool addedPostCityInit = pMsgServ->AddNotification(target, kSC4MessagePostCityInit);
        const bool addedPreCityShutdown = pMsgServ->AddNotification(target, kSC4MessagePreCityShutdown);

        return addedPostCityInit && addedPreCityShutdown;
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

std::string GetKeyName(DWORD vkCode) {
    switch (vkCode) {
        case VK_MENU: case VK_LMENU: case VK_RMENU: return "ALT";
        case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: return "SHIFT";
        case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return "CTRL";
        case 'W': return "W";
        case 'A': return "A";
        case 'S': return "S";
        case 'D': return "D";
        case VK_UP: return "Up Arrow";
        case VK_DOWN: return "Down Arrow";
        case VK_LEFT: return "Left Arrow";
        case VK_RIGHT: return "Right Arrow";
        default: return "Other Key (" + std::to_string(vkCode) + ")";
    }
}

LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && g_IsCityLoaded) {
        if (IsCurrentProcessForeground()) {
            MSLLHOOKSTRUCT* pMouse = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
            Logger& log = Logger::GetInstance();

            switch (wParam) {
                case WM_LBUTTONDOWN: {
                    LogMouseButtonEvent("WM_LBUTTONDOWN (Left Mouse Down)", pMouse->pt);
                    break;
                }
                case WM_LBUTTONUP: {
                    LogMouseButtonEvent("WM_LBUTTONUP (Left Mouse Up)", pMouse->pt);
                    break;
                }
                case WM_RBUTTONDOWN: {
                    LogMouseButtonEvent("WM_RBUTTONDOWN (Right Mouse Down)", pMouse->pt);
                    break;
                }
                case WM_RBUTTONUP: {
                    LogMouseButtonEvent("WM_RBUTTONUP (Right Mouse Up)", pMouse->pt);
                    break;
                }
                case WM_MBUTTONDOWN: {
                    log.WriteLine(LogLevel::Info, "Mouse Hook: WM_MBUTTONDOWN (Middle Mouse Down)");
                    g_IsMiddleMouseDown = true;
                    g_LastMousePos = pMouse->pt;
                    if (g_IdleTimerID != 0) {
                        log.WriteLine(LogLevel::Info, "Action Interrupted: Killing Idle Timer");
                        KillIdleTimer();
                    }
                    break;
                }
                case WM_MBUTTONUP: {
                    log.WriteLine(LogLevel::Info, "Mouse Hook: WM_MBUTTONUP (Middle Mouse Up)");
                    g_IsMiddleMouseDown = false;
                    log.WriteLine(LogLevel::Info, "Pan Stopped: Starting 1000ms Idle Timer");
                    StartIdleTimer(kPanIdleRedrawDelayMs);
                    break;
                }
                case WM_MOUSEMOVE: {
                    if (g_IsMiddleMouseDown) {
                        int deltaX = pMouse->pt.x - g_LastMousePos.x;
                        int deltaY = pMouse->pt.y - g_LastMousePos.y;
                        
                        // Calculate rotation deltas (adjust multipliers for sensitivity)
                        float yawDelta = static_cast<float>(deltaX) * kMouseRotationSensitivity;
                        float pitchDelta = static_cast<float>(deltaY) * kMouseRotationSensitivity;

                        UpdateCameraPitchYaw(pitchDelta, yawDelta);
                        UpdateCameraPositionFromRenderer(log, true);

                        g_LastMousePos = pMouse->pt;
                    }
                    break;
                }
                case WM_MOUSEWHEEL: {
                    short zDelta = HIWORD(pMouse->mouseData);
                    log.WriteLine(LogLevel::Info, "Mouse Hook: WM_MOUSEWHEEL (Delta: " + std::to_string(zDelta) + ")");
                    
                    // The "Reverse Arch Down to Street Level" logic
                    // When scrolling, we tie the Pitch to the scroll delta to simulate an arching dive
                    float pitchArchDelta = (zDelta > 0) ? -kWheelPitchStep : kWheelPitchStep;
                    UpdateCameraPitchYaw(pitchArchDelta, 0.0f);

                    log.WriteLine(LogLevel::Info, "Zoom Scrolled: Starting/Resetting 1500ms Idle Timer");
                    StartIdleTimer(kZoomIdleRedrawDelayMs);
                    UpdateCameraPositionFromRenderer(log, false);
                    
                    // We DO NOT return 1 here anymore. We WANT the game's internal DirectInput 
                    // to execute the standard 5-step zoom while we alter the pitch to create the arch.
                    break;
                }
            }
        }
    }
    return CallNextHookEx(g_MouseHook, nCode, wParam, lParam);
}

LRESULT CALLBACK KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && g_IsCityLoaded) {
        if (IsCurrentProcessForeground()) {
            KBDLLHOOKSTRUCT* pKey = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
            Logger& log = Logger::GetInstance();
            
            DWORD vkCode = pKey->vkCode;
            
            if (IsKeyDownMessage(wParam)) {
                log.WriteLine(LogLevel::Info, "Keyboard Hook: Key Down [VK_CODE: " + std::to_string(vkCode) + "]");
            } else if (IsKeyUpMessage(wParam)) {
                log.WriteLine(LogLevel::Info, "Keyboard Hook: Key Up [VK_CODE: " + std::to_string(vkCode) + "]");
            }

            NormalizeModifierKey(vkCode);

            std::string keyName = GetKeyName(vkCode);
            
            if (IsKeyDownMessage(wParam)) {
                if (!g_KeyState[vkCode]) {
                    g_KeyState[vkCode] = true;
                    log.WriteLine(LogLevel::Info, "Keyboard State: '" + keyName + "' Pressed");
                }
            } else if (IsKeyUpMessage(wParam)) {
                g_KeyState[vkCode] = false;
                log.WriteLine(LogLevel::Info, "Keyboard State: '" + keyName + "' Released");
            }
        }
    }
    return CallNextHookEx(g_KeyboardHook, nCode, wParam, lParam);
}

class cSC4MouseCamDirector : public cRZCOMDllDirector, public cIGZMessageTarget2
{
public:
	cSC4MouseCamDirector()
	{
		AddRef();
	}

    // Resolve COM multiple inheritance ambiguity
    uint32_t AddRef() override { return cRZCOMDllDirector::AddRef(); }
    uint32_t Release() override { return cRZCOMDllDirector::Release(); }

    bool QueryInterface(uint32_t riid, void** ppvObj) override
    {
        if (riid == kGZIID_cIGZMessageTarget2) {
            *ppvObj = static_cast<cIGZMessageTarget2*>(this);
            AddRef();
            return true;
        }
        return cRZCOMDllDirector::QueryInterface(riid, ppvObj);
    }

	uint32_t GetDirectorID() const override
	{
		return 0x8C4B3A11;
	}

	bool OnStart(cIGZCOM* pCOM) override
	{
		Logger::GetInstance().Initialize(GetDefaultLogPath());
		Logger::GetInstance().WriteLine(LogLevel::Info, "Plugin Loaded. Waiting for city to load...");

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
            Logger::GetInstance().WriteLine(LogLevel::Info, "City Loaded! Activating Hooks...");
            g_IsCityLoaded = true;
            ResetInputState();
            InstallHooks();
        }
        else if (msgType == kSC4MessagePreCityShutdown) {
            Logger::GetInstance().WriteLine(LogLevel::Info, "City Shutting Down. Deactivating Hooks...");
            g_IsCityLoaded = false;
            ResetInputState();
            UninstallHooks();
        }

        return true;
    }
};

cRZCOMDllDirector* RZGetCOMDllDirector() {
	static cSC4MouseCamDirector sDirector;
	return &sDirector;
}
