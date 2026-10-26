// Lumen Engine — Lua source helpers for the code editor: tokenizer (highlighting), syntax diagnostics, completion.
#pragma once
#include <string>
#include <vector>

namespace lumen
{
	enum class LuaTok : unsigned char { Text, Keyword, Builtin, String, Number, Comment, Operator, Lumen, Type, Preproc, Function, Constant, Heading, Emphasis };
	struct LuaSpan { int begin, end; LuaTok kind; }; // byte offsets inside one line
	struct LuaDiag { int line = 0, column = 0; std::string message; bool error = true; };

	// Tokenizes every line; multi-line strings/comments are tracked across lines.
	void LuaHighlight(const std::vector<std::string>& lines, std::vector<std::vector<LuaSpan>>& out);
	// Syntax check with the engine's Lua (compile only). Also reports common mistakes (globals assigned in a function, `=` vs `==` in conditions are left to the compiler).
	std::vector<LuaDiag> LuaCheck(const std::string& source, const std::string& chunkName);
	struct LuaCompletion { std::string label, detail, doc; int kind = 0; };
	// Candidates for the identifier prefix at the cursor: keywords, standard library, the `lumen` API and identifiers of the file.
	std::vector<LuaCompletion> LuaComplete(const std::string& source, const std::string& prefix, const std::string& qualifier);
}
