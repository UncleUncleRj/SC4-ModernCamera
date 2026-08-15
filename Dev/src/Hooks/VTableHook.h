#pragma once

#include <cstddef>

class VTableHook
{
public:
	bool Install(void** vtable, size_t index, void* replacement);
	bool Uninstall();
	bool IsInstalled() const;
	void* GetOriginal() const;

private:
	static bool PatchSlot(void** slot, void* replacement, void** originalOut);

	void** mSlot = nullptr;
	void* mReplacement = nullptr;
	void* mOriginal = nullptr;
};
