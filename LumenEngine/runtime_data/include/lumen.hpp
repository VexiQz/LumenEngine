// Lumen Engine — small C++ conveniences over lumen_c.h for native game modules.
#pragma once
#include "lumen_c.h"
#include <cstdio>
#include <string>
#include <utility>

namespace lumen
{
	inline const LumenHost*& HostRef() { static const LumenHost* h = nullptr; return h; }
	inline void Bind(const LumenHost* host) { HostRef() = host; }

	template <typename... A>
	void Log(int level, const char* fmt, A... args)
	{
		char buf[1024];
		std::snprintf(buf, sizeof(buf), fmt, args...);
		if (HostRef()) HostRef()->log(level, buf);
	}
	template <typename... A> void Info(const char* fmt, A... a) { Log(LUMEN_LOG_INFO, fmt, a...); }
	template <typename... A> void Warn(const char* fmt, A... a) { Log(LUMEN_LOG_WARN, fmt, a...); }
	template <typename... A> void Error(const char* fmt, A... a) { Log(LUMEN_LOG_ERROR, fmt, a...); }

	// Registers `functions` as the Lua module `name` (require "name").
	inline void RegisterModule(lua_State* L, const char* name, const luaL_Reg* functions)
	{
		lua_getglobal(L, "package");
		lua_getfield(L, -1, "preload");
		lua_pushstring(L, name);
		lua_pushlightuserdata(L, (void*)functions);
		lua_pushcclosure(L, [](lua_State* L) -> int {
			const luaL_Reg* fns = (const luaL_Reg*)lua_touserdata(L, lua_upvalueindex(1));
			lua_newtable(L);
			luaL_setfuncs(L, fns, 0);
			return 1;
		}, 1);
		lua_settable(L, -3);
		lua_pop(L, 2);
	}
}
