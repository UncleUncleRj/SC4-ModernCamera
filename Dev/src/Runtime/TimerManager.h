#pragma once

#include <Windows.h>

#include <array>
#include <cstddef>

enum class RuntimeTimer : size_t
{
	// One-shot redraw after camera input becomes idle.
	IdleRedraw,
	// One-shot periodic redraw that reschedules itself when required.
	PeriodicRedraw,
	// Repeating keyboard-pan update while a captured key remains held.
	KeyboardPan,
	// One-shot timer that hides the camera-dump confirmation.
	CameraDumpConfirmation,
	// One-shot timer that hides the camera-bookmark confirmation.
	BookmarkConfirmation,
	// One-shot Escape input that clears a native View3D tool.
	NativeToolClearEsc,
	// One-shot restoration of 3D-view focus after a UI transition.
	ModernCameraFocusRestore,
	// Number of timer slots. This is not a valid timer.
	Count,
};

// Owns Win32 timer IDs for the plugin's named runtime timer slots.
class TimerManager
{
public:
	// Starts an inactive timer slot and returns false when the slot is already active or setup fails.
	bool Start(RuntimeTimer timer, UINT delayMs, TIMERPROC callback);
	// Stops the timer in the selected slot when it is active.
	void Stop(RuntimeTimer timer);
	// Returns whether the selected slot owns an active Win32 timer.
	bool IsActive(RuntimeTimer timer) const;
	// Returns whether a callback ID belongs to the selected timer slot.
	bool IsCurrent(RuntimeTimer timer, UINT_PTR timerID) const;

private:
	// Number of usable timer slots.
	static constexpr size_t kTimerCount = static_cast<size_t>(RuntimeTimer::Count);

	// Maps a timer enum value to its array index.
	static constexpr size_t ToIndex(RuntimeTimer timer)
	{
		return static_cast<size_t>(timer);
	}

	// Win32 timer ID for each runtime-owned timer slot. Zero means inactive.
	std::array<UINT_PTR, kTimerCount> mTimerIDs{};
};
