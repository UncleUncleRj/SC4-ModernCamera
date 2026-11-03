/*
 * gzcom-dll - an open-source DLL Plugin SDK for SimCity 4
 *
 * cISC4PoliceSimulator.h
 *
 * Copyright (C) 2016 Nelson Gomez
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

class cISC4Occupant;
class SC4Percentage;

template<typename T> class cISC4SimGrid;
template<typename T> class SC4List;

static const uint32_t GZIID_cISC4PoliceSimulator = 0x67659895;

class cISC4PoliceSimulator : public cIGZUnknown
{
	public:
		virtual bool Init(void) = 0;
		virtual bool Shutdown(void) = 0;

		virtual cISC4SimGrid<short>* GetPolicePowerGrid(void) = 0;

		virtual intptr_t GetDepartmentBudget(void) = 0;

		virtual bool GetStationFunding(cISC4Occupant* pStation, int32_t& nFunding, SC4Percentage& sFundingPercent) = 0;
		virtual bool SetStationFunding(cISC4Occupant* pStation, SC4Percentage const& sFundingPercentage) = 0;

		virtual uint32_t GetJailDepartmentBudget(void) = 0;

		virtual bool IsOnStrike(void) = 0;
		virtual SC4Percentage GetChanceOfStrike(void) = 0; // Binary-verified at 0x6E0F90: returned through a hidden pointer.

		virtual bool BeginStrike(cISC4Occupant* pStation) = 0;
		virtual bool EndStrike(void) = 0;

		virtual uint32_t GetStrikeStartDate(void) = 0;
		virtual uint32_t GetStrikeLength(void) = 0;

		virtual bool GetStrikingStations(SC4List<cISC4Occupant*>& sList) = 0;
		virtual bool IsStationOnStrike(cISC4Occupant* pStation) = 0;

		virtual float GetAverageCoverage(void) = 0;
		virtual float GetPercentCoverage(void) = 0;
		virtual bool GetOccupantCoverage(cISC4Occupant* pStation, SC4Percentage const& sPercentage, float& fRadiusX, float& fRadiusZ) = 0;

		virtual bool GetPoliceStations(SC4List<cISC4Occupant*>& sList) = 0;
		virtual bool IsPoliceStation(cISC4Occupant* pBuilding) = 0;

		virtual float GetStationDispatchRadius(cISC4Occupant* pStation) = 0;

		virtual uint32_t GetPoliceStationCount(void) = 0;
		virtual SC4Percentage GetStationEfficiency(cISC4Occupant* pStation) = 0; // Binary-verified at 0x6E0E70: returned through a hidden pointer.

		virtual uint32_t GetTotalDispatchCount(void) = 0;

		virtual bool ResetUnjailedCriminals(void) = 0;
		virtual uint32_t GetJailCapacity(void) = 0;
		virtual uint32_t GetJailPopulation(void) = 0;
		virtual uint32_t GetCriminalCount(void) = 0;

		virtual int32_t GetJails(SC4List<cISC4Occupant*>& sList) = 0;
		virtual bool IsJail(cISC4Occupant* pBuilding) = 0;

		virtual uint32_t GetArrestCount(void) = 0;
		virtual uint32_t GetJailCount(void) = 0;
		virtual uint32_t GetJailCapacity(cISC4Occupant* pJail) = 0;
		virtual SC4Percentage GetJailedPercentage(void) = 0; // Binary-verified at 0x6E0B30: returned through a hidden pointer.
		virtual int64_t GetJailCurrentUpkeepCost(cISC4Occupant* pJail) = 0;

		virtual bool ProcessMonthlyCrimeStats(void) = 0;

		virtual bool IsInJailbreak(cISC4Occupant* pJail) = 0;
		virtual float BeginJailbreak(cISC4Occupant* pJail) = 0;
		virtual bool EndJailbreak(cISC4Occupant* pJail) = 0;
		virtual bool GetJailbreakBuildings(SC4List<cISC4Occupant*>& sList) = 0;

		virtual uint32_t GetPoliceStationArrestCount(cISC4Occupant* pStation) = 0;
		virtual uint32_t GetPoliceStationCrimeCount(cISC4Occupant* pStation) = 0;
};