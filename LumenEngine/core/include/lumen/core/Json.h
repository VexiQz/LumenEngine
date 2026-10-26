// Lumen Engine — JSON value, parser and serializer.
//
// Design goals: never throws and never aborts on malformed input or wrong types (the LSP server
// feeds it data from arbitrary clients), keeps object key order (stable .lum diffs), 64-bit integers
// and full UTF-8/UTF-16 escape handling.
#pragma once

#include "lumen/core/Common.h"

namespace lumen
{
	struct JsonParseOptions
	{
		bool allowComments = false;       // // and /* */ comments
		bool allowTrailingCommas = false; // [1,2,] and {"a":1,}
		size_t maxDepth = 256;
	};

	struct JsonDumpOptions
	{
		int indent = -1;            // -1: compact, otherwise spaces per level
		bool sortKeys = false;
		bool escapeNonAscii = false; // emit \uXXXX for every non ASCII code point
	};

	class Json
	{
	public:
		enum class Type : uint8_t
		{
			Null,
			Bool,
			Int,
			Double,
			String,
			Array,
			Object
		};

		using Array = std::vector<Json>;
		using Member = std::pair<std::string, Json>;
		using Object = std::vector<Member>;

		using ParseOptions = JsonParseOptions;
		using DumpOptions = JsonDumpOptions;

		Json() = default;
		Json(std::nullptr_t) {}
		Json(bool v) : type_(Type::Bool) { u_.b = v; }
		Json(int v) : type_(Type::Int) { u_.i = v; }
		Json(unsigned v) : type_(Type::Int) { u_.i = (int64_t)v; }
		Json(long v) : type_(Type::Int) { u_.i = (int64_t)v; }
		Json(unsigned long v) : type_(Type::Int) { u_.i = (int64_t)v; }
		Json(long long v) : type_(Type::Int) { u_.i = (int64_t)v; }
		Json(unsigned long long v) : type_(Type::Int) { u_.i = (int64_t)v; }
		Json(double v) : type_(Type::Double) { u_.d = v; }
		Json(float v) : type_(Type::Double) { u_.d = (double)v; }
		Json(const char* v) : type_(Type::String), str_(v ? v : "") {}
		Json(std::string v) : type_(Type::String), str_(std::move(v)) {}
		Json(std::string_view v) : type_(Type::String), str_(v) {}
		Json(Array v) : type_(Type::Array), arr_(std::move(v)) {}
		Json(Object v) : type_(Type::Object), obj_(std::move(v)) {}

		static Json MakeArray() { return Json(Array{}); }
		static Json MakeObject() { return Json(Object{}); }

		// --- type queries ------------------------------------------------------------------------
		Type GetType() const { return type_; }
		bool IsNull() const { return type_ == Type::Null; }
		bool IsBool() const { return type_ == Type::Bool; }
		bool IsInt() const { return type_ == Type::Int; }
		bool IsDouble() const { return type_ == Type::Double; }
		bool IsNumber() const { return type_ == Type::Int || type_ == Type::Double; }
		bool IsString() const { return type_ == Type::String; }
		bool IsArray() const { return type_ == Type::Array; }
		bool IsObject() const { return type_ == Type::Object; }

		// --- value access with defaults (never fails) ------------------------------------------------
		bool AsBool(bool def = false) const;
		int64_t AsInt(int64_t def = 0) const;
		double AsDouble(double def = 0.0) const;
		const std::string& AsString() const; // empty string if not a string
		std::string AsString(const std::string& def) const;

		// --- containers -------------------------------------------------------------------------------
		size_t Size() const; // elements of an array / members of an object / 0 otherwise
		bool Empty() const { return Size() == 0; }

		// Arrays
		const Json& At(size_t index) const; // null value when out of range or not an array
		Json& operator[](size_t index);       // converts null into an array, grows as needed
		void Push(Json v);                    // converts null into an array
		const Array& Items() const;           // empty array if not an array
		Array& ItemsMutable();                // converts null into an array

		// Objects
		bool Contains(std::string_view key) const;
		const Json* Find(std::string_view key) const;
		Json* FindMutable(std::string_view key);
		const Json& Get(std::string_view key) const; // null value when missing or not an object
		Json& operator[](std::string_view key);     // converts null into an object, inserts when missing
		Json& operator[](const char* key) { return (*this)[std::string_view(key)]; }
		Json& operator[](const std::string& key) { return (*this)[std::string_view(key)]; }
		void Set(std::string_view key, Json v);
		bool Erase(std::string_view key);
		const Object& Members() const; // empty object if not an object
		Object& MembersMutable();       // converts null into an object

		// Typed object field helpers: return `def` when the key is missing or has a different type.
		std::string GetString(std::string_view key, const std::string& def = {}) const;
		int64_t GetInt(std::string_view key, int64_t def = 0) const;
		double GetDouble(std::string_view key, double def = 0.0) const;
		bool GetBool(std::string_view key, bool def = false) const;
		std::vector<std::string> GetStringArray(std::string_view key) const;

		// --- text ---------------------------------------------------------------------------------------
		// Parses `text`. On failure returns false, leaves `out` null and fills `error` with
		// "line:column: message".
		static bool Parse(std::string_view text, Json& out, std::string* error = nullptr, const ParseOptions& options = {});
		// Convenience: returns a null Json on failure.
		static Json ParseOrNull(std::string_view text, const ParseOptions& options = {});
		std::string Dump(const DumpOptions& options = {}) const;
		std::string Dump(int indent) const
		{
			DumpOptions o;
			o.indent = indent;
			return Dump(o);
		}

		bool operator==(const Json& other) const;
		bool operator!=(const Json& other) const { return !(*this == other); }

	private:
		void DumpTo(std::string& out, const DumpOptions& options, int depth) const;

		Type type_ = Type::Null;
		union U
		{
			bool b;
			int64_t i;
			double d;
			U() : i(0) {}
		} u_;
		std::string str_;
		Array arr_;
		Object obj_;
	};

} // namespace lumen
