#include "PluginPaths.h"

#include <Windows.h>

#include <array>
#include <stdexcept>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace
{
	// Current and legacy names used to locate plugin-owned runtime data.
	constexpr const wchar_t* kPluginDataDirectory = L"SC4-ModernCamera";
	constexpr const wchar_t* kLegacyPluginDataDirectory = L"SC4-3DMouseCam";
	constexpr const wchar_t* kSettingsFileName = L"SC4-ModernCamera.json";
	constexpr const wchar_t* kThemeManifestFileName = L"theme.json";
	constexpr const wchar_t* kLogFileName = L"SC4-ModernCamera.log";
	constexpr const wchar_t* kPreviousLogFileName = L"SC4-ModernCamera.last";
	constexpr const wchar_t* kTestFileName = L"test.json";
	// Initial buffer size for resolving the module filename, grown when needed.
	constexpr size_t kInitialModulePathCapacity = 512;

	// Maps a legacy filename to its current destination filename.
	struct FileMigration
	{
		const wchar_t* sourceName;
		const wchar_t* destinationName;
	};

	// Legacy settings and log filenames that are migrated without overwriting data.
	constexpr std::array kLegacyFileMigrations = {
		FileMigration{ L"SC4-3DMouseCam.json", kSettingsFileName },
		FileMigration{ L"SC4-3DMouseCam.log", kLogFileName },
		FileMigration{ L"SC4-3DMouseCam.last", kPreviousLogFileName },
	};

	// Resolves the directory that contains the loaded plugin DLL.
	std::filesystem::path ResolvePluginDirectory()
	{
		std::wstring modulePath(kInitialModulePathCapacity, L'\0');

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

	// Moves one legacy file only when its current destination does not exist.
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

	// Creates the writable data directory and migrates known legacy files into it.
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

		for (const FileMigration& migration : kLegacyFileMigrations)
		{
			MoveFileIfMissing(
				pluginDirectory / migration.sourceName,
				dataDirectory / migration.destinationName);
		}
		MoveFileIfMissing(pluginDirectory / kTestFileName, dataDirectory / kTestFileName);

		for (const FileMigration& migration : kLegacyFileMigrations)
		{
			MoveFileIfMissing(
				dataDirectory / migration.sourceName,
				dataDirectory / migration.destinationName);
		}

		for (const FileMigration& migration : kLegacyFileMigrations)
		{
			MoveFileIfMissing(
				legacyDataDirectory / migration.sourceName,
				dataDirectory / migration.destinationName);
		}
		MoveFileIfMissing(legacyDataDirectory / kTestFileName, dataDirectory / kTestFileName);

		return dataDirectory;
	}
}

const std::filesystem::path& PluginPaths::GetPluginDirectory()
{
	static const std::filesystem::path directory = ResolvePluginDirectory();
	return directory;
}

const std::filesystem::path& PluginPaths::GetDataDirectory()
{
	static const std::filesystem::path directory = ResolveDataDirectory();
	return directory;
}

std::filesystem::path PluginPaths::GetSettingsPath()
{
	return GetDataDirectory() / kSettingsFileName;
}

std::filesystem::path PluginPaths::GetThemeManifestPath()
{
	return GetDataDirectory() / kThemeManifestFileName;
}

std::filesystem::path PluginPaths::GetLogPath()
{
	return GetDataDirectory() / kLogFileName;
}

std::filesystem::path PluginPaths::GetTestSettingsPath()
{
	return GetDataDirectory() / kTestFileName;
}
