#include "TestWindow.h"

#include "GZServPtrs.h"
#include "Logger.h"
#include "PluginPaths.h"
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

namespace
{
	constexpr uint32_t kWinSC4AppID = 0x6104489A;
	constexpr uint32_t kWindowResourceGroup = 0x3D0C0700;
	constexpr uint32_t kWindowResourceInstance = 0x3D0C0701;
	constexpr uint32_t kWindowCLSID = 0x3D0C0702;

	constexpr uint32_t kCloseButtonID = 0x3D0C0710;
	constexpr uint32_t kScrollUpButtonID = 0x3D0C0711;
	constexpr uint32_t kScrollDownButtonID = 0x3D0C0712;
	constexpr uint32_t kScrollBarID = 0x3D0C0713;
	constexpr uint32_t kCloseXButtonID = 0x3D0C0714;

	constexpr uint32_t kMessageTypeCommand = 3;
	constexpr uint32_t kCommandButtonClicked = 0x287259F6;

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
}

TestWindow::TestWindow()
	: refCount(0), parentWindow(nullptr), rootWindow(nullptr), scrollOffset(0)
{
}

TestWindow::~TestWindow()
{
	Destroy();
}

bool TestWindow::QueryInterface(uint32_t riid, void** ppvObj)
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

uint32_t TestWindow::AddRef()
{
	return ++refCount;
}

uint32_t TestWindow::Release()
{
	if (refCount > 0)
	{
		--refCount;
	}
	return refCount;
}

bool TestWindow::Create()
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
			"Test UI: failed to load SC4-3DMouseCam-TestUI.dat from the Plugins folder.");
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

void TestWindow::Destroy()
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
	Logger::GetInstance().WriteLine(LogLevel::Info, "Test UI: native control laboratory closed.");
}

bool TestWindow::IsVisible() const
{
	return rootWindow && rootWindow->IsVisible();
}

void TestWindow::AddControl(cIGZWin* window, cIGZUnknown* interfacePointer, uint32_t id,
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

bool TestWindow::BuildControls()
{
	// Controls are authored in the UI DAT and instantiated by SC4's own UI
	// script service. The cIGZWinCtrlMgr factory methods are still incomplete
	// reverse-engineered interfaces and caused an x86 stack-balance failure.
	struct ScriptControl { uint32_t id; const char* name; bool fixed; };
	const ScriptControl scriptControls[] = {
		{ kCloseButtonID, "close", true },
		{ kCloseXButtonID, "closeX", true },
		{ kScrollUpButtonID, "scrollUp", true },
		{ kScrollDownButtonID, "scrollDown", true },
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

	addLabel(0x3D0C0720, "SC4-3DMouseCam Native UI Control Laboratory", 48, cIGZFont::Style_Bold);
	Logger::GetInstance().WriteLine(LogLevel::Info, "Test UI: label creation succeeded.");
	addLabel(0x3D0C0721, "Each interaction is logged and written to test.json beside the DLL.", 72);
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

void TestWindow::ScrollBy(int32_t delta)
{
	scrollOffset = std::clamp(scrollOffset + delta, 0, 600);
	ApplyScrollPosition();
	SaveTestState();
	Logger::GetInstance().WriteLine(LogLevel::Info, "Test UI: scroll offset = " + std::to_string(scrollOffset));
}

void TestWindow::ApplyScrollPosition()
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

const TestWindow::ChildControl* TestWindow::FindControl(uint32_t id) const
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

void TestWindow::RecordEvent(const cGZMessage& msg)
{
	const uint32_t controlID = msg.dwData2;
	const ChildControl* control = FindControl(controlID);
	const std::string name = control ? control->name : "unknownControl";

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

void TestWindow::SaveTestState() const
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
		output << "\n    \"" << EscapeJson(control ? control->name : "unknownControl") << "\": {"
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

bool TestWindow::DoWinProcMessage(cIGZWin*, cGZMessage& msg)
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

bool TestWindow::DoWinMsg(cIGZWin* pWin, uint32_t messageID, uint32_t data1, uint32_t data2, uint32_t data3)
{
	return false;
}
