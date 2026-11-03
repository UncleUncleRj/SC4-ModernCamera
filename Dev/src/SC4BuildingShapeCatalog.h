#pragma once

#include "SC4BuildingShape.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

/// <summary>
/// The building shapes listed in the plugin's building-shapes.json. Each gives one building its
/// true shape, which the building composer paints the building's pictures onto at free camera
/// angles. Shapes are written by hand for each building, measured against its pictures.
/// </summary>
class SC4BuildingShapeCatalog
{
public:
	/// <summary>
	/// Replaces the catalog with the shapes a document describes. Entries with problems are left out.
	/// </summary>
	/// <param name="text">Text of a building-shapes document.</param>
	/// <param name="problems">Receives one line for each problem found.</param>
	/// <returns>False when the document as a whole could not be read.</returns>
	bool Load(std::string_view text, std::vector<std::string>& problems);
	/// <summary>
	/// Empties the catalog.
	/// </summary>
	void Clear();
	/// <summary>
	/// Returns the shape of a model.
	/// </summary>
	/// <param name="group">Group of the model resource.</param>
	/// <param name="instance">Instance of the model resource, at any zoom stage and rotation.</param>
	/// <returns>The shape, or null when the catalog has none for the model.</returns>
	const SC4BuildingShape* Find(uint32_t group, uint32_t instance) const;
	/// <summary>
	/// Returns the number of shapes held.
	/// </summary>
	/// <returns>The count.</returns>
	size_t GetCount() const;

private:
	// Shapes by group in the high half and base instance in the low half.
	std::unordered_map<uint64_t, SC4BuildingShape> shapes;
};
