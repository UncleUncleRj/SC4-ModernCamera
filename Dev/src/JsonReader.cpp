#include "JsonReader.h"

#include <charconv>
#include <cstdint>

namespace
{
	// Deepest nesting of arrays and objects accepted, which bounds the recursion.
	constexpr int32_t kMaximumDepth = 64;

	// Reads one document. Each Parse function starts at the first character of its value and
	// stops just past its last.
	class JsonParser
	{
	public:
		/// <summary>
		/// Prepares to read a document.
		/// </summary>
		/// <param name="text">Document text.</param>
		explicit JsonParser(std::string_view text)
			: text(text)
		{
		}

		/// <summary>
		/// Reads the whole document.
		/// </summary>
		/// <param name="value">Receives the document's value.</param>
		/// <param name="error">Receives a description of the first problem.</param>
		/// <returns>True when the text is one valid value.</returns>
		bool Parse(JsonValue& value, std::string& error)
		{
			if (text.substr(0, 3) == "\xEF\xBB\xBF") {
				position = 3;
			}
			if (ParseValue(value, 0)) {
				SkipSpace();
				if (position == text.size()) {
					return true;
				}
				Fail("unexpected text after the document");
			}
			error = Describe();
			return false;
		}

	private:
		/// <summary>
		/// Records the first problem found.
		/// </summary>
		/// <param name="message">What went wrong.</param>
		/// <returns>False, for returning straight from a Parse function.</returns>
		bool Fail(const char* message)
		{
			if (!problem) {
				problem = message;
			}
			return false;
		}

		/// <summary>
		/// Describes the recorded problem and where it happened.
		/// </summary>
		/// <returns>The description.</returns>
		std::string Describe() const
		{
			size_t line = 1;
			size_t column = 1;
			for (size_t i = 0; i < position && i < text.size(); ++i) {
				if (text[i] == '\n') {
					++line;
					column = 1;
				}
				else {
					++column;
				}
			}
			return std::string(problem ? problem : "invalid JSON")
				+ " at line " + std::to_string(line) + ", column " + std::to_string(column);
		}

		// Skips the whitespace JSON allows between tokens.
		void SkipSpace()
		{
			while (position < text.size()
				&& (text[position] == ' ' || text[position] == '\t' || text[position] == '\n' || text[position] == '\r')) {
				++position;
			}
		}

		/// <summary>
		/// Reads any value.
		/// </summary>
		/// <param name="value">Receives the value.</param>
		/// <param name="depth">Number of arrays and objects around it.</param>
		/// <returns>True on success.</returns>
		bool ParseValue(JsonValue& value, int32_t depth)
		{
			SkipSpace();
			if (position >= text.size()) {
				return Fail("unexpected end of the document");
			}

			const char first = text[position];
			switch (first) {
			case '{':
				return ParseObject(value, depth);
			case '[':
				return ParseArray(value, depth);
			case '"':
				value.type = JsonValue::Type::String;
				return ParseString(value.text);
			case 't':
				value.type = JsonValue::Type::Boolean;
				value.boolean = true;
				return ParseWord("true");
			case 'f':
				value.type = JsonValue::Type::Boolean;
				value.boolean = false;
				return ParseWord("false");
			case 'n':
				value.type = JsonValue::Type::Null;
				return ParseWord("null");
			default:
				if (first == '-' || (first >= '0' && first <= '9')) {
					value.type = JsonValue::Type::Number;
					return ParseNumber(value.number);
				}
				return Fail("unexpected character");
			}
		}

		/// <summary>
		/// Reads one of the literal words.
		/// </summary>
		/// <param name="word">Word expected here.</param>
		/// <returns>True when the word is present.</returns>
		bool ParseWord(std::string_view word)
		{
			if (text.substr(position, word.size()) != word) {
				return Fail("unknown word");
			}
			position += word.size();
			return true;
		}

		// Returns whether the character here is a decimal digit.
		bool AtDigit() const
		{
			return position < text.size() && text[position] >= '0' && text[position] <= '9';
		}

		/// <summary>
		/// Reads a number in JSON's grammar, which is stricter than the C library's.
		/// </summary>
		/// <param name="number">Receives the number.</param>
		/// <returns>True on success.</returns>
		bool ParseNumber(double& number)
		{
			const size_t start = position;
			if (text[position] == '-') {
				++position;
			}
			if (position < text.size() && text[position] == '0') {
				++position;
			}
			else if (AtDigit()) {
				while (AtDigit()) {
					++position;
				}
			}
			else {
				return Fail("invalid number");
			}

			if (position < text.size() && text[position] == '.') {
				++position;
				if (!AtDigit()) {
					return Fail("invalid number");
				}
				while (AtDigit()) {
					++position;
				}
			}

			if (position < text.size() && (text[position] == 'e' || text[position] == 'E')) {
				++position;
				if (position < text.size() && (text[position] == '+' || text[position] == '-')) {
					++position;
				}
				if (!AtDigit()) {
					return Fail("invalid number");
				}
				while (AtDigit()) {
					++position;
				}
			}

			// from_chars ignores the locale, which the game may have changed.
			const char* end = text.data() + position;
			const std::from_chars_result result = std::from_chars(text.data() + start, end, number);
			if (result.ec != std::errc() || result.ptr != end) {
				position = start;
				return Fail("number out of range");
			}
			return true;
		}

		/// <summary>
		/// Reads four hexadecimal digits of a \u escape.
		/// </summary>
		/// <param name="code">Receives the code unit.</param>
		/// <returns>True on success.</returns>
		bool ParseHex(uint32_t& code)
		{
			code = 0;
			for (int32_t i = 0; i < 4; ++i) {
				if (position >= text.size()) {
					return Fail("invalid escape in a string");
				}
				const char digit = text[position];
				code <<= 4;
				if (digit >= '0' && digit <= '9') {
					code |= static_cast<uint32_t>(digit - '0');
				}
				else if (digit >= 'a' && digit <= 'f') {
					code |= static_cast<uint32_t>(digit - 'a' + 10);
				}
				else if (digit >= 'A' && digit <= 'F') {
					code |= static_cast<uint32_t>(digit - 'A' + 10);
				}
				else {
					return Fail("invalid escape in a string");
				}
				++position;
			}
			return true;
		}

		/// <summary>
		/// Appends a code point to a string as UTF-8.
		/// </summary>
		/// <param name="out">String to extend.</param>
		/// <param name="code">Code point, at most 0x10FFFF.</param>
		static void AppendUtf8(std::string& out, uint32_t code)
		{
			if (code < 0x80) {
				out.push_back(static_cast<char>(code));
			}
			else if (code < 0x800) {
				out.push_back(static_cast<char>(0xC0 | (code >> 6)));
				out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
			}
			else if (code < 0x10000) {
				out.push_back(static_cast<char>(0xE0 | (code >> 12)));
				out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
				out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
			}
			else {
				out.push_back(static_cast<char>(0xF0 | (code >> 18)));
				out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
				out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
				out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
			}
		}

		/// <summary>
		/// Reads a string, decoding its escapes.
		/// </summary>
		/// <param name="out">Receives the string in UTF-8.</param>
		/// <returns>True on success.</returns>
		bool ParseString(std::string& out)
		{
			out.clear();
			++position;
			while (position < text.size()) {
				const char character = text[position];
				if (character == '"') {
					++position;
					return true;
				}
				if (static_cast<unsigned char>(character) < 0x20) {
					return Fail("control character in a string");
				}
				++position;
				if (character != '\\') {
					out.push_back(character);
					continue;
				}

				if (position >= text.size()) {
					break;
				}
				const char escape = text[position++];
				switch (escape) {
				case '"':
					out.push_back('"');
					break;
				case '\\':
					out.push_back('\\');
					break;
				case '/':
					out.push_back('/');
					break;
				case 'b':
					out.push_back('\b');
					break;
				case 'f':
					out.push_back('\f');
					break;
				case 'n':
					out.push_back('\n');
					break;
				case 'r':
					out.push_back('\r');
					break;
				case 't':
					out.push_back('\t');
					break;
				case 'u': {
					uint32_t code = 0;
					if (!ParseHex(code)) {
						return false;
					}
					if (code >= 0xDC00 && code <= 0xDFFF) {
						return Fail("unpaired surrogate in a string");
					}
					if (code >= 0xD800 && code <= 0xDBFF) {
						uint32_t low = 0;
						if (text.substr(position, 2) != "\\u") {
							return Fail("unpaired surrogate in a string");
						}
						position += 2;
						if (!ParseHex(low)) {
							return false;
						}
						if (low < 0xDC00 || low > 0xDFFF) {
							return Fail("unpaired surrogate in a string");
						}
						code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
					}
					AppendUtf8(out, code);
					break;
				}
				default:
					--position;
					return Fail("invalid escape in a string");
				}
			}
			return Fail("unterminated string");
		}

		/// <summary>
		/// Reads an array.
		/// </summary>
		/// <param name="value">Receives the array.</param>
		/// <param name="depth">Number of arrays and objects around it.</param>
		/// <returns>True on success.</returns>
		bool ParseArray(JsonValue& value, int32_t depth)
		{
			if (depth >= kMaximumDepth) {
				return Fail("arrays and objects nested too deeply");
			}
			value.type = JsonValue::Type::Array;
			++position;
			SkipSpace();
			if (position < text.size() && text[position] == ']') {
				++position;
				return true;
			}

			for (;;) {
				value.items.emplace_back();
				if (!ParseValue(value.items.back(), depth + 1)) {
					return false;
				}
				SkipSpace();
				if (position < text.size() && text[position] == ',') {
					++position;
					continue;
				}
				if (position < text.size() && text[position] == ']') {
					++position;
					return true;
				}
				return Fail("expected , or ] in an array");
			}
		}

		/// <summary>
		/// Reads an object.
		/// </summary>
		/// <param name="value">Receives the object.</param>
		/// <param name="depth">Number of arrays and objects around it.</param>
		/// <returns>True on success.</returns>
		bool ParseObject(JsonValue& value, int32_t depth)
		{
			if (depth >= kMaximumDepth) {
				return Fail("arrays and objects nested too deeply");
			}
			value.type = JsonValue::Type::Object;
			++position;
			SkipSpace();
			if (position < text.size() && text[position] == '}') {
				++position;
				return true;
			}

			for (;;) {
				SkipSpace();
				if (position >= text.size() || text[position] != '"') {
					return Fail("expected a member name");
				}
				JsonMember member;
				if (!ParseString(member.name)) {
					return false;
				}
				SkipSpace();
				if (position >= text.size() || text[position] != ':') {
					return Fail("expected : after a member name");
				}
				++position;
				if (!ParseValue(member.value, depth + 1)) {
					return false;
				}
				value.members.push_back(std::move(member));

				SkipSpace();
				if (position < text.size() && text[position] == ',') {
					++position;
					continue;
				}
				if (position < text.size() && text[position] == '}') {
					++position;
					return true;
				}
				return Fail("expected , or } in an object");
			}
		}

		// Document text.
		std::string_view text;
		// Index of the next character to read.
		size_t position = 0;
		// First problem found, or null.
		const char* problem = nullptr;
	};
}

const JsonValue* JsonValue::Find(std::string_view name) const
{
	if (type != Type::Object) {
		return nullptr;
	}
	for (const JsonMember& member : members) {
		if (member.name == name) {
			return &member.value;
		}
	}
	return nullptr;
}

bool ParseJson(std::string_view text, JsonValue& value, std::string& error)
{
	value = JsonValue{};
	JsonParser parser(text);
	return parser.Parse(value, error);
}
