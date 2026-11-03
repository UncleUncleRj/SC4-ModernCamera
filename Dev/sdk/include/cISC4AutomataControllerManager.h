/*
 * gzcom-dll - an open-source DLL Plugin SDK for SimCity 4
 *
 * cISC4AutomataControllerManager.h
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

class cISC4AutomataController;
class cISC4NetworkOccupant;
class cISC4Occupant;
class cS3DVector3;
class SC4String;
struct tControllerTemplate;
struct tControllerTime;

template <typename T> class SC4List;

static const uint32_t GZIID_cISC4AutomataControllerManager = 0xca14c9fc;

class cISC4AutomataControllerManager : public cIGZUnknown
{
public:
	virtual bool Init() = 0;
	virtual bool Shutdown() = 0;

	virtual cISC4AutomataController* AddControllerFromTemplateName(cISC4Occupant* unknown1, SC4String const& unknown2, float const* position) = 0;
	virtual cISC4AutomataController* AddControllerFromTemplate(cISC4Occupant* unknown1, tControllerTemplate* unknown2, float const* position) = 0;

	virtual bool RemoveController(cISC4Occupant* unknown1, SC4String const& unknown2) = 0;
	virtual bool RemoveController(cISC4AutomataController* unknown1) = 0;

	virtual bool AddControllersFromOccupantGroup(SC4String const& unknown1, cISC4Occupant* unknown2, float* unknown3) = 0;
	virtual bool AddControllersFromOccupantGroup(uint32_t unknown1, cISC4Occupant* unknown2, float* unknown3) = 0;
	virtual bool RemoveControllersFromOccupantGroup(SC4String const& unknown1, cISC4Occupant* unknown2) = 0;
	virtual bool RemoveControllersFromOccupantGroup(uint32_t unknown1, cISC4Occupant* unknown2) = 0;

	virtual bool AddControllers(cISC4Occupant* unknown1) = 0;
	virtual bool RemoveControllers(cISC4Occupant* unknown1) = 0;

	virtual bool MoveControllers(cISC4Occupant* unknown1) = 0;
	virtual bool MoveController(cISC4AutomataController* unknown1) = 0;

	virtual cISC4AutomataController* GetController(cISC4Occupant* unknown1, SC4String const& unknown2) = 0;
	virtual cISC4AutomataController* GetController(cISC4Occupant* unknown1, tControllerTemplate* unknown2) = 0;
	virtual cISC4AutomataController* GetController(cS3DVector3 position, SC4String const& name, float radius) = 0; // Binary-verified at 0x516410: the vector is passed by value.

	virtual bool GetRelevantControllersByWorldPos(float x, float z, uint32_t controllerType, SC4List<cISC4AutomataController*>& list) = 0;
	virtual void ReleaseControllerList(SC4List<cISC4AutomataController*>& list) = 0;

	virtual cISC4AutomataController* GetCompositeAttractionEffect(cISC4Occupant* unknown1, cS3DVector3 const& unknown2, float* unknown3, float* unknown4) = 0;
	virtual cISC4AutomataController* GetCompositeAttractionCommotion(cISC4NetworkOccupant* unknown1) = 0;
	virtual tControllerTime* GetControllerTime() const = 0;
};