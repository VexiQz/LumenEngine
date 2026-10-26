// Lumen Engine — in-process Lua language server used by the code editor (no sockets, no child process).
#pragma once

#include "LuaEdit.h"
#include "lumen/core/Lsp.h"

#include <map>
#include <memory>

namespace lumen
{
	class EditorLsp
	{
	public:
		// apiDir: runtime lua/api folder, moduleDirs: require roots (engine lua dir + project root).
		void Init(const std::string& apiDir, const std::vector<std::string>& moduleDirs);
		bool Ready() const { return server_ != nullptr; }

		// Sends the full text when it changed (didOpen first time, didChange afterwards).
		void Sync(const std::string& absPath, const std::string& text);
		void Close(const std::string& absPath);

		std::vector<LuaDiag> Diagnostics(const std::string& absPath) const;
		std::vector<LuaCompletion> Complete(const std::string& absPath, int line, int byteCol);
		std::string Hover(const std::string& absPath, int line, int byteCol); // markdown, empty when nothing
		std::string Signature(const std::string& absPath, int line, int byteCol);
		struct Location { std::string path; int line = 0, column = 0; bool found = false; };
		Location Definition(const std::string& absPath, int line, int byteCol);

		static std::string ToUri(const std::string& absPath);
		static std::string FromUri(const std::string& uri);

	private:
		Json Request(const char* method, Json params);
		void Consume(const std::vector<Json>& out);

		std::unique_ptr<lsp::Server> server_;
		struct Doc { std::string text; int version = 0; };
		std::map<std::string, Doc> docs_;       // by uri
		std::map<std::string, std::vector<LuaDiag>> diags_;
		int nextId_ = 1;
	};
}
