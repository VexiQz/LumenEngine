#include "CodeLang.h"

#include "lumen/core/Fs.h"
#include "lumen/core/Strings.h"

#include <cctype>
#include <cstring>
#include <unordered_set>

namespace lumen
{
	namespace
	{
		using Set = std::unordered_set<std::string>;

		const Set& CppKeywords()
		{
			static const Set s = {"alignas", "alignof", "asm", "auto", "break", "case", "catch", "class", "concept", "const", "consteval", "constexpr", "constinit",
				"const_cast", "continue", "co_await", "co_return", "co_yield", "decltype", "default", "delete", "do", "dynamic_cast", "else", "enum", "explicit",
				"export", "extern", "final", "for", "friend", "goto", "if", "inline", "mutable", "namespace", "new", "noexcept", "operator", "override", "private",
				"protected", "public", "register", "reinterpret_cast", "requires", "return", "sizeof", "static", "static_assert", "static_cast", "struct", "switch",
				"template", "this", "thread_local", "throw", "try", "typedef", "typeid", "typename", "union", "using", "virtual", "volatile", "while", "and", "or", "not",
				"xor", "bitand", "bitor", "compl"};
			return s;
		}
		const Set& CppTypes()
		{
			static const Set s = {"void", "bool", "char", "char8_t", "char16_t", "char32_t", "wchar_t", "short", "int", "long", "float", "double", "signed", "unsigned",
				"size_t", "ssize_t", "ptrdiff_t", "intptr_t", "uintptr_t", "int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t",
				"string", "string_view", "vector", "map", "unordered_map", "set", "unordered_set", "array", "pair", "tuple", "optional", "variant", "unique_ptr",
				"shared_ptr", "weak_ptr", "function", "lua_State", "lua_Number", "lua_Integer", "luaL_Reg", "LumenHost", "XMFLOAT2", "XMFLOAT3", "XMFLOAT4", "XMMATRIX",
				"XMVECTOR", "Entity", "Scene"};
			return s;
		}
		const Set& CppConstants()
		{
			static const Set s = {"true", "false", "nullptr", "NULL", "__FILE__", "__LINE__", "__func__", "__cplusplus", "EOF", "INT_MAX", "INT_MIN", "FLT_MAX", "UINT_MAX"};
			return s;
		}
		const Set& HlslKeywords()
		{
			static const Set s = {"break", "case", "cbuffer", "centroid", "class", "column_major", "const", "continue", "default", "discard", "do", "else", "export", "extern", "for",
				"groupshared", "if", "in", "inline", "inout", "interface", "linear", "namespace", "nointerpolation", "noperspective", "out", "precise", "register", "return",
				"row_major", "sample", "shared", "snorm", "static", "struct", "switch", "tbuffer", "typedef", "uniform", "unorm", "volatile", "while", "technique", "pass", "sampler",
				"SamplerState", "SamplerComparisonState", "ConstantBuffer", "StructuredBuffer", "RWStructuredBuffer", "ByteAddressBuffer", "RWByteAddressBuffer", "numthreads",
				"template", "using", "enum", "namespace"};
			return s;
		}
		const Set& HlslTypes()
		{
			static const Set s = {"void", "bool", "int", "uint", "dword", "half", "float", "double", "min16float", "min16int", "min16uint", "float2", "float3", "float4", "int2",
				"int3", "int4", "uint2", "uint3", "uint4", "bool2", "bool3", "bool4", "half2", "half3", "half4", "float2x2", "float3x3", "float4x4", "float3x4", "float4x3",
				"matrix", "vector", "Texture1D", "Texture2D", "Texture3D", "TextureCube", "Texture2DArray", "TextureCubeArray", "Texture2DMS", "RWTexture1D", "RWTexture2D",
				"RWTexture3D", "RWTexture2DArray", "Buffer", "RWBuffer", "AppendStructuredBuffer", "ConsumeStructuredBuffer", "RaytracingAccelerationStructure"};
			return s;
		}
		const Set& HlslBuiltins()
		{
			static const Set s = {"abs", "acos", "all", "any", "asin", "atan", "atan2", "ceil", "clamp", "clip", "cos", "cosh", "cross", "ddx", "ddy", "degrees", "determinant",
				"distance", "dot", "exp", "exp2", "faceforward", "floor", "fmod", "frac", "frexp", "fwidth", "isnan", "isinf", "ldexp", "length", "lerp", "log", "log2", "log10",
				"mad", "max", "min", "modf", "mul", "normalize", "pow", "radians", "rcp", "reflect", "refract", "round", "rsqrt", "saturate", "sign", "sin", "sincos", "sinh",
				"smoothstep", "sqrt", "step", "tan", "tanh", "transpose", "trunc", "countbits", "firstbithigh", "firstbitlow", "reversebits", "asfloat", "asint", "asuint",
				"f16tof32", "f32tof16", "InterlockedAdd", "InterlockedMin", "InterlockedMax", "InterlockedOr", "InterlockedAnd", "InterlockedExchange", "GroupMemoryBarrierWithGroupSync",
				"DeviceMemoryBarrier", "AllMemoryBarrier", "WaveActiveSum", "WaveGetLaneIndex", "Sample", "SampleLevel", "SampleGrad", "SampleCmp", "Load", "Store", "GetDimensions",
				"mul", "lit", "dst", "noise", "tex2D", "SV_Position", "SV_Target", "SV_Target0", "SV_Target1", "SV_Depth", "SV_DispatchThreadID", "SV_GroupID", "SV_GroupThreadID",
				"SV_GroupIndex", "SV_VertexID", "SV_InstanceID", "SV_IsFrontFace", "SV_PrimitiveID", "POSITION", "NORMAL", "TEXCOORD", "COLOR", "TANGENT"};
			return s;
		}
		const Set& CMakeKeywords()
		{
			static const Set s = {"if", "else", "elseif", "endif", "foreach", "endforeach", "while", "endwhile", "function", "endfunction", "macro", "endmacro", "return", "break",
				"continue", "block", "endblock", "AND", "OR", "NOT", "STREQUAL", "MATCHES", "EXISTS", "DEFINED", "PUBLIC", "PRIVATE", "INTERFACE", "STATIC", "SHARED", "MODULE",
				"REQUIRED", "ON", "OFF", "TRUE", "FALSE"};
			return s;
		}

		bool IdentStart(char c) { return std::isalpha((unsigned char)c) || c == '_' || (unsigned char)c >= 0x80; }
		bool IdentChar(char c) { return std::isalnum((unsigned char)c) || c == '_' || (unsigned char)c >= 0x80; }

		bool AllCaps(const std::string& w)
		{
			if (w.size() < 2) return false;
			bool letter = false;
			for (char c : w)
			{
				if (std::islower((unsigned char)c)) return false;
				if (std::isupper((unsigned char)c)) letter = true;
			}
			return letter;
		}

		// ------------------------------------------------------------------------------------------------
		// C / C++ / HLSL
		// ------------------------------------------------------------------------------------------------
		void HighlightC(bool hlsl, const std::vector<std::string>& lines, std::vector<std::vector<LuaSpan>>& out)
		{
			const Set& kw = hlsl ? HlslKeywords() : CppKeywords();
			const Set& types = hlsl ? HlslTypes() : CppTypes();
			bool inBlockComment = false, inRaw = false;
			std::string rawEnd; // `)delim"`
			out.assign(lines.size(), {});
			for (size_t li = 0; li < lines.size(); ++li)
			{
				const std::string& s = lines[li];
				auto& spans = out[li];
				const int n = (int)s.size();
				int i = 0;
				if (inBlockComment)
				{
					const size_t e = s.find("*/");
					if (e == std::string::npos) { spans.push_back({0, n, LuaTok::Comment}); continue; }
					spans.push_back({0, (int)e + 2, LuaTok::Comment});
					i = (int)e + 2;
					inBlockComment = false;
				}
				else if (inRaw)
				{
					const size_t e = s.find(rawEnd);
					if (e == std::string::npos) { spans.push_back({0, n, LuaTok::String}); continue; }
					spans.push_back({0, (int)(e + rawEnd.size()), LuaTok::String});
					i = (int)(e + rawEnd.size());
					inRaw = false;
				}
				// preprocessor directive at the start of the line
				{
					int k = i;
					while (k < n && (s[k] == ' ' || s[k] == '\t')) ++k;
					if (k < n && s[k] == '#' && i == 0)
					{
						int e = k + 1;
						while (e < n && (s[e] == ' ' || s[e] == '\t')) ++e;
						while (e < n && IdentChar(s[e])) ++e;
						spans.push_back({k, e, LuaTok::Preproc});
						i = e;
						// `#include <header>` / "header"
						const std::string directive = s.substr(k, e - k);
						if (directive.find("include") != std::string::npos || directive.find("import") != std::string::npos)
						{
							int a = e;
							while (a < n && (s[a] == ' ' || s[a] == '\t')) ++a;
							if (a < n && (s[a] == '<' || s[a] == '"'))
							{
								const char close = s[a] == '<' ? '>' : '"';
								int b = a + 1;
								while (b < n && s[b] != close) ++b;
								if (b < n) ++b;
								spans.push_back({a, b, LuaTok::String});
								i = b;
							}
						}
					}
				}
				while (i < n)
				{
					const char c = s[i];
					if (c == ' ' || c == '\t') { ++i; continue; }
					if (c == '/' && i + 1 < n && s[i + 1] == '/') { spans.push_back({i, n, LuaTok::Comment}); i = n; break; }
					if (c == '/' && i + 1 < n && s[i + 1] == '*')
					{
						const size_t e = s.find("*/", (size_t)i + 2);
						if (e == std::string::npos) { spans.push_back({i, n, LuaTok::Comment}); inBlockComment = true; i = n; break; }
						spans.push_back({i, (int)e + 2, LuaTok::Comment});
						i = (int)e + 2;
						continue;
					}
					// raw string literal R"delim( ... )delim"
					if ((c == 'R' || ((c == 'u' || c == 'U' || c == 'L') && i + 1 < n && s[i + 1] == 'R')) && !hlsl)
					{
						int q = i + (c == 'R' ? 1 : 2);
						if (q < n && s[q] == '"')
						{
							int d = q + 1;
							while (d < n && s[d] != '(' && d - q < 18) ++d;
							if (d < n && s[d] == '(')
							{
								rawEnd = ")" + s.substr((size_t)q + 1, (size_t)(d - q - 1)) + "\"";
								const size_t e = s.find(rawEnd, (size_t)d + 1);
								if (e == std::string::npos) { spans.push_back({i, n, LuaTok::String}); inRaw = true; i = n; break; }
								spans.push_back({i, (int)(e + rawEnd.size()), LuaTok::String});
								i = (int)(e + rawEnd.size());
								continue;
							}
						}
					}
					if (c == '"' || c == '\'')
					{
						int j = i + 1;
						while (j < n && s[j] != c)
						{
							if (s[j] == '\\' && j + 1 < n) ++j;
							++j;
						}
						if (j < n) ++j;
						spans.push_back({i, j, LuaTok::String});
						i = j;
						continue;
					}
					if (std::isdigit((unsigned char)c) || (c == '.' && i + 1 < n && std::isdigit((unsigned char)s[i + 1])))
					{
						int j = i + 1;
						const bool hex = c == '0' && j < n && (s[j] == 'x' || s[j] == 'X');
						while (j < n && (std::isalnum((unsigned char)s[j]) || s[j] == '.' || s[j] == '\'' ||
										   ((s[j] == '+' || s[j] == '-') && !hex && (s[j - 1] == 'e' || s[j - 1] == 'E'))))
							++j;
						spans.push_back({i, j, LuaTok::Number});
						i = j;
						continue;
					}
					if (IdentStart(c))
					{
						int j = i + 1;
						while (j < n && IdentChar(s[j])) ++j;
						const std::string w = s.substr((size_t)i, (size_t)(j - i));
						int look = j;
						while (look < n && (s[look] == ' ' || s[look] == '\t')) ++look;
						const bool call = look < n && s[look] == '(';
						LuaTok k = LuaTok::Text;
						if (kw.count(w)) k = LuaTok::Keyword;
						else if (types.count(w)) k = LuaTok::Type;
						else if (!hlsl && CppConstants().count(w)) k = LuaTok::Constant;
						else if (hlsl && (w == "true" || w == "false")) k = LuaTok::Constant;
						else if (hlsl && HlslBuiltins().count(w)) k = LuaTok::Builtin;
						else if (hlsl && w.size() > 3 && w.compare(0, 3, "SV_") == 0) k = LuaTok::Builtin;
						else if (AllCaps(w)) k = LuaTok::Constant;
						else if (call) k = LuaTok::Function;
						else if (std::isupper((unsigned char)w[0]) && look < n && (s[look] == '*' || s[look] == '&' || IdentStart(s[look])) && i > 0) k = LuaTok::Type;
						else if (i >= 2 && s[i - 1] == ':' && s[i - 2] == ':' && !call && std::isupper((unsigned char)w[0])) k = LuaTok::Type;
						if (k != LuaTok::Text) spans.push_back({i, j, k});
						i = j;
						continue;
					}
					// operators
					if (std::strchr("+-*/%=<>!&|^~?:;,.(){}[]", c))
					{
						int j = i + 1;
						spans.push_back({i, j, LuaTok::Operator});
						i = j;
						continue;
					}
					++i;
				}
			}
		}

		// ------------------------------------------------------------------------------------------------
		// JSON
		// ------------------------------------------------------------------------------------------------
		void HighlightJson(const std::vector<std::string>& lines, std::vector<std::vector<LuaSpan>>& out)
		{
			out.assign(lines.size(), {});
			for (size_t li = 0; li < lines.size(); ++li)
			{
				const std::string& s = lines[li];
				auto& spans = out[li];
				const int n = (int)s.size();
				int i = 0;
				while (i < n)
				{
					const char c = s[i];
					if (c == '/' && i + 1 < n && s[i + 1] == '/') { spans.push_back({i, n, LuaTok::Comment}); break; }
					if (c == '"')
					{
						int j = i + 1;
						while (j < n && s[j] != '"')
						{
							if (s[j] == '\\' && j + 1 < n) ++j;
							++j;
						}
						if (j < n) ++j;
						int k = j;
						while (k < n && (s[k] == ' ' || s[k] == '\t')) ++k;
						spans.push_back({i, j, k < n && s[k] == ':' ? LuaTok::Lumen : LuaTok::String}); // keys in the accent colour
						i = j;
						continue;
					}
					if (std::isdigit((unsigned char)c) || (c == '-' && i + 1 < n && std::isdigit((unsigned char)s[i + 1])))
					{
						int j = i + 1;
						while (j < n && (std::isdigit((unsigned char)s[j]) || s[j] == '.' || s[j] == 'e' || s[j] == 'E' || s[j] == '+' || s[j] == '-')) ++j;
						spans.push_back({i, j, LuaTok::Number});
						i = j;
						continue;
					}
					if (IdentStart(c))
					{
						int j = i + 1;
						while (j < n && IdentChar(s[j])) ++j;
						const std::string w = s.substr((size_t)i, (size_t)(j - i));
						if (w == "true" || w == "false" || w == "null") spans.push_back({i, j, LuaTok::Constant});
						i = j;
						continue;
					}
					if (std::strchr("{}[]:,", c)) spans.push_back({i, i + 1, LuaTok::Operator});
					++i;
				}
			}
		}

		// ------------------------------------------------------------------------------------------------
		// Markdown
		// ------------------------------------------------------------------------------------------------
		void HighlightMarkdown(const std::vector<std::string>& lines, std::vector<std::vector<LuaSpan>>& out)
		{
			out.assign(lines.size(), {});
			bool fence = false;
			for (size_t li = 0; li < lines.size(); ++li)
			{
				const std::string& s = lines[li];
				auto& spans = out[li];
				const int n = (int)s.size();
				int first = 0;
				while (first < n && (s[first] == ' ' || s[first] == '\t')) ++first;
				if (s.compare((size_t)first, 3, "```") == 0 || s.compare((size_t)first, 3, "~~~") == 0)
				{
					spans.push_back({first, n, LuaTok::Preproc});
					fence = !fence;
					continue;
				}
				if (fence) { spans.push_back({0, n, LuaTok::String}); continue; }
				if (first < n && s[first] == '#') { spans.push_back({first, n, LuaTok::Heading}); continue; }
				if (first < n && s[first] == '>') { spans.push_back({first, n, LuaTok::Comment}); continue; }
				int i = 0;
				if (first + 1 < n && (s[first] == '-' || s[first] == '*' || s[first] == '+') && s[first + 1] == ' ')
				{
					spans.push_back({first, first + 1, LuaTok::Keyword});
					i = first + 2;
				}
				else if (std::isdigit((unsigned char)(first < n ? s[first] : ' ')))
				{
					int j = first;
					while (j < n && std::isdigit((unsigned char)s[j])) ++j;
					if (j < n && s[j] == '.') { spans.push_back({first, j + 1, LuaTok::Keyword}); i = j + 1; }
				}
				while (i < n)
				{
					const char c = s[i];
					if (c == '`')
					{
						const size_t e = s.find('`', (size_t)i + 1);
						if (e != std::string::npos) { spans.push_back({i, (int)e + 1, LuaTok::String}); i = (int)e + 1; continue; }
					}
					else if (c == '*' || c == '_')
					{
						const std::string mark = (i + 1 < n && s[i + 1] == c) ? std::string(2, c) : std::string(1, c);
						const size_t e = s.find(mark, (size_t)i + mark.size());
						if (e != std::string::npos && e > (size_t)i + mark.size())
						{
							spans.push_back({i, (int)(e + mark.size()), LuaTok::Emphasis});
							i = (int)(e + mark.size());
							continue;
						}
					}
					else if (c == '[')
					{
						const size_t e = s.find(']', (size_t)i);
						if (e != std::string::npos && e + 1 < (size_t)n && s[e + 1] == '(')
						{
							const size_t f = s.find(')', e);
							if (f != std::string::npos) { spans.push_back({i, (int)f + 1, LuaTok::Builtin}); i = (int)f + 1; continue; }
						}
					}
					++i;
				}
			}
		}

		// ------------------------------------------------------------------------------------------------
		// CMake
		// ------------------------------------------------------------------------------------------------
		void HighlightCMake(const std::vector<std::string>& lines, std::vector<std::vector<LuaSpan>>& out)
		{
			out.assign(lines.size(), {});
			for (size_t li = 0; li < lines.size(); ++li)
			{
				const std::string& s = lines[li];
				auto& spans = out[li];
				const int n = (int)s.size();
				int i = 0;
				while (i < n)
				{
					const char c = s[i];
					if (c == '#') { spans.push_back({i, n, LuaTok::Comment}); break; }
					if (c == '"')
					{
						int j = i + 1;
						while (j < n && s[j] != '"')
						{
							if (s[j] == '\\' && j + 1 < n) ++j;
							++j;
						}
						if (j < n) ++j;
						spans.push_back({i, j, LuaTok::String});
						i = j;
						continue;
					}
					if (c == '$' && i + 1 < n && s[i + 1] == '{')
					{
						const size_t e = s.find('}', (size_t)i);
						const int j = e == std::string::npos ? n : (int)e + 1;
						spans.push_back({i, j, LuaTok::Type});
						i = j;
						continue;
					}
					if (IdentStart(c))
					{
						int j = i + 1;
						while (j < n && (IdentChar(s[j]) || s[j] == '.' || s[j] == '-')) ++j;
						const std::string w = s.substr((size_t)i, (size_t)(j - i));
						int look = j;
						while (look < n && s[look] == ' ') ++look;
						if (CMakeKeywords().count(w)) spans.push_back({i, j, LuaTok::Keyword});
						else if (look < n && s[look] == '(') spans.push_back({i, j, LuaTok::Function});
						else if (AllCaps(w)) spans.push_back({i, j, LuaTok::Constant});
						i = j;
						continue;
					}
					if (std::isdigit((unsigned char)c))
					{
						int j = i + 1;
						while (j < n && (std::isdigit((unsigned char)s[j]) || s[j] == '.')) ++j;
						spans.push_back({i, j, LuaTok::Number});
						i = j;
						continue;
					}
					if (std::strchr("()", c)) spans.push_back({i, i + 1, LuaTok::Operator});
					++i;
				}
			}
		}
	}

	CodeLang LangForPath(const std::string& path)
	{
		const std::string ext = str::ToLower(fs::Extension(path));
		const std::string name = str::ToLower(fs::FileName(path));
		if (ext == ".lua") return CodeLang::Lua;
		if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c" || ext == ".h" || ext == ".hpp" || ext == ".hh" || ext == ".hxx" || ext == ".inl" || ext == ".ipp" || ext == ".mm")
			return CodeLang::Cpp;
		if (ext == ".hlsl" || ext == ".hlsli" || ext == ".fx" || ext == ".glsl" || ext == ".vert" || ext == ".frag" || ext == ".comp" || ext == ".cginc") return CodeLang::Hlsl;
		if (ext == ".json" || ext == ".lum" || ext == ".scene" || ext == ".mat" || ext == ".luarc") return CodeLang::Json;
		if (ext == ".md" || ext == ".markdown") return CodeLang::Markdown;
		if (ext == ".cmake" || name == "cmakelists.txt") return CodeLang::CMake;
		return CodeLang::Plain;
	}

	const char* LangName(CodeLang l)
	{
		switch (l)
		{
		case CodeLang::Lua: return "Lua";
		case CodeLang::Cpp: return "C++";
		case CodeLang::Hlsl: return "HLSL";
		case CodeLang::Json: return "JSON";
		case CodeLang::Markdown: return "Markdown";
		case CodeLang::CMake: return "CMake";
		default: return "Text";
		}
	}

	const char* LineComment(CodeLang l)
	{
		switch (l)
		{
		case CodeLang::Lua: return "--";
		case CodeLang::Cpp:
		case CodeLang::Hlsl: return "//";
		case CodeLang::CMake: return "#";
		default: return "";
		}
	}

	void CodeHighlight(CodeLang lang, const std::vector<std::string>& lines, std::vector<std::vector<LuaSpan>>& out)
	{
		switch (lang)
		{
		case CodeLang::Lua: LuaHighlight(lines, out); return;
		case CodeLang::Cpp: HighlightC(false, lines, out); return;
		case CodeLang::Hlsl: HighlightC(true, lines, out); return;
		case CodeLang::Json: HighlightJson(lines, out); return;
		case CodeLang::Markdown: HighlightMarkdown(lines, out); return;
		case CodeLang::CMake: HighlightCMake(lines, out); return;
		default:
			out.assign(lines.size(), {});
			for (size_t i = 0; i < lines.size(); ++i) out[i] = {{0, (int)lines[i].size(), LuaTok::Text}};
			return;
		}
	}
}
