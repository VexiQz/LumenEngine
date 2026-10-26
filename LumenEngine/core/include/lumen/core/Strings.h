// Lumen Engine — string helpers (UTF-8 aware where it matters).
#pragma once

#include "lumen/core/Common.h"

namespace lumen::str
{

	std::string_view TrimLeft(std::string_view s);
	std::string_view TrimRight(std::string_view s);
	std::string_view Trim(std::string_view s);

	bool StartsWith(std::string_view s, std::string_view prefix);
	bool EndsWith(std::string_view s, std::string_view suffix);
	bool Contains(std::string_view s, std::string_view needle);
	bool EqualsIgnoreCase(std::string_view a, std::string_view b);

	std::string ToLower(std::string_view s);
	std::string ToUpper(std::string_view s);

	std::vector<std::string> Split(std::string_view s, char delimiter, bool keepEmpty = true);
	std::vector<std::string> SplitLines(std::string_view s);
	std::string Join(const std::vector<std::string>& parts, std::string_view separator);
	std::string ReplaceAll(std::string s, std::string_view from, std::string_view to);

	// Parses numbers without exceptions. Returns false when the whole string is not a valid number.
	bool ParseInt(std::string_view s, int64_t& out, int base = 10);
	bool ParseDouble(std::string_view s, double& out);

	// Escapes a string so that it can be embedded between double quotes in C/C++/CMake sources.
	std::string EscapeCString(std::string_view s);

	// UTF-8 helpers
	// Decodes one code point starting at s[i]; advances i. Invalid sequences yield U+FFFD.
	uint32_t DecodeUtf8(std::string_view s, size_t& i);
	void AppendUtf8(std::string& out, uint32_t codepoint);
	// Number of UTF-16 code units for the UTF-8 range [begin, end) (LSP positions are UTF-16 based).
	size_t Utf16Length(std::string_view utf8);
	// Converts an LSP (UTF-16 based) character offset on a line to a UTF-8 byte offset.
	size_t Utf16ToByteOffset(std::string_view line, size_t utf16Offset);
	// Converts a UTF-8 byte offset on a line to an LSP (UTF-16 based) character offset.
	size_t ByteToUtf16Offset(std::string_view line, size_t byteOffset);

	// Splits an identifier into words for fuzzy matching ("getEntityByName" -> get Entity By Name).
	// Returns a score > 0 when `query` fuzzily matches `candidate` (higher is better), 0 otherwise.
	int FuzzyScore(std::string_view query, std::string_view candidate);

	// Stable 64-bit FNV-1a hash.
	uint64_t Hash64(const void* data, size_t size, uint64_t seed = 1469598103934665603ull);
	inline uint64_t Hash64(std::string_view s, uint64_t seed = 1469598103934665603ull)
	{
		return Hash64(s.data(), s.size(), seed);
	}
	std::string ToHex(uint64_t v);
	std::string ToHex(const uint8_t* data, size_t size);

} // namespace lumen::str
