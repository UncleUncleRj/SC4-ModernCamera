/*
 * gzcom-dll - an open-source DLL Plugin SDK for SimCity 4
 *
 * cISC424HourClock.h
 *
 * Copyright (C) 2016 Nelson Gomez
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

class SC4Percentage;

static const uint32_t GZIID_cISC424HourClock = 0x681bd53a;

class cISC424HourClock : public cIGZUnknown
{
	public:
		virtual bool Init(void) = 0;
		virtual bool Shutdown(void) = 0;

		virtual int32_t SetVisualTimeMode(int32_t nTimeMode) = 0;
		virtual int32_t GetVisualTimeMode(void) = 0;

		virtual SC4Percentage GetTimePercentage() = 0; // Binary-verified at 0x445420: returned through a hidden pointer.
		virtual int32_t SetTimePercentage(float& fPercentage) = 0;
		virtual SC4Percentage GetVisualTimePercentage() = 0; // Binary-verified at 0x445450: returned through a hidden pointer.
		
		virtual uint32_t GetHour(void) = 0;
		virtual uint32_t SetHour(uint32_t dwHour) = 0;
		virtual uint32_t GetVisualHour(void) = 0;
		
		virtual uint32_t GetMinute(void) = 0;
		virtual uint32_t SetMinute(uint32_t dwMinute) = 0;
		virtual uint32_t GetVisualMinute(void) = 0;

		virtual uint32_t GetSeconds(void) = 0;
		virtual uint32_t SetSeconds(uint32_t dwSeconds) = 0;
		virtual uint32_t GetVisualSeconds(void) = 0;

		virtual uint32_t GetEventHour(bool bUnknown) = 0;
		
		virtual uint32_t GetTimeMagnification(float& fMagnificationOut) = 0;
		virtual bool SetTimeMagnification(float fMagnification) = 0; // Binary-verified at 0x445050: only AL is set.
		
		virtual uint32_t GetTimeMagnificationMinMax(float& fMinOut, float& fMaxOut) = 0;
		virtual bool SetTimeMagnificationMinMax(float fMin, float fMax) = 0;

		virtual bool Pause(bool bUnknown) = 0;
		virtual bool IsPaused(void) = 0;

		virtual bool HiddenPause(bool bUnknown) = 0;
		virtual bool IsHiddenPaused(void) = 0;

		virtual bool IsNight(void) = 0;
		virtual void EnableDayNightTransitionNotifications(bool bToggle) = 0;
};