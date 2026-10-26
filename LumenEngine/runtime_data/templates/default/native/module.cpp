// ${PROJECT_NAME}: native game module. Built by the engine (Build panel) into a shared library and loaded when the
// game starts. Everything registered here is available to Lua: local fast = require("fastmath").
#include "lumen.hpp"

#include <cmath>

namespace
{
	// fastmath.smoothstep(edge0, edge1, x)
	int SmoothStep(lua_State* L)
	{
		const double e0 = luaL_checknumber(L, 1), e1 = luaL_checknumber(L, 2), x = luaL_checknumber(L, 3);
		double t = (x - e0) / (e1 - e0);
		t = t < 0 ? 0 : (t > 1 ? 1 : t);
		lua_pushnumber(L, t * t * (3 - 2 * t));
		return 1;
	}

	// fastmath.sum_sines(count, frequency): a CPU heavy loop that is much faster in native code.
	int SumSines(lua_State* L)
	{
		const lua_Integer n = luaL_checkinteger(L, 1);
		const double f = luaL_checknumber(L, 2);
		double acc = 0;
		for (lua_Integer i = 0; i < n; ++i)
			acc += std::sin(f * (double)i);
		lua_pushnumber(L, acc);
		return 1;
	}

	const luaL_Reg kFunctions[] = {{"smoothstep", SmoothStep}, {"sum_sines", SumSines}, {nullptr, nullptr}};
}

extern "C"
{
	LUMEN_EXPORT int lumen_module_open(lua_State* L, const LumenHost* host)
	{
		lumen::Bind(host);
		lumen::RegisterModule(L, "fastmath", kFunctions);
		lumen::Info("native module '${MODULE_NAME}' loaded (API v%u)", host->version);
		return 0;
	}

	LUMEN_EXPORT void lumen_module_close(lua_State*)
	{
		lumen::Info("native module '${MODULE_NAME}' unloaded");
	}
}
