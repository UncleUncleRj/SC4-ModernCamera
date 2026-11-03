/*
 * gzcom-dll - an open-source DLL Plugin SDK for SimCity 4
 *
 * cISC4CommercialSimulator.h
 *
 * Copyright (C) 2025 Nicholas Hayes
 *
 * Modified for SC4-ModernCamera on 2026-11-02, from gzcom-dll adda27c: added the interface ID the game's QueryInterface code uses.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation, under
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <https://www.gnu.org/licenses/>.
 */

#pragma once
#include "cIGZUnknown.h"
#include "cISC4BuildingDevelopmentSimulator.h"

template <typename T> class cISC4SimGrid;

static const uint32_t GZIID_cISC4CommercialSimulator = 0x077abeb2;

class cISC4CommercialSimulator : public cIGZUnknown
{
public:
	virtual bool Init() = 0;
	virtual bool Shutdown() = 0;

	virtual cISC4SimGrid<uint8_t>* GetProximityMap(cISC4BuildingDevelopmentSimulator::DeveloperType) = 0;
};