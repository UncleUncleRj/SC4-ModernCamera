#include "VTableHook.h"

#include <Windows.h>

// Patches one vtable slot and retains the original function target.
bool VTableHook::Install(void** vtable, size_t index, void* replacement)
{
	if (mSlot || !vtable || !replacement) {
		return false;
	}

	void** slot = &vtable[index];
	void* original = nullptr;
	if (!PatchSlot(slot, replacement, &original)) {
		return false;
	}

	mSlot = slot;
	mReplacement = replacement;
	mOriginal = original;
	return true;
}

// Restores the original target when another plugin has not replaced this hook.
bool VTableHook::Uninstall()
{
	if (!mSlot) {
		return false;
	}

	const bool restored = *mSlot == mReplacement
		&& PatchSlot(mSlot, mOriginal, nullptr);
	mSlot = nullptr;
	mReplacement = nullptr;
	mOriginal = nullptr;
	return restored;
}

// Returns whether this object has installed a vtable replacement.
bool VTableHook::IsInstalled() const
{
	return mSlot != nullptr;
}

// Returns the function pointer captured during Install.
void* VTableHook::GetOriginal() const
{
	return mOriginal;
}

// Temporarily makes a vtable slot writable, then replaces its target.
bool VTableHook::PatchSlot(void** slot, void* replacement, void** originalOut)
{
	if (!slot || !replacement) {
		return false;
	}

	DWORD oldProtect = 0;
	if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
		return false;
	}

	if (originalOut) {
		*originalOut = *slot;
	}
	*slot = replacement;

	DWORD ignored = 0;
	VirtualProtect(slot, sizeof(void*), oldProtect, &ignored);
	return true;
}
