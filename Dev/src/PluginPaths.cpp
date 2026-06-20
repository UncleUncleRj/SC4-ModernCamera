#include "PluginPaths.h"

#include <Windows.h>

#include <stdexcept>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace
{
	std::filesystem::path ResolvePluginDirectory()
	{
		std::wstring modulePath(512, L'\0');

		for (;;)
		{
			const DWORD length = GetModuleFileNameW(
				reinterpret_cast<HMODULE>(&__ImageBase),
				modulePath.data(),
				static_cast<DWORD>(modulePath.size()));

			if (length == 0)
			{
				throw std::runtime_error("Failed to resolve the plugin DLL path.");
			}

			if (length < modulePath.size() - 1)
			{
				modulePath.resize(length);
				return std::filesystem::path(modulePath).parent_path();
			}

			modulePath.resize(modulePath.size() * 2);
		}
	}
}

const std::filesystem::path& PluginPaths::GetPluginDirectory()
{
	static const std::filesystem::path directory = ResolvePluginDirectory();
	return directory;
}

std::filesystem::path PluginPaths::GetSettingsPath()
{
	return GetPluginDirectory() / L"SC4-3DMouseCam.json";
}

std::filesystem::path PluginPaths::GetLogPath()
{
	return GetPluginDirectory() / L"SC4-3DMouseCam.log";
}
