#include <Windows.h>
#include "cRZCOMDllDirector.h"
#include "Logger.h"
#include "cIGZMessageServer2.h"
#include "cIGZMessageTarget2.h"
#include "cIGZMessage2Standard.h"
#include "GZServPtrs.h"
#include "cISC4App.h"
#include "cIGZWin.h"
#include "cISC4View3DWin.h"
#include "cISC43DRender.h"
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
#include <cstdlib>

static constexpr uint32_t kSC4MessagePostCityInit = 0x26D31EC1;
static constexpr uint32_t kSC4MessagePreCityShutdown = 0x26D31EC2;
static constexpr uint32_t kGZWin_WinSC4App = 0x6104489a;
static constexpr uint32_t kGZWin_SC4View3DWin = 0x9a47b417;

// Global State
bool g_IsCityLoaded = false;
HHOOK g_MouseHook = NULL;
HHOOK g_KeyboardHook = NULL;
bool g_KeyState[256] = { false };

// Memory Addresses from sc4-3d-camera-dll for Pitch and Yaw
static constexpr uint32_t pitchAddress1 = 0xabcfd8;
static constexpr uint32_t pitchAddress2 = 0xabaccc;
static constexpr uint32_t yawAddress0 = 0x7ccb0a;
static constexpr uint32_t yawAddress1 = 0xabcfc4;
static constexpr uint32_t yawAddress2 = 0xabacb8;

// Cinematic Camera State
bool g_IsMiddleMouseDown = false;
POINT g_LastMousePos = { 0, 0 };
float g_CurrentPitch = 0.52359879f; // Default SC4 Zoom 1 pitch
float g_CurrentYaw = -0.39269909f;  // Default SC4 yaw

UINT_PTR g_IdleTimerID = 0;

typedef bool(__thiscall* pfn_cSC4CameraControl_UpdateCameraPosition)(cSC4CameraControl* pThis, uint32_t updateMode);
static pfn_cSC4CameraControl_UpdateCameraPosition UpdateCameraPosition = reinterpret_cast<pfn_cSC4CameraControl_UpdateCameraPosition>(0x7ccf80);

void OverwriteMemoryFloat(uintptr_t address, float newValue) {
    DWORD oldProtect;
    if (VirtualProtect(reinterpret_cast<void*>(address), sizeof(float), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        float oldValue = *reinterpret_cast<float*>(address);
        *reinterpret_cast<float*>(address) = newValue;
        VirtualProtect(reinterpret_cast<void*>(address), sizeof(float), oldProtect, &oldProtect);
        
        if (oldValue != newValue) {
            char hexAddr[32];
            sprintf_s(hexAddr, sizeof(hexAddr), "0x%X", address);
            Logger::GetInstance().WriteLine(LogLevel::Info, std::string("Memory Patch [") + hexAddr + "]: " + std::to_string(oldValue) + " -> " + std::to_string(newValue));
        }
    } else {
        char hexAddr[32];
        sprintf_s(hexAddr, sizeof(hexAddr), "0x%X", address);
        Logger::GetInstance().WriteLine(LogLevel::Error, std::string("VirtualProtect FAILED at ") + hexAddr);
    }
}

void UpdateCameraPitchYaw(float pitchDelta, float yawDelta) {
    g_CurrentPitch += pitchDelta;
    g_CurrentYaw += yawDelta;

    // Clamp pitch between ~15 degrees and ~85 degrees so we don't flip the camera upside down
    if (g_CurrentPitch < 0.261f) g_CurrentPitch = 0.261f;
    if (g_CurrentPitch > 1.483f) g_CurrentPitch = 1.483f;

    // Overwrite the game's internal camera floats
    for (int i = 0; i < 5; i++) {
        OverwriteMemoryFloat(pitchAddress1 + i * 4, g_CurrentPitch);
        OverwriteMemoryFloat(pitchAddress2 + i * 4, g_CurrentPitch);
    }
    
    OverwriteMemoryFloat(yawAddress0, g_CurrentYaw);
    for (int i = 0; i < 5; i++) {
        OverwriteMemoryFloat(yawAddress1 + i * 4, g_CurrentYaw);
        OverwriteMemoryFloat(yawAddress2 + i * 4, g_CurrentYaw);
    }
}

void TriggerCityRedraw() {
    Logger::GetInstance().WriteLine(LogLevel::Info, "Executing ForceFullRedraw()...");
    cISC4AppPtr pSC4App;
    if (pSC4App) {
        cIGZWin* mainWindow = pSC4App->GetMainWindow();
        if (mainWindow) {
            cIGZWin* pParentWin = mainWindow->GetChildWindowFromID(kGZWin_WinSC4App);
            if (pParentWin) {
                cISC4View3DWin* pView3D = nullptr;
                if (pParentWin->GetChildAs(kGZWin_SC4View3DWin, kGZIID_cISC4View3DWin, reinterpret_cast<void**>(&pView3D))) {
                    cISC43DRender* renderer = pView3D->GetRenderer();
                    if (renderer) {
                        renderer->ForceFullRedraw();
                        Logger::GetInstance().WriteLine(LogLevel::Info, "ForceFullRedraw() Success.");
                    }
                    pView3D->Release();
                }
            }
        }
    }
}

VOID CALLBACK RedrawTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime) {
    Logger& log = Logger::GetInstance();
    if (idEvent == g_IdleTimerID && g_IdleTimerID != 0) {
        log.WriteLine(LogLevel::Info, "Camera Idle Timer Reached 0: Firing TriggerCityRedraw");
        KillTimer(NULL, g_IdleTimerID);
        g_IdleTimerID = 0;
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
        DWORD fgPid = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &fgPid);
        if (fgPid == GetCurrentProcessId()) {
            MSLLHOOKSTRUCT* pMouse = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
            Logger& log = Logger::GetInstance();

            switch (wParam) {
                case WM_LBUTTONDOWN: {
                    log.WriteLine(LogLevel::Info, "Mouse Hook: WM_LBUTTONDOWN (Left Mouse Down) at X:" + std::to_string(pMouse->pt.x) + " Y:" + std::to_string(pMouse->pt.y));
                    break;
                }
                case WM_LBUTTONUP: {
                    log.WriteLine(LogLevel::Info, "Mouse Hook: WM_LBUTTONUP (Left Mouse Up) at X:" + std::to_string(pMouse->pt.x) + " Y:" + std::to_string(pMouse->pt.y));
                    break;
                }
                case WM_RBUTTONDOWN: {
                    log.WriteLine(LogLevel::Info, "Mouse Hook: WM_RBUTTONDOWN (Right Mouse Down) at X:" + std::to_string(pMouse->pt.x) + " Y:" + std::to_string(pMouse->pt.y));
                    break;
                }
                case WM_RBUTTONUP: {
                    log.WriteLine(LogLevel::Info, "Mouse Hook: WM_RBUTTONUP (Right Mouse Up) at X:" + std::to_string(pMouse->pt.x) + " Y:" + std::to_string(pMouse->pt.y));
                    break;
                }
                case WM_MBUTTONDOWN: {
                    log.WriteLine(LogLevel::Info, "Mouse Hook: WM_MBUTTONDOWN (Middle Mouse Down)");
                    g_IsMiddleMouseDown = true;
                    g_LastMousePos = pMouse->pt;
                    if (g_IdleTimerID != 0) {
                        log.WriteLine(LogLevel::Info, "Action Interrupted: Killing Idle Timer");
                        KillTimer(NULL, g_IdleTimerID);
                        g_IdleTimerID = 0;
                    }
                    break;
                }
                case WM_MBUTTONUP: {
                    log.WriteLine(LogLevel::Info, "Mouse Hook: WM_MBUTTONUP (Middle Mouse Up)");
                    g_IsMiddleMouseDown = false;
                    if (g_IdleTimerID != 0) {
                        KillTimer(NULL, g_IdleTimerID);
                    }
                    log.WriteLine(LogLevel::Info, "Pan Stopped: Starting 1000ms Idle Timer");
                    g_IdleTimerID = SetTimer(NULL, 0, 1000, RedrawTimerProc);
                    break;
                }
                case WM_MOUSEMOVE: {
                    if (g_IsMiddleMouseDown) {
                        int deltaX = pMouse->pt.x - g_LastMousePos.x;
                        int deltaY = pMouse->pt.y - g_LastMousePos.y;
                        
                        log.WriteLine(LogLevel::Info, "Mouse Hook: WM_MOUSEMOVE (Raw X:" + std::to_string(pMouse->pt.x) + " Y:" + std::to_string(pMouse->pt.y) + ") (DeltaX: " + std::to_string(deltaX) + " DeltaY: " + std::to_string(deltaY) + ")");

                        // Calculate rotation deltas (adjust multipliers for sensitivity)
                        float yawDelta = static_cast<float>(deltaX) * 0.005f;
                        float pitchDelta = static_cast<float>(deltaY) * 0.005f;

                        // Trigger visual update properly using internal engine call
                        cISC4AppPtr pSC4App;
                        if (pSC4App) {
                            cIGZWin* mainWindow = pSC4App->GetMainWindow();
                            if (mainWindow) {
                                cIGZWin* pParentWin = mainWindow->GetChildWindowFromID(kGZWin_WinSC4App);
                                if (pParentWin) {
                                    cISC4View3DWin* pView3D = nullptr;
                                    if (pParentWin->GetChildAs(kGZWin_SC4View3DWin, kGZIID_cISC4View3DWin, reinterpret_cast<void**>(&pView3D))) {
                                        cISC43DRender* renderer = pView3D->GetRenderer();
                                        if (renderer) {
                                            cSC4CameraControl* pCamControl = renderer->GetCameraControl();
                                            log.WriteLine(LogLevel::Info, "Renderer Acquired. Patching Memory...");
                                            
                                            // Execute brutal memory patch
                                            UpdateCameraPitchYaw(pitchDelta, yawDelta);
                                            
                                            if (pCamControl) {
                                                log.WriteLine(LogLevel::Info, "Calling Engine UpdateCameraPosition(pCamControl, 2)...");
                                                bool updateResult = UpdateCameraPosition(pCamControl, 2); 
                                                log.WriteLine(LogLevel::Info, "Engine UpdateCameraPosition Returned: " + std::to_string(updateResult));
                                            } else {
                                                log.WriteLine(LogLevel::Error, "Failed to get cSC4CameraControl from Renderer!");
                                            }
                                        }
                                        pView3D->Release();
                                    }
                                }
                            }
                        }

                        g_LastMousePos = pMouse->pt;
                    }
                    break;
                }
                case WM_MOUSEWHEEL: {
                    short zDelta = HIWORD(pMouse->mouseData);
                    log.WriteLine(LogLevel::Info, "Mouse Hook: WM_MOUSEWHEEL (Delta: " + std::to_string(zDelta) + ")");
                    
                    // The "Reverse Arch Down to Street Level" logic
                    // When scrolling, we tie the Pitch to the scroll delta to simulate an arching dive
                    float pitchArchDelta = (zDelta > 0) ? -0.08f : 0.08f; 
                    UpdateCameraPitchYaw(pitchArchDelta, 0.0f);

                    if (g_IdleTimerID != 0) {
                        KillTimer(NULL, g_IdleTimerID);
                    }
                    log.WriteLine(LogLevel::Info, "Zoom Scrolled: Starting/Resetting 1500ms Idle Timer");
                    g_IdleTimerID = SetTimer(NULL, 0, 1500, RedrawTimerProc);

                    cISC4AppPtr pSC4App;
                    if (pSC4App) {
                        cIGZWin* mainWindow = pSC4App->GetMainWindow();
                        if (mainWindow) {
                            cIGZWin* pParentWin = mainWindow->GetChildWindowFromID(kGZWin_WinSC4App);
                            if (pParentWin) {
                                cISC4View3DWin* pView3D = nullptr;
                                if (pParentWin->GetChildAs(kGZWin_SC4View3DWin, kGZIID_cISC4View3DWin, reinterpret_cast<void**>(&pView3D))) {
                                    cISC43DRender* renderer = pView3D->GetRenderer();
                                    if (renderer) {
                                        cSC4CameraControl* pCamControl = renderer->GetCameraControl();
                                        if (pCamControl) {
                                            log.WriteLine(LogLevel::Info, "Scroll Hook: Calling Engine UpdateCameraPosition(pCamControl, 2)...");
                                            bool updateResult = UpdateCameraPosition(pCamControl, 2);
                                            log.WriteLine(LogLevel::Info, "Scroll Hook: Engine Update Returned: " + std::to_string(updateResult));
                                        }
                                    }
                                    pView3D->Release();
                                }
                            }
                        }
                    }
                    
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
        DWORD fgPid = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &fgPid);
        if (fgPid == GetCurrentProcessId()) {
            KBDLLHOOKSTRUCT* pKey = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
            Logger& log = Logger::GetInstance();
            
            DWORD vkCode = pKey->vkCode;
            
            if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
                log.WriteLine(LogLevel::Info, "Keyboard Hook: Key Down [VK_CODE: " + std::to_string(vkCode) + "]");
            } else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
                log.WriteLine(LogLevel::Info, "Keyboard Hook: Key Up [VK_CODE: " + std::to_string(vkCode) + "]");
            }

            // Normalize left/right modifiers to their standard generic code so the array checks properly
            if (vkCode == VK_LSHIFT || vkCode == VK_RSHIFT) vkCode = VK_SHIFT;
            if (vkCode == VK_LCONTROL || vkCode == VK_RCONTROL) vkCode = VK_CONTROL;
            if (vkCode == VK_LMENU || vkCode == VK_RMENU) vkCode = VK_MENU;

            std::string keyName = GetKeyName(vkCode);
            
            if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
                if (!g_KeyState[vkCode]) {
                    g_KeyState[vkCode] = true;
                    log.WriteLine(LogLevel::Info, "Keyboard State: '" + keyName + "' Pressed");
                } else {
                    log.WriteLine(LogLevel::Info, "Keyboard State: '" + keyName + "' Held");
                }
            } else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
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
        // GZIID_cIGZMessageTarget2 is 0x090fa124 based on the header file definition
        if (riid == 0x090fa124) {
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
		char* userProfile = nullptr;
		size_t len = 0;
		_dupenv_s(&userProfile, &len, "USERPROFILE");
		
		std::string logPath = "SC4-3DMouseCam.log";
		if (userProfile) {
			logPath = std::string(userProfile) + "\\Documents\\SimCity 4\\Plugins\\SC4-3DMouseCam.log";
			free(userProfile);
		}
		Logger::GetInstance().Initialize(logPath);
		Logger::GetInstance().WriteLine(LogLevel::Info, "Plugin Loaded. Waiting for city to load...");

        // Register to receive messages from the game
        cIGZMessageServer2Ptr pMsgServ;
        if (pMsgServ) {
            pMsgServ->AddNotification(this, kSC4MessagePostCityInit);
            pMsgServ->AddNotification(this, kSC4MessagePreCityShutdown);
        }

		return true;
	}
    
    bool DoMessage(cIGZMessage2* pMsg) override
    {
        uint32_t msgType = pMsg->GetType();

        if (msgType == kSC4MessagePostCityInit) {
            Logger::GetInstance().WriteLine(LogLevel::Info, "City Loaded! Activating Hooks...");
            g_IsCityLoaded = true;
            memset(g_KeyState, 0, sizeof(g_KeyState)); // Reset key states just in case
            
            // Install Windows API Hooks
            if (!g_MouseHook) {
                g_MouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc, GetModuleHandle(NULL), 0);
            }
            if (!g_KeyboardHook) {
                g_KeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardHookProc, GetModuleHandle(NULL), 0);
            }
        }
        else if (msgType == kSC4MessagePreCityShutdown) {
            Logger::GetInstance().WriteLine(LogLevel::Info, "City Shutting Down. Deactivating Hooks...");
            g_IsCityLoaded = false;
            
            // Remove Windows API Hooks
            if (g_MouseHook) {
                UnhookWindowsHookEx(g_MouseHook);
                g_MouseHook = NULL;
            }
            if (g_KeyboardHook) {
                UnhookWindowsHookEx(g_KeyboardHook);
                g_KeyboardHook = NULL;
            }
        }

        return true;
    }
};

cRZCOMDllDirector* RZGetCOMDllDirector() {
	static cSC4MouseCamDirector sDirector;
	return &sDirector;
}
