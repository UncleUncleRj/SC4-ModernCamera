/*
 * gzcom-dll - an open-source DLL Plugin SDK for SimCity 4
 *
 * cIGZWinSlider.h
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

static const uint32_t GZIID_cIGZWinSlider = 0x21325207;

class cIGZBuffer;
class cIGZWin;
class cRZRect;

class cIGZWinSlider : public cIGZUnknown
{
public:
	enum class tSliderWindowDirection : int32_t
	{
		Horizontal = 1,
		Vertical = 2
	};

	virtual cIGZWin* AsIGZWin() = 0;

	virtual int32_t GetValue() const = 0;
	virtual bool SetValue(int32_t value) = 0;

	virtual void SetMinimumValue(int32_t value) = 0;
	virtual int32_t GetMinimumValue() const = 0;

	virtual void SetMaximumValue(int32_t value) = 0;
	virtual int32_t GetMaximumValue() const = 0;

	virtual void SetDirection(tSliderWindowDirection direction) = 0;
	virtual void GetDirection(tSliderWindowDirection& direction) const = 0;

	virtual bool SetImage(cIGZBuffer* pImageBuffer, cRZRect const& imageArea) = 0;
	virtual bool GetImage(cIGZBuffer*& pImageBuffer, cRZRect& imageArea) = 0;
};