#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

class cISC43DRender;
class cISC4Occupant;

/// <summary>
/// Keeps the icons the game floats over buildings, such as No Power and No Work, looking the
/// way the Classic camera shows them at any camera angle. They are 3D models baked for one
/// view direction, so the camera's yaw and pitch otherwise fold them out of shape.
/// All members run on the game thread.
/// </summary>
class SC4CityIcons
{
public:
	/// <summary>
	/// Collects the icons already in the loaded city. Run once when the city finishes loading.
	/// </summary>
	void TrackExistingIcons();
	/// <summary>
	/// Starts tracking an occupant the game inserted, if it is an icon.
	/// </summary>
	/// <param name="occupant">Inserted occupant.</param>
	/// <param name="occupantType">Native type of the occupant.</param>
	void OnOccupantInserted(cISC4Occupant* occupant, uint32_t occupantType);
	/// <summary>
	/// Stops tracking an occupant the game removed.
	/// </summary>
	/// <param name="occupant">Removed occupant.</param>
	/// <param name="occupantType">Native type of the occupant.</param>
	void OnOccupantRemoved(cISC4Occupant* occupant, uint32_t occupantType);
	/// <summary>
	/// Releases every tracked icon. Run before the city or the game's services shut down.
	/// </summary>
	void Clear();
	/// <summary>
	/// Returns the number of tracked icons.
	/// </summary>
	/// <returns>The number of icon occupants being kept facing the camera.</returns>
	size_t GetCount() const;
	/// <summary>
	/// Gives every icon model the supplied rotation. Models are checked each frame because
	/// the game rebuilds them on its own schedule when the zoom or rotation changes, which
	/// resets their rotation.
	/// </summary>
	/// <param name="rotation">Model rotation, row by row.</param>
	/// <param name="renderer">Active renderer.</param>
	/// <param name="gridRebuildPending">True when a render-grid rebuild and full redraw will follow this call.</param>
	void ApplyRotation(const float rotation[3][3], cISC43DRender* renderer, bool gridRebuildPending);

private:
	/// <summary>
	/// Adds an occupant to the tracked list if it is an icon and is not already listed.
	/// </summary>
	/// <param name="occupant">Prop occupant to consider.</param>
	void Track(cISC4Occupant* occupant);
	/// <summary>
	/// Occupant-manager callback that considers one prop occupant.
	/// </summary>
	/// <param name="occupant">Prop occupant from the iteration.</param>
	/// <param name="data">The SC4CityIcons instance.</param>
	/// <returns>True to continue the iteration.</returns>
	static bool TrackIterator(cISC4Occupant* occupant, void* data);

	// Tracked icons, each holding one reference. Raw pointers, so that nothing here calls
	// into the game when the DLL unloads after an abortive quit.
	std::vector<cISC4Occupant*> icons;
};
