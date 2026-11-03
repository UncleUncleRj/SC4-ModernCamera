/*
 * gzcom-dll - an open-source DLL Plugin SDK for SimCity 4
 *
 * cIGZFont.h
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

class cIGZFontRenderer;
class cIGZString;
class cRZRect;
class cRZPoint;

class cIGZFont : public cIGZUnknown
{
public:
	enum class AntialiasingMode : int32_t
	{
		Unknown = 0,
		None = 1,
		Color = 2,
		Background = 3
	};

	enum class CharEncoding : uint32_t
	{
		// These values map to the FreeType 2 FT_ENCODING enumeration.
		// The Windows version of SC4 Rush Hour/Deluxe uses a different
		// font library that only supports Unicode.

		None = 0,				// FT_ENCODING_NONE
		MicrosoftSymbol = 1,	// FT_ENCODING_MS_SYMBOL
		Unicode = 2,			// FT_ENCODING_UNICODE
		OldLatin2 = 3,			// FT_ENCODING_OLD_LATIN_2
		ShiftJIS = 4,			// FT_ENCODING_SJIS
		PRC = 5,				// FT_ENCODING_PRC
		Big5 = 6,				// FT_ENCODING_BIG5
		Wansung = 7,			// FT_ENCODING_WANSUNG
		Johab = 8,				// FT_ENCODING_JOHAB
		AdobeStandard = 9,		// FT_ENCODING_ADOBE_STANDARD
		AdobeExpert = 10,		// FT_ENCODING_ADOBE_EXPERT
		AdobeCustom = 11,		// FT_ENCODING_ADOBE_CUSTOM
		AppleRoman = 12,		// FT_ENCODING_APPLE_ROMAN
	};

	enum Style : int32_t
	{
		Style_Normal = 0,
		Style_Bold = 1,
		Style_Italic = 2,
		Style_Underline = 4,
		Style_Strikethrough = 8,
		Style_Shadow = 16,
		Style_Unknown1 = 32,
	};

	enum class Type : uint32_t
	{
		MonoSpaced = 1,
		VariableWidth = 2,
	};

	enum class WordWrapMode : int32_t
	{
		Unknown1 = 1,
		Unknown2 = 2,
	};

	virtual bool Init() = 0;
	virtual bool Shutdown() = 0;

	virtual void GetName(cIGZString& name) = 0;
	virtual Style GetStyle() = 0;
	virtual bool GetStyle(Style mask) const = 0;
	virtual void SetStyle(Style mask, bool enabled) = 0;
	virtual Type GetType() = 0;
	virtual uint32_t GetSize() const = 0;
	virtual bool SetSize(uint32_t size) = 0;

	virtual uint32_t GetFontColor() = 0;
	virtual uint32_t GetFontBackgroundColor() = 0;
	virtual void SetFontColor(uint32_t color, uint32_t backgroundColor) = 0;

	virtual AntialiasingMode GetAntialiasing() = 0;
	virtual void SetAntialiasing(AntialiasingMode mode) = 0;
	virtual float GetAntialiasingGammaCorrection() = 0;
	virtual void SetAntialiasingGammaCorrection(float value) = 0;
	virtual float GetAntialiasingSharpness() = 0;
	virtual void SetAntialiasingSharpness(float value) = 0;

	virtual void GetFilePath(cIGZString& path) = 0;
	virtual void SetFilePath(cIGZString const& path) = 0;

	virtual void GetCodePage(int32_t& codePage) = 0;
	virtual void SetCodePage(int32_t codePage) = 0;
	virtual bool IsCodePageAvailable(int32_t codePage) = 0;
	virtual bool IsCharEncodingAvailable(CharEncoding encoding) = 0;
	virtual bool IsCharAvailable(void* buffer, uint32_t bufferLength) = 0;
	virtual bool PreloadChars(void* buffer, uint32_t bufferLength) = 0;

	virtual void GetScaling(float& param_1, float& param_2) = 0;
	virtual void SetScaling(float param_1, float param_2) = 0;
	virtual void GetAdvanceScaling(float& param_1, float& param_2) = 0;
	virtual void SetAdvanceScaling(float param_1, float param_2) = 0;
	virtual void GetSpacingAdjustment(int32_t& param_1, int32_t& param_2) = 0;
	virtual void SetSpacingAdjustment(int32_t param_1, int32_t param_2) = 0;
	virtual int32_t GetLineHeight() = 0;
	virtual int32_t CharWidth(void* buffer, uint32_t bufferLength) = 0;
	virtual int32_t CharHeight(void* buffer, uint32_t bufferLength) = 0;
	virtual int32_t GetSizeAboveBaseline() = 0;
	virtual int32_t GetSizeBelowBaseline() = 0;
	virtual int32_t GetAdvanceWidth(void* buffer, uint32_t bufferLength) = 0;

	virtual uint32_t CalculateWordsToFitInWidth(void* buffer, uint32_t bufferLength, uint32_t width, uint32_t* stringWidth, WordWrapMode mode) = 0;
	virtual uint32_t CalculateCharsToFitInWidth(void* buffer, uint32_t bufferLength, uint32_t width, uint32_t* stringWidth) = 0;
	virtual uint32_t CalculateNumberOfLines(void* buffer, uint32_t bufferLength, uint32_t param_3) = 0;
	virtual uint32_t CalculateWidthOfLines(void* buffer, uint32_t bufferLength, uint32_t param_3) = 0;
	virtual void CalculateTextArea(void* buffer, uint32_t bufferLength, cRZRect& bounds, bool multiLine, bool multiLineHeightOnly) = 0;
	virtual void CalculateTextArea(void* buffer, uint32_t bufferLength, cRZRect& bounds, int32_t param_4, int32_t param_5, int32_t param_6) = 0;
	virtual void TextArea(void* buffer, uint32_t bufferLength, cRZRect& area) = 0;
	virtual uint32_t GetStringWidth(void* buffer, uint32_t bufferLength) = 0;
	virtual uint32_t GetStringVisibleWidth(void* buffer, uint32_t bufferLength, uint32_t& stringLength) = 0;
	virtual uint32_t FormatForEllipsis(cIGZString& text, cIGZString const& ellipsis, cRZRect const& area, bool skipCalculations) = 0;
	virtual bool DrawTextLine(void* renderState, int32_t param_2, int32_t param_3, void* buffer, uint32_t bufferLength, uint32_t param_6, uint32_t param_7, cRZRect const* param_8, cRZPoint* param_9) = 0;
	virtual bool DrawTextParagraph(void* renderState, cRZRect const& param_2, void* buffer, uint32_t bufferLength, int32_t param_5, uint32_t param_6, uint32_t param_7, cRZRect const* param_8, cRZPoint* param_9) = 0;

	virtual cIGZFontRenderer* GetRenderer() = 0;
	virtual void SetRenderer(cIGZFontRenderer* pRenderer) = 0;
	virtual bool LockSurfaceForFontUse(void* renderState) = 0;
	virtual bool UnlockSurfaceForFontUse(void* renderState) = 0;
};