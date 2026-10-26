#include "lumen/core/Lsp.h"

#include "lumen/core/Fs.h"
#include "lumen/core/Strings.h"

#include <algorithm>
#include <climits>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace lumen::lsp
{
	using namespace lumen::lua;

	namespace
	{
		const char* const kKeywords[] = {"and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if", "in",
			"local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while"};

		struct Snippet
		{
			const char* label;
			const char* text;
			const char* doc;
		};
		const Snippet kSnippets[] = {
			{"function", "function ${1:name}(${2})\n\t$0\nend", "Function declaration"},
			{"local function", "local function ${1:name}(${2})\n\t$0\nend", "Local function"},
			{"for i", "for ${1:i} = ${2:1}, ${3:10} do\n\t$0\nend", "Numeric for loop"},
			{"for in ipairs", "for ${1:i}, ${2:v} in ipairs(${3:list}) do\n\t$0\nend", "Iterate an array"},
			{"for in pairs", "for ${1:k}, ${2:v} in pairs(${3:table}) do\n\t$0\nend", "Iterate a table"},
			{"if", "if ${1:condition} then\n\t$0\nend", "If statement"},
			{"ifelse", "if ${1:condition} then\n\t$2\nelse\n\t$0\nend", "If / else"},
			{"while", "while ${1:condition} do\n\t$0\nend", "While loop"},
			{"repeat", "repeat\n\t$0\nuntil ${1:condition}", "Repeat loop"},
			{"script", "---@class ${1:MyScript} : Script\nlocal $1 = {}\n\n-- inspector properties: plain values are editable per entity\n$1.speed = 5\n\nfunction $1:start()\n\t$0\nend\n\nfunction $1:update(dt)\nend\n\nreturn $1", "Entity script skeleton"},
			{"module", "local M = {}\n\nfunction M.${1:name}(${2})\n\t$0\nend\n\nreturn M", "Module skeleton"},
			{"require", "local ${1:mod} = require \"${2:module}\"", "Require a module"},
		};

		int Utf16Col(const DocInfo& d, int line, int byteCol) { return ByteColToUtf16(d.LineText(line), byteCol); }

		Json MakePos(const DocInfo& d, int line, int byteCol)
		{
			Json p = Json::MakeObject();
			p["line"] = line;
			p["character"] = Utf16Col(d, line, byteCol);
			return p;
		}

		Json MakeRange(const DocInfo& d, int l0, int c0, int l1, int c1)
		{
			Json r = Json::MakeObject();
			r["start"] = MakePos(d, l0, c0);
			r["end"] = MakePos(d, l1, c1);
			return r;
		}

		Json TokRange(const DocInfo& d, int tok)
		{
			const Token& t = d.tokens[(size_t)tok];
			return MakeRange(d, t.line, t.col, t.endLine, t.endCol);
		}

		Json Location(const DocInfo& d, int tok)
		{
			Json l = Json::MakeObject();
			l["uri"] = d.uri;
			l["range"] = TokRange(d, tok);
			return l;
		}

		struct Cursor
		{
			int line = 0, byteCol = 0;
			uint32_t offset = 0;
		};

		Cursor ReadCursor(const DocInfo& d, const Json& params)
		{
			const Json& pos = params.Get("position");
			Cursor c;
			c.line = (int)pos.GetInt("line");
			if (c.line < 0) c.line = 0;
			if (c.line >= (int)d.lineStarts.size()) c.line = (int)d.lineStarts.size() - 1;
			c.byteCol = Utf16ToByteCol(d.LineText(c.line), (int)pos.GetInt("character"));
			c.offset = d.lineStarts[(size_t)c.line] + (uint32_t)c.byteCol;
			return c;
		}

		std::string Markdown(const std::string& code, const std::string& doc)
		{
			std::string s = "```lua\n" + code + "\n```";
			if (!doc.empty()) s += "\n\n" + doc;
			return s;
		}

		std::string DefMarkdown(const Def& d)
		{
			std::string doc = d.ann.description;
			std::string params;
			for (const ParamInfo& p : d.params)
				if (!p.doc.empty()) params += "\n- `" + p.name + "` " + p.doc;
			if (!params.empty()) doc += "\n\n**Parameters**" + params;
			std::string rets;
			for (const ParamInfo& r : d.ann.returns)
				if (!r.doc.empty()) rets += "\n- `" + r.type + "` " + r.doc;
			if (!rets.empty()) doc += "\n\n**Returns**" + rets;
			if (d.ann.deprecated) doc = "**Deprecated.**\n\n" + doc;
			return Markdown(d.signature(), doc);
		}

		int CompletionKind(Member::Kind k)
		{
			switch (k)
			{
			case Member::Function: return 3;
			case Member::Method: return 2;
			case Member::Field: return 5;
			case Member::Namespace: return 9;
			case Member::Variable: return 6;
			case Member::Module: return 9;
			case Member::Class: return 7;
			}
			return 1;
		}

		// code token indices (comments skipped)
		std::vector<int> CodeTokens(const DocInfo& d)
		{
			std::vector<int> v;
			for (size_t i = 0; i < d.tokens.size(); ++i)
				if (d.tokens[i].kind != Tok::Comment && d.tokens[i].kind != Tok::Eof) v.push_back((int)i);
			return v;
		}

		// Walks back from the end of a `a.b:c(...)[..]` chain to its first token. `endPos` indexes `code`.
		int ChainStart(const DocInfo& d, const std::vector<int>& code, int endPos)
		{
			int i = endPos;
			while (i >= 0)
			{
				const Token& t = d.tokens[(size_t)code[(size_t)i]];
				const std::string_view s = d.Text(t);
				if (t.kind == Tok::Op && (s == ")" || s == "]"))
				{
					int depth = 0, j = i;
					for (; j >= 0; --j)
					{
						const std::string_view q = d.Text(d.tokens[(size_t)code[(size_t)j]]);
						const Tok qk = d.tokens[(size_t)code[(size_t)j]].kind;
						if (qk != Tok::Op) continue;
						if (q == ")" || q == "]" || q == "}") ++depth;
						else if (q == "(" || q == "[" || q == "{")
						{
							if (--depth == 0) break;
						}
					}
					if (j < 0) return i;
					i = j - 1;
					if (i < 0) return j;
					const Token& pt = d.tokens[(size_t)code[(size_t)i]];
					const std::string_view ps = d.Text(pt);
					if (pt.kind == Tok::Name || pt.kind == Tok::String || (pt.kind == Tok::Op && (ps == ")" || ps == "]"))) continue;
					return j; // parenthesized expression start
				}
				if (t.kind == Tok::Name || t.kind == Tok::String || t.kind == Tok::Number)
				{
					if (i >= 1)
					{
						const Token& pt = d.tokens[(size_t)code[(size_t)i - 1]];
						const std::string_view ps = d.Text(pt);
						if (pt.kind == Tok::Op && (ps == "." || ps == ":") && i >= 2)
						{
							i -= 2;
							continue;
						}
					}
					return i;
				}
				return i + 1 <= endPos ? i + 1 : i;
			}
			return 0;
		}

		std::string ApplyIncremental(const std::string& text, const Json& range, const std::string& newText)
		{
			auto lineStartOf = [&](int line) -> size_t {
				size_t pos = 0;
				for (int l = 0; l < line; ++l)
				{
					size_t nl = text.find('\n', pos);
					if (nl == std::string::npos) return text.size();
					pos = nl + 1;
				}
				return pos;
			};
			auto offsetOf = [&](const Json& p) -> size_t {
				const int line = (int)p.GetInt("line");
				const size_t ls = lineStartOf(line);
				size_t le = text.find('\n', ls);
				if (le == std::string::npos) le = text.size();
				std::string_view lt = std::string_view(text).substr(ls, le - ls);
				if (!lt.empty() && lt.back() == '\r') lt.remove_suffix(1);
				return ls + (size_t)Utf16ToByteCol(lt, (int)p.GetInt("character"));
			};
			size_t a = offsetOf(range.Get("start")), b = offsetOf(range.Get("end"));
			if (b < a) std::swap(a, b);
			std::string out = text.substr(0, a);
			out += newText;
			out += text.substr(b);
			return out;
		}

		Json Response(const Json& id, Json result)
		{
			Json r = Json::MakeObject();
			r["jsonrpc"] = "2.0";
			r["id"] = id;
			r["result"] = std::move(result);
			return r;
		}

		Json ErrorResponse(const Json& id, int code, const std::string& msg)
		{
			Json r = Json::MakeObject();
			r["jsonrpc"] = "2.0";
			r["id"] = id;
			Json e = Json::MakeObject();
			e["code"] = code;
			e["message"] = msg;
			r["error"] = e;
			return r;
		}

		Json Notification(const std::string& method, Json params)
		{
			Json r = Json::MakeObject();
			r["jsonrpc"] = "2.0";
			r["method"] = method;
			r["params"] = std::move(params);
			return r;
		}
	}

	Server::Server(std::vector<std::string> apiDirs, std::vector<std::string> moduleDirs) : apiDirs_(std::move(apiDirs)), moduleDirs_(std::move(moduleDirs)) {}

	// =============================================================================================
	// dispatch
	// =============================================================================================
	std::vector<Json> Server::Handle(const Json& msg)
	{
		std::vector<Json> out;
		const std::string method = msg.GetString("method");
		const Json* idp = msg.Find("id");
		const Json params = msg.Get("params");
		if (method.empty()) return out; // a response to a request we never sent

		if (!idp)
		{
			// notifications
			if (method == "exit")
			{
				exit_ = true;
			}
			else if (method == "initialized" || method == "$/cancelRequest" || method == "$/setTrace")
			{
			}
			else if (method == "textDocument/didOpen")
			{
				const Json& td = params.Get("textDocument");
				const std::string uri = td.GetString("uri");
				ws_.Open(uri, td.GetString("text"), (int)td.GetInt("version"));
				out.push_back(PublishDiagnostics(uri));
			}
			else if (method == "textDocument/didChange")
			{
				const std::string uri = params.Get("textDocument").GetString("uri");
				DocInfo* d = ws_.Find(uri);
				std::string text = d ? d->text : std::string{};
				for (const Json& ch : params.Get("contentChanges").Items())
				{
					if (ch.Contains("range")) text = ApplyIncremental(text, ch.Get("range"), ch.GetString("text"));
					else text = ch.GetString("text");
				}
				ws_.Open(uri, std::move(text), (int)params.Get("textDocument").GetInt("version"));
				out.push_back(PublishDiagnostics(uri));
			}
			else if (method == "textDocument/didClose")
			{
				const std::string uri = params.Get("textDocument").GetString("uri");
				ws_.Close(uri);
				// reload the on-disk version so cross file features keep working
				ws_.Load(UriToPath(uri));
				Json p = Json::MakeObject();
				p["uri"] = uri;
				p["diagnostics"] = Json::MakeArray();
				out.push_back(Notification("textDocument/publishDiagnostics", p));
			}
			else if (method == "workspace/didChangeWatchedFiles")
			{
				ws_.Scan();
			}
			return out;
		}

		const Json& id = *idp;
		if (method == "initialize")
		{
			out.push_back(Response(id, Initialize(params)));
			return out;
		}
		if (method == "shutdown")
		{
			shutdown_ = true;
			out.push_back(Response(id, Json()));
			return out;
		}
		if (!initialized_)
		{
			out.push_back(ErrorResponse(id, -32002, "server not initialized"));
			return out;
		}
		Json result;
		if (method == "textDocument/completion") result = Completion(params);
		else if (method == "textDocument/hover") result = Hover(params);
		else if (method == "textDocument/definition") result = Definition(params);
		else if (method == "textDocument/references") result = References(params);
		else if (method == "textDocument/prepareRename") result = PrepareRename(params);
		else if (method == "textDocument/rename") result = Rename(params);
		else if (method == "textDocument/signatureHelp") result = SignatureHelp(params);
		else if (method == "textDocument/documentSymbol") result = DocumentSymbols(params);
		else if (method == "textDocument/documentHighlight") result = DocumentHighlight(params);
		else if (method == "workspace/symbol") result = WorkspaceSymbols(params);
		else
		{
			out.push_back(ErrorResponse(id, -32601, "method not found: " + method));
			return out;
		}
		out.push_back(Response(id, std::move(result)));
		return out;
	}

	Json Server::Initialize(const Json& params)
	{
		std::string root;
		const Json& folders = params.Get("workspaceFolders");
		if (folders.Size() > 0) root = UriToPath(folders.At(0).GetString("uri"));
		if (root.empty() && params.Contains("rootUri") && params.Get("rootUri").IsString()) root = UriToPath(params.GetString("rootUri"));
		if (root.empty()) root = params.GetString("rootPath");
		const Json& opts = params.Get("initializationOptions");
		for (const std::string& s : opts.GetStringArray("apiDirs")) apiDirs_.push_back(s);
		for (const std::string& s : opts.GetStringArray("moduleDirs")) moduleDirs_.push_back(s);
		ws_.SetRoots(root, moduleDirs_, apiDirs_);
		ws_.Scan();
		initialized_ = true;

		Json caps = Json::MakeObject();
		Json sync = Json::MakeObject();
		sync["openClose"] = true;
		sync["change"] = 2;
		caps["textDocumentSync"] = sync;
		Json comp = Json::MakeObject();
		Json trig = Json::MakeArray();
		for (const char* c : {".", ":", "\"", "'", "/"}) trig.Push(c);
		comp["triggerCharacters"] = trig;
		comp["resolveProvider"] = false;
		caps["completionProvider"] = comp;
		caps["hoverProvider"] = true;
		caps["definitionProvider"] = true;
		caps["referencesProvider"] = true;
		caps["documentHighlightProvider"] = true;
		Json rename = Json::MakeObject();
		rename["prepareProvider"] = true;
		caps["renameProvider"] = rename;
		Json sig = Json::MakeObject();
		Json st = Json::MakeArray();
		st.Push("(");
		st.Push(",");
		sig["triggerCharacters"] = st;
		caps["signatureHelpProvider"] = sig;
		caps["documentSymbolProvider"] = true;
		caps["workspaceSymbolProvider"] = true;
		Json res = Json::MakeObject();
		res["capabilities"] = caps;
		Json info = Json::MakeObject();
		info["name"] = "lumen-lsp";
		info["version"] = LUMEN_VERSION_STRING;
		res["serverInfo"] = info;
		return res;
	}

	Json Server::PublishDiagnostics(const std::string& uri)
	{
		Json p = Json::MakeObject();
		p["uri"] = uri;
		Json arr = Json::MakeArray();
		if (const DocInfo* d = ws_.Find(uri))
			for (const Diag& g : ws_.Diagnose(*d))
			{
				Json j = Json::MakeObject();
				j["range"] = MakeRange(*d, g.line, g.col, g.endLine, g.endCol);
				j["severity"] = g.severity;
				j["source"] = "lumen";
				j["message"] = g.message;
				Json tags = Json::MakeArray();
				if (g.unnecessary) tags.Push(1);
				if (g.deprecated) tags.Push(2);
				if (!tags.Empty()) j["tags"] = tags;
				arr.Push(std::move(j));
			}
		p["diagnostics"] = arr;
		return Notification("textDocument/publishDiagnostics", p);
	}

	// =============================================================================================
	// completion
	// =============================================================================================
	namespace
	{
		Json MemberItem(const Member& m, bool snippetCalls, const Workspace& ws)
		{
			Json it = Json::MakeObject();
			it["label"] = m.name;
			it["kind"] = CompletionKind(m.kind);
			if (!m.detail.empty()) it["detail"] = m.detail;
			else if (!m.type.empty()) it["detail"] = Workspace::DisplayType(m.type);
			if (m.def)
			{
				Json doc = Json::MakeObject();
				doc["kind"] = "markdown";
				doc["value"] = DefMarkdown(*m.def);
				it["documentation"] = doc;
				if (m.def->ann.deprecated)
				{
					Json tags = Json::MakeArray();
					tags.Push(1);
					it["tags"] = tags;
				}
			}
			if (snippetCalls && m.def && (m.def->kind == Def::Function || m.def->kind == Def::Method))
			{
				std::string snip = m.name + "(";
				int n = 1;
				for (const ParamInfo& p : m.def->params)
				{
					if (n > 1) snip += ", ";
					snip += "${" + std::to_string(n++) + ":" + p.name + "}";
				}
				snip += ")";
				it["insertText"] = snip;
				it["insertTextFormat"] = 2;
			}
			(void)ws;
			return it;
		}
	}

	Json Server::Completion(const Json& params)
	{
		Json items = Json::MakeArray();
		const std::string uri = params.Get("textDocument").GetString("uri");
		const DocInfo* dp = ws_.Find(uri);
		if (!dp) return items;
		const DocInfo& d = *dp;
		const Cursor cur = ReadCursor(d, params);
		const std::string_view lineText = d.LineText(cur.line);
		const bool nextIsParen = (size_t)cur.byteCol < lineText.size() && lineText[(size_t)cur.byteCol] == '(';

		// token under / before the cursor
		int under = d.TokenAt(cur.line, cur.byteCol, true);
		if (under >= 0)
		{
			const Token& t = d.tokens[(size_t)under];
			if (t.kind == Tok::Comment) return items;
			if (t.kind == Tok::String && cur.offset > t.begin && cur.offset < t.end + (d.text[t.end - 1] == d.text[t.begin] && t.end - t.begin > 1 ? 0u : 1u))
			{
				// inside a string: module names after require
				const std::vector<int> code = CodeTokens(d);
				int pos = -1;
				for (size_t i = 0; i < code.size(); ++i)
					if (code[i] == under) pos = (int)i;
				bool isRequire = false;
				if (pos >= 1)
				{
					const std::string_view p1 = d.Text(d.tokens[(size_t)code[(size_t)pos - 1]]);
					if (p1 == "require") isRequire = true;
					else if (p1 == "(" && pos >= 2 && d.Text(d.tokens[(size_t)code[(size_t)pos - 2]]) == "require") isRequire = true;
				}
				if (!isRequire) return items;
				for (const std::string& m : ws_.ModuleNames())
				{
					Json it = Json::MakeObject();
					it["label"] = m;
					it["kind"] = 9;
					Json edit = Json::MakeObject();
					edit["range"] = MakeRange(d, t.line, t.col + 1, cur.line, cur.byteCol);
					edit["newText"] = m;
					it["textEdit"] = edit;
					items.Push(std::move(it));
				}
				return items;
			}
		}

		// find the token before the identifier being typed
		const std::vector<int> code = CodeTokens(d);
		int prefixTok = -1;
		if (under >= 0 && d.tokens[(size_t)under].kind == Tok::Name && d.tokens[(size_t)under].end == cur.offset) prefixTok = under;
		int before = -1; // position in `code` of the last token before the prefix/cursor
		for (size_t i = 0; i < code.size(); ++i)
		{
			const Token& t = d.tokens[(size_t)code[i]];
			if (prefixTok >= 0 ? code[i] < prefixTok : t.end <= cur.offset) before = (int)i;
			else break;
		}
		const std::string prefix = prefixTok >= 0 ? std::string(d.Text(d.tokens[(size_t)prefixTok])) : std::string{};
		(void)prefix;

		if (before >= 0)
		{
			const Token& bt = d.tokens[(size_t)code[(size_t)before]];
			const std::string_view bs = d.Text(bt);
			if (bt.kind == Tok::Op && (bs == "." || bs == ":") && before >= 1)
			{
				const int start = ChainStart(d, code, before - 1);
				const std::string type = ws_.TypeOfChain(d, code[(size_t)start], code[(size_t)before - 1]);
				for (const Member& m : ws_.MembersOf(type, bs == ":", uri))
					items.Push(MemberItem(m, !nextIsParen, ws_));
				return items;
			}
			if (bt.kind == Tok::Op && bs == "..") return items;
			// no completion right after a number or a closing bracket
			if (prefixTok < 0 && (bt.kind == Tok::Number || (bt.kind == Tok::Op && (bs == ")" || bs == "]" || bs == "}")))) return items;
		}

		// general identifiers
		std::set<std::string> seen;
		const int tokForScope = prefixTok >= 0 ? prefixTok : (before >= 0 ? code[(size_t)before] + 1 : 0);
		for (const Symbol& s : ws_.VisibleSymbols(d, tokForScope))
		{
			if (!seen.insert(s.name).second) continue;
			Json it = Json::MakeObject();
			it["label"] = s.name;
			it["kind"] = s.isFunction ? 3 : s.kind == SymKind::Param ? 6 : 6;
			const std::string type = ws_.TypeOfSymbol(d, (int)(&s - d.symbols.data()));
			std::string detail = s.isFunction ? s.signature() : (type.empty() ? s.name : s.name + ": " + Workspace::DisplayType(type));
			it["detail"] = detail;
			if (!s.ann.description.empty())
			{
				Json doc = Json::MakeObject();
				doc["kind"] = "markdown";
				doc["value"] = s.ann.description;
				it["documentation"] = doc;
			}
			it["sortText"] = "0" + s.name;
			items.Push(std::move(it));
		}
		for (const Member& m : ws_.Globals())
		{
			if (!seen.insert(m.name).second) continue;
			Json it = MemberItem(m, !nextIsParen, ws_);
			it["sortText"] = "1" + m.name;
			items.Push(std::move(it));
		}
		for (const char* k : kKeywords)
		{
			Json it = Json::MakeObject();
			it["label"] = k;
			it["kind"] = 14;
			it["sortText"] = std::string("2") + k;
			items.Push(std::move(it));
		}
		for (const Snippet& sn : kSnippets)
		{
			Json it = Json::MakeObject();
			it["label"] = sn.label;
			it["kind"] = 15;
			it["insertText"] = sn.text;
			it["insertTextFormat"] = 2;
			it["detail"] = sn.doc;
			it["sortText"] = std::string("3") + sn.label;
			items.Push(std::move(it));
		}
		return items;
	}

	// =============================================================================================
	// hover / definition
	// =============================================================================================
	namespace
	{
		struct Target
		{
			enum Kind { None, Local, DefTarget, Namespace, Module, Typed } kind = None;
			int symbol = -1;
			const Def* def = nullptr;
			std::string name;
			std::string type;
			std::string modulePath;
			int tok = -1;
		};

		Target ResolveAt(const Workspace& ws, const DocInfo& d, int tok)
		{
			Target t;
			t.tok = tok;
			const Token& tk = d.tokens[(size_t)tok];
			const std::vector<int> code = CodeTokens(d);
			int pos = -1;
			for (size_t i = 0; i < code.size(); ++i)
				if (code[i] == tok) { pos = (int)i; break; }
			if (pos < 0) return t;
			if (tk.kind == Tok::String)
			{
				if (pos >= 1)
				{
					const std::string_view p1 = d.Text(d.tokens[(size_t)code[(size_t)pos - 1]]);
					const bool req = p1 == "require" || (p1 == "(" && pos >= 2 && d.Text(d.tokens[(size_t)code[(size_t)pos - 2]]) == "require");
					if (req)
					{
						std::string_view lit = d.Text(tk);
						if (lit.size() >= 2)
						{
							t.name = std::string(lit.substr(1, lit.size() - 2));
							t.modulePath = ws.ResolveModule(t.name, d.uri);
							if (!t.modulePath.empty()) t.kind = Target::Module;
						}
					}
				}
				return t;
			}
			if (tk.kind != Tok::Name) return t;
			t.name = std::string(d.Text(tk));
			// member access?
			if (pos >= 2)
			{
				const Token& sep = d.tokens[(size_t)code[(size_t)pos - 1]];
				const std::string_view ss = d.Text(sep);
				if (sep.kind == Tok::Op && (ss == "." || ss == ":"))
				{
					const int start = ChainStart(d, code, pos - 2);
					const std::string type = ws.TypeOfChain(d, code[(size_t)start], code[(size_t)pos - 2]);
					for (const Member& m : ws.MembersOf(type, false, d.uri))
						if (m.name == t.name)
						{
							t.def = m.def;
							t.type = m.type;
							t.kind = m.def ? Target::DefTarget : Target::Typed;
							return t;
						}
					return t;
				}
			}
			int si = -1;
			if (const Symbol* s = ws.Resolve(d, tok, &si))
			{
				(void)s;
				t.kind = Target::Local;
				t.symbol = si;
				return t;
			}
			if (const Def* def = ws.FindDef(t.name))
			{
				t.kind = Target::DefTarget;
				t.def = def;
				t.type = ws.TypeOfDef(*def);
				return t;
			}
			if (ws.HasNamespace(t.name))
			{
				t.kind = Target::Namespace;
				t.type = "ns:" + t.name;
			}
			return t;
		}
	}

	Json Server::Hover(const Json& params)
	{
		const std::string uri = params.Get("textDocument").GetString("uri");
		const DocInfo* dp = ws_.Find(uri);
		if (!dp) return Json();
		const DocInfo& d = *dp;
		const Cursor cur = ReadCursor(d, params);
		const int tok = d.TokenAt(cur.line, cur.byteCol);
		if (tok < 0) return Json();
		const Target t = ResolveAt(ws_, d, tok);
		std::string md;
		switch (t.kind)
		{
		case Target::Local:
		{
			const Symbol& s = d.symbols[(size_t)t.symbol];
			const std::string type = ws_.TypeOfSymbol(d, t.symbol);
			Symbol shown = s;
			if (!s.isFunction && shown.type.empty() && !type.empty()) shown.type = Workspace::DisplayType(type);
			if (s.kind == SymKind::Param) md = Markdown("(parameter) " + s.name + (type.empty() ? "" : ": " + Workspace::DisplayType(type)), s.ann.description);
			else md = Markdown(shown.signature(), s.ann.description);
			break;
		}
		case Target::DefTarget:
			md = DefMarkdown(*t.def);
			if (t.def->kind == Def::Field || t.def->kind == Def::Table)
			{
				const std::string ty = ws_.TypeOfDef(*t.def);
				if (t.def->ann.type.empty() && !ty.empty()) md = Markdown("(field) " + t.def->path + ": " + Workspace::DisplayType(ty), t.def->ann.description);
			}
			break;
		case Target::Namespace: md = Markdown(t.name + ": table", ""); break;
		case Target::Module: md = Markdown("module \"" + t.name + "\"", t.modulePath); break;
		case Target::Typed: md = Markdown("(field) " + t.name + ": " + Workspace::DisplayType(t.type), ""); break;
		default: return Json();
		}
		Json h = Json::MakeObject();
		Json c = Json::MakeObject();
		c["kind"] = "markdown";
		c["value"] = md;
		h["contents"] = c;
		h["range"] = TokRange(d, tok);
		return h;
	}

	Json Server::Definition(const Json& params)
	{
		const std::string uri = params.Get("textDocument").GetString("uri");
		const DocInfo* dp = ws_.Find(uri);
		if (!dp) return Json();
		const DocInfo& d = *dp;
		const Cursor cur = ReadCursor(d, params);
		const int tok = d.TokenAt(cur.line, cur.byteCol);
		if (tok < 0) return Json();
		const Target t = ResolveAt(ws_, d, tok);
		if (t.kind == Target::Local) return Location(d, d.symbols[(size_t)t.symbol].tok);
		if (t.kind == Target::DefTarget && t.def)
		{
			if (const DocInfo* od = ws_.Find(t.def->uri)) return Location(*od, t.def->tok);
		}
		if (t.kind == Target::Module)
		{
			Json l = Json::MakeObject();
			l["uri"] = PathToUri(t.modulePath);
			Json r = Json::MakeObject();
			Json z = Json::MakeObject();
			z["line"] = 0;
			z["character"] = 0;
			r["start"] = z;
			r["end"] = z;
			l["range"] = r;
			return l;
		}
		return Json();
	}

	// =============================================================================================
	// references / rename / highlight
	// =============================================================================================
	Json Server::References(const Json& params)
	{
		Json out = Json::MakeArray();
		const std::string uri = params.Get("textDocument").GetString("uri");
		const DocInfo* dp = ws_.Find(uri);
		if (!dp) return out;
		const DocInfo& d = *dp;
		const Cursor cur = ReadCursor(d, params);
		const int tok = d.TokenAt(cur.line, cur.byteCol);
		if (tok < 0) return out;
		const bool includeDecl = params.Get("context").GetBool("includeDeclaration", true);
		const Target t = ResolveAt(ws_, d, tok);
		if (t.kind == Target::Local)
		{
			if (includeDecl) out.Push(Location(d, d.symbols[(size_t)t.symbol].tok));
			for (const Ref& r : d.refs)
				if (r.symbol == t.symbol) out.Push(Location(d, r.tok));
			return out;
		}
		if (t.name.empty()) return out;
		// global / member: every non-local occurrence by name
		for (const DocInfo* od : ws_.Docs())
		{
			std::set<int> localToks;
			for (const Symbol& s : od->symbols) localToks.insert(s.tok);
			for (const Ref& r : od->refs)
				if (r.symbol >= 0) localToks.insert(r.tok);
			for (size_t i = 0; i < od->tokens.size(); ++i)
			{
				const Token& tk = od->tokens[i];
				if (tk.kind == Tok::Name && od->Text(tk) == t.name && !localToks.count((int)i)) out.Push(Location(*od, (int)i));
			}
		}
		return out;
	}

	Json Server::DocumentHighlight(const Json& params)
	{
		Json out = Json::MakeArray();
		const std::string uri = params.Get("textDocument").GetString("uri");
		const DocInfo* dp = ws_.Find(uri);
		if (!dp) return out;
		const DocInfo& d = *dp;
		const Cursor cur = ReadCursor(d, params);
		const int tok = d.TokenAt(cur.line, cur.byteCol);
		if (tok < 0) return out;
		int si = -1;
		if (!ws_.Resolve(d, tok, &si) || si < 0) return out;
		auto add = [&](int tk, int kind) {
			Json h = Json::MakeObject();
			h["range"] = TokRange(d, tk);
			h["kind"] = kind;
			out.Push(std::move(h));
		};
		add(d.symbols[(size_t)si].tok, 3);
		for (const Ref& r : d.refs)
			if (r.symbol == si) add(r.tok, r.write ? 3 : 2);
		return out;
	}

	Json Server::PrepareRename(const Json& params)
	{
		const std::string uri = params.Get("textDocument").GetString("uri");
		const DocInfo* dp = ws_.Find(uri);
		if (!dp) return Json();
		const DocInfo& d = *dp;
		const Cursor cur = ReadCursor(d, params);
		const int tok = d.TokenAt(cur.line, cur.byteCol);
		if (tok < 0) return Json();
		const Target t = ResolveAt(ws_, d, tok);
		if (t.kind != Target::Local) return Json();
		Json r = Json::MakeObject();
		r["range"] = TokRange(d, tok);
		r["placeholder"] = t.name;
		return r;
	}

	Json Server::Rename(const Json& params)
	{
		const std::string uri = params.Get("textDocument").GetString("uri");
		const DocInfo* dp = ws_.Find(uri);
		if (!dp) return Json();
		const DocInfo& d = *dp;
		const Cursor cur = ReadCursor(d, params);
		const int tok = d.TokenAt(cur.line, cur.byteCol);
		const std::string newName = params.GetString("newName");
		if (tok < 0 || newName.empty()) return Json();
		const Target t = ResolveAt(ws_, d, tok);
		if (t.kind != Target::Local) return Json();
		// the new name must be a valid identifier
		bool valid = !newName.empty() && (std::isalpha((unsigned char)newName[0]) || newName[0] == '_') && !IsKeyword(newName);
		for (char c : newName)
			if (!(std::isalnum((unsigned char)c) || c == '_')) valid = false;
		if (!valid) return Json();
		Json edits = Json::MakeArray();
		auto add = [&](int tk) {
			Json e = Json::MakeObject();
			e["range"] = TokRange(d, tk);
			e["newText"] = newName;
			edits.Push(std::move(e));
		};
		add(d.symbols[(size_t)t.symbol].tok);
		for (const Ref& r : d.refs)
			if (r.symbol == t.symbol) add(r.tok);
		Json changes = Json::MakeObject();
		changes[uri] = edits;
		Json we = Json::MakeObject();
		we["changes"] = changes;
		return we;
	}

	// =============================================================================================
	// signature help
	// =============================================================================================
	Json Server::SignatureHelp(const Json& params)
	{
		const std::string uri = params.Get("textDocument").GetString("uri");
		const DocInfo* dp = ws_.Find(uri);
		if (!dp) return Json();
		const DocInfo& d = *dp;
		const Cursor cur = ReadCursor(d, params);
		const std::vector<int> code = CodeTokens(d);
		// walk back to the unmatched '('
		int depth = 0, commas = 0, open = -1;
		for (int i = (int)code.size() - 1; i >= 0; --i)
		{
			const Token& t = d.tokens[(size_t)code[(size_t)i]];
			if (t.begin >= cur.offset) continue;
			if (t.kind != Tok::Op) continue;
			const std::string_view s = d.Text(t);
			if (s == ")" || s == "]" || s == "}") ++depth;
			else if (s == "(" || s == "[" || s == "{")
			{
				if (depth == 0)
				{
					if (s == "(") { open = i; break; }
					return Json(); // inside a table / index
				}
				--depth;
			}
			else if (s == "," && depth == 0)
				++commas;
		}
		if (open < 1) return Json();
		const int start = ChainStart(d, code, open - 1);
		const std::string callee = ws_.TypeOfChain(d, code[(size_t)start], code[(size_t)open - 1]);
		std::string label, doc;
		std::vector<std::pair<std::string, std::string>> plist;
		if (callee.rfind("fn:", 0) == 0)
		{
			const Def* def = ws_.FindDef(callee.substr(3));
			if (!def) return Json();
			label = def->signature();
			doc = def->ann.description;
			for (const ParamInfo& p : def->params) plist.emplace_back(p.type.empty() ? p.name : p.name + ": " + p.type, p.doc);
			if (def->vararg) plist.emplace_back("...", "");
		}
		else if (callee.rfind("lfn:", 0) == 0)
		{
			const size_t hash = callee.rfind('#');
			const DocInfo* od = ws_.Find(callee.substr(4, hash - 4));
			if (!od) return Json();
			const Symbol& s = od->symbols[(size_t)std::atoi(callee.c_str() + hash + 1)];
			label = s.signature();
			doc = s.ann.description;
			for (const std::string& pn : s.paramNames)
			{
				std::string ty;
				for (const ParamInfo& p : s.ann.params)
					if (p.name == pn) ty = p.type;
				plist.emplace_back(ty.empty() ? pn : pn + ": " + ty, "");
			}
		}
		else
			return Json();
		Json sig = Json::MakeObject();
		sig["label"] = label;
		if (!doc.empty())
		{
			Json dj = Json::MakeObject();
			dj["kind"] = "markdown";
			dj["value"] = doc;
			sig["documentation"] = dj;
		}
		Json plabels = Json::MakeArray();
		for (const auto& [l, pd] : plist)
		{
			Json p = Json::MakeObject();
			p["label"] = l;
			if (!pd.empty()) p["documentation"] = pd;
			plabels.Push(std::move(p));
		}
		sig["parameters"] = plabels;
		Json res = Json::MakeObject();
		Json sigs = Json::MakeArray();
		sigs.Push(sig);
		res["signatures"] = sigs;
		res["activeSignature"] = 0;
		res["activeParameter"] = std::min(commas, std::max(0, (int)plist.size() - 1));
		return res;
	}

	// =============================================================================================
	// symbols
	// =============================================================================================
	Json Server::DocumentSymbols(const Json& params)
	{
		Json out = Json::MakeArray();
		const std::string uri = params.Get("textDocument").GetString("uri");
		const DocInfo* dp = ws_.Find(uri);
		if (!dp) return out;
		const DocInfo& d = *dp;
		auto add = [&](const std::string& name, int kind, int tok, const std::string& detail) {
			Json s = Json::MakeObject();
			s["name"] = name;
			s["kind"] = kind;
			if (!detail.empty()) s["detail"] = detail;
			s["range"] = TokRange(d, tok);
			s["selectionRange"] = TokRange(d, tok);
			out.Push(std::move(s));
		};
		for (const Def& def : d.defs)
		{
			std::string name = def.path;
			if (!name.empty() && name[0] == '~') name.erase(0, 1);
			const int kind = def.kind == Def::Method ? 6 : def.kind == Def::Function ? 12 : def.kind == Def::Class ? 5 : def.kind == Def::Table ? 2 : 8;
			add(name, kind, def.tok, "");
		}
		for (const Symbol& s : d.symbols)
		{
			if (s.kind == SymKind::LocalFunction) add(s.name, 12, s.tok, s.signature());
			else if (s.kind == SymKind::Local && s.visibleTo == INT_MAX) add(s.name, 13, s.tok, "");
		}
		return out;
	}

	Json Server::WorkspaceSymbols(const Json& params)
	{
		Json out = Json::MakeArray();
		const std::string q = str::ToLower(params.GetString("query"));
		int count = 0;
		for (const DocInfo* d : ws_.Docs())
		{
			if (d->isMeta) continue;
			for (const Def& def : d->defs)
			{
				std::string name = def.path;
				if (!name.empty() && name[0] == '~') name.erase(0, 1);
				if (!q.empty() && str::ToLower(name).find(q) == std::string::npos) continue;
				Json s = Json::MakeObject();
				s["name"] = name;
				s["kind"] = def.kind == Def::Method ? 6 : def.kind == Def::Function ? 12 : def.kind == Def::Class ? 5 : 13;
				s["location"] = Location(*d, def.tok);
				out.Push(std::move(s));
				if (++count >= 300) return out;
			}
		}
		return out;
	}

	// =============================================================================================
	// transport
	// =============================================================================================
	namespace
	{
		bool ReadFramed(const std::function<int()>& getc, std::string& body)
		{
			size_t length = 0;
			bool haveLength = false;
			std::string line;
			while (true)
			{
				line.clear();
				while (true)
				{
					const int c = getc();
					if (c < 0) return false;
					if (c == '\n') break;
					if (c != '\r') line += (char)c;
				}
				if (line.empty())
				{
					if (haveLength) break;
					continue;
				}
				const size_t colon = line.find(':');
				if (colon != std::string::npos && str::ToLower(line.substr(0, colon)) == "content-length")
				{
					length = (size_t)std::strtoull(line.c_str() + colon + 1, nullptr, 10);
					haveLength = true;
				}
			}
			if (length > 256u * 1024 * 1024) return false;
			body.resize(length);
			for (size_t i = 0; i < length; ++i)
			{
				const int c = getc();
				if (c < 0) return false;
				body[i] = (char)c;
			}
			return true;
		}

		std::string Frame(const Json& message)
		{
			const std::string body = message.Dump();
			return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
		}
	}

	bool ReadMessage(FILE* in, std::string& body)
	{
		return ReadFramed([&]() { return fgetc(in); }, body);
	}

	void WriteMessage(FILE* out, const Json& message)
	{
		const std::string f = Frame(message);
		fwrite(f.data(), 1, f.size(), out);
		fflush(out);
	}

	int RunStdio(Server& server)
	{
		std::string body;
		while (!server.ShouldExit() && ReadMessage(stdin, body))
		{
			Json msg;
			if (!Json::Parse(body, msg)) continue;
			for (const Json& r : server.Handle(msg)) WriteMessage(stdout, r);
		}
		return server.ExitCode();
	}

	int RunTcp(Server& server, int port, const std::atomic<bool>* stop)
	{
#if defined(_WIN32)
		WSADATA wsa;
		WSAStartup(MAKEWORD(2, 2), &wsa);
		using Sock = SOCKET;
		const Sock invalid = INVALID_SOCKET;
		auto closeSock = [](Sock s) { closesocket(s); };
#else
		using Sock = int;
		const Sock invalid = -1;
		auto closeSock = [](Sock s) { close(s); };
#endif
		Sock ls = socket(AF_INET, SOCK_STREAM, 0);
		if (ls == invalid) return 2;
		int yes = 1;
		setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		addr.sin_port = htons((uint16_t)port);
		if (bind(ls, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(ls, 1) != 0)
		{
			closeSock(ls);
			return 2;
		}
		auto setTimeout = [](Sock s, int ms) {
#if defined(_WIN32)
			DWORD t = (DWORD)ms;
			setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&t, sizeof(t));
#else
			timeval tv{ms / 1000, (ms % 1000) * 1000};
			setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
		};
		setTimeout(ls, 250);
		while (!(stop && stop->load()) && !server.ShouldExit())
		{
			Sock cs = accept(ls, nullptr, nullptr);
			if (cs == invalid) continue; // timeout
			setTimeout(cs, 250);
			std::string buffer;
			size_t bpos = 0;
			bool eof = false;
			auto getc = [&]() -> int {
				while (true)
				{
					if (bpos < buffer.size()) return (unsigned char)buffer[bpos++];
					char tmp[4096];
					const int n = (int)recv(cs, tmp, sizeof(tmp), 0);
					if (n > 0)
					{
						buffer.assign(tmp, (size_t)n);
						bpos = 0;
						continue;
					}
					if (n == 0)
					{
						eof = true;
						return -1;
					}
#if defined(_WIN32)
					const bool timeout = WSAGetLastError() == WSAETIMEDOUT;
#else
					const bool timeout = errno == EAGAIN || errno == EWOULDBLOCK;
#endif
					if (timeout && !(stop && stop->load())) continue;
					eof = true;
					return -1;
				}
			};
			std::string body;
			while (!eof && ReadFramed(getc, body))
			{
				Json msg;
				if (!Json::Parse(body, msg)) continue;
				for (const Json& r : server.Handle(msg))
				{
					const std::string f = Frame(r);
					size_t sent = 0;
					while (sent < f.size())
					{
						const int n = (int)send(cs, f.data() + sent, (int)(f.size() - sent), 0);
						if (n <= 0) break;
						sent += (size_t)n;
					}
				}
				if (server.ShouldExit()) break;
			}
			closeSock(cs);
		}
		closeSock(ls);
		return 0;
	}

} // namespace lumen::lsp
