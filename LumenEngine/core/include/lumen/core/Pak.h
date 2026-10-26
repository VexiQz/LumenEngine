// Lumen Engine — LPAK archive: a simple, seekable, integrity-checked container used for release
// builds (compiled Lua bytecode, shaders, small assets) and by the runtime to ship embedded data.
//
// Layout (little endian):
//   header  : "LPAK" | u32 version | u32 flags | u32 entryCount | u64 indexOffset | u64 indexSize | u64 reserved
//   blobs   : raw file contents, 16 byte aligned
//   index   : entryCount * { u16 pathLen | path | u64 offset | u64 size | u32 flags | u64 fnv1a64 }
//   footer  : u64 fnv1a64 of the index bytes | "KAPL"
#pragma once

#include "lumen/core/Common.h"

#include <mutex>

namespace lumen
{
	enum PakEntryFlags : uint32_t
	{
		PakEntry_None = 0,
		PakEntry_LuaBytecode = 1u << 0, // content is precompiled Lua bytecode
		PakEntry_Text = 1u << 1,
		PakEntry_Shader = 1u << 2,
	};

	struct PakEntry
	{
		std::string path; // normalized, '/' separated, relative
		uint64_t offset = 0;
		uint64_t size = 0;
		uint32_t flags = 0;
		uint64_t hash = 0;
	};

	class PakWriter
	{
	public:
		// Adds (or replaces) an entry.
		void Add(const std::string& path, const void* data, size_t size, uint32_t flags = PakEntry_None);
		void Add(const std::string& path, const std::vector<uint8_t>& data, uint32_t flags = PakEntry_None)
		{
			Add(path, data.data(), data.size(), flags);
		}
		bool AddFile(const std::string& path, const std::string& diskPath, uint32_t flags = PakEntry_None);
		size_t Count() const { return entries_.size(); }
		uint64_t TotalBytes() const;
		// Serializes into memory / writes to disk (atomically).
		std::vector<uint8_t> Serialize() const;
		Status Save(const std::string& path) const;

	private:
		struct Item
		{
			std::string path;
			std::vector<uint8_t> data;
			uint32_t flags;
		};
		std::vector<Item> entries_;
	};

	class PakReader
	{
	public:
		PakReader() = default;
		~PakReader();
		PakReader(const PakReader&) = delete;
		PakReader& operator=(const PakReader&) = delete;

		Status Open(const std::string& path);
		// Takes ownership of an in-memory archive.
		Status OpenMemory(std::vector<uint8_t> data);
		void Close();
		bool IsOpen() const { return open_; }

		bool Exists(const std::string& path) const;
		const PakEntry* Find(const std::string& path) const;
		const std::vector<PakEntry>& Entries() const { return entries_; }
		// Reads an entry and verifies its checksum.
		bool Read(const std::string& path, std::vector<uint8_t>& out) const;
		bool ReadText(const std::string& path, std::string& out) const;
		// Verifies every entry; returns the paths whose checksum does not match.
		std::vector<std::string> Verify() const;
		const std::string& SourcePath() const { return sourcePath_; }

	private:
		Status ParseIndex(const uint8_t* header, const uint8_t* index, uint64_t indexSize);
		bool ReadRange(uint64_t offset, uint64_t size, void* dst) const;

		bool open_ = false;
		std::string sourcePath_;
		std::vector<uint8_t> memory_;
		bool inMemory_ = false;
		void* file_ = nullptr; // FILE*
		mutable std::mutex mutex_;
		std::vector<PakEntry> entries_;
		std::vector<std::pair<std::string, size_t>> lookup_; // sorted by path
	};

} // namespace lumen
