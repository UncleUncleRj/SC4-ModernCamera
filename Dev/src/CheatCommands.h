#pragma once

#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

/// <summary>
/// Names and parsing for the Modern Camera codes typed in SC4's cheat console. SC4 sends the
/// typed line to the plugin when any registered name is a prefix of it (binary-verified:
/// cGZCheatCodeManager::DoesCheatCodeMatch at 0x92184B returns the first registered prefix),
/// so the plugin decides which command was meant by the whole first word.
/// </summary>
namespace CheatCommands
{
	/// <summary>One action a Modern Camera code requests.</summary>
	enum class Command : uint8_t
	{
		// Switches between Modern and Classic.
		ToggleMode,
		// Resets the camera, as Reset Camera Location does.
		Fix,
		// Writes the camera diagnostics to the log.
		Dump,
		// Restores every setting to its default.
		Defaults,
		// Turns WASD movement on or off.
		WASD,
		// Turns Q/E rotation on or off.
		QE,
		// Switches the mouse wheel between smooth and stepped zoom.
		Zoom,
		// Turns the bookmark hotkeys on or off.
		Bookmarks,
		// Swaps the directions Q and E turn the camera.
		InvertQE,
		// Reverses vertical mouse rotation.
		InvertM3,
		// Reverses the mouse wheel zoom.
		InvertWheel,
		// Switches what releasing Alt does: restore the Modern view or keep the Classic one.
		AltRestore,
		// Turns the news ticker update notices on or off.
		UpdateNotices,
		// Sets the keyboard panning speed.
		PanSpeed,
		// Sets the Shift panning boost.
		ShiftBoost,
		// Sets the Q/E rotation speed.
		QESpeed,
		// Sets the mouse rotation sensitivity.
		M3Speed,
		// Sets the wheel zoom sensitivity.
		ZoomSpeed,
		// Turns Write Logs on or off.
		Log,
	};

	/// <summary>Outcome of reading one console line.</summary>
	enum class ParseResult : uint8_t
	{
		// The line belongs to SC4 or another plugin.
		NotOurs,
		// The first word starts with a Modern Camera prefix but names no command.
		Unknown,
		// The first word names a command.
		Matched,
	};

	// Prefix of every code, which keeps the names apart from other plugins' codes.
	inline constexpr std::string_view kLongPrefix = "ModernCamera";
	// Shorter alias of kLongPrefix that reaches the same commands.
	inline constexpr std::string_view kShortPrefix = "SC4MC";
	// Plugin-owned cheat IDs, one range for each prefix. SC4 rejects only duplicate names
	// (binary-verified: RegisterCheatCode at 0x921B38), so unique IDs are this plugin's job.
	inline constexpr uint32_t kLongIdBase = 0x3D0C4E00;
	inline constexpr uint32_t kShortIdBase = 0x3D0C4F00;

	/// <summary>A command and the part of its name that follows the prefix.</summary>
	struct Definition
	{
		// Command the name requests.
		Command command;
		// Name after the prefix; empty for the bare prefix.
		std::string_view suffix;
	};

	// Every command, in the order of its cheat ID within each range.
	inline constexpr std::array kDefinitions = {
		Definition{ Command::ToggleMode, "" },
		Definition{ Command::Fix, "Fix" },
		Definition{ Command::Dump, "Dump" },
		Definition{ Command::Defaults, "Defaults" },
		Definition{ Command::WASD, "WASD" },
		Definition{ Command::QE, "QE" },
		Definition{ Command::Zoom, "Zoom" },
		Definition{ Command::Bookmarks, "Bookmarks" },
		Definition{ Command::InvertQE, "InvertQE" },
		Definition{ Command::InvertM3, "InvertM3" },
		Definition{ Command::InvertWheel, "InvertWheel" },
		Definition{ Command::AltRestore, "AltRestore" },
		Definition{ Command::UpdateNotices, "UpdateNotices" },
		Definition{ Command::PanSpeed, "PanSpeed" },
		Definition{ Command::ShiftBoost, "ShiftBoost" },
		Definition{ Command::QESpeed, "QESpeed" },
		Definition{ Command::M3Speed, "M3Speed" },
		Definition{ Command::ZoomSpeed, "ZoomSpeed" },
		Definition{ Command::Log, "Log" },
	};

	// Number of commands; each is registered once under each prefix.
	inline constexpr size_t kCommandCount = kDefinitions.size();

	/// <summary>One name to register with SC4's cheat code manager.</summary>
	struct Registration
	{
		// Name the player types.
		std::string name;
		// Plugin-owned cheat ID.
		uint32_t id;
		// Command the name requests.
		Command command;
	};

	/// <summary>
	/// Returns every name to register: each command under the ModernCamera prefix and again
	/// under the SC4MC prefix.
	/// </summary>
	/// <returns>The names with their unique cheat IDs.</returns>
	inline std::vector<Registration> GetRegistrations()
	{
		std::vector<Registration> registrations;
		registrations.reserve(2 * kCommandCount);
		for (size_t index = 0; index < kCommandCount; ++index) {
			const Definition& definition = kDefinitions[index];
			const uint32_t offset = static_cast<uint32_t>(index + 1);
			registrations.push_back({
				std::string(kLongPrefix) + std::string(definition.suffix),
				kLongIdBase + offset,
				definition.command });
			registrations.push_back({
				std::string(kShortPrefix) + std::string(definition.suffix),
				kShortIdBase + offset,
				definition.command });
		}
		return registrations;
	}

	/// <summary>Returns the full ModernCamera name of a command, for messages and the log.</summary>
	/// <param name="command">Command to name.</param>
	/// <returns>The prefix followed by the command's suffix.</returns>
	inline std::string GetName(Command command)
	{
		for (const Definition& definition : kDefinitions) {
			if (definition.command == command) {
				return std::string(kLongPrefix) + std::string(definition.suffix);
			}
		}
		return std::string(kLongPrefix);
	}

	/// <summary>Returns whether a character separates words in a console line.</summary>
	/// <param name="character">Character to test.</param>
	/// <returns>True for a space, tab, or line break.</returns>
	inline bool IsSeparator(char character)
	{
		return character == ' ' || character == '\t' || character == '\r' || character == '\n';
	}

	/// <summary>Returns text without its leading and trailing separators.</summary>
	/// <param name="text">Text to trim.</param>
	/// <returns>The trimmed view of text.</returns>
	inline std::string_view Trim(std::string_view text)
	{
		size_t first = 0;
		while (first < text.size() && IsSeparator(text[first])) {
			++first;
		}
		size_t last = text.size();
		while (last > first && IsSeparator(text[last - 1])) {
			--last;
		}
		return text.substr(first, last - first);
	}

	/// <summary>Compares two ASCII strings without regard to case.</summary>
	/// <param name="first">First string.</param>
	/// <param name="second">Second string.</param>
	/// <returns>True when the strings match ignoring case.</returns>
	inline bool EqualsIgnoreCase(std::string_view first, std::string_view second)
	{
		if (first.size() != second.size()) {
			return false;
		}
		for (size_t index = 0; index < first.size(); ++index) {
			if (std::tolower(static_cast<unsigned char>(first[index]))
				!= std::tolower(static_cast<unsigned char>(second[index]))) {
				return false;
			}
		}
		return true;
	}

	/// <summary>Returns whether text begins with a prefix, ignoring case.</summary>
	/// <param name="text">Text to test.</param>
	/// <param name="prefix">Expected beginning.</param>
	/// <returns>True when text starts with prefix.</returns>
	inline bool StartsWithIgnoreCase(std::string_view text, std::string_view prefix)
	{
		return text.size() >= prefix.size() && EqualsIgnoreCase(text.substr(0, prefix.size()), prefix);
	}

	/// <summary>
	/// Reads one console line. The first word must equal a command's name in full, so a code
	/// that begins with another code's name is never mistaken for it.
	/// </summary>
	/// <param name="line">Line the player typed.</param>
	/// <param name="command">Receives the command when the result is Matched.</param>
	/// <param name="arguments">Receives the rest of the line, trimmed, when the result is Matched.</param>
	/// <returns>Whether the line names a command, names none of ours, or uses our prefix with an unknown name.</returns>
	inline ParseResult ParseLine(std::string_view line, Command& command, std::string& arguments)
	{
		arguments.clear();
		line = Trim(line);
		size_t wordEnd = 0;
		while (wordEnd < line.size() && !IsSeparator(line[wordEnd])) {
			++wordEnd;
		}
		const std::string_view word = line.substr(0, wordEnd);

		for (const std::string_view prefix : { kLongPrefix, kShortPrefix }) {
			if (!StartsWithIgnoreCase(word, prefix)) {
				continue;
			}
			const std::string_view suffix = word.substr(prefix.size());
			for (const Definition& definition : kDefinitions) {
				if (EqualsIgnoreCase(suffix, definition.suffix)) {
					command = definition.command;
					arguments = std::string(Trim(line.substr(wordEnd)));
					return ParseResult::Matched;
				}
			}
			return ParseResult::Unknown;
		}
		return ParseResult::NotOurs;
	}

	/// <summary>
	/// Reads a finite number written with a dot, whatever the system locale.
	/// </summary>
	/// <param name="text">Argument text.</param>
	/// <param name="value">Receives the number when the text is valid.</param>
	/// <returns>True when the whole text is one finite number.</returns>
	inline bool ParseNumber(std::string_view text, float& value)
	{
		if (text.empty()) {
			return false;
		}
		float parsed = 0.0f;
		const char* const end = text.data() + text.size();
		const std::from_chars_result result = std::from_chars(text.data(), end, parsed);
		if (result.ec != std::errc() || result.ptr != end || !std::isfinite(parsed)) {
			return false;
		}
		value = parsed;
		return true;
	}
}
