/*
 * gzcom-dll - an open-source DLL Plugin SDK for SimCity 4
 *
 * cIGZWinScrollbar.h
 *
 * Copyright (C) 2026 Nicholas Hayes
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

static const uint32_t GZIID_cIGZWinScrollbar = 0x61325a2d;

class cIGZBuffer;
class cIGZWin;
class cIGZWinSlider;
class cRZRect;

class cIGZWinScrollbar : public cIGZUnknown
{
public:
	enum class Orientation : int32_t
	{
		Horizontal = 1,
		Vertical = 2
	};

	virtual cIGZWin* AsIGZWin() = 0;
	virtual cIGZWinSlider* AsIGZWinSlider() = 0;

	virtual bool SetValue(int32_t value) = 0;
	virtual int32_t GetValue() const = 0;

	virtual bool SetMinMax(int32_t min, int32_t max) = 0;
	virtual bool GetMinMax(int32_t& min, int32_t& max) const = 0;
	virtual bool SetOrientation(Orientation value) = 0;
	virtual Orientation GetOrientation() const = 0;

	virtual bool SetPageSize(int32_t value) = 0;
	virtual int32_t GetPageSize() const = 0;
	virtual bool SetLineSize(int32_t value) = 0;
	virtual int32_t GetLineSize() const = 0;
	virtual void SetLineAndPageCount(uint32_t lineCount, uint32_t pageCount) = 0;
	virtual void GetLineAndPageCount(uint32_t& lineCount, uint32_t& pageCount) const = 0;

	virtual bool ImageSetup(cRZRect const& imageArea, cIGZBuffer* pImageBuffer, int32_t unknown1) = 0;
	virtual bool GetImage(cRZRect& imageArea, cIGZBuffer*& pImageBuffer, int32_t& unknown1) const = 0;
	virtual bool GetMinMaxInfo(cRZRect& rect) const = 0;
};