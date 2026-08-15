#pragma once

#include <cstddef>

// Tracks one vtable replacement and can restore its original target.
class VTableHook
{
public:
	// Replaces one vtable slot and records its original target for later restoration.
	bool Install(void** vtable, size_t index, void* replacement);
	// Restores the original target only if the slot still contains this hook.
	bool Uninstall();
	// Returns whether a slot is currently owned by this hook object.
	bool IsInstalled() const;
	// Returns the target captured before installation, or null when not installed.
	void* GetOriginal() const;

private:
	// Temporarily changes one slot's page protection, writes a target, then restores it.
	static bool PatchSlot(void** slot, void* replacement, void** originalOut);

	// Address of the currently patched vtable slot.
	void** mSlot = nullptr;
	// Replacement target written into the patched slot.
	void* mReplacement = nullptr;
	// Original target captured before the slot was patched.
	void* mOriginal = nullptr;
};
