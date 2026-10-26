// Lumen Engine — native Lua modules (`require "lumen.json"`, "lumen.fs", ...).
//
// They are registered in package.preload, so nothing is loaded until a script asks for it. A LibsPolicy limits
// what scripts may touch (file system roots, processes, network) for sandboxed/exported games.
#pragma once

#include "lumen/core/Common.h"

struct lua_State;

namespace lumen::lua
{
	struct LibsPolicy
	{
		bool allowProcess = true;
		bool allowNetwork = true;
		bool allowFs = true;
		std::vector<std::string> fsRoots; // when not empty, lumen.fs / lumen.zip / lumen.storage only touch these trees
	};

	// Registers every native module in package.preload. Safe to call once per state.
	void OpenLibs(lua_State* L, const LibsPolicy& policy = {});
	// Names of the registered modules ("lumen.json", ...), in stable order.
	const std::vector<std::string>& LibNames();
}
