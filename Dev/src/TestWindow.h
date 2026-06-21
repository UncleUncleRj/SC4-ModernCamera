#pragma once

#include "cIGZWinProc.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

class cGZMessage;
class cIGZUnknown;
class cIGZWin;

class TestWindow final : public cIGZWinProc
{
public:
	TestWindow();
	~TestWindow();

	bool QueryInterface(uint32_t riid, void** ppvObj) override;
	uint32_t AddRef() override;
	uint32_t Release() override;
	bool DoWinProcMessage(cIGZWin* pWin, cGZMessage& msg) override;
	bool DoWinMsg(cIGZWin* pWin, uint32_t messageID, uint32_t data1, uint32_t data2, uint32_t data3) override;

	bool Create();
	void Destroy();
	bool IsVisible() const;

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
	void ApplyScrollPosition();
	void RecordEvent(const cGZMessage& msg);
	void SaveTestState() const;
	const ChildControl* FindControl(uint32_t id) const;

	uint32_t refCount;
	cIGZWin* parentWindow;
	cIGZWin* rootWindow;
	int32_t scrollOffset;
	std::vector<ChildControl> controls;
	std::unordered_map<uint32_t, EventState> eventStates;
};
