#include "lumen/core/Json.h"

#include "lumen/core/Strings.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace lumen
{
	namespace
	{
		const Json& NullJson()
		{
			static const Json v;
			return v;
		}
		const std::string& EmptyString()
		{
			static const std::string v;
			return v;
		}
		const Json::Array& EmptyArray()
		{
			static const Json::Array v;
			return v;
		}
		const Json::Object& EmptyObject()
		{
			static const Json::Object v;
			return v;
		}
	} // namespace

	// ------------------------------------------------------------------------------------------------
	// accessors
	// ------------------------------------------------------------------------------------------------
	bool Json::AsBool(bool def) const
	{
		switch (type_)
		{
		case Type::Bool: return u_.b;
		case Type::Int: return u_.i != 0;
		case Type::Double: return u_.d != 0.0;
		default: return def;
		}
	}

	int64_t Json::AsInt(int64_t def) const
	{
		switch (type_)
		{
		case Type::Int: return u_.i;
		case Type::Double:
			if (std::isfinite(u_.d) && u_.d >= -9.2e18 && u_.d <= 9.2e18)
				return (int64_t)u_.d;
			return def;
		case Type::Bool: return u_.b ? 1 : 0;
		default: return def;
		}
	}

	double Json::AsDouble(double def) const
	{
		switch (type_)
		{
		case Type::Int: return (double)u_.i;
		case Type::Double: return u_.d;
		case Type::Bool: return u_.b ? 1.0 : 0.0;
		default: return def;
		}
	}

	const std::string& Json::AsString() const
	{
		return type_ == Type::String ? str_ : EmptyString();
	}

	std::string Json::AsString(const std::string& def) const
	{
		return type_ == Type::String ? str_ : def;
	}

	size_t Json::Size() const
	{
		if (type_ == Type::Array)
			return arr_.size();
		if (type_ == Type::Object)
			return obj_.size();
		return 0;
	}

	const Json& Json::At(size_t index) const
	{
		if (type_ != Type::Array || index >= arr_.size())
			return NullJson();
		return arr_[index];
	}

	Json& Json::operator[](size_t index)
	{
		if (type_ != Type::Array)
		{
			*this = Json(Array{});
		}
		if (index >= arr_.size())
			arr_.resize(index + 1);
		return arr_[index];
	}

	void Json::Push(Json v)
	{
		if (type_ != Type::Array)
			*this = Json(Array{});
		arr_.push_back(std::move(v));
	}

	const Json::Array& Json::Items() const
	{
		return type_ == Type::Array ? arr_ : EmptyArray();
	}

	Json::Array& Json::ItemsMutable()
	{
		if (type_ != Type::Array)
			*this = Json(Array{});
		return arr_;
	}

	bool Json::Contains(std::string_view key) const
	{
		return Find(key) != nullptr;
	}

	const Json* Json::Find(std::string_view key) const
	{
		if (type_ != Type::Object)
			return nullptr;
		for (const Member& m : obj_)
		{
			if (m.first == key)
				return &m.second;
		}
		return nullptr;
	}

	Json* Json::FindMutable(std::string_view key)
	{
		if (type_ != Type::Object)
			return nullptr;
		for (Member& m : obj_)
		{
			if (m.first == key)
				return &m.second;
		}
		return nullptr;
	}

	const Json& Json::Get(std::string_view key) const
	{
		const Json* p = Find(key);
		return p ? *p : NullJson();
	}

	Json& Json::operator[](std::string_view key)
	{
		if (type_ != Type::Object)
			*this = Json(Object{});
		for (Member& m : obj_)
		{
			if (m.first == key)
				return m.second;
		}
		obj_.emplace_back(std::string(key), Json());
		return obj_.back().second;
	}

	void Json::Set(std::string_view key, Json v)
	{
		(*this)[key] = std::move(v);
	}

	bool Json::Erase(std::string_view key)
	{
		if (type_ != Type::Object)
			return false;
		for (size_t i = 0; i < obj_.size(); ++i)
		{
			if (obj_[i].first == key)
			{
				obj_.erase(obj_.begin() + (ptrdiff_t)i);
				return true;
			}
		}
		return false;
	}

	const Json::Object& Json::Members() const
	{
		return type_ == Type::Object ? obj_ : EmptyObject();
	}

	Json::Object& Json::MembersMutable()
	{
		if (type_ != Type::Object)
			*this = Json(Object{});
		return obj_;
	}

	std::string Json::GetString(std::string_view key, const std::string& def) const
	{
		const Json* p = Find(key);
		return p && p->IsString() ? p->str_ : def;
	}

	int64_t Json::GetInt(std::string_view key, int64_t def) const
	{
		const Json* p = Find(key);
		return p && p->IsNumber() ? p->AsInt(def) : def;
	}

	double Json::GetDouble(std::string_view key, double def) const
	{
		const Json* p = Find(key);
		return p && p->IsNumber() ? p->AsDouble(def) : def;
	}

	bool Json::GetBool(std::string_view key, bool def) const
	{
		const Json* p = Find(key);
		return p && p->IsBool() ? p->u_.b : def;
	}

	std::vector<std::string> Json::GetStringArray(std::string_view key) const
	{
		std::vector<std::string> out;
		const Json* p = Find(key);
		if (p && p->IsArray())
		{
			for (const Json& item : p->arr_)
			{
				if (item.IsString())
					out.push_back(item.str_);
			}
		}
		return out;
	}

	bool Json::operator==(const Json& o) const
	{
		if (IsNumber() && o.IsNumber())
		{
			if (type_ == Type::Int && o.type_ == Type::Int)
				return u_.i == o.u_.i;
			return AsDouble() == o.AsDouble();
		}
		if (type_ != o.type_)
			return false;
		switch (type_)
		{
		case Type::Null: return true;
		case Type::Bool: return u_.b == o.u_.b;
		case Type::String: return str_ == o.str_;
		case Type::Array: return arr_ == o.arr_;
		case Type::Object:
		{
			if (obj_.size() != o.obj_.size())
				return false;
			for (const Member& m : obj_)
			{
				const Json* other = o.Find(m.first);
				if (!other || !(*other == m.second))
					return false;
			}
			return true;
		}
		default: return false;
		}
	}

	// ------------------------------------------------------------------------------------------------
	// serializer
	// ------------------------------------------------------------------------------------------------
	static void EscapeString(std::string& out, const std::string& s, bool escapeNonAscii)
	{
		out += '"';
		size_t i = 0;
		while (i < s.size())
		{
			const unsigned char c = (unsigned char)s[i];
			switch (c)
			{
			case '"': out += "\\\""; ++i; continue;
			case '\\': out += "\\\\"; ++i; continue;
			case '\b': out += "\\b"; ++i; continue;
			case '\f': out += "\\f"; ++i; continue;
			case '\n': out += "\\n"; ++i; continue;
			case '\r': out += "\\r"; ++i; continue;
			case '\t': out += "\\t"; ++i; continue;
			default: break;
			}
			if (c < 0x20 || c == 0x7F)
			{
				char buf[8];
				std::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)c);
				out += buf;
				++i;
			}
			else if (c < 0x80)
			{
				out += (char)c;
				++i;
			}
			else
			{
				const size_t begin = i;
				const uint32_t cp = str::DecodeUtf8(s, i);
				if (escapeNonAscii)
				{
					char buf[16];
					if (cp >= 0x10000)
					{
						const uint32_t v = cp - 0x10000;
						std::snprintf(buf, sizeof(buf), "\\u%04x\\u%04x", (unsigned)(0xD800 + (v >> 10)), (unsigned)(0xDC00 + (v & 0x3FF)));
					}
					else
					{
						std::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)cp);
					}
					out += buf;
				}
				else if (cp == 0xFFFD && !(i - begin == 3 && (unsigned char)s[begin] == 0xEF))
				{
					// Invalid UTF-8 byte: emit the replacement character so the output stays valid JSON.
					out += "\xEF\xBF\xBD";
				}
				else
				{
					out.append(s, begin, i - begin);
				}
			}
		}
		out += '"';
	}

	static void FormatDouble(std::string& out, double d)
	{
		if (!std::isfinite(d))
		{
			out += "null"; // JSON has no NaN/Inf
			return;
		}
		char buf[40];
		// Shortest representation that round-trips.
		for (int precision = 6; precision <= 17; ++precision)
		{
			std::snprintf(buf, sizeof(buf), "%.*g", precision, d);
			if (std::strtod(buf, nullptr) == d)
				break;
		}
		std::string s = buf;
		if (s.find_first_of(".eEn") == std::string::npos)
			s += ".0";
		out += s;
	}

	void Json::DumpTo(std::string& out, const DumpOptions& options, int depth) const
	{
		const bool pretty = options.indent >= 0;
		const auto newline = [&](int d) {
			if (!pretty)
				return;
			out += '\n';
			out.append((size_t)d * (size_t)options.indent, ' ');
		};

		switch (type_)
		{
		case Type::Null: out += "null"; break;
		case Type::Bool: out += u_.b ? "true" : "false"; break;
		case Type::Int: out += std::to_string(u_.i); break;
		case Type::Double: FormatDouble(out, u_.d); break;
		case Type::String: EscapeString(out, str_, options.escapeNonAscii); break;
		case Type::Array:
		{
			if (arr_.empty())
			{
				out += "[]";
				break;
			}
			out += '[';
			for (size_t i = 0; i < arr_.size(); ++i)
			{
				if (i)
					out += ',';
				newline(depth + 1);
				arr_[i].DumpTo(out, options, depth + 1);
			}
			newline(depth);
			out += ']';
			break;
		}
		case Type::Object:
		{
			if (obj_.empty())
			{
				out += "{}";
				break;
			}
			std::vector<const Member*> order;
			order.reserve(obj_.size());
			for (const Member& m : obj_)
				order.push_back(&m);
			if (options.sortKeys)
				std::sort(order.begin(), order.end(), [](const Member* a, const Member* b) { return a->first < b->first; });
			out += '{';
			for (size_t i = 0; i < order.size(); ++i)
			{
				if (i)
					out += ',';
				newline(depth + 1);
				EscapeString(out, order[i]->first, options.escapeNonAscii);
				out += pretty ? ": " : ":";
				order[i]->second.DumpTo(out, options, depth + 1);
			}
			newline(depth);
			out += '}';
			break;
		}
		}
	}

	std::string Json::Dump(const DumpOptions& options) const
	{
		std::string out;
		DumpTo(out, options, 0);
		return out;
	}

	// ------------------------------------------------------------------------------------------------
	// parser
	// ------------------------------------------------------------------------------------------------
	namespace
	{
		class Parser
		{
		public:
			Parser(std::string_view text, const Json::ParseOptions& options) : s_(text), opt_(options) {}

			bool Run(Json& out, std::string* error)
			{
				// Skip UTF-8 BOM
				if (s_.size() >= 3 && (unsigned char)s_[0] == 0xEF && (unsigned char)s_[1] == 0xBB && (unsigned char)s_[2] == 0xBF)
					pos_ = 3;
				SkipWs();
				Json value;
				if (!ParseValue(value, 0))
					return Fail(error);
				SkipWs();
				if (!failed_ && pos_ != s_.size())
					SetError("unexpected trailing characters");
				if (failed_)
					return Fail(error);
				out = std::move(value);
				return true;
			}

		private:
			bool Fail(std::string* error)
			{
				if (error)
				{
					size_t line = 1, col = 1;
					for (size_t i = 0; i < errorPos_ && i < s_.size(); ++i)
					{
						if (s_[i] == '\n')
						{
							++line;
							col = 1;
						}
						else
						{
							++col;
						}
					}
					*error = StrFormat("%zu:%zu: %s", line, col, message_.c_str());
				}
				return false;
			}

			void SetError(const char* msg)
			{
				if (!failed_)
				{
					failed_ = true;
					message_ = msg;
					errorPos_ = pos_;
				}
			}

			bool Eof() const { return pos_ >= s_.size(); }

			void SkipWs()
			{
				while (pos_ < s_.size())
				{
					const char c = s_[pos_];
					if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
					{
						++pos_;
					}
					else if (opt_.allowComments && c == '/' && pos_ + 1 < s_.size() && s_[pos_ + 1] == '/')
					{
						while (pos_ < s_.size() && s_[pos_] != '\n')
							++pos_;
					}
					else if (opt_.allowComments && c == '/' && pos_ + 1 < s_.size() && s_[pos_ + 1] == '*')
					{
						pos_ += 2;
						while (pos_ + 1 < s_.size() && !(s_[pos_] == '*' && s_[pos_ + 1] == '/'))
							++pos_;
						pos_ = std::min(pos_ + 2, s_.size());
					}
					else
					{
						break;
					}
				}
			}

			bool ParseValue(Json& out, size_t depth)
			{
				if (depth > opt_.maxDepth)
				{
					SetError("nesting too deep");
					return false;
				}
				if (Eof())
				{
					SetError("unexpected end of input");
					return false;
				}
				const char c = s_[pos_];
				switch (c)
				{
				case '{': return ParseObject(out, depth);
				case '[': return ParseArray(out, depth);
				case '"':
				{
					std::string str;
					if (!ParseString(str))
						return false;
					out = Json(std::move(str));
					return true;
				}
				case 't': return ParseLiteral("true", Json(true), out);
				case 'f': return ParseLiteral("false", Json(false), out);
				case 'n': return ParseLiteral("null", Json(), out);
				default:
					if (c == '-' || (c >= '0' && c <= '9'))
						return ParseNumber(out);
					SetError("unexpected character");
					return false;
				}
			}

			bool ParseLiteral(const char* word, Json value, Json& out)
			{
				const size_t n = std::strlen(word);
				if (s_.compare(pos_, n, word) != 0)
				{
					SetError("invalid literal");
					return false;
				}
				pos_ += n;
				out = std::move(value);
				return true;
			}

			bool ParseNumber(Json& out)
			{
				const size_t start = pos_;
				bool isFloat = false;
				if (s_[pos_] == '-')
					++pos_;
				if (Eof())
				{
					SetError("invalid number");
					return false;
				}
				if (s_[pos_] == '0')
				{
					++pos_;
				}
				else if (s_[pos_] >= '1' && s_[pos_] <= '9')
				{
					while (!Eof() && s_[pos_] >= '0' && s_[pos_] <= '9')
						++pos_;
				}
				else
				{
					SetError("invalid number");
					return false;
				}
				if (!Eof() && s_[pos_] == '.')
				{
					isFloat = true;
					++pos_;
					if (Eof() || s_[pos_] < '0' || s_[pos_] > '9')
					{
						SetError("digit expected after decimal point");
						return false;
					}
					while (!Eof() && s_[pos_] >= '0' && s_[pos_] <= '9')
						++pos_;
				}
				if (!Eof() && (s_[pos_] == 'e' || s_[pos_] == 'E'))
				{
					isFloat = true;
					++pos_;
					if (!Eof() && (s_[pos_] == '+' || s_[pos_] == '-'))
						++pos_;
					if (Eof() || s_[pos_] < '0' || s_[pos_] > '9')
					{
						SetError("digit expected in exponent");
						return false;
					}
					while (!Eof() && s_[pos_] >= '0' && s_[pos_] <= '9')
						++pos_;
				}
				const std::string token(s_.substr(start, pos_ - start));
				if (!isFloat)
				{
					int64_t iv = 0;
					if (str::ParseInt(token, iv))
					{
						out = Json((long long)iv);
						return true;
					}
					// Integer does not fit in 64 bits: fall back to double.
				}
				out = Json(std::strtod(token.c_str(), nullptr));
				return true;
			}

			static int HexValue(char c)
			{
				if (c >= '0' && c <= '9') return c - '0';
				if (c >= 'a' && c <= 'f') return c - 'a' + 10;
				if (c >= 'A' && c <= 'F') return c - 'A' + 10;
				return -1;
			}

			bool ParseHex4(uint32_t& out)
			{
				if (pos_ + 4 > s_.size())
				{
					SetError("incomplete \\u escape");
					return false;
				}
				uint32_t v = 0;
				for (int i = 0; i < 4; ++i)
				{
					const int h = HexValue(s_[pos_ + (size_t)i]);
					if (h < 0)
					{
						SetError("invalid \\u escape");
						return false;
					}
					v = (v << 4) | (uint32_t)h;
				}
				pos_ += 4;
				out = v;
				return true;
			}

			bool ParseString(std::string& out)
			{
				++pos_; // opening quote
				while (true)
				{
					if (Eof())
					{
						SetError("unterminated string");
						return false;
					}
					const unsigned char c = (unsigned char)s_[pos_];
					if (c == '"')
					{
						++pos_;
						return true;
					}
					if (c < 0x20)
					{
						SetError("control character in string");
						return false;
					}
					if (c != '\\')
					{
						out += (char)c;
						++pos_;
						continue;
					}
					++pos_;
					if (Eof())
					{
						SetError("unterminated escape");
						return false;
					}
					const char e = s_[pos_++];
					switch (e)
					{
					case '"': out += '"'; break;
					case '\\': out += '\\'; break;
					case '/': out += '/'; break;
					case 'b': out += '\b'; break;
					case 'f': out += '\f'; break;
					case 'n': out += '\n'; break;
					case 'r': out += '\r'; break;
					case 't': out += '\t'; break;
					case 'u':
					{
						uint32_t cp = 0;
						if (!ParseHex4(cp))
							return false;
						if (cp >= 0xD800 && cp <= 0xDBFF)
						{
							// high surrogate: a low surrogate must follow
							if (pos_ + 1 < s_.size() && s_[pos_] == '\\' && s_[pos_ + 1] == 'u')
							{
								pos_ += 2;
								uint32_t lo = 0;
								if (!ParseHex4(lo))
									return false;
								if (lo >= 0xDC00 && lo <= 0xDFFF)
									cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
								else
									cp = 0xFFFD;
							}
							else
							{
								cp = 0xFFFD;
							}
						}
						else if (cp >= 0xDC00 && cp <= 0xDFFF)
						{
							cp = 0xFFFD;
						}
						str::AppendUtf8(out, cp);
						break;
					}
					default:
						--pos_;
						SetError("invalid escape sequence");
						return false;
					}
				}
			}

			bool ParseArray(Json& out, size_t depth)
			{
				++pos_; // [
				Json::Array items;
				SkipWs();
				if (!Eof() && s_[pos_] == ']')
				{
					++pos_;
					out = Json(std::move(items));
					return true;
				}
				while (true)
				{
					SkipWs();
					Json v;
					if (!ParseValue(v, depth + 1))
						return false;
					items.push_back(std::move(v));
					SkipWs();
					if (Eof())
					{
						SetError("unterminated array");
						return false;
					}
					if (s_[pos_] == ',')
					{
						++pos_;
						SkipWs();
						if (opt_.allowTrailingCommas && !Eof() && s_[pos_] == ']')
						{
							++pos_;
							break;
						}
						continue;
					}
					if (s_[pos_] == ']')
					{
						++pos_;
						break;
					}
					SetError("expected ',' or ']'");
					return false;
				}
				out = Json(std::move(items));
				return true;
			}

			bool ParseObject(Json& out, size_t depth)
			{
				++pos_; // {
				Json::Object members;
				SkipWs();
				if (!Eof() && s_[pos_] == '}')
				{
					++pos_;
					out = Json(std::move(members));
					return true;
				}
				while (true)
				{
					SkipWs();
					if (Eof() || s_[pos_] != '"')
					{
						SetError("expected string key");
						return false;
					}
					std::string key;
					if (!ParseString(key))
						return false;
					SkipWs();
					if (Eof() || s_[pos_] != ':')
					{
						SetError("expected ':'");
						return false;
					}
					++pos_;
					SkipWs();
					Json v;
					if (!ParseValue(v, depth + 1))
						return false;
					// Duplicate keys: the last one wins (like most parsers).
					bool replaced = false;
					for (Json::Member& m : members)
					{
						if (m.first == key)
						{
							m.second = std::move(v);
							replaced = true;
							break;
						}
					}
					if (!replaced)
						members.emplace_back(std::move(key), std::move(v));
					SkipWs();
					if (Eof())
					{
						SetError("unterminated object");
						return false;
					}
					if (s_[pos_] == ',')
					{
						++pos_;
						SkipWs();
						if (opt_.allowTrailingCommas && !Eof() && s_[pos_] == '}')
						{
							++pos_;
							break;
						}
						continue;
					}
					if (s_[pos_] == '}')
					{
						++pos_;
						break;
					}
					SetError("expected ',' or '}'");
					return false;
				}
				out = Json(std::move(members));
				return true;
			}

			std::string_view s_;
			Json::ParseOptions opt_;
			size_t pos_ = 0;
			bool failed_ = false;
			std::string message_;
			size_t errorPos_ = 0;
		};
	} // namespace

	bool Json::Parse(std::string_view text, Json& out, std::string* error, const ParseOptions& options)
	{
		Parser parser(text, options);
		Json result;
		if (!parser.Run(result, error))
		{
			out = Json();
			return false;
		}
		out = std::move(result);
		return true;
	}

	Json Json::ParseOrNull(std::string_view text, const ParseOptions& options)
	{
		Json out;
		if (!Parse(text, out, nullptr, options))
			return Json();
		return out;
	}

} // namespace lumen
