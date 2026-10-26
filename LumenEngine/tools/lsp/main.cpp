// lumen-lsp: Language Server for Lumen Lua projects.
//   lumen-lsp [--stdio] [--tcp PORT] [--api DIR]... [--module DIR]...
#include "lumen/core/Fs.h"
#include "lumen/core/Lsp.h"
#include "lumen/core/Platform.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace lumen;

int main(int argc, char** argv)
{
	std::vector<std::string> api, modules;
	int port = 0;
	for (int i = 1; i < argc; ++i)
	{
		const std::string a = argv[i];
		if (a == "--tcp" && i + 1 < argc) port = std::atoi(argv[++i]);
		else if (a == "--api" && i + 1 < argc) api.push_back(fs::Normalize(argv[++i]));
		else if (a == "--module" && i + 1 < argc) modules.push_back(fs::Normalize(argv[++i]));
		else if (a == "--stdio") port = 0;
		else if (a == "--version")
		{
			std::printf("lumen-lsp %s\n", LUMEN_VERSION_STRING);
			return 0;
		}
	}
	if (api.empty())
	{
		// next to the executable: lumen_runtime/lua/api, then a source checkout
		for (const std::string& c : {fs::Join(platform::ExecutableDir(), "lumen_runtime/lua/api"), fs::Join(platform::ExecutableDir(), "../lumen_runtime/lua/api"),
				 fs::Join(platform::ExecutableDir(), "runtime_data/lua/api")})
			if (fs::IsDirectory(c))
			{
				api.push_back(c);
				modules.push_back(fs::Parent(c));
				break;
			}
	}
	lsp::Server server(api, modules);
	if (port > 0) return lsp::RunTcp(server, port);
	return lsp::RunStdio(server);
}
