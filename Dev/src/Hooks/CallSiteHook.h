#pragma once

#include <cstdint>

// Redirects one native near-call instruction and can restore it.
class CallSiteHook
{
public:
	/// <summary>
	/// Redirects a near call, after checking that it still calls the expected function.
	/// </summary>
	/// <param name="callSite">Address of the call instruction.</param>
	/// <param name="expectedTarget">Function the instruction must currently call.</param>
	/// <param name="replacement">Function to call instead.</param>
	/// <returns>True when the call now reaches the replacement.</returns>
	bool Install(uintptr_t callSite, uintptr_t expectedTarget, void* replacement);
	/// <summary>
	/// Restores the original call if the instruction still calls the replacement. Otherwise it
	/// stays installed, because another plugin may have chained on top and still forwards here.
	/// </summary>
	/// <returns>True when the original call was restored.</returns>
	bool Uninstall();
	/// <summary>
	/// Returns whether this object currently owns a redirected call.
	/// </summary>
	/// <returns>True when installed.</returns>
	bool IsInstalled() const;
	/// <summary>
	/// Returns the function the instruction called before installation.
	/// </summary>
	/// <returns>The original function, or null when not installed.</returns>
	void* GetOriginal() const;

private:
	/// <summary>
	/// Writes a new target into a near-call instruction.
	/// </summary>
	/// <param name="callSite">Address of the call instruction.</param>
	/// <param name="target">Function the instruction should call.</param>
	/// <returns>True when the instruction was written.</returns>
	static bool WriteTarget(uintptr_t callSite, uintptr_t target);

	// Address of the redirected call instruction.
	uintptr_t mCallSite = 0;
	// Function the instruction calls while installed.
	uintptr_t mReplacement = 0;
	// Function the instruction called before installation.
	uintptr_t mOriginal = 0;
};
