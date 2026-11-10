#pragma once

#include "Logger.h"

#include <exception>
#include <string>

/// <summary>
/// Logs an exception stopped at a native callback boundary.
/// </summary>
/// <param name="callback">Name of the callback that failed.</param>
/// <param name="detail">Description of the failure.</param>
inline void LogNativeCallbackFailure(const char* callback, const char* detail) noexcept
{
	try {
		Logger::GetInstance().WriteLine(LogLevel::Error, std::string(callback) + " failed: " + detail);
	}
	catch (...) {
		// Logging is the last resort, and nothing may escape to the native caller.
	}
}

/// <summary>
/// Runs a callback that SC4 or Windows invoked without letting a C++ exception reach the
/// caller. SC4's message server, tick loop, window procedures, and vtable callers catch
/// nothing, and neither do Windows timer and hook dispatch, so an escaping exception would
/// skip other work or end the process.
/// </summary>
/// <param name="callback">Name logged if the callback fails.</param>
/// <param name="function">Callback body.</param>
/// <returns>True when the body returned normally; false when it threw.</returns>
template <typename Function>
bool RunGuardedNativeCallback(const char* callback, Function&& function) noexcept
{
	try {
		function();
		return true;
	}
	catch (const std::exception& exception) {
		LogNativeCallbackFailure(callback, exception.what());
	}
	catch (...) {
		LogNativeCallbackFailure(callback, "unknown exception");
	}
	return false;
}
