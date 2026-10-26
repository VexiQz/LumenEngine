#include "lumen/core/LuaAnalysis.h"

#include "lumen/core/Strings.h"

#include <algorithm>
#include <climits>

namespace lumen::lua
{
	// =============================================================================================
	// Tokenizer
	// =============================================================================================
	namespace
	{
		const char* const kKeywords[] = {"and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if", "in",
			"local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while"};

		bool IsNameStart(unsigned char c) { return std::isalpha(c) || c == '_' || c >= 0x80; }
		bool IsNameChar(unsigned char c) { return std::isalnum(c) || c == '_' || c >= 0x80; }

		// Returns the level of a long bracket opening at s[i] ('[' '='* '['), or -1.
		int LongBracketLevel(std::string_view s, size_t i)
		{
			if (i >= s.size() || s[i] != '[') return -1;
			size_t j = i + 1;
			int level = 0;
			while (j < s.size() && s[j] == '=')
			{
				++level;
				++j;
			}
			return (j < s.size() && s[j] == '[') ? level : -1;
		}

		// Position just after the closing bracket of the given level, or s.size() when unterminated.
		size_t FindLongClose(std::string_view s, size_t from, int level)
		{
			std::string close = "]" + std::string((size_t)level, '=') + "]";
			size_t p = s.find(close, from);
			return p == std::string_view::npos ? s.size() : p + close.size();
		}
	}

	bool IsKeyword(std::string_view s)
	{
		for (const char* k : kKeywords)
			if (s == k) return true;
		return false;
	}

	std::vector<Token> Tokenize(std::string_view src)
	{
		std::vector<Token> out;
		size_t i = 0;
		int line = 0;
		size_t lineStart = 0;
		const size_t n = src.size();

		auto advanceTo = [&](size_t to) {
			for (size_t k = i; k < to && k < n; ++k)
				if (src[k] == '\n')
				{
					++line;
					lineStart = k + 1;
				}
			i = std::min(to, n);
		};
		auto push = [&](Tok kind, size_t begin, size_t end, int sl, int sc) {
			Token t;
			t.kind = kind;
			t.begin = (uint32_t)begin;
			t.end = (uint32_t)end;
			t.line = sl;
			t.col = sc;
			t.endLine = line;
			t.endCol = (int)(end - lineStart);
			out.push_back(t);
		};

		while (i < n)
		{
			const unsigned char c = (unsigned char)src[i];
			if (c == '\n')
			{
				++line;
				++i;
				lineStart = i;
				continue;
			}
			if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v')
			{
				++i;
				continue;
			}
			const size_t begin = i;
			const int sl = line, sc = (int)(i - lineStart);
			if (c == '-' && i + 1 < n && src[i + 1] == '-')
			{
				size_t end;
				const int level = LongBracketLevel(src, i + 2);
				bool isDoc = false;
				if (level >= 0)
					end = FindLongClose(src, i + 4 + (size_t)level, level);
				else
				{
					end = src.find('\n', i);
					if (end == std::string_view::npos) end = n;
					isDoc = i + 2 < n && src[i + 2] == '-';
				}
				advanceTo(end);
				push(Tok::Comment, begin, end, sl, sc);
				out.back().doc = isDoc;
				continue;
			}
			if (IsNameStart(c))
			{
				size_t j = i + 1;
				while (j < n && IsNameChar((unsigned char)src[j])) ++j;
				const bool kw = IsKeyword(src.substr(i, j - i));
				i = j;
				push(kw ? Tok::Keyword : Tok::Name, begin, j, sl, sc);
				continue;
			}
			if (std::isdigit(c) || (c == '.' && i + 1 < n && std::isdigit((unsigned char)src[i + 1])))
			{
				size_t j = i;
				if (c == '0' && j + 1 < n && (src[j + 1] == 'x' || src[j + 1] == 'X'))
				{
					j += 2;
					while (j < n && (std::isxdigit((unsigned char)src[j]) || src[j] == '.' || ((src[j] == 'p' || src[j] == 'P'))
											 || ((src[j] == '+' || src[j] == '-') && (src[j - 1] == 'p' || src[j - 1] == 'P'))))
						++j;
				}
				else
				{
					while (j < n && (std::isdigit((unsigned char)src[j]) || src[j] == '.'
											 || ((src[j] == 'e' || src[j] == 'E'))
											 || ((src[j] == '+' || src[j] == '-') && (src[j - 1] == 'e' || src[j - 1] == 'E'))))
						++j;
				}
				i = j;
				push(Tok::Number, begin, j, sl, sc);
				continue;
			}
			if (c == '"' || c == '\'')
			{
				size_t j = i + 1;
				while (j < n && src[j] != (char)c && src[j] != '\n')
				{
					if (src[j] == '\\' && j + 1 < n) ++j;
					++j;
				}
				if (j < n && src[j] == (char)c) ++j;
				advanceTo(j);
				push(Tok::String, begin, j, sl, sc);
				continue;
			}
			if (c == '[')
			{
				const int level = LongBracketLevel(src, i);
				if (level >= 0)
				{
					const size_t end = FindLongClose(src, i + 2 + (size_t)level, level);
					advanceTo(end);
					push(Tok::String, begin, end, sl, sc);
					continue;
				}
			}
			// operators
			size_t len = 1;
			if (i + 2 < n && src.substr(i, 3) == "...") len = 3;
			else if (i + 1 < n)
			{
				const std::string_view two = src.substr(i, 2);
				if (two == ".." || two == "==" || two == "~=" || two == "<=" || two == ">=" || two == "//" || two == "::" || two == "<<" || two == ">>") len = 2;
			}
			i += len;
			push(Tok::Op, begin, begin + len, sl, sc);
		}
		Token eof;
		eof.kind = Tok::Eof;
		eof.begin = eof.end = (uint32_t)n;
		eof.line = eof.endLine = line;
		eof.col = eof.endCol = (int)(n - lineStart);
		out.push_back(eof);
		return out;
	}

	// =============================================================================================
	// Positions
	// =============================================================================================
	int ByteColToUtf16(std::string_view line, int byteCol)
	{
		int units = 0;
		for (size_t i = 0; i < line.size() && (int)i < byteCol;)
		{
			const unsigned char c = (unsigned char)line[i];
			size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
			units += len == 4 ? 2 : 1;
			i += len;
		}
		return units;
	}

	int Utf16ToByteCol(std::string_view line, int utf16Col)
	{
		int units = 0;
		size_t i = 0;
		while (i < line.size() && units < utf16Col)
		{
			const unsigned char c = (unsigned char)line[i];
			size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
			units += len == 4 ? 2 : 1;
			i += len;
		}
		return (int)i;
	}

	std::string_view DocInfo::LineText(int line) const
	{
		if (line < 0 || line >= (int)lineStarts.size()) return {};
		const size_t b = lineStarts[(size_t)line];
		size_t e = line + 1 < (int)lineStarts.size() ? lineStarts[(size_t)line + 1] : text.size();
		std::string_view v = std::string_view(text).substr(b, e - b);
		while (!v.empty() && (v.back() == '\n' || v.back() == '\r')) v.remove_suffix(1);
		return v;
	}

	int DocInfo::TokenAt(int line, int col, bool inclusiveEnd) const
	{
		if (line < 0 || line >= (int)lineStarts.size()) return -1;
		const uint32_t off = lineStarts[(size_t)line] + (uint32_t)col;
		// last token with begin <= off
		int lo = 0, hi = (int)tokens.size() - 1, best = -1;
		while (lo <= hi)
		{
			const int mid = (lo + hi) / 2;
			if (tokens[(size_t)mid].begin <= off)
			{
				best = mid;
				lo = mid + 1;
			}
			else
				hi = mid - 1;
		}
		if (best < 0) return -1;
		if (inclusiveEnd)
		{
			// prefer a token that ends exactly at the cursor (typing an identifier)
			if (best > 0 && tokens[(size_t)best].begin == off && tokens[(size_t)best - 1].end == off && tokens[(size_t)best - 1].kind == Tok::Name) return best - 1;
			if (tokens[(size_t)best].end >= off) return best;
			return -1;
		}
		if (tokens[(size_t)best].end > off) return best;
		return -1;
	}

	// =============================================================================================
	// Annotations
	// =============================================================================================
	namespace
	{
		std::string_view Trimmed(std::string_view s) { return str::Trim(s); }

		// Reads one type expression from the start of `s` (stops at whitespace outside brackets) and advances `s`.
		std::string TakeType(std::string_view& s)
		{
			s = Trimmed(s);
			size_t i = 0;
			int depth = 0;
			while (i < s.size())
			{
				const char c = s[i];
				if (c == '(' || c == '<' || c == '{' || c == '[') ++depth;
				else if (c == ')' || c == '>' || c == '}' || c == ']') --depth;
				else if ((c == ' ' || c == '\t') && depth <= 0)
				{
					// `fun(a): b` and `A | B` continue over spaces
					size_t j = i;
					while (j < s.size() && (s[j] == ' ' || s[j] == '\t')) ++j;
					if (j < s.size() && (s[j] == '|' || (s[j] == ':' && s.substr(0, 3) == "fun"))) { i = j + 1; while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i; continue; }
					break;
				}
				else if (c == ',' && depth <= 0) break;
				else if (c == '|' && depth <= 0)
				{
					++i;
					while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
					continue;
				}
				++i;
			}
			std::string t(s.substr(0, i));
			s.remove_prefix(i);
			return t;
		}

		void ParseAnnotationLine(std::string_view line, Annotations& a, std::vector<std::string>& desc)
		{
			line = Trimmed(line);
			if (line.empty() || line[0] != '@')
			{
				desc.emplace_back(line);
				return;
			}
			line.remove_prefix(1);
			size_t sp = line.find_first_of(" \t");
			const std::string tag(line.substr(0, sp));
			std::string_view rest = sp == std::string_view::npos ? std::string_view{} : Trimmed(line.substr(sp));
			if (tag == "param")
			{
				ParamInfo p;
				size_t e = rest.find_first_of(" \t");
				p.name = std::string(rest.substr(0, e));
				if (!p.name.empty() && p.name.back() == '?') p.name.pop_back();
				rest = e == std::string_view::npos ? std::string_view{} : rest.substr(e);
				p.type = TakeType(rest);
				p.doc = std::string(Trimmed(rest));
				a.params.push_back(p);
			}
			else if (tag == "return")
			{
				while (!rest.empty())
				{
					ParamInfo r;
					r.type = TakeType(rest);
					rest = Trimmed(rest);
					if (!rest.empty() && rest[0] == '#')
					{
						r.doc = std::string(Trimmed(rest.substr(1)));
						rest = {};
					}
					else if (!rest.empty() && rest[0] != ',')
					{
						size_t e = rest.find_first_of(" \t,");
						r.name = std::string(rest.substr(0, e));
						rest = e == std::string_view::npos ? std::string_view{} : rest.substr(e);
						rest = Trimmed(rest);
						if (!rest.empty() && rest[0] == '#')
						{
							r.doc = std::string(Trimmed(rest.substr(1)));
							rest = {};
						}
					}
					a.returns.push_back(r);
					rest = Trimmed(rest);
					if (!rest.empty() && rest[0] == ',') rest = Trimmed(rest.substr(1));
					else break;
				}
			}
			else if (tag == "class")
			{
				size_t e = rest.find_first_of(" \t:");
				a.classes = std::string(rest.substr(0, e));
				if (e != std::string_view::npos)
				{
					std::string_view r = Trimmed(rest.substr(e));
					if (!r.empty() && r[0] == ':') r = Trimmed(r.substr(1));
					const size_t e2 = r.find_first_of(" \t,");
					a.classBase = std::string(r.substr(0, e2));
				}
			}
			else if (tag == "field")
			{
				if (rest.substr(0, 7) == "private" || rest.substr(0, 9) == "protected" || rest.substr(0, 6) == "public")
					rest = Trimmed(rest.substr(rest.find_first_of(" \t")));
				ParamInfo f;
				size_t e = rest.find_first_of(" \t");
				f.name = std::string(rest.substr(0, e));
				if (!f.name.empty() && f.name.back() == '?') f.name.pop_back();
				rest = e == std::string_view::npos ? std::string_view{} : rest.substr(e);
				f.type = TakeType(rest);
				f.doc = std::string(Trimmed(rest));
				a.fields.push_back(f);
			}
			else if (tag == "type")
				a.type = TakeType(rest);
			else if (tag == "meta")
				a.meta = true;
			else if (tag == "deprecated")
				a.deprecated = true;
		}

		bool IsLineComment(const DocInfo& d, const Token& t)
		{
			return t.kind == Tok::Comment && d.text.compare(t.begin, 3, "--[") != 0;
		}

		// Collects the comment block ending right above token `idx`.
		Annotations CollectDoc(const DocInfo& d, int idx)
		{
			Annotations a;
			if (idx <= 0) return a;
			std::vector<int> block;
			int expectLine = d.tokens[(size_t)idx].line;
			int k = idx - 1;
			while (k >= 0 && IsLineComment(d, d.tokens[(size_t)k]))
			{
				const Token& c = d.tokens[(size_t)k];
				if (c.endLine != expectLine - 1) break;
				// comment must stand on its own line (not trailing code)
				if (k > 0 && d.tokens[(size_t)k - 1].kind != Tok::Comment && d.tokens[(size_t)k - 1].endLine == c.line) break;
				block.push_back(k);
				expectLine = c.line;
				--k;
			}
			std::reverse(block.begin(), block.end());
			std::vector<std::string> desc;
			for (int b : block)
			{
				std::string_view text = d.Text(d.tokens[(size_t)b]);
				text.remove_prefix(2);
				if (!text.empty() && text[0] == '-') text.remove_prefix(1);
				ParseAnnotationLine(text, a, desc);
			}
			std::string joined;
			for (const std::string& l : desc)
			{
				if (!joined.empty()) joined += '\n';
				joined += l;
			}
			a.description = std::string(Trimmed(joined));
			return a;
		}
	}

	std::string Symbol::signature() const
	{
		std::string s = isFunction ? "function " : "local ";
		s += name;
		if (isFunction)
		{
			s += "(";
			for (size_t i = 0; i < paramNames.size(); ++i)
			{
				if (i) s += ", ";
				s += paramNames[i];
				for (const ParamInfo& p : ann.params)
					if (p.name == paramNames[i] && !p.type.empty()) s += ": " + p.type;
			}
			s += ")";
			if (!ann.returns.empty())
			{
				s += ": ";
				for (size_t i = 0; i < ann.returns.size(); ++i)
					s += (i ? ", " : "") + ann.returns[i].type;
			}
		}
		else if (!type.empty())
			s += ": " + type;
		return s;
	}

	std::string Def::signature() const
	{
		std::string p = path;
		if (!p.empty() && p[0] == '~') p.erase(0, 1);
		if (kind == Function || kind == Method)
		{
			p += "(";
			for (size_t i = 0; i < params.size(); ++i)
			{
				if (i) p += ", ";
				p += params[i].name;
				if (!params[i].type.empty()) p += ": " + params[i].type;
			}
			if (vararg) p += params.empty() ? "..." : ", ...";
			p += ")";
			if (!ann.returns.empty())
			{
				p += ": ";
				for (size_t i = 0; i < ann.returns.size(); ++i)
					p += (i ? ", " : "") + ann.returns[i].type;
			}
		}
		else if (!ann.type.empty())
			p += ": " + ann.type;
		else if (!type.empty())
			p += ": " + type;
		return p;
	}

	// =============================================================================================
	// Analyzer
	// =============================================================================================
	namespace
	{
		enum BlockKind { B_Function, B_Do, B_While, B_For, B_If, B_Repeat };
		struct Block
		{
			BlockKind kind;
			std::vector<int> syms;
			size_t brDepth = 0;
		};

		struct Analyzer
		{
			DocInfo& d;
			std::vector<int> ct; // indexes of non-comment tokens
			std::vector<Block> blocks;
			std::vector<char> br;
			std::unordered_map<std::string, std::vector<int>> byName;
			std::set<std::string> defPaths;
			bool pendingDo = false;
			int n = 0;

			explicit Analyzer(DocInfo& doc) : d(doc) {}

			const Token& T(int i) const { return d.tokens[(size_t)ct[(size_t)i]]; }
			std::string_view S(int i) const { return i >= 0 && i < n ? d.Text(T(i)) : std::string_view{}; }
			bool Is(int i, std::string_view s) const
			{
				if (i < 0 || i >= n) return false;
				const Tok k = T(i).kind;
				return (k == Tok::Op || k == Tok::Keyword) && S(i) == s;
			}
			bool IsName(int i) const { return i >= 0 && i < n && T(i).kind == Tok::Name; }
			int Doc(int i) const { return i >= n ? (int)d.tokens.size() : ct[(size_t)i]; }

			bool OperandEnd(int i) const
			{
				if (i < 0 || i >= n) return false;
				const Token& t = T(i);
				if (t.kind == Tok::Name || t.kind == Tok::Number || t.kind == Tok::String) return true;
				const std::string_view s = S(i);
				if (t.kind == Tok::Op) return s == ")" || s == "]" || s == "}" || s == "...";
				return s == "end" || s == "true" || s == "false" || s == "nil";
			}

			// Index of the first token after the expression (list) starting at i.
			int StmtEnd(int i) const
			{
				int depth = 0, nested = 0;
				bool prevOperand = false;
				for (int j = i; j < n; ++j)
				{
					const Token& t = T(j);
					const std::string_view s = S(j);
					if (t.kind == Tok::Eof) return j;
					if (t.kind == Tok::Op)
					{
						if (s == "(" || s == "{" || s == "[")
						{
							if (depth == 0 && nested == 0 && s == "(" && prevOperand && T(j).line > T(j - 1).endLine) return j; // ambiguous call on a new line
							++depth;
							prevOperand = false;
						}
						else if (s == ")" || s == "}" || s == "]")
						{
							--depth;
							if (depth < 0) return j;
							prevOperand = true;
						}
						else if (s == ";")
						{
							if (depth == 0 && nested == 0) return j;
						}
						else if (s == "...")
						{
							if (depth == 0 && nested == 0 && prevOperand) return j;
							prevOperand = true;
						}
						else if (s == "::")
						{
							if (depth == 0 && nested == 0) return j;
						}
						else
							prevOperand = false;
						continue;
					}
					if (t.kind == Tok::Keyword)
					{
						if (s == "function")
						{
							if (depth == 0 && nested == 0 && prevOperand) return j;
							++nested;
							prevOperand = false;
						}
						else if (s == "if" || s == "do")
						{
							if (nested == 0 && depth == 0) return j;
							++nested;
						}
						else if (s == "repeat")
						{
							if (nested == 0 && depth == 0) return j;
							++nested;
						}
						else if (s == "until")
						{
							if (nested == 0) return j;
							--nested;
						}
						else if (s == "end")
						{
							if (nested == 0) return j;
							--nested;
							if (nested == 0) prevOperand = true;
						}
						else if (nested > 0 || depth > 0)
						{
						}
						else if (s == "and" || s == "or" || s == "not")
							prevOperand = false;
						else if (s == "true" || s == "false" || s == "nil")
						{
							if (prevOperand) return j;
							prevOperand = true;
						}
						else
							return j; // local, for, while, return, break, goto, then, else, elseif, in
						continue;
					}
					if (nested > 0 || depth > 0)
					{
						prevOperand = true;
						continue;
					}
					if (t.kind == Tok::Name || t.kind == Tok::Number)
					{
						if (prevOperand) return j;
						prevOperand = true;
					}
					else if (t.kind == Tok::String)
						prevOperand = true;
				}
				return n;
			}

			// Splits an expression list [first, end) into ranges at top level commas.
			std::vector<std::pair<int, int>> SplitList(int first, int end) const
			{
				std::vector<std::pair<int, int>> out;
				int depth = 0, nested = 0, start = first;
				for (int j = first; j < end; ++j)
				{
					const std::string_view s = S(j);
					const Tok k = T(j).kind;
					if (k == Tok::Op)
					{
						if (s == "(" || s == "{" || s == "[") ++depth;
						else if (s == ")" || s == "}" || s == "]") --depth;
						else if (s == "," && depth == 0 && nested == 0)
						{
							out.emplace_back(start, j - 1);
							start = j + 1;
						}
					}
					else if (k == Tok::Keyword)
					{
						if (s == "function" || s == "if" || s == "do" || s == "repeat") ++nested;
						else if (s == "end" || s == "until") --nested;
					}
				}
				if (start < end) out.emplace_back(start, end - 1);
				return out;
			}

			int NewSymbol(const std::string& name, SymKind kind, int tokCt, int visibleFromCt)
			{
				Symbol s;
				s.name = name;
				s.kind = kind;
				s.tok = ct[(size_t)tokCt];
				s.visibleFrom = Doc(visibleFromCt);
				s.visibleTo = INT_MAX;
				d.symbols.push_back(std::move(s));
				const int id = (int)d.symbols.size() - 1;
				byName[name].push_back(id);
				if (!blocks.empty()) blocks.back().syms.push_back(id);
				return id;
			}

			int Lookup(const std::string& name, int docTok) const
			{
				auto it = byName.find(name);
				if (it == byName.end()) return -1;
				int best = -1;
				for (int id : it->second)
				{
					const Symbol& s = d.symbols[(size_t)id];
					if (s.visibleFrom <= docTok && docTok < s.visibleTo && (best < 0 || s.visibleFrom >= d.symbols[(size_t)best].visibleFrom)) best = id;
				}
				return best;
			}

			void AddRef(int ctIdx, bool write)
			{
				Ref r;
				r.tok = ct[(size_t)ctIdx];
				r.write = write;
				r.symbol = Lookup(std::string(S(ctIdx)), r.tok);
				d.refs.push_back(r);
			}

			void CloseBlock(int closeCt)
			{
				if (blocks.empty()) return;
				Block b = std::move(blocks.back());
				blocks.pop_back();
				for (int id : b.syms) d.symbols[(size_t)id].visibleTo = Doc(closeCt);
				if (b.kind == B_Function)
					while (br.size() > b.brDepth) br.pop_back();
			}

			// Names inside `( ... )` starting at the open paren; returns the index of the close paren.
			int PeekParams(int open, std::vector<ParamInfo>& out, bool& vararg) const
			{
				int j = open + 1;
				for (; j < n && !Is(j, ")"); ++j)
				{
					if (IsName(j)) out.push_back({std::string(S(j)), "", ""});
					else if (Is(j, "...")) vararg = true;
				}
				return j;
			}

			static void ApplyParamTypes(std::vector<ParamInfo>& params, const Annotations& a)
			{
				for (ParamInfo& p : params)
					for (const ParamInfo& ap : a.params)
						if (ap.name == p.name)
						{
							p.type = ap.type;
							p.doc = ap.doc;
						}
			}

			// Parses a function body starting at the '(' index; creates param symbols; pushes the block.
			// Returns the index after ')'.
			int FunctionBody(int open, const Annotations& ann, bool addSelf, const std::string& selfType, std::vector<std::string>* paramNames, bool* vararg)
			{
				Block b;
				b.kind = B_Function;
				b.brDepth = br.size();
				blocks.push_back(std::move(b));
				std::vector<ParamInfo> params;
				bool va = false;
				const int close = PeekParams(open, params, va);
				if (addSelf)
				{
					const int id = NewSymbol("self", SymKind::Param, open, std::min(close + 1, n));
					d.symbols[(size_t)id].type = selfType;
					d.symbols[(size_t)id].uses = 1;
				}
				for (const ParamInfo& p : params)
				{
					// locate the token for the symbol position
					int tk = open;
					for (int j = open + 1; j < close; ++j)
						if (IsName(j) && S(j) == p.name) { tk = j; break; }
					const int id = NewSymbol(p.name, SymKind::Param, tk, std::min(close + 1, n));
					for (const ParamInfo& ap : ann.params)
						if (ap.name == p.name)
						{
							d.symbols[(size_t)id].type = ap.type;
							d.symbols[(size_t)id].ann.description = ap.doc;
						}
					if (paramNames) paramNames->push_back(p.name);
				}
				if (va)
				{
					const int id = NewSymbol("...", SymKind::Param, open, std::min(close + 1, n));
					d.symbols[(size_t)id].uses = 1;
				}
				if (vararg) *vararg = va;
				br.push_back('F');
				return std::min(close + 1, n);
			}

			Def MakeDef(const std::string& path, Def::Kind kind, int nameCt, const Annotations& ann)
			{
				Def def;
				def.path = path;
				def.kind = kind;
				def.ann = ann;
				def.type = ann.type;
				def.tok = ct[(size_t)nameCt];
				def.uri = d.uri;
				return def;
			}

			void AddDef(Def def)
			{
				if (defPaths.count(def.path)) {
					// keep the richest (annotated) definition
					for (Def& e : d.defs)
						if (e.path == def.path)
						{
							if (e.ann.empty() && !def.ann.empty()) e = std::move(def);
							return;
						}
				}
				defPaths.insert(def.path);
				d.defs.push_back(std::move(def));
			}

			// Root name used in def paths: classes use their class name, other locals "~name", globals plain.
			std::string RootPath(int symbolId, const std::string& name) const
			{
				if (symbolId < 0) return name;
				const Symbol& s = d.symbols[(size_t)symbolId];
				if (s.type.rfind("cls:", 0) == 0) return s.type.substr(4);
				return "~" + name;
			}

			void Run();
			void HandleLocal(int& i);
			void HandleFunctionKeyword(int& i);
			void HandleFor(int& i);
			void HandleStatementStart(int i);
		};

		void Analyzer::HandleLocal(int& i)
		{
			// i at `local`
			const int localCt = i;
			if (Is(i + 1, "function") && IsName(i + 2))
			{
				Annotations ann = CollectDoc(d, ct[(size_t)localCt]);
				const int id = NewSymbol(std::string(S(i + 2)), SymKind::LocalFunction, i + 2, i + 2);
				d.symbols[(size_t)id].isFunction = true;
				d.symbols[(size_t)id].ann = ann;
				d.symbols[(size_t)id].uses = 0;
				d.symbols[(size_t)id].type = "function";
				int open = i + 3;
				if (Is(open, "("))
				{
					// the symbol belongs to the enclosing block, the body pushes its own
					std::vector<std::string> names;
					bool va = false;
					i = FunctionBody(open, ann, false, "", &names, &va) - 1;
					d.symbols[(size_t)id].paramNames = names;
					d.symbols[(size_t)id].isVararg = va;
				}
				else
					i = i + 2;
				return;
			}
			Annotations ann = CollectDoc(d, ct[(size_t)localCt]);
			std::vector<int> ids;
			int j = i + 1;
			std::vector<int> nameCts;
			while (IsName(j))
			{
				nameCts.push_back(j);
				++j;
				if (Is(j, "<"))
				{
					while (j < n && !Is(j, ">")) ++j;
					++j;
				}
				if (Is(j, ",")) ++j;
				else break;
			}
			int visibleFrom = j;
			std::vector<std::pair<int, int>> exprs;
			int stmtEndIdx = j;
			if (Is(j, "="))
			{
				stmtEndIdx = StmtEnd(j + 1);
				visibleFrom = stmtEndIdx;
				exprs = SplitList(j + 1, stmtEndIdx);
			}
			for (size_t k = 0; k < nameCts.size(); ++k)
			{
				const int id = NewSymbol(std::string(S(nameCts[k])), SymKind::Local, nameCts[k], visibleFrom);
				Symbol& s = d.symbols[(size_t)id];
				s.ann = k == 0 ? ann : Annotations{};
				if (k == 0 && !ann.type.empty()) s.type = ann.type;
				if (k == 0 && !ann.classes.empty()) s.type = "cls:" + ann.classes;
				if (k < exprs.size())
				{
					s.initFirst = ct[(size_t)exprs[k].first];
					s.initLast = ct[(size_t)exprs[k].second];
					// require "x"
					if (exprs[k].first + 1 <= exprs[k].second && S(exprs[k].first) == "require")
					{
						int a = exprs[k].first + 1;
						if (Is(a, "(")) ++a;
						if (a < n && T(a).kind == Tok::String)
						{
							std::string_view lit = S(a);
							if (lit.size() >= 2) s.requireName = std::string(lit.substr(1, lit.size() - 2));
						}
					}
					if (Is(exprs[k].first, "function") && Is(exprs[k].first + 1, "(")) s.isFunction = true;
				}
				else if (!exprs.empty())
				{
					s.initFirst = ct[(size_t)exprs.back().first];
					s.initLast = ct[(size_t)exprs.back().second];
					s.destructure = (int)(k - (exprs.size() - 1));
				}
				if (!ann.classes.empty() && k == 0)
				{
					d.classes.push_back(ann.classes);
					if (!ann.classBase.empty()) d.classBases[ann.classes] = ann.classBase;
					Def cd = MakeDef(ann.classes, Def::Class, nameCts[0], ann);
					AddDef(cd);
					for (const ParamInfo& f : ann.fields)
					{
						Def fd = MakeDef(ann.classes + "." + f.name, Def::Field, nameCts[0], Annotations{});
						fd.type = f.type;
						fd.ann.description = f.doc;
						fd.ann.type = f.type;
						AddDef(fd);
					}
				}
			}
			i = j - 1; // continue after the names; the initializer is walked normally
		}

		void Analyzer::HandleFor(int& i)
		{
			// i at `for`
			int j = i + 1;
			std::vector<int> names;
			while (IsName(j))
			{
				names.push_back(j);
				++j;
				if (Is(j, ",")) ++j;
				else break;
			}
			// find the `do`
			int nest = 0, doIdx = n;
			for (int k = j; k < n; ++k)
			{
				if (Is(k, "function")) ++nest;
				else if (Is(k, "end")) --nest;
				else if (nest <= 0 && Is(k, "do"))
				{
					doIdx = k;
					break;
				}
			}
			Block b;
			b.kind = B_For;
			blocks.push_back(std::move(b));
			int iterFirst = -1, iterLast = -1;
			if (Is(j, "in")) { iterFirst = j + 1; iterLast = doIdx - 1; }
			// `ipairs(x)` / `pairs(x)` detection
			int mode = 0;
			int innerFirst = iterFirst, innerLast = iterLast;
			if (iterFirst >= 0 && (S(iterFirst) == "ipairs" || S(iterFirst) == "pairs") && Is(iterFirst + 1, "(") && Is(iterLast, ")"))
			{
				mode = S(iterFirst) == "ipairs" ? 1 : 2;
				innerFirst = iterFirst + 2;
				innerLast = iterLast - 1;
			}
			for (size_t k = 0; k < names.size(); ++k)
			{
				const int id = NewSymbol(std::string(S(names[k])), SymKind::ForVar, names[k], doIdx);
				Symbol& s = d.symbols[(size_t)id];
				if (iterFirst < 0)
					s.type = "number";
				else if (mode != 0 && innerFirst <= innerLast)
				{
					s.initFirst = ct[(size_t)innerFirst];
					s.initLast = ct[(size_t)innerLast];
					if (k == 0) s.forMode = mode == 1 ? 2 : 3;
					else if (k == 1) s.forMode = 1;
				}
			}
			pendingDo = true;
			i = j - 1;
		}

		void Analyzer::HandleFunctionKeyword(int& i)
		{
			const int fnCt = i;
			// named statement function?
			if (IsName(i + 1))
			{
				Annotations ann = CollectDoc(d, ct[(size_t)fnCt]);
				int j = i + 1;
				const int firstName = j;
				std::string chain(S(j));
				std::vector<std::string> parts{chain};
				bool method = false;
				int lastName = j;
				++j;
				while ((Is(j, ".") || Is(j, ":")) && IsName(j + 1))
				{
					if (Is(j, ":")) method = true;
					parts.emplace_back(S(j + 1));
					lastName = j + 1;
					j += 2;
				}
				// root reference
				const int rootSym = Lookup(parts[0], ct[(size_t)firstName]);
				{
					Ref r;
					r.tok = ct[(size_t)firstName];
					r.symbol = rootSym;
					r.write = parts.size() == 1;
					d.refs.push_back(r);
				}
				std::string root = RootPath(rootSym, parts[0]);
				std::string owner = root;
				for (size_t k = 1; k + 1 < parts.size(); ++k) owner += "." + parts[k];
				std::string path;
				if (parts.size() == 1)
					path = rootSym >= 0 && d.symbols[(size_t)rootSym].kind != SymKind::Global ? "" : parts[0];
				else
					path = owner + (method ? ":" : ".") + parts.back();
				std::string selfType;
				if (method)
				{
					// type of `self`
					selfType = "self:" + owner;
					if (!owner.empty() && owner[0] != '~') selfType = "cls:" + owner;
					if (!owner.empty() && owner[0] != '~' && !(std::find(d.classes.begin(), d.classes.end(), owner) != d.classes.end())) selfType = "self:" + owner;
				}
				std::vector<std::string> names;
				bool va = false;
				if (Is(j, "("))
				{
					std::vector<ParamInfo> params;
					bool vv = false;
					PeekParams(j, params, vv);
					ApplyParamTypes(params, ann);
					if (!path.empty())
					{
						Def def = MakeDef(path, method ? Def::Method : Def::Function, lastName, ann);
						def.params = params;
						def.vararg = vv;
						AddDef(def);
					}
					else if (rootSym >= 0 && parts.size() == 1)
					{
						Symbol& s = d.symbols[(size_t)rootSym];
						s.isFunction = true;
						if (s.ann.empty()) s.ann = ann;
						for (const ParamInfo& p : params) s.paramNames.push_back(p.name);
					}
					if (parts.size() == 1 && rootSym < 0) d.assignedGlobals.insert(parts[0]);
					i = FunctionBody(j, ann, method, selfType, &names, &va) - 1;
				}
				else
					i = j - 1;
				return;
			}
			// anonymous function
			if (Is(i + 1, "("))
			{
				Annotations none;
				i = FunctionBody(i + 1, none, false, "", nullptr, nullptr) - 1;
			}
		}

		void Analyzer::HandleStatementStart(int i)
		{
			// `name {.field} = expr`  or  `a, b = ...`
			int j = i;
			std::vector<std::string> parts{std::string(S(j))};
			std::vector<int> nameCts{j};
			++j;
			bool simple = true;
			while (true)
			{
				if (Is(j, ".") && IsName(j + 1))
				{
					parts.emplace_back(S(j + 1));
					nameCts.push_back(j + 1);
					j += 2;
				}
				else
				{
					simple = false;
					break;
				}
			}
			// destination list a, b = ...
			if (Is(j, ",") && parts.size() == 1)
			{
				std::vector<int> lhs{i};
				int k = j;
				bool ok = true;
				while (Is(k, ","))
				{
					if (!IsName(k + 1)) { ok = false; break; }
					lhs.push_back(k + 1);
					k += 2;
				}
				if (ok && Is(k, "="))
					for (int id : lhs)
					{
						const std::string name(S(id));
						const int s = Lookup(name, ct[(size_t)id]);
						if (s < 0) d.assignedGlobals.insert(name);
						Ref r;
						r.tok = ct[(size_t)id];
						r.symbol = s;
						r.write = true;
						d.refs.push_back(r);
					}
				return;
			}
			(void)simple;
			if (!Is(j, "=")) return;
			const std::string firstName = parts[0];
			const int rootSym = Lookup(firstName, ct[(size_t)i]);
			Annotations ann = CollectDoc(d, ct[(size_t)i]);
			const int endIdx = StmtEnd(j + 1);
			const auto exprs = SplitList(j + 1, endIdx);
			std::string path;
			if (parts.size() == 1)
			{
				if (rootSym >= 0)
				{
					// assignment to a local: remember the initializer for type inference
					Symbol& s = d.symbols[(size_t)rootSym];
					if (s.type.empty() && s.initFirst < 0 && !exprs.empty())
					{
						s.initFirst = ct[(size_t)exprs[0].first];
						s.initLast = ct[(size_t)exprs[0].second];
					}
					return;
				}
				path = firstName;
				d.assignedGlobals.insert(firstName);
			}
			else
			{
				std::string root = RootPath(rootSym, firstName);
				path = root;
				for (size_t k = 1; k < parts.size(); ++k) path += "." + parts[k];
				// `self.x = v` inside a method: field of the owner table
				if (rootSym >= 0 && d.symbols[(size_t)rootSym].name == "self")
				{
					const std::string& st = d.symbols[(size_t)rootSym].type;
					if (st.rfind("self:", 0) == 0 && parts.size() == 2) path = st.substr(5) + "." + parts[1];
					else if (st.rfind("cls:", 0) == 0 && parts.size() == 2) path = st.substr(4) + "." + parts[1];
					else
						return;
				}
			}
			Def::Kind kind = Def::Field;
			Def def;
			if (!exprs.empty() && Is(exprs[0].first, "function") && Is(exprs[0].first + 1, "("))
			{
				kind = Def::Function;
				std::vector<ParamInfo> params;
				bool va = false;
				PeekParams(exprs[0].first + 1, params, va);
				ApplyParamTypes(params, ann);
				def = MakeDef(path, kind, nameCts.back(), ann);
				def.params = params;
				def.vararg = va;
			}
			else
			{
				if (!exprs.empty() && Is(exprs[0].first, "{")) kind = Def::Table;
				def = MakeDef(path, kind, nameCts.back(), ann);
				if (!exprs.empty())
				{
					def.initFirst = ct[(size_t)exprs[0].first];
					def.initLast = ct[(size_t)exprs[0].second];
				}
			}
			if (!ann.classes.empty())
			{
				d.classes.push_back(ann.classes);
				if (!ann.classBase.empty()) d.classBases[ann.classes] = ann.classBase;
				Def cd = MakeDef(ann.classes, Def::Class, nameCts.back(), ann);
				AddDef(cd);
				for (const ParamInfo& f : ann.fields)
				{
					Def fd = MakeDef(ann.classes + "." + f.name, Def::Field, nameCts.back(), Annotations{});
					fd.type = f.type;
					fd.ann.type = f.type;
					fd.ann.description = f.doc;
					AddDef(fd);
				}
			}
			AddDef(def);
		}

		void Analyzer::Run()
		{
			n = (int)ct.size();
			Block root;
			root.kind = B_Function;
			blocks.push_back(std::move(root));

			auto stmtStart = [&](int i) {
				if (i == 0) return true;
				const int p = i - 1;
				if (Is(p, ";") || Is(p, "::")) return true;
				if (T(p).kind == Tok::Keyword)
				{
					const std::string_view s = S(p);
					return s == "do" || s == "then" || s == "else" || s == "repeat" || s == "end" || s == "true" || s == "false" || s == "nil";
				}
				return OperandEnd(p);
			};

			for (int i = 0; i < n; ++i)
			{
				const Token& t = T(i);
				const std::string_view s = S(i);
				if (t.kind == Tok::Eof) break;
				if (t.kind == Tok::Op)
				{
					if (s == "(" || s == "{" || s == "[") br.push_back(s[0]);
					else if ((s == ")" || s == "}" || s == "]") && !br.empty() && br.back() != 'F') br.pop_back();
					continue;
				}
				if (t.kind == Tok::Keyword)
				{
					if (s == "local") HandleLocal(i);
					else if (s == "function") HandleFunctionKeyword(i);
					else if (s == "for") HandleFor(i);
					else if (s == "while")
					{
						Block b;
						b.kind = B_While;
						blocks.push_back(std::move(b));
						pendingDo = true;
					}
					else if (s == "do")
					{
						if (pendingDo) pendingDo = false;
						else
						{
							Block b;
							b.kind = B_Do;
							blocks.push_back(std::move(b));
						}
					}
					else if (s == "if")
					{
						Block b;
						b.kind = B_If;
						blocks.push_back(std::move(b));
					}
					else if (s == "repeat")
					{
						Block b;
						b.kind = B_Repeat;
						blocks.push_back(std::move(b));
					}
					else if (s == "else" || s == "elseif")
					{
						if (!blocks.empty() && blocks.back().kind == B_If)
						{
							for (int id : blocks.back().syms) d.symbols[(size_t)id].visibleTo = Doc(i);
							blocks.back().syms.clear();
						}
					}
					else if (s == "end")
					{
						if (blocks.size() > 1) CloseBlock(i);
					}
					else if (s == "until")
					{
						if (blocks.size() > 1 && blocks.back().kind == B_Repeat) CloseBlock(StmtEnd(i + 1));
					}
					else if (s == "return")
					{
						if (blocks.size() == 1 && IsName(i + 1) && i + 2 < n && T(i + 2).kind == Tok::Eof)
						{
							d.returnedSymbol = std::string(S(i + 1));
						}
						else if (blocks.size() == 1 && IsName(i + 1) && i + 3 < n && Is(i + 2, ";") && T(i + 3).kind == Tok::Eof)
							d.returnedSymbol = std::string(S(i + 1));
					}
					else if (s == "goto")
						++i;
					continue;
				}
				if (t.kind != Tok::Name) continue;
				// plain name
				if (Is(i - 1, ".") || Is(i - 1, ":")) continue;
				if (Is(i + 1, "::") || Is(i - 1, "::")) continue;
				if (Is(i + 1, "=") && !br.empty() && br.back() == '{') continue; // table key
				if (stmtStart(i) && (br.empty() || br.back() == 'F')) HandleStatementStart(i);
				// reference (HandleStatementStart may have added one for list assignments; avoid duplicates)
				bool dup = false;
				for (size_t k = d.refs.size(); k-- > 0 && k + 8 > d.refs.size();)
					if (d.refs[k].tok == ct[(size_t)i]) { dup = true; break; }
				if (!dup)
				{
					bool write = false;
					if (Is(i + 1, "=") && stmtStart(i) && (br.empty() || br.back() == 'F')) write = true;
					AddRef(i, write);
				}
			}
			// close everything that is still open
			while (blocks.size() > 1) CloseBlock(n);
			for (int id : blocks.back().syms) d.symbols[(size_t)id].visibleTo = INT_MAX;
		}
	}

	void Analyze(DocInfo& doc, const std::string& uri, std::string text)
	{
		doc.uri = uri;
		doc.text = std::move(text);
		doc.tokens = Tokenize(doc.text);
		doc.symbols.clear();
		doc.refs.clear();
		doc.defs.clear();
		doc.classes.clear();
		doc.classBases.clear();
		doc.assignedGlobals.clear();
		doc.requires_.clear();
		doc.returnedSymbol.clear();
		doc.returnedSymbolIndex = -1;
		doc.lineStarts.clear();
		doc.lineStarts.push_back(0);
		for (size_t i = 0; i < doc.text.size(); ++i)
			if (doc.text[i] == '\n') doc.lineStarts.push_back((uint32_t)i + 1);
		doc.isMeta = false;
		for (const Token& t : doc.tokens)
		{
			if (t.kind == Tok::Comment && doc.text.compare(t.begin, 8, "---@meta") == 0) { doc.isMeta = true; break; }
			if (t.kind != Tok::Comment) break;
		}

		Analyzer a(doc);
		for (size_t i = 0; i < doc.tokens.size(); ++i)
			if (doc.tokens[i].kind != Tok::Comment) a.ct.push_back((int)i);
		a.Run();

		// `require "x"` calls anywhere
		for (size_t i = 0; i + 1 < doc.tokens.size(); ++i)
		{
			const Token& t = doc.tokens[i];
			if (t.kind == Tok::Name && doc.Text(t) == "require")
			{
				size_t j = i + 1;
				if (j < doc.tokens.size() && doc.Text(doc.tokens[j]) == "(") ++j;
				if (j < doc.tokens.size() && doc.tokens[j].kind == Tok::String)
				{
					std::string_view lit = doc.Text(doc.tokens[j]);
					if (lit.size() >= 2) doc.requires_.emplace_back(lit.substr(1, lit.size() - 2));
				}
			}
		}
		// returned local
		if (!doc.returnedSymbol.empty())
			for (size_t i = 0; i < doc.symbols.size(); ++i)
				if (doc.symbols[i].name == doc.returnedSymbol && doc.symbols[i].visibleTo == INT_MAX) doc.returnedSymbolIndex = (int)i;
		// uses
		for (const Ref& r : doc.refs)
			if (r.symbol >= 0 && !r.write) ++doc.symbols[(size_t)r.symbol].uses;
	}

} // namespace lumen::lua
