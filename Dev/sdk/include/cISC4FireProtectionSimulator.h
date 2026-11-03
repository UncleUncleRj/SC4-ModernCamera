/*
 * gzcom-dll - an open-source DLL Plugin SDK for SimCity 4
 *
 * cISC4FireProtectionSimulator.h
 *
 * Copyright (C) 2025 Nicholas Hayes
 *
 * Modified for SC4-ModernCamera on 2026-11-02, from gzcom-dll adda27c: signatures corrected to match the game's 641 code; added the interface ID the game's QueryInterface code uses.
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
#include "SC4List.h"

class cISC4DepartmentBudget;
class cISC4Occupant;
class SC4Percentage;

template<typename T> class cISC4SimGrid;
template<typename T> class SC4List;

static const uint32_t GZIID_cISC4FireProtectionSimulator = 0x88f54c7b;

class cISC4FireProtectionSimulator : public cIGZUnknown
{
public:
	virtual bool Init() = 0;
	virtual bool Shutdown() = 0;

	virtual cISC4SimGrid<int16_t>* GetFireProtectionGrid() const = 0;

	virtual cISC4DepartmentBudget* GetDepartmentBudget() const = 0;

	virtual bool IsFireStation(cISC4Occupant* pStation) const = 0;

	virtual bool GetStationFunding(cISC4Occupant* pStation, int32_t& nFunding, SC4Percentage& sFundingPercent) const = 0;
	virtual bool SetStationFunding(cISC4Occupant* pStation, SC4Percentage const& sFundingPercent) = 0;

	virtual SC4Percentage GetStationEfficiency(cISC4Occupant* pStation) = 0; // Binary-verified at 0x6AD430: returned through a hidden pointer.
	virtual bool IsOnStrike() = 0;
	virtual SC4Percentage GetChanceOfStrike() = 0; // Binary-verified at 0x6AD950: returned through a hidden pointer.
	virtual bool BeginStrike(cISC4Occupant* pStation) = 0;
	virtual bool EndStrike() = 0;

	virtual uint32_t GetStrikeStartDate() = 0;
	virtual uint32_t GetStrikeLength() = 0;

	virtual bool GetStrikingStations(SC4List<cISC4Occupant*>& list) = 0;
	virtual bool IsStationOnStrike(cISC4Occupant* pStation) = 0;

	virtual float GetAverageCoverage() = 0;
	virtual float GetPercentCoverage() = 0;
	virtual bool GetOccupantCoverage(cISC4Occupant* pStation, SC4Percentage const& sPercentage, float& fRadiusX, float& fRadiusZ) = 0;

	virtual int32_t GetFireStations(SC4List<cISC4Occupant*>& list) = 0;
	virtual uint32_t GetFireStationCount() = 0;
	virtual float GetStationDispatchRadius(cISC4Occupant* pStation) = 0;
	virtual uint32_t GetTotalDispatchCount() = 0;
};