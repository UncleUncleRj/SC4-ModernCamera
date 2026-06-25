#pragma once

#include "cIGZWinProc.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class cGZMessage;
class cIGZUnknown;
class cIGZWin;
class PluginSettings;

using SC4WindowHandle = uint32_t;
inline constexpr SC4WindowHandle InvalidSC4WindowHandle = 0;

enum class SC4WindowButton : uint8_t
{
	XOnly = 0,
	OK = 1,
	Close = 2,
	Accept = 3,
};

enum class SC4WindowTemplate : uint8_t
{
	ControlLaboratory = 0,
};

struct SC4BasicWindowOptions
{
	int32_t width = 420;
	int32_t height = 240;
	std::string title;
	std::string text;
	SC4WindowButton button = SC4WindowButton::XOnly;
};

class BasicManagedWindow final : public cIGZWinProc
{
public:
	BasicManagedWindow();
	~BasicManagedWindow();

	bool QueryInterface(uint32_t riid, void** ppvObj) override;
	uint32_t AddRef() override;
	uint32_t Release() override;
	bool DoWinProcMessage(cIGZWin* pWin, cGZMessage& msg) override;
	bool DoWinMsg(cIGZWin* pWin, uint32_t messageID, uint32_t data1, uint32_t data2, uint32_t data3) override;

	bool Create(const SC4BasicWindowOptions& options);
	void Destroy();
	void Close();
	bool IsVisible() const;

private:
	uint32_t refCount;
	cIGZWin* parentWindow;
	cIGZWin* rootWindow;
};

class BakedManagedWindow : public cIGZWinProc
{
public:
	enum class Placement : uint8_t
	{
		Center,
		TopRightButton,
	};

	BakedManagedWindow(
		const char* logName,
		uint32_t resourceInstanceID,
		uint32_t rootCLSID,
		uint32_t closeXButtonID,
		uint32_t okButtonID,
		Placement placement = Placement::Center);
	~BakedManagedWindow();

	bool QueryInterface(uint32_t riid, void** ppvObj) override;
	uint32_t AddRef() override;
	uint32_t Release() override;
	bool DoWinProcMessage(cIGZWin* pWin, cGZMessage& msg) override;
	bool DoWinMsg(cIGZWin* pWin, uint32_t messageID, uint32_t data1, uint32_t data2, uint32_t data3) override;

	bool Create();
	void Destroy();
	void Close();
	bool IsVisible() const;

protected:
	virtual bool OnButtonClick(uint32_t controlID);
	virtual void OnClosed();
	cIGZWin* GetRootWindow() const;

private:
	const char* logName;
	uint32_t resourceInstanceID;
	uint32_t rootCLSID;
	uint32_t closeXButtonID;
	uint32_t okButtonID;
	Placement placement;
	uint32_t refCount;
	cIGZWin* parentWindow;
	cIGZWin* rootWindow;
};

class SettingsWindow final : public BakedManagedWindow
{
public:
	SettingsWindow();
	void SetWindowManager(class SC4WindowManager* manager);

protected:
	bool OnButtonClick(uint32_t controlID) override;
	void OnClosed() override;

private:
	SC4WindowManager* manager;
};

class MenuButtonWindow final : public BakedManagedWindow
{
public:
	MenuButtonWindow();
	void SetWindowManager(class SC4WindowManager* manager);
	void SetSettingsOpen(bool open);

protected:
	bool OnButtonClick(uint32_t controlID) override;

private:
	SC4WindowManager* manager;
};

class GreetingWindow final : public BakedManagedWindow
{
public:
	GreetingWindow();
	void SetWindowManager(class SC4WindowManager* manager);

protected:
	bool OnButtonClick(uint32_t controlID) override;

private:
	SC4WindowManager* manager;
};

class ControlsWindow final : public BakedManagedWindow
{
public:
	ControlsWindow();
};

class ControlLaboratoryWindow final : public cIGZWinProc
{
public:
	ControlLaboratoryWindow();
	~ControlLaboratoryWindow();

	bool QueryInterface(uint32_t riid, void** ppvObj) override;
	uint32_t AddRef() override;
	uint32_t Release() override;
	bool DoWinProcMessage(cIGZWin* pWin, cGZMessage& msg) override;
	bool DoWinMsg(cIGZWin* pWin, uint32_t messageID, uint32_t data1, uint32_t data2, uint32_t data3) override;

	bool Create();
	void Destroy();
	bool IsVisible() const;
	bool HandleMouseWheel(int32_t wheelDelta, int32_t parentX, int32_t parentY, intptr_t nativeWindowHandle);

private:
	struct ChildControl
	{
		cIGZWin* window;
		cIGZUnknown* interfacePointer;
		uint32_t id;
		int32_t baseLeft;
		int32_t baseTop;
		int32_t width;
		int32_t height;
		bool fixed;
		std::string name;
	};

	struct EventState
	{
		uint32_t count = 0;
		uint32_t messageType = 0;
		uint32_t data1 = 0;
		uint32_t data3 = 0;
		bool selected = false;
		std::string caption;
	};

	bool BuildControls();
	void AddControl(cIGZWin* window, cIGZUnknown* interfacePointer, uint32_t id,
		const char* name, int32_t left, int32_t top, int32_t right, int32_t bottom, bool fixed = false);
	void ScrollBy(int32_t delta);
	void ActivateScrollbarArrow(int32_t direction, intptr_t nativeWindowHandle);
	void ApplyScrollPosition();
	void RecordEvent(const cGZMessage& msg);
	void SaveTestState() const;
	const ChildControl* FindControl(uint32_t id) const;

	uint32_t refCount;
	cIGZWin* parentWindow;
	cIGZWin* rootWindow;
	int32_t scrollOffset;
	int32_t wheelDeltaRemainder;
	int32_t wheelScrollbarDirection;
	bool wheelScrollbarNotificationHandled;
	std::vector<ChildControl> controls;
	std::unordered_map<uint32_t, EventState> eventStates;
};

// Owns every plugin-created SC4 window and is the single integration point
// for city lifecycle, popup dialogs, and window-specific input routing.
class SC4WindowManager
{
public:
	void OnCityLoaded(PluginSettings& settings);
	void OnCityShutdown();
	void OnVersionNoticeTimer();

	bool ShowVersionNoticeIfNeeded(PluginSettings& settings);
	bool ShowNotification(const char* caption, const char* message) const;
	SC4WindowHandle CreateManagedWindow();
	SC4WindowHandle CreateManagedWindow(const SC4BasicWindowOptions& options);
	SC4WindowHandle CreateManagedWindow(SC4WindowTemplate windowTemplate);
	bool CloseWindow(SC4WindowHandle handle);

	bool ShowControlLaboratory();
	bool ShowGreetingWindow();
	bool ShowControlsWindow();
	bool ShowSettingsWindow();
	bool ToggleSettingsWindow();
	void OnSettingsWindowClosed();
	void CloseAllWindows();
	bool HasVisibleWindow() const;
	bool HandleMouseWheel(
		int32_t wheelDelta, int32_t parentX, int32_t parentY, intptr_t nativeWindowHandle);

private:
	struct BasicWindowEntry
	{
		SC4WindowHandle handle;
		std::unique_ptr<BasicManagedWindow> window;
	};

	ControlLaboratoryWindow controlLaboratory;
	GreetingWindow greetingWindow;
	ControlsWindow controlsWindow;
	SettingsWindow settingsWindow;
	MenuButtonWindow menuButtonWindow;
	std::vector<BasicWindowEntry> basicWindows;
	SC4WindowHandle nextWindowHandle = 100;
	PluginSettings* pendingVersionNoticeSettings = nullptr;
	uintptr_t versionNoticeTimerID = 0;
};
