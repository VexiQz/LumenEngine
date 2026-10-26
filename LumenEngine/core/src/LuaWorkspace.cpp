#include "lumen/core/LuaAnalysis.h"

#include "lumen/core/Fs.h"
#include "lumen/core/LuaCompiler.h"
#include "lumen/core/Strings.h"

#include <algorithm>
#include <climits>

namespace lumen::lua
{
	// =============================================================================================
	// URIs
	// =============================================================================================
	std::string PathToUri(const std::string& path)
	{
		std::string p = fs::Normalize(path);
		std::string out = "file://";
		if (!p.empty() && p[0] != '/') out += '/';
		static const char* hex = "0123456789ABCDEF";
		for (unsigned char c : p)
		{
			if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~' || c == ':') out += (char)c;
			else
			{
				out += '%';
				out += hex[c >> 4];
				out += hex[c & 15];
			}
		}
		return out;
	}

	std::string UriToPath(const std::string& uri)
	{
		if (uri.compare(0, 7, "file://") != 0) return uri;
		std::string out;
		for (size_t i = 7; i < uri.size(); ++i)
		{
			if (uri[i] == '%' && i + 2 < uri.size() + 0 && std::isxdigit((unsigned char)uri[i + 1]) && std::isxdigit((unsigned char)uri[i + 2]))
			{
				out += (char)std::stoi(uri.substr(i + 1, 2), nullptr, 16);
				i += 2;
			}
			else
				out += uri[i];
		}
		// "/C:/x" -> "C:/x"
		if (out.size() >= 3 && out[0] == '/' && std::isalpha((unsigned char)out[1]) && out[2] == ':') out.erase(0, 1);
		return out;
	}

	namespace
	{
		bool HasPrefixType(const std::string& t)
		{
			for (const char* p : {"cls:", "ns:", "self:", "lns:", "mod:", "fn:", "lfn:"})
				if (t.rfind(p, 0) == 0) return true;
			return false;
		}

		bool IsIgnoredDir(const std::string& name)
		{
			return name == ".git" || name == ".lumen" || name == "build" || name == "node_modules" || name == ".vscode";
		}
	}

	// =============================================================================================
	// Workspace storage
	// =============================================================================================
	void Workspace::SetRoots(std::string projectRoot, std::vector<std::string> moduleDirs, std::vector<std::string> apiDirs)
	{
		projectRoot_ = fs::Normalize(projectRoot);
		moduleDirs_ = std::move(moduleDirs);
		apiDirs_ = std::move(apiDirs);
	}

	DocInfo* Workspace::Load(const std::string& path) const
	{
		const std::string uri = PathToUri(path);
		auto op = open_.find(uri);
		if (op != open_.end()) return op->second.get();
		const uint64_t mt = fs::LastWriteTimeMs(path);
		auto it = disk_.find(uri);
		if (it != disk_.end() && mtimes_[uri] == mt) return it->second.get();
		std::string text;
		if (!fs::ReadText(path, text)) return nullptr;
		auto doc = std::make_unique<DocInfo>();
		Analyze(*doc, uri, std::move(text));
		DocInfo* raw = doc.get();
		disk_[uri] = std::move(doc);
		mtimes_[uri] = mt;
		return raw;
	}

	void Workspace::Scan()
	{
		auto scanDir = [&](const std::string& dir) {
			if (!fs::IsDirectory(dir)) return;
			fs::ListOptions lo;
			lo.recursive = true;
			lo.ignore = {".git", ".lumen", "build", "node_modules", ".vscode"};
			int count = 0;
			fs::Walk(dir, lo, [&](const fs::DirEntry& e) {
				if (e.isDirectory) return true;
				if (fs::ExtensionLower(e.path) != ".lua" || e.size > 2u * 1024 * 1024) return true;
				Load(e.path);
				return ++count < 4000;
			});
		};
		for (const std::string& d : apiDirs_) scanDir(d);
		for (const std::string& d : moduleDirs_) scanDir(d);
		if (!projectRoot_.empty()) scanDir(projectRoot_);
		Rebuild();
	}

	DocInfo& Workspace::Open(const std::string& uri, std::string text, int version)
	{
		auto doc = std::make_unique<DocInfo>();
		Analyze(*doc, uri, std::move(text));
		doc->version = version;
		DocInfo* raw = doc.get();
		open_[uri] = std::move(doc);
		Rebuild();
		return *raw;
	}

	void Workspace::Close(const std::string& uri)
	{
		open_.erase(uri);
		disk_.erase(uri);
		Rebuild();
	}

	DocInfo* Workspace::Find(const std::string& uri)
	{
		auto it = open_.find(uri);
		if (it != open_.end()) return it->second.get();
		auto d = disk_.find(uri);
		return d != disk_.end() ? d->second.get() : nullptr;
	}

	const DocInfo* Workspace::Find(const std::string& uri) const
	{
		auto it = open_.find(uri);
		if (it != open_.end()) return it->second.get();
		auto d = disk_.find(uri);
		return d != disk_.end() ? d->second.get() : nullptr;
	}

	std::vector<const DocInfo*> Workspace::Docs() const
	{
		std::vector<const DocInfo*> out;
		for (const auto& [uri, d] : open_) out.push_back(d.get());
		for (const auto& [uri, d] : disk_)
			if (!open_.count(uri)) out.push_back(d.get());
		return out;
	}

	void Workspace::Rebuild()
	{
		defIndex_.clear();
		namespaces_.clear();
		classes_.clear();
		classBases_.clear();
		userGlobals_.clear();
		for (const DocInfo* d : Docs())
		{
			for (size_t i = 0; i < d->defs.size(); ++i)
			{
				const std::string& p = d->defs[i].path;
				if (p.empty() || p[0] == '~') continue;
				defIndex_.emplace(p, std::make_pair(d, (int)i));
				for (size_t k = 0; k < p.size(); ++k)
					if (p[k] == '.' || p[k] == ':') namespaces_.insert(p.substr(0, k));
			}
			for (const std::string& c : d->classes) classes_.insert(c);
			for (const auto& [c, b] : d->classBases) classBases_[c] = b;
			if (!d->isMeta)
				for (const std::string& g : d->assignedGlobals) userGlobals_.insert(g);
		}
	}

	std::string Workspace::ResolveModule(const std::string& name, const std::string& fromUri) const
	{
		if (name.empty()) return {};
		std::string rel = name;
		std::replace(rel.begin(), rel.end(), '.', '/');
		std::vector<std::string> dirs;
		if (!fromUri.empty()) dirs.push_back(fs::Parent(UriToPath(fromUri)));
		if (!projectRoot_.empty())
		{
			dirs.push_back(projectRoot_);
			dirs.push_back(fs::Join(projectRoot_, "scripts"));
		}
		for (const std::string& d : moduleDirs_) dirs.push_back(d);
		for (const std::string& d : dirs)
		{
			for (const std::string& c : {fs::Join(d, rel + ".lua"), fs::Join(d, rel, "init.lua")})
				if (fs::IsFile(c)) return c;
		}
		return {};
	}

	std::vector<std::string> Workspace::ModuleNames() const
	{
		std::set<std::string> names;
		std::vector<std::string> roots = moduleDirs_;
		if (!projectRoot_.empty())
		{
			roots.push_back(projectRoot_);
			roots.push_back(fs::Join(projectRoot_, "scripts"));
		}
		for (const std::string& r : roots)
		{
			if (!fs::IsDirectory(r)) continue;
			fs::ListOptions lo;
			lo.recursive = true;
			lo.ignore = {".git", ".lumen", "build", "node_modules", "api"};
			fs::Walk(r, lo, [&](const fs::DirEntry& e) {
				if (e.isDirectory || fs::ExtensionLower(e.path) != ".lua") return true;
				std::string rel = fs::Relative(e.path, r);
				rel = rel.substr(0, rel.size() - 4);
				if (rel.size() > 5 && rel.compare(rel.size() - 5, 5, "/init") == 0) rel.resize(rel.size() - 5);
				std::replace(rel.begin(), rel.end(), '/', '.');
				names.insert(rel);
				return names.size() < 5000;
			});
		}
		return {names.begin(), names.end()};
	}

	// =============================================================================================
	// Definitions
	// =============================================================================================
	const Def* Workspace::FindDef(const std::string& path) const
	{
		auto it = defIndex_.find(path);
		if (it == defIndex_.end()) return nullptr;
		// prefer an annotated definition
		const Def* best = nullptr;
		auto range = defIndex_.equal_range(path);
		for (auto r = range.first; r != range.second; ++r)
		{
			const Def& d = r->second.first->defs[(size_t)r->second.second];
			if (!best || (best->ann.empty() && !d.ann.empty())) best = &d;
		}
		return best;
	}

	std::vector<const Def*> Workspace::DefsWithPrefix(const std::string& prefix) const
	{
		std::vector<const Def*> out;
		for (auto it = defIndex_.lower_bound(prefix); it != defIndex_.end() && it->first.compare(0, prefix.size(), prefix) == 0; ++it)
			out.push_back(&it->second.first->defs[(size_t)it->second.second]);
		return out;
	}

	bool Workspace::HasNamespace(const std::string& path) const { return namespaces_.count(path) != 0; }
	bool Workspace::IsClass(const std::string& name) const { return classes_.count(name) != 0; }
	std::string Workspace::ClassBase(const std::string& name) const
	{
		auto it = classBases_.find(name);
		return it == classBases_.end() ? std::string{} : it->second;
	}

	// =============================================================================================
	// Types
	// =============================================================================================
	std::string Workspace::DisplayType(const std::string& t)
	{
		if (t.rfind("cls:", 0) == 0) return t.substr(4);
		if (t.rfind("ns:", 0) == 0) return "table";
		if (t.rfind("lns:", 0) == 0 || t.rfind("mod:", 0) == 0 || t.rfind("self:", 0) == 0) return "table";
		if (t.rfind("fn:", 0) == 0 || t.rfind("lfn:", 0) == 0) return "function";
		return t;
	}

	std::string Workspace::TypeRef(const std::string& rawIn) const
	{
		std::string raw(str::Trim(rawIn));
		if (raw.empty()) return {};
		if (HasPrefixType(raw)) return raw;
		// unions: first alternative that is not nil
		{
			int depth = 0;
			size_t start = 0;
			std::vector<std::string> alts;
			for (size_t i = 0; i <= raw.size(); ++i)
			{
				const char c = i < raw.size() ? raw[i] : '|';
				if (c == '(' || c == '<' || c == '{') ++depth;
				else if (c == ')' || c == '>' || c == '}') --depth;
				else if (c == '|' && depth == 0)
				{
					alts.emplace_back(str::Trim(std::string_view(raw).substr(start, i - start)));
					start = i + 1;
				}
			}
			if (alts.size() > 1)
			{
				for (const std::string& a : alts)
					if (a != "nil") return TypeRef(a);
				return {};
			}
		}
		if (raw.back() == '?') raw.pop_back();
		if (raw.rfind("fun(", 0) == 0) return "function";
		if (raw.rfind("table<", 0) == 0 || raw[0] == '{') return "table";
		if (raw.size() > 2 && raw.compare(raw.size() - 2, 2, "[]") == 0)
		{
			const std::string inner = TypeRef(raw.substr(0, raw.size() - 2));
			return inner.empty() ? std::string{} : inner + "[]";
		}
		if (raw == "integer" || raw == "float") return "number";
		if (IsClass(raw)) return "cls:" + raw;
		return raw;
	}

	std::string Workspace::ElementType(const std::string& t) const
	{
		if (t.size() > 2 && t.compare(t.size() - 2, 2, "[]") == 0) return t.substr(0, t.size() - 2);
		return {};
	}

	std::string Workspace::TypeOfDef(const Def& def, int depth) const
	{
		if (depth > 8) return {};
		if (def.kind == Def::Function || def.kind == Def::Method) return "fn:" + def.path;
		if (def.kind == Def::Class) return "cls:" + def.path;
		if (!def.ann.type.empty()) return TypeRef(def.ann.type);
		if (!def.type.empty()) return TypeRef(def.type);
		const DocInfo* doc = Find(def.uri);
		if (def.kind == Def::Table)
		{
			if (!def.path.empty() && def.path[0] == '~') return "lns:" + def.uri + "|" + def.path;
			return "ns:" + def.path;
		}
		if (doc && def.initFirst >= 0) return TypeOfChain(*doc, def.initFirst, def.initLast, 0, depth + 1);
		return {};
	}

	std::string Workspace::TypeOfSymbol(const DocInfo& doc, int idx, int depth) const
	{
		if (idx < 0 || idx >= (int)doc.symbols.size() || depth > 8) return {};
		const Symbol& s = doc.symbols[(size_t)idx];
		if (!s.type.empty())
		{
			if (s.type == "function" && s.kind == SymKind::LocalFunction) return "lfn:" + doc.uri + "#" + std::to_string(idx);
			return TypeRef(s.type);
		}
		if (s.kind == SymKind::LocalFunction) return "lfn:" + doc.uri + "#" + std::to_string(idx);
		if (!s.requireName.empty())
		{
			const std::string p = ResolveModule(s.requireName, doc.uri);
			if (p.empty()) return HasNamespace(s.requireName) ? "ns:" + s.requireName : std::string{};
			return "mod:" + PathToUri(p);
		}
		if (s.initFirst >= 0)
		{
			const Token& f = doc.tokens[(size_t)s.initFirst];
			if (s.forMode == 0 && doc.Text(f) == "{" && s.destructure == 0)
				return "lns:" + doc.uri + "|~" + s.name;
			const std::string base = TypeOfChain(doc, s.initFirst, s.initLast, s.destructure, depth + 1);
			if (s.forMode == 1) return ElementType(base);
			if (s.forMode == 2) return "number";
			if (s.forMode == 3) return {};
			return base;
		}
		return {};
	}

	namespace
	{
		int MatchClose(const DocInfo& d, const std::vector<int>& v, int open)
		{
			int depth = 0;
			for (int i = open; i < (int)v.size(); ++i)
			{
				const Token& t = d.tokens[(size_t)v[(size_t)i]];
				if (t.kind != Tok::Op) continue;
				const char c = d.text[t.begin];
				if (c == '(' || c == '{' || c == '[') ++depth;
				else if (c == ')' || c == '}' || c == ']')
				{
					--depth;
					if (depth == 0) return i;
				}
			}
			return (int)v.size() - 1;
		}
	}

	std::string Workspace::TypeOfChain(const DocInfo& doc, int first, int last, int retIdx, int depth) const
	{
		if (depth > 8 || first < 0 || last < first || last >= (int)doc.tokens.size()) return {};
		std::vector<int> v;
		for (int i = first; i <= last; ++i)
			if (doc.tokens[(size_t)i].kind != Tok::Comment) v.push_back(i);
		if (v.empty()) return {};
		auto tx = [&](int k) { return k < (int)v.size() ? doc.Text(doc.tokens[(size_t)v[(size_t)k]]) : std::string_view{}; };
		auto kind = [&](int k) { return doc.tokens[(size_t)v[(size_t)k]].kind; };
		std::string cur;
		int i = 0;
		const Tok k0 = kind(0);
		if (k0 == Tok::Name)
		{
			const std::string name(tx(0));
			int symIdx = -1;
			if (Lookup(doc, name, v[0], &symIdx))
				cur = TypeOfSymbol(doc, symIdx, depth + 1);
			else if (const Def* d = FindDef(name))
				cur = TypeOfDef(*d, depth + 1);
			else if (HasNamespace(name))
				cur = "ns:" + name;
			i = 1;
		}
		else if (k0 == Tok::String) { cur = "string"; i = 1; }
		else if (k0 == Tok::Number) { cur = "number"; i = 1; }
		else if (k0 == Tok::Keyword && (tx(0) == "true" || tx(0) == "false")) return "boolean";
		else if (k0 == Tok::Op && tx(0) == "{")
		{
			const int c = MatchClose(doc, v, 0);
			if (c == (int)v.size() - 1) return "table";
			cur = "table";
			i = c + 1;
		}
		else if (k0 == Tok::Op && tx(0) == "(")
		{
			const int c = MatchClose(doc, v, 0);
			if (c > 1) cur = TypeOfChain(doc, v[1], v[(size_t)c - 1], 0, depth + 1);
			i = c + 1;
		}
		else
			return {};

		while (i < (int)v.size())
		{
			const std::string_view s = tx(i);
			const Tok k = kind(i);
			if (k == Tok::Op && (s == "." || s == ":") && i + 1 < (int)v.size() && kind(i + 1) == Tok::Name)
			{
				const std::string name(tx(i + 1));
				bool found = false;
				for (const Member& m : MembersOf(cur, false, doc.uri))
					if (m.name == name)
					{
						cur = m.type;
						found = true;
						break;
					}
				if (!found) return {};
				i += 2;
			}
			else if ((k == Tok::Op && s == "(") || k == Tok::String || (k == Tok::Op && s == "{"))
			{
				int close = i;
				std::vector<int> args;
				if (k == Tok::Op && s == "(")
				{
					close = MatchClose(doc, v, i);
					for (int a = i + 1; a < close; ++a) args.push_back(v[(size_t)a]);
				}
				else if (k == Tok::Op)
				{
					close = MatchClose(doc, v, i);
					args.push_back(v[(size_t)i]);
				}
				else
					args.push_back(v[(size_t)i]);
				const bool isLast = close >= (int)v.size() - 1;
				const int nth = isLast ? retIdx : 0;
				std::string result;
				if (cur == "fn:require")
				{
					if (!args.empty() && doc.tokens[(size_t)args[0]].kind == Tok::String)
					{
						std::string_view lit = doc.Text(doc.tokens[(size_t)args[0]]);
						if (lit.size() >= 2)
						{
							const std::string modName(lit.substr(1, lit.size() - 2));
							const std::string p = ResolveModule(modName, doc.uri);
							if (!p.empty()) result = "mod:" + PathToUri(p);
							else if (HasNamespace(modName)) result = "ns:" + modName; // native libraries described by the API stubs
						}
					}
				}
				else if (cur == "fn:setmetatable")
				{
					if (!args.empty()) result = TypeOfChain(doc, args.front(), args.back(), 0, depth + 1);
					// first argument only
					int d2 = 0, e = (int)args.size() - 1;
					for (size_t a = 0; a < args.size(); ++a)
					{
						const Token& t = doc.tokens[(size_t)args[a]];
						if (t.kind == Tok::Op)
						{
							const char c = doc.text[t.begin];
							if (c == '(' || c == '{' || c == '[') ++d2;
							else if (c == ')' || c == '}' || c == ']') --d2;
							else if (c == ',' && d2 == 0) { e = (int)a - 1; break; }
						}
					}
					if (e >= 0) result = TypeOfChain(doc, args.front(), args[(size_t)e], 0, depth + 1);
				}
				else if (cur.rfind("fn:", 0) == 0)
				{
					if (const Def* d = FindDef(cur.substr(3)))
						if ((size_t)nth < d->ann.returns.size()) result = TypeRef(d->ann.returns[(size_t)nth].type);
				}
				else if (cur.rfind("lfn:", 0) == 0)
				{
					const size_t hash = cur.rfind('#');
					const DocInfo* od = Find(cur.substr(4, hash - 4));
					if (od)
					{
						const int si = std::atoi(cur.c_str() + hash + 1);
						if (si >= 0 && si < (int)od->symbols.size() && (size_t)nth < od->symbols[(size_t)si].ann.returns.size())
							result = TypeRef(od->symbols[(size_t)si].ann.returns[(size_t)nth].type);
					}
				}
				cur = result;
				i = close + 1;
			}
			else if (k == Tok::Op && s == "[")
			{
				const int c = MatchClose(doc, v, i);
				cur = ElementType(cur);
				i = c + 1;
			}
			else
				return {};
			if (cur.empty() && i < (int)v.size()) return {};
		}
		return cur;
	}

	std::vector<Member> Workspace::MembersOf(const std::string& type, bool methodsOnly, const std::string& fromUri) const
	{
		std::vector<Member> out;
		std::set<std::string> seen;
		auto add = [&](const std::string& name, const Def* def, Member::Kind kind) {
			if (name.empty() || !seen.insert(name).second) return;
			Member m;
			m.name = name;
			m.def = def;
			m.kind = kind;
			if (def)
			{
				m.type = TypeOfDef(*def);
				m.detail = def->signature();
			}
			out.push_back(std::move(m));
		};
		auto kindOf = [](const Def& d) {
			switch (d.kind)
			{
			case Def::Function: return Member::Function;
			case Def::Method: return Member::Method;
			case Def::Class: return Member::Class;
			case Def::Table: return Member::Namespace;
			default: return Member::Field;
			}
		};
		// members stored in the index under `base` + sep + name
		auto fromIndex = [&](const std::string& base, bool allowDot, bool allowColon) {
			for (const Def* d : DefsWithPrefix(base))
			{
				if (d->path.size() <= base.size()) continue;
				const char sep = d->path[base.size()];
				if (!((sep == '.' && allowDot) || (sep == ':' && allowColon))) continue;
				const std::string rest = d->path.substr(base.size() + 1);
				if (rest.find_first_of(".:") != std::string::npos) continue;
				add(rest, d, kindOf(*d));
			}
		};
		auto fromDoc = [&](const DocInfo& d, const std::string& base) {
			for (const Def& def : d.defs)
			{
				if (def.path.size() <= base.size() || def.path.compare(0, base.size(), base) != 0) continue;
				const char sep = def.path[base.size()];
				if (sep != '.' && sep != ':') continue;
				const std::string rest = def.path.substr(base.size() + 1);
				if (rest.find_first_of(".:") != std::string::npos) continue;
				add(rest, &def, kindOf(def));
			}
		};

		if (type.rfind("ns:", 0) == 0)
		{
			const std::string base = type.substr(3);
			fromIndex(base, true, false);
			// child namespaces without an explicit definition
			const std::string prefix = base + ".";
			for (auto it = namespaces_.lower_bound(prefix); it != namespaces_.end() && it->compare(0, prefix.size(), prefix) == 0; ++it)
			{
				const std::string rest = it->substr(prefix.size());
				if (rest.find_first_of(".:") != std::string::npos) continue;
				if (seen.insert(rest).second)
				{
					Member m;
					m.name = rest;
					m.kind = Member::Namespace;
					m.type = "ns:" + *it;
					out.push_back(std::move(m));
				}
			}
		}
		else if (type.rfind("cls:", 0) == 0)
		{
			std::string cls = type.substr(4);
			for (int guard = 0; !cls.empty() && guard < 8; ++guard)
			{
				fromIndex(cls, true, true);
				cls = ClassBase(cls);
			}
		}
		else if (type.rfind("lns:", 0) == 0)
		{
			const size_t bar = type.find('|');
			if (const DocInfo* d = Find(type.substr(4, bar - 4))) fromDoc(*d, type.substr(bar + 1));
		}
		else if (type.rfind("mod:", 0) == 0)
		{
			if (const DocInfo* d = Find(type.substr(4)))
				if (!d->returnedSymbol.empty()) fromDoc(*d, "~" + d->returnedSymbol);
		}
		else if (type.rfind("self:", 0) == 0)
		{
			const std::string owner = type.substr(5);
			if (const DocInfo* d = Find(fromUri)) fromDoc(*d, owner);
			if (IsClass("Script")) fromIndex("Script", true, true);
		}
		else if (type == "string")
			fromIndex("string", true, false);
		else if (type.size() > 2 && type.compare(type.size() - 2, 2, "[]") == 0)
			fromIndex("table", true, false);
		else if (type == "table")
			fromIndex("table", true, false);
		else if (!type.empty() && IsClass(type))
			return MembersOf("cls:" + type, methodsOnly, fromUri);

		if (methodsOnly)
			out.erase(std::remove_if(out.begin(), out.end(), [](const Member& m) { return m.kind != Member::Method && m.kind != Member::Function; }), out.end());
		return out;
	}

	std::vector<Member> Workspace::Globals() const
	{
		std::vector<Member> out;
		std::set<std::string> seen;
		for (const auto& [path, ref] : defIndex_)
		{
			if (path.find_first_of(".:") != std::string::npos) continue;
			const Def& d = ref.first->defs[(size_t)ref.second];
			if (d.kind == Def::Class && !d.uri.empty() && Find(d.uri) && Find(d.uri)->isMeta) continue;
			if (!seen.insert(path).second) continue;
			Member m;
			m.name = path;
			m.def = &d;
			m.kind = d.kind == Def::Function ? Member::Function : d.kind == Def::Table ? Member::Namespace : Member::Variable;
			m.type = TypeOfDef(d);
			m.detail = d.signature();
			out.push_back(std::move(m));
		}
		for (const std::string& ns : namespaces_)
		{
			if (ns.find_first_of(".:") != std::string::npos || !seen.insert(ns).second) continue;
			Member m;
			m.name = ns;
			m.kind = Member::Namespace;
			m.type = "ns:" + ns;
			out.push_back(std::move(m));
		}
		for (const std::string& g : userGlobals_)
			if (seen.insert(g).second)
			{
				Member m;
				m.name = g;
				m.kind = Member::Variable;
				out.push_back(std::move(m));
			}
		return out;
	}

	// =============================================================================================
	// Symbols
	// =============================================================================================
	const Symbol* Workspace::Lookup(const DocInfo& doc, const std::string& name, int tok, int* symbolIndex) const
	{
		int best = -1;
		for (size_t i = 0; i < doc.symbols.size(); ++i)
		{
			const Symbol& s = doc.symbols[i];
			if (s.name != name || s.visibleFrom > tok || tok >= s.visibleTo) continue;
			if (best < 0 || s.visibleFrom >= doc.symbols[(size_t)best].visibleFrom) best = (int)i;
		}
		if (symbolIndex) *symbolIndex = best;
		return best >= 0 ? &doc.symbols[(size_t)best] : nullptr;
	}

	const Symbol* Workspace::Resolve(const DocInfo& doc, int tok, int* symbolIndex) const
	{
		// declarations resolve to themselves
		for (size_t i = 0; i < doc.symbols.size(); ++i)
			if (doc.symbols[i].tok == tok)
			{
				if (symbolIndex) *symbolIndex = (int)i;
				return &doc.symbols[i];
			}
		for (const Ref& r : doc.refs)
			if (r.tok == tok)
			{
				if (symbolIndex) *symbolIndex = r.symbol;
				return r.symbol >= 0 ? &doc.symbols[(size_t)r.symbol] : nullptr;
			}
		if (symbolIndex) *symbolIndex = -1;
		return nullptr;
	}

	std::vector<Symbol> Workspace::VisibleSymbols(const DocInfo& doc, int tok) const
	{
		std::map<std::string, const Symbol*> best;
		for (const Symbol& s : doc.symbols)
		{
			if (s.visibleFrom > tok || tok >= s.visibleTo || s.name == "...") continue;
			auto it = best.find(s.name);
			if (it == best.end() || s.visibleFrom >= it->second->visibleFrom) best[s.name] = &s;
		}
		std::vector<Symbol> out;
		for (auto& [n, s] : best) out.push_back(*s);
		return out;
	}

	std::vector<Diag> Workspace::Diagnose(const DocInfo& doc) const
	{
		std::vector<Diag> out;
		Diagnostic err;
		if (!CheckSyntax(doc.text, doc.uri, &err))
		{
			Diag d;
			d.severity = 1;
			d.line = std::max(0, err.line - 1);
			std::string_view lt = doc.LineText(d.line);
			size_t b = 0;
			while (b < lt.size() && (lt[b] == ' ' || lt[b] == '\t')) ++b;
			d.col = (int)b;
			d.endLine = d.line;
			d.endCol = (int)lt.size();
			d.message = err.message;
			// strip the "chunk:line:" prefix produced by Lua
			const size_t p = d.message.find(": ");
			if (p != std::string::npos && p < d.message.size() / 2 + 40 && d.message.find(':') < p) d.message = d.message.substr(p + 2);
			out.push_back(std::move(d));
			return out;
		}
		if (doc.isMeta) return out;
		const bool haveStubs = FindDef("print") != nullptr;
		if (haveStubs)
			for (const Ref& r : doc.refs)
			{
				if (r.symbol >= 0 || r.write) continue;
				const Token& t = doc.tokens[(size_t)r.tok];
				const std::string name(doc.Text(t));
				if (name == "_G" || name == "_ENV" || name == "_VERSION" || name == "self") continue;
				if (FindDef(name) || HasNamespace(name) || doc.assignedGlobals.count(name) || userGlobals_.count(name)) continue;
				Diag d;
				d.severity = 2;
				d.line = t.line;
				d.col = t.col;
				d.endLine = t.endLine;
				d.endCol = t.endCol;
				d.message = "Undefined global '" + name + "'";
				out.push_back(std::move(d));
			}
		for (const Symbol& s : doc.symbols)
		{
			if ((s.kind != SymKind::Local && s.kind != SymKind::LocalFunction) || s.uses > 0 || s.name.empty() || s.name[0] == '_') continue;
			const Token& t = doc.tokens[(size_t)s.tok];
			Diag d;
			d.severity = 4;
			d.unnecessary = true;
			d.line = t.line;
			d.col = t.col;
			d.endLine = t.endLine;
			d.endCol = t.endCol;
			d.message = "Unused local '" + s.name + "'";
			out.push_back(std::move(d));
		}
		for (const Ref& r : doc.refs)
		{
			if (r.symbol >= 0) continue;
			const Token& t = doc.tokens[(size_t)r.tok];
			if (const Def* def = FindDef(std::string(doc.Text(t))))
				if (def->ann.deprecated)
				{
					Diag d;
					d.severity = 2;
					d.deprecated = true;
					d.line = t.line;
					d.col = t.col;
					d.endLine = t.endLine;
					d.endCol = t.endCol;
					d.message = "'" + std::string(doc.Text(t)) + "' is deprecated";
					out.push_back(std::move(d));
				}
		}
		return out;
	}

} // namespace lumen::lua
