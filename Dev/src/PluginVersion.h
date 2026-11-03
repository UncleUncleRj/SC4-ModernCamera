#pragma once

#include <cstdint>

namespace PluginVersion
{
	// Major component of the plugin version displayed to users.
    inline constexpr uint32_t Major = 2;
	// Minor component of the plugin version displayed to users.
    inline constexpr uint32_t Minor = 0;
	// Patch component of the plugin version displayed to users.
    inline constexpr uint32_t Patch = 0;
	// Dotted version string used in settings and user-facing UI.
    inline constexpr char String[] = "2.0.0";
}
