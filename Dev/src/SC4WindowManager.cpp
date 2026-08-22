#include "SC4WindowManager.h"

#include "GZServPtrs.h"
#include "Logger.h"
#include "PluginPaths.h"
#include "PluginSettings.h"
#include "PluginVersion.h"
#include "SC4GameConstants.h"
#include "SC4VersionDetection.h"
#include "cGZMessage.h"
#include "cGZPersistResourceKey.h"
#include "cIGZString.h"
#include "cIGZUIScriptService.h"
#include "cIGZWin.h"
#include "cIGZWinBtn.h"
#include "cIGZWinGen.h"
#include "cIGZWinSlider.h"
#include "cIGZWinText.h"
#include "cISC4App.h"
#include "cRZAutoRefCount.h"
#include "cRZBaseString.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace
{
	// Resource, class, and control IDs declared by the built-in UI DAT. An
	// external theme supplies the same window and control contract from its own
	// resource group.
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
	constexpr uint32_t kGreetingSuppressChangelogsButtonID = 0x3D0C0847;
	constexpr uint32_t kControlsCloseXButtonID = 0x3D0C0850;
	constexpr uint32_t kControlsOKButtonID = 0x3D0C0853;
	// The two menu controls occupy the same top-right button location. One is
	// shown for Modern mode and the other for Classic mode.
	constexpr uint32_t kMenuButtonID = 0x3D0C0910;
	// This transparent child control covers the lower-right mode light and its
	// backing rectangle, keeping its mouse states separate from the menu glyph.
	constexpr uint32_t kMenuModeToggleButtonID = 0x3D0C0911;
	// 0x3D0C0912 and 0x3D0C0913 were used by prototype text LED controls. Keep
	// those IDs retired.
	constexpr uint32_t kMenuClassicButtonID = 0x3D0C0914;
	// 0x3D0C0915 was used by the layered Classic mode lip. Keep it retired.
	// The lower-right LED and its visible backing rectangle are the menu's
	// camera-mode switch. The rest of the button opens Camera Settings.
	constexpr int32_t kMenuModeToggleLeft = 40;
	constexpr int32_t kMenuModeToggleTop = 46;
	constexpr int32_t kMenuModeToggleRight = 60;
	constexpr int32_t kMenuModeToggleBottom = 72;
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
	constexpr uint32_t kSettingsPanSliderID = 0x3D0C0939;
	constexpr uint32_t kSettingsQERotationOnButtonID = 0x3D0C093A;
	constexpr uint32_t kSettingsQERotationOffButtonID = 0x3D0C093B;
	constexpr uint32_t kSettingsRedrawOffButtonID = 0x3D0C0940;
	constexpr uint32_t kSettingsRedrawNormalButtonID = 0x3D0C0941;
	constexpr uint32_t kSettingsRedrawHighButtonID = 0x3D0C0942;
	constexpr uint32_t kSettingsRedrawAggressiveButtonID = 0x3D0C0943;
	constexpr uint32_t kSettingsRestoreDefaultsButtonID = 0x3D0C0950;
	constexpr uint32_t kSettingsAdvancedButtonID = 0x3D0C0951;
	constexpr uint32_t kSettingsReadChangelogButtonID = 0x3D0C0952;
	constexpr uint32_t kSettingsCloseButtonID = 0x3D0C0953;
	constexpr uint32_t kSettingsBookmarkHotkeysButtonID = 0x3D0C0954;
	constexpr uint32_t kAdvancedCloseXButtonID = 0x3D0C0960;
	constexpr uint32_t kAdvancedCloseButtonID = 0x3D0C0963;
	constexpr uint32_t kAdvancedDebugLoggingOffButtonID = 0x3D0C0968;
	constexpr uint32_t kAdvancedDebugLoggingNormalButtonID = 0x3D0C0969;
	constexpr uint32_t kAdvancedDebugLoggingVerboseButtonID = 0x3D0C096A;
	constexpr uint32_t kAdvancedCameraDumpHotkeyOnButtonID = 0x3D0C096C;
	constexpr uint32_t kAdvancedCameraDumpHotkeyOffButtonID = 0x3D0C096D;
	constexpr uint32_t kAdvancedShowChangelogsButtonID = 0x3D0C096E;
	// Fixed handle returned for the one control-laboratory window.
	constexpr SC4WindowHandle kControlLaboratoryHandle = 1;

	// Stores the selected external theme after its manifest passes the runtime
	// compatibility checks. The default state represents the built-in UI.
	struct ThemeState
	{
		// True only when the external resource group is valid and active.
		bool active = false;
		// Theme name used in diagnostics.
		std::string name;
		// Resource group that supplies the themed window scripts.
		uint32_t resourceGroup = kWindowResourceGroup;
		// Compatible minor version declared by the theme contract.
		uint32_t contractMinor = 0;
	};

	// Active theme selection for windows created during the current city.
	ThemeState g_Theme;

	// Formats a resource or control identifier for readable diagnostics.
	std::string FormatHexID(uint32_t id)
	{
		std::ostringstream stream;
		stream << "0x" << std::uppercase << std::hex << id;
		return stream.str();
	}

	// Reads a quoted manifest property without requiring a JSON dependency.
	bool ReadThemeManifestValue(
		const std::string& json,
		const char* name,
		std::string& value)
	{
		const std::regex expression(
			std::string("\"") + name + "\"\\s*:\\s*\"([^\"]+)\"",
			std::regex::icase);
		std::smatch match;
		if (!std::regex_search(json, match, expression))
		{
			return false;
		}
		value = match[1].str();
		return true;
	}

	// Reads resourceGroup from either the quoted or numeric manifest form.
	bool ReadThemeResourceGroup(const std::string& json, std::string& value)
	{
		if (ReadThemeManifestValue(json, "resourceGroup", value))
		{
			return true;
		}
		const std::regex expression(
			R"("resourceGroup"\s*:\s*([0-9]+))",
			std::regex::icase);
		std::smatch match;
		if (!std::regex_search(json, match, expression))
		{
			return false;
		}
		value = match[1].str();
		return true;
	}

	// Resets the current selection, then activates a compatible external theme
	// when theme.json names a non-built-in resource group. Failed validation
	// leaves the built-in UI selected.
	void LoadThemeManifest()
	{
		g_Theme = {};
		const std::filesystem::path path = PluginPaths::GetThemeManifestPath();
		std::ifstream stream(path, std::ios::binary);
		if (!stream)
		{
			return;
		}

		const std::string json(
			(std::istreambuf_iterator<char>(stream)),
			std::istreambuf_iterator<char>());
		std::string name;
		std::string contractVersion;
		std::string resourceGroup;
		if (!ReadThemeManifestValue(json, "name", name)
			|| !ReadThemeManifestValue(json, "contractVersion", contractVersion)
			|| !ReadThemeResourceGroup(json, resourceGroup))
		{
			Logger::GetInstance().WriteLine(
				LogLevel::Warning,
				"UI Theme: theme.json is missing name, contractVersion, or resourceGroup; using built-in UI.");
			return;
		}

		const std::regex versionExpression(R"(^\s*(\d+)\.(\d+)\s*$)");
		std::smatch versionMatch;
		if (!std::regex_match(contractVersion, versionMatch, versionExpression))
		{
			Logger::GetInstance().WriteLine(
				LogLevel::Warning,
				"UI Theme: incompatible contract version for '" + name
				+ "' (" + contractVersion + "); using built-in UI.");
			return;
		}

		try
		{
			const unsigned long contractMajor = std::stoul(versionMatch[1].str());
			const unsigned long contractMinor = std::stoul(versionMatch[2].str());
			if (contractMajor != 1 || contractMinor > UINT32_MAX)
			{
				Logger::GetInstance().WriteLine(
					LogLevel::Warning,
					"UI Theme: incompatible contract version for '" + name
					+ "' (" + contractVersion + "); using built-in UI.");
				return;
			}
			size_t parsed = 0;
			const unsigned long group = std::stoul(resourceGroup, &parsed, 0);
			if (parsed != resourceGroup.size()
				|| group > UINT32_MAX
				|| group == kWindowResourceGroup)
			{
				throw std::out_of_range("resource group");
			}
			g_Theme.active = true;
			g_Theme.name = name;
			g_Theme.resourceGroup = static_cast<uint32_t>(group);
			g_Theme.contractMinor = static_cast<uint32_t>(contractMinor);
			Logger::GetInstance().WriteLine(
				LogLevel::Info,
				"UI Theme: activated '" + name + "' contract " + contractVersion
				+ " resource group " + resourceGroup + ".");
		}
		catch (const std::exception&)
		{
			Logger::GetInstance().WriteLine(
				LogLevel::Warning,
				"UI Theme: invalid contractVersion or resourceGroup for '" + name
				+ "'; using built-in UI.");
		}
	}

	// Control IDs used by the authored control-laboratory resource.
	constexpr uint32_t kCloseButtonID = 0x3D0C0710;
	constexpr uint32_t kScrollUpButtonID = 0x3D0C0711;
	constexpr uint32_t kScrollDownButtonID = 0x3D0C0712;
	constexpr uint32_t kScrollBarID = 0x3D0C0713;
	constexpr uint32_t kCloseXButtonID = 0x3D0C0714;

	// GZ command-message type and notification values emitted by native
	// controls. Their numeric values are part of the SC4 UI protocol.
	constexpr uint32_t kMessageTypeCommand = 3;
	constexpr uint32_t kCommandButtonClicked = 0x287259F6;
	constexpr uint32_t kCommandValueChanged = 0x887113A3;
	// Native notification-dialog signature used by the supported executable.
	using CreateSC4NotificationDialog = bool(__cdecl*)(cIGZString const& caption, cIGZString const& message);
	// Native notification-dialog address used by that executable version.
	constexpr uintptr_t kCreateSC4NotificationDialogAddress = 0x78DD80;
	// Win32 timer delays. The one-millisecond timers defer window work until
	// after the current SC4 command dispatch completes.
	constexpr UINT kVersionNoticeDelayMs = 3000;
	constexpr UINT kSettingsSliderSaveDelayMs = 200;
	constexpr UINT kSettingsReopenDelayMs = 1;
	constexpr UINT kDeferredWindowOpenDelayMs = 1;
	// Owners of the currently armed one-shot timer callbacks.
	SC4WindowManager* g_DelayedVersionNoticeManager = nullptr;
	SettingsWindow* g_DelayedSettingsSaveWindow = nullptr;
	SC4WindowManager* g_DelayedSettingsReopenManager = nullptr;
	SC4WindowManager* g_DelayedWindowOpenManager = nullptr;

	// Forwards the version-notice timer to its owning window manager.
	VOID CALLBACK DelayedVersionNoticeTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
	{
		if (g_DelayedVersionNoticeManager)
		{
			g_DelayedVersionNoticeManager->OnVersionNoticeTimer();
		}
	}

	// Forwards the deferred settings-save timer to its owning settings window.
	VOID CALLBACK DelayedSettingsSaveTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
	{
		if (g_DelayedSettingsSaveWindow)
		{
			g_DelayedSettingsSaveWindow->OnDelayedSettingsSaveTimer();
		}
	}

	// Reopens the settings window after the current settings command finishes.
	VOID CALLBACK DelayedSettingsReopenTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
	{
		if (g_DelayedSettingsReopenManager)
		{
			g_DelayedSettingsReopenManager->OnSettingsReopenTimer();
		}
	}

	// Opens a secondary window after the originating button command finishes.
	VOID CALLBACK DeferredWindowOpenTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
	{
		if (g_DelayedWindowOpenManager)
		{
			g_DelayedWindowOpenManager->OnDeferredWindowOpenTimer();
		}
	}

	// Maps one primary settings slider to its setting, valid range, and logging
	// labels. This keeps the three sensitivity controls on one code path.
	struct SensitivitySlider
	{
		uint32_t controlID;
		float PluginSettings::* value;
		float minimum;
		float range;
		const char* optionName;
		const char* changeReason;
		const char* classicModeName;
	};

	// Metadata for rotation, zoom, and pan sliders in the primary settings UI.
	constexpr std::array kSensitivitySliders = {
		SensitivitySlider{
			kSettingsRotationSliderID,
			&PluginSettings::rotationSensitivity,
			PluginSettings::kMinimumSensitivity,
			PluginSettings::kSensitivityRange,
			"Rotation Sensitivity",
			"rotation sensitivity change",
			"rotation sensitivity" },
		SensitivitySlider{
			kSettingsZoomSliderID,
			&PluginSettings::zoomSensitivity,
			PluginSettings::kMinimumSensitivity,
			PluginSettings::kSensitivityRange,
			"Zoom Sensitivity",
			"zoom sensitivity change",
			"zoom sensitivity" },
		SensitivitySlider{
			kSettingsPanSliderID,
			&PluginSettings::panSensitivity,
			PluginSettings::kMinimumPanSensitivity,
			PluginSettings::kPanSensitivityRange,
			"Panning Speed",
			"panning speed change",
			"panning speed" },
	};

	// Returns slider metadata for a settings control, or null when it is not a
	// sensitivity slider.
	const SensitivitySlider* FindSensitivitySlider(uint32_t controlID)
	{
		for (const SensitivitySlider& slider : kSensitivitySliders)
		{
			if (slider.controlID == controlID)
			{
				return &slider;
			}
		}
		return nullptr;
	}

	// Implements the two COM-style interfaces exposed by each native window
	// procedure and increments the matching reference count on success.
	bool QueryWindowProcInterface(cIGZWinProc* windowProc, uint32_t riid, void** ppvObj)
	{
		if (!ppvObj)
		{
			return false;
		}
		if (riid == GZIID_cIGZWinProc)
		{
			*ppvObj = windowProc;
		}
		else if (riid == GZIID_cIGZUnknown)
		{
			*ppvObj = static_cast<cIGZUnknown*>(windowProc);
		}
		else
		{
			*ppvObj = nullptr;
			return false;
		}

		windowProc->AddRef();
		return true;
	}

	// Increments a non-owning native window-procedure reference count.
	uint32_t AddWindowProcRef(uint32_t& refCount)
	{
		return ++refCount;
	}

	// Decrements a non-owning native window-procedure reference count without
	// deleting the C++ wrapper.
	uint32_t ReleaseWindowProcRef(uint32_t& refCount)
	{
		if (refCount > 0)
		{
			--refCount;
		}
		return refCount;
	}

	// Stops an armed Win32 timer and clears the stored identifier.
	void CancelTimer(uintptr_t& timerID)
	{
		if (timerID != 0)
		{
			KillTimer(NULL, static_cast<UINT_PTR>(timerID));
			timerID = 0;
		}
	}

	// Escapes text for the small JSON state document written by the control
	// laboratory.
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

	// Attaches a window procedure when the native window exposes cIGZWinGen.
	void SetWinProc(cIGZWin* window, cIGZWinProc* winProc)
	{
		cRZAutoRefCount<cIGZWinGen> windowGen;
		if (window && window->QueryInterface(GZIID_cIGZWinGen, windowGen.AsPPVoid()))
		{
			windowGen->SetWinProc(winProc);
		}
	}

	// Updates text through cIGZWinText when available, otherwise through the
	// generic window caption API.
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

	// Updates a button caption and enables caption rendering for button controls.
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

	// Converts a boolean setting to the label used in UI diagnostics.
	const char* ToOnOffString(bool value)
	{
		return value ? "On" : "Off";
	}

	// Formats a sensitivity value with stable two-decimal precision.
	std::string FormatSettingFloat(float value)
	{
		std::ostringstream stream;
		stream << std::fixed << std::setprecision(2) << value;
		return stream.str();
	}

	// Maps a clamped setting value to the native slider's centesimal range.
	int32_t SliderValueFromSetting(float value, float minimum, float range)
	{
		const float clamped = std::clamp(value, minimum, minimum + range);
		return static_cast<int32_t>((clamped * 100.0f) + 0.5f);
	}

	// Finds a slider in a baked window and applies the corresponding float
	// setting without relying on a notification round trip.
	void SetSliderValueFromSetting(cIGZWin* root, uint32_t sliderID, float value, float minimum, float range)
	{
		cRZAutoRefCount<cIGZWinSlider> slider;
		if (root && root->GetChildAsRecursive(sliderID, GZIID_cIGZWinSlider, slider.AsPPVoid()))
		{
			slider->SetValue(SliderValueFromSetting(value, minimum, range));
		}
	}

	// Logs one settings transition only when the old and new values differ.
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

	// Toggles one button in a mutually exclusive baked-control group.
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

	// Applies one independent on/off state to a toggle-capable baked button.
	void SetButtonToggle(cIGZWin* root, uint32_t id, bool enabled)
	{
		const uint32_t ids[2] = { id, 0 };
		ApplyButtonChoice(root, enabled ? id : 0, ids, 1);
	}

	// Enables or disables one baked control and invalidates it for redraw.
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

	// Shows or hides one baked control and invalidates its parent hierarchy.
	void SetControlVisible(cIGZWin* root, uint32_t id, bool visible)
	{
		cIGZWin* child = root ? root->GetChildWindowFromIDRecursive(id) : nullptr;
		if (!child)
		{
			return;
		}

		if (visible)
		{
			child->ShowWindow();
		}
		else
		{
			child->HideWindow();
		}
		child->InvalidateSelfAndParents();
	}

	// Enables or disables every control in a supplied baked-control group.
	void SetControlsEnabled(cIGZWin* root, const uint32_t* ids, size_t count, bool enabled)
	{
		for (size_t i = 0; i < count; ++i)
		{
			SetControlEnabled(root, ids[i], enabled);
		}
	}

	// Confirms that an external themed resource exposes every required control
	// with the expected native type before it is allowed to replace the built-in
	// window.
	bool ValidateThemeControls(
		cIGZWin* root,
		uint32_t resourceInstanceID,
		std::string& failure)
	{
		std::vector<uint32_t> buttons;
		std::vector<uint32_t> sliders;
		switch (resourceInstanceID)
		{
		case kGreetingWindowResourceInstance:
			buttons = {
				kGreetingCloseXButtonID,
				kGreetingOKButtonID,
				kGreetingViewControlsButtonID,
				kGreetingSuppressChangelogsButtonID,
			};
			break;
		case kControlsWindowResourceInstance:
			buttons = { kControlsCloseXButtonID, kControlsOKButtonID };
			break;
		case kMenuButtonResourceInstance:
			buttons = {
				kMenuButtonID,
				kMenuClassicButtonID,
			};
			break;
		case kSettingsWindowResourceInstance:
			buttons = {
				kSettingsCloseXButtonID,
				kSettingsModernButtonID,
				kSettingsClassicButtonID,
				kSettingsWASDOnButtonID,
				kSettingsWASDOffButtonID,
				kSettingsQERotationOnButtonID,
				kSettingsQERotationOffButtonID,
				kSettingsInvertOnButtonID,
				kSettingsInvertOffButtonID,
				kSettingsResetCameraButtonID,
				kSettingsRestoreDefaultsButtonID,
				kSettingsAdvancedButtonID,
				kSettingsReadChangelogButtonID,
				kSettingsCloseButtonID,
				kSettingsBookmarkHotkeysButtonID,
			};
			sliders = {
				kSettingsRotationSliderID,
				kSettingsZoomSliderID,
				kSettingsPanSliderID,
			};
			break;
		case kAdvancedWindowResourceInstance:
			buttons = {
				kAdvancedCloseXButtonID,
				kAdvancedCloseButtonID,
				kSettingsRedrawOffButtonID,
				kSettingsRedrawNormalButtonID,
				kSettingsRedrawHighButtonID,
				kSettingsRedrawAggressiveButtonID,
				kAdvancedDebugLoggingOffButtonID,
				kAdvancedDebugLoggingNormalButtonID,
				kAdvancedDebugLoggingVerboseButtonID,
				kAdvancedCameraDumpHotkeyOnButtonID,
				kAdvancedCameraDumpHotkeyOffButtonID,
				kAdvancedShowChangelogsButtonID,
			};
			break;
		default:
			return true;
		}

		for (uint32_t id : buttons)
		{
			cIGZWin* child = root ? root->GetChildWindowFromIDRecursive(id) : nullptr;
			cRZAutoRefCount<cIGZWinBtn> button;
			if (!child || !child->QueryInterface(GZIID_cIGZWinBtn, button.AsPPVoid()))
			{
				failure = "required button " + FormatHexID(id) + " is missing or has the wrong type";
				return false;
			}
		}
		for (uint32_t id : sliders)
		{
			cIGZWin* child = root ? root->GetChildWindowFromIDRecursive(id) : nullptr;
			cRZAutoRefCount<cIGZWinSlider> slider;
			if (!child || !child->QueryInterface(GZIID_cIGZWinSlider, slider.AsPPVoid()))
			{
				failure = "required slider " + FormatHexID(id) + " is missing or has the wrong type";
				return false;
			}
		}
		return true;
	}

	// Returns whether a control ID selects one redraw-aggression option.
	bool IsRedrawAggressionControl(uint32_t controlID)
	{
		return controlID == kSettingsRedrawOffButtonID
			|| controlID == kSettingsRedrawNormalButtonID
			|| controlID == kSettingsRedrawHighButtonID
			|| controlID == kSettingsRedrawAggressiveButtonID;
	}

	// Returns whether a visible window contains a parent-window coordinate.
	bool WindowContainsParentPoint(cIGZWin* window, int32_t parentX, int32_t parentY)
	{
		return window
			&& window->IsVisible()
			&& parentX >= window->GetL()
			&& parentX < window->GetR()
			&& parentY >= window->GetT()
			&& parentY < window->GetB();
	}

	// Returns whether a parent-window point lands on the compact menu's
	// lower-right camera-mode indicator.
	bool IsMenuModeTogglePoint(cIGZWin* window, int32_t parentX, int32_t parentY)
	{
		if (!window)
		{
			return false;
		}

		const int32_t localX = parentX - window->GetL();
		const int32_t localY = parentY - window->GetT();
		return localX >= kMenuModeToggleLeft
			&& localX < kMenuModeToggleRight
			&& localY >= kMenuModeToggleTop
			&& localY < kMenuModeToggleBottom;
	}
}

// Initializes an empty native-window wrapper with no retained interfaces.
BasicManagedWindow::BasicManagedWindow()
	: refCount(0), parentWindow(nullptr), rootWindow(nullptr)
{
}

// Releases native resources if its owner destroys this wrapper while it is
// still open.
BasicManagedWindow::~BasicManagedWindow()
{
	Destroy();
}

// Resolves the COM-style window-procedure interface implemented by this
// wrapper.
bool BasicManagedWindow::QueryInterface(uint32_t riid, void** ppvObj)
{
	return QueryWindowProcInterface(this, riid, ppvObj);
}

// Increments the reference count expected by the native window manager.
uint32_t BasicManagedWindow::AddRef()
{
	return AddWindowProcRef(refCount);
}

// Decrements the native reference count without transferring C++ ownership.
uint32_t BasicManagedWindow::Release()
{
	return ReleaseWindowProcRef(refCount);
}

// Creates the basic window template, configures its requested content, and
// centers it in the main application window.
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
	parentWindow = app->GetMainWindow()->GetChildWindowFromID(SC4WindowID::MainApplication);
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

// Detaches the native procedure and releases retained root and parent
// interfaces.
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

// Hides the basic window without releasing its native resource.
void BasicManagedWindow::Close()
{
	if (rootWindow)
	{
		rootWindow->HideWindow();
	}
}

// Reports whether the basic window remains visible.
bool BasicManagedWindow::IsVisible() const
{
	return rootWindow && rootWindow->IsVisible();
}

// Tests a parent-window coordinate against the visible basic-window bounds.
bool BasicManagedWindow::ContainsPoint(int32_t parentX, int32_t parentY) const
{
	return WindowContainsParentPoint(rootWindow, parentX, parentY);
}

// Handles the close commands emitted by the basic window's standard controls.
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

// Leaves low-level GZ messages for the native basic-window path.
bool BasicManagedWindow::DoWinMsg(cIGZWin*, uint32_t, uint32_t, uint32_t, uint32_t)
{
	return false;
}

// Retains the resource and standard control identifiers needed to open one
// baked window.
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

// Releases the baked resource if its owner destroys the wrapper while open.
BakedManagedWindow::~BakedManagedWindow()
{
	Destroy();
}

// Resolves the COM-style window-procedure interface implemented by this baked
// wrapper.
bool BakedManagedWindow::QueryInterface(uint32_t riid, void** ppvObj)
{
	return QueryWindowProcInterface(this, riid, ppvObj);
}

// Increments the reference count expected by the native window manager.
uint32_t BakedManagedWindow::AddRef()
{
	return AddWindowProcRef(refCount);
}

// Decrements the native reference count without transferring C++ ownership.
uint32_t BakedManagedWindow::Release()
{
	return ReleaseWindowProcRef(refCount);
}

// Attempts the selected external theme first and validates its resource,
// falling back to the built-in UI DAT when that attempt fails. It then attaches
// this procedure, positions the root, and invokes OnCreated().
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

	parentWindow = app->GetMainWindow()->GetChildWindowFromID(SC4WindowID::MainApplication);
	if (!parentWindow)
	{
		Logger::GetInstance().WriteLine(
			LogLevel::Error,
			std::string(logName) + ": SC4 application window was unavailable.");
		return false;
	}
	parentWindow->AddRef();

	cIGZUIScriptServicePtr scriptService;
	auto createFromGroup = [&](uint32_t group) {
		const cGZPersistResourceKey key(0x00000000, group, resourceInstanceID);
		return scriptService
			&& scriptService->CreateWindowFromScript(
				key,
				parentWindow,
				rootCLSID,
				GZIID_cIGZWin,
				reinterpret_cast<void**>(&rootWindow))
			&& rootWindow;
	};

	bool created = false;
	if (g_Theme.active)
	{
		created = createFromGroup(g_Theme.resourceGroup);
		std::string validationFailure;
		if (created && !ValidateThemeControls(rootWindow, resourceInstanceID, validationFailure))
		{
			created = false;
		}
		if (!created)
		{
			Logger::GetInstance().WriteLine(
				LogLevel::Warning,
				std::string("UI Theme: '") + g_Theme.name + "' failed for " + logName
				+ (validationFailure.empty() ? "." : ": " + validationFailure + ".")
				+ " Reopening the built-in window.");
			if (rootWindow)
			{
				rootWindow->Release();
				rootWindow = nullptr;
			}
		}
	}
	if (!created)
	{
		created = createFromGroup(kWindowResourceGroup);
	}
	if (!created)
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

// Detaches the native procedure and releases retained root and parent
// interfaces for this baked window.
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

// Hides the baked root window and gives subclasses a close notification.
void BakedManagedWindow::Close()
{
	if (rootWindow)
	{
		rootWindow->HideWindow();
		OnClosed();
	}
}

// Raises the baked root window above its sibling windows.
void BakedManagedWindow::BringToFront()
{
	if (rootWindow)
	{
		rootWindow->PullToFront();
		rootWindow->InvalidateSelfAndParents();
	}
}

// Lowers the baked root window behind its sibling windows.
void BakedManagedWindow::SendToBack()
{
	if (rootWindow)
	{
		rootWindow->SendToBack();
	}
}

// Changes baked-root visibility without releasing its native resource.
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

// Reports whether the baked root window remains visible.
bool BakedManagedWindow::IsVisible() const
{
	return rootWindow && rootWindow->IsVisible();
}

// Tests a parent-window coordinate against the visible baked-window bounds.
bool BakedManagedWindow::ContainsPoint(int32_t parentX, int32_t parentY) const
{
	return WindowContainsParentPoint(rootWindow, parentX, parentY);
}

// Routes GZ command messages to the subclass command handler.
bool BakedManagedWindow::DoWinProcMessage(cIGZWin*, cGZMessage& msg)
{
	if (msg.dwMessageType == kMessageTypeCommand)
	{
		return OnCommandMessage(msg);
	}
	return false;
}

// Leaves low-level GZ messages for the native baked-window path.
bool BakedManagedWindow::DoWinMsg(cIGZWin*, uint32_t, uint32_t, uint32_t, uint32_t)
{
	return false;
}

// Provides a no-op setup hook for subclasses that have no creation work.
void BakedManagedWindow::OnCreated()
{
}

// Handles standard button commands and lets subclasses override the rest.
bool BakedManagedWindow::OnCommandMessage(cGZMessage& msg)
{
	if (msg.dwData1 == kCommandButtonClicked)
	{
		return OnButtonClick(msg.dwData2);
	}
	return false;
}

// Closes the baked window for its configured close and confirmation controls.
bool BakedManagedWindow::OnButtonClick(uint32_t controlID)
{
	if (controlID == closeXButtonID || controlID == okButtonID)
	{
		Close();
		return true;
	}
	return false;
}

// Provides a no-op close hook for subclasses that have no close work.
void BakedManagedWindow::OnClosed()
{
}

// Returns the baked root window without changing its reference count.
cIGZWin* BakedManagedWindow::GetRootWindow() const
{
	return rootWindow;
}

// Configures the baked resource, standard controls, and deferred-save state
// for the primary settings window.
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
	syncingControls(false),
	delayedSaveReason()
{
}

// Retains the manager used for coordinated settings-window transitions.
void SettingsWindow::SetWindowManager(SC4WindowManager* windowManager)
{
	manager = windowManager;
}

// Retains the mutable settings object mirrored by the primary controls.
void SettingsWindow::SetSettings(PluginSettings* pluginSettings)
{
	settings = pluginSettings;
}

// Retains runtime callbacks invoked after a primary-setting transition.
void SettingsWindow::SetCallbacks(const SC4WindowManagerCallbacks* managerCallbacks)
{
	callbacks = managerCallbacks;
}

// Copies the current settings into an already-created baked settings window.
void SettingsWindow::RefreshFromSettings()
{
	SyncControlsFromSettings();
}

// Synchronizes controls as soon as the baked settings resource is available.
void SettingsWindow::OnCreated()
{
	SyncControlsFromSettings();
}

// Applies one selected button to a mutually exclusive primary-control group.
void SettingsWindow::ApplyChoice(uint32_t selectedID, const uint32_t* ids, size_t count)
{
	ApplyButtonChoice(GetRootWindow(), selectedID, ids, count);
}

// Maps a boolean setting to its paired On and Off controls.
void SettingsWindow::ApplyBooleanPair(uint32_t onID, uint32_t offID, bool value)
{
	const uint32_t ids[2] = { onID, offID };
	ApplyChoice(value ? onID : offID, ids, 2);
}

// Copies all primary settings into their baked controls and disables controls
// that have no effect while Classic camera mode is selected.
void SettingsWindow::SyncControlsFromSettings()
{
	if (!settings)
	{
		return;
	}

	const uint32_t cameraModeIDs[2] = { kSettingsModernButtonID, kSettingsClassicButtonID };
	syncingControls = true;
	ApplyChoice(
		settings->cameraMode == CameraMode::Modern ? kSettingsModernButtonID : kSettingsClassicButtonID,
		cameraModeIDs,
		2);
	ApplyBooleanPair(kSettingsWASDOnButtonID, kSettingsWASDOffButtonID, settings->wasdMovement);
	ApplyBooleanPair(
		kSettingsQERotationOnButtonID,
		kSettingsQERotationOffButtonID,
		settings->qeRotation);
	SetSliderValueFromSetting(
		GetRootWindow(),
		kSettingsRotationSliderID,
		settings->rotationSensitivity,
		PluginSettings::kMinimumSensitivity,
		PluginSettings::kSensitivityRange);
	SetSliderValueFromSetting(
		GetRootWindow(),
		kSettingsZoomSliderID,
		settings->zoomSensitivity,
		PluginSettings::kMinimumSensitivity,
		PluginSettings::kSensitivityRange);
	SetSliderValueFromSetting(
		GetRootWindow(),
		kSettingsPanSliderID,
		settings->panSensitivity,
		PluginSettings::kMinimumPanSensitivity,
		PluginSettings::kPanSensitivityRange);
	ApplyBooleanPair(kSettingsInvertOnButtonID, kSettingsInvertOffButtonID, settings->invertVertical);
	SetButtonToggle(GetRootWindow(), kSettingsBookmarkHotkeysButtonID, settings->bookmarksEnabled);

	const bool modernCameraActive = settings->cameraMode == CameraMode::Modern;
	const uint32_t modernOnlyControlIDs[] = {
		kSettingsWASDOnButtonID,
		kSettingsWASDOffButtonID,
		kSettingsQERotationOnButtonID,
		kSettingsQERotationOffButtonID,
		kSettingsRotationSliderID,
		kSettingsZoomSliderID,
		kSettingsPanSliderID,
		kSettingsInvertOnButtonID,
		kSettingsInvertOffButtonID,
	};
	SetControlsEnabled(
		GetRootWindow(),
		modernOnlyControlIDs,
		sizeof(modernOnlyControlIDs) / sizeof(modernOnlyControlIDs[0]),
		modernCameraActive);
	syncingControls = false;
}

// Logs the current state, then saves it immediately or debounces the write for
// slider changes. In either case the controls are resynchronized afterward.
void SettingsWindow::ApplySettingsChange(const char* reason, bool saveImmediately)
{
	if (!settings)
	{
		return;
	}
	Logger::GetInstance().WriteLine(
		LogLevel::Info,
		"Settings UI state: mode="
		+ std::string(PluginSettingText::ToString(settings->cameraMode))
		+ " wasd=" + (settings->wasdMovement ? "true" : "false")
		+ " qeRotation=" + (settings->qeRotation ? "true" : "false")
		+ " rotationSensitivity=" + std::to_string(settings->rotationSensitivity)
		+ " zoomSensitivity=" + std::to_string(settings->zoomSensitivity)
		+ " panSensitivity=" + std::to_string(settings->panSensitivity)
		+ " invertVertical=" + (settings->invertVertical ? "true" : "false")
		+ " bookmarksEnabled=" + (settings->bookmarksEnabled ? "true" : "false")
		+ " redrawAggression="
		+ PluginSettingText::ToString(settings->redrawAggression)
		+ " debugLogging="
		+ PluginSettingText::ToString(settings->debugLogging));

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

// Debounces a settings write so continuous slider movement does not write on
// every notification.
void SettingsWindow::ScheduleSettingsSave(const char* reason)
{
	delayedSavePending = true;
	delayedSaveReason = reason ? reason : "change";
	CancelTimer(delayedSaveTimerID);

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

// Completes the pending debounced save, if this window owns one.
void SettingsWindow::FlushPendingSettingsSave()
{
	CancelTimer(delayedSaveTimerID);
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
			+ " panSensitivity="
			+ FormatSettingFloat(settings->panSensitivity)
			+ ".");
	}
}

// Consumes the delayed settings-save timer for this window.
void SettingsWindow::OnDelayedSettingsSaveTimer()
{
	FlushPendingSettingsSave();
}

// Converts the cursor's horizontal position over a slider into a bounded
// setting value. The existing setting is returned when cursor coordinates are
// unavailable.
float SettingsWindow::ReadSliderValueFromCursor(uint32_t sliderID, float fallback, float minimum, float range) const
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
	return minimum + (ratio * range);
}

// Applies the value-change notification for any registered sensitivity slider.
// The shared metadata preserves each slider's setting range and diagnostics.
bool SettingsWindow::ApplySensitivitySliderChange(uint32_t controlID)
{
	const SensitivitySlider* slider = FindSensitivitySlider(controlID);
	if (!slider)
	{
		return false;
	}
	if (syncingControls)
	{
		return true;
	}
	if (settings->cameraMode != CameraMode::Modern)
	{
		Logger::GetInstance().WriteLine(
			LogLevel::Verbose,
			std::string("Settings UI: ignored ")
			+ slider->classicModeName
			+ " slider while Classic camera is selected.");
		return true;
	}

	float& value = settings->*(slider->value);
	const float previous = value;
	value = ReadSliderValueFromCursor(
		slider->controlID,
		value,
		slider->minimum,
		slider->range);
	LogOptionChanged(
		"Settings UI",
		slider->optionName,
		FormatSettingFloat(previous),
		FormatSettingFloat(value),
		LogLevel::Verbose);
	ApplySettingsChange(slider->changeReason, false);
	return true;
}

// Handles slider value changes before delegating button commands to the baked
// window implementation.
bool SettingsWindow::OnCommandMessage(cGZMessage& msg)
{
	if (!settings)
	{
		return BakedManagedWindow::OnCommandMessage(msg);
	}

	if (msg.dwData1 == kCommandValueChanged
		&& ApplySensitivitySliderChange(msg.dwData2))
	{
		return true;
	}

	return BakedManagedWindow::OnCommandMessage(msg);
}

// Sets the requested camera mode, persists the change, refreshes every
// dependent window, then notifies the runtime after the controls agree with
// the new mode.
void SettingsWindow::ApplyCameraMode(CameraMode mode)
{
	const CameraMode previous = settings->cameraMode;
	settings->cameraMode = mode;
	LogOptionChanged(
		"Settings UI",
		"Camera Mode",
		PluginSettingText::ToString(previous),
		PluginSettingText::ToString(settings->cameraMode));
	ApplySettingsChange("camera mode change");
	if (manager)
	{
		manager->RefreshSettingsWindows();
	}
	if (callbacks && callbacks->OnCameraModeChanged)
	{
		callbacks->OnCameraModeChanged(mode == CameraMode::Modern);
	}
}

// Applies primary settings controls, including Modern-only WASD, Q/E rotation,
// sensitivity, and inversion options. Unrecognized controls use the standard
// baked close handling.
bool SettingsWindow::OnButtonClick(uint32_t controlID)
{
	if (controlID == kSettingsCloseXButtonID || controlID == kSettingsCloseButtonID)
	{
		Close();
		return true;
	}

	if (settings)
	{
		const bool modernCameraActive = settings->cameraMode == CameraMode::Modern;
		switch (controlID)
		{
		case kSettingsModernButtonID:
			ApplyCameraMode(CameraMode::Modern);
			return true;
		case kSettingsClassicButtonID:
			ApplyCameraMode(CameraMode::Classic);
			return true;
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
		case kSettingsQERotationOnButtonID:
			if (!modernCameraActive)
			{
				SyncControlsFromSettings();
				return true;
			}
			LogOptionChanged("Settings UI", "Q/E Rotation", ToOnOffString(settings->qeRotation), "On");
			settings->qeRotation = true;
			ApplySettingsChange("Q/E rotation change");
			if (callbacks && callbacks->OnInputSettingsChanged)
			{
				callbacks->OnInputSettingsChanged();
			}
			return true;
		case kSettingsQERotationOffButtonID:
			if (!modernCameraActive)
			{
				SyncControlsFromSettings();
				return true;
			}
			LogOptionChanged("Settings UI", "Q/E Rotation", ToOnOffString(settings->qeRotation), "Off");
			settings->qeRotation = false;
			ApplySettingsChange("Q/E rotation change");
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
		case kSettingsBookmarkHotkeysButtonID:
			LogOptionChanged(
				"Settings UI",
				"Bookmark Hotkeys",
				ToOnOffString(settings->bookmarksEnabled),
				ToOnOffString(!settings->bookmarksEnabled));
			settings->bookmarksEnabled = !settings->bookmarksEnabled;
			ApplySettingsChange("bookmark hotkey change");
			return true;
		case kSettingsRestoreDefaultsButtonID:
		{
			const CameraMode previousCameraMode = settings->cameraMode;
			const bool previousWASD = settings->wasdMovement;
			const bool previousQERotation = settings->qeRotation;
			const float previousRotationSensitivity = settings->rotationSensitivity;
			const float previousZoomSensitivity = settings->zoomSensitivity;
			const float previousPanSensitivity = settings->panSensitivity;
			const bool previousInvertVertical = settings->invertVertical;
			const RedrawAggression previousRedrawAggression = settings->redrawAggression;
			const DebugLogging previousDebugLogging = settings->debugLogging;
			const bool previousCameraDumpHotkey = settings->cameraDumpHotkey;
			const bool previousBookmarksEnabled = settings->bookmarksEnabled;
			const bool previousShowChangelogs = settings->showChangelogsOnUpdate;
			settings->RestoreDefaults();
			LogOptionChanged(
				"Settings UI",
				"Camera Mode",
				PluginSettingText::ToString(previousCameraMode),
				PluginSettingText::ToString(settings->cameraMode));
			LogOptionChanged(
				"Settings UI",
				"WASD Movement",
				ToOnOffString(previousWASD),
				ToOnOffString(settings->wasdMovement));
			LogOptionChanged(
				"Settings UI",
				"Q/E Rotation",
				ToOnOffString(previousQERotation),
				ToOnOffString(settings->qeRotation));
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
				"Panning Speed",
				FormatSettingFloat(previousPanSensitivity),
				FormatSettingFloat(settings->panSensitivity));
			LogOptionChanged(
				"Settings UI",
				"Invert Vertical",
				ToOnOffString(previousInvertVertical),
				ToOnOffString(settings->invertVertical));
			LogOptionChanged(
				"Settings UI",
				"Redraw Aggression",
				PluginSettingText::ToString(previousRedrawAggression),
				PluginSettingText::ToString(settings->redrawAggression));
			LogOptionChanged(
				"Settings UI",
				"Diagnostics Logging",
				PluginSettingText::ToString(previousDebugLogging),
				PluginSettingText::ToString(settings->debugLogging));
			LogOptionChanged(
				"Settings UI",
				"Camera Dump Hotkey",
				ToOnOffString(previousCameraDumpHotkey),
				ToOnOffString(settings->cameraDumpHotkey));
			LogOptionChanged(
				"Settings UI",
				"Bookmarks Enabled",
				ToOnOffString(previousBookmarksEnabled),
				ToOnOffString(settings->bookmarksEnabled));
			LogOptionChanged(
				"Settings UI",
				"Show Changelogs on Update",
				ToOnOffString(previousShowChangelogs),
				ToOnOffString(settings->showChangelogsOnUpdate));
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
			if (callbacks && callbacks->OnDebugLoggingChanged)
			{
				callbacks->OnDebugLoggingChanged();
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

// Flushes a pending save and tells the manager that the primary window closed.
void SettingsWindow::OnClosed()
{
	FlushPendingSettingsSave();
	if (manager)
	{
		manager->OnSettingsWindowClosed();
	}
}

// Configures the compact top-right menu resource that opens settings and
// exposes the camera-mode switch indicator.
MenuButtonWindow::MenuButtonWindow()
	: BakedManagedWindow(
		"Menu Button UI",
		kMenuButtonResourceInstance,
		kMenuButtonCLSID,
		0,
		0,
		Placement::TopRightButton),
	manager(nullptr),
	settings(nullptr)
{
}

// Retains the manager that owns settings-window and mode-switch actions.
void MenuButtonWindow::SetWindowManager(SC4WindowManager* windowManager)
{
	manager = windowManager;
}

// Retains the settings used to choose the visible Modern or Classic artwork.
void MenuButtonWindow::SetSettings(PluginSettings* pluginSettings)
{
	settings = pluginSettings;
}

// Selects the correct menu artwork as soon as the baked resource is created.
void MenuButtonWindow::OnCreated()
{
	RefreshMode();
}

// Shows the Modern or Classic menu control according to the active camera
// mode, then refreshes cursor feedback for the visible control.
void MenuButtonWindow::RefreshMode()
{
	const bool modern = settings && settings->cameraMode == CameraMode::Modern;
	if (modern)
	{
		SetControlVisible(GetRootWindow(), kMenuClassicButtonID, false);
		SetControlVisible(GetRootWindow(), kMenuButtonID, true);
	}
	else
	{
		SetControlVisible(GetRootWindow(), kMenuButtonID, false);
		SetControlVisible(GetRootWindow(), kMenuClassicButtonID, true);
	}
	RefreshCursor();
}

// Re-evaluates the current cursor so the active menu control can supply its
// native hover cursor.
void MenuButtonWindow::RefreshCursor()
{
	POINT cursor{};
	HWND activeWindow = GetActiveWindow();
	if (activeWindow
		&& GetCursorPos(&cursor)
		&& ScreenToClient(activeWindow, &cursor))
	{
		UpdateCursorIfHovered(cursor.x, cursor.y);
	}
}

// Updates the visible Modern or Classic menu button's native cursor directly.
void MenuButtonWindow::UpdateActiveButtonCursor()
{
	cIGZWin* root = GetRootWindow();
	const uint32_t activeButtonID =
		settings && settings->cameraMode == CameraMode::Modern
			? kMenuButtonID
			: kMenuClassicButtonID;
	cIGZWin* activeButton = root
		? root->GetChildWindowFromIDRecursive(activeButtonID)
		: nullptr;
	if (activeButton)
	{
		activeButton->UpdateCursor();
	}
}

// Returns whether the cursor is over the menu and updates its native cursor
// when it is.
bool MenuButtonWindow::UpdateCursorIfHovered(int32_t parentX, int32_t parentY)
{
	if (!ContainsPoint(parentX, parentY))
	{
		return false;
	}

	UpdateActiveButtonCursor();
	return true;
}

// Invalidates the menu after the settings window changes state. The current
// artwork has no separate open-state visual, so the argument is intentionally
// unused.
void MenuButtonWindow::SetSettingsOpen(bool open)
{
	(void)open;
	if (GetRootWindow())
	{
		GetRootWindow()->InvalidateSelfAndParents();
	}
}

// Opens or closes settings when the button face is clicked. The separate
// lower-right LED control switches camera modes without affecting the glyph.
bool MenuButtonWindow::OnButtonClick(uint32_t controlID)
{
	if (controlID == kMenuModeToggleButtonID)
	{
		if (manager)
		{
			manager->ToggleCameraMode();
		}
		UpdateActiveButtonCursor();
		return true;
	}

	if (controlID == kMenuButtonID || controlID == kMenuClassicButtonID)
	{
		POINT cursor{};
		HWND activeWindow = GetActiveWindow();
		cIGZWin* root = GetRootWindow();
		if (activeWindow
			&& root
			&& !root->GetChildWindowFromIDRecursive(kMenuModeToggleButtonID)
			&& GetCursorPos(&cursor)
			&& ScreenToClient(activeWindow, &cursor)
			&& IsMenuModeTogglePoint(root, cursor.x, cursor.y))
		{
			if (manager)
			{
				manager->ToggleCameraMode();
			}
			UpdateActiveButtonCursor();
			return true;
		}

		const bool opened = manager && manager->ToggleSettingsWindow();
		SetSettingsOpen(opened);
		UpdateActiveButtonCursor();
		return true;
	}
	return false;
}

// Configures the baked greeting and changelog resource.
GreetingWindow::GreetingWindow()
	: BakedManagedWindow(
		"Greeting UI",
		kGreetingWindowResourceInstance,
		kGreetingWindowCLSID,
		kGreetingCloseXButtonID,
		kGreetingOKButtonID),
	manager(nullptr),
	settings(nullptr)
{
}

// Retains the manager used to open the controls reference window.
void GreetingWindow::SetWindowManager(SC4WindowManager* windowManager)
{
	manager = windowManager;
}

// Retains the settings that hold the automatic-changelog preference.
void GreetingWindow::SetSettings(PluginSettings* pluginSettings)
{
	settings = pluginSettings;
}

// Synchronizes the changelog preference as soon as the greeting is created.
void GreetingWindow::OnCreated()
{
	RefreshFromSettings();
}

// Mirrors the automatic-changelog preference in the greeting toggle button.
void GreetingWindow::RefreshFromSettings()
{
	SetButtonToggle(
		GetRootWindow(),
		kGreetingSuppressChangelogsButtonID,
		settings && !settings->showChangelogsOnUpdate);
}

// Toggles automatic changelogs or opens the controls reference window, then
// delegates ordinary close actions to the baked base class.
bool GreetingWindow::OnButtonClick(uint32_t controlID)
{
	if (controlID == kGreetingSuppressChangelogsButtonID && settings)
	{
		settings->showChangelogsOnUpdate = !settings->showChangelogsOnUpdate;
		if (!settings->Save())
		{
			Logger::GetInstance().WriteLine(
				LogLevel::Warning,
				"Changelog UI: failed to save the automatic changelog preference.");
		}
		RefreshFromSettings();
		if (manager)
		{
			manager->RefreshSettingsWindows();
		}
		return true;
	}
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

// Configures the read-only baked reference panel for the camera controls.
ControlsWindow::ControlsWindow()
	: BakedManagedWindow(
		"Controls UI",
		kControlsWindowResourceInstance,
		kControlsWindowCLSID,
		kControlsCloseXButtonID,
		kControlsOKButtonID)
{
}

// Configures the baked resource and callback storage for advanced settings.
AdvancedSettingsWindow::AdvancedSettingsWindow()
	: BakedManagedWindow(
		"Advanced Settings UI",
		kAdvancedWindowResourceInstance,
		kAdvancedWindowCLSID,
		kAdvancedCloseXButtonID,
		kAdvancedCloseButtonID),
	manager(nullptr),
	settings(nullptr),
	callbacks(nullptr)
{
}

// Retains the manager used to refresh related visible windows.
void AdvancedSettingsWindow::SetWindowManager(SC4WindowManager* windowManager)
{
	manager = windowManager;
}

// Retains the mutable settings object mirrored by advanced controls.
void AdvancedSettingsWindow::SetSettings(PluginSettings* pluginSettings)
{
	settings = pluginSettings;
}

// Retains runtime callbacks for settings that change active behavior.
void AdvancedSettingsWindow::SetCallbacks(const SC4WindowManagerCallbacks* managerCallbacks)
{
	callbacks = managerCallbacks;
}

// Copies the current advanced settings into an already-created baked window.
void AdvancedSettingsWindow::RefreshFromSettings()
{
	SyncControlsFromSettings();
}

// Synchronizes advanced controls as soon as the baked resource is available.
void AdvancedSettingsWindow::OnCreated()
{
	SyncControlsFromSettings();
}

// Copies redraw, diagnostics, camera-dump, and changelog preferences into the
// advanced controls. Redraw choices remain disabled in Classic camera mode.
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
	const uint32_t cameraDumpHotkeyIDs[2] = {
		kAdvancedCameraDumpHotkeyOnButtonID,
		kAdvancedCameraDumpHotkeyOffButtonID,
	};
	ApplyButtonChoice(
		GetRootWindow(),
		settings->cameraDumpHotkey
			? kAdvancedCameraDumpHotkeyOnButtonID
			: kAdvancedCameraDumpHotkeyOffButtonID,
		cameraDumpHotkeyIDs,
		2);
	SetButtonToggle(
		GetRootWindow(),
		kAdvancedShowChangelogsButtonID,
		settings->showChangelogsOnUpdate);
}

// Applies advanced-option buttons, persists their new values immediately, and
// notifies the runtime for redraw or logging changes that take effect at once.
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
			if (manager)
			{
				manager->RefreshSettingsWindows();
			}
			return true;
		}

		bool changed = true;
		bool redrawChanged = false;
		bool debugLoggingChanged = false;
		bool cameraDumpHotkeyChanged = false;
		bool showChangelogsChanged = false;
		const RedrawAggression previousRedrawAggression = settings->redrawAggression;
		const DebugLogging previousDebugLogging = settings->debugLogging;
		const bool previousCameraDumpHotkey = settings->cameraDumpHotkey;
		const bool previousShowChangelogs = settings->showChangelogsOnUpdate;
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
		case kAdvancedCameraDumpHotkeyOnButtonID:
			settings->cameraDumpHotkey = true;
			cameraDumpHotkeyChanged = true;
			break;
		case kAdvancedCameraDumpHotkeyOffButtonID:
			settings->cameraDumpHotkey = false;
			cameraDumpHotkeyChanged = true;
			break;
		case kAdvancedShowChangelogsButtonID:
			settings->showChangelogsOnUpdate = !settings->showChangelogsOnUpdate;
			showChangelogsChanged = true;
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
					PluginSettingText::ToString(previousRedrawAggression),
					PluginSettingText::ToString(settings->redrawAggression));
			}
			if (debugLoggingChanged)
			{
				LogOptionChanged(
					"Advanced Settings UI",
					"Diagnostics Logging",
					PluginSettingText::ToString(previousDebugLogging),
					PluginSettingText::ToString(settings->debugLogging));
			}
			if (cameraDumpHotkeyChanged)
			{
				LogOptionChanged(
					"Advanced Settings UI",
					"Camera Dump Hotkey",
					ToOnOffString(previousCameraDumpHotkey),
					ToOnOffString(settings->cameraDumpHotkey));
			}
			if (showChangelogsChanged)
			{
				LogOptionChanged(
					"Advanced Settings UI",
					"Show Changelogs on Update",
					ToOnOffString(previousShowChangelogs),
					ToOnOffString(settings->showChangelogsOnUpdate));
			}

			const char* reason = redrawChanged
				? "redraw aggression change"
				: debugLoggingChanged
					? "diagnostics logging change"
					: cameraDumpHotkeyChanged
						? "camera dump hotkey change"
						: "changelog preference change";
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
			if (manager)
			{
				manager->RefreshSettingsWindows();
			}
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

// Initializes an empty control laboratory with no retained native interfaces
// or recorded control geometry.
ControlLaboratoryWindow::ControlLaboratoryWindow()
	: refCount(0), parentWindow(nullptr), rootWindow(nullptr), scrollOffset(0), wheelDeltaRemainder(0),
	wheelScrollbarDirection(0), wheelScrollbarNotificationHandled(false)
{
}

// Releases laboratory controls if its owner destroys the window while open.
ControlLaboratoryWindow::~ControlLaboratoryWindow()
{
	Destroy();
}

// Resolves the COM-style window-procedure interface implemented by the
// laboratory.
bool ControlLaboratoryWindow::QueryInterface(uint32_t riid, void** ppvObj)
{
	return QueryWindowProcInterface(this, riid, ppvObj);
}

// Increments the reference count expected by the native window manager.
uint32_t ControlLaboratoryWindow::AddRef()
{
	return AddWindowProcRef(refCount);
}

// Decrements the native reference count without transferring C++ ownership.
uint32_t ControlLaboratoryWindow::Release()
{
	return ReleaseWindowProcRef(refCount);
}

// Creates the authored laboratory resource, records its native controls, and
// writes the current test-state document. The controls themselves come from
// the UI DAT, not from cIGZWinCtrlMgr factory calls.
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

	parentWindow = app->GetMainWindow()->GetChildWindowFromID(SC4WindowID::MainApplication);
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

// Detaches the native procedure, clears the recorded control layout, and
// releases the laboratory root and its parent interface.
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

// Reports whether the laboratory root window remains visible.
bool ControlLaboratoryWindow::IsVisible() const
{
	return rootWindow && rootWindow->IsVisible();
}

// Tests a parent-window coordinate against the visible laboratory bounds.
bool ControlLaboratoryWindow::ContainsPoint(int32_t parentX, int32_t parentY) const
{
	return WindowContainsParentPoint(rootWindow, parentX, parentY);
}

// Converts wheel input over the laboratory into native scrollbar-arrow
// actions, preserving partial high-resolution wheel deltas between calls.
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

// Activates the matching native scrollbar arrow when a window handle is
// available, otherwise applies the same 40-pixel content step directly.
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

// Finds each control authored in the laboratory UI resource and records its
// initial geometry, scroll behavior, and diagnostic name.
bool ControlLaboratoryWindow::BuildControls()
{
	// Controls are authored in the UI DAT and instantiated by SC4's own UI
	// script service.
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
			const int32_t left = child->GetL();
			const int32_t top = child->GetT();
			controls.push_back({
				child,
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
}

// Changes the laboratory content offset within its supported range and saves
// the resulting state.
void ControlLaboratoryWindow::ScrollBy(int32_t delta)
{
	scrollOffset = std::clamp(scrollOffset + delta, 0, 600);
	ApplyScrollPosition();
	SaveTestState();
	Logger::GetInstance().WriteLine(LogLevel::Info, "Test UI: scroll offset = " + std::to_string(scrollOffset));
}

// Repositions scrollable controls from the current content offset and hides
// any controls outside the laboratory's visible content viewport.
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
			// deterministic without resizing or re-anchoring the control.
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

// Returns recorded metadata for a laboratory control ID, or null when the ID
// does not belong to the authored resource.
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

// Captures a native control notification in the laboratory diagnostics and
// persists the updated event state.
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

// Writes the current laboratory scroll and event state to test.json using a
// temporary file before replacing the previous document.
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

// Records authored-control notifications and handles the laboratory's close
// and scroll commands. Other messages remain available to native SC4 code.
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

// Leaves low-level GZ messages for the native laboratory-window path.
bool ControlLaboratoryWindow::DoWinMsg(cIGZWin* pWin, uint32_t messageID, uint32_t data1, uint32_t data2, uint32_t data3)
{
	return false;
}

// Starts the city UI session: selects a compatible theme, creates the compact
// menu button, and schedules or suppresses the version greeting according to
// the persisted changelog preference.
void SC4WindowManager::OnCityLoaded(PluginSettings& settings)
{
	LoadThemeManifest();
	pendingSettings = &settings;
	menuButtonWindow.SetWindowManager(this);
	menuButtonWindow.SetSettings(&settings);
	if (!menuButtonWindow.Create())
	{
		Logger::GetInstance().WriteLine(
			LogLevel::Warning,
			"Window Manager: failed to create the camera settings menu button.");
	}
	else
	{
		menuButtonWindow.SetVisible(true);
		menuButtonWindow.RefreshMode();
	}

	if (settings.NeedsVersionNotice() && !settings.showChangelogsOnUpdate)
	{
		Logger::GetInstance().WriteLine(
			LogLevel::Info,
			"Window Manager: automatic changelog suppressed; recording the installed version.");
		settings.AcknowledgeCurrentVersion();
		return;
	}

	if (settings.NeedsVersionNotice())
	{
		CancelTimer(versionNoticeTimerID);

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
				+ "WASD: Pan Camera\n"
				+ "Shift + WASD: Pan Faster\n"
				+ "WASD Movement can be disabled in settings.\n"
				+ "Scroll Wheel: Zoom\n"
				+ "Mouse 3 + Drag: Pan & Tilt\n"
				+ "Alt: Hold Classic Angle\n\n"
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

// Replaces the runtime callbacks used after UI-driven settings changes.
void SC4WindowManager::SetCallbacks(SC4WindowManagerCallbacks managerCallbacks)
{
	callbacks = std::move(managerCallbacks);
}

// Consumes the delayed version-notice timer and opens the greeting when it is
// still needed.
void SC4WindowManager::OnVersionNoticeTimer()
{
	CancelTimer(versionNoticeTimerID);

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

// Recreates the primary settings window after a deferred refresh request.
void SC4WindowManager::OnSettingsReopenTimer()
{
	CancelTimer(settingsReopenTimerID);
	if (g_DelayedSettingsReopenManager == this)
	{
		g_DelayedSettingsReopenManager = nullptr;
	}

	settingsWindow.Destroy();
	ShowSettingsWindow();
}

// Opens the requested secondary window after the originating button command
// has returned to SC4's native UI dispatcher.
void SC4WindowManager::OnDeferredWindowOpenTimer()
{
	CancelTimer(deferredWindowOpenTimerID);
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

// Creates a centered basic window with default options and returns its handle.
SC4WindowHandle SC4WindowManager::CreateManagedWindow()
{
	return CreateManagedWindow(SC4BasicWindowOptions{});
}

// Creates a centered basic window with caller-supplied content and returns an
// invalid handle if its native resource cannot be created.
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

// Opens a supported named window template and returns its stable handle.
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

// Closes a basic window or destroys the control laboratory selected by handle.
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

// Cancels all pending UI timers, drops city-owned setting pointers, and
// releases every native window before the city context disappears.
void SC4WindowManager::OnCityShutdown()
{
	CancelTimer(settingsReopenTimerID);
	CancelTimer(versionNoticeTimerID);
	CancelTimer(deferredWindowOpenTimerID);
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

// Shows the native SC4 notification dialog on the supported executable. The
// native Boolean return is not a success indicator, so a completed call is
// reported as success.
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

// Shows the version greeting when needed, or acknowledges the installed
// version immediately when automatic changelogs are disabled.
bool SC4WindowManager::ShowVersionNoticeIfNeeded(PluginSettings& settings)
{
	if (!settings.NeedsVersionNotice())
	{
		return true;
	}

	if (!settings.showChangelogsOnUpdate)
	{
		return settings.AcknowledgeCurrentVersion();
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

// Opens the authored native-control laboratory.
bool SC4WindowManager::ShowControlLaboratory()
{
	return controlLaboratory.Create();
}

// Opens the greeting window with the current city settings.
bool SC4WindowManager::ShowGreetingWindow()
{
	greetingWindow.SetWindowManager(this);
	greetingWindow.SetSettings(pendingSettings);
	return greetingWindow.Create();
}

// Opens the read-only controls reference window.
bool SC4WindowManager::ShowControlsWindow()
{
	return controlsWindow.Create();
}

// Recreates the advanced settings window, then places it above primary
// settings so both windows retain their expected focus order.
bool SC4WindowManager::ShowAdvancedSettingsWindow()
{
	advancedSettingsWindow.Destroy();
	advancedSettingsWindow.SetWindowManager(this);
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

// Opens the primary settings window and invalidates the menu after its state
// changes.
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

// Closes the primary settings window when visible; otherwise opens it.
bool SC4WindowManager::ToggleSettingsWindow()
{
	if (settingsWindow.IsVisible())
	{
		settingsWindow.Close();
		return false;
	}
	return ShowSettingsWindow();
}

// Switches between Modern and Classic camera modes from the menu's lower lip,
// persists the result, refreshes dependent controls, and then notifies the
// runtime of the completed mode transition.
void SC4WindowManager::ToggleCameraMode()
{
	if (!pendingSettings)
	{
		return;
	}

	const CameraMode previous = pendingSettings->cameraMode;
	pendingSettings->cameraMode = previous == CameraMode::Modern
		? CameraMode::Classic
		: CameraMode::Modern;
	LogOptionChanged(
		"Menu Button UI",
		"Camera Mode",
		PluginSettingText::ToString(previous),
		PluginSettingText::ToString(pendingSettings->cameraMode));
	if (!pendingSettings->Save())
	{
		Logger::GetInstance().WriteLine(
			LogLevel::Warning,
			"Menu Button UI: failed to save the camera mode change.");
	}
	RefreshSettingsWindows();
	if (callbacks.OnCameraModeChanged)
	{
		callbacks.OnCameraModeChanged(pendingSettings->cameraMode == CameraMode::Modern);
	}
	menuButtonWindow.RefreshCursor();
}

// Shows or hides the compact camera-settings menu button.
void SC4WindowManager::SetMenuButtonVisible(bool visible)
{
	Logger::GetInstance().WriteLine(
		LogLevel::Info,
		std::string("Menu Button UI: ")
		+ (visible ? "showing" : "hiding")
		+ " camera settings button.");
	menuButtonWindow.SetVisible(visible);
}

// Invalidates the menu after primary settings close and notifies the runtime.
void SC4WindowManager::OnSettingsWindowClosed()
{
	menuButtonWindow.SetSettingsOpen(false);
	if (callbacks.OnSettingsWindowClosed)
	{
		callbacks.OnSettingsWindowClosed();
	}
}

// Synchronizes all visible settings-related windows after a shared setting
// changes, including the menu artwork for the active camera mode.
void SC4WindowManager::RefreshSettingsWindows()
{
	menuButtonWindow.RefreshMode();
	if (settingsWindow.IsVisible())
	{
		settingsWindow.RefreshFromSettings();
	}
	if (advancedSettingsWindow.IsVisible())
	{
		advancedSettingsWindow.RefreshFromSettings();
	}
	if (greetingWindow.IsVisible())
	{
		greetingWindow.RefreshFromSettings();
	}
}

// Defers a primary settings-window rebuild until the current UI command has
// finished. This is used after restoring defaults.
void SC4WindowManager::ScheduleSettingsWindowReopen()
{
	CancelTimer(settingsReopenTimerID);

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

// Defers opening a secondary settings or changelog window until the current
// SC4 button command has completed.
void SC4WindowManager::ScheduleDeferredWindowOpen(DeferredWindowOpen window)
{
	CancelTimer(deferredWindowOpenTimerID);

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

// Flushes the pending slider save and releases every window owned by this
// manager.
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

// Reports whether any full window is visible. The passive top-right menu icon
// is intentionally excluded from this modal-input check.
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

// Routes a parent-window hit test through every visible UI surface. The menu
// is checked first so its active native button can update cursor feedback.
bool SC4WindowManager::IsPointOverVisibleWindow(int32_t parentX, int32_t parentY)
{
	if (menuButtonWindow.UpdateCursorIfHovered(parentX, parentY))
	{
		return true;
	}
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

// Lets the control laboratory consume wheel input over its scrollable content.
bool SC4WindowManager::HandleMouseWheel(
	int32_t wheelDelta, int32_t parentX, int32_t parentY, intptr_t nativeWindowHandle)
{
	return controlLaboratory.HandleMouseWheel(
		wheelDelta, parentX, parentY, nativeWindowHandle);
}
