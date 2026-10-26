// Lumen Engine — Language Server Protocol server for Lua (diagnostics, completion, hover, definition,
// references, rename, signature help, document/workspace symbols), aware of the Lumen API stubs.
#pragma once

#include "lumen/core/Json.h"
#include "lumen/core/LuaAnalysis.h"

#include <atomic>
#include <cstdio>

namespace lumen::lsp
{
	class Server
	{
	public:
		// `apiDirs`: directories with annotated stub files (runtime_data/lua/api). `moduleDirs`: extra require roots.
		Server(std::vector<std::string> apiDirs = {}, std::vector<std::string> moduleDirs = {});

		// Handles one decoded JSON-RPC message and returns the messages to send back (responses + notifications).
		std::vector<Json> Handle(const Json& message);
		bool ShouldExit() const { return exit_; }
		int ExitCode() const { return shutdown_ ? 0 : 1; }
		lua::Workspace& GetWorkspace() { return ws_; }

	private:
		Json Initialize(const Json& params);
		Json Completion(const Json& params);
		Json Hover(const Json& params);
		Json Definition(const Json& params);
		Json References(const Json& params);
		Json PrepareRename(const Json& params);
		Json Rename(const Json& params);
		Json SignatureHelp(const Json& params);
		Json DocumentSymbols(const Json& params);
		Json WorkspaceSymbols(const Json& params);
		Json DocumentHighlight(const Json& params);
		Json PublishDiagnostics(const std::string& uri);

		std::vector<std::string> apiDirs_, moduleDirs_;
		lua::Workspace ws_;
		bool initialized_ = false, shutdown_ = false, exit_ = false;
	};

	// Content-Length framed transport over C streams.
	bool ReadMessage(FILE* in, std::string& body);
	void WriteMessage(FILE* out, const Json& message);
	// Runs the server on stdin/stdout until exit. Returns the process exit code.
	int RunStdio(Server& server);
	// Listens on a TCP port (loopback) and serves one client at a time on the calling thread. `stop` ends the loop.
	int RunTcp(Server& server, int port, const std::atomic<bool>* stop = nullptr);

} // namespace lumen::lsp
