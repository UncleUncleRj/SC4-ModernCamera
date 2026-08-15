#pragma once

#include <cstdint>

namespace SC4VersionDetection
{
	// Returns the SC4 executable revision detected for this process.
	uint16_t GetGameVersion();
	// Returns whether version detection reports the supported revision 641.
	bool IsDigitalDistributionVersion();
}
