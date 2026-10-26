// lumen.json, lumen.hash, lumen.compress, lumen.zip
#include "LuaUtil.h"
#include "lumen/core/Archive.h"
#include "lumen/core/Fs.h"
#include "lumen/core/Image.h"
#include "lumen/core/Json.h"
#include "lumen/core/Sha256.h"
#include "lumen/core/Strings.h"

#include <algorithm>
#include <cstring>
#include <random>

namespace lumen::lua::detail
{
	// ============================================================== json
	namespace
	{
		const int kMaxDepth = 200;

		const char* JsonType(lua_State* L, int idx)
		{
			if (!lua_getmetatable(L, idx))
				return nullptr;
			lua_getfield(L, -1, "__jsontype");
			const char* t = lua_isstring(L, -1) ? lua_tostring(L, -1) : nullptr;
			lua_pop(L, 2);
			return t;
		}

		bool ToJson(lua_State* L, int idx, Json& out, int depth, bool sortKeys, std::string& err)
		{
			idx = lua_absindex(L, idx);
			luaL_checkstack(L, 8, "json: nesting too deep");
			if (depth > kMaxDepth)
			{
				err = "json: nesting too deep (cycle?)";
				return false;
			}
			switch (lua_type(L, idx))
			{
			case LUA_TNIL: out = Json(); return true;
			case LUA_TBOOLEAN: out = Json(lua_toboolean(L, idx) != 0); return true;
			case LUA_TNUMBER:
				if (lua_isinteger(L, idx))
					out = Json((long long)lua_tointeger(L, idx));
				else
				{
					const double d = lua_tonumber(L, idx);
					if (d != d || d > 1.79e308 || d < -1.79e308)
					{
						err = "json: cannot encode NaN or infinity";
						return false;
					}
					out = Json(d);
				}
				return true;
			case LUA_TSTRING: out = Json(std::string(View(L, idx))); return true;
			case LUA_TLIGHTUSERDATA:
				if (lua_touserdata(L, idx) == nullptr)
				{
					out = Json();
					return true;
				}
				err = "json: cannot encode userdata";
				return false;
			case LUA_TTABLE:
			{
				// __tojson hook
				if (luaL_getmetafield(L, idx, "__tojson") != LUA_TNIL)
				{
					lua_pushvalue(L, idx);
					lua_call(L, 1, 1);
					const bool ok = ToJson(L, -1, out, depth + 1, sortKeys, err);
					lua_pop(L, 1);
					return ok;
				}
				const char* forced = JsonType(L, idx);
				bool isArray = true;
				size_t n = 0, count = 0;
				lua_pushnil(L);
				while (lua_next(L, idx))
				{
					++count;
					if (!(lua_type(L, -2) == LUA_TNUMBER && lua_isinteger(L, -2) && lua_tointeger(L, -2) >= 1))
						isArray = false;
					else
						n = std::max<size_t>(n, (size_t)lua_tointeger(L, -2));
					lua_pop(L, 1);
				}
				if (isArray && n != count)
					isArray = false;
				if (forced && !strcmp(forced, "object"))
					isArray = false;
				else if (forced && !strcmp(forced, "array"))
					isArray = (count == 0) || isArray;
				if (isArray)
				{
					out = Json::MakeArray();
					for (size_t i = 1; i <= n; ++i)
					{
						lua_rawgeti(L, idx, (lua_Integer)i);
						Json v;
						const bool ok = ToJson(L, -1, v, depth + 1, sortKeys, err);
						lua_pop(L, 1);
						if (!ok)
							return false;
						out.Push(std::move(v));
					}
					return true;
				}
				std::vector<std::pair<std::string, int>> keys; // key text, unused
				std::vector<std::string> names;
				lua_pushnil(L);
				while (lua_next(L, idx))
				{
					if (lua_type(L, -2) == LUA_TSTRING)
						names.emplace_back(View(L, -2));
					else if (lua_type(L, -2) == LUA_TNUMBER)
					{
						lua_pushvalue(L, -2);
						names.emplace_back(lua_tostring(L, -1));
						lua_pop(L, 1);
					}
					else
					{
						lua_pop(L, 2);
						err = "json: object keys must be strings or numbers";
						return false;
					}
					lua_pop(L, 1);
				}
				std::sort(names.begin(), names.end());
				out = Json::MakeObject();
				for (const std::string& k : names)
				{
					lua_getfield(L, idx, k.c_str());
					if (lua_isnil(L, -1))
					{
						// numeric key stored as number
						lua_pop(L, 1);
						lua_Number num = 0;
						if (str::ParseDouble(k, num))
						{
							lua_pushnumber(L, num);
							lua_gettable(L, idx);
						}
						else
							lua_pushnil(L);
					}
					Json v;
					const bool ok = ToJson(L, -1, v, depth + 1, sortKeys, err);
					lua_pop(L, 1);
					if (!ok)
						return false;
					if (!v.IsNull() || true)
						out.Set(k, std::move(v));
				}
				return true;
			}
			default:
				err = std::string("json: cannot encode ") + luaL_typename(L, idx);
				return false;
			}
		}

		void PushJson(lua_State* L, const Json& j, bool keepNull)
		{
			luaL_checkstack(L, 6, "json: nesting too deep");
			switch (j.GetType())
			{
			case Json::Type::Null:
				if (keepNull)
					lua_pushlightuserdata(L, nullptr);
				else
					lua_pushnil(L);
				break;
			case Json::Type::Bool: lua_pushboolean(L, j.AsBool()); break;
			case Json::Type::Int: lua_pushinteger(L, (lua_Integer)j.AsInt()); break;
			case Json::Type::Double: lua_pushnumber(L, j.AsDouble()); break;
			case Json::Type::String: Push(L, j.AsString()); break;
			case Json::Type::Array:
				lua_createtable(L, (int)j.Size(), 0);
				for (size_t i = 0; i < j.Size(); ++i)
				{
					PushJson(L, j.At(i), keepNull);
					lua_rawseti(L, -2, (lua_Integer)i + 1);
				}
				break;
			case Json::Type::Object:
				lua_createtable(L, 0, (int)j.Size());
				for (const Json::Member& m : j.Members())
				{
					PushJson(L, m.second, keepNull);
					lua_setfield(L, -2, m.first.c_str());
				}
				break;
			}
		}

		int JsonEncode(lua_State* L)
		{
			luaL_checkany(L, 1);
			Json j;
			std::string err;
			bool sort = FieldBool(L, 2, "sort_keys", true);
			if (!ToJson(L, 1, j, 0, sort, err))
				return Fail(L, err);
			JsonDumpOptions o;
			o.indent = (int)FieldNum(L, 2, "indent", -1);
			o.sortKeys = false; // already sorted when requested
			o.escapeNonAscii = FieldBool(L, 2, "ascii", false);
			Push(L, j.Dump(o));
			return 1;
		}

		int JsonDecode(lua_State* L)
		{
			const std::string_view text = View(L, 1);
			JsonParseOptions po;
			po.allowComments = FieldBool(L, 2, "comments", false);
			po.allowTrailingCommas = FieldBool(L, 2, "trailing_commas", false);
			Json j;
			std::string err;
			if (!Json::Parse(text, j, &err, po))
				return Fail(L, err);
			PushJson(L, j, FieldBool(L, 2, "keep_null", false));
			return 1;
		}

		int JsonLoad(lua_State* L)
		{
			const std::string path = Str(L, 1);
			if (!PathAllowed(L, path))
				return Fail(L, "access denied: " + path);
			std::string text;
			if (!fs::ReadText(path, text))
				return Fail(L, "cannot read " + path);
			JsonParseOptions po;
			po.allowComments = FieldBool(L, 2, "comments", true);
			po.allowTrailingCommas = FieldBool(L, 2, "trailing_commas", true);
			Json j;
			std::string err;
			if (!Json::Parse(text, j, &err, po))
				return Fail(L, path + ":" + err);
			PushJson(L, j, FieldBool(L, 2, "keep_null", false));
			return 1;
		}

		int JsonSave(lua_State* L)
		{
			const std::string path = Str(L, 1);
			if (!PathAllowed(L, path))
				return Fail(L, "access denied: " + path);
			Json j;
			std::string err;
			if (!ToJson(L, 2, j, 0, true, err))
				return Fail(L, err);
			JsonDumpOptions o;
			o.indent = (int)FieldNum(L, 3, "indent", 2);
			const std::string text = j.Dump(o) + "\n";
			fs::CreateDirs(fs::Parent(fs::Absolute(path)));
			if (!fs::WriteAtomic(path, text.data(), text.size()))
				return Fail(L, "cannot write " + path);
			lua_pushboolean(L, 1);
			return 1;
		}

		int JsonArray(lua_State* L)
		{
			if (lua_istable(L, 1))
				lua_pushvalue(L, 1);
			else
				lua_newtable(L);
			lua_newtable(L);
			lua_pushstring(L, "array");
			lua_setfield(L, -2, "__jsontype");
			lua_setmetatable(L, -2);
			return 1;
		}
		int JsonObject(lua_State* L)
		{
			if (lua_istable(L, 1))
				lua_pushvalue(L, 1);
			else
				lua_newtable(L);
			lua_newtable(L);
			lua_pushstring(L, "object");
			lua_setfield(L, -2, "__jsontype");
			lua_setmetatable(L, -2);
			return 1;
		}
	} // namespace

	int OpenJson(lua_State* L)
	{
		static const luaL_Reg fns[] = {{"encode", JsonEncode}, {"decode", JsonDecode}, {"load", JsonLoad}, {"save", JsonSave},
			{"array", JsonArray}, {"object", JsonObject}, {nullptr, nullptr}};
		luaL_newlib(L, fns);
		lua_pushlightuserdata(L, nullptr);
		lua_setfield(L, -2, "null");
		return 1;
	}

	// ============================================================== hash / encodings
	namespace
	{
		uint32_t Rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

		struct Md5
		{
			static std::string Digest(std::string_view in)
			{
				static const uint32_t K[64] = {
					0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1,
					0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453,
					0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942,
					0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
					0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d,
					0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
				static const int S[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
					4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
				uint32_t a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;
				std::string msg(in);
				msg.push_back((char)0x80);
				while (msg.size() % 64 != 56)
					msg.push_back(0);
				const uint64_t bits = (uint64_t)in.size() * 8;
				for (int i = 0; i < 8; ++i)
					msg.push_back((char)((bits >> (8 * i)) & 0xff));
				for (size_t off = 0; off < msg.size(); off += 64)
				{
					uint32_t M[16];
					for (int i = 0; i < 16; ++i)
					{
						const uint8_t* p = (const uint8_t*)&msg[off + i * 4];
						M[i] = p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
					}
					uint32_t A = a0, B = b0, C = c0, D = d0;
					for (int i = 0; i < 64; ++i)
					{
						uint32_t F;
						int g;
						if (i < 16) { F = (B & C) | (~B & D); g = i; }
						else if (i < 32) { F = (D & B) | (~D & C); g = (5 * i + 1) % 16; }
						else if (i < 48) { F = B ^ C ^ D; g = (3 * i + 5) % 16; }
						else { F = C ^ (B | ~D); g = (7 * i) % 16; }
						F = F + A + K[i] + M[g];
						A = D; D = C; C = B;
						B = B + Rol(F, S[i]);
					}
					a0 += A; b0 += B; c0 += C; d0 += D;
				}
				std::string out(16, 0);
				const uint32_t v[4] = {a0, b0, c0, d0};
				for (int i = 0; i < 4; ++i)
					for (int j = 0; j < 4; ++j)
						out[i * 4 + j] = (char)((v[i] >> (8 * j)) & 0xff);
				return out;
			}
		};

		std::string Sha1(std::string_view in)
		{
			uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
			std::string msg(in);
			msg.push_back((char)0x80);
			while (msg.size() % 64 != 56)
				msg.push_back(0);
			const uint64_t bits = (uint64_t)in.size() * 8;
			for (int i = 7; i >= 0; --i)
				msg.push_back((char)((bits >> (8 * i)) & 0xff));
			for (size_t off = 0; off < msg.size(); off += 64)
			{
				uint32_t w[80];
				for (int i = 0; i < 16; ++i)
				{
					const uint8_t* p = (const uint8_t*)&msg[off + i * 4];
					w[i] = ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
				}
				for (int i = 16; i < 80; ++i)
					w[i] = Rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
				uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
				for (int i = 0; i < 80; ++i)
				{
					uint32_t f, k;
					if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
					else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
					else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
					else { f = b ^ c ^ d; k = 0xCA62C1D6; }
					const uint32_t t = Rol(a, 5) + f + e + k + w[i];
					e = d; d = c; c = Rol(b, 30); b = a; a = t;
				}
				h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
			}
			std::string out(20, 0);
			for (int i = 0; i < 5; ++i)
				for (int j = 0; j < 4; ++j)
					out[i * 4 + j] = (char)((h[i] >> (24 - 8 * j)) & 0xff);
			return out;
		}

		std::string Sha256Raw(std::string_view in)
		{
			Sha256 s;
			s.Update(in);
			uint8_t d[32];
			s.Final(d);
			return std::string((const char*)d, 32);
		}

		using HashFn = std::string (*)(std::string_view);
		std::string Hmac(HashFn h, std::string_view key, std::string_view msg)
		{
			std::string k(key);
			if (k.size() > 64)
				k = h(k);
			k.resize(64, 0);
			std::string ip(64, 0), op(64, 0);
			for (int i = 0; i < 64; ++i)
			{
				ip[i] = (char)(k[i] ^ 0x36);
				op[i] = (char)(k[i] ^ 0x5c);
			}
			return h(op + h(ip + std::string(msg)));
		}

		HashFn Algo(lua_State* L, int idx)
		{
			const std::string n = Str(L, idx);
			if (n == "md5") return [](std::string_view s) { return Md5::Digest(s); };
			if (n == "sha1") return Sha1;
			if (n == "sha256") return Sha256Raw;
			luaL_error(L, "unknown hash '%s' (md5, sha1, sha256)", n.c_str());
			return nullptr;
		}

		std::string Hex(std::string_view s)
		{
			return str::ToHex((const uint8_t*)s.data(), s.size());
		}

		const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		const char kB64Url[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

		std::string B64Encode(std::string_view in, bool url, bool pad)
		{
			const char* tbl = url ? kB64Url : kB64;
			std::string out;
			size_t i = 0;
			for (; i + 2 < in.size(); i += 3)
			{
				const uint32_t v = ((uint8_t)in[i] << 16) | ((uint8_t)in[i + 1] << 8) | (uint8_t)in[i + 2];
				out += tbl[v >> 18];
				out += tbl[(v >> 12) & 63];
				out += tbl[(v >> 6) & 63];
				out += tbl[v & 63];
			}
			if (i + 1 == in.size())
			{
				const uint32_t v = (uint8_t)in[i] << 16;
				out += tbl[v >> 18];
				out += tbl[(v >> 12) & 63];
				if (pad)
					out += "==";
			}
			else if (i + 2 == in.size())
			{
				const uint32_t v = ((uint8_t)in[i] << 16) | ((uint8_t)in[i + 1] << 8);
				out += tbl[v >> 18];
				out += tbl[(v >> 12) & 63];
				out += tbl[(v >> 6) & 63];
				if (pad)
					out += '=';
			}
			return out;
		}

		bool B64Decode(std::string_view in, std::string& out)
		{
			uint32_t acc = 0;
			int bits = 0;
			for (char c : in)
			{
				int v;
				if (c >= 'A' && c <= 'Z') v = c - 'A';
				else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
				else if (c >= '0' && c <= '9') v = c - '0' + 52;
				else if (c == '+' || c == '-') v = 62;
				else if (c == '/' || c == '_') v = 63;
				else if (c == '=' || c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
				else return false;
				acc = (acc << 6) | (uint32_t)v;
				bits += 6;
				if (bits >= 8)
				{
					bits -= 8;
					out.push_back((char)((acc >> bits) & 0xff));
				}
			}
			return true;
		}

		int HashDigest(lua_State* L, HashFn f)
		{
			const std::string d = f(View(L, 1));
			if (lua_toboolean(L, 2))
				Push(L, d);
			else
				Push(L, Hex(d));
			return 1;
		}

		int HMd5(lua_State* L) { return HashDigest(L, [](std::string_view s) { return Md5::Digest(s); }); }
		int HSha1(lua_State* L) { return HashDigest(L, Sha1); }
		int HSha256(lua_State* L) { return HashDigest(L, Sha256Raw); }
		int HFile(lua_State* L)
		{
			const std::string path = Str(L, 1);
			if (!PathAllowed(L, path))
				return Fail(L, "access denied: " + path);
			if (!fs::IsFile(path))
				return Fail(L, "no such file: " + path);
			Push(L, Sha256::HashFileHex(path));
			return 1;
		}
		int HHmac(lua_State* L)
		{
			HashFn f = Algo(L, 1);
			const std::string d = Hmac(f, View(L, 2), View(L, 3));
			if (lua_toboolean(L, 4))
				Push(L, d);
			else
				Push(L, Hex(d));
			return 1;
		}
		int HCrc32(lua_State* L)
		{
			lua_pushinteger(L, image::Crc32((const uint8_t*)View(L, 1).data(), View(L, 1).size()));
			return 1;
		}
		int HAdler32(lua_State* L)
		{
			const std::string_view s = View(L, 1);
			uint32_t a = 1, b = 0;
			for (unsigned char c : s)
			{
				a = (a + c) % 65521;
				b = (b + a) % 65521;
			}
			lua_pushinteger(L, (lua_Integer)((b << 16) | a));
			return 1;
		}
		int HFnv(lua_State* L)
		{
			lua_pushinteger(L, (lua_Integer)str::Hash64(View(L, 1)));
			return 1;
		}
		int HB64Enc(lua_State* L)
		{
			Push(L, B64Encode(View(L, 1), OptBool(L, 2, false), OptBool(L, 3, true)));
			return 1;
		}
		int HB64Dec(lua_State* L)
		{
			std::string out;
			if (!B64Decode(View(L, 1), out))
				return Fail(L, "invalid base64");
			Push(L, out);
			return 1;
		}
		int HHexEnc(lua_State* L)
		{
			Push(L, Hex(View(L, 1)));
			return 1;
		}
		int HHexDec(lua_State* L)
		{
			const std::string_view s = View(L, 1);
			if (s.size() % 2)
				return Fail(L, "odd length hex");
			std::string out;
			for (size_t i = 0; i < s.size(); i += 2)
			{
				auto nib = [](char c) -> int {
					if (c >= '0' && c <= '9') return c - '0';
					if (c >= 'a' && c <= 'f') return c - 'a' + 10;
					if (c >= 'A' && c <= 'F') return c - 'A' + 10;
					return -1;
				};
				const int a = nib(s[i]), b = nib(s[i + 1]);
				if (a < 0 || b < 0)
					return Fail(L, "invalid hex");
				out.push_back((char)(a * 16 + b));
			}
			Push(L, out);
			return 1;
		}
		int HRandomBytes(lua_State* L)
		{
			const lua_Integer n = luaL_checkinteger(L, 1);
			luaL_argcheck(L, n >= 0 && n <= (1 << 24), 1, "size out of range");
			std::random_device rd;
			std::string out((size_t)n, 0);
			for (auto& c : out)
				c = (char)(rd() & 0xff);
			Push(L, out);
			return 1;
		}
		int HUuid(lua_State* L)
		{
			std::random_device rd;
			uint8_t b[16];
			for (auto& c : b)
				c = (uint8_t)(rd() & 0xff);
			b[6] = (b[6] & 0x0f) | 0x40;
			b[8] = (b[8] & 0x3f) | 0x80;
			std::string h = str::ToHex(b, 16);
			Push(L, h.substr(0, 8) + "-" + h.substr(8, 4) + "-" + h.substr(12, 4) + "-" + h.substr(16, 4) + "-" + h.substr(20));
			return 1;
		}
		// constant time compare
		int HEqual(lua_State* L)
		{
			const std::string_view a = View(L, 1), b = View(L, 2);
			unsigned diff = (unsigned)(a.size() ^ b.size());
			for (size_t i = 0; i < std::min(a.size(), b.size()); ++i)
				diff |= (unsigned char)(a[i] ^ b[i]);
			lua_pushboolean(L, diff == 0);
			return 1;
		}
		// PBKDF2-HMAC
		int HPbkdf2(lua_State* L)
		{
			HashFn f = Algo(L, 1);
			const std::string_view pass = View(L, 2), salt = View(L, 3);
			const lua_Integer iter = luaL_checkinteger(L, 4), len = luaL_checkinteger(L, 5);
			luaL_argcheck(L, iter >= 1 && iter <= 10000000, 4, "iterations out of range");
			luaL_argcheck(L, len >= 1 && len <= 1024, 5, "length out of range");
			std::string out;
			for (uint32_t block = 1; out.size() < (size_t)len; ++block)
			{
				std::string s(salt);
				for (int i = 3; i >= 0; --i)
					s.push_back((char)((block >> (8 * i)) & 0xff));
				std::string u = Hmac(f, pass, s), t = u;
				for (lua_Integer i = 1; i < iter; ++i)
				{
					u = Hmac(f, pass, u);
					for (size_t j = 0; j < t.size(); ++j)
						t[j] = (char)(t[j] ^ u[j]);
				}
				out += t;
			}
			out.resize((size_t)len);
			Push(L, lua_toboolean(L, 6) ? out : Hex(out));
			return 1;
		}
	} // namespace

	int OpenHash(lua_State* L)
	{
		static const luaL_Reg fns[] = {{"md5", HMd5}, {"sha1", HSha1}, {"sha256", HSha256}, {"sha256_file", HFile}, {"hmac", HHmac},
			{"crc32", HCrc32}, {"adler32", HAdler32}, {"fnv1a64", HFnv}, {"base64_encode", HB64Enc}, {"base64_decode", HB64Dec},
			{"hex_encode", HHexEnc}, {"hex_decode", HHexDec}, {"random_bytes", HRandomBytes}, {"uuid4", HUuid}, {"equal", HEqual},
			{"pbkdf2", HPbkdf2}, {nullptr, nullptr}};
		luaL_newlib(L, fns);
		return 1;
	}

	// ============================================================== compress (deflate / zlib / gzip)
	namespace
	{
		struct BitWriter
		{
			std::vector<uint8_t>& out;
			uint64_t acc = 0;
			int n = 0;
			explicit BitWriter(std::vector<uint8_t>& o) : out(o) {}
			void Bits(uint32_t v, int count)
			{
				acc |= (uint64_t)v << n;
				n += count;
				while (n >= 8)
				{
					out.push_back((uint8_t)(acc & 0xff));
					acc >>= 8;
					n -= 8;
				}
			}
			void Code(uint32_t code, int len) // Huffman codes are sent MSB first
			{
				uint32_t r = 0;
				for (int i = 0; i < len; ++i)
					r |= ((code >> i) & 1u) << (len - 1 - i);
				Bits(r, len);
			}
			void Flush()
			{
				if (n > 0)
					out.push_back((uint8_t)(acc & 0xff));
				acc = 0;
				n = 0;
			}
		};

		const uint16_t kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
		const uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
		const uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
		const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

		void FixedLit(BitWriter& w, int sym)
		{
			if (sym < 144) w.Code(0x30 + sym, 8);
			else if (sym < 256) w.Code(0x190 + (sym - 144), 9);
			else if (sym < 280) w.Code(sym - 256, 7);
			else w.Code(0xC0 + (sym - 280), 8);
		}

		// DEFLATE with LZ77 (hash chains, lazy matching) and fixed Huffman codes, split in blocks of at most 1 MiB of input.
		std::vector<uint8_t> Deflate(const uint8_t* data, size_t size, int level)
		{
			std::vector<uint8_t> out;
			out.reserve(size / 2 + 64);
			BitWriter w(out);
			if (level <= 0)
			{
				// stored blocks
				size_t pos = 0;
				do
				{
					const size_t chunk = std::min<size_t>(65535, size - pos);
					const bool last = pos + chunk >= size;
					w.Bits(last ? 1 : 0, 1);
					w.Bits(0, 2);
					w.Flush();
					out.push_back((uint8_t)(chunk & 0xff));
					out.push_back((uint8_t)(chunk >> 8));
					out.push_back((uint8_t)(~chunk & 0xff));
					out.push_back((uint8_t)((~chunk >> 8) & 0xff));
					out.insert(out.end(), data + pos, data + pos + chunk);
					pos += chunk;
				} while (pos < size);
				return out;
			}
			const int maxChain = level >= 9 ? 512 : level >= 6 ? 128 : 32;
			const int HBITS = 15;
			std::vector<int32_t> head(1u << HBITS, -1);
			std::vector<int32_t> prev(32768, -1);
			auto hash3 = [&](size_t i) { return (uint32_t)(((data[i] << 10) ^ (data[i + 1] << 5) ^ data[i + 2]) & ((1u << HBITS) - 1)); };
			auto insert = [&](size_t i) {
				if (i + 2 < size)
				{
					const uint32_t h = hash3(i);
					prev[i & 32767] = head[h];
					head[h] = (int32_t)i;
				}
			};
			auto findMatch = [&](size_t i, int& bestLen, int& bestDist) {
				bestLen = 0;
				bestDist = 0;
				if (i + 3 > size)
					return;
				int32_t cand = head[hash3(i)];
				int chain = maxChain;
				const size_t maxLen = std::min<size_t>(258, size - i);
				while (cand >= 0 && chain-- > 0)
				{
					const size_t dist = i - (size_t)cand;
					if (dist > 32768 || dist == 0)
						break;
					if (data[cand + bestLen] == data[i + bestLen] || bestLen == 0)
					{
						size_t l = 0;
						while (l < maxLen && data[cand + l] == data[i + l])
							++l;
						if ((int)l > bestLen)
						{
							bestLen = (int)l;
							bestDist = (int)dist;
							if (l == maxLen)
								break;
						}
					}
					const int32_t next = prev[(size_t)cand & 32767];
					if (next >= cand)
						break;
					cand = next;
				}
				if (bestLen < 3)
					bestLen = 0;
			};
			w.Bits(1, 1); // BFINAL
			w.Bits(1, 2); // fixed Huffman
			size_t i = 0;
			while (i < size)
			{
				int len, dist;
				findMatch(i, len, dist);
				if (len >= 3 && level >= 4 && i + 1 < size)
				{
					insert(i);
					int len2, dist2;
					findMatch(i + 1, len2, dist2);
					if (len2 > len + 1)
					{
						FixedLit(w, data[i]);
						++i;
						continue;
					}
					// emit match at i (already inserted i)
					int lc = 28;
					while (kLenBase[lc] > len) --lc;
					FixedLit(w, 257 + lc);
					if (kLenExtra[lc]) w.Bits((uint32_t)(len - kLenBase[lc]), kLenExtra[lc]);
					int dc = 29;
					while (kDistBase[dc] > dist) --dc;
					w.Code((uint32_t)dc, 5);
					if (kDistExtra[dc]) w.Bits((uint32_t)(dist - kDistBase[dc]), kDistExtra[dc]);
					for (int k = 1; k < len; ++k)
						insert(i + k);
					i += len;
					continue;
				}
				if (len >= 3)
				{
					int lc = 28;
					while (kLenBase[lc] > len) --lc;
					FixedLit(w, 257 + lc);
					if (kLenExtra[lc]) w.Bits((uint32_t)(len - kLenBase[lc]), kLenExtra[lc]);
					int dc = 29;
					while (kDistBase[dc] > dist) --dc;
					w.Code((uint32_t)dc, 5);
					if (kDistExtra[dc]) w.Bits((uint32_t)(dist - kDistBase[dc]), kDistExtra[dc]);
					for (int k = 0; k < len; ++k)
						insert(i + k);
					i += len;
				}
				else
				{
					FixedLit(w, data[i]);
					insert(i);
					++i;
				}
			}
			FixedLit(w, 256);
			w.Flush();
			return out;
		}

		uint32_t Adler(const uint8_t* d, size_t n)
		{
			uint32_t a = 1, b = 0;
			for (size_t i = 0; i < n; ++i)
			{
				a = (a + d[i]) % 65521;
				b = (b + a) % 65521;
			}
			return (b << 16) | a;
		}

		int CLevel(lua_State* L, int idx)
		{
			return lua_isnoneornil(L, idx) ? 6 : (int)luaL_checkinteger(L, idx);
		}
		int PushBytes(lua_State* L, const std::vector<uint8_t>& v)
		{
			lua_pushlstring(L, (const char*)v.data(), v.size());
			return 1;
		}
		int CDeflate(lua_State* L)
		{
			const std::string_view s = View(L, 1);
			return PushBytes(L, Deflate((const uint8_t*)s.data(), s.size(), CLevel(L, 2)));
		}
		int CInflate(lua_State* L)
		{
			const std::string_view s = View(L, 1);
			std::vector<uint8_t> out;
			const lua_Integer maxOut = lua_isnoneornil(L, 2) ? (lua_Integer)(1 << 30) : luaL_checkinteger(L, 2);
			if (!archive::Inflate((const uint8_t*)s.data(), s.size(), out, 0, (uint64_t)maxOut))
				return Fail(L, "invalid deflate data");
			return PushBytes(L, out);
		}
		int CZlibCompress(lua_State* L)
		{
			const std::string_view s = View(L, 1);
			std::vector<uint8_t> out = {0x78, 0x9C};
			const std::vector<uint8_t> d = Deflate((const uint8_t*)s.data(), s.size(), CLevel(L, 2));
			out.insert(out.end(), d.begin(), d.end());
			const uint32_t a = Adler((const uint8_t*)s.data(), s.size());
			for (int i = 3; i >= 0; --i)
				out.push_back((uint8_t)((a >> (8 * i)) & 0xff));
			return PushBytes(L, out);
		}
		int CZlibDecompress(lua_State* L)
		{
			const std::string_view s = View(L, 1);
			if (s.size() < 6 || ((uint8_t)s[0] & 0x0f) != 8 || (((uint8_t)s[0] << 8) | (uint8_t)s[1]) % 31 != 0)
				return Fail(L, "not zlib data");
			std::vector<uint8_t> out;
			if (!archive::Inflate((const uint8_t*)s.data() + 2, s.size() - 2, out))
				return Fail(L, "invalid zlib data");
			const uint8_t* t = (const uint8_t*)s.data() + s.size() - 4;
			const uint32_t want = ((uint32_t)t[0] << 24) | (t[1] << 16) | (t[2] << 8) | t[3];
			if (want != Adler(out.data(), out.size()))
				return Fail(L, "zlib checksum mismatch");
			return PushBytes(L, out);
		}
		int CGzip(lua_State* L)
		{
			const std::string_view s = View(L, 1);
			std::vector<uint8_t> out = {0x1f, 0x8b, 8, 0, 0, 0, 0, 0, 0, 0xff};
			const std::vector<uint8_t> d = Deflate((const uint8_t*)s.data(), s.size(), CLevel(L, 2));
			out.insert(out.end(), d.begin(), d.end());
			const uint32_t crc = image::Crc32((const uint8_t*)s.data(), s.size());
			for (int i = 0; i < 4; ++i)
				out.push_back((uint8_t)((crc >> (8 * i)) & 0xff));
			for (int i = 0; i < 4; ++i)
				out.push_back((uint8_t)(((uint32_t)s.size() >> (8 * i)) & 0xff));
			return PushBytes(L, out);
		}
		int CGunzip(lua_State* L)
		{
			const std::string_view s = View(L, 1);
			std::vector<uint8_t> out;
			if (!archive::Gunzip((const uint8_t*)s.data(), s.size(), out))
				return Fail(L, "invalid gzip data");
			return PushBytes(L, out);
		}
	} // namespace

	int OpenCompress(lua_State* L)
	{
		static const luaL_Reg fns[] = {{"deflate", CDeflate}, {"inflate", CInflate}, {"zlib_compress", CZlibCompress},
			{"zlib_decompress", CZlibDecompress}, {"gzip", CGzip}, {"gunzip", CGunzip}, {nullptr, nullptr}};
		luaL_newlib(L, fns);
		return 1;
	}

	// ============================================================== zip
	namespace
	{
		struct ZipR
		{
			archive::ZipReader reader;
		};
		struct ZipW
		{
			archive::ZipWriter writer;
			bool done = false;
		};

		ZipR* CheckZr(lua_State* L) { return static_cast<ZipR*>(luaL_checkudata(L, 1, "lumen.zip.reader")); }
		ZipW* CheckZw(lua_State* L) { return static_cast<ZipW*>(luaL_checkudata(L, 1, "lumen.zip.writer")); }

		int ZOpen(lua_State* L)
		{
			const std::string path = Str(L, 1);
			if (!PathAllowed(L, path))
				return Fail(L, "access denied: " + path);
			ZipR* z = NewUdata<ZipR>(L, "lumen.zip.reader");
			Status s = z->reader.Open(path);
			if (!s)
				return Fail(L, s.message);
			return 1;
		}
		int ZOpenMemory(lua_State* L)
		{
			const std::string_view d = View(L, 1);
			ZipR* z = NewUdata<ZipR>(L, "lumen.zip.reader");
			Status s = z->reader.OpenMemory(std::vector<uint8_t>(d.begin(), d.end()));
			if (!s)
				return Fail(L, s.message);
			return 1;
		}
		int ZList(lua_State* L)
		{
			ZipR* z = CheckZr(L);
			lua_newtable(L);
			lua_Integer i = 1;
			for (const archive::ZipEntry& e : z->reader.Entries())
			{
				lua_newtable(L);
				Push(L, e.name);
				lua_setfield(L, -2, "name");
				lua_pushinteger(L, (lua_Integer)e.size);
				lua_setfield(L, -2, "size");
				lua_pushinteger(L, (lua_Integer)e.compressedSize);
				lua_setfield(L, -2, "compressed");
				lua_pushboolean(L, e.IsDirectory());
				lua_setfield(L, -2, "dir");
				lua_rawseti(L, -2, i++);
			}
			return 1;
		}
		int ZRead(lua_State* L)
		{
			ZipR* z = CheckZr(L);
			const archive::ZipEntry* e = z->reader.Find(Str(L, 2));
			if (!e)
				return Fail(L, "no such entry");
			std::vector<uint8_t> out;
			Status s = z->reader.Read(*e, out);
			if (!s)
				return Fail(L, s.message);
			return PushBytes(L, out);
		}
		int ZExists(lua_State* L)
		{
			lua_pushboolean(L, CheckZr(L)->reader.Find(Str(L, 2)) != nullptr);
			return 1;
		}
		int ZExtractAll(lua_State* L)
		{
			ZipR* z = CheckZr(L);
			const std::string dest = Str(L, 2);
			if (!PathAllowed(L, dest))
				return Fail(L, "access denied: " + dest);
			int count = 0;
			for (const archive::ZipEntry& e : z->reader.Entries())
			{
				const std::string rel = archive::SafeEntryPath(e.name, 0);
				if (rel.empty() || e.IsSymlink())
					continue;
				const std::string out = fs::Join(dest, rel);
				if (e.IsDirectory())
				{
					fs::CreateDirs(out);
					continue;
				}
				std::vector<uint8_t> data;
				Status s = z->reader.Read(e, data);
				if (!s)
					return Fail(L, s.message);
				fs::CreateDirs(fs::Parent(out));
				if (!fs::WriteBinary(out, data.data(), data.size()))
					return Fail(L, "cannot write " + out);
				++count;
			}
			lua_pushinteger(L, count);
			return 1;
		}
		int ZCreate(lua_State* L)
		{
			NewUdata<ZipW>(L, "lumen.zip.writer");
			return 1;
		}
		int ZwAdd(lua_State* L)
		{
			ZipW* z = CheckZw(L);
			if (z->done)
				return luaL_error(L, "zip writer already finished");
			const std::string name = Str(L, 2);
			if (archive::SafeEntryPath(name, 0).empty())
				return luaL_error(L, "unsafe entry name '%s'", name.c_str());
			const std::string_view d = View(L, 3);
			z->writer.AddFile(name, d.data(), d.size(), OptBool(L, 4, false));
			lua_settop(L, 1);
			return 1;
		}
		int ZwAddFile(lua_State* L)
		{
			ZipW* z = CheckZw(L);
			const std::string name = Str(L, 2), path = Str(L, 3);
			if (!PathAllowed(L, path))
				return Fail(L, "access denied: " + path);
			if (archive::SafeEntryPath(name, 0).empty())
				return luaL_error(L, "unsafe entry name '%s'", name.c_str());
			Status s = z->writer.AddDiskFile(name, path);
			if (!s)
				return Fail(L, s.message);
			lua_settop(L, 1);
			return 1;
		}
		int ZwAddDir(lua_State* L)
		{
			ZipW* z = CheckZw(L);
			const std::string dir = Str(L, 2);
			const std::string prefix = lua_isnoneornil(L, 3) ? "" : Str(L, 3);
			if (!PathAllowed(L, dir))
				return Fail(L, "access denied: " + dir);
			fs::ListOptions lo;
			lo.recursive = true;
			int n = 0;
			Status err = Status::Ok();
			fs::Walk(dir, lo, [&](const fs::DirEntry& e) {
				if (e.isDirectory)
					return true;
				Status s = z->writer.AddDiskFile(prefix + fs::Relative(e.path, dir), e.path);
				if (!s)
				{
					err = s;
					return false;
				}
				++n;
				return true;
			});
			if (!err)
				return Fail(L, err.message);
			lua_pushinteger(L, n);
			return 1;
		}
		int ZwSave(lua_State* L)
		{
			ZipW* z = CheckZw(L);
			const std::string path = Str(L, 2);
			if (!PathAllowed(L, path))
				return Fail(L, "access denied: " + path);
			fs::CreateDirs(fs::Parent(fs::Absolute(path)));
			Status s = z->writer.Save(path);
			z->done = true;
			if (!s)
				return Fail(L, s.message);
			lua_pushboolean(L, 1);
			return 1;
		}
		int ZwString(lua_State* L)
		{
			ZipW* z = CheckZw(L);
			z->done = true;
			return PushBytes(L, z->writer.Finish());
		}
	} // namespace

	int OpenZip(lua_State* L)
	{
		static const luaL_Reg rm[] = {{"list", ZList}, {"read", ZRead}, {"exists", ZExists}, {"extract_all", ZExtractAll}, {nullptr, nullptr}};
		static const luaL_Reg wm[] = {{"add", ZwAdd}, {"add_file", ZwAddFile}, {"add_dir", ZwAddDir}, {"save", ZwSave}, {"tostring", ZwString}, {nullptr, nullptr}};
		MakeClass(L, "lumen.zip.reader", rm, nullptr, GcUdata<ZipR>);
		MakeClass(L, "lumen.zip.writer", wm, nullptr, GcUdata<ZipW>);
		static const luaL_Reg fns[] = {{"open", ZOpen}, {"open_memory", ZOpenMemory}, {"create", ZCreate}, {nullptr, nullptr}};
		luaL_newlib(L, fns);
		return 1;
	}
} // namespace lumen::lua::detail
