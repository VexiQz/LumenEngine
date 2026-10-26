// Internal helpers shared by the native Lua modules.
#pragma once

#include "lumen/core/Common.h"
#include "lumen/core/LuaLibs.h"

extern "C"
{
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

namespace lumen::lua::detail
{
	const LibsPolicy& Policy(lua_State* L);
	void SetPolicy(lua_State* L, const LibsPolicy& p);
	// true when the path may be accessed; otherwise pushes nothing and the caller raises/returns the error
	bool PathAllowed(lua_State* L, const std::string& path);
	inline int Fail(lua_State* L, const std::string& msg)
	{
		lua_pushnil(L);
		lua_pushlstring(L, msg.data(), msg.size());
		return 2;
	}
	inline std::string Str(lua_State* L, int i)
	{
		size_t n;
		const char* s = luaL_checklstring(L, i, &n);
		return std::string(s, n);
	}
	inline std::string_view View(lua_State* L, int i)
	{
		size_t n;
		const char* s = luaL_checklstring(L, i, &n);
		return std::string_view(s, n);
	}
	inline void Push(lua_State* L, std::string_view s) { lua_pushlstring(L, s.data(), s.size()); }
	inline bool OptBool(lua_State* L, int i, bool def)
	{
		return lua_isnoneornil(L, i) ? def : lua_toboolean(L, i) != 0;
	}
	// table field helpers (idx must be absolute)
	inline bool FieldBool(lua_State* L, int idx, const char* k, bool def)
	{
		if (!lua_istable(L, idx)) return def;
		lua_getfield(L, idx, k);
		bool v = lua_isnil(L, -1) ? def : lua_toboolean(L, -1) != 0;
		lua_pop(L, 1);
		return v;
	}
	inline lua_Number FieldNum(lua_State* L, int idx, const char* k, lua_Number def)
	{
		if (!lua_istable(L, idx)) return def;
		lua_getfield(L, idx, k);
		lua_Number v = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : def;
		lua_pop(L, 1);
		return v;
	}
	inline std::string FieldStr(lua_State* L, int idx, const char* k, const std::string& def = {})
	{
		if (!lua_istable(L, idx)) return def;
		lua_getfield(L, idx, k);
		std::string v = lua_isstring(L, -1) ? std::string(lua_tostring(L, -1)) : def;
		lua_pop(L, 1);
		return v;
	}
	// Creates a userdata holding a T (placement new) with metatable `meta` (must be registered). Destructor via __gc.
	template <typename T, typename... A>
	T* NewUdata(lua_State* L, const char* meta, A&&... a)
	{
		void* mem = lua_newuserdatauv(L, sizeof(T), 0);
		T* t = new (mem) T(std::forward<A>(a)...);
		luaL_setmetatable(L, meta);
		return t;
	}
	template <typename T>
	int GcUdata(lua_State* L)
	{
		T* t = static_cast<T*>(lua_touserdata(L, 1));
		if (t) t->~T();
		return 0;
	}
	// Registers a metatable with methods (__index = methods) + optional extras.
	void MakeClass(lua_State* L, const char* meta, const luaL_Reg* methods, const luaL_Reg* metamethods, lua_CFunction gc);

	int OpenJson(lua_State* L);
	int OpenHash(lua_State* L);
	int OpenCompress(lua_State* L);
	int OpenZip(lua_State* L);
	int OpenFs(lua_State* L);
	int OpenHttp(lua_State* L);
	int OpenProcess(lua_State* L);
	int OpenTime(lua_State* L);
	int OpenLog(lua_State* L);
	int OpenMathx(lua_State* L);
	int OpenNoise(lua_State* L);
	int OpenRandom(lua_State* L);
	int OpenGeom(lua_State* L);
	int OpenColor(lua_State* L);
	int OpenNet(lua_State* L);
	int OpenRegex(lua_State* L);
	int OpenUtf8x(lua_State* L);
	int OpenSystem(lua_State* L);
}
