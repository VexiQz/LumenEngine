// Lumen Engine — static analysis of Lua source for tooling (language server, editor completion).
//
// A tolerant, token based analyzer: it never fails on broken code. It resolves scopes, declarations
// and references, understands EmmyLua annotations (---@param, ---@return, ---@class, ---@field,
// ---@type, ---@meta) and derives a small type system from them. The API of the engine itself is
// described by annotated stub files (runtime_data/lua/api/*.lua) that are analyzed with the same code.
#pragma once

#include "lumen/core/Common.h"

#include <map>
#include <set>
#include <unordered_map>

namespace lumen::lua
{
	enum class Tok : uint8_t
	{
		Name,
		Keyword,
		Number,
		String,
		Comment,
		Op,
		Eof
	};

	struct Token
	{
		Tok kind = Tok::Eof;
		uint32_t begin = 0, end = 0; // byte offsets into the source
		int line = 0, col = 0;       // 0-based start (col in bytes)
		int endLine = 0, endCol = 0; // 0-based end (exclusive)
		bool doc = false;            // comment starting with '---'
	};

	// Splits source into tokens (comments included). Handles long strings/comments, escapes, numbers.
	std::vector<Token> Tokenize(std::string_view src);
	bool IsKeyword(std::string_view s);

	enum class SymKind : uint8_t
	{
		Local,
		Param,
		LocalFunction,
		ForVar,
		Global
	};

	struct ParamInfo
	{
		std::string name;
		std::string type; // "" when unknown
		std::string doc;
	};

	struct Annotations
	{
		std::string description;
		std::vector<ParamInfo> params;
		std::vector<ParamInfo> returns; // name may be empty
		std::string classes;            // @class name
		std::string classBase;
		std::string type;               // @type
		std::vector<ParamInfo> fields;  // @field name type desc
		bool meta = false;
		bool deprecated = false;
		bool empty() const { return description.empty() && params.empty() && returns.empty() && classes.empty() && type.empty() && fields.empty(); }
	};

	struct Symbol
	{
		std::string name;
		SymKind kind = SymKind::Local;
		int tok = 0;              // declaration token index
		int visibleFrom = 0;      // token index range in which the symbol can be referenced
		int visibleTo = 0;
		std::string type;         // inferred or annotated ("" unknown)
		Annotations ann;
		std::vector<std::string> paramNames; // for functions
		bool isFunction = false;
		bool isVararg = false;
		int initFirst = -1, initLast = -1; // initializer expression (doc token indices) for lazy type inference
		int destructure = 0;               // `local a, b = f()`: index of the returned value this symbol takes
		int forMode = 0;                   // 1: value of ipairs/pairs(expr), 2: key of ipairs (number), 3: key of pairs
		int uses = 0;
		std::string requireName;  // for `local m = require "x"`
		std::string signature() const;
	};

	// A definition addressable by path: "lumen.scene.find", "Entity:position", "Entity.name", "string.format".
	struct Def
	{
		enum Kind : uint8_t { Function, Method, Field, Table, Class };
		std::string path;
		Kind kind = Function;
		std::vector<ParamInfo> params;
		bool vararg = false;
		Annotations ann;
		std::string type;  // value type for fields/tables (annotated)
		int initFirst = -1, initLast = -1; // initializer expression for lazy inference
		int tok = 0;       // name token in the defining document
		std::string uri;   // defining document
		std::string signature() const; // "lumen.scene.find(name: string): Entity?"
	};

	struct Ref
	{
		int tok = 0;
		int symbol = -1; // index into DocInfo::symbols, -1 = global / unresolved
		bool write = false;
	};

	struct Diag
	{
		int severity = 2; // LSP numbering
		int line = 0, col = 0, endLine = 0, endCol = 0;
		std::string message;
		bool unnecessary = false;
		bool deprecated = false;
	};

	struct DocInfo
	{
		std::string uri;
		std::string text;
		std::vector<Token> tokens;
		std::vector<uint32_t> lineStarts;
		std::vector<Symbol> symbols;
		std::vector<Ref> refs;
		std::vector<Def> defs;           // global / table-field / class member definitions made in this file
		std::vector<std::string> classes;
		std::map<std::string, std::string> classBases;
		std::string returnedSymbol;      // name of the local returned at file end ("M" in `return M`)
		int returnedSymbolIndex = -1;
		bool isMeta = false;
		std::set<std::string> assignedGlobals; // globals assigned (not declared) in this file
		std::vector<std::string> requires_;   // module names required
		int version = 0;

		std::string_view Text(const Token& t) const { return std::string_view(text).substr(t.begin, t.end - t.begin); }
		std::string_view LineText(int line) const;
		// Token index containing (line, col) (byte col) or the one right before it; -1 if none.
		int TokenAt(int line, int col, bool inclusiveEnd = false) const;
	};

	// Analyzes one document. Does not need a workspace; cross document knowledge is added by the Workspace.
	void Analyze(DocInfo& doc, const std::string& uri, std::string text);

	// --- positions ------------------------------------------------------------------------------------
	// LSP uses UTF-16 code units for columns, the analyzer bytes.
	int ByteColToUtf16(std::string_view line, int byteCol);
	int Utf16ToByteCol(std::string_view line, int utf16Col);

	struct Member
	{
		std::string name;
		enum Kind : uint8_t { Function, Method, Field, Namespace, Variable, Module, Class } kind = Field;
		const Def* def = nullptr;
		const Symbol* symbol = nullptr;
		std::string type;
		std::string detail;
	};

	// Knows every analyzed document plus the engine API stubs, and answers semantic questions.
	class Workspace
	{
	public:
		void SetRoots(std::string projectRoot, std::vector<std::string> moduleDirs, std::vector<std::string> apiDirs);
		// Loads every .lua file below the roots (stubs first). Safe to call again.
		void Scan();

		DocInfo& Open(const std::string& uri, std::string text, int version = 0);
		void Close(const std::string& uri);
		DocInfo* Find(const std::string& uri);
		const DocInfo* Find(const std::string& uri) const;
		bool IsOpen(const std::string& uri) const { return open_.count(uri) != 0; }

		// Loads (and caches) a document from disk, used for modules and stubs that are not open.
		DocInfo* Load(const std::string& path) const;
		std::string ResolveModule(const std::string& name, const std::string& fromUri) const; // file path or ""
		std::vector<std::string> ModuleNames() const;

		// Documents that define globals/stubs (all analyzed docs).
		std::vector<const DocInfo*> Docs() const;

		// --- semantic queries -----------------------------------------------------------------------
		const Def* FindDef(const std::string& path) const;
		std::vector<const Def*> DefsWithPrefix(const std::string& prefix) const;
		bool HasNamespace(const std::string& path) const;
		bool IsClass(const std::string& name) const;
		std::string ClassBase(const std::string& name) const;

		// Type of the expression formed by tokens [first, last] of `doc` (a name/field/call chain).
		std::string TypeOfChain(const DocInfo& doc, int first, int last, int returnIndex = 0, int depth = 0) const;
		// Members that can follow `type` + '.' or ':'.
		std::vector<Member> MembersOf(const std::string& type, bool methodsOnly = false, const std::string& fromUri = {}) const;
		std::string ElementType(const std::string& type) const;
		// Type reference ("Entity?" -> "cls:Entity", "Entity[]" -> "cls:Entity[]", "integer" -> "number").
		std::string TypeRef(const std::string& raw) const;
		std::string TypeOfSymbol(const DocInfo& doc, int symbolIndex, int depth = 0) const;
		std::string TypeOfDef(const Def& def, int depth = 0) const;
		// Human readable type name for hover ("cls:Entity" -> "Entity").
		static std::string DisplayType(const std::string& type);
		// Global names (stub tables, functions, user globals).
		std::vector<Member> Globals() const;
		const Symbol* Resolve(const DocInfo& doc, int tok, int* symbolIndex = nullptr) const;
		// Symbol visible at token index `tok` named `name`.
		const Symbol* Lookup(const DocInfo& doc, const std::string& name, int tok, int* symbolIndex = nullptr) const;
		std::vector<Symbol> VisibleSymbols(const DocInfo& doc, int tok) const;
		// Diagnostics derived from analysis (undefined globals, unused locals) plus syntax errors.
		std::vector<Diag> Diagnose(const DocInfo& doc) const;

	private:
		void Rebuild();
		std::string projectRoot_;
		std::vector<std::string> moduleDirs_, apiDirs_;
		std::map<std::string, std::unique_ptr<DocInfo>> open_;
		mutable std::map<std::string, std::unique_ptr<DocInfo>> disk_; // by uri
		mutable std::map<std::string, uint64_t> mtimes_;
		std::multimap<std::string, std::pair<const DocInfo*, int>> defIndex_; // path -> (doc, def idx)
		std::set<std::string> namespaces_;
		std::map<std::string, std::string> classBases_;
		std::set<std::string> classes_;
		std::set<std::string> userGlobals_;
	};

	std::string PathToUri(const std::string& path);
	std::string UriToPath(const std::string& uri);

} // namespace lumen::lua
