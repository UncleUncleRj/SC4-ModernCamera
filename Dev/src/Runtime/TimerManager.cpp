#include "TimerManager.h"

// Starts a previously inactive runtime timer and retains its Win32 ID.
bool TimerManager::Start(RuntimeTimer timer, UINT delayMs, TIMERPROC callback)
{
	const size_t index = ToIndex(timer);
	if (mTimerIDs[index] != 0 || !callback) {
		return false;
	}

	mTimerIDs[index] = SetTimer(NULL, 0, delayMs, callback);
	return mTimerIDs[index] != 0;
}

// Stops an active timer and clears its retained ID.
void TimerManager::Stop(RuntimeTimer timer)
{
	UINT_PTR& timerID = mTimerIDs[ToIndex(timer)];
	if (timerID != 0) {
		KillTimer(NULL, timerID);
		timerID = 0;
	}
}

// Returns whether the selected timer slot currently has an ID.
bool TimerManager::IsActive(RuntimeTimer timer) const
{
	return mTimerIDs[ToIndex(timer)] != 0;
}

// Matches a callback ID against the selected active timer slot.
bool TimerManager::IsCurrent(RuntimeTimer timer, UINT_PTR timerID) const
{
	return timerID != 0 && mTimerIDs[ToIndex(timer)] == timerID;
}
