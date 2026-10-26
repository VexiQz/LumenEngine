#include "lumen/core/Strings.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace lumen
{
	std::string StrFormat(const char* fmt, ...)
	{
		va_list args;
		va_start(args, fmt);
		va_list copy;
		va_copy(copy, args);
		const int needed = std::vsnprintf(nullptr, 0, fmt, copy);
		va_end(copy);
		std::string out;
		if (needed > 0)
		{
			out.resize((size_t)needed + 1);
			std::vsnprintf(out.data(), out.size(), fmt, args);
			out.resize((size_t)needed);
		}
		va_end(args);
		return out;
	}
} // namespace lumen

namespace lumen::str
{
	static bool IsSpace(char c)
	{
		return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
	}

	std::string_view TrimLeft(std::string_view s)
	{
		size_t i = 0;
		while (i < s.size() && IsSpace(s[i]))
			++i;
		return s.substr(i);
	}

	std::string_view TrimRight(std::string_view s)
	{
		size_t n = s.size();
		while (n > 0 && IsSpace(s[n - 1]))
			--n;
		return s.substr(0, n);
	}

	std::string_view Trim(std::string_view s)
	{
		return TrimRight(TrimLeft(s));
	}

	bool StartsWith(std::string_view s, std::string_view prefix)
	{
		return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
	}

	bool EndsWith(std::string_view s, std::string_view suffix)
	{
		return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
	}

	bool Contains(std::string_view s, std::string_view needle)
	{
		return s.find(needle) != std::string_view::npos;
	}

	bool EqualsIgnoreCase(std::string_view a, std::string_view b)
	{
		if (a.size() != b.size())
			return false;
		for (size_t i = 0; i < a.size(); ++i)
		{
			if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
				return false;
		}
		return true;
	}

	std::string ToLower(std::string_view s)
	{
		std::string out(s);
		for (char& c : out)
			c = (char)std::tolower((unsigned char)c);
		return out;
	}

	std::string ToUpper(std::string_view s)
	{
		std::string out(s);
		for (char& c : out)
			c = (char)std::toupper((unsigned char)c);
		return out;
	}

	std::vector<std::string> Split(std::string_view s, char delimiter, bool keepEmpty)
	{
		std::vector<std::string> out;
		size_t start = 0;
		while (true)
		{
			const size_t pos = s.find(delimiter, start);
			const std::string_view part = pos == std::string_view::npos ? s.substr(start) : s.substr(start, pos - start);
			if (keepEmpty || !part.empty())
				out.emplace_back(part);
			if (pos == std::string_view::npos)
				break;
			start = pos + 1;
		}
		return out;
	}

	std::vector<std::string> SplitLines(std::string_view s)
	{
		std::vector<std::string> out;
		size_t start = 0;
		while (start <= s.size())
		{
			size_t pos = s.find('\n', start);
			std::string_view line = pos == std::string_view::npos ? s.substr(start) : s.substr(start, pos - start);
			if (!line.empty() && line.back() == '\r')
				line.remove_suffix(1);
			if (pos == std::string_view::npos)
			{
				if (!line.empty())
					out.emplace_back(line);
				break;
			}
			out.emplace_back(line);
			start = pos + 1;
		}
		return out;
	}

	std::string Join(const std::vector<std::string>& parts, std::string_view separator)
	{
		std::string out;
		for (size_t i = 0; i < parts.size(); ++i)
		{
			if (i)
				out.append(separator);
			out.append(parts[i]);
		}
		return out;
	}

	std::string ReplaceAll(std::string s, std::string_view from, std::string_view to)
	{
		if (from.empty())
			return s;
		size_t pos = 0;
		while ((pos = s.find(from, pos)) != std::string::npos)
		{
			s.replace(pos, from.size(), to);
			pos += to.size();
		}
		return s;
	}

	bool ParseInt(std::string_view s, int64_t& out, int base)
	{
		s = Trim(s);
		if (s.empty())
			return false;
		if (s.front() == '+')
			s.remove_prefix(1);
		int64_t value = 0;
		const auto res = std::from_chars(s.data(), s.data() + s.size(), value, base);
		if (res.ec != std::errc() || res.ptr != s.data() + s.size())
			return false;
		out = value;
		return true;
	}

	bool ParseDouble(std::string_view s, double& out)
	{
		s = Trim(s);
		if (s.empty())
			return false;
		// strtod needs a NUL terminated buffer.
		std::string tmp(s);
		char* end = nullptr;
		errno = 0;
		const double v = std::strtod(tmp.c_str(), &end);
		if (end != tmp.c_str() + tmp.size() || errno == ERANGE)
			return false;
		out = v;
		return true;
	}

	std::string EscapeCString(std::string_view s)
	{
		std::string out;
		out.reserve(s.size() + 8);
		for (const char c : s)
		{
			switch (c)
			{
			case '\\': out += "\\\\"; break;
			case '"': out += "\\\""; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if ((unsigned char)c < 0x20)
				{
					char buf[8];
					std::snprintf(buf, sizeof(buf), "\\x%02x", (unsigned)(unsigned char)c);
					out += buf;
				}
				else
				{
					out += c;
				}
			}
		}
		return out;
	}

	uint32_t DecodeUtf8(std::string_view s, size_t& i)
	{
		const auto byte = [&](size_t k) -> uint32_t { return (unsigned char)s[k]; };
		if (i >= s.size())
			return 0xFFFD;
		const uint32_t b0 = byte(i);
		if (b0 < 0x80)
		{
			++i;
			return b0;
		}
		int extra = 0;
		uint32_t cp = 0;
		uint32_t minValue = 0;
		if ((b0 & 0xE0) == 0xC0) { extra = 1; cp = b0 & 0x1F; minValue = 0x80; }
		else if ((b0 & 0xF0) == 0xE0) { extra = 2; cp = b0 & 0x0F; minValue = 0x800; }
		else if ((b0 & 0xF8) == 0xF0) { extra = 3; cp = b0 & 0x07; minValue = 0x10000; }
		else { ++i; return 0xFFFD; }

		if (i + (size_t)extra >= s.size())
		{
			++i;
			return 0xFFFD;
		}
		for (int k = 1; k <= extra; ++k)
		{
			const uint32_t bk = byte(i + (size_t)k);
			if ((bk & 0xC0) != 0x80)
			{
				++i;
				return 0xFFFD;
			}
			cp = (cp << 6) | (bk & 0x3F);
		}
		if (cp < minValue || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
		{
			++i;
			return 0xFFFD;
		}
		i += (size_t)extra + 1;
		return cp;
	}

	void AppendUtf8(std::string& out, uint32_t cp)
	{
		if (cp < 0x80)
		{
			out += (char)cp;
		}
		else if (cp < 0x800)
		{
			out += (char)(0xC0 | (cp >> 6));
			out += (char)(0x80 | (cp & 0x3F));
		}
		else if (cp < 0x10000)
		{
			out += (char)(0xE0 | (cp >> 12));
			out += (char)(0x80 | ((cp >> 6) & 0x3F));
			out += (char)(0x80 | (cp & 0x3F));
		}
		else
		{
			out += (char)(0xF0 | (cp >> 18));
			out += (char)(0x80 | ((cp >> 12) & 0x3F));
			out += (char)(0x80 | ((cp >> 6) & 0x3F));
			out += (char)(0x80 | (cp & 0x3F));
		}
	}

	size_t Utf16Length(std::string_view utf8)
	{
		size_t n = 0;
		size_t i = 0;
		while (i < utf8.size())
		{
			const uint32_t cp = DecodeUtf8(utf8, i);
			n += cp >= 0x10000 ? 2 : 1;
		}
		return n;
	}

	size_t Utf16ToByteOffset(std::string_view line, size_t utf16Offset)
	{
		size_t units = 0;
		size_t i = 0;
		while (i < line.size() && units < utf16Offset)
		{
			const uint32_t cp = DecodeUtf8(line, i);
			units += cp >= 0x10000 ? 2 : 1;
		}
		return i;
	}

	size_t ByteToUtf16Offset(std::string_view line, size_t byteOffset)
	{
		byteOffset = std::min(byteOffset, line.size());
		return Utf16Length(line.substr(0, byteOffset));
	}

	int FuzzyScore(std::string_view query, std::string_view candidate)
	{
		if (query.empty())
			return 1;
		if (candidate.size() < query.size())
			return 0;

		// Exact prefix (case sensitive) beats everything.
		if (StartsWith(candidate, query))
			return 1000 - (int)std::min<size_t>(candidate.size() - query.size(), 500);
		if (candidate.size() >= query.size() && EqualsIgnoreCase(candidate.substr(0, query.size()), query))
			return 800 - (int)std::min<size_t>(candidate.size() - query.size(), 400);

		// Subsequence match with bonuses for word boundaries and consecutive characters.
		int score = 0;
		size_t qi = 0;
		int streak = 0;
		for (size_t ci = 0; ci < candidate.size() && qi < query.size(); ++ci)
		{
			const char cc = candidate[ci];
			const char qc = query[qi];
			if (std::tolower((unsigned char)cc) == std::tolower((unsigned char)qc))
			{
				int bonus = 10;
				const bool boundary = ci == 0 || candidate[ci - 1] == '_' || candidate[ci - 1] == '.' ||
					(std::islower((unsigned char)candidate[ci - 1]) && std::isupper((unsigned char)cc));
				if (boundary)
					bonus += 30;
				if (cc == qc)
					bonus += 5;
				bonus += streak * 8;
				score += bonus;
				++streak;
				++qi;
			}
			else
			{
				streak = 0;
			}
		}
		if (qi < query.size())
			return 0;
		const int lengthPenalty = (int)std::min<size_t>(candidate.size() - query.size(), 100);
		return std::max(1, std::min(700, score - lengthPenalty));
	}

	uint64_t Hash64(const void* data, size_t size, uint64_t seed)
	{
		const uint8_t* p = (const uint8_t*)data;
		uint64_t h = seed;
		for (size_t i = 0; i < size; ++i)
		{
			h ^= p[i];
			h *= 1099511628211ull;
		}
		return h;
	}

	std::string ToHex(uint64_t v)
	{
		char buf[17];
		std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)v);
		return buf;
	}

	std::string ToHex(const uint8_t* data, size_t size)
	{
		static const char* digits = "0123456789abcdef";
		std::string out;
		out.resize(size * 2);
		for (size_t i = 0; i < size; ++i)
		{
			out[i * 2] = digits[data[i] >> 4];
			out[i * 2 + 1] = digits[data[i] & 15];
		}
		return out;
	}

} // namespace lumen::str
