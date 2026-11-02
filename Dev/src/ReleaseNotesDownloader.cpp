#include "ReleaseNotesDownloader.h"

#include "PluginVersion.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iterator>
#include <memory>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
	constexpr wchar_t kReleaseHost[] = L"github.com";
	constexpr wchar_t kLatestReleasePath[] = L"/UncleUncleRj/SC4-ModernCamera/releases/latest";
	// A release page is normally about 160 KB; these reject anything abnormal.
	constexpr size_t kMaxPageBytes = 8 * 1024 * 1024;
	constexpr size_t kMaxTextBytes = 64 * 1024;
	// Each one also bounds how long Shutdown can wait for the worker.
	constexpr int kResolveTimeoutMs = 3000;
	constexpr int kConnectTimeoutMs = 3000;
	constexpr int kSendTimeoutMs = 3000;
	constexpr int kReceiveTimeoutMs = 5000;

	/// <summary>Closes a WinHTTP handle owned by the worker that opened it.</summary>
	struct InternetHandleCloser
	{
		void operator()(HINTERNET handle) const
		{
			WinHttpCloseHandle(handle);
		}
	};
	using InternetHandle = std::unique_ptr<void, InternetHandleCloser>;

	/// <summary>Describes the failed WinHTTP call and its Windows error code.</summary>
	std::string DescribeLastError(const char* call)
	{
		return std::string(call) + " failed with error " + std::to_string(GetLastError());
	}

	/// <summary>Returns whether a byte is HTML whitespace.</summary>
	bool IsHtmlSpace(char c)
	{
		return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f';
	}

	/// <summary>Returns a lowercase copy of an ASCII tag or attribute name.</summary>
	std::string ToLower(std::string_view value)
	{
		std::string result(value);
		std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
		return result;
	}

	/// <summary>Finds an attribute in the raw text between a tag name and its end.</summary>
	/// <returns>The unquoted value, which is empty for a valueless attribute.</returns>
	std::optional<std::string_view> FindAttribute(std::string_view attributes, std::string_view name)
	{
		size_t pos = 0;
		while (pos < attributes.size()) {
			while (pos < attributes.size() && (IsHtmlSpace(attributes[pos]) || attributes[pos] == '/')) {
				++pos;
			}
			const size_t nameStart = pos;
			while (pos < attributes.size() && !IsHtmlSpace(attributes[pos])
				&& attributes[pos] != '=' && attributes[pos] != '/') {
				++pos;
			}
			if (pos == nameStart) {
				++pos;
				continue;
			}
			const std::string attributeName = ToLower(attributes.substr(nameStart, pos - nameStart));
			while (pos < attributes.size() && IsHtmlSpace(attributes[pos])) {
				++pos;
			}

			std::string_view value;
			if (pos < attributes.size() && attributes[pos] == '=') {
				++pos;
				while (pos < attributes.size() && IsHtmlSpace(attributes[pos])) {
					++pos;
				}
				size_t valueStart = pos;
				size_t valueEnd = pos;
				if (pos < attributes.size() && (attributes[pos] == '"' || attributes[pos] == '\'')) {
					valueStart = pos + 1;
					valueEnd = std::min(attributes.find(attributes[pos], valueStart), attributes.size());
					pos = std::min(valueEnd + 1, attributes.size());
				}
				else {
					while (pos < attributes.size() && !IsHtmlSpace(attributes[pos])) {
						++pos;
					}
					valueEnd = pos;
				}
				value = attributes.substr(valueStart, valueEnd - valueStart);
			}
			if (attributeName == name) {
				return value;
			}
		}
		return std::nullopt;
	}

	/// <summary>Returns whether a tag's class attribute contains one exact class.</summary>
	bool HasClass(std::string_view attributes, std::string_view className)
	{
		const std::optional<std::string_view> classes = FindAttribute(attributes, "class");
		if (!classes) {
			return false;
		}
		size_t pos = 0;
		while (pos < classes->size()) {
			while (pos < classes->size() && IsHtmlSpace((*classes)[pos])) {
				++pos;
			}
			const size_t start = pos;
			while (pos < classes->size() && !IsHtmlSpace((*classes)[pos])) {
				++pos;
			}
			if (classes->substr(start, pos - start) == className) {
				return true;
			}
		}
		return false;
	}

	/// <summary>Appends a Unicode code point to a UTF-8 string.</summary>
	void AppendUtf8(std::string& output, uint32_t codePoint)
	{
		if (codePoint < 0x80) {
			output += static_cast<char>(codePoint);
		}
		else if (codePoint < 0x800) {
			output += static_cast<char>(0xC0 | (codePoint >> 6));
			output += static_cast<char>(0x80 | (codePoint & 0x3F));
		}
		else if (codePoint < 0x10000) {
			output += static_cast<char>(0xE0 | (codePoint >> 12));
			output += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
			output += static_cast<char>(0x80 | (codePoint & 0x3F));
		}
		else {
			output += static_cast<char>(0xF0 | (codePoint >> 18));
			output += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
			output += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
			output += static_cast<char>(0x80 | (codePoint & 0x3F));
		}
	}

	/// <summary>Replaces numeric and common named character references.</summary>
	std::string DecodeEntities(std::string_view text)
	{
		struct NamedEntity
		{
			std::string_view name;
			uint32_t codePoint;
		};
		static constexpr NamedEntity kNamedEntities[] = {
			{ "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' },
			{ "nbsp", 0xA0 }, { "middot", 0xB7 }, { "copy", 0xA9 }, { "reg", 0xAE },
			{ "ndash", 0x2013 }, { "mdash", 0x2014 }, { "lsquo", 0x2018 }, { "rsquo", 0x2019 },
			{ "ldquo", 0x201C }, { "rdquo", 0x201D }, { "bull", 0x2022 }, { "hellip", 0x2026 },
		};

		std::string output;
		output.reserve(text.size());
		size_t pos = 0;
		while (pos < text.size()) {
			const size_t ampersand = text.find('&', pos);
			if (ampersand == std::string_view::npos) {
				output += text.substr(pos);
				break;
			}
			output += text.substr(pos, ampersand - pos);
			pos = ampersand + 1;

			const size_t semicolon = text.find(';', ampersand);
			if (semicolon == std::string_view::npos || semicolon - ampersand > 12) {
				output += '&';
				continue;
			}
			const std::string_view reference = text.substr(ampersand + 1, semicolon - ampersand - 1);
			std::optional<uint32_t> codePoint;
			if (reference.size() > 1 && reference[0] == '#') {
				const bool hex = reference[1] == 'x' || reference[1] == 'X';
				const std::string digits(reference.substr(hex ? 2 : 1));
				char* end = nullptr;
				const unsigned long value = std::strtoul(digits.c_str(), &end, hex ? 16 : 10);
				if (!digits.empty() && *end == '\0' && value > 0 && value <= 0x10FFFF
					&& !(value >= 0xD800 && value <= 0xDFFF)) {
					codePoint = static_cast<uint32_t>(value);
				}
			}
			else {
				for (const NamedEntity& entity : kNamedEntities) {
					if (entity.name == reference) {
						codePoint = entity.codePoint;
						break;
					}
				}
			}
			if (codePoint) {
				AppendUtf8(output, *codePoint);
				pos = semicolon + 1;
			}
			else {
				output += '&';
			}
		}
		return output;
	}

	/// <summary>Builds readable lines from text and block boundaries.</summary>
	class TextBuilder
	{
	public:
		/// <summary>Appends text, collapsing whitespace as a browser does.</summary>
		void AddText(const std::string& text)
		{
			for (const char c : text) {
				if (IsHtmlSpace(c)) {
					pendingSpace = true;
				}
				else {
					Flush();
					output += c;
				}
			}
		}

		/// <summary>Appends preformatted text with its line breaks intact.</summary>
		void AddPreformatted(const std::string& text)
		{
			for (const char c : text) {
				if (c != '\r') {
					Flush();
					output += c;
				}
			}
		}

		/// <summary>Separates the next text from the current text with a space.</summary>
		void AddSpace()
		{
			pendingSpace = true;
		}

		/// <summary>Requires at least this many line breaks before the next text.</summary>
		void RequireBreak(int lineBreaks)
		{
			pendingBreak = std::max(pendingBreak, lineBreaks);
		}

		/// <summary>Enters a list; ordered lists number their items.</summary>
		void BeginList(bool ordered)
		{
			RequireBreak(1);
			lists.push_back(ordered ? 1 : 0);
		}

		/// <summary>Leaves the innermost list.</summary>
		void EndList()
		{
			if (!lists.empty()) {
				lists.pop_back();
			}
			RequireBreak(lists.empty() ? 2 : 1);
		}

		/// <summary>Starts a list item with its indentation and marker.</summary>
		void BeginListItem()
		{
			RequireBreak(1);
			if (lists.empty()) {
				itemPrefix = "- ";
				return;
			}
			itemPrefix.assign((lists.size() - 1) * 2, ' ');
			if (lists.back() > 0) {
				itemPrefix += std::to_string(lists.back()++) + ". ";
			}
			else {
				itemPrefix += "- ";
			}
		}

		/// <summary>Returns the finished text without trailing whitespace.</summary>
		std::string Finish()
		{
			while (!output.empty() && IsHtmlSpace(output.back())) {
				output.pop_back();
			}
			return std::move(output);
		}

	private:
		/// <summary>Writes the pending separators ahead of the next character.</summary>
		void Flush()
		{
			if (output.empty()) {
				pendingBreak = 0;
				pendingSpace = false;
			}
			if (pendingBreak > 0) {
				int existing = 0;
				for (auto it = output.rbegin(); it != output.rend() && *it == '\n'; ++it) {
					++existing;
				}
				output.append(static_cast<size_t>(std::max(0, pendingBreak - existing)), '\n');
				pendingBreak = 0;
				pendingSpace = false;
			}

			const bool lineStart = output.empty() || output.back() == '\n';
			if (!itemPrefix.empty()) {
				if (!lineStart) {
					output += '\n';
				}
				output += itemPrefix;
				itemPrefix.clear();
			}
			else if (lineStart && !lists.empty()) {
				// Keeps a <br> continuation aligned under its list item's text.
				output.append(lists.size() * 2, ' ');
			}
			else if (pendingSpace && !lineStart && !IsHtmlSpace(output.back())) {
				output += ' ';
			}
			pendingSpace = false;
		}

		// Text completed so far.
		std::string output;
		// Line breaks required before the next character.
		int pendingBreak = 0;
		// Whether whitespace precedes the next character.
		bool pendingSpace = false;
		// Marker waiting for the current list item's first character.
		std::string itemPrefix;
		// Next number of each open ordered list, or zero for an unordered list.
		std::vector<int> lists;
	};

	/// <summary>Returns whether an element has no content or closing tag.</summary>
	bool IsVoidElement(std::string_view name)
	{
		static constexpr std::string_view kVoidElements[] = {
			"area", "base", "br", "col", "embed", "hr", "img", "input",
			"link", "meta", "param", "source", "track", "wbr",
		};
		return std::find(std::begin(kVoidElements), std::end(kVoidElements), name) != std::end(kVoidElements);
	}

	/// <summary>Returns whether an element's content is script or style source, not markup.</summary>
	bool IsRawTextElement(std::string_view name)
	{
		return name == "script" || name == "style";
	}

	/// <summary>Returns whether an element shows no readable text: icons, copy buttons, hidden content.</summary>
	bool IsSkippedElement(std::string_view name, std::string_view attributes)
	{
		return name == "button" || name == "svg" || name == "template" || name == "textarea"
			|| FindAttribute(attributes, "hidden").has_value();
	}

	/// <summary>Returns line breaks around an element that forms a paragraph, or zero.</summary>
	int ParagraphBreaks(std::string_view name)
	{
		static constexpr std::string_view kParagraphs[] = {
			"p", "pre", "blockquote", "table", "hr",
		};
		return std::find(std::begin(kParagraphs), std::end(kParagraphs), name) != std::end(kParagraphs) ? 2 : 0;
	}

	/// <summary>Returns whether a name is h1 through h6.</summary>
	bool IsHeading(std::string_view name)
	{
		return name.size() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6';
	}

	/// <summary>Returns whether an element starts and ends its own line.</summary>
	bool IsLineElement(std::string_view name)
	{
		static constexpr std::string_view kLines[] = {
			"tr", "details", "summary", "dl", "dt", "dd", "section", "article",
		};
		return std::find(std::begin(kLines), std::end(kLines), name) != std::end(kLines);
	}

	/// <summary>One parsed start or end tag.</summary>
	struct HtmlTag
	{
		// Lowercase element name.
		std::string name;
		// Raw attribute text.
		std::string_view attributes;
		// True for an end tag.
		bool closing = false;
		// True for a tag written with a trailing slash.
		bool selfClosing = false;
		// Position just past the tag's closing bracket.
		size_t end = 0;
	};

	/// <summary>Parses the tag at a '&lt;', honoring quoted attribute values.</summary>
	/// <returns>The tag, or no value when the bracket does not begin a complete tag.</returns>
	std::optional<HtmlTag> ParseTag(std::string_view html, size_t start)
	{
		size_t pos = start + 1;
		HtmlTag tag;
		if (pos < html.size() && html[pos] == '/') {
			tag.closing = true;
			++pos;
		}
		if (pos >= html.size() || !std::isalpha(static_cast<unsigned char>(html[pos]))) {
			return std::nullopt;
		}
		const size_t nameStart = pos;
		while (pos < html.size() && !IsHtmlSpace(html[pos]) && html[pos] != '/' && html[pos] != '>') {
			++pos;
		}
		tag.name = ToLower(html.substr(nameStart, pos - nameStart));

		const size_t attributesStart = pos;
		char quote = '\0';
		for (; pos < html.size(); ++pos) {
			const char c = html[pos];
			if (quote != '\0') {
				if (c == quote) {
					quote = '\0';
				}
			}
			else if (c == '"' || c == '\'') {
				quote = c;
			}
			else if (c == '>') {
				tag.attributes = html.substr(attributesStart, pos - attributesStart);
				tag.selfClosing = html[pos - 1] == '/';
				tag.end = pos + 1;
				return tag;
			}
		}
		return std::nullopt;
	}

	/// <summary>Finds an end tag's position with a case-insensitive name match.</summary>
	size_t FindEndTag(std::string_view html, size_t start, std::string_view name)
	{
		for (size_t pos = html.find("</", start); pos != std::string_view::npos; pos = html.find("</", pos + 2)) {
			if (ToLower(html.substr(pos + 2, name.size())) == name) {
				return pos;
			}
		}
		return html.size();
	}

	/// <summary>Trims a capped text to a whole UTF-8 character and marks the cut.</summary>
	void LimitTextLength(std::string& text)
	{
		if (text.size() <= kMaxTextBytes) {
			return;
		}
		size_t length = kMaxTextBytes;
		while (length > 0 && (static_cast<unsigned char>(text[length]) & 0xC0) == 0x80) {
			--length;
		}
		text.resize(length);
		text += "\n...";
	}
}

std::optional<std::string> ExtractReleaseNotesText(std::string_view html)
{
	TextBuilder builder;
	// Open divs inside Box-body, counting Box-body itself; zero until it is found.
	int boxDepth = 0;
	// The value of boxDepth just inside the notes; zero until they are found.
	int notesDepth = 0;
	std::string skippedName;
	int skippedDepth = 0;
	int preDepth = 0;
	bool finished = false;

	size_t pos = 0;
	while (pos < html.size() && !finished) {
		const size_t open = html.find('<', pos);
		const size_t textEnd = open == std::string_view::npos ? html.size() : open;
		if (notesDepth > 0 && skippedDepth == 0 && textEnd > pos) {
			const std::string text = DecodeEntities(html.substr(pos, textEnd - pos));
			if (preDepth > 0) {
				builder.AddPreformatted(text);
			}
			else {
				builder.AddText(text);
			}
		}
		if (open == std::string_view::npos) {
			break;
		}

		if (html.compare(open, 4, "<!--") == 0) {
			const size_t commentEnd = html.find("-->", open + 4);
			pos = commentEnd == std::string_view::npos ? html.size() : commentEnd + 3;
			continue;
		}
		const std::optional<HtmlTag> parsed = ParseTag(html, open);
		if (!parsed) {
			// Doctypes and other declarations end at the next bracket. A bare '<'
			// in text is treated as text.
			const bool declaration = open + 1 < html.size() && (html[open + 1] == '!' || html[open + 1] == '?');
			if (declaration) {
				const size_t declarationEnd = html.find('>', open);
				pos = declarationEnd == std::string_view::npos ? html.size() : declarationEnd + 1;
			}
			else {
				if (notesDepth > 0 && skippedDepth == 0) {
					builder.AddText("<");
				}
				pos = open + 1;
			}
			continue;
		}

		const HtmlTag& tag = *parsed;
		pos = tag.end;
		const bool startsContent = !tag.closing && !tag.selfClosing && !IsVoidElement(tag.name);

		// Script and style source can contain anything, including text that looks
		// like a tag, so jump straight to the end tag.
		if (startsContent && IsRawTextElement(tag.name)) {
			pos = FindEndTag(html, tag.end, tag.name);
			continue;
		}

		if (boxDepth == 0) {
			if (startsContent && tag.name == "div" && HasClass(tag.attributes, "Box-body")) {
				boxDepth = 1;
			}
			continue;
		}

		// The title and author rows come first; only the notes element is kept.
		if (notesDepth == 0) {
			if (tag.name == "div") {
				if (tag.closing) {
					--boxDepth;
					finished = boxDepth == 0;
				}
				else if (startsContent) {
					++boxDepth;
					if (HasClass(tag.attributes, "markdown-body")) {
						notesDepth = boxDepth;
					}
				}
			}
			continue;
		}

		if (skippedDepth > 0) {
			if (tag.name == skippedName) {
				if (tag.closing) {
					--skippedDepth;
				}
				else if (startsContent) {
					++skippedDepth;
				}
			}
			continue;
		}
		if (startsContent && IsSkippedElement(tag.name, tag.attributes)) {
			skippedName = tag.name;
			skippedDepth = 1;
			continue;
		}

		const std::string_view name = tag.name;
		if (name == "div") {
			if (tag.closing) {
				finished = boxDepth == notesDepth;
				--boxDepth;
				builder.RequireBreak(1);
			}
			else if (startsContent) {
				++boxDepth;
				builder.RequireBreak(1);
			}
		}
		else if (name == "br") {
			builder.RequireBreak(1);
		}
		else if (name == "ul" || name == "ol") {
			if (tag.closing) {
				builder.EndList();
			}
			else if (startsContent) {
				builder.BeginList(name == "ol");
			}
		}
		else if (name == "li") {
			if (tag.closing) {
				builder.RequireBreak(1);
			}
			else if (startsContent) {
				builder.BeginListItem();
			}
		}
		else if (IsHeading(name)) {
			builder.RequireBreak(tag.closing ? 1 : 2);
		}
		else if (const int breaks = ParagraphBreaks(name); breaks > 0) {
			builder.RequireBreak(breaks);
			if (name == "pre" && startsContent) {
				++preDepth;
			}
			else if (name == "pre" && tag.closing && preDepth > 0) {
				--preDepth;
			}
		}
		else if (IsLineElement(name)) {
			builder.RequireBreak(1);
		}
		else if (name == "td" || name == "th") {
			builder.AddSpace();
		}
	}

	if (notesDepth == 0) {
		return std::nullopt;
	}
	std::string text = builder.Finish();
	if (text.empty()) {
		return std::nullopt;
	}
	LimitTextLength(text);
	return text;
}

std::optional<std::string> ExtractReleaseVersion(std::string_view url)
{
	constexpr std::string_view kTagPath = "/releases/tag/";
	const size_t tagPath = url.rfind(kTagPath);
	if (tagPath == std::string_view::npos) {
		return std::nullopt;
	}
	std::string_view tag = url.substr(tagPath + kTagPath.size());
	if (!tag.empty() && (tag.front() == 'v' || tag.front() == 'V')) {
		tag.remove_prefix(1);
	}
	// The value reaches a Lua chunk, so nothing but digits and dots may pass.
	if (tag.empty() || tag.front() == '.' || tag.back() == '.'
		|| tag.find_first_not_of("0123456789.") != std::string_view::npos) {
		return std::nullopt;
	}
	return std::string(tag);
}

bool IsNewerVersion(std::string_view candidate, std::string_view current)
{
	// Reads the next dotted component and advances past it.
	const auto nextComponent = [](std::string_view& version) {
		const size_t dot = version.find('.');
		const std::string component(version.substr(0, dot));
		version = dot == std::string_view::npos ? std::string_view{} : version.substr(dot + 1);
		return std::strtoul(component.c_str(), nullptr, 10);
	};

	while (!candidate.empty() || !current.empty()) {
		const unsigned long candidateComponent = nextComponent(candidate);
		const unsigned long currentComponent = nextComponent(current);
		if (candidateComponent != currentComponent) {
			return candidateComponent > currentComponent;
		}
	}
	return false;
}

ReleaseNotesDownloader::~ReleaseNotesDownloader()
{
	Shutdown();
}

void ReleaseNotesDownloader::Start()
{
	std::lock_guard lock(mutex);
	if (downloading || (text && latestVersion) || stopping.load(std::memory_order_relaxed)) {
		return;
	}
	// A finished worker publishes under this lock as its last act, so this join
	// does not wait on the network.
	if (worker.joinable()) {
		worker.join();
	}
	failure.clear();
	downloading = true;
	try {
		worker = std::thread(&ReleaseNotesDownloader::WorkerMain, this);
	}
	catch (const std::system_error& error) {
		downloading = false;
		failure = std::string("could not start the download thread: ") + error.what();
	}
}

bool ReleaseNotesDownloader::IsDownloading() const
{
	std::lock_guard lock(mutex);
	return downloading;
}

std::optional<std::string> ReleaseNotesDownloader::GetText() const
{
	std::lock_guard lock(mutex);
	return text;
}

std::optional<std::string> ReleaseNotesDownloader::GetLatestVersion() const
{
	std::lock_guard lock(mutex);
	return latestVersion;
}

std::string ReleaseNotesDownloader::ConsumeFailure()
{
	std::lock_guard lock(mutex);
	return std::exchange(failure, {});
}

void ReleaseNotesDownloader::Shutdown()
{
	std::thread finishing;
	{
		std::lock_guard lock(mutex);
		stopping.store(true, std::memory_order_relaxed);
		finishing = std::move(worker);
	}
	// The worker needs the lock to publish, so join outside it.
	if (finishing.joinable()) {
		finishing.join();
	}
}

void ReleaseNotesDownloader::WorkerMain()
{
	std::optional<std::string> downloaded;
	std::optional<std::string> version;
	std::string error;
	try {
		std::string html;
		std::string finalUrl;
		if (DownloadPage(html, finalUrl, error)) {
			version = ExtractReleaseVersion(finalUrl);
			downloaded = ExtractReleaseNotesText(html);
			if (!version) {
				error = "the latest release redirect had no version: " + finalUrl;
			}
			else if (!downloaded) {
				error = "the release page had no notes inside Box-body";
			}
		}
	}
	catch (const std::exception& exception) {
		downloaded.reset();
		version.reset();
		error = exception.what();
	}

	std::lock_guard lock(mutex);
	// A partial retry keeps whatever an earlier download already found.
	if (downloaded) {
		text = std::move(downloaded);
	}
	if (version) {
		latestVersion = std::move(version);
	}
	failure = std::move(error);
	downloading = false;
}

bool ReleaseNotesDownloader::DownloadPage(std::string& html, std::string& finalUrl, std::string& error) const
{
	std::wstring userAgent = L"SC4-ModernCamera/";
	for (const char* c = PluginVersion::String; *c != '\0'; ++c) {
		userAgent += static_cast<wchar_t>(static_cast<unsigned char>(*c));
	}

	const InternetHandle session(WinHttpOpen(
		userAgent.c_str(),
		WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		WINHTTP_NO_PROXY_NAME,
		WINHTTP_NO_PROXY_BYPASS,
		0));
	if (!session) {
		error = DescribeLastError("WinHttpOpen");
		return false;
	}
	if (!WinHttpSetTimeouts(session.get(), kResolveTimeoutMs, kConnectTimeoutMs, kSendTimeoutMs, kReceiveTimeoutMs)) {
		error = DescribeLastError("WinHttpSetTimeouts");
		return false;
	}

	const InternetHandle connection(WinHttpConnect(session.get(), kReleaseHost, INTERNET_DEFAULT_HTTPS_PORT, 0));
	if (!connection) {
		error = DescribeLastError("WinHttpConnect");
		return false;
	}
	const InternetHandle request(WinHttpOpenRequest(
		connection.get(),
		L"GET",
		kLatestReleasePath,
		nullptr,
		WINHTTP_NO_REFERER,
		WINHTTP_DEFAULT_ACCEPT_TYPES,
		WINHTTP_FLAG_SECURE));
	if (!request) {
		error = DescribeLastError("WinHttpOpenRequest");
		return false;
	}

	// WinHTTP follows the redirect from /latest to the release's own page.
	if (!WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
		error = DescribeLastError("WinHttpSendRequest");
		return false;
	}
	if (!WinHttpReceiveResponse(request.get(), nullptr)) {
		error = DescribeLastError("WinHttpReceiveResponse");
		return false;
	}

	DWORD status = 0;
	DWORD statusSize = sizeof(status);
	if (!WinHttpQueryHeaders(
		request.get(),
		WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX,
		&status,
		&statusSize,
		WINHTTP_NO_HEADER_INDEX)) {
		error = DescribeLastError("WinHttpQueryHeaders");
		return false;
	}
	if (status != HTTP_STATUS_OK) {
		error = "GitHub returned HTTP " + std::to_string(status);
		return false;
	}

	// The first call reports the size in bytes; a valid URL is plain ASCII.
	DWORD urlBytes = 0;
	WinHttpQueryOption(request.get(), WINHTTP_OPTION_URL, nullptr, &urlBytes);
	std::wstring wideUrl(urlBytes / sizeof(wchar_t), L'\0');
	if (wideUrl.empty() || !WinHttpQueryOption(request.get(), WINHTTP_OPTION_URL, wideUrl.data(), &urlBytes)) {
		error = DescribeLastError("WinHttpQueryOption");
		return false;
	}
	wideUrl.resize(urlBytes / sizeof(wchar_t));
	for (const wchar_t c : wideUrl) {
		if (c != L'\0') {
			finalUrl += c < 0x80 ? static_cast<char>(c) : '?';
		}
	}

	std::vector<char> buffer(16 * 1024);
	while (true) {
		if (stopping.load(std::memory_order_relaxed)) {
			error = "the download was stopped by shutdown";
			return false;
		}
		DWORD bytesRead = 0;
		if (!WinHttpReadData(request.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead)) {
			error = DescribeLastError("WinHttpReadData");
			return false;
		}
		if (bytesRead == 0) {
			return true;
		}
		if (html.size() + bytesRead > kMaxPageBytes) {
			error = "the release page exceeded the download size limit";
			return false;
		}
		html.append(buffer.data(), bytesRead);
	}
}
