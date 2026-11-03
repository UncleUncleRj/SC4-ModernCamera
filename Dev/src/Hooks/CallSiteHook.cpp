#include "CallSiteHook.h"

#include <Windows.h>

#include <cstring>

namespace
{
	// Opcode of a near call with a 32-bit displacement.
	constexpr uint8_t kNearCallOpcode = 0xE8;
	// Length of that instruction, which its displacement is measured from.
	constexpr uintptr_t kNearCallLength = 5;

	// Returns the function a near call reaches, or 0 when the bytes are not a near call.
	uintptr_t ReadTarget(uintptr_t callSite)
	{
		if (*reinterpret_cast<const uint8_t*>(callSite) != kNearCallOpcode) {
			return 0;
		}

		int32_t displacement = 0;
		std::memcpy(&displacement, reinterpret_cast<const void*>(callSite + 1), sizeof(displacement));
		return callSite + kNearCallLength + static_cast<intptr_t>(displacement);
	}
}

// Redirects the call after confirming it still reaches the expected function.
bool CallSiteHook::Install(uintptr_t callSite, uintptr_t expectedTarget, void* replacement)
{
	if (mCallSite || !callSite || !replacement || ReadTarget(callSite) != expectedTarget) {
		return false;
	}

	const uintptr_t target = reinterpret_cast<uintptr_t>(replacement);
	if (!WriteTarget(callSite, target)) {
		return false;
	}

	mCallSite = callSite;
	mReplacement = target;
	mOriginal = expectedTarget;
	return true;
}

// Restores the call unless another plugin has redirected it since.
bool CallSiteHook::Uninstall()
{
	if (!mCallSite || ReadTarget(mCallSite) != mReplacement || !WriteTarget(mCallSite, mOriginal)) {
		return false;
	}

	mCallSite = 0;
	mReplacement = 0;
	mOriginal = 0;
	return true;
}

// Returns whether this object owns a redirected call.
bool CallSiteHook::IsInstalled() const
{
	return mCallSite != 0;
}

// Returns the function captured during Install.
void* CallSiteHook::GetOriginal() const
{
	return reinterpret_cast<void*>(mOriginal);
}

// Temporarily makes the instruction writable, then replaces its displacement.
bool CallSiteHook::WriteTarget(uintptr_t callSite, uintptr_t target)
{
	const auto displacementAddress = reinterpret_cast<void*>(callSite + 1);
	DWORD oldProtect = 0;
	if (!VirtualProtect(displacementAddress, sizeof(int32_t), PAGE_EXECUTE_READWRITE, &oldProtect)) {
		return false;
	}

	const int32_t displacement = static_cast<int32_t>(target - (callSite + kNearCallLength));
	std::memcpy(displacementAddress, &displacement, sizeof(displacement));

	DWORD ignored = 0;
	VirtualProtect(displacementAddress, sizeof(int32_t), oldProtect, &ignored);
	FlushInstructionCache(GetCurrentProcess(), displacementAddress, sizeof(int32_t));
	return true;
}
