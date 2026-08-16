#pragma once

#include <cstdint>

// Reverse-engineered native window identifiers shared by UI and input code.
namespace SC4WindowID
{
	// Main SimCity 4 application window in the game's native UI hierarchy.
	inline constexpr uint32_t MainApplication = 0x6104489A;
	// Parent window that owns the active 3D city view.
	inline constexpr uint32_t View3D = 0x9A47B417;
	// Child surface that receives direct 3D city input.
	inline constexpr uint32_t View3DSurface = 0x6A5E44B6;
}
