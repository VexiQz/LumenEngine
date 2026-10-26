#include "lumen/core/Pak.h"

#include "lumen/core/Fs.h"
#include "lumen/core/Platform.h"
#include "lumen/core/Strings.h"

#include <algorithm>
#include <cstdio>

#if defined(LUMEN_PLATFORM_WINDOWS)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace lumen
{
	namespace
	{
		constexpr char kMagic[4] = {'L', 'P', 'A', 'K'};
		constexpr char kFooterMagic[4] = {'K', 'A', 'P', 'L'};
		constexpr uint32_t kVersion = 1;
		constexpr size_t kHeaderSize = 4 + 4 + 4 + 4 + 8 + 8 + 8; // 40
		constexpr size_t kFooterSize = 8 + 4;
		constexpr uint64_t kAlign = 16;

		void PutU16(std::vector<uint8_t>& v, uint16_t x)
		{
			v.push_back((uint8_t)(x & 0xFF));
			v.push_back((uint8_t)(x >> 8));
		}
		void PutU32(std::vector<uint8_t>& v, uint32_t x)
		{
			for (int i = 0; i < 4; ++i)
				v.push_back((uint8_t)(x >> (8 * i)));
		}
		void PutU64(std::vector<uint8_t>& v, uint64_t x)
		{
			for (int i = 0; i < 8; ++i)
				v.push_back((uint8_t)(x >> (8 * i)));
		}
		uint16_t GetU16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
		uint32_t GetU32(const uint8_t* p)
		{
			uint32_t x = 0;
			for (int i = 0; i < 4; ++i)
				x |= (uint32_t)p[i] << (8 * i);
			return x;
		}
		uint64_t GetU64(const uint8_t* p)
		{
			uint64_t x = 0;
			for (int i = 0; i < 8; ++i)
				x |= (uint64_t)p[i] << (8 * i);
			return x;
		}

		std::string NormalizeEntryPath(const std::string& path)
		{
			std::string p = fs::Normalize(path);
			while (str::StartsWith(p, "./"))
				p.erase(0, 2);
			while (!p.empty() && p.front() == '/')
				p.erase(0, 1);
			return p;
		}

		FILE* OpenRead(const std::string& path)
		{
#if defined(LUMEN_PLATFORM_WINDOWS)
			return _wfopen(platform::Utf8ToWide(fs::ToNative(path)).c_str(), L"rb");
#else
			return std::fopen(path.c_str(), "rb");
#endif
		}

		int Seek64(FILE* f, uint64_t offset)
		{
#if defined(LUMEN_PLATFORM_WINDOWS)
			return _fseeki64(f, (long long)offset, SEEK_SET);
#else
			return fseeko(f, (off_t)offset, SEEK_SET);
#endif
		}
	} // namespace

	// ------------------------------------------------------------------------------------------------
	// writer
	// ------------------------------------------------------------------------------------------------
	void PakWriter::Add(const std::string& path, const void* data, size_t size, uint32_t flags)
	{
		const std::string p = NormalizeEntryPath(path);
		Item* target = nullptr;
		for (Item& e : entries_)
		{
			if (e.path == p)
			{
				target = &e;
				break;
			}
		}
		if (!target)
		{
			entries_.push_back({});
			target = &entries_.back();
			target->path = p;
		}
		target->data.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
		target->flags = flags;
	}

	bool PakWriter::AddFile(const std::string& path, const std::string& diskPath, uint32_t flags)
	{
		std::vector<uint8_t> bytes;
		if (!fs::ReadBinary(diskPath, bytes))
			return false;
		Add(path, bytes.data(), bytes.size(), flags);
		return true;
	}

	uint64_t PakWriter::TotalBytes() const
	{
		uint64_t total = 0;
		for (const Item& e : entries_)
			total += e.data.size();
		return total;
	}

	std::vector<uint8_t> PakWriter::Serialize() const
	{
		// Sort for deterministic output (reproducible builds).
		std::vector<const Item*> order;
		for (const Item& e : entries_)
			order.push_back(&e);
		std::sort(order.begin(), order.end(), [](const Item* a, const Item* b) { return a->path < b->path; });

		std::vector<uint8_t> out;
		out.resize(kHeaderSize, 0);

		std::vector<PakEntry> table;
		for (const Item* e : order)
		{
			while (out.size() % kAlign != 0)
				out.push_back(0);
			PakEntry entry;
			entry.path = e->path;
			entry.offset = out.size();
			entry.size = e->data.size();
			entry.flags = e->flags;
			entry.hash = str::Hash64(e->data.data(), e->data.size());
			out.insert(out.end(), e->data.begin(), e->data.end());
			table.push_back(std::move(entry));
		}
		while (out.size() % kAlign != 0)
			out.push_back(0);

		const uint64_t indexOffset = out.size();
		std::vector<uint8_t> index;
		for (const PakEntry& e : table)
		{
			PutU16(index, (uint16_t)std::min<size_t>(e.path.size(), 0xFFFF));
			index.insert(index.end(), e.path.begin(), e.path.begin() + (ptrdiff_t)std::min<size_t>(e.path.size(), 0xFFFF));
			PutU64(index, e.offset);
			PutU64(index, e.size);
			PutU32(index, e.flags);
			PutU64(index, e.hash);
		}
		out.insert(out.end(), index.begin(), index.end());
		PutU64(out, str::Hash64(index.data(), index.size()));
		out.insert(out.end(), kFooterMagic, kFooterMagic + 4);

		// Fill the header.
		std::vector<uint8_t> header;
		header.insert(header.end(), kMagic, kMagic + 4);
		PutU32(header, kVersion);
		PutU32(header, 0);
		PutU32(header, (uint32_t)table.size());
		PutU64(header, indexOffset);
		PutU64(header, index.size());
		PutU64(header, 0);
		std::memcpy(out.data(), header.data(), kHeaderSize);
		return out;
	}

	Status PakWriter::Save(const std::string& path) const
	{
		const std::vector<uint8_t> bytes = Serialize();
		if (!fs::WriteAtomic(path, bytes.data(), bytes.size()))
			return Status::Fail("cannot write archive: " + path);
		return Status::Ok();
	}

	// ------------------------------------------------------------------------------------------------
	// reader
	// ------------------------------------------------------------------------------------------------
	PakReader::~PakReader()
	{
		Close();
	}

	void PakReader::Close()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (file_)
			std::fclose((FILE*)file_);
		file_ = nullptr;
		memory_.clear();
		inMemory_ = false;
		open_ = false;
		entries_.clear();
		lookup_.clear();
		sourcePath_.clear();
	}

	Status PakReader::ParseIndex(const uint8_t* header, const uint8_t* index, uint64_t indexSize)
	{
		const uint32_t count = GetU32(header + 12);
		uint64_t pos = 0;
		entries_.clear();
		entries_.reserve(count);
		for (uint32_t i = 0; i < count; ++i)
		{
			if (pos + 2 > indexSize)
				return Status::Fail("corrupt archive index");
			const uint16_t len = GetU16(index + pos);
			pos += 2;
			if (pos + len + 8 + 8 + 4 + 8 > indexSize)
				return Status::Fail("corrupt archive index");
			PakEntry e;
			e.path.assign(reinterpret_cast<const char*>(index + pos), len);
			pos += len;
			e.offset = GetU64(index + pos);
			pos += 8;
			e.size = GetU64(index + pos);
			pos += 8;
			e.flags = GetU32(index + pos);
			pos += 4;
			e.hash = GetU64(index + pos);
			pos += 8;
			entries_.push_back(std::move(e));
		}
		lookup_.clear();
		for (size_t i = 0; i < entries_.size(); ++i)
			lookup_.emplace_back(entries_[i].path, i);
		std::sort(lookup_.begin(), lookup_.end());
		return Status::Ok();
	}

	Status PakReader::OpenMemory(std::vector<uint8_t> data)
	{
		Close();
		std::lock_guard<std::mutex> lock(mutex_);
		if (data.size() < kHeaderSize + kFooterSize || std::memcmp(data.data(), kMagic, 4) != 0)
			return Status::Fail("not an LPAK archive");
		if (GetU32(data.data() + 4) != kVersion)
			return Status::Fail("unsupported LPAK version");
		const uint64_t indexOffset = GetU64(data.data() + 16);
		const uint64_t indexSize = GetU64(data.data() + 24);
		if (indexOffset + indexSize + kFooterSize > data.size())
			return Status::Fail("truncated archive");
		const uint8_t* footer = data.data() + indexOffset + indexSize;
		if (std::memcmp(footer + 8, kFooterMagic, 4) != 0)
			return Status::Fail("archive footer missing");
		if (GetU64(footer) != str::Hash64(data.data() + indexOffset, (size_t)indexSize))
			return Status::Fail("archive index checksum mismatch");
		Status st = ParseIndex(data.data(), data.data() + indexOffset, indexSize);
		if (!st)
			return st;
		memory_ = std::move(data);
		inMemory_ = true;
		open_ = true;
		return Status::Ok();
	}

	Status PakReader::Open(const std::string& path)
	{
		Close();
		FILE* f = OpenRead(path);
		if (!f)
			return Status::Fail("cannot open archive: " + path);

		uint8_t header[kHeaderSize];
		if (std::fread(header, 1, kHeaderSize, f) != kHeaderSize || std::memcmp(header, kMagic, 4) != 0)
		{
			std::fclose(f);
			return Status::Fail("not an LPAK archive: " + path);
		}
		if (GetU32(header + 4) != kVersion)
		{
			std::fclose(f);
			return Status::Fail("unsupported LPAK version");
		}
		const uint64_t indexOffset = GetU64(header + 16);
		const uint64_t indexSize = GetU64(header + 24);
		if (indexSize > (1ull << 30))
		{
			std::fclose(f);
			return Status::Fail("archive index too large");
		}
		std::vector<uint8_t> index((size_t)indexSize + kFooterSize);
		if (Seek64(f, indexOffset) != 0 || std::fread(index.data(), 1, index.size(), f) != index.size())
		{
			std::fclose(f);
			return Status::Fail("truncated archive: " + path);
		}
		if (std::memcmp(index.data() + indexSize + 8, kFooterMagic, 4) != 0 ||
			GetU64(index.data() + indexSize) != str::Hash64(index.data(), (size_t)indexSize))
		{
			std::fclose(f);
			return Status::Fail("archive index is corrupt: " + path);
		}
		std::lock_guard<std::mutex> lock(mutex_);
		Status st = ParseIndex(header, index.data(), indexSize);
		if (!st)
		{
			std::fclose(f);
			return st;
		}
		file_ = f;
		open_ = true;
		sourcePath_ = path;
		return Status::Ok();
	}

	const PakEntry* PakReader::Find(const std::string& path) const
	{
		const std::string p = NormalizeEntryPath(path);
		const auto it = std::lower_bound(lookup_.begin(), lookup_.end(), std::make_pair(p, (size_t)0),
			[](const auto& a, const auto& b) { return a.first < b.first; });
		if (it == lookup_.end() || it->first != p)
			return nullptr;
		return &entries_[it->second];
	}

	bool PakReader::Exists(const std::string& path) const
	{
		return Find(path) != nullptr;
	}

	bool PakReader::ReadRange(uint64_t offset, uint64_t size, void* dst) const
	{
		if (inMemory_)
		{
			if (offset + size > memory_.size())
				return false;
			std::memcpy(dst, memory_.data() + offset, (size_t)size);
			return true;
		}
		if (!file_)
			return false;
		if (Seek64((FILE*)file_, offset) != 0)
			return false;
		return size == 0 || std::fread(dst, 1, (size_t)size, (FILE*)file_) == size;
	}

	bool PakReader::Read(const std::string& path, std::vector<uint8_t>& out) const
	{
		const PakEntry* e = Find(path);
		if (!e)
			return false;
		std::lock_guard<std::mutex> lock(mutex_);
		out.resize((size_t)e->size);
		if (!ReadRange(e->offset, e->size, out.data()))
			return false;
		return str::Hash64(out.data(), out.size()) == e->hash;
	}

	bool PakReader::ReadText(const std::string& path, std::string& out) const
	{
		std::vector<uint8_t> bytes;
		if (!Read(path, bytes))
			return false;
		out.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
		return true;
	}

	std::vector<std::string> PakReader::Verify() const
	{
		std::vector<std::string> bad;
		std::vector<uint8_t> tmp;
		for (const PakEntry& e : entries_)
		{
			if (!Read(e.path, tmp))
				bad.push_back(e.path);
		}
		return bad;
	}

} // namespace lumen
