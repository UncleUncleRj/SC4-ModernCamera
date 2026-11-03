#include "SC4BuildingShapeCatalog.h"

#include "JsonReader.h"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace
{
	// Type of the game's model resources, which a shape's model key must name.
	constexpr uint32_t kModelResourceType = 0x5AD0E817;
	// Digits of a model instance that select its zoom stage and rotation.
	constexpr uint32_t kZoomAndRotationDigits = 0x00000FF0;
	// Document format this code reads.
	constexpr double kFormatVersion = 1.0;
	// Largest coordinate accepted, in world units. The largest lot is 16 tiles of 16 units.
	constexpr double kCoordinateLimit = 256.0;

	/// <summary>
	/// Returns the catalog key of a model.
	/// </summary>
	/// <param name="group">Group of the model resource.</param>
	/// <param name="instance">Instance of the model resource, at any zoom stage and rotation.</param>
	/// <returns>The key.</returns>
	uint64_t MakeKey(uint32_t group, uint32_t instance)
	{
		return (static_cast<uint64_t>(group) << 32) | (instance & ~kZoomAndRotationDigits);
	}

	/// <summary>
	/// Reads a hexadecimal number written with a 0x prefix.
	/// </summary>
	/// <param name="text">Text of the number.</param>
	/// <param name="value">Receives the number.</param>
	/// <returns>True when the whole text is such a number.</returns>
	bool ParseHex(std::string_view text, uint32_t& value)
	{
		if (text.size() < 3 || text[0] != '0' || (text[1] != 'x' && text[1] != 'X')) {
			return false;
		}
		const char* end = text.data() + text.size();
		const std::from_chars_result result = std::from_chars(text.data() + 2, end, value, 16);
		return result.ec == std::errc() && result.ptr == end;
	}

	/// <summary>
	/// Reads a resource key written as 0xTYPE-0xGROUP-0xINSTANCE.
	/// </summary>
	/// <param name="text">Text of the key.</param>
	/// <param name="type">Receives the type.</param>
	/// <param name="group">Receives the group.</param>
	/// <param name="instance">Receives the instance.</param>
	/// <returns>True when the text is such a key.</returns>
	bool ParseModelKey(std::string_view text, uint32_t& type, uint32_t& group, uint32_t& instance)
	{
		const size_t first = text.find('-');
		const size_t second = first == std::string_view::npos ? std::string_view::npos : text.find('-', first + 1);
		if (second == std::string_view::npos) {
			return false;
		}
		return ParseHex(text.substr(0, first), type)
			&& ParseHex(text.substr(first + 1, second - first - 1), group)
			&& ParseHex(text.substr(second + 1), instance);
	}

	/// <summary>
	/// Reads an array holding a fixed number of coordinates.
	/// </summary>
	/// <param name="value">Array to read, which may be absent.</param>
	/// <param name="out">Receives the coordinates.</param>
	/// <param name="count">Number of coordinates expected.</param>
	/// <returns>True when the array holds exactly that many usable coordinates.</returns>
	bool ReadCoordinates(const JsonValue* value, float* out, size_t count)
	{
		if (!value || value->type != JsonValue::Type::Array || value->items.size() != count) {
			return false;
		}
		for (size_t i = 0; i < count; ++i) {
			const JsonValue& item = value->items[i];
			if (item.type != JsonValue::Type::Number || !std::isfinite(item.number) || std::abs(item.number) > kCoordinateLimit) {
				return false;
			}
			out[i] = static_cast<float>(item.number);
		}
		return true;
	}

	/// <summary>
	/// Reads one part of a shape.
	/// </summary>
	/// <param name="value">The part's object.</param>
	/// <param name="part">Receives the part.</param>
	/// <returns>Empty on success, otherwise what is wrong with the part.</returns>
	std::string ReadPart(const JsonValue& value, SC4ShapePart& part)
	{
		if (value.type != JsonValue::Type::Object) {
			return "is not an object";
		}

		const JsonValue* shape = value.Find("shape");
		if (!shape || shape->type != JsonValue::Type::String) {
			return "has no shape";
		}
		if (shape->text == "box") {
			part.kind = SC4ShapePartKind::Box;
		}
		else if (shape->text == "hipRoof") {
			part.kind = SC4ShapePartKind::HipRoof;
		}
		else if (shape->text == "ground") {
			part.kind = SC4ShapePartKind::Ground;
		}
		else {
			return "has an unknown shape, '" + shape->text + "'";
		}

		const JsonValue* bounds = value.Find("bounds");
		if (!bounds
			|| !ReadCoordinates(bounds->Find("min"), part.minimum, 3)
			|| !ReadCoordinates(bounds->Find("max"), part.maximum, 3)) {
			return "needs bounds with a min and a max of three numbers each";
		}
		if (part.minimum[0] >= part.maximum[0] || part.minimum[2] >= part.maximum[2]) {
			return "has bounds with no width or no depth";
		}
		if (part.kind != SC4ShapePartKind::Ground && part.minimum[1] >= part.maximum[1]) {
			return "has bounds with no height";
		}
		if (part.kind != SC4ShapePartKind::HipRoof) {
			return {};
		}

		if (const JsonValue* top = value.Find("top")) {
			if (!ReadCoordinates(top->Find("min"), part.topMinimum, 2) || !ReadCoordinates(top->Find("max"), part.topMaximum, 2)) {
				return "needs a top with a min and a max of two numbers each";
			}
		}
		else {
			const JsonValue* inset = value.Find("inset");
			float amount[2] = {};
			if (inset && inset->type == JsonValue::Type::Number && std::isfinite(inset->number) && std::abs(inset->number) <= kCoordinateLimit) {
				amount[0] = static_cast<float>(inset->number);
				amount[1] = amount[0];
			}
			else if (!ReadCoordinates(inset, amount, 2)) {
				return "needs a top, or an inset of one or two numbers";
			}
			if (amount[0] < 0.0f || amount[1] < 0.0f) {
				return "has a negative inset";
			}
			// An inset past the middle makes the slopes meet in a ridge or a point.
			const float middleX = 0.5f * (part.minimum[0] + part.maximum[0]);
			const float middleZ = 0.5f * (part.minimum[2] + part.maximum[2]);
			part.topMinimum[0] = (std::min)(part.minimum[0] + amount[0], middleX);
			part.topMaximum[0] = (std::max)(part.maximum[0] - amount[0], middleX);
			part.topMinimum[1] = (std::min)(part.minimum[2] + amount[1], middleZ);
			part.topMaximum[1] = (std::max)(part.maximum[2] - amount[1], middleZ);
		}

		if (part.topMinimum[0] > part.topMaximum[0] || part.topMinimum[1] > part.topMaximum[1]
			|| part.topMinimum[0] < part.minimum[0] || part.topMaximum[0] > part.maximum[0]
			|| part.topMinimum[1] < part.minimum[2] || part.topMaximum[1] > part.maximum[2]) {
			return "has a top outside its bounds";
		}
		return {};
	}
}

bool SC4BuildingShapeCatalog::Load(std::string_view text, std::vector<std::string>& problems)
{
	shapes.clear();
	JsonValue root;
	std::string error;
	if (!ParseJson(text, root, error)) {
		problems.push_back(error);
		return false;
	}

	const JsonValue* version = root.Find("formatVersion");
	if (!version || version->type != JsonValue::Type::Number || version->number != kFormatVersion) {
		problems.push_back("formatVersion must be 1");
		return false;
	}
	const JsonValue* models = root.Find("models");
	if (!models || models->type != JsonValue::Type::Array) {
		problems.push_back("the document has no models list");
		return false;
	}

	for (size_t m = 0; m < models->items.size(); ++m) {
		const JsonValue& entry = models->items[m];
		std::string where = "model " + std::to_string(m + 1);
		const JsonValue* key = entry.Find("model");
		uint32_t type = 0;
		uint32_t group = 0;
		uint32_t instance = 0;
		if (!key || key->type != JsonValue::Type::String || !ParseModelKey(key->text, type, group, instance)) {
			problems.push_back(where + ": model must be written as 0xTYPE-0xGROUP-0xINSTANCE");
			continue;
		}
		where += " (" + key->text + ")";
		if (type != kModelResourceType) {
			problems.push_back(where + ": the type must be 0x5AD0E817, a model");
			continue;
		}

		SC4BuildingShape shape;
		shape.group = group;
		shape.baseInstance = instance & ~kZoomAndRotationDigits;
		const JsonValue* name = entry.Find("name");
		shape.name = name && name->type == JsonValue::Type::String ? name->text : key->text;

		const JsonValue* parts = entry.Find("parts");
		if (!parts || parts->type != JsonValue::Type::Array) {
			problems.push_back(where + ": has no parts list");
			continue;
		}
		for (size_t p = 0; p < parts->items.size(); ++p) {
			SC4ShapePart part;
			const std::string problem = ReadPart(parts->items[p], part);
			if (problem.empty()) {
				shape.parts.push_back(part);
				continue;
			}
			const JsonValue* partName = parts->items[p].Find("name");
			const std::string label = partName && partName->type == JsonValue::Type::String
				? "'" + partName->text + "'"
				: std::to_string(p + 1);
			problems.push_back(where + ", part " + label + ": " + problem);
		}
		if (shape.parts.empty()) {
			problems.push_back(where + ": has no usable parts");
			continue;
		}

		if (!shapes.try_emplace(MakeKey(group, instance), std::move(shape)).second) {
			problems.push_back(where + ": listed more than once; the first entry is used");
		}
	}
	return true;
}

void SC4BuildingShapeCatalog::Clear()
{
	shapes.clear();
}

const SC4BuildingShape* SC4BuildingShapeCatalog::Find(uint32_t group, uint32_t instance) const
{
	const auto found = shapes.find(MakeKey(group, instance));
	return found != shapes.end() ? &found->second : nullptr;
}

size_t SC4BuildingShapeCatalog::GetCount() const
{
	return shapes.size();
}
