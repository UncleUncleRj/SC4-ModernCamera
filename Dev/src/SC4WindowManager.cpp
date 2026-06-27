#include "SC4WindowManager.h"

#include "GZServPtrs.h"
#include "Logger.h"
#include "PluginPaths.h"
#include "PluginSettings.h"
#include "PluginVersion.h"
#include "SC4VersionDetection.h"
#include "cGZMessage.h"
#include "cGZPersistResourceKey.h"
#include "cIGZFont.h"
#include "cIGZString.h"
#include "cIGZUIScriptService.h"
#include "cIGZWin.h"
#include "cIGZWinBtn.h"
#include "cIGZWinCtrlMgr.h"
#include "cIGZWinGen.h"
#include "cIGZWinText.h"
#include "cISC4App.h"
#include "cRZAutoRefCount.h"
#include "cRZBaseString.h"
#include "cRZRect.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

namespace
{
	constexpr uint32_t kWinSC4AppID = 0x6104489A;
	constexpr uint32_t kWindowResourceGroup = 0x3D0C0700;
	constexpr uint32_t kWindowResourceInstance = 0x3D0C0701;
	constexpr uint32_t kWindowCLSID = 0x3D0C0702;
	constexpr uint32_t kBasicWindowResourceInstance = 0x3D0C0703;
	constexpr uint32_t kBasicWindowCLSID = 0x3D0C0704;
	constexpr uint32_t kGreetingWindowResourceInstance = 0x3D0C0705;
	constexpr uint32_t kGreetingWindowCLSID = 0x3D0C0706;
	constexpr uint32_t kControlsWindowResourceInstance = 0x3D0C0707;
	constexpr uint32_t kControlsWindowCLSID = 0x3D0C0708;
	constexpr uint32_t kMenuButtonResourceInstance = 0x3D0C0901;
	constexpr uint32_t kMenuButtonCLSID = 0x3D0C0902;
	constexpr uint32_t kSettingsWindowResourceInstance = 0x3D0C0903;
	constexpr uint32_t kSettingsWindowCLSID = 0x3D0C0904;
	constexpr uint32_t kAdvancedWindowResourceInstance = 0x3D0C0905;
	constexpr uint32_t kAdvancedWindowCLSID = 0x3D0C0906;
	constexpr uint32_t kBasicCloseXButtonID = 0x3D0C0810;
	constexpr uint32_t kBasicTitleID = 0x3D0C0820;
	constexpr uint32_t kBasicTextID = 0x3D0C0821;
	constexpr uint32_t kBasicActionButtonID = 0x3D0C0830;
	constexpr uint32_t kGreetingCloseXButtonID = 0x3D0C0840;
	constexpr uint32_t kGreetingOKButtonID = 0x3D0C0843;
	constexpr uint32_t kGreetingViewControlsButtonID = 0x3D0C0844;
	constexpr uint32_t kControlsCloseXButtonID = 0x3D0C0850;
	constexpr uint32_t kControlsOKButtonID = 0x3D0C0853;
	constexpr uint32_t kMenuButtonID = 0x3D0C0910;
	constexpr uint32_t kSettingsCloseXButtonID = 0x3D0C0920;
	constexpr uint32_t kSettingsModernButtonID = 0x3D0C0930;
	constexpr uint32_t kSettingsClassicButtonID = 0x3D0C0931;
	constexpr uint32_t kSettingsWASDOnButtonID = 0x3D0C0932;
	constexpr uint32_t kSettingsWASDOffButtonID = 0x3D0C0933;
	constexpr uint32_t kSettingsRotationSliderID = 0x3D0C0934;
	constexpr uint32_t kSettingsZoomSliderID = 0x3D0C0935;
	constexpr uint32_t kSettingsInvertOnButtonID = 0x3D0C0936;
	constexpr uint32_t kSettingsInvertOffButtonID = 0x3D0C0937;
	constexpr uint32_t kSettingsResetCameraButtonID = 0x3D0C0938;
	constexpr uint32_t kSettingsRedrawOffButtonID = 0x3D0C0940;
	constexpr uint32_t kSettingsRedrawNormalButtonID = 0x3D0C0941;
	constexpr uint32_t kSettingsRedrawHighButtonID = 0x3D0C0942;
	constexpr uint32_t kSettingsRedrawAggressiveButtonID = 0x3D0C0943;
	constexpr uint32_t kSettingsRestoreDefaultsButtonID = 0x3D0C0950;
	constexpr uint32_t kSettingsAdvancedButtonID = 0x3D0C0951;
	constexpr uint32_t kSettingsReadChangelogButtonID = 0x3D0C0952;
	constexpr uint32_t kSettingsCloseButtonID = 0x3D0C0953;
	constexpr uint32_t kAdvancedCloseXButtonID = 0x3D0C0960;
	constexpr uint32_t kAdvancedCloseButtonID = 0x3D0C0963;
	constexpr uint32_t kAdvancedDebugLoggingOffButtonID = 0x3D0C0968;
	constexpr uint32_t kAdvancedDebugLoggingNormalButtonID = 0x3D0C0969;
	constexpr uint32_t kAdvancedDebugLoggingVerboseButtonID = 0x3D0C096A;
	constexpr SC4WindowHandle kControlLaboratoryHandle = 1;

	constexpr uint32_t kCloseButtonID = 0x3D0C0710;
	constexpr uint32_t kScrollUpButtonID = 0x3D0C0711;
	constexpr uint32_t kScrollDownButtonID = 0x3D0C0712;
	constexpr uint32_t kScrollBarID = 0x3D0C0713;
	constexpr uint32_t kCloseXButtonID = 0x3D0C0714;

	constexpr uint32_t kMessageTypeCommand = 3;
	constexpr uint32_t kCommandButtonClicked = 0x287259F6;
	constexpr uint32_t kCommandValueChanged = 0x887113A3;
	using CreateSC4NotificationDialog = bool(__cdecl*)(cIGZString const& caption, cIGZString const& message);
	constexpr uintptr_t kCreateSC4NotificationDialogAddress = 0x78DD80;
	constexpr UINT kVersionNoticeDelayMs = 3000;
	constexpr UINT kSettingsSliderSaveDelayMs = 200;
	constexpr UINT kSettingsReopenDelayMs = 1;
	constexpr UINT kDeferredWindowOpenDelayMs = 1;

	SC4WindowManager* g_DelayedVersionNoticeManager = nullptr;
	SettingsWindow* g_DelayedSettingsSaveWindow = nullptr;
	SC4WindowManager* g_DelayedSettingsReopenManager = nullptr;
	SC4WindowManager* g_DelayedWindowOpenManager = nullptr;

	VOID CALLBACK DelayedVersionNoticeTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
	{
		if (g_DelayedVersionNoticeManager)
		{
			g_DelayedVersionNoticeManager->OnVersionNoticeTimer();
		}
	}

	VOID CALLBACK DelayedSettingsSaveTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
	{
		if (g_DelayedSettingsSaveWindow)
		{
			g_DelayedSettingsSaveWindow->OnDelayedSettingsSaveTimer();
		}
	}

	VOID CALLBACK DelayedSettingsReopenTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
	{
		if (g_DelayedSettingsReopenManager)
		{
			g_DelayedSettingsReopenManager->OnSettingsReopenTimer();
		}
	}

	VOID CALLBACK DeferredWindowOpenTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
	{
		if (g_DelayedWindowOpenManager)
		{
			g_DelayedWindowOpenManager->OnDeferredWindowOpenTimer();
		}
	}

	// The SDK does not yet expose the concrete interfaces for these controls.
	// All of them begin with AsIGZWin(), which is the only method the harness
	// needs; raw command data is intentionally captured for interface research.
	class ControlWindowAdapter : public cIGZUnknown
	{
	public:
		virtual cIGZWin* AsIGZWin() = 0;
	};

	std::string EscapeJson(const std::string& value)
	{
		std::ostringstream output;
		for (unsigned char c : value)
		{
			switch (c)
			{
			case '\\': output << "\\\\"; break;
			case '"': output << "\\\""; break;
			case '\n': output << "\\n"; break;
			case '\r': output << "\\r"; break;
			case '\t': output << "\\t"; break;
			default:
				if (c < 0x20)
				{
					output << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
				}
				else
				{
					output << static_cast<char>(c);
				}
			}
		}
		return output.str();
	}

	void SetWinProc(cIGZWin* window, cIGZWinProc* winProc)
	{
		cRZAutoRefCount<cIGZWinGen> windowGen;
		if (window && window->QueryInterface(GZIID_cIGZWinGen, windowGen.AsPPVoid()))
		{
			windowGen->SetWinProc(winProc);
		}
	}

	bool SetTextCaption(cIGZWin* window, const std::string& caption)
	{
		if (!window)
		{
			return false;
		}

		cRZBaseString text(caption.c_str());
		cRZAutoRefCount<cIGZWinText> textControl;
		if (window->QueryInterface(GZIID_cIGZWinText, textControl.AsPPVoid()))
		{
			return textControl->SetCaption(text);
		}
		return window->SetCaption(text);
	}

	bool SetButtonCaption(cIGZWin* window, const char* caption)
	{
		if (!window || !caption)
		{
			return false;
		}

		cRZBaseString text(caption);
		cRZAutoRefCount<cIGZWinBtn> button;
		if (window->QueryInterface(GZIID_cIGZWinBtn, button.AsPPVoid()))
		{
			button->SetBtnFlag(cIGZWinBtn::BtnFlagShowCaption, true);
			return button->SetCaption(text);
		}
		return window->SetCaption(text);
	}

	const char* ToCameraModeString(CameraMode value)
	{
		return value == CameraMode::Classic ? "Classic" : "Modern";
	}

	const char* ToRedrawAggressionString(RedrawAggression value)
	{
		switch (value)
		{
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

	const char* ToDebugLoggingString(DebugLogging value)
	{
		switch (value)
		{
		case DebugLogging::Off:
			return "Off";
		case DebugLogging::Verbose:
			return "Verbose";
		default:
			return "Normal";
		}
	}

	const char* ToOnOffString(bool value)
	{
		return value ? "On" : "Off";
	}

	std::string FormatSettingFloat(float value)
	{
		std::ostringstream stream;
		stream << std::fixed << std::setprecision(2) << value;
		return stream.str();
	}

	void LogOptionChanged(
		const char* source,
		const char* option,
		const std::string& before,
		const std::string& after,
		LogLevel level = LogLevel::Info)
	{
		if (before == after)
		{
			return;
		}

		Logger::GetInstance().WriteLine(
			level,
			std::string(source ? source : "Settings UI")
			+ ": "
			+ (option ? option : "option")
			+ " changed from "
			+ before
			+ " to "
			+ after
			+ ".");
	}

	void ApplyButtonChoice(cIGZWin* root, uint32_t selectedID, const uint32_t* ids, size_t count)
	{
		if (!root)
		{
			return;
		}

		for (size_t i = 0; i < count; ++i)
		{
			cIGZWin* child = root->GetChildWindowFromIDRecursive(ids[i]);
			cRZAutoRefCount<cIGZWinBtn> button;
			if (child && child->QueryInterface(GZIID_cIGZWinBtn, button.AsPPVoid()))
			{
				if (ids[i] == selectedID)
				{
					button->ToggleOn();
				}
				else
				{
					button->ToggleOff();
				}
				child->InvalidateSelfAndParents();
			}
		}
	}

	void SetControlEnabled(cIGZWin* root, uint32_t id, bool enabled)
	{
		cIGZWin* child = root ? root->GetChildWindowFromIDRecursive(id) : nullptr;
		if (!child)
		{
			return;
		}

		child->SetFlag(cIGZWin::WinFlag_Enabled, enabled);
		child->InvalidateSelfAndParents();
	}

	void SetControlsEnabled(cIGZWin* root, const uint32_t* ids, size_t count, bool enabled)
	{
		for (size_t i = 0; i < count; ++i)
		{
			SetControlEnabled(root, ids[i], enabled);
		}
	}

	bool IsRedrawAggressionControl(uint32_t controlID)
	{
		return controlID == kSettingsRedrawOffButtonID
			|| controlID == kSettingsRedrawNormalButtonID
			|| controlID == kSettingsRedrawHighButtonID
			|| controlID == kSettingsRedrawAggressiveButtonID;
	}

	bool WindowContainsParentPoint(cIGZWin* window, int32_t parentX, int32_t parentY)
	{
		return window
			&& window->IsVisible()
			&& parentX >= window->GetL()
			&& parentX < window->GetR()
			&& parentY >= window->GetT()
			&& parentY < window->GetB();
	}
}

BasicManagedWindow::BasicManagedWindow()
	: refCount(0), parentWindow(nullptr), rootWindow(nullptr)
{
}

BasicManagedWindow::~BasicManagedWindow()
{
	Destroy();
}

bool BasicManagedWindow::QueryInterface(uint32_t riid, void** ppvObj)
{
	if (!ppvObj)
	{
		return false;
	}
	if (riid == GZIID_cIGZWinProc)
	{
		*ppvObj = static_cast<cIGZWinProc*>(this);
	}
	else if (riid == GZIID_cIGZUnknown)
	{
		*ppvObj = static_cast<cIGZUnknown*>(this);
	}
	else
	{
		*ppvObj = nullptr;
		return false;
	}

	AddRef();
	return true;
}

uint32_t BasicManagedWindow::AddRef()
{
	return ++refCount;
}

uint32_t BasicManagedWindow::Release()
{
	if (refCount > 0)
	{
		--refCount;
	}
	return refCount;
}

bool BasicManagedWindow::Create(const SC4BasicWindowOptions& requestedOptions)
{
	if (rootWindow)
	{
		rootWindow->ShowWindow();
		rootWindow->PullToFront();
		return true;
	}

	cISC4AppPtr app;
	if (!app || !app->GetMainWindow())
	{
		return false;
	}
	parentWindow = app->GetMainWindow()->GetChildWindowFromID(kWinSC4AppID);
	if (!parentWindow)
	{
		return false;
	}
	parentWindow->AddRef();

	cIGZUIScriptServicePtr scriptService;
	const cGZPersistResourceKey key(
		0x00000000, kWindowResourceGroup, kBasicWindowResourceInstance);
	if (!scriptService || !scriptService->CreateWindowFromScript(
		key, parentWindow, kBasicWindowCLSID, GZIID_cIGZWin,
		reinterpret_cast<void**>(&rootWindow)) || !rootWindow)
	{
		parentWindow->Release();
		parentWindow = nullptr;
		Logger::GetInstance().WriteLine(
			LogLevel::Error, "Window Manager: failed to create the basic window template.");
		return false;
	}

	parentWindow->ChildAdd(rootWindow);
	SetWinProc(rootWindow, this);
	const int32_t width = std::clamp(requestedOptions.width, 240, 900);
	const int32_t height = std::clamp(requestedOptions.height, 140, 700);
	if (!rootWindow->SetSize(width, height))
	{
		Logger::GetInstance().WriteLine(
			LogLevel::Warning, "Window Manager: the basic window rejected its requested size.");
	}

	cIGZWin* closeX = rootWindow->GetChildWindowFromIDRecursive(kBasicCloseXButtonID);
	cIGZWin* title = rootWindow->GetChildWindowFromIDRecursive(kBasicTitleID);
	cIGZWin* text = rootWindow->GetChildWindowFromIDRecursive(kBasicTextID);
	cIGZWin* action = rootWindow->GetChildWindowFromIDRecursive(kBasicActionButtonID);
	if (!closeX || !title || !text || !action)
	{
		Destroy();
		return false;
	}

	const int32_t widthDelta = width - 420;
	const int32_t heightDelta = height - 240;
	closeX->GZWinMoveTo(widthDelta, 0);
	title->SetSize(std::max(120, width - 62), title->GetH());
	text->SetSize(std::max(180, width - 40), std::max(40, height - 112));
	action->GZWinMoveTo(widthDelta, heightDelta);

	if (!SetTextCaption(title, requestedOptions.title))
	{
		Logger::GetInstance().WriteLine(
			LogLevel::Warning, "Window Manager: failed to set the basic window title caption.");
	}
	if (!SetTextCaption(text, requestedOptions.text))
	{
		Logger::GetInstance().WriteLine(
			LogLevel::Warning, "Window Manager: failed to set the basic window body caption.");
	}

	if (requestedOptions.button == SC4WindowButton::XOnly)
	{
		action->HideWindow();
	}
	else
	{
		const char* caption = "Close";
		if (requestedOptions.button == SC4WindowButton::OK)
		{
			caption = "OK";
		}
		else if (requestedOptions.button == SC4WindowButton::Accept)
		{
			caption = "Accept";
		}
		if (!SetButtonCaption(action, caption))
		{
			Logger::GetInstance().WriteLine(
				LogLevel::Warning, "Window Manager: failed to set the basic window action button caption.");
		}
		action->ShowWindow();
	}

	const int32_t windowX = std::max(0, (parentWindow->GetW() - width) / 2);
	const int32_t windowY = std::max(0, (parentWindow->GetH() - height) / 2);
	rootWindow->GZWinMoveTo(windowX, windowY);
	rootWindow->PullToFront();
	rootWindow->ShowWindow();
	Logger::GetInstance().WriteLine(LogLevel::Info, "Window Manager: basic window created.");
	return true;
}

void BasicManagedWindow::Destroy()
{
	if (rootWindow)
	{
		SetWinProc(rootWindow, nullptr);
		if (parentWindow)
		{
			parentWindow->ChildRemove(rootWindow);
		}
		rootWindow->Release();
		rootWindow = nullptr;
	}
	if (parentWindow)
	{
		parentWindow->Release();
		parentWindow = nullptr;
	}
}

void BasicManagedWindow::Close()
{
	if (rootWindow)
	{
		rootWindow->HideWindow();
	}
}

bool BasicManagedWindow::IsVisible() const
{
	return rootWindow && rootWindow->IsVisible();
}

bool BasicManagedWindow::ContainsPoint(int32_t parentX, int32_t parentY) const
{
	return WindowContainsParentPoint(rootWindow, parentX, parentY);
}

bool BasicManagedWindow::DoWinProcMessage(cIGZWin*, cGZMessage& msg)
{
	if (msg.dwMessageType == kMessageTypeCommand
		&& msg.dwData1 == kCommandButtonClicked
		&& (msg.dwData2 == kBasicCloseXButtonID || msg.dwData2 == kBasicActionButtonID))
	{
		Close();
		return true;
	}
	return false;
}

bool BasicManagedWindow::DoWinMsg(cIGZWin*, uint32_t, uint32_t, uint32_t, uint32_t)
{
	return false;
}

BakedManagedWindow::BakedManagedWindow(
	const char* logName,
	uint32_t resourceInstanceID,
	uint32_t rootCLSID,
	uint32_t closeXButtonID,
	uint32_t okButtonID,
	Placement placement)
	: logName(logName),
	resourceInstanceID(resourceInstanceID),
	rootCLSID(rootCLSID),
	closeXButtonID(closeXButtonID),
	okButtonID(okButtonID),
	placement(placement),
	refCount(0),
	parentWindow(nullptr),
	rootWindow(nullptr)
{
}

BakedManagedWindow::~BakedManagedWindow()
{
	Destroy();
}

bool BakedManagedWindow::QueryInterface(uint32_t riid, void** ppvObj)
{
	if (!ppvObj)
	{
		return false;
	}
	if (riid == GZIID_cIGZWinProc)
	{
		*ppvObj = static_cast<cIGZWinProc*>(this);
	}
	else if (riid == GZIID_cIGZUnknown)
	{
		*ppvObj = static_cast<cIGZUnknown*>(this);
	}
	else
	{
		*ppvObj = nullptr;
		return false;
	}

	AddRef();
	return true;
}

uint32_t BakedManagedWindow::AddRef()
{
	return ++refCount;
}

uint32_t BakedManagedWindow::Release()
{
	if (refCount > 0)
	{
		--refCount;
	}
	return refCount;
}

bool BakedManagedWindow::Create()
{
	if (rootWindow)
	{
		rootWindow->ShowWindow();
		rootWindow->PullToFront();
		return true;
	}

	cISC4AppPtr app;
	if (!app || !app->GetMainWindow())
	{
		Logger::GetInstance().WriteLine(
			LogLevel::Error,
			std::string(logName) + ": SC4 main window was unavailable.");
		return false;
	}

	parentWindow = app->GetMainWindow()->GetChildWindowFromID(kWinSC4AppID);
	if (!parentWindow)
	{
		Logger::GetInstance().WriteLine(
			LogLevel::Error,
			std::string(logName) + ": SC4 application window was unavailable.");
		return false;
	}
	parentWindow->AddRef();

	cIGZUIScriptServicePtr scriptService;
	const cGZPersistResourceKey key(0x00000000, kWindowResourceGroup, resourceInstanceID);
	if (!scriptService || !scriptService->CreateWindowFromScript(
		key, parentWindow, rootCLSID, GZIID_cIGZWin, reinterpret_cast<void**>(&rootWindow)) || !rootWindow)
	{
		Logger::GetInstance().WriteLine(
			LogLevel::Error,
			std::string(logName) + ": failed to load the UI resource from the Plugins folder.");
		parentWindow->Release();
		parentWindow = nullptr;
		return false;
	}

	parentWindow->ChildAdd(rootWindow);
	int32_t windowX = std::max(0, (parentWindow->GetW() - rootWindow->GetW()) / 2);
	int32_t windowY = std::max(0, (parentWindow->GetH() - rootWindow->GetH()) / 2);
	if (placement == Placement::TopRightButton)
	{
		windowX = std::max(0, parentWindow->GetW() - 150);
		windowY = 0;
	}
	rootWindow->GZWinMoveTo(windowX, windowY);
	SetWinProc(rootWindow, this);
	rootWindow->PullToFront();
	rootWindow->ShowWindow();
	OnCreated();
	Logger::GetInstance().WriteLine(LogLevel::Info, std::string(logName) + ": opened.");
	return true;
}

void BakedManagedWindow::Destroy()
{
	if (rootWindow)
	{
		SetWinProc(rootWindow, nullptr);
		if (parentWindow)
		{
			parentWindow->ChildDelete(rootWindow);
		}
		rootWindow->Release();
		rootWindow = nullptr;
	}
	if (parentWindow)
	{
		parentWindow->Release();
		parentWindow = nullptr;
	}
}

void BakedManagedWindow::Close()
{
	if (rootWindow)
	{
		rootWindow->HideWindow();
		OnClosed();
	}
}

void BakedManagedWindow::BringToFront()
{
	if (rootWindow)
	{
		rootWindow->PullToFront();
		rootWindow->InvalidateSelfAndParents();
	}
}

void BakedManagedWindow::SendToBack()
{
	if (rootWindow)
	{
		rootWindow->SendToBack();
	}
}

void BakedManagedWindow::SetVisible(bool visible)
{
	if (!rootWindow || rootWindow->IsVisible() == visible)
	{
		return;
	}

	if (visible)
	{
		rootWindow->ShowWindow();
		rootWindow->PullToFront();
	}
	else
	{
		rootWindow->HideWindow();
	}
	rootWindow->InvalidateSelfAndParents();
}

bool BakedManagedWindow::IsVisible() const
{
	return rootWindow && rootWindow->IsVisible();
}

bool BakedManagedWindow::ContainsPoint(int32_t parentX, int32_t parentY) const
{
	return WindowContainsParentPoint(rootWindow, parentX, parentY);
}

bool BakedManagedWindow::DoWinProcMessage(cIGZWin*, cGZMessage& msg)
{
	if (msg.dwMessageType == kMessageTypeCommand)
	{
		return OnCommandMessage(msg);
	}
	return false;
}

bool BakedManagedWindow::DoWinMsg(cIGZWin*, uint32_t, uint32_t, uint32_t, uint32_t)
{
	return false;
}

void BakedManagedWindow::OnCreated()
{
}

bool BakedManagedWindow::OnCommandMessage(cGZMessage& msg)
{
	if (msg.dwData1 == kCommandButtonClicked)
	{
		return OnButtonClick(msg.dwData2);
	}
	return false;
}

bool BakedManagedWindow::OnButtonClick(uint32_t controlID)
{
	if (controlID == closeXButtonID || controlID == okButtonID)
	{
		Close();
		return true;
	}
	return false;
}

void BakedManagedWindow::OnClosed()
{
}

cIGZWin* BakedManagedWindow::GetRootWindow() const
{
	return rootWindow;
}

SettingsWindow::SettingsWindow()
	: BakedManagedWindow(
		"Settings UI",
		kSettingsWindowResourceInstance,
		kSettingsWindowCLSID,
		kSettingsCloseXButtonID,
		kSettingsCloseButtonID),
	manager(nullptr),
	settings(nullptr),
	callbacks(nullptr),
	delayedSaveTimerID(0),
	delayedSavePending(false),
	delayedSaveReason()
{
}

void SettingsWindow::SetWindowManager(SC4WindowManager* windowManager)
{
	manager = windowManager;
}

void SettingsWindow::SetSettings(PluginSettings* pluginSettings)
{
	settings = pluginSettings;
}

void SettingsWindow::SetCallbacks(const SC4WindowManagerCallbacks* managerCallbacks)
{
	callbacks = managerCallbacks;
}

void SettingsWindow::RefreshFromSettings()
{
	SyncControlsFromSettings();
}

void SettingsWindow::OnCreated()
{
	SyncControlsFromSettings();
}

void SettingsWindow::ApplyChoice(uint32_t selectedID, const uint32_t* ids, size_t count)
{
	ApplyButtonChoice(GetRootWindow(), selectedID, ids, count);
}

void SettingsWindow::ApplyBooleanPair(uint32_t onID, uint32_t offID, bool value)
{
	const uint32_t ids[2] = { onID, offID };
	ApplyChoice(value ? onID : offID, ids, 2);
}

void SettingsWindow::SyncControlsFromSettings()
{
	if (!settings)
	{
		return;
	}

	const uint32_t cameraModeIDs[2] = { kSettingsModernButtonID, kSettingsClassicButtonID };
	ApplyChoice(
		settings->cameraMode == CameraMode::Modern ? kSettingsModernButtonID : kSettingsClassicButtonID,
		cameraModeIDs,
		2);
	ApplyBooleanPair(kSettingsWASDOnButtonID, kSettingsWASDOffButtonID, settings->wasdMovement);
	ApplyBooleanPair(kSettingsInvertOnButtonID, kSettingsInvertOffButtonID, settings->invertVertical);

	const bool modernCameraActive = settings->cameraMode == CameraMode::Modern;
	const uint32_t modernOnlyControlIDs[] = {
		kSettingsWASDOnButtonID,
		kSettingsWASDOffButtonID,
		kSettingsRotationSliderID,
		kSettingsZoomSliderID,
		kSettingsInvertOnButtonID,
		kSettingsInvertOffButtonID,
	};
	SetControlsEnabled(
		GetRootWindow(),
		modernOnlyControlIDs,
		sizeof(modernOnlyControlIDs) / sizeof(modernOnlyControlIDs[0]),
		modernCameraActive);
}

void SettingsWindow::ApplySettingsChange(const char* reason, bool saveImmediately)
{
	if (!settings)
	{
		return;
	}
	Logger::GetInstance().WriteLine(
		LogLevel::Info,
		"Settings UI state: mode="
		+ std::string(settings->cameraMode == CameraMode::Modern ? "Modern" : "Classic")
		+ " wasd=" + (settings->wasdMovement ? "true" : "false")
		+ " rotationSensitivity=" + std::to_string(settings->rotationSensitivity)
		+ " zoomSensitivity=" + std::to_string(settings->zoomSensitivity)
		+ " invertVertical=" + (settings->invertVertical ? "true" : "false")
		+ " redrawAggression="
		+ (settings->redrawAggression == RedrawAggression::Classic ? "Classic"
			: settings->redrawAggression == RedrawAggression::High ? "High"
			: settings->redrawAggression == RedrawAggression::Extreme ? "Extreme"
			: "Normal")
		+ " debugLogging="
		+ ToDebugLoggingString(settings->debugLogging));

	if (!saveImmediately)
	{
		ScheduleSettingsSave(reason);
		SyncControlsFromSettings();
		return;
	}

	FlushPendingSettingsSave();
	if (!settings->Save())
	{
		Logger::GetInstance().WriteLine(
			LogLevel::Warning,
			std::string("Settings UI: failed to save settings after ") + (reason ? reason : "change") + ".");
	}
	else
	{
		Logger::GetInstance().WriteLine(
			LogLevel::Info,
			std::string("Settings UI: saved settings after ") + (reason ? reason : "change") + ".");
	}
	SyncControlsFromSettings();
}

void SettingsWindow::ScheduleSettingsSave(const char* reason)
{
	delayedSavePending = true;
	delayedSaveReason = reason ? reason : "change";
	if (delayedSaveTimerID != 0)
	{
		KillTimer(NULL, static_cast<UINT_PTR>(delayedSaveTimerID));
		delayedSaveTimerID = 0;
	}

	g_DelayedSettingsSaveWindow = this;
	delayedSaveTimerID = static_cast<uintptr_t>(
		SetTimer(NULL, 0, kSettingsSliderSaveDelayMs, DelayedSettingsSaveTimerProc));
	if (delayedSaveTimerID == 0)
	{
		Logger::GetInstance().WriteLine(
			LogLevel::Warning,
			"Settings UI: failed to start delayed settings save timer; saving immediately.");
		FlushPendingSettingsSave();
	}
}

void SettingsWindow::FlushPendingSettingsSave()
{
	if (delayedSaveTimerID != 0)
	{
		KillTimer(NULL, static_cast<UINT_PTR>(delayedSaveTimerID));
		delayedSaveTimerID = 0;
	}
	if (g_DelayedSettingsSaveWindow == this)
	{
		g_DelayedSettingsSaveWindow = nullptr;
	}

	if (!delayedSavePending || !settings)
	{
		return;
	}

	delayedSavePending = false;
	const std::string reason = delayedSaveReason.empty() ? "change" : delayedSaveReason;
	delayedSaveReason.clear();
	if (!settings->Save())
	{
		Logger::GetInstance().WriteLine(
			LogLevel::Warning,
			"Settings UI: failed to save delayed settings after " + reason + ".");
	}
	else
	{
		Logger::GetInstance().WriteLine(
			LogLevel::Info,
			"Settings UI: saved delayed settings after "
			+ reason
			+ "; rotationSensitivity="
			+ FormatSettingFloat(settings->rotationSensitivity)
			+ " zoomSensitivity="
			+ FormatSettingFloat(settings->zoomSensitivity)
			+ ".");
	}
}

void SettingsWindow::OnDelayedSettingsSaveTimer()
{
	FlushPendingSettingsSave();
}

float SettingsWindow::ReadSliderValueFromCursor(uint32_t sliderID, float fallback) const
{
	cIGZWin* root = GetRootWindow();
	cIGZWin* slider = root ? root->GetChildWindowFromIDRecursive(sliderID) : nullptr;
	if (!root || !slider)
	{
		return fallback;
	}

	POINT cursor{};
	HWND activeWindow = GetActiveWindow();
	if (!activeWindow || !GetCursorPos(&cursor) || !ScreenToClient(activeWindow, &cursor))
	{
		return fallback;
	}

	const int32_t localX = cursor.x - root->GetL();
	const int32_t left = slider->GetL();
	const int32_t width = std::max(1, slider->GetW());
	const float ratio = std::clamp(
		static_cast<float>(localX - left) / static_cast<float>(width),
		0.0f,
		1.0f);
	return 0.1f + (ratio * 2.9f);
}

bool SettingsWindow::OnCommandMessage(cGZMessage& msg)
{
	if (!settings)
	{
		return BakedManagedWindow::OnCommandMessage(msg);
	}

	if (msg.dwData1 == kCommandValueChanged)
	{
		if (msg.dwData2 == kSettingsRotationSliderID)
		{
			if (settings->cameraMode != CameraMode::Modern)
			{
				Logger::GetInstance().WriteLine(
					LogLevel::Verbose,
					"Settings UI: ignored rotation sensitivity slider while Classic camera is selected.");
				return true;
			}
			const float previous = settings->rotationSensitivity;
			settings->rotationSensitivity = ReadSliderValueFromCursor(
				kSettingsRotationSliderID,
				settings->rotationSensitivity);
			LogOptionChanged(
				"Settings UI",
				"Rotation Sensitivity",
				FormatSettingFloat(previous),
				FormatSettingFloat(settings->rotationSensitivity),
				LogLevel::Verbose);
			ApplySettingsChange("rotation sensitivity change", false);
			return true;
		}
		if (msg.dwData2 == kSettingsZoomSliderID)
		{
			if (settings->cameraMode != CameraMode::Modern)
			{
				Logger::GetInstance().WriteLine(
					LogLevel::Verbose,
					"Settings UI: ignored zoom sensitivity slider while Classic camera is selected.");
				return true;
			}
			const float previous = settings->zoomSensitivity;
			settings->zoomSensitivity = ReadSliderValueFromCursor(
				kSettingsZoomSliderID,
				settings->zoomSensitivity);
			LogOptionChanged(
				"Settings UI",
				"Zoom Sensitivity",
				FormatSettingFloat(previous),
				FormatSettingFloat(settings->zoomSensitivity),
				LogLevel::Verbose);
			ApplySettingsChange("zoom sensitivity change", false);
			return true;
		}
	}

	return BakedManagedWindow::OnCommandMessage(msg);
}

bool SettingsWindow::OnButtonClick(uint32_t controlID)
{
	if (settings)
	{
		const bool modernCameraActive = settings->cameraMode == CameraMode::Modern;
		switch (controlID)
		{
		case kSettingsModernButtonID:
		{
			const CameraMode previous = settings->cameraMode;
			settings->cameraMode = CameraMode::Modern;
			LogOptionChanged(
				"Settings UI",
				"Camera Mode",
				ToCameraModeString(previous),
				ToCameraModeString(settings->cameraMode));
			ApplySettingsChange("camera mode change");
			if (manager)
			{
				manager->RefreshSettingsWindows();
			}
			if (callbacks && callbacks->OnCameraModeChanged)
			{
				callbacks->OnCameraModeChanged(true);
			}
			return true;
		}
		case kSettingsClassicButtonID:
		{
			const CameraMode previous = settings->cameraMode;
			settings->cameraMode = CameraMode::Classic;
			LogOptionChanged(
				"Settings UI",
				"Camera Mode",
				ToCameraModeString(previous),
				ToCameraModeString(settings->cameraMode));
			ApplySettingsChange("camera mode change");
			if (manager)
			{
				manager->RefreshSettingsWindows();
			}
			if (callbacks && callbacks->OnResetCameraRequested)
			{
				callbacks->OnResetCameraRequested();
			}
			if (callbacks && callbacks->OnCameraModeChanged)
			{
				callbacks->OnCameraModeChanged(false);
			}
			return true;
		}
		case kSettingsWASDOnButtonID:
			if (!modernCameraActive)
			{
				Logger::GetInstance().WriteLine(
					LogLevel::Verbose,
					"Settings UI: ignored WASD Movement On while Classic camera is selected.");
				return true;
			}
			LogOptionChanged("Settings UI", "WASD Movement", ToOnOffString(settings->wasdMovement), "On");
			settings->wasdMovement = true;
			ApplySettingsChange("WASD movement change");
			if (callbacks && callbacks->OnInputSettingsChanged)
			{
				callbacks->OnInputSettingsChanged();
			}
			return true;
		case kSettingsWASDOffButtonID:
			if (!modernCameraActive)
			{
				Logger::GetInstance().WriteLine(
					LogLevel::Verbose,
					"Settings UI: ignored WASD Movement Off while Classic camera is selected.");
				return true;
			}
			LogOptionChanged("Settings UI", "WASD Movement", ToOnOffString(settings->wasdMovement), "Off");
			settings->wasdMovement = false;
			ApplySettingsChange("WASD movement change");
			if (callbacks && callbacks->OnInputSettingsChanged)
			{
				callbacks->OnInputSettingsChanged();
			}
			return true;
		case kSettingsInvertOnButtonID:
			if (!modernCameraActive)
			{
				Logger::GetInstance().WriteLine(
					LogLevel::Verbose,
					"Settings UI: ignored Invert Vertical On while Classic camera is selected.");
				return true;
			}
			LogOptionChanged("Settings UI", "Invert Vertical", ToOnOffString(settings->invertVertical), "On");
			settings->invertVertical = true;
			ApplySettingsChange("invert vertical change");
			return true;
		case kSettingsInvertOffButtonID:
			if (!modernCameraActive)
			{
				Logger::GetInstance().WriteLine(
					LogLevel::Verbose,
					"Settings UI: ignored Invert Vertical Off while Classic camera is selected.");
				return true;
			}
			LogOptionChanged("Settings UI", "Invert Vertical", ToOnOffString(settings->invertVertical), "Off");
			settings->invertVertical = false;
			ApplySettingsChange("invert vertical change");
			return true;
		case kSettingsRedrawOffButtonID:
			if (!modernCameraActive)
			{
				Logger::GetInstance().WriteLine(
					LogLevel::Verbose,
					"Settings UI: ignored Redraw Aggression Classic while Classic camera is selected.");
				return true;
			}
			LogOptionChanged(
				"Settings UI",
				"Redraw Aggression",
				ToRedrawAggressionString(settings->redrawAggression),
				"Classic");
			settings->redrawAggression = RedrawAggression::Classic;
			ApplySettingsChange("redraw aggression change");
			if (callbacks && callbacks->OnRedrawAggressionChanged)
			{
				callbacks->OnRedrawAggressionChanged();
			}
			if (callbacks && callbacks->OnDebugLoggingChanged)
			{
				callbacks->OnDebugLoggingChanged();
			}
			return true;
		case kSettingsRedrawNormalButtonID:
			if (!modernCameraActive)
			{
				Logger::GetInstance().WriteLine(
					LogLevel::Verbose,
					"Settings UI: ignored Redraw Aggression Normal while Classic camera is selected.");
				return true;
			}
			LogOptionChanged(
				"Settings UI",
				"Redraw Aggression",
				ToRedrawAggressionString(settings->redrawAggression),
				"Normal");
			settings->redrawAggression = RedrawAggression::Normal;
			ApplySettingsChange("redraw aggression change");
			if (callbacks && callbacks->OnRedrawAggressionChanged)
			{
				callbacks->OnRedrawAggressionChanged();
			}
			return true;
		case kSettingsRedrawHighButtonID:
			if (!modernCameraActive)
			{
				Logger::GetInstance().WriteLine(
					LogLevel::Verbose,
					"Settings UI: ignored Redraw Aggression High while Classic camera is selected.");
				return true;
			}
			LogOptionChanged(
				"Settings UI",
				"Redraw Aggression",
				ToRedrawAggressionString(settings->redrawAggression),
				"High");
			settings->redrawAggression = RedrawAggression::High;
			ApplySettingsChange("redraw aggression change");
			if (callbacks && callbacks->OnRedrawAggressionChanged)
			{
				callbacks->OnRedrawAggressionChanged();
			}
			return true;
		case kSettingsRedrawAggressiveButtonID:
			if (!modernCameraActive)
			{
				Logger::GetInstance().WriteLine(
					LogLevel::Verbose,
					"Settings UI: ignored Redraw Aggression Extreme while Classic camera is selected.");
				return true;
			}
			LogOptionChanged(
				"Settings UI",
				"Redraw Aggression",
				ToRedrawAggressionString(settings->redrawAggression),
				"Extreme");
			settings->redrawAggression = RedrawAggression::Extreme;
			ApplySettingsChange("redraw aggression change");
			if (callbacks && callbacks->OnRedrawAggressionChanged)
			{
				callbacks->OnRedrawAggressionChanged();
			}
			return true;
		case kSettingsRestoreDefaultsButtonID:
		{
			const CameraMode previousCameraMode = settings->cameraMode;
			const bool previousWASD = settings->wasdMovement;
			const float previousRotationSensitivity = settings->rotationSensitivity;
			const float previousZoomSensitivity = settings->zoomSensitivity;
			const bool previousInvertVertical = settings->invertVertical;
			const RedrawAggression previousRedrawAggression = settings->redrawAggression;
			const DebugLogging previousDebugLogging = settings->debugLogging;
			settings->RestoreDefaults();
			LogOptionChanged(
				"Settings UI",
				"Camera Mode",
				ToCameraModeString(previousCameraMode),
				ToCameraModeString(settings->cameraMode));
			LogOptionChanged(
				"Settings UI",
				"WASD Movement",
				ToOnOffString(previousWASD),
				ToOnOffString(settings->wasdMovement));
			LogOptionChanged(
				"Settings UI",
				"Rotation Sensitivity",
				FormatSettingFloat(previousRotationSensitivity),
				FormatSettingFloat(settings->rotationSensitivity));
			LogOptionChanged(
				"Settings UI",
				"Zoom Sensitivity",
				FormatSettingFloat(previousZoomSensitivity),
				FormatSettingFloat(settings->zoomSensitivity));
			LogOptionChanged(
				"Settings UI",
				"Invert Vertical",
				ToOnOffString(previousInvertVertical),
				ToOnOffString(settings->invertVertical));
			LogOptionChanged(
				"Settings UI",
				"Redraw Aggression",
				ToRedrawAggressionString(previousRedrawAggression),
				ToRedrawAggressionString(settings->redrawAggression));
			LogOptionChanged(
				"Settings UI",
				"Diagnostics Logging",
				ToDebugLoggingString(previousDebugLogging),
				ToDebugLoggingString(settings->debugLogging));
			ApplySettingsChange("restore defaults");
			if (manager)
			{
				manager->RefreshSettingsWindows();
				manager->ScheduleSettingsWindowReopen();
			}
			if (callbacks && callbacks->OnCameraModeChanged)
			{
				callbacks->OnCameraModeChanged(settings->cameraMode == CameraMode::Modern);
			}
			if (callbacks && callbacks->OnInputSettingsChanged)
			{
				callbacks->OnInputSettingsChanged();
			}
			if (callbacks && callbacks->OnRedrawAggressionChanged)
			{
				callbacks->OnRedrawAggressionChanged();
			}
			return true;
		}
		case kSettingsResetCameraButtonID:
			if (callbacks && callbacks->OnResetCameraRequested)
			{
				callbacks->OnResetCameraRequested();
			}
			return true;
		case kSettingsAdvancedButtonID:
			if (manager)
			{
				manager->ScheduleDeferredWindowOpen(DeferredWindowOpen::AdvancedSettings);
			}
			return true;
		default:
			break;
		}
	}

	if (controlID == kSettingsReadChangelogButtonID)
	{
		if (manager)
		{
			manager->ScheduleDeferredWindowOpen(DeferredWindowOpen::Changelog);
		}
		return true;
	}
	return BakedManagedWindow::OnButtonClick(controlID);
}

void SettingsWindow::OnClosed()
{
	FlushPendingSettingsSave();
	if (manager)
	{
		manager->OnSettingsWindowClosed();
	}
}

MenuButtonWindow::MenuButtonWindow()
	: BakedManagedWindow(
		"Menu Button UI",
		kMenuButtonResourceInstance,
		kMenuButtonCLSID,
		0,
		0,
		Placement::TopRightButton),
	manager(nullptr)
{
}

void MenuButtonWindow::SetWindowManager(SC4WindowManager* windowManager)
{
	manager = windowManager;
}

void MenuButtonWindow::SetSettingsOpen(bool open)
{
	cIGZWin* buttonWindow = GetRootWindow()
		? GetRootWindow()->GetChildWindowFromIDRecursive(kMenuButtonID)
		: nullptr;
	cRZAutoRefCount<cIGZWinBtn> button;
	if (buttonWindow && buttonWindow->QueryInterface(GZIID_cIGZWinBtn, button.AsPPVoid()))
	{
		if (open)
		{
			button->ToggleOn();
		}
		else
		{
			button->ToggleOff();
		}
	}
}

bool MenuButtonWindow::OnButtonClick(uint32_t controlID)
{
	if (controlID == kMenuButtonID)
	{
		const bool opened = manager && manager->ToggleSettingsWindow();
		SetSettingsOpen(opened);
		return true;
	}
	return false;
}

GreetingWindow::GreetingWindow()
	: BakedManagedWindow(
		"Greeting UI",
		kGreetingWindowResourceInstance,
		kGreetingWindowCLSID,
		kGreetingCloseXButtonID,
		kGreetingOKButtonID),
	manager(nullptr)
{
}

void GreetingWindow::SetWindowManager(SC4WindowManager* windowManager)
{
	manager = windowManager;
}

bool GreetingWindow::OnButtonClick(uint32_t controlID)
{
	if (controlID == kGreetingViewControlsButtonID)
	{
		if (manager)
		{
			manager->ShowControlsWindow();
		}
		return true;
	}
	return BakedManagedWindow::OnButtonClick(controlID);
}

ControlsWindow::ControlsWindow()
	: BakedManagedWindow(
		"Controls UI",
		kControlsWindowResourceInstance,
		kControlsWindowCLSID,
		kControlsCloseXButtonID,
		kControlsOKButtonID)
{
}

AdvancedSettingsWindow::AdvancedSettingsWindow()
	: BakedManagedWindow(
		"Advanced Settings UI",
		kAdvancedWindowResourceInstance,
		kAdvancedWindowCLSID,
		kAdvancedCloseXButtonID,
		kAdvancedCloseButtonID),
	settings(nullptr),
	callbacks(nullptr)
{
}

void AdvancedSettingsWindow::SetSettings(PluginSettings* pluginSettings)
{
	settings = pluginSettings;
}

void AdvancedSettingsWindow::SetCallbacks(const SC4WindowManagerCallbacks* managerCallbacks)
{
	callbacks = managerCallbacks;
}

void AdvancedSettingsWindow::RefreshFromSettings()
{
	SyncControlsFromSettings();
}

void AdvancedSettingsWindow::OnCreated()
{
	SyncControlsFromSettings();
}

void AdvancedSettingsWindow::SyncControlsFromSettings()
{
	if (!settings)
	{
		return;
	}

	const uint32_t redrawIDs[4] = {
		kSettingsRedrawOffButtonID,
		kSettingsRedrawNormalButtonID,
		kSettingsRedrawHighButtonID,
		kSettingsRedrawAggressiveButtonID,
	};
	const bool modernCameraActive = settings->cameraMode == CameraMode::Modern;
	uint32_t selectedRedrawID = modernCameraActive ? kSettingsRedrawNormalButtonID : kSettingsRedrawOffButtonID;
	if (modernCameraActive)
	{
		switch (settings->redrawAggression)
		{
		case RedrawAggression::Classic:
			selectedRedrawID = kSettingsRedrawOffButtonID;
			break;
		case RedrawAggression::High:
			selectedRedrawID = kSettingsRedrawHighButtonID;
			break;
		case RedrawAggression::Extreme:
			selectedRedrawID = kSettingsRedrawAggressiveButtonID;
			break;
		default:
			break;
		}
	}
	ApplyButtonChoice(GetRootWindow(), selectedRedrawID, redrawIDs, 4);
	if (modernCameraActive)
	{
		SetControlsEnabled(GetRootWindow(), redrawIDs, 4, true);
	}
	else
	{
		SetControlEnabled(GetRootWindow(), kSettingsRedrawOffButtonID, true);
		SetControlEnabled(GetRootWindow(), kSettingsRedrawNormalButtonID, false);
		SetControlEnabled(GetRootWindow(), kSettingsRedrawHighButtonID, false);
		SetControlEnabled(GetRootWindow(), kSettingsRedrawAggressiveButtonID, false);
	}

	const uint32_t debugLoggingIDs[3] = {
		kAdvancedDebugLoggingOffButtonID,
		kAdvancedDebugLoggingNormalButtonID,
		kAdvancedDebugLoggingVerboseButtonID,
	};
	uint32_t selectedDebugLoggingID = kAdvancedDebugLoggingNormalButtonID;
	switch (settings->debugLogging)
	{
	case DebugLogging::Off:
		selectedDebugLoggingID = kAdvancedDebugLoggingOffButtonID;
		break;
	case DebugLogging::Verbose:
		selectedDebugLoggingID = kAdvancedDebugLoggingVerboseButtonID;
		break;
	default:
		break;
	}
	ApplyButtonChoice(GetRootWindow(), selectedDebugLoggingID, debugLoggingIDs, 3);
}

bool AdvancedSettingsWindow::OnButtonClick(uint32_t controlID)
{
	if (settings)
	{
		if (settings->cameraMode != CameraMode::Modern && IsRedrawAggressionControl(controlID))
		{
			Logger::GetInstance().WriteLine(
				LogLevel::Verbose,
				"Advanced Settings UI: ignored Redraw Aggression button while Classic camera is selected.");
			SyncControlsFromSettings();
			return true;
		}

		bool changed = true;
		bool redrawChanged = false;
		bool debugLoggingChanged = false;
		const RedrawAggression previousRedrawAggression = settings->redrawAggression;
		const DebugLogging previousDebugLogging = settings->debugLogging;
		switch (controlID)
		{
		case kSettingsRedrawOffButtonID:
			settings->redrawAggression = RedrawAggression::Classic;
			redrawChanged = true;
			break;
		case kSettingsRedrawNormalButtonID:
			settings->redrawAggression = RedrawAggression::Normal;
			redrawChanged = true;
			break;
		case kSettingsRedrawHighButtonID:
			settings->redrawAggression = RedrawAggression::High;
			redrawChanged = true;
			break;
		case kSettingsRedrawAggressiveButtonID:
			settings->redrawAggression = RedrawAggression::Extreme;
			redrawChanged = true;
			break;
		case kAdvancedDebugLoggingOffButtonID:
			settings->debugLogging = DebugLogging::Off;
			debugLoggingChanged = true;
			break;
		case kAdvancedDebugLoggingNormalButtonID:
			settings->debugLogging = DebugLogging::Normal;
			debugLoggingChanged = true;
			break;
		case kAdvancedDebugLoggingVerboseButtonID:
			settings->debugLogging = DebugLogging::Verbose;
			debugLoggingChanged = true;
			break;
		default:
			changed = false;
			break;
		}

		if (changed)
		{
			if (redrawChanged)
			{
				LogOptionChanged(
					"Advanced Settings UI",
					"Redraw Aggression",
					ToRedrawAggressionString(previousRedrawAggression),
					ToRedrawAggressionString(settings->redrawAggression));
			}
			if (debugLoggingChanged)
			{
				LogOptionChanged(
					"Advanced Settings UI",
					"Diagnostics Logging",
					ToDebugLoggingString(previousDebugLogging),
					ToDebugLoggingString(settings->debugLogging));
			}

			const char* reason = redrawChanged && debugLoggingChanged
				? "advanced settings change"
				: redrawChanged ? "redraw aggression change"
				: "diagnostics change";
			if (!settings->Save())
			{
				Logger::GetInstance().WriteLine(
					LogLevel::Warning,
					std::string("Advanced Settings UI: failed to save settings after ") + reason + ".");
			}
			else
			{
				Logger::GetInstance().WriteLine(
					LogLevel::Info,
					std::string("Advanced Settings UI: saved settings after ") + reason + ".");
			}
			SyncControlsFromSettings();
			if (redrawChanged && callbacks && callbacks->OnRedrawAggressionChanged)
			{
				callbacks->OnRedrawAggressionChanged();
			}
			if (debugLoggingChanged && callbacks && callbacks->OnDebugLoggingChanged)
			{
				callbacks->OnDebugLoggingChanged();
			}
			return true;
		}
	}

	return BakedManagedWindow::OnButtonClick(controlID);
}

ControlLaboratoryWindow::ControlLaboratoryWindow()
	: refCount(0), parentWindow(nullptr), rootWindow(nullptr), scrollOffset(0), wheelDeltaRemainder(0),
	wheelScrollbarDirection(0), wheelScrollbarNotificationHandled(false)
{
}

ControlLaboratoryWindow::~ControlLaboratoryWindow()
{
	Destroy();
}

bool ControlLaboratoryWindow::QueryInterface(uint32_t riid, void** ppvObj)
{
	if (!ppvObj)
	{
		return false;
	}

	if (riid == GZIID_cIGZWinProc)
	{
		*ppvObj = static_cast<cIGZWinProc*>(this);
	}
	else if (riid == GZIID_cIGZUnknown)
	{
		*ppvObj = static_cast<cIGZUnknown*>(this);
	}
	else
	{
		*ppvObj = nullptr;
		return false;
	}

	AddRef();
	return true;
}

uint32_t ControlLaboratoryWindow::AddRef()
{
	return ++refCount;
}

uint32_t ControlLaboratoryWindow::Release()
{
	if (refCount > 0)
	{
		--refCount;
	}
	return refCount;
}

bool ControlLaboratoryWindow::Create()
{
	if (rootWindow)
	{
		rootWindow->ShowWindow();
		rootWindow->PullToFront();
		return true;
	}

	cISC4AppPtr app;
	if (!app || !app->GetMainWindow())
	{
		Logger::GetInstance().WriteLine(LogLevel::Error, "Test UI: SC4 main window was unavailable.");
		return false;
	}

	parentWindow = app->GetMainWindow()->GetChildWindowFromID(kWinSC4AppID);
	if (!parentWindow)
	{
		Logger::GetInstance().WriteLine(LogLevel::Error, "Test UI: SC4 application window was unavailable.");
		return false;
	}
	parentWindow->AddRef();

	cIGZUIScriptServicePtr scriptService;
	const cGZPersistResourceKey key(0x00000000, kWindowResourceGroup, kWindowResourceInstance);
	if (!scriptService || !scriptService->CreateWindowFromScript(
		key, parentWindow, kWindowCLSID, GZIID_cIGZWin, reinterpret_cast<void**>(&rootWindow)) || !rootWindow)
	{
		Logger::GetInstance().WriteLine(
			LogLevel::Error,
			"Test UI: failed to load SC4-ModernCamera.dat from the Plugins folder.");
		parentWindow->Release();
		parentWindow = nullptr;
		return false;
	}

	parentWindow->ChildAdd(rootWindow);
	Logger::GetInstance().WriteLine(LogLevel::Info, "Test UI: window resource created; positioning it.");
	const int32_t windowX = std::max(0, (parentWindow->GetW() - rootWindow->GetW()) / 2);
	const int32_t windowY = std::max(0, (parentWindow->GetH() - rootWindow->GetH()) / 2);
	rootWindow->GZWinMoveTo(windowX, windowY);
	SetWinProc(rootWindow, this);

	Logger::GetInstance().WriteLine(LogLevel::Info, "Test UI: building native controls.");
	if (!BuildControls())
	{
		Destroy();
		return false;
	}

	rootWindow->PullToFront();
	rootWindow->ShowWindow();
	SaveTestState();
	Logger::GetInstance().WriteLine(LogLevel::Info, "Test UI: native control laboratory opened.");
	return true;
}

void ControlLaboratoryWindow::Destroy()
{
	if (!rootWindow)
	{
		if (parentWindow)
		{
			parentWindow->Release();
			parentWindow = nullptr;
		}
		return;
	}

	SetWinProc(rootWindow, nullptr);
	rootWindow->ChildDeleteAll();
	for (const ChildControl& control : controls)
	{
		if (control.interfacePointer)
		{
			control.interfacePointer->Release();
		}
	}
	controls.clear();

	if (parentWindow)
	{
		parentWindow->ChildDelete(rootWindow);
	}
	rootWindow->Release();
	rootWindow = nullptr;

	if (parentWindow)
	{
		parentWindow->Release();
		parentWindow = nullptr;
	}
	scrollOffset = 0;
	wheelDeltaRemainder = 0;
	wheelScrollbarDirection = 0;
	wheelScrollbarNotificationHandled = false;
	Logger::GetInstance().WriteLine(LogLevel::Info, "Test UI: native control laboratory closed.");
}

bool ControlLaboratoryWindow::IsVisible() const
{
	return rootWindow && rootWindow->IsVisible();
}

bool ControlLaboratoryWindow::ContainsPoint(int32_t parentX, int32_t parentY) const
{
	return WindowContainsParentPoint(rootWindow, parentX, parentY);
}

bool ControlLaboratoryWindow::HandleMouseWheel(
	int32_t wheelDelta, int32_t parentX, int32_t parentY, intptr_t nativeWindowHandle)
{
	if (!ContainsPoint(parentX, parentY))
	{
		return false;
	}

	// Retain partial high-resolution wheel deltas until they form a standard
	// notch. Positive wheel motion scrolls toward the top of the content.
	wheelDeltaRemainder += wheelDelta;
	while (wheelDeltaRemainder >= WHEEL_DELTA)
	{
		ActivateScrollbarArrow(-1, nativeWindowHandle);
		wheelDeltaRemainder -= WHEEL_DELTA;
	}
	while (wheelDeltaRemainder <= -WHEEL_DELTA)
	{
		ActivateScrollbarArrow(1, nativeWindowHandle);
		wheelDeltaRemainder += WHEEL_DELTA;
	}
	return true;
}

void ControlLaboratoryWindow::ActivateScrollbarArrow(int32_t direction, intptr_t nativeWindowHandle)
{
	cIGZWin* scrollbar = rootWindow ? rootWindow->GetChildWindowFromID(kScrollBarID) : nullptr;
	HWND windowHandle = reinterpret_cast<HWND>(nativeWindowHandle);
	if (!scrollbar || !windowHandle)
	{
		ScrollBy(direction * 40);
		return;
	}

	// Send the same input SC4 receives when the user clicks a scrollbar arrow.
	// Its native control updates the thumb, then our scrollbar notification
	// updates the content using the same 40-pixel line step.
	const int32_t x = rootWindow->GetL() + ((scrollbar->GetL() + scrollbar->GetR()) / 2);
	const int32_t y = rootWindow->GetT()
		+ (direction < 0 ? scrollbar->GetT() + 12 : scrollbar->GetB() - 12);
	const LPARAM position = MAKELPARAM(x, y);

	wheelScrollbarDirection = direction;
	wheelScrollbarNotificationHandled = false;
	SendMessage(windowHandle, WM_LBUTTONDOWN, MK_LBUTTON, position);
	SendMessage(windowHandle, WM_LBUTTONUP, 0, position);
	wheelScrollbarDirection = 0;

	// Preserve wheel scrolling if a renderer/window configuration does not
	// produce a synchronous scrollbar notification.
	if (!wheelScrollbarNotificationHandled)
	{
		ScrollBy(direction * 40);
	}
}

void ControlLaboratoryWindow::AddControl(cIGZWin* window, cIGZUnknown* interfacePointer, uint32_t id,
	const char* name, int32_t left, int32_t top, int32_t right, int32_t bottom, bool fixed)
{
	if (!window || !interfacePointer)
	{
		return;
	}

	window->SetID(id);
	window->SetArea(left, top, right, bottom);
	window->SetNotificationTarget(rootWindow);
	rootWindow->ChildAdd(window);
	controls.push_back({ window, interfacePointer, id, left, top, right - left, bottom - top, fixed, name });
}

bool ControlLaboratoryWindow::BuildControls()
{
	// Controls are authored in the UI DAT and instantiated by SC4's own UI
	// script service. The cIGZWinCtrlMgr factory methods are still incomplete
	// reverse-engineered interfaces and caused an x86 stack-balance failure.
	struct ScriptControl { uint32_t id; const char* name; bool fixed; };
	const ScriptControl scriptControls[] = {
		{ kCloseButtonID, "close", true },
		{ kCloseXButtonID, "closeX", true },
		{ kScrollBarID, "verticalScrollbar", true },
		{ 0x3D0C0720, "title", true }, { 0x3D0C0721, "description", true },
		{ 0x3D0C0722, "buttonHeading", false },
		{ 0x3D0C0730, "standardButton", false }, { 0x3D0C0731, "toggleButton", false },
		{ 0x3D0C0734, "checkBox", false }, { 0x3D0C0738, "checkBoxLabel", false },
		{ 0x3D0C0740, "inputHeading", false },
		{ 0x3D0C0750, "horizontalSlider", false }, { 0x3D0C0751, "horizontalScrollbar", false },
		{ 0x3D0C0752, "spinner", false }, { 0x3D0C0753, "editableText", false },
		{ 0x3D0C0754, "readOnlyText", false }, { 0x3D0C0755, "optionGroup", false },
		{ 0x3D0C0760, "textHeading", false }, { 0x3D0C0761, "regularText", false },
		{ 0x3D0C0762, "headerText", false }, { 0x3D0C0763, "lightText", false },
		{ 0x3D0C0770, "colorHeading", false }, { 0x3D0C0771, "purpleText", false },
		{ 0x3D0C0772, "redText", false }, { 0x3D0C0773, "greenText", false },
		{ 0x3D0C0774, "blueText", false }, { 0x3D0C0780, "endText", false },
	};

	for (const ScriptControl& spec : scriptControls)
	{
		cIGZWin* child = rootWindow->GetChildWindowFromIDRecursive(spec.id);
		if (child)
		{
			if (spec.id == kScrollUpButtonID || spec.id == kScrollDownButtonID)
			{
				child->HideWindow();
			}
			const int32_t left = child->GetL();
			const int32_t top = child->GetT();
			controls.push_back({
				child,
				nullptr,
				spec.id,
				left,
				top,
				child->GetR() - left,
				child->GetB() - top,
				spec.fixed,
				spec.name });
		}
		else
		{
			std::ostringstream message;
			message << "Test UI: scripted control missing, id=0x" << std::hex << std::uppercase << spec.id;
			Logger::GetInstance().WriteLine(LogLevel::Warning, message.str());
		}
	}

	Logger::GetInstance().WriteLine(
		LogLevel::Info,
		"Test UI: registered " + std::to_string(controls.size()) + " scripted controls.");
	ApplyScrollPosition();
	return !controls.empty();

	cIGZWinCtrlMgrPtr manager;
	if (!manager)
	{
		Logger::GetInstance().WriteLine(LogLevel::Error, "Test UI: control manager was unavailable.");
		return false;
	}
	Logger::GetInstance().WriteLine(LogLevel::Info, "Test UI: control manager acquired.");

	auto addLabel = [&](uint32_t id, const char* caption, int32_t top, uint32_t style = 0, uint32_t color = 0x203050)
	{
		cRZBaseString text(caption);
		cIGZWinText* label = manager->CreateLabel(id, text);
		if (label)
		{
			label->SetFontStyle(style);
			label->SetTextColor(color);
			AddControl(label->AsIGZWin(), label, id, caption, 24, top, 490, top + 22);
		}
	};

	auto addButton = [&](uint32_t id, const char* name, const char* caption, int32_t left, int32_t top,
		cIGZWinBtn* button, bool fixed = false)
	{
		Logger::GetInstance().WriteLine(LogLevel::Info, std::string("Test UI: adding ") + name + '.');
		if (button)
		{
			button->SetBtnFlag(cIGZWinBtn::BtnFlagShowCaption, true);
			button->SetBtnFlag(cIGZWinBtn::BtnFlagFill, true);
			AddControl(button->AsIGZWin(), button, id, name, left, top, left + 180, top + 28, fixed);
		}
	};

	addLabel(0x3D0C0720, "SC4-ModernCamera Native UI Control Laboratory", 48, cIGZFont::Style_Bold);
	Logger::GetInstance().WriteLine(LogLevel::Info, "Test UI: label creation succeeded.");
	addLabel(0x3D0C0721, "Each interaction is logged and written to Plugins/SC4-ModernCamera/test.json.", 72);
	addLabel(0x3D0C0722, "BUTTON CONTROLS", 108, cIGZFont::Style_Bold);

	cRZBaseString buttonText("Standard Button");
	addButton(0x3D0C0730, "standardButton", "Standard Button", 28, 136,
		manager->CreateButton(0x3D0C0730, buttonText, nullptr, nullptr, 0));
	cRZBaseString toggleText("Toggle Button");
	addButton(0x3D0C0731, "toggleButton", "Toggle Button", 250, 136,
		manager->CreateToggleButton(0x3D0C0731, toggleText, nullptr, nullptr, 0));
	cRZBaseString fixedText("Fixed Button");
	addButton(0x3D0C0732, "fixedButton", "Fixed Button", 28, 172,
		manager->CreateFixedButton(0x3D0C0732, fixedText, nullptr, nullptr, 0));
	cRZBaseString fixedToggleText("Fixed Toggle");
	addButton(0x3D0C0733, "fixedToggleButton", "Fixed Toggle", 250, 172,
		manager->CreateFixedToggleButton(0x3D0C0733, fixedToggleText, nullptr, nullptr, 0));
	cRZBaseString checkText("Checkbox");
	addButton(0x3D0C0734, "checkBox", "Checkbox", 28, 208,
		manager->CreateCheckBox(0x3D0C0734, checkText, nullptr, nullptr, 0));
	cRZBaseString radioCheckText("Radio Check");
	addButton(0x3D0C0735, "radioCheck", "Radio Check", 250, 208,
		manager->CreateRadioCheck(0x3D0C0735, radioCheckText, nullptr, nullptr, 0));
	cRZBaseString radioOneText("Radio: Choice A");
	addButton(0x3D0C0736, "radioChoiceA", "Radio: Choice A", 28, 244,
		manager->CreateRadioButton(0x3D0C0736, radioOneText, nullptr, nullptr, 0));
	cRZBaseString radioTwoText("Radio: Choice B");
	addButton(0x3D0C0737, "radioChoiceB", "Radio: Choice B", 250, 244,
		manager->CreateRadioButton(0x3D0C0737, radioTwoText, nullptr, nullptr, 0));

	addLabel(0x3D0C0740, "VALUE AND TEXT INPUTS", 292, cIGZFont::Style_Bold);

	auto addAdapterControl = [&](uint32_t id, const char* name, void* rawInterface,
		int32_t left, int32_t top, int32_t right, int32_t bottom, bool fixed = false)
	{
		Logger::GetInstance().WriteLine(LogLevel::Info, std::string("Test UI: adding ") + name + '.');
		ControlWindowAdapter* adapter = reinterpret_cast<ControlWindowAdapter*>(rawInterface);
		if (adapter)
		{
			AddControl(adapter->AsIGZWin(), adapter, id, name, left, top, right, bottom, fixed);
		}
	};

	addLabel(0x3D0C0741, "Horizontal Slider (25 / 100)", 322);
	addAdapterControl(0x3D0C0750, "horizontalSlider", manager->CreateSlider(0x3D0C0750, 0, 0, 100, 25),
		28, 348, 470, 374);
	addLabel(0x3D0C0742, "Horizontal Scrollbar (40 / 100)", 390);
	addAdapterControl(0x3D0C0751, "horizontalScrollbar", manager->CreateScrollbar(0x3D0C0751, 0, 0, 100, 40),
		28, 416, 470, 442);
	addLabel(0x3D0C0743, "Spinner (0-10, step 1, value 5)", 458);
	addAdapterControl(0x3D0C0752, "spinner", manager->CreateSpinner(0x3D0C0752, 0, 0, 10, 1, 5),
		28, 484, 230, 514);
	addLabel(0x3D0C0744, "Editable Line Input", 530);
	void* editableRaw = manager->CreateLineInput(0x3D0C0753, true);
	addAdapterControl(0x3D0C0753, "editableLineInput", editableRaw, 28, 556, 470, 586);
	if (cIGZWin* editable = rootWindow->GetChildWindowFromID(0x3D0C0753))
	{
		cRZBaseString initialText("Type here");
		editable->SetCaption(initialText);
	}
	addLabel(0x3D0C0745, "Read-only Line Input", 602);
	void* readOnlyRaw = manager->CreateLineInput(0x3D0C0754, false);
	addAdapterControl(0x3D0C0754, "readOnlyLineInput", readOnlyRaw, 28, 628, 470, 658);
	if (cIGZWin* readOnly = rootWindow->GetChildWindowFromID(0x3D0C0754))
	{
		cRZBaseString initialText("Read-only sample");
		readOnly->SetCaption(initialText);
	}
	cIGZBuffer* systemBitmap = manager->SystemBMP(0);
	if (systemBitmap)
	{
		addAdapterControl(0x3D0C0756, "bitmapControl",
			manager->CreateWinBMP(0x3D0C0756, systemBitmap, false), 330, 674, 470, 702);
		addLabel(0x3D0C0746, "Bitmap Control (system bitmap 0)", 674);
	}
	else
	{
		addLabel(0x3D0C0746, "Bitmap Control: system bitmap unavailable", 674);
	}
	cRZBaseString optionGroupText("Option Group Container");
	cRZRect optionGroupArea{};
	optionGroupArea.nX = 0;
	optionGroupArea.nY = 0;
	optionGroupArea.nWidth = 440;
	optionGroupArea.nHeight = 28;
	addAdapterControl(0x3D0C0755, "optionGroup",
		manager->CreateOptGrp(0x3D0C0755, optionGroupText, &optionGroupArea), 28, 708, 470, 736);

	addLabel(0x3D0C0760, "TEXT STYLE SAMPLES", 750, cIGZFont::Style_Bold);
	addLabel(0x3D0C0761, "Regular: The quick brown fox", 780);
	addLabel(0x3D0C0762, "Bold: The quick brown fox", 808, cIGZFont::Style_Bold);
	addLabel(0x3D0C0763, "Italic: The quick brown fox", 836, cIGZFont::Style_Italic);
	addLabel(0x3D0C0764, "Bold Italic: The quick brown fox", 864,
		cIGZFont::Style_Bold | cIGZFont::Style_Italic);
	addLabel(0x3D0C0765, "Underline: The quick brown fox", 892, cIGZFont::Style_Underline);
	addLabel(0x3D0C0766, "Strikethrough: The quick brown fox", 920, cIGZFont::Style_Strikethrough);
	addLabel(0x3D0C0767, "Shadow: The quick brown fox", 948, cIGZFont::Style_Shadow);

	addLabel(0x3D0C0770, "COLOR SAMPLES", 992, cIGZFont::Style_Bold);
	addLabel(0x3D0C0771, "Purple", 1022, 0, 0x800080);
	addLabel(0x3D0C0772, "Red", 1050, 0, 0xFF0000);
	addLabel(0x3D0C0773, "Green", 1078, 0, 0x008000);
	addLabel(0x3D0C0774, "Blue", 1106, 0, 0x0000FF);
	addLabel(0x3D0C0775, "Outlined text", 1134, 0, 0x203050);
	if (cIGZWin* outlinedWindow = rootWindow->GetChildWindowFromID(0x3D0C0775))
	{
		cRZAutoRefCount<cIGZWinText> outlined;
		if (outlinedWindow->QueryInterface(GZIID_cIGZWinText, outlined.AsPPVoid()))
		{
			outlined->SetOutlineUse(true, 0xFFFFFF);
		}
	}

	addLabel(0x3D0C0780, "END OF TEST CONTENT", 1180, cIGZFont::Style_Bold);

	cRZBaseString closeText("Close");
	addButton(kCloseButtonID, "close", "Close", 390, 10,
		manager->CreateFixedButton(kCloseButtonID, closeText, nullptr, nullptr, 0), true);
	cRZBaseString upText("Scroll Up");
	addButton(kScrollUpButtonID, "scrollUp", "Scroll Up", 390, 500,
		manager->CreateFixedButton(kScrollUpButtonID, upText, nullptr, nullptr, 0), true);
	cRZBaseString downText("Scroll Down");
	addButton(kScrollDownButtonID, "scrollDown", "Scroll Down", 390, 534,
		manager->CreateFixedButton(kScrollDownButtonID, downText, nullptr, nullptr, 0), true);
	addAdapterControl(kScrollBarID, "verticalScrollbar", manager->CreateScrollbar(kScrollBarID, 1, 0, 600, 0),
		520, 48, 548, 490, true);

	ApplyScrollPosition();
	return true;
}

void ControlLaboratoryWindow::ScrollBy(int32_t delta)
{
	scrollOffset = std::clamp(scrollOffset + delta, 0, 600);
	ApplyScrollPosition();
	SaveTestState();
	Logger::GetInstance().WriteLine(LogLevel::Info, "Test UI: scroll offset = " + std::to_string(scrollOffset));
}

void ControlLaboratoryWindow::ApplyScrollPosition()
{
	for (const ChildControl& control : controls)
	{
		if (control.fixed)
		{
			continue;
		}

		const int32_t top = control.baseTop - scrollOffset;
		const int32_t deltaX = control.baseLeft - control.window->GetL();
		const int32_t deltaY = top - control.window->GetT();
		if (deltaX != 0 || deltaY != 0)
		{
			// Despite its name, SC4's GZWinMoveTo applies a relative delta.
			// Computing the delta from the current position makes scrolling
			// deterministic and avoids the SDK's broken SetArea overloads.
			control.window->GZWinMoveTo(deltaX, deltaY);
		}
		// SC4 does not clip child windows to the dialog bounds. Only show
		// controls that fit completely inside the content viewport.
		const bool visible = top >= 108 && top + control.height <= 530;
		if (visible)
		{
			control.window->ShowWindow();
		}
		else
		{
			control.window->HideWindow();
		}
	}
	rootWindow->InvalidateSelf();
}

const ControlLaboratoryWindow::ChildControl* ControlLaboratoryWindow::FindControl(uint32_t id) const
{
	for (const ChildControl& control : controls)
	{
		if (control.id == id)
		{
			return &control;
		}
	}
	return nullptr;
}

void ControlLaboratoryWindow::RecordEvent(const cGZMessage& msg)
{
	const uint32_t controlID = msg.dwData2;
	const ChildControl* control = FindControl(controlID);
	// GZWinOptGrp creates anonymous child buttons for its options. Their button
	// notifications use control ID 0 immediately before the owning option-group
	// selection notification.
	const std::string name = control
		? control->name
		: (controlID == 0 ? "optionGroupInternalButton" : "unknownControl");

	EventState& state = eventStates[controlID];
	++state.count;
	state.messageType = msg.dwMessageType;
	state.data1 = msg.dwData1;
	state.data3 = msg.dwData3;

	if (rootWindow)
	{
		cIGZWin* child = rootWindow->GetChildWindowFromID(controlID);
		if (child && child->GetCaption())
		{
			state.caption = child->GetCaption()->ToChar();
		}

		cRZAutoRefCount<cIGZWinBtn> button;
		if (child && child->QueryInterface(GZIID_cIGZWinBtn, button.AsPPVoid()))
		{
			state.selected = button->IsOn() || button->IsChecked();
		}
	}

	std::ostringstream line;
	line << "Test UI event: " << name
		<< " id=0x" << std::hex << std::uppercase << controlID
		<< " message=0x" << msg.dwMessageType
		<< " data1=0x" << msg.dwData1
		<< " data3=0x" << msg.dwData3;
	Logger::GetInstance().WriteLine(LogLevel::Info, line.str());
	SaveTestState();
}

void ControlLaboratoryWindow::SaveTestState() const
{
	const std::filesystem::path path = PluginPaths::GetTestSettingsPath();
	const std::filesystem::path tempPath = path.wstring() + L".tmp";
	std::ofstream output(tempPath, std::ios::out | std::ios::trunc);
	if (!output)
	{
		Logger::GetInstance().WriteLine(LogLevel::Warning, "Test UI: failed to open test.json for writing.");
		return;
	}

	output << "{\n  \"scrollOffset\": " << scrollOffset << ",\n  \"controls\": {";
	bool first = true;
	for (const auto& [id, state] : eventStates)
	{
		const ChildControl* control = FindControl(id);
		if (!first)
		{
			output << ',';
		}
		first = false;
		const std::string name = control
			? control->name
			: (id == 0 ? "optionGroupInternalButton" : "unknownControl");
		output << "\n    \"" << EscapeJson(name) << "\": {"
			<< "\"id\": \"0x" << std::hex << std::uppercase << id << std::dec << "\", "
			<< "\"events\": " << state.count << ", "
			<< "\"messageType\": " << state.messageType << ", "
			<< "\"data1\": " << state.data1 << ", "
			<< "\"data3\": " << state.data3 << ", "
			<< "\"selected\": " << (state.selected ? "true" : "false") << ", "
			<< "\"caption\": \"" << EscapeJson(state.caption) << "\"}";
	}
	if (!first)
	{
		output << '\n';
	}
	output << "  }\n}\n";
	output.close();

	std::error_code error;
	std::filesystem::remove(path, error);
	error.clear();
	std::filesystem::rename(tempPath, path, error);
	if (error)
	{
		Logger::GetInstance().WriteLine(LogLevel::Warning, "Test UI: failed to replace test.json.");
	}
}

bool ControlLaboratoryWindow::DoWinProcMessage(cIGZWin*, cGZMessage& msg)
{
	if (FindControl(msg.dwData2))
	{
		RecordEvent(msg);
	}

	if (msg.dwMessageType != kMessageTypeCommand)
	{
		return false;
	}

	if (!FindControl(msg.dwData2))
	{
		RecordEvent(msg);
	}
	if (msg.dwData1 == kCommandButtonClicked)
	{
		if (msg.dwData2 == kCloseButtonID || msg.dwData2 == kCloseXButtonID)
		{
			rootWindow->HideWindow();
			return true;
		}
		if (msg.dwData2 == kScrollUpButtonID)
		{
			ScrollBy(-80);
			return true;
		}
		if (msg.dwData2 == kScrollDownButtonID)
		{
			ScrollBy(80);
			return true;
		}
	}
	else if (msg.dwData2 == kScrollBarID)
	{
		if (wheelScrollbarDirection != 0)
		{
			if (!wheelScrollbarNotificationHandled)
			{
				wheelScrollbarNotificationHandled = true;
				ScrollBy(wheelScrollbarDirection * 40);
			}
			return true;
		}

		cIGZWin* scrollbar = rootWindow->GetChildWindowFromID(kScrollBarID);
		POINT cursor{};
		HWND activeWindow = GetActiveWindow();
		if (scrollbar && activeWindow && GetCursorPos(&cursor) && ScreenToClient(activeWindow, &cursor))
		{
			const int32_t localY = cursor.y - rootWindow->GetT();
			const int32_t top = scrollbar->GetT();
			const int32_t bottom = scrollbar->GetB();
			constexpr int32_t arrowZone = 24;

			if (localY <= top + arrowZone)
			{
				ScrollBy(-40);
			}
			else if (localY >= bottom - arrowZone)
			{
				ScrollBy(40);
			}
			else
			{
				const int32_t trackTop = top + arrowZone;
				const int32_t trackHeight = std::max(1, bottom - top - (arrowZone * 2));
				const float ratio = std::clamp(
					static_cast<float>(localY - trackTop) / static_cast<float>(trackHeight),
					0.0f,
					1.0f);
				scrollOffset = static_cast<int32_t>(ratio * 600.0f);
				ApplyScrollPosition();
				SaveTestState();
			}
		}
		return true;
	}

	return false;
}

bool ControlLaboratoryWindow::DoWinMsg(cIGZWin* pWin, uint32_t messageID, uint32_t data1, uint32_t data2, uint32_t data3)
{
	return false;
}

void SC4WindowManager::OnCityLoaded(PluginSettings& settings)
{
	pendingSettings = &settings;
	menuButtonWindow.SetWindowManager(this);
	if (!menuButtonWindow.Create())
	{
		Logger::GetInstance().WriteLine(
			LogLevel::Warning,
			"Window Manager: failed to create the camera settings menu button.");
	}
	else
	{
		menuButtonWindow.SetVisible(true);
	}

	if (settings.NeedsVersionNotice())
	{
		if (versionNoticeTimerID != 0)
		{
			KillTimer(NULL, static_cast<UINT_PTR>(versionNoticeTimerID));
			versionNoticeTimerID = 0;
		}

		pendingVersionNoticeSettings = &settings;
		g_DelayedVersionNoticeManager = this;
		versionNoticeTimerID = static_cast<uintptr_t>(
			SetTimer(NULL, 0, kVersionNoticeDelayMs, DelayedVersionNoticeTimerProc));
		if (versionNoticeTimerID == 0)
		{
			Logger::GetInstance().WriteLine(
				LogLevel::Warning,
				"Window Manager: failed to start the delayed version notice timer; falling back to the native version notice.");
			const std::string title = std::string("SC4 Modern Camera ") + PluginVersion::String;
			const std::string message =
				std::string("SC4-ModernCamera v") + PluginVersion::String + " installed!\n\n"
				+ "Camera Options are available from the camera settings button in the upper right of the screen.\n\n"
				+ "Controls:\n"
				+ "WASD: Move Camera (optional)\n"
				+ "Scroll Wheel: Zoom\n"
				+ "Mouse 3 + Drag: Pan & Tilt\n\n"
				+ "Changelog:\n"
				+ "- Polished the welcome/changelog window and UI image handling.\n"
				+ "- Added persistent JSON settings.\n"
				+ "- Added version-aware first-install notice.\n"
				+ "- Added native SC4 window manager foundation.\n"
				+ "- Added optional WASD camera movement support groundwork.\n"
				+ "- Added plugin data folder: Plugins/SC4-ModernCamera/.";
			if (ShowNotification(title.c_str(), message.c_str()))
			{
				settings.AcknowledgeCurrentVersion();
			}
			pendingVersionNoticeSettings = nullptr;
			if (g_DelayedVersionNoticeManager == this)
			{
				g_DelayedVersionNoticeManager = nullptr;
			}
		}
		else
		{
			Logger::GetInstance().WriteLine(
				LogLevel::Info,
				"Window Manager: scheduled the version notice for 3 seconds after city load.");
		}
	}
}

void SC4WindowManager::SetCallbacks(SC4WindowManagerCallbacks managerCallbacks)
{
	callbacks = std::move(managerCallbacks);
}

void SC4WindowManager::OnVersionNoticeTimer()
{
	if (versionNoticeTimerID != 0)
	{
		KillTimer(NULL, static_cast<UINT_PTR>(versionNoticeTimerID));
		versionNoticeTimerID = 0;
	}

	PluginSettings* settings = pendingVersionNoticeSettings;
	pendingVersionNoticeSettings = nullptr;
	if (g_DelayedVersionNoticeManager == this)
	{
		g_DelayedVersionNoticeManager = nullptr;
	}

	if (settings)
	{
		Logger::GetInstance().WriteLine(
			LogLevel::Info,
			"Window Manager: delayed version notice timer fired.");
		ShowVersionNoticeIfNeeded(*settings);
	}
}

void SC4WindowManager::OnSettingsReopenTimer()
{
	if (settingsReopenTimerID != 0)
	{
		KillTimer(NULL, static_cast<UINT_PTR>(settingsReopenTimerID));
		settingsReopenTimerID = 0;
	}
	if (g_DelayedSettingsReopenManager == this)
	{
		g_DelayedSettingsReopenManager = nullptr;
	}

	settingsWindow.Destroy();
	ShowSettingsWindow();
}

void SC4WindowManager::OnDeferredWindowOpenTimer()
{
	if (deferredWindowOpenTimerID != 0)
	{
		KillTimer(NULL, static_cast<UINT_PTR>(deferredWindowOpenTimerID));
		deferredWindowOpenTimerID = 0;
	}
	if (g_DelayedWindowOpenManager == this)
	{
		g_DelayedWindowOpenManager = nullptr;
	}

	DeferredWindowOpen window = pendingDeferredWindowOpen;
	pendingDeferredWindowOpen = DeferredWindowOpen::None;

	switch (window)
	{
	case DeferredWindowOpen::AdvancedSettings:
		ShowAdvancedSettingsWindow();
		break;
	case DeferredWindowOpen::Changelog:
		greetingWindow.Destroy();
		if (ShowGreetingWindow())
		{
			settingsWindow.SendToBack();
			greetingWindow.BringToFront();
			greetingWindow.BringToFront();
		}
		break;
	default:
		break;
	}
}

SC4WindowHandle SC4WindowManager::CreateManagedWindow()
{
	return CreateManagedWindow(SC4BasicWindowOptions{});
}

SC4WindowHandle SC4WindowManager::CreateManagedWindow(const SC4BasicWindowOptions& options)
{
	auto window = std::make_unique<BasicManagedWindow>();
	if (!window->Create(options))
	{
		return InvalidSC4WindowHandle;
	}

	const SC4WindowHandle handle = nextWindowHandle++;
	basicWindows.push_back({ handle, std::move(window) });
	return handle;
}

SC4WindowHandle SC4WindowManager::CreateManagedWindow(SC4WindowTemplate windowTemplate)
{
	switch (windowTemplate)
	{
	case SC4WindowTemplate::ControlLaboratory:
		return ShowControlLaboratory() ? kControlLaboratoryHandle : InvalidSC4WindowHandle;
	default:
		return InvalidSC4WindowHandle;
	}
}

bool SC4WindowManager::CloseWindow(SC4WindowHandle handle)
{
	if (handle == kControlLaboratoryHandle)
	{
		controlLaboratory.Destroy();
		return true;
	}

	for (BasicWindowEntry& entry : basicWindows)
	{
		if (entry.handle == handle)
		{
			entry.window->Close();
			return true;
		}
	}
	return false;
}

void SC4WindowManager::OnCityShutdown()
{
	if (settingsReopenTimerID != 0)
	{
		KillTimer(NULL, static_cast<UINT_PTR>(settingsReopenTimerID));
		settingsReopenTimerID = 0;
	}
	if (versionNoticeTimerID != 0)
	{
		KillTimer(NULL, static_cast<UINT_PTR>(versionNoticeTimerID));
		versionNoticeTimerID = 0;
	}
	if (deferredWindowOpenTimerID != 0)
	{
		KillTimer(NULL, static_cast<UINT_PTR>(deferredWindowOpenTimerID));
		deferredWindowOpenTimerID = 0;
	}
	pendingDeferredWindowOpen = DeferredWindowOpen::None;
	pendingVersionNoticeSettings = nullptr;
	pendingSettings = nullptr;
	if (g_DelayedVersionNoticeManager == this)
	{
		g_DelayedVersionNoticeManager = nullptr;
	}
	if (g_DelayedSettingsReopenManager == this)
	{
		g_DelayedSettingsReopenManager = nullptr;
	}
	if (g_DelayedWindowOpenManager == this)
	{
		g_DelayedWindowOpenManager = nullptr;
	}
	CloseAllWindows();
}

bool SC4WindowManager::ShowNotification(const char* caption, const char* message) const
{
	if (!caption || !message || !SC4VersionDetection::IsDigitalDistributionVersion())
	{
		return false;
	}

	const auto createDialog = reinterpret_cast<CreateSC4NotificationDialog>(
		kCreateSC4NotificationDialogAddress);
	cRZBaseString captionString(caption);
	cRZBaseString messageString(message);
	// The native function's Boolean return is not a success indicator.
	createDialog(captionString, messageString);
	return true;
}

bool SC4WindowManager::ShowVersionNoticeIfNeeded(PluginSettings& settings)
{
	if (!settings.NeedsVersionNotice())
	{
		return true;
	}

	if (!ShowGreetingWindow())
	{
		Logger::GetInstance().WriteLine(
			LogLevel::Warning,
			"The baked greeting window could not be displayed.");
		return false;
	}

	if (!settings.AcknowledgeCurrentVersion())
	{
		Logger::GetInstance().WriteLine(
			LogLevel::Warning,
			"The version notice was shown, but its acknowledgement could not be saved.");
		return false;
	}
	return true;
}

bool SC4WindowManager::ShowControlLaboratory()
{
	return controlLaboratory.Create();
}

bool SC4WindowManager::ShowGreetingWindow()
{
	greetingWindow.SetWindowManager(this);
	return greetingWindow.Create();
}

bool SC4WindowManager::ShowControlsWindow()
{
	return controlsWindow.Create();
}

bool SC4WindowManager::ShowAdvancedSettingsWindow()
{
	advancedSettingsWindow.Destroy();
	advancedSettingsWindow.SetSettings(pendingSettings);
	advancedSettingsWindow.SetCallbacks(&callbacks);
	if (!advancedSettingsWindow.Create())
	{
		return false;
	}

	settingsWindow.SendToBack();
	advancedSettingsWindow.BringToFront();
	advancedSettingsWindow.BringToFront();
	return true;
}

bool SC4WindowManager::ShowSettingsWindow()
{
	settingsWindow.SetWindowManager(this);
	settingsWindow.SetSettings(pendingSettings);
	settingsWindow.SetCallbacks(&callbacks);
	if (!settingsWindow.Create())
	{
		menuButtonWindow.SetSettingsOpen(false);
		return false;
	}
	menuButtonWindow.SetSettingsOpen(true);
	return true;
}

bool SC4WindowManager::ToggleSettingsWindow()
{
	if (settingsWindow.IsVisible())
	{
		settingsWindow.Close();
		return false;
	}
	return ShowSettingsWindow();
}

void SC4WindowManager::SetMenuButtonVisible(bool visible)
{
	Logger::GetInstance().WriteLine(
		LogLevel::Info,
		std::string("Menu Button UI: ")
		+ (visible ? "showing" : "hiding")
		+ " camera settings button.");
	menuButtonWindow.SetVisible(visible);
}

void SC4WindowManager::OnSettingsWindowClosed()
{
	menuButtonWindow.SetSettingsOpen(false);
}

void SC4WindowManager::RefreshSettingsWindows()
{
	if (settingsWindow.IsVisible())
	{
		settingsWindow.RefreshFromSettings();
	}
	if (advancedSettingsWindow.IsVisible())
	{
		advancedSettingsWindow.RefreshFromSettings();
	}
}

void SC4WindowManager::ScheduleSettingsWindowReopen()
{
	if (settingsReopenTimerID != 0)
	{
		KillTimer(NULL, static_cast<UINT_PTR>(settingsReopenTimerID));
		settingsReopenTimerID = 0;
	}

	g_DelayedSettingsReopenManager = this;
	settingsReopenTimerID = static_cast<uintptr_t>(
		SetTimer(NULL, 0, kSettingsReopenDelayMs, DelayedSettingsReopenTimerProc));
	if (settingsReopenTimerID == 0)
	{
		Logger::GetInstance().WriteLine(
			LogLevel::Warning,
			"Settings UI: failed to schedule settings window refresh after defaults.");
	}
}

void SC4WindowManager::ScheduleDeferredWindowOpen(DeferredWindowOpen window)
{
	if (deferredWindowOpenTimerID != 0)
	{
		KillTimer(NULL, static_cast<UINT_PTR>(deferredWindowOpenTimerID));
		deferredWindowOpenTimerID = 0;
	}

	pendingDeferredWindowOpen = window;
	g_DelayedWindowOpenManager = this;
	deferredWindowOpenTimerID = static_cast<uintptr_t>(
		SetTimer(NULL, 0, kDeferredWindowOpenDelayMs, DeferredWindowOpenTimerProc));
	if (deferredWindowOpenTimerID == 0)
	{
		Logger::GetInstance().WriteLine(
			LogLevel::Warning,
			"Settings UI: failed to schedule child window open; opening immediately.");
		OnDeferredWindowOpenTimer();
	}
}

void SC4WindowManager::CloseAllWindows()
{
	settingsWindow.OnDelayedSettingsSaveTimer();
	settingsWindow.Destroy();
	advancedSettingsWindow.Destroy();
	controlsWindow.Destroy();
	greetingWindow.Destroy();
	controlLaboratory.Destroy();
	menuButtonWindow.Destroy();
	for (BasicWindowEntry& entry : basicWindows)
	{
		entry.window->Destroy();
	}
	basicWindows.clear();
}

bool SC4WindowManager::HasVisibleWindow() const
{
	if (controlsWindow.IsVisible())
	{
		return true;
	}
	if (greetingWindow.IsVisible())
	{
		return true;
	}
	if (controlLaboratory.IsVisible())
	{
		return true;
	}
	if (settingsWindow.IsVisible())
	{
		return true;
	}
	if (advancedSettingsWindow.IsVisible())
	{
		return true;
	}
	for (const BasicWindowEntry& entry : basicWindows)
	{
		if (entry.window->IsVisible())
		{
			return true;
		}
	}
	return false;
}

bool SC4WindowManager::IsPointOverVisibleWindow(int32_t parentX, int32_t parentY) const
{
	if (controlsWindow.ContainsPoint(parentX, parentY))
	{
		return true;
	}
	if (greetingWindow.ContainsPoint(parentX, parentY))
	{
		return true;
	}
	if (controlLaboratory.ContainsPoint(parentX, parentY))
	{
		return true;
	}
	if (settingsWindow.ContainsPoint(parentX, parentY))
	{
		return true;
	}
	if (advancedSettingsWindow.ContainsPoint(parentX, parentY))
	{
		return true;
	}
	for (const BasicWindowEntry& entry : basicWindows)
	{
		if (entry.window->ContainsPoint(parentX, parentY))
		{
			return true;
		}
	}
	return false;
}

bool SC4WindowManager::HandleMouseWheel(
	int32_t wheelDelta, int32_t parentX, int32_t parentY, intptr_t nativeWindowHandle)
{
	return controlLaboratory.HandleMouseWheel(
		wheelDelta, parentX, parentY, nativeWindowHandle);
}
