#pragma once
#include "LuaUtil.h"
#include <cmath>

namespace lumen::lua::detail
{
	struct Vec
	{
		int n;
		double v[4];
	};
	const char* VecMeta(int n);
	// Accepts vecN userdata (n >= minN, missing components are 0) or a table {x,y,z,w}/{1,2,3,4}. Returns false if not convertible.
	bool ToVec(lua_State* L, int idx, int minN, double out[4], int* nOut = nullptr);
	void PushVec(lua_State* L, int n, double x, double y = 0, double z = 0, double w = 0);
	inline void PushVec3(lua_State* L, double x, double y, double z) { PushVec(L, 3, x, y, z); }
	inline void PushVec2(lua_State* L, double x, double y) { PushVec(L, 2, x, y); }
	// like ToVec but raises a Lua argument error
	void CheckVec(lua_State* L, int idx, int minN, double out[4], int* nOut = nullptr);
}
