#include "SC4VersionDetection.h"

#include <Windows.h>
#include <memory>

namespace
{
	uint64_t GetAssemblyVersion(HMODULE hModule)
	{
		wchar_t versionFile[MAX_PATH]{};

		if (GetModuleFileNameW(hModule, versionFile, MAX_PATH) == 0)
		{
			return 0;
		}

		DWORD verHandle = 0;
		const DWORD verSize = GetFileVersionInfoSizeW(versionFile, &verHandle);

		if (verSize > 0)
		{
			auto verData = std::make_unique<BYTE[]>(verSize);
			LPBYTE buffer = nullptr;
			UINT size = 0;

			if (GetFileVersionInfoW(versionFile, 0, verSize, verData.get())
				&& VerQueryValueW(verData.get(), L"\\", reinterpret_cast<LPVOID*>(&buffer), &size)
				&& size > 0)
			{
				VS_FIXEDFILEINFO* verInfo = reinterpret_cast<VS_FIXEDFILEINFO*>(buffer);

				if (verInfo->dwSignature == 0xfeef04bd)
				{
					uint64_t value = static_cast<uint64_t>(verInfo->dwFileVersionMS) << 32;
					value |= verInfo->dwFileVersionLS;

					return value;
				}
			}
		}

		return 0;
	}

	uint16_t DetermineGameVersion()
	{
		const uint64_t fileVersion = GetAssemblyVersion(nullptr);
		const uint16_t majorVersion = static_cast<uint16_t>((fileVersion >> 48) & 0xFFFF);
		const uint16_t minorVersion = static_cast<uint16_t>((fileVersion >> 32) & 0xFFFF);
		const uint16_t revision = static_cast<uint16_t>((fileVersion >> 16) & 0xFFFF);

		if (fileVersion > 0 && majorVersion == 1 && minorVersion == 1)
		{
			return revision;
		}

		// Fallback used by SC4Fix/sc4-dll-utilities when the executable has no
		// readable version resource.
		const uint8_t sentinel = *reinterpret_cast<uint8_t*>(0x6E5000);

		switch (sentinel)
		{
		case 0x8B:
			return 610; // Cannot distinguish from 613 with this fallback.
		case 0xFF:
			return 638;
		case 0x24:
			return 640;
		case 0x0F:
			return 641;
		default:
			return 0;
		}
	}
}

uint16_t SC4VersionDetection::GetGameVersion()
{
	static const uint16_t gameVersion = DetermineGameVersion();

	return gameVersion;
}

bool SC4VersionDetection::IsDigitalDistributionVersion()
{
	return GetGameVersion() == 641;
}
