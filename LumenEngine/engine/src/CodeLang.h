// Lumen Engine — languages known to the code editor and their syntax highlighters.
#pragma once
#include "LuaEdit.h"
#include <string>
#include <vector>

namespace lumen
{
	enum class CodeLang : unsigned char { Plain, Lua, Cpp, Hlsl, Json, Markdown, CMake };

	CodeLang LangForPath(const std::string& path);
	const char* LangName(CodeLang l);
	// Line comment prefix used by "toggle comment" ("--", "//", "#", "" when the language has none).
	const char* LineComment(CodeLang l);
	// Tokenizes `lines` for the language (spans are byte offsets, multi-line constructs are tracked across lines).
	void CodeHighlight(CodeLang lang, const std::vector<std::string>& lines, std::vector<std::vector<LuaSpan>>& out);
}
