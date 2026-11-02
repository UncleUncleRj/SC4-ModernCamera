#include "SC4CityIcons.h"

#include "GZServPtrs.h"
#include "Logger.h"
#include "SC4CityIconRotation.h"
#include "cIS3DModelInstance.h"
#include "cISC43DRender.h"
#include "cISC4App.h"
#include "cISC4City.h"
#include "cISC4Occupant.h"
#include "cISC4OccupantManager.h"

#include <algorithm>
#include <cstring>
#include <string>

// The models an occupant shows. The SDK only forward-declares this class. Binary-verified on
// the 641 executable: vtable 0xAA4468, created by cISC4Occupant::GetOrCreatePlaceableObject
// (0x5E91C0), with these slots used the same way by the model maker at 0x495430. Only the
// slots this file needs are declared.
class cISC43DPlaceableObject : public cIGZUnknown
{
public:
	// Slot 3 (0x5EBBF0): returns the number of models.
	virtual int32_t GetModelCount() = 0;
	// Slot 4 (0x5EBC00): copies up to count models without adding references, sets count to
	// the number copied, and returns it.
	virtual uint32_t GetModels(cIS3DModelInstance** models, uint32_t& count) = 0;
};

namespace
{
	// Native type of prop occupants (binary-verified: the model maker's prop case at 0x494390).
	constexpr uint32_t kPropOccupantType = 0xA823821E;
	// Occupant group of the icons over buildings. The stock game has four, read from
	// SimCity_1.dat: Zot_NoPower, Zot_NoWater, Zot_NoWork, and Zot_NoCar.
	constexpr uint32_t kIconOccupantGroup = 0x5001;
	// Most models read from one icon. Each stock icon has one.
	constexpr uint32_t kMaxModelsPerIcon = 4;
}

void SC4CityIcons::TrackExistingIcons()
{
	cISC4AppPtr app;
	cISC4City* city = app ? app->GetCity() : nullptr;
	cISC4OccupantManager* occupantManager = city ? city->GetOccupantManager() : nullptr;
	if (!occupantManager) {
		Logger::GetInstance().WriteLine(LogLevel::Warning, "City Icons: occupant manager unavailable at city load.");
		return;
	}

	occupantManager->IterateOccupants(TrackIterator, this, nullptr, nullptr, kPropOccupantType);
	Logger::GetInstance().WriteLine(
		LogLevel::Info,
		"City Icons: tracking " + std::to_string(icons.size()) + " at city load.");
}

void SC4CityIcons::OnOccupantInserted(cISC4Occupant* occupant, uint32_t occupantType)
{
	if (occupant && occupantType == kPropOccupantType) {
		Track(occupant);
	}
}

void SC4CityIcons::OnOccupantRemoved(cISC4Occupant* occupant, uint32_t occupantType)
{
	if (occupantType != kPropOccupantType) {
		return;
	}

	const auto found = std::find(icons.begin(), icons.end(), occupant);
	if (found == icons.end()) {
		return;
	}

	*found = icons.back();
	icons.pop_back();
	occupant->Release();
}

void SC4CityIcons::Clear()
{
	for (cISC4Occupant* icon : icons) {
		icon->Release();
	}
	icons.clear();
}

size_t SC4CityIcons::GetCount() const
{
	return icons.size();
}

void SC4CityIcons::ApplyRotation(const float rotation[3][3], cISC43DRender* renderer, bool gridRebuildPending)
{
	for (cISC4Occupant* icon : icons) {
		cISC43DPlaceableObject* placeable = icon->GetPlaceableObject();
		if (!placeable) {
			continue;
		}

		cIS3DModelInstance* models[kMaxModelsPerIcon];
		uint32_t modelCount = kMaxModelsPerIcon;
		placeable->GetModels(models, modelCount);

		for (uint32_t i = 0; i < modelCount; ++i) {
			cS3DTransform transform{};
			models[i]->GetTransform(transform);
			if (IsSameCityIconRotation(transform.rotation, rotation)) {
				continue;
			}

			std::memcpy(transform.rotation, rotation, sizeof(transform.rotation));
			transform.flags |= kS3DTransformHasRotation;
			models[i]->SetTransform(transform);

			// Moving tells the renderer the model's bounds changed, so it redraws both areas
			// and fixes a new icon's bounds before its first draw. A grid rebuild and full
			// redraw already do that. MoveModel (0x7C9220) inserts a model with no grid entry,
			// which would show an icon the game has hidden, so those are left alone.
			if (!gridRebuildPending && models[i]->GetCustomData()) {
				renderer->MoveModel(models[i]);
			}
		}
	}
}

void SC4CityIcons::Track(cISC4Occupant* occupant)
{
	if (occupant->IsOccupantGroup(kIconOccupantGroup)
		&& std::find(icons.begin(), icons.end(), occupant) == icons.end()) {
		occupant->AddRef();
		icons.push_back(occupant);
	}
}

bool SC4CityIcons::TrackIterator(cISC4Occupant* occupant, void* data)
{
	static_cast<SC4CityIcons*>(data)->Track(occupant);
	return true;
}
