#include "PluginPaths.h"

#include <Windows.h>

#include <stdexcept>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace
{
	constexpr const wchar_t* kPluginDataDirectory = L"SC4-ModernCamera";
	constexpr const wchar_t* kLegacyPluginDataDirectory = L"SC4-3DMouseCam";
	constexpr const wchar_t* kSettingsFileName = L"SC4-ModernCamera.json";
	constexpr const wchar_t* kLogFileName = L"SC4-ModernCamera.log";
	constexpr const wchar_t* kPreviousLogFileName = L"SC4-ModernCamera.last";

	// Resolves the directory containing the loaded plugin DLL.
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

	// Moves a legacy file only when the destination has not already been created.
	void MoveFileIfMissing(
		const std::filesystem::path& source,
		const std::filesystem::path& destination)
	{
		if (!std::filesystem::exists(source) || std::filesystem::exists(destination))
		{
			return;
		}

		std::error_code error;
		std::filesystem::rename(source, destination, error);
		if (!error)
		{
			return;
		}

		error.clear();
		std::filesystem::copy_file(source, destination, std::filesystem::copy_options::none, error);
		if (!error)
		{
			std::filesystem::remove(source, error);
		}
	}

// Creates or migrates the writable data directory beneath the plugin directory.
	std::filesystem::path ResolveDataDirectory()
	{
		const std::filesystem::path pluginDirectory = ResolvePluginDirectory();
		const std::filesystem::path dataDirectory = pluginDirectory / kPluginDataDirectory;
		const std::filesystem::path legacyDataDirectory = pluginDirectory / kLegacyPluginDataDirectory;
		std::error_code error;
		if (!std::filesystem::exists(dataDirectory) && std::filesystem::is_directory(legacyDataDirectory))
		{
			std::filesystem::rename(legacyDataDirectory, dataDirectory, error);
			error.clear();
		}

		std::filesystem::create_directories(dataDirectory, error);
		if (error && !std::filesystem::is_directory(dataDirectory))
		{
			throw std::runtime_error("Failed to create the SC4-ModernCamera data directory.");
		}

		MoveFileIfMissing(pluginDirectory / L"SC4-3DMouseCam.json", dataDirectory / kSettingsFileName);
		MoveFileIfMissing(pluginDirectory / L"SC4-3DMouseCam.log", dataDirectory / kLogFileName);
		MoveFileIfMissing(pluginDirectory / L"SC4-3DMouseCam.last", dataDirectory / kPreviousLogFileName);
		MoveFileIfMissing(pluginDirectory / L"test.json", dataDirectory / L"test.json");

		MoveFileIfMissing(dataDirectory / L"SC4-3DMouseCam.json", dataDirectory / kSettingsFileName);
		MoveFileIfMissing(dataDirectory / L"SC4-3DMouseCam.log", dataDirectory / kLogFileName);
		MoveFileIfMissing(dataDirectory / L"SC4-3DMouseCam.last", dataDirectory / kPreviousLogFileName);
		MoveFileIfMissing(legacyDataDirectory / L"SC4-3DMouseCam.json", dataDirectory / kSettingsFileName);
		MoveFileIfMissing(legacyDataDirectory / L"SC4-3DMouseCam.log", dataDirectory / kLogFileName);
		MoveFileIfMissing(legacyDataDirectory / L"SC4-3DMouseCam.last", dataDirectory / kPreviousLogFileName);
		MoveFileIfMissing(legacyDataDirectory / L"test.json", dataDirectory / L"test.json");

		return dataDirectory;
	}
}

// Returns the cached plugin directory resolved from the loaded DLL location.
const std::filesystem::path& PluginPaths::GetPluginDirectory()
{
	static const std::filesystem::path directory = ResolvePluginDirectory();
	return directory;
}

// Returns the cached writable directory for settings and logs.
const std::filesystem::path& PluginPaths::GetDataDirectory()
{
	static const std::filesystem::path directory = ResolveDataDirectory();
	return directory;
}

// Returns the settings-file path in the plugin data directory.
std::filesystem::path PluginPaths::GetSettingsPath()
{
	return GetDataDirectory() / kSettingsFileName;
}

// Returns the diagnostic-log path in the plugin data directory.
std::filesystem::path PluginPaths::GetLogPath()
{
	return GetDataDirectory() / kLogFileName;
}

// Returns the separate settings path used by the test UI workflow.
std::filesystem::path PluginPaths::GetTestSettingsPath()
{
	return GetDataDirectory() / L"test.json";
}
