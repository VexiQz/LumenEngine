#include "lumen/core/Archive.h"

#include "lumen/core/Fs.h"
#include "lumen/core/Image.h"
#include "lumen/core/Process.h"
#include "lumen/core/Strings.h"

#include <algorithm>
#include <cstring>
#include <sys/stat.h>
#ifndef _WIN32
#include <unistd.h>
#endif

namespace lumen::archive
{
	namespace
	{
		// ---------------------------------------------------------------------------------------------
		// DEFLATE decoder (canonical Huffman, table driven by counts — compact and fast enough).
		// ---------------------------------------------------------------------------------------------
		constexpr int kMaxBits = 15;

		struct BitReader
		{
			const uint8_t* p;
			size_t n;
			size_t pos = 0;
			uint32_t bitBuf = 0;
			int bitCnt = 0;
			bool overrun = false;

			int Bits(int need)
			{
				uint32_t val = bitBuf;
				while (bitCnt < need)
				{
					if (pos >= n)
					{
						overrun = true;
						return 0;
					}
					val |= (uint32_t)p[pos++] << bitCnt;
					bitCnt += 8;
				}
				bitBuf = val >> need;
				bitCnt -= need;
				return (int)(val & ((1u << need) - 1));
			}
		};

		struct Huffman
		{
			uint16_t count[kMaxBits + 1];
			uint16_t symbol[288];
		};

		bool BuildHuffman(Huffman& h, const uint8_t* lengths, int n)
		{
			std::memset(h.count, 0, sizeof(h.count));
			for (int i = 0; i < n; ++i)
				h.count[lengths[i]]++;
			if (h.count[0] == n)
				return true; // no codes: valid when no symbol is used
			int left = 1;
			for (int len = 1; len <= kMaxBits; ++len)
			{
				left <<= 1;
				left -= h.count[len];
				if (left < 0)
					return false; // over-subscribed
			}
			uint16_t offs[kMaxBits + 1];
			offs[1] = 0;
			for (int len = 1; len < kMaxBits; ++len)
				offs[len + 1] = offs[len] + h.count[len];
			for (int i = 0; i < n; ++i)
				if (lengths[i] != 0)
					h.symbol[offs[lengths[i]]++] = (uint16_t)i;
			return true;
		}

		int DecodeSymbol(BitReader& br, const Huffman& h)
		{
			int code = 0, first = 0, index = 0;
			for (int len = 1; len <= kMaxBits; ++len)
			{
				code |= br.Bits(1);
				if (br.overrun)
					return -1;
				const int count = h.count[len];
				if (code - count < first)
					return h.symbol[index + (code - first)];
				index += count;
				first += count;
				first <<= 1;
				code <<= 1;
			}
			return -1;
		}

		const uint16_t kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
		const uint16_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
		const uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
		const uint16_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

		bool InflateCodes(BitReader& br, std::vector<uint8_t>& out, const Huffman& lit, const Huffman& dist, uint64_t maxOutput, std::atomic<bool>* cancel)
		{
			size_t nextCancelCheck = out.size() + (64u << 10);
			for (;;)
			{
				if (cancel && out.size() >= nextCancelCheck)
				{
					if (cancel->load())
						return false;
					nextCancelCheck = out.size() + (64u << 10);
				}
				int sym = DecodeSymbol(br, lit);
				if (sym < 0)
					return false;
				if (sym < 256)
				{
					out.push_back((uint8_t)sym);
				}
				else if (sym == 256)
				{
					return true;
				}
				else
				{
					sym -= 257;
					if (sym >= 29)
						return false;
					const int len = kLenBase[sym] + br.Bits(kLenExtra[sym]);
					const int ds = DecodeSymbol(br, dist);
					if (ds < 0 || ds >= 30)
						return false;
					const size_t d = (size_t)kDistBase[ds] + (size_t)br.Bits(kDistExtra[ds]);
					if (br.overrun || d > out.size())
						return false;
					size_t from = out.size() - d;
					for (int i = 0; i < len; ++i)
						out.push_back(out[from + (size_t)i]);
				}
				if (out.size() > maxOutput)
					return false;
			}
		}

		const uint8_t kClOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

		// ---------------------------------------------------------------------------------------------
		inline uint16_t RdU16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
		inline uint32_t RdU32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
		inline uint64_t RdU64(const uint8_t* p) { return (uint64_t)RdU32(p) | ((uint64_t)RdU32(p + 4) << 32); }
		void WrU16(std::vector<uint8_t>& v, uint16_t x) { v.push_back((uint8_t)x); v.push_back((uint8_t)(x >> 8)); }
		void WrU32(std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back((uint8_t)(x >> (8 * i))); }

		bool IsReservedWindowsName(const std::string& comp)
		{
			std::string base = str::ToUpper(comp);
			const size_t dot = base.find('.');
			if (dot != std::string::npos)
				base = base.substr(0, dot);
			static const char* reserved[] = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
				"LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
			for (const char* r : reserved)
				if (base == r)
					return true;
			return false;
		}

		bool WriteExtracted(const std::string& dest, const std::string& rel, const uint8_t* data, size_t size, bool exec, bool overwrite)
		{
			const std::string path = fs::Join(dest, rel);
			if (!overwrite && fs::Exists(path))
				return true;
			fs::CreateDirs(fs::Parent(path));
			if (!fs::WriteBinary(path, data, size))
				return false;
#ifndef _WIN32
			if (exec)
				chmod(fs::ToNative(path).c_str(), 0755);
#else
			(void)exec;
#endif
			return true;
		}
	} // namespace

	static bool InflateImpl(const uint8_t* data, size_t size, std::vector<uint8_t>& out, size_t expectedSize, uint64_t maxOutput, size_t* consumed, std::atomic<bool>* cancel)
	{
		out.clear();
		if (cancel && cancel->load())
			return false;
		if (expectedSize)
			out.reserve(expectedSize);
		BitReader br{data, size};
		int last;
		do
		{
			if (cancel && cancel->load())
				return false;
			last = br.Bits(1);
			const int type = br.Bits(2);
			if (br.overrun)
				return false;
			if (type == 0)
			{
				br.bitBuf = 0;
				br.bitCnt = 0;
				if (br.pos + 4 > size)
					return false;
				const uint32_t len = RdU16(data + br.pos);
				const uint32_t nlen = RdU16(data + br.pos + 2);
				br.pos += 4;
				if (len != (~nlen & 0xFFFFu) || br.pos + len > size)
					return false;
				out.insert(out.end(), data + br.pos, data + br.pos + len);
				br.pos += len;
			}
			else if (type == 1)
			{
				static Huffman lit, dist;
				static bool init = false;
				if (!init)
				{
					uint8_t lengths[288];
					int i = 0;
					for (; i < 144; ++i) lengths[i] = 8;
					for (; i < 256; ++i) lengths[i] = 9;
					for (; i < 280; ++i) lengths[i] = 7;
					for (; i < 288; ++i) lengths[i] = 8;
					BuildHuffman(lit, lengths, 288);
					uint8_t dl[30];
					for (int k = 0; k < 30; ++k) dl[k] = 5;
					BuildHuffman(dist, dl, 30);
					init = true;
				}
				if (!InflateCodes(br, out, lit, dist, maxOutput, cancel))
					return false;
			}
			else if (type == 2)
			{
				const int nlen = br.Bits(5) + 257;
				const int ndist = br.Bits(5) + 1;
				const int ncode = br.Bits(4) + 4;
				if (br.overrun || nlen > 286 || ndist > 30)
					return false;
				uint8_t lengths[320] = {};
				for (int i = 0; i < ncode; ++i)
					lengths[kClOrder[i]] = (uint8_t)br.Bits(3);
				Huffman cl;
				if (!BuildHuffman(cl, lengths, 19))
					return false;
				uint8_t ll[320] = {};
				int idx = 0;
				while (idx < nlen + ndist)
				{
					int sym = DecodeSymbol(br, cl);
					if (sym < 0)
						return false;
					if (sym < 16)
						ll[idx++] = (uint8_t)sym;
					else
					{
						int len = 0, rep;
						if (sym == 16)
						{
							if (idx == 0)
								return false;
							len = ll[idx - 1];
							rep = 3 + br.Bits(2);
						}
						else if (sym == 17)
							rep = 3 + br.Bits(3);
						else
							rep = 11 + br.Bits(7);
						if (idx + rep > nlen + ndist)
							return false;
						while (rep--)
							ll[idx++] = (uint8_t)len;
					}
					if (br.overrun)
						return false;
				}
				if (ll[256] == 0)
					return false;
				Huffman lit, dist;
				if (!BuildHuffman(lit, ll, nlen) || !BuildHuffman(dist, ll + nlen, ndist))
					return false;
				if (!InflateCodes(br, out, lit, dist, maxOutput, cancel))
					return false;
			}
			else
				return false;
			if (out.size() > maxOutput)
				return false;
		} while (!last);
		if (consumed)
			*consumed = br.pos;
		return true;
	}

	bool Inflate(const uint8_t* data, size_t size, std::vector<uint8_t>& out, size_t expectedSize, uint64_t maxOutput, std::atomic<bool>* cancel)
	{
		return InflateImpl(data, size, out, expectedSize, maxOutput, nullptr, cancel);
	}

	bool Gunzip(const uint8_t* data, size_t size, std::vector<uint8_t>& out, uint64_t maxOutput, std::atomic<bool>* cancel)
	{
		out.clear();
		size_t pos = 0;
		bool any = false;
		while (pos + 18 <= size)
		{
			if (cancel && cancel->load())
				return false;
			if (data[pos] != 0x1f || data[pos + 1] != 0x8b || data[pos + 2] != 8)
				break; // trailing garbage after the last member is tolerated
			const uint8_t flags = data[pos + 3];
			size_t p = pos + 10;
			if (flags & 4)
			{
				if (p + 2 > size)
					return false;
				p += 2u + RdU16(data + p);
			}
			if (flags & 8)
			{
				while (p < size && data[p])
					++p;
				++p;
			}
			if (flags & 16)
			{
				while (p < size && data[p])
					++p;
				++p;
			}
			if (flags & 2)
				p += 2;
			if (p >= size)
				return false;
			std::vector<uint8_t> part;
			size_t used = 0;
			if (!InflateImpl(data + p, size - p, part, 0, maxOutput - out.size(), &used, cancel))
				return false;
			const size_t trailer = p + used;
			if (trailer + 8 > size)
				return false;
			if (RdU32(data + trailer) != image::Crc32(part.data(), part.size()) || RdU32(data + trailer + 4) != (uint32_t)(part.size() & 0xFFFFFFFFu))
				return false;
			out.insert(out.end(), part.begin(), part.end());
			if (out.size() > maxOutput)
				return false;
			pos = trailer + 8;
			any = true;
		}
		return any;
	}

	std::string SafeEntryPath(const std::string& name, int stripComponents)
	{
		std::string n = name;
		std::replace(n.begin(), n.end(), '\\', '/');
		if (n.empty() || n[0] == '/' || (n.size() > 1 && n[1] == ':'))
			return {};
		std::vector<std::string> parts;
		for (const std::string& c : str::Split(n, '/', false))
		{
			if (c == ".")
				continue;
			if (c == "..")
				return {};
			if (c.find('\0') != std::string::npos)
				return {};
			for (char ch : c)
				if ((unsigned char)ch < 32 || ch == ':' || ch == '*' || ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|')
					return {};
			if (IsReservedWindowsName(c))
				return {};
			parts.push_back(c);
		}
		if ((int)parts.size() <= stripComponents)
			return {};
		return str::Join(std::vector<std::string>(parts.begin() + stripComponents, parts.end()), "/");
	}

	// ----------------------------------------------------------------------------------------------------
	// ZIP
	// ----------------------------------------------------------------------------------------------------
	Status ZipReader::Open(const std::string& path)
	{
		std::vector<uint8_t> bytes;
		if (!fs::ReadBinary(path, bytes))
			return Status::Fail("cannot read " + path);
		return OpenMemory(std::move(bytes));
	}

	Status ZipReader::OpenMemory(std::vector<uint8_t> bytes)
	{
		data_ = std::move(bytes);
		entries_.clear();
		const size_t n = data_.size();
		if (n < 22)
			return Status::Fail("not a zip archive");
		// find end of central directory
		size_t eocd = std::string::npos;
		const size_t minPos = n > (65535 + 22) ? n - (65535 + 22) : 0;
		for (size_t i = n - 22 + 1; i-- > minPos;)
		{
			if (RdU32(&data_[i]) == 0x06054b50u)
			{
				eocd = i;
				break;
			}
		}
		if (eocd == std::string::npos)
			return Status::Fail("zip: end of central directory not found");
		uint64_t total = RdU16(&data_[eocd + 10]);
		uint64_t cdSize = RdU32(&data_[eocd + 12]);
		uint64_t cdOffset = RdU32(&data_[eocd + 16]);
		if (total == 0xFFFF || cdOffset == 0xFFFFFFFFu || cdSize == 0xFFFFFFFFu)
		{
			// ZIP64 locator
			if (eocd >= 20 && RdU32(&data_[eocd - 20]) == 0x07064b50u)
			{
				const uint64_t z64 = RdU64(&data_[eocd - 20 + 8]);
				if (z64 + 56 <= n && RdU32(&data_[z64]) == 0x06064b50u)
				{
					total = RdU64(&data_[z64 + 32]);
					cdSize = RdU64(&data_[z64 + 40]);
					cdOffset = RdU64(&data_[z64 + 48]);
				}
			}
		}
		if (cdOffset + cdSize > n)
			return Status::Fail("zip: corrupt central directory");
		size_t p = (size_t)cdOffset;
		for (uint64_t i = 0; i < total; ++i)
		{
			if (p + 46 > n || RdU32(&data_[p]) != 0x02014b50u)
				return Status::Fail("zip: bad central directory entry");
			ZipEntry e;
			e.method = RdU16(&data_[p + 10]);
			e.crc32 = RdU32(&data_[p + 16]);
			e.compressedSize = RdU32(&data_[p + 20]);
			e.size = RdU32(&data_[p + 24]);
			const uint16_t nameLen = RdU16(&data_[p + 28]);
			const uint16_t extraLen = RdU16(&data_[p + 30]);
			const uint16_t commentLen = RdU16(&data_[p + 32]);
			e.externalAttr = RdU32(&data_[p + 38]);
			e.localHeaderOffset = RdU32(&data_[p + 42]);
			if (p + 46 + nameLen + extraLen + commentLen > n)
				return Status::Fail("zip: truncated entry");
			e.name.assign((const char*)&data_[p + 46], nameLen);
			// ZIP64 extra field
			size_t x = p + 46 + nameLen;
			const size_t xend = x + extraLen;
			while (x + 4 <= xend)
			{
				const uint16_t id = RdU16(&data_[x]);
				const uint16_t sz = RdU16(&data_[x + 2]);
				if (id == 1)
				{
					size_t q = x + 4;
					if (e.size == 0xFFFFFFFFu && q + 8 <= xend) { e.size = RdU64(&data_[q]); q += 8; }
					if (e.compressedSize == 0xFFFFFFFFu && q + 8 <= xend) { e.compressedSize = RdU64(&data_[q]); q += 8; }
					if (e.localHeaderOffset == 0xFFFFFFFFu && q + 8 <= xend) { e.localHeaderOffset = RdU64(&data_[q]); q += 8; }
				}
				x += 4 + sz;
			}
			entries_.push_back(std::move(e));
			p += 46 + nameLen + extraLen + commentLen;
		}
		return Status::Ok();
	}

	const ZipEntry* ZipReader::Find(const std::string& name) const
	{
		for (const ZipEntry& e : entries_)
			if (e.name == name)
				return &e;
		return nullptr;
	}

	Status ZipReader::Read(const ZipEntry& e, std::vector<uint8_t>& out, std::atomic<bool>* cancel) const
	{
		if (cancel && cancel->load())
			return Status::Fail("cancelled");
		const size_t n = data_.size();
		const size_t lh = (size_t)e.localHeaderOffset;
		if (lh + 30 > n || RdU32(&data_[lh]) != 0x04034b50u)
			return Status::Fail("zip: bad local header for " + e.name);
		const size_t start = lh + 30 + RdU16(&data_[lh + 26]) + RdU16(&data_[lh + 28]);
		if (start + e.compressedSize > n)
			return Status::Fail("zip: truncated data for " + e.name);
		if (e.method == 0)
			out.assign(data_.begin() + (ptrdiff_t)start, data_.begin() + (ptrdiff_t)(start + e.compressedSize));
		else if (e.method == 8)
		{
			if (!Inflate(&data_[start], (size_t)e.compressedSize, out, (size_t)e.size, e.size + 1024, cancel))
				return cancel && cancel->load() ? Status::Fail("cancelled") : Status::Fail("zip: inflate failed for " + e.name);
		}
		else
			return Status::Fail("zip: unsupported compression method " + std::to_string(e.method));
		if (out.size() != e.size)
			return Status::Fail("zip: size mismatch for " + e.name);
		if (image::Crc32(out.data(), out.size()) != e.crc32)
			return Status::Fail("zip: checksum mismatch for " + e.name);
		return cancel && cancel->load() ? Status::Fail("cancelled") : Status::Ok();
	}

	Status ExtractZip(const std::string& zipPath, const std::string& destDir, const ExtractOptions& options)
	{
		if (options.cancel && options.cancel->load())
			return Status::Fail("cancelled");
		ZipReader zr;
		Status s = zr.Open(zipPath);
		if (!s)
			return s;
		if (zr.Entries().size() > options.maxFiles)
			return Status::Fail("zip: too many entries");
		uint64_t total = 0;
		for (const ZipEntry& e : zr.Entries())
			total += e.size;
		if (total > options.maxTotalBytes)
			return Status::Fail("zip: archive would extract to more than the allowed size");
		fs::CreateDirs(destDir);
		for (const ZipEntry& e : zr.Entries())
		{
			if (options.cancel && options.cancel->load())
				return Status::Fail("cancelled");
			const std::string rel = SafeEntryPath(e.name, options.stripComponents);
			if (rel.empty())
			{
				if (!e.name.empty() && !SafeEntryPath(e.name, 0).empty())
					continue; // merely stripped away
				if (e.name.empty() || e.IsDirectory())
					continue;
				return Status::Fail("zip: unsafe entry name \"" + e.name + "\"");
			}
			if (options.filter && !options.filter(rel))
				continue;
			if (e.IsDirectory())
			{
				fs::CreateDirs(fs::Join(destDir, rel));
				continue;
			}
			if (e.IsSymlink())
				continue; // symlinks inside archives are never materialised
			std::vector<uint8_t> content;
			s = zr.Read(e, content, options.cancel);
			if (!s)
				return options.cancel && options.cancel->load() ? Status::Fail("cancelled") : s;
			if (!WriteExtracted(destDir, rel, content.data(), content.size(), e.IsExecutable(), options.overwrite))
				return Status::Fail("zip: cannot write " + rel);
			if (options.onFile)
				options.onFile(rel);
		}
		return options.cancel && options.cancel->load() ? Status::Fail("cancelled") : Status::Ok();
	}

	// ----------------------------------------------------------------------------------------------------
	// TAR
	// ----------------------------------------------------------------------------------------------------
	namespace
	{
		uint64_t ParseOctal(const char* p, size_t n)
		{
			// GNU base-256 encoding
			if ((unsigned char)p[0] & 0x80)
			{
				uint64_t v = (unsigned char)p[0] & 0x7f;
				for (size_t i = 1; i < n; ++i)
					v = (v << 8) | (unsigned char)p[i];
				return v;
			}
			uint64_t v = 0;
			size_t i = 0;
			while (i < n && (p[i] == ' ' || p[i] == 0))
				++i;
			for (; i < n && p[i] >= '0' && p[i] <= '7'; ++i)
				v = (v << 3) | (uint64_t)(p[i] - '0');
			return v;
		}

		std::string CStr(const char* p, size_t n)
		{
			size_t len = 0;
			while (len < n && p[len])
				++len;
			return std::string(p, len);
		}
	} // namespace

	Status ExtractTar(const std::string& tarPath, const std::string& destDir, const ExtractOptions& options)
	{
		std::vector<uint8_t> raw;
		if (options.cancel && options.cancel->load())
			return Status::Fail("cancelled");
		if (!fs::ReadBinary(tarPath, raw))
			return Status::Fail("cannot read " + tarPath);
		std::vector<uint8_t> tar;
		if (raw.size() > 2 && raw[0] == 0x1f && raw[1] == 0x8b)
		{
			if (!Gunzip(raw.data(), raw.size(), tar, options.maxTotalBytes, options.cancel))
				return options.cancel && options.cancel->load() ? Status::Fail("cancelled") : Status::Fail("gzip: corrupt or too large: " + tarPath);
			raw.clear();
			raw.shrink_to_fit();
		}
		else
			tar = std::move(raw);

		fs::CreateDirs(destDir);
		size_t pos = 0;
		uint64_t files = 0, bytes = 0;
		std::string longName;
		while (pos + 512 <= tar.size())
		{
			if (options.cancel && options.cancel->load())
				return Status::Fail("cancelled");
			const char* h = (const char*)&tar[pos];
			bool zero = true;
			for (int i = 0; i < 512 && zero; ++i)
				zero = h[i] == 0;
			if (zero)
				break;
			// header checksum
			uint64_t sum = 0;
			for (int i = 0; i < 512; ++i)
				sum += (i >= 148 && i < 156) ? 32 : (unsigned char)h[i];
			if (sum != ParseOctal(h + 148, 8))
				return Status::Fail("tar: bad header checksum");
			std::string name = CStr(h, 100);
			const uint64_t size = ParseOctal(h + 124, 12);
			const uint64_t mode = ParseOctal(h + 100, 8);
			const char type = h[156];
			if (CStr(h + 257, 5) == "ustar")
			{
				const std::string prefix = CStr(h + 345, 155);
				if (!prefix.empty())
					name = prefix + "/" + name;
			}
			const size_t dataStart = pos + 512;
			if (dataStart + size > tar.size())
				return Status::Fail("tar: truncated archive");
			const size_t next = dataStart + (size_t)((size + 511) & ~511ull);

			if (type == 'L') // GNU long name
			{
				longName = CStr((const char*)&tar[dataStart], (size_t)size);
				pos = next;
				continue;
			}
			if (type == 'x' || type == 'g') // pax headers: honour "path="
			{
				const std::string pax((const char*)&tar[dataStart], (size_t)size);
				size_t q = 0;
				while (q < pax.size())
				{
					const size_t sp = pax.find(' ', q);
					if (sp == std::string::npos)
						break;
					int64_t len = 0;
					str::ParseInt(pax.substr(q, sp - q), len);
					if (len <= 0 || q + (size_t)len > pax.size())
						break;
					const std::string rec = pax.substr(sp + 1, (size_t)len - (sp + 1 - q) - 1);
					if (type == 'x' && str::StartsWith(rec, "path="))
						longName = rec.substr(5);
					q += (size_t)len;
				}
				pos = next;
				continue;
			}
			if (!longName.empty())
			{
				name = longName;
				longName.clear();
			}
			pos = next;

			const std::string rel = SafeEntryPath(name, options.stripComponents);
			if (rel.empty())
			{
				if (SafeEntryPath(name, 0).empty() && !name.empty() && name != "./")
				{
					// "./" and stripped roots are harmless; anything else is an attack or garbage
					std::string t = name;
					std::replace(t.begin(), t.end(), '\\', '/');
					if (t.find("..") != std::string::npos || (!t.empty() && t[0] == '/'))
						return Status::Fail("tar: unsafe entry name \"" + name + "\"");
				}
				continue;
			}
			if (options.filter && !options.filter(rel))
				continue;
			if (type == '5')
			{
				fs::CreateDirs(fs::Join(destDir, rel));
				continue;
			}
			if (type != '0' && type != 0 && type != '7')
				continue; // symlinks, hard links, devices: never extracted
			bytes += size;
			if (++files > options.maxFiles || bytes > options.maxTotalBytes)
				return Status::Fail("tar: archive too large");
			if (!WriteExtracted(destDir, rel, &tar[dataStart], (size_t)size, (mode & 0111) != 0, options.overwrite))
				return Status::Fail("tar: cannot write " + rel);
			if (options.onFile)
				options.onFile(rel);
		}
		return options.cancel && options.cancel->load() ? Status::Fail("cancelled") : Status::Ok();
	}

	Status Extract(const std::string& archivePath, const std::string& destDir, const ExtractOptions& options)
	{
		const std::string lower = str::ToLower(archivePath);
		if (str::EndsWith(lower, ".zip") || str::EndsWith(lower, ".nupkg") || str::EndsWith(lower, ".vsix"))
			return ExtractZip(archivePath, destDir, options);
		if (str::EndsWith(lower, ".tar") || str::EndsWith(lower, ".tar.gz") || str::EndsWith(lower, ".tgz"))
			return ExtractTar(archivePath, destDir, options);
		// Everything else is delegated to the system tar (bsdtar understands xz, bz2, zstd, 7z, ...).
		const std::string tarExe = FindExecutable("tar");
		if (tarExe.empty())
			return Status::Fail("no extractor available for " + fs::FileName(archivePath) + " (system tar not found)");
		fs::CreateDirs(destDir);
		ProcessOptions po;
		po.program = tarExe;
		po.args = {"-xf", fs::ToNative(archivePath), "-C", fs::ToNative(destDir)};
		po.cancel = options.cancel;
		if (options.stripComponents > 0)
		{
			po.args.push_back("--strip-components");
			po.args.push_back(std::to_string(options.stripComponents));
		}
		po.timeoutMs = 20u * 60u * 1000u;
		const ProcessResult r = RunProcess(po);
		if (!r.started)
			return Status::Fail("cannot start tar");
		if (r.cancelled)
			return Status::Fail("cancelled");
		if (r.exitCode != 0)
			return Status::Fail("tar failed: " + r.output);
		return Status::Ok();
	}

	// ----------------------------------------------------------------------------------------------------
	// ZIP writer (stored entries)
	// ----------------------------------------------------------------------------------------------------
	void ZipWriter::AddFile(const std::string& name, const void* data, size_t size, bool executable)
	{
		Item it;
		it.name = name;
		it.size = size;
		it.crc = image::Crc32((const uint8_t*)data, size);
		it.offset = data_.size();
		it.attr = ((executable ? 0100755u : 0100644u) << 16);
		WrU32(data_, 0x04034b50u);
		WrU16(data_, 20);
		WrU16(data_, 0x0800); // UTF-8 names
		WrU16(data_, 0);
		WrU16(data_, 0);
		WrU16(data_, 0x21); // 1980-01-01
		WrU32(data_, it.crc);
		WrU32(data_, (uint32_t)size);
		WrU32(data_, (uint32_t)size);
		WrU16(data_, (uint16_t)name.size());
		WrU16(data_, 0);
		data_.insert(data_.end(), name.begin(), name.end());
		data_.insert(data_.end(), (const uint8_t*)data, (const uint8_t*)data + size);
		items_.push_back(std::move(it));
	}

	Status ZipWriter::AddDiskFile(const std::string& name, const std::string& diskPath)
	{
		std::vector<uint8_t> bytes;
		if (!fs::ReadBinary(diskPath, bytes))
			return Status::Fail("cannot read " + diskPath);
		AddFile(name, bytes.data(), bytes.size());
		return Status::Ok();
	}

	std::vector<uint8_t> ZipWriter::Finish()
	{
		std::vector<uint8_t> out = data_;
		const size_t cdStart = out.size();
		for (const Item& it : items_)
		{
			WrU32(out, 0x02014b50u);
			WrU16(out, 0x031e);
			WrU16(out, 20);
			WrU16(out, 0x0800);
			WrU16(out, 0);
			WrU16(out, 0);
			WrU16(out, 0x21);
			WrU32(out, it.crc);
			WrU32(out, (uint32_t)it.size);
			WrU32(out, (uint32_t)it.size);
			WrU16(out, (uint16_t)it.name.size());
			WrU16(out, 0);
			WrU16(out, 0);
			WrU16(out, 0);
			WrU16(out, 0);
			WrU32(out, it.attr);
			WrU32(out, (uint32_t)it.offset);
			out.insert(out.end(), it.name.begin(), it.name.end());
		}
		const size_t cdSize = out.size() - cdStart;
		WrU32(out, 0x06054b50u);
		WrU16(out, 0);
		WrU16(out, 0);
		WrU16(out, (uint16_t)items_.size());
		WrU16(out, (uint16_t)items_.size());
		WrU32(out, (uint32_t)cdSize);
		WrU32(out, (uint32_t)cdStart);
		WrU16(out, 0);
		return out;
	}

	Status ZipWriter::Save(const std::string& path)
	{
		const std::vector<uint8_t> bytes = Finish();
		if (!fs::WriteAtomic(path, bytes.data(), bytes.size()))
			return Status::Fail("cannot write " + path);
		return Status::Ok();
	}

} // namespace lumen::archive
