#include "lumen/core/LuaLibs.h"

#include "LuaUtil.h"
#include "lumen/core/Fs.h"

#include <algorithm>

namespace lumen::lua
{
	namespace detail
	{
		static const char* kPolicyKey = "lumen.policy";

		struct PolicyBox
		{
			LibsPolicy p;
		};

		void SetPolicy(lua_State* L, const LibsPolicy& p)
		{
			PolicyBox* b = static_cast<PolicyBox*>(lua_newuserdatauv(L, sizeof(PolicyBox), 0));
			new (b) PolicyBox{p};
			lua_newtable(L);
			lua_pushcfunction(L, GcUdata<PolicyBox>);
			lua_setfield(L, -2, "__gc");
			lua_setmetatable(L, -2);
			lua_setfield(L, LUA_REGISTRYINDEX, kPolicyKey);
		}

		const LibsPolicy& Policy(lua_State* L)
		{
			static const LibsPolicy defaults;
			lua_getfield(L, LUA_REGISTRYINDEX, kPolicyKey);
			PolicyBox* b = static_cast<PolicyBox*>(lua_touserdata(L, -1));
			lua_pop(L, 1);
			return b ? b->p : defaults;
		}

		bool PathAllowed(lua_State* L, const std::string& path)
		{
			const LibsPolicy& p = Policy(L);
			if (!p.allowFs)
				return false;
			if (p.fsRoots.empty())
				return true;
			const std::string abs = fs::Canonical(fs::Absolute(path));
			for (const std::string& r : p.fsRoots)
				if (fs::PathEquals(abs, fs::Canonical(r)) || fs::IsSubPath(abs, fs::Canonical(r)))
					return true;
			return false;
		}

		void MakeClass(lua_State* L, const char* meta, const luaL_Reg* methods, const luaL_Reg* metamethods, lua_CFunction gc)
		{
			luaL_newmetatable(L, meta);
			if (metamethods)
				luaL_setfuncs(L, metamethods, 0);
			if (gc)
			{
				lua_pushcfunction(L, gc);
				lua_setfield(L, -2, "__gc");
			}
			lua_newtable(L);
			if (methods)
				luaL_setfuncs(L, methods, 0);
			lua_setfield(L, -2, "__index");
			lua_pushstring(L, meta);
			lua_setfield(L, -2, "__name");
			lua_pop(L, 1);
		}
	} // namespace detail

	namespace
	{
		struct ModuleDef
		{
			const char* name;
			lua_CFunction open;
		};
		const ModuleDef kModules[] = {
			{"lumen.json", detail::OpenJson}, {"lumen.hash", detail::OpenHash}, {"lumen.compress", detail::OpenCompress},
			{"lumen.zip", detail::OpenZip}, {"lumen.fs", detail::OpenFs}, {"lumen.http", detail::OpenHttp},
			{"lumen.process", detail::OpenProcess}, {"lumen.time", detail::OpenTime}, {"lumen.log", detail::OpenLog},
			{"lumen.mathx", detail::OpenMathx}, {"lumen.noise", detail::OpenNoise}, {"lumen.random", detail::OpenRandom},
			{"lumen.geom", detail::OpenGeom}, {"lumen.color", detail::OpenColor}, {"lumen.net", detail::OpenNet},
			{"lumen.regex", detail::OpenRegex}, {"lumen.utf8x", detail::OpenUtf8x}, {"lumen.system", detail::OpenSystem},
		};
	} // namespace

	const std::vector<std::string>& LibNames()
	{
		static const std::vector<std::string> names = [] {
			std::vector<std::string> n;
			for (const ModuleDef& m : kModules)
				n.push_back(m.name);
			return n;
		}();
		return names;
	}

	void OpenLibs(lua_State* L, const LibsPolicy& policy)
	{
		detail::SetPolicy(L, policy);
		luaL_getsubtable(L, LUA_REGISTRYINDEX, LUA_PRELOAD_TABLE);
		for (const ModuleDef& m : kModules)
		{
			lua_pushcfunction(L, m.open);
			lua_setfield(L, -2, m.name);
		}
		lua_pop(L, 1);
	}
} // namespace lumen::lua
