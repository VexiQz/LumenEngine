// lumen-lua: runs Lua scripts with the Lumen native modules (lumen.json, lumen.fs, lumen.regex, ...) and the
// engine's Lua library (runtime_data/lua) available. Also a REPL and a syntax checker.
//
//   lumen-lua script.lua [args...]     run a script
//   lumen-lua -e "print(1+1)"          run a chunk
//   lumen-lua -c file.lua              syntax check only
//   lumen-lua -i                       interactive prompt
//   lumen-lua --modules                list native modules
//   --lib <dir>                        additional package.path root (may repeat)
//   --sandbox <dir>                    restrict lumen.fs & co to <dir>, disable processes and network
#include "lumen/core/Fs.h"
#include "lumen/core/Log.h"
#include "lumen/core/LuaCompiler.h"
#include "lumen/core/LuaLibs.h"
#include "lumen/core/Platform.h"
#include "lumen/core/Runtime.h"

extern "C"
{
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include <cstdio>
#include <cstring>
#include <iostream>

using namespace lumen;

static int Traceback(lua_State* L)
{
	const char* msg = lua_tostring(L, 1);
	if (msg)
		luaL_traceback(L, L, msg, 1);
	else if (!lua_isnoneornil(L, 1))
	{
		if (!luaL_callmeta(L, 1, "__tostring"))
			lua_pushfstring(L, "(error object is a %s value)", luaL_typename(L, 1));
		luaL_traceback(L, L, lua_tostring(L, -1), 1);
	}
	return 1;
}

static int DoCall(lua_State* L, int nargs)
{
	const int base = lua_gettop(L) - nargs;
	lua_pushcfunction(L, Traceback);
	lua_insert(L, base);
	const int status = lua_pcall(L, nargs, LUA_MULTRET, base);
	lua_remove(L, base);
	if (status != LUA_OK)
	{
		fprintf(stderr, "%s\n", lua_tostring(L, -1));
		lua_pop(L, 1);
	}
	return status;
}

static void AddPackagePath(lua_State* L, const std::string& dir)
{
	lua_getglobal(L, "package");
	lua_getfield(L, -1, "path");
	std::string path = lua_tostring(L, -1);
	lua_pop(L, 1);
	path = dir + "/?.lua;" + dir + "/?/init.lua;" + path;
	lua_pushlstring(L, path.data(), path.size());
	lua_setfield(L, -2, "path");
	lua_pop(L, 1);
}

int main(int argc, char** argv)
{
	std::vector<std::string> libDirs;
	std::string sandbox, expr, check, script;
	bool interactive = false, listModules = false;
	std::vector<std::string> scriptArgs;
	for (int i = 1; i < argc; ++i)
	{
		const std::string a = argv[i];
		if (script.empty() && a == "--lib" && i + 1 < argc) libDirs.push_back(argv[++i]);
		else if (script.empty() && a == "--sandbox" && i + 1 < argc) sandbox = argv[++i];
		else if (script.empty() && a == "-e" && i + 1 < argc) expr = argv[++i];
		else if (script.empty() && a == "-c" && i + 1 < argc) check = argv[++i];
		else if (script.empty() && a == "-i") interactive = true;
		else if (script.empty() && a == "--modules") listModules = true;
		else if (script.empty() && a == "-v") { printf("lumen-lua (%s)\n", lua::VersionString().c_str()); return 0; }
		else if (script.empty() && a[0] != '-') script = a;
		else if (!script.empty()) scriptArgs.push_back(a);
		else { fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
	}
	if (listModules)
	{
		for (const std::string& n : lua::LibNames())
			printf("%s\n", n.c_str());
		return 0;
	}
	if (!check.empty())
	{
		std::string text;
		if (!fs::ReadText(check, text)) { fprintf(stderr, "cannot read %s\n", check.c_str()); return 1; }
		lua::Diagnostic d;
		if (!lua::CheckSyntax(text, check, &d)) { fprintf(stderr, "%s:%d:%d: %s\n", check.c_str(), d.line, d.column, d.message.c_str()); return 1; }
		return 0;
	}

	lua_State* L = luaL_newstate();
	luaL_openlibs(L);
	lua::LibsPolicy policy;
	if (!sandbox.empty())
	{
		policy.allowProcess = false;
		policy.allowNetwork = false;
		policy.fsRoots.push_back(fs::Absolute(sandbox));
	}
	lua::OpenLibs(L, policy);

	// Lua library of the engine: next to the executable (lumen_runtime/lua) or in the source tree
	std::vector<std::string> roots = libDirs;
	if (auto env = platform::GetEnv("LUMEN_LUA_PATH"); env && !env->empty()) roots.push_back(*env);
	roots.push_back(fs::Join(platform::ExecutableDir(), "lumen_runtime/lua"));
	roots.push_back(fs::Join(platform::ExecutableDir(), "../runtime_data/lua"));
	for (auto it = roots.rbegin(); it != roots.rend(); ++it)
		if (fs::IsDirectory(*it)) AddPackagePath(L, fs::Normalize(*it));
	if (!script.empty() && fs::IsFile(script)) AddPackagePath(L, fs::Parent(fs::Absolute(script)));

	lua_newtable(L);
	lua_pushstring(L, script.empty() ? "lumen-lua" : script.c_str());
	lua_rawseti(L, -2, 0);
	for (size_t i = 0; i < scriptArgs.size(); ++i)
	{
		lua_pushstring(L, scriptArgs[i].c_str());
		lua_rawseti(L, -2, (lua_Integer)i + 1);
	}
	lua_setglobal(L, "arg");

	int rc = 0;
	if (!expr.empty())
	{
		if (luaL_loadbuffer(L, expr.data(), expr.size(), "=(command line)") != LUA_OK) { fprintf(stderr, "%s\n", lua_tostring(L, -1)); rc = 1; }
		else rc = DoCall(L, 0) == LUA_OK ? 0 : 1;
	}
	else if (!script.empty())
	{
		if (luaL_loadfile(L, script.c_str()) != LUA_OK) { fprintf(stderr, "%s\n", lua_tostring(L, -1)); rc = 1; }
		else
		{
			for (const std::string& a : scriptArgs) lua_pushstring(L, a.c_str());
			rc = DoCall(L, (int)scriptArgs.size()) == LUA_OK ? 0 : 1;
		}
	}
	else
		interactive = true;

	if (interactive && rc == 0)
	{
		printf("%s - Lumen native modules loaded (require \"lumen.json\", ...)\n", lua::VersionString().c_str());
		std::string line;
		for (;;)
		{
			printf("> ");
			fflush(stdout);
			if (!std::getline(std::cin, line)) break;
			std::string chunk = "return " + line;
			if (luaL_loadbuffer(L, chunk.data(), chunk.size(), "=stdin") != LUA_OK)
			{
				lua_pop(L, 1);
				if (luaL_loadbuffer(L, line.data(), line.size(), "=stdin") != LUA_OK)
				{
					fprintf(stderr, "%s\n", lua_tostring(L, -1));
					lua_pop(L, 1);
					continue;
				}
			}
			const int top = lua_gettop(L) - 1;
			if (DoCall(L, 0) == LUA_OK)
			{
				const int n = lua_gettop(L) - top;
				for (int i = 1; i <= n; ++i)
				{
					size_t len;
					const char* s = luaL_tolstring(L, top + i, &len);
					printf("%s%s", i > 1 ? "\t" : "", s);
					lua_pop(L, 1);
				}
				if (n > 0) printf("\n");
			}
			lua_settop(L, top);
		}
	}
	lua_close(L);
	return rc;
}
