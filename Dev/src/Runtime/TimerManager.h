#pragma once

#include <Windows.h>

#include <array>
#include <cstddef>

enum class RuntimeTimer : size_t
{
	IdleRedraw,
	PeriodicRedraw,
	KeyboardPan,
	CameraDumpConfirmation,
	NativeToolClearEsc,
	ModernCameraFocusRestore,
	Count,
};

class TimerManager
{
public:
	bool Start(RuntimeTimer timer, UINT delayMs, TIMERPROC callback);
	void Stop(RuntimeTimer timer);
	bool IsActive(RuntimeTimer timer) const;
	bool IsCurrent(RuntimeTimer timer, UINT_PTR timerID) const;

private:
	static constexpr size_t kTimerCount = static_cast<size_t>(RuntimeTimer::Count);

	static constexpr size_t ToIndex(RuntimeTimer timer)
	{
		return static_cast<size_t>(timer);
	}

	std::array<UINT_PTR, kTimerCount> mTimerIDs{};
};
