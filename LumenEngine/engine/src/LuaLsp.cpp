#include "LuaLsp.h"
#include "lumen/core/Fs.h"
#include "lumen/core/Strings.h"

namespace lumen
{
	namespace
	{
		Json Pos(int line, int col)
		{
			Json p = Json::MakeObject();
			p["line"] = line;
			p["character"] = col;
			return p;
		}
		Json DocPos(const std::string& uri, int line, int col)
		{
			Json p = Json::MakeObject();
			Json td = Json::MakeObject();
			td["uri"] = uri;
			p["textDocument"] = td;
			p["position"] = Pos(line, col);
			return p;
		}
		std::string MarkupText(const Json& v)
		{
			if (v.IsString()) return v.AsString();
			if (v.IsObject()) return v.GetString("value");
			return {};
		}
	}

	std::string EditorLsp::ToUri(const std::string& absPath)
	{
		std::string p = fs::Normalize(absPath);
		std::string out = "file://";
		if (p.empty() || p[0] != '/') out += '/';
		for (unsigned char c : p)
		{
			if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~' || c == ':') out += (char)c;
			else { char b[4]; std::snprintf(b, sizeof b, "%%%02X", c); out += b; }
		}
		return out;
	}

	std::string EditorLsp::FromUri(const std::string& uri)
	{
		std::string s = uri.rfind("file://", 0) == 0 ? uri.substr(7) : uri;
		std::string out;
		for (size_t i = 0; i < s.size(); ++i)
		{
			if (s[i] == '%' && i + 2 < s.size() + 0 && std::isxdigit((unsigned char)s[i + 1]) && std::isxdigit((unsigned char)s[i + 2]))
			{
				out += (char)std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
				i += 2;
			}
			else out += s[i];
		}
		if (out.size() > 2 && out[0] == '/' && out[2] == ':') out.erase(0, 1); // /C:/x
		return out;
	}

	void EditorLsp::Init(const std::string& apiDir, const std::vector<std::string>& moduleDirs)
	{
		std::vector<std::string> api;
		if (!apiDir.empty() && fs::IsDirectory(apiDir)) api.push_back(apiDir);
		server_ = std::make_unique<lsp::Server>(api, moduleDirs);
		Json p = Json::MakeObject();
		p["processId"] = nullptr;
		p["capabilities"] = Json::MakeObject();
		Request("initialize", p);
		Json n = Json::MakeObject();
		n["jsonrpc"] = "2.0";
		n["method"] = "initialized";
		n["params"] = Json::MakeObject();
		server_->Handle(n);
	}

	void EditorLsp::Consume(const std::vector<Json>& out)
	{
		for (const Json& m : out)
		{
			if (m.GetString("method") != "textDocument/publishDiagnostics") continue;
			const Json& p = m.Get("params");
			std::vector<LuaDiag> list;
			for (const Json& d : p.Get("diagnostics").Items())
			{
				LuaDiag x;
				x.line = (int)d.Get("range").Get("start").GetInt("line") + 1;
				x.column = (int)d.Get("range").Get("start").GetInt("character");
				x.message = d.GetString("message");
				x.error = d.GetInt("severity", 1) == 1;
				list.push_back(std::move(x));
			}
			diags_[p.GetString("uri")] = std::move(list);
		}
	}

	Json EditorLsp::Request(const char* method, Json params)
	{
		if (!server_) return Json();
		Json m = Json::MakeObject();
		m["jsonrpc"] = "2.0";
		const int id = nextId_++;
		m["id"] = id;
		m["method"] = method;
		m["params"] = std::move(params);
		const std::vector<Json> out = server_->Handle(m);
		Json result;
		for (const Json& r : out)
		{
			if (r.Contains("id") && r.Get("id").AsInt(-1) == id) result = r.Get("result");
		}
		Consume(out);
		return result;
	}

	void EditorLsp::Sync(const std::string& absPath, const std::string& text)
	{
		if (!server_) return;
		const std::string uri = ToUri(absPath);
		auto it = docs_.find(uri);
		Json n = Json::MakeObject();
		n["jsonrpc"] = "2.0";
		Json params = Json::MakeObject();
		Json td = Json::MakeObject();
		td["uri"] = uri;
		if (it == docs_.end())
		{
			td["version"] = 1;
			td["languageId"] = "lua";
			td["text"] = text;
			params["textDocument"] = td;
			n["method"] = "textDocument/didOpen";
			docs_[uri] = {text, 1};
		}
		else
		{
			if (it->second.text == text) return;
			it->second.text = text;
			td["version"] = ++it->second.version;
			params["textDocument"] = td;
			Json change = Json::MakeObject();
			change["text"] = text;
			Json changes = Json::MakeArray();
			changes.Push(change);
			params["contentChanges"] = changes;
			n["method"] = "textDocument/didChange";
		}
		n["params"] = params;
		Consume(server_->Handle(n));
	}

	void EditorLsp::Close(const std::string& absPath)
	{
		if (!server_) return;
		const std::string uri = ToUri(absPath);
		if (!docs_.erase(uri)) return;
		diags_.erase(uri);
		Json n = Json::MakeObject();
		n["jsonrpc"] = "2.0";
		n["method"] = "textDocument/didClose";
		Json params = Json::MakeObject();
		Json td = Json::MakeObject();
		td["uri"] = uri;
		params["textDocument"] = td;
		n["params"] = params;
		Consume(server_->Handle(n));
	}

	std::vector<LuaDiag> EditorLsp::Diagnostics(const std::string& absPath) const
	{
		auto it = diags_.find(ToUri(absPath));
		return it == diags_.end() ? std::vector<LuaDiag>{} : it->second;
	}

	std::vector<LuaCompletion> EditorLsp::Complete(const std::string& absPath, int line, int col)
	{
		std::vector<LuaCompletion> out;
		Json r = Request("textDocument/completion", DocPos(ToUri(absPath), line, col));
		const Json& items = r.IsObject() ? r.Get("items") : r;
		for (const Json& it : items.Items())
		{
			LuaCompletion c;
			c.label = it.GetString("label");
			c.detail = it.GetString("detail");
			c.doc = MarkupText(it.Get("documentation"));
			c.kind = (int)it.GetInt("kind");
			if (!c.label.empty()) out.push_back(std::move(c));
		}
		return out;
	}

	std::string EditorLsp::Hover(const std::string& absPath, int line, int col)
	{
		Json r = Request("textDocument/hover", DocPos(ToUri(absPath), line, col));
		if (!r.IsObject()) return {};
		const Json& c = r.Get("contents");
		if (c.IsArray())
		{
			std::string s;
			for (const Json& p : c.Items()) s += MarkupText(p) + "\n";
			return s;
		}
		return MarkupText(c);
	}

	std::string EditorLsp::Signature(const std::string& absPath, int line, int col)
	{
		Json r = Request("textDocument/signatureHelp", DocPos(ToUri(absPath), line, col));
		if (!r.IsObject()) return {};
		const Json& sigs = r.Get("signatures");
		if (sigs.Empty()) return {};
		return sigs.At((size_t)std::max<int64_t>(0, r.GetInt("activeSignature"))).GetString("label");
	}

	EditorLsp::Location EditorLsp::Definition(const std::string& absPath, int line, int col)
	{
		Location loc;
		Json r = Request("textDocument/definition", DocPos(ToUri(absPath), line, col));
		const Json& d = r.IsArray() ? r.At(0) : r;
		if (!d.IsObject()) return loc;
		loc.path = FromUri(d.GetString("uri"));
		loc.line = (int)d.Get("range").Get("start").GetInt("line");
		loc.column = (int)d.Get("range").Get("start").GetInt("character");
		loc.found = !loc.path.empty();
		return loc;
	}
}
