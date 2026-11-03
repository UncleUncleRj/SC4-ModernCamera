#pragma once

#include <string>
#include <string_view>
#include <vector>

struct JsonMember;

// A value read from a JSON document.
struct JsonValue
{
	// Kinds of JSON value.
	enum class Type
	{
		Null,
		Boolean,
		Number,
		String,
		Array,
		Object,
	};

	// Kind of value held.
	Type type = Type::Null;
	// Value of a boolean.
	bool boolean = false;
	// Value of a number.
	double number = 0.0;
	// Value of a string, in UTF-8.
	std::string text;
	// Items of an array.
	std::vector<JsonValue> items;
	// Members of an object, in document order.
	std::vector<JsonMember> members;

	/// <summary>
	/// Returns a member of an object.
	/// </summary>
	/// <param name="name">Member name.</param>
	/// <returns>The first member with that name, or null when this is not an object or has none.</returns>
	const JsonValue* Find(std::string_view name) const;
};

// One named member of a JSON object.
struct JsonMember
{
	// Member name, in UTF-8.
	std::string name;
	// Member value.
	JsonValue value;
};

/// <summary>
/// Reads a JSON document (RFC 8259). A leading UTF-8 byte order mark is ignored.
/// </summary>
/// <param name="text">Document text in UTF-8.</param>
/// <param name="value">Receives the document's value.</param>
/// <param name="error">Receives a description of the first problem, with its line and column.</param>
/// <returns>True when the whole text is one valid JSON value.</returns>
bool ParseJson(std::string_view text, JsonValue& value, std::string& error);
