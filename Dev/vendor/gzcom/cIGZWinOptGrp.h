/*
 * gzcom-dll - an open-source DLL Plugin SDK for SimCity 4
 *
 * cIGZWinOptGrp.h
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

static const uint32_t GZIID_cIGZWinOptGrp = 0xa1336cc0;

class cIGZBuffer;
class cIGZFont;
class cIGZString;
class cIGZWin;
class cIGZWinBtn;
class cRZRect;

class cIGZWinOptGrp : public cIGZUnknown
{
public:
	enum class AutoFillType : int32_t
	{
		Auto = 0,
		Manual = 1
	};

	enum class tOptGrpFontColorIdx : int32_t
	{
		LabelFontColorNormal = 0,
		LabelFontColorDisabled = 1,
		ButtonFontColorNormal = 2,
		ButtonFontColorHighlighted = 3,
		ButtonFontColorDisabled = 4,
	};

	enum OptGrpFlag : int32_t
	{
		OptGrpFlag_MainCaption = 0x1,
		OptGrpFlag_ItemCaption = 0x2,
		OptGrpFlag_DrawFill = 0x4,
		OptGrpFlag_Outline = 0x8,
	};

	virtual cIGZWin* AsIGZWin() = 0;

	virtual bool GetOptGrpFlag(OptGrpFlag flags) const = 0;
	virtual bool SetOptGrpFlag(OptGrpFlag flags, bool enabled) = 0;
	virtual bool SetGutters(uint8_t unknown1, uint8_t unknown2) = 0;
	virtual bool GetGutters(int32_t& unknown1, int32_t& unknown2) const = 0;

	virtual bool SetLabelFont(cIGZFont* pFont) = 0;
	virtual bool SetBtnFont(cIGZFont* pFont) = 0;
	virtual bool SetFontColor(tOptGrpFontColorIdx index, uint32_t color) = 0;
	virtual bool GetFontColor(tOptGrpFontColorIdx index, uint32_t& color) const = 0;
	virtual bool ClearFontColor(tOptGrpFontColorIdx index) = 0;

	virtual uint32_t GetValue() const = 0;
	virtual bool SetValue(uint32_t unknown1) = 0;

	virtual bool OptBtnAdd(uint32_t id, cIGZString const& caption, uint32_t unknown1, cIGZBuffer* imageBuffer, cRZRect const& imageArea) = 0;
	virtual bool OptBtnDelete(uint32_t id) = 0;
	virtual bool OptBtnMoveTo(uint32_t id, int32_t x, int32_t y) = 0;
	virtual bool OptBtnSize(uint32_t id, int32_t width, int32_t height) = 0;

	virtual bool GetOptBtn(uint32_t id, cIGZWinBtn** ppBtn) = 0;
	virtual bool SetBackBMP(cIGZBuffer* buffer) = 0;
	virtual bool SetOutlineColors(uint32_t unknown1, uint32_t unknown2, uint32_t unknown3, uint32_t unknown4) = 0;
	virtual bool GetOutlineColors(uint32_t& unknown1, uint32_t& unknown2, uint32_t& unknown3, uint32_t& unknown4) const = 0;
	virtual bool SetBtnCLSID(uint32_t clsid) = 0;

	virtual bool AutoSize() = 0;
	virtual bool LayoutW(int32_t unknown1) = 0;
	virtual bool LayoutH(int32_t unknown1) = 0;
	virtual bool GetMinMaxInfo(cRZRect& rect) = 0;

	virtual void SetAutoFillType(AutoFillType type, int32_t unknown2) = 0;
	virtual bool GetAutoFillType(AutoFillType& type, int32_t& unknown2) const = 0;
};