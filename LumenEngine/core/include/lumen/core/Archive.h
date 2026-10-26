// Lumen Engine — archive support without external libraries: DEFLATE decoder, ZIP and TAR(.gz) extraction,
// and a ZIP writer. Extraction is hardened against path traversal ("zip slip"), absolute paths, device names
// and decompression bombs. Formats this code does not decode itself (.tar.xz, .tar.bz2, .7z) are delegated to
// the system `tar` through Extract().
#pragma once

#include "lumen/core/Common.h"

#include <atomic>
#include <functional>

namespace lumen::archive
{
	// RFC 1951 raw DEFLATE. `expectedSize` (0 = unknown) pre-sizes the output; `maxOutput` aborts bombs.
	bool Inflate(const uint8_t* data, size_t size, std::vector<uint8_t>& out, size_t expectedSize = 0, uint64_t maxOutput = (4ull << 30), std::atomic<bool>* cancel = nullptr);
	// RFC 1952 gzip container (multi member files are concatenated).
	bool Gunzip(const uint8_t* data, size_t size, std::vector<uint8_t>& out, uint64_t maxOutput = (4ull << 30), std::atomic<bool>* cancel = nullptr);

	// Stored/deflated ZIP entries may be extracted through ZipReader without touching the disk.
	struct ZipEntry
	{
		std::string name;
		uint64_t compressedSize = 0;
		uint64_t size = 0;
		uint32_t crc32 = 0;
		uint16_t method = 0;       // 0 stored, 8 deflate
		uint32_t externalAttr = 0;
		uint64_t localHeaderOffset = 0;
		bool IsDirectory() const { return !name.empty() && name.back() == '/'; }
		bool IsSymlink() const { return ((externalAttr >> 16) & 0170000u) == 0120000u; }
		bool IsExecutable() const { return ((externalAttr >> 16) & 0111u) != 0; }
	};

	class ZipReader
	{
	public:
		Status Open(const std::string& path);
		Status OpenMemory(std::vector<uint8_t> data);
		const std::vector<ZipEntry>& Entries() const { return entries_; }
		const ZipEntry* Find(const std::string& name) const;
		// Reads and verifies (CRC-32) one entry.
		Status Read(const ZipEntry& entry, std::vector<uint8_t>& out, std::atomic<bool>* cancel = nullptr) const;
	private:
		std::vector<uint8_t> data_;
		std::vector<ZipEntry> entries_;
	};

	struct ExtractOptions
	{
		int stripComponents = 0;                                     // drop leading path components ("pkg-1.0/")
		std::function<bool(const std::string& relativePath)> filter; // return false to skip an entry
		bool overwrite = true;
		uint64_t maxTotalBytes = (8ull << 30);
		uint64_t maxFiles = 2000000;
		std::function<void(const std::string& relativePath)> onFile;
		std::atomic<bool>* cancel = nullptr;
	};

	Status ExtractZip(const std::string& zipPath, const std::string& destDir, const ExtractOptions& options = {});
	// `.tar` and `.tar.gz` / `.tgz` (auto detected by the gzip magic number).
	Status ExtractTar(const std::string& tarPath, const std::string& destDir, const ExtractOptions& options = {});
	// Chooses the extractor by file name. Unknown compressions (.tar.xz, .tar.bz2, .7z, .tar.zst) go to the system tar.
	Status Extract(const std::string& archivePath, const std::string& destDir, const ExtractOptions& options = {});

	// Returns the sanitized relative path, or "" when the entry must be rejected.
	std::string SafeEntryPath(const std::string& name, int stripComponents);

	// Minimal ZIP writer (deflate level "stored" or caller supplied compressed data is not required: entries are stored).
	class ZipWriter
	{
	public:
		void AddFile(const std::string& name, const void* data, size_t size, bool executable = false);
		Status AddDiskFile(const std::string& name, const std::string& diskPath);
		std::vector<uint8_t> Finish();
		Status Save(const std::string& path);
	private:
		struct Item
		{
			std::string name;
			uint32_t crc = 0;
			uint64_t size = 0;
			uint64_t offset = 0;
			uint32_t attr = 0;
		};
		std::vector<uint8_t> data_;
		std::vector<Item> items_;
	};

} // namespace lumen::archive
