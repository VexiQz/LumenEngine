#include "lumen/core/Fs.h"

#include "lumen/core/Platform.h"
#include "lumen/core/Strings.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>

namespace stdfs = std::filesystem;

namespace lumen::fs
{
	// ----------------------------------------------------------------------------------------------
	// path <-> string
	// ----------------------------------------------------------------------------------------------
	stdfs::path ToPath(const std::string& utf8)
	{
#if defined(__cpp_lib_char8_t) || (defined(_MSVC_LANG) && _MSVC_LANG > 201703L)
		return stdfs::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
#else
		return stdfs::u8path(utf8);
#endif
	}

	std::string FromPath(const stdfs::path& p)
	{
#if defined(__cpp_lib_char8_t) || (defined(_MSVC_LANG) && _MSVC_LANG > 201703L)
		const std::u8string s = p.generic_u8string();
		return std::string(reinterpret_cast<const char*>(s.data()), s.size());
#else
		return p.generic_u8string();
#endif
	}

	std::string ToNative(const std::string& path)
	{
#if defined(LUMEN_PLATFORM_WINDOWS)
		std::string out = path;
		std::replace(out.begin(), out.end(), '/', '\\');
		return out;
#else
		return path;
#endif
	}

	// ----------------------------------------------------------------------------------------------
	// lexical operations
	// ----------------------------------------------------------------------------------------------
	std::string Normalize(const std::string& path)
	{
		if (path.empty())
			return {};
		std::string tmp = path;
		std::replace(tmp.begin(), tmp.end(), '\\', '/');
		std::string out = FromPath(ToPath(tmp).lexically_normal());
		// lexically_normal keeps a trailing slash for directories; strip it except for roots ("/" or "C:/").
		while (out.size() > 1 && out.back() == '/' && !(out.size() == 3 && out[1] == ':'))
			out.pop_back();
		if (out.empty())
			out = ".";
		return out;
	}

	std::string Join(const std::string& a, const std::string& b)
	{
		if (a.empty())
			return Normalize(b);
		if (b.empty())
			return Normalize(a);
		if (IsAbsolute(b))
			return Normalize(b);
		return Normalize(a + "/" + b);
	}

	std::string Parent(const std::string& path)
	{
		const std::string n = Normalize(path);
		const size_t pos = n.find_last_of('/');
		if (pos == std::string::npos)
			return {};
		if (pos == 0)
			return "/";
		if (pos == 2 && n.size() > 2 && n[1] == ':')
			return n.substr(0, 3);
		return n.substr(0, pos);
	}

	std::string FileName(const std::string& path)
	{
		const std::string n = Normalize(path);
		const size_t pos = n.find_last_of('/');
		return pos == std::string::npos ? n : n.substr(pos + 1);
	}

	std::string Stem(const std::string& path)
	{
		const std::string name = FileName(path);
		const size_t dot = name.find_last_of('.');
		if (dot == std::string::npos || dot == 0)
			return name;
		return name.substr(0, dot);
	}

	std::string Extension(const std::string& path)
	{
		const std::string name = FileName(path);
		const size_t dot = name.find_last_of('.');
		if (dot == std::string::npos || dot == 0)
			return {};
		return name.substr(dot);
	}

	std::string ExtensionLower(const std::string& path)
	{
		return str::ToLower(Extension(path));
	}

	std::string ReplaceExtension(const std::string& path, const std::string& newExtension)
	{
		const std::string ext = Extension(path);
		std::string base = path.substr(0, path.size() - ext.size());
		if (!newExtension.empty() && newExtension[0] != '.')
			base += '.';
		return base + newExtension;
	}

	bool IsAbsolute(const std::string& path)
	{
		if (path.empty())
			return false;
		if (path[0] == '/' || path[0] == '\\')
			return true;
		return path.size() >= 3 && std::isalpha((unsigned char)path[0]) && path[1] == ':' && (path[2] == '/' || path[2] == '\\');
	}

	std::string CurrentDir()
	{
		std::error_code ec;
		const stdfs::path p = stdfs::current_path(ec);
		return ec ? std::string() : Normalize(FromPath(p));
	}

	std::string Absolute(const std::string& path)
	{
		if (path.empty())
			return CurrentDir();
		if (IsAbsolute(path))
			return Normalize(path);
		return Normalize(CurrentDir() + "/" + path);
	}

	std::string Canonical(const std::string& path)
	{
		std::error_code ec;
		const stdfs::path p = stdfs::weakly_canonical(ToPath(path), ec);
		if (ec)
			return Absolute(path);
		return Normalize(FromPath(p));
	}

	std::string Relative(const std::string& path, const std::string& base)
	{
		const std::string a = Normalize(path);
		const std::string b = Normalize(base);
		std::error_code ec;
		const stdfs::path rel = ToPath(a).lexically_relative(ToPath(b));
		if (rel.empty())
			return a;
		const std::string s = FromPath(rel);
		return s.empty() ? "." : s;
	}

	bool PathEquals(const std::string& a, const std::string& b)
	{
		const std::string na = Normalize(a);
		const std::string nb = Normalize(b);
#if defined(LUMEN_PLATFORM_WINDOWS)
		return str::EqualsIgnoreCase(na, nb);
#else
		return na == nb;
#endif
	}

	bool IsSubPath(const std::string& path, const std::string& base)
	{
		std::string a = Normalize(path);
		std::string b = Normalize(base);
#if defined(LUMEN_PLATFORM_WINDOWS)
		a = str::ToLower(a);
		b = str::ToLower(b);
#endif
		if (a == b)
			return true;
		if (b == "/" )
			return !a.empty() && a[0] == '/';
		if (a.size() <= b.size())
			return false;
		return a.compare(0, b.size(), b) == 0 && a[b.size()] == '/';
	}

	std::string SanitizeFileName(const std::string& name)
	{
		std::string out;
		out.reserve(name.size());
		for (const char c : name)
		{
			switch (c)
			{
			case '<': case '>': case ':': case '"': case '/': case '\\': case '|': case '?': case '*':
				out += '_';
				break;
			default:
				out += ((unsigned char)c < 0x20) ? '_' : c;
			}
		}
		while (!out.empty() && (out.back() == ' ' || out.back() == '.'))
			out.pop_back();
		if (out.empty())
			out = "_";
		return out;
	}

	// ----------------------------------------------------------------------------------------------
	// queries
	// ----------------------------------------------------------------------------------------------
	bool Exists(const std::string& path)
	{
		std::error_code ec;
		return stdfs::exists(ToPath(path), ec) && !ec;
	}

	bool IsFile(const std::string& path)
	{
		std::error_code ec;
		return stdfs::is_regular_file(ToPath(path), ec) && !ec;
	}

	bool IsDirectory(const std::string& path)
	{
		std::error_code ec;
		return stdfs::is_directory(ToPath(path), ec) && !ec;
	}

	bool IsEmptyDirectory(const std::string& path)
	{
		std::error_code ec;
		return stdfs::is_empty(ToPath(path), ec) && !ec;
	}

	uint64_t FileSize(const std::string& path)
	{
		std::error_code ec;
		const auto s = stdfs::file_size(ToPath(path), ec);
		return ec ? 0 : (uint64_t)s;
	}

	static uint64_t ToUnixMs(const stdfs::file_time_type& t)
	{
		using namespace std::chrono;
		const auto sctp = time_point_cast<system_clock::duration>(t - stdfs::file_time_type::clock::now() + system_clock::now());
		return (uint64_t)duration_cast<milliseconds>(sctp.time_since_epoch()).count();
	}

	uint64_t LastWriteTimeMs(const std::string& path)
	{
		std::error_code ec;
		const auto t = stdfs::last_write_time(ToPath(path), ec);
		return ec ? 0 : ToUnixMs(t);
	}

	bool IsWritableDirectory(const std::string& dir)
	{
		if (!IsDirectory(dir))
		{
			if (!CreateDirs(dir))
				return false;
		}
		const std::string probe = Join(dir, UniqueName(".lumen-write-test"));
		const bool ok = WriteText(probe, "x");
		Remove(probe);
		return ok;
	}

	// ----------------------------------------------------------------------------------------------
	// reading / writing
	// ----------------------------------------------------------------------------------------------
	bool ReadBinary(const std::string& path, std::vector<uint8_t>& out)
	{
		std::ifstream f(ToPath(path), std::ios::binary | std::ios::ate);
		if (!f)
			return false;
		const std::streamoff size = f.tellg();
		if (size < 0)
			return false;
		out.resize((size_t)size);
		f.seekg(0, std::ios::beg);
		if (size > 0)
			f.read(reinterpret_cast<char*>(out.data()), size);
		return (bool)f || f.eof();
	}

	bool ReadText(const std::string& path, std::string& out)
	{
		std::ifstream f(ToPath(path), std::ios::binary | std::ios::ate);
		if (!f)
			return false;
		const std::streamoff size = f.tellg();
		if (size < 0)
			return false;
		out.resize((size_t)size);
		f.seekg(0, std::ios::beg);
		if (size > 0)
			f.read(out.data(), size);
		// Strip a UTF-8 BOM.
		if (out.size() >= 3 && (unsigned char)out[0] == 0xEF && (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF)
			out.erase(0, 3);
		return (bool)f || f.eof();
	}

	bool WriteBinary(const std::string& path, const void* data, size_t size)
	{
		const std::string parent = Parent(path);
		if (!parent.empty() && !IsDirectory(parent))
			CreateDirs(parent);
		std::ofstream f(ToPath(path), std::ios::binary | std::ios::trunc);
		if (!f)
			return false;
		if (size > 0)
			f.write(static_cast<const char*>(data), (std::streamsize)size);
		f.flush();
		return (bool)f;
	}

	bool WriteText(const std::string& path, std::string_view text)
	{
		return WriteBinary(path, text.data(), text.size());
	}

	bool WriteAtomic(const std::string& path, const void* data, size_t size)
	{
		const std::string tmp = path + "." + UniqueName("tmp");
		if (!WriteBinary(tmp, data, size))
		{
			Remove(tmp);
			return false;
		}
		std::error_code ec;
		stdfs::rename(ToPath(tmp), ToPath(path), ec);
		if (ec)
		{
			// Windows can refuse to replace an open file; fall back to remove + rename.
			stdfs::remove(ToPath(path), ec);
			ec.clear();
			stdfs::rename(ToPath(tmp), ToPath(path), ec);
			if (ec)
			{
				Remove(tmp);
				return false;
			}
		}
		return true;
	}

	bool WriteTextIfChanged(const std::string& path, std::string_view text, bool* changed)
	{
		if (changed)
			*changed = false;
		if (IsFile(path) && FileSize(path) == text.size())
		{
			std::vector<uint8_t> bytes;
			if (ReadBinary(path, bytes) && bytes.size() == text.size() &&
				(text.empty() || std::memcmp(bytes.data(), text.data(), text.size()) == 0))
				return true;
		}
		if (!WriteText(path, text))
			return false;
		if (changed)
			*changed = true;
		return true;
	}

	// ----------------------------------------------------------------------------------------------
	// directories
	// ----------------------------------------------------------------------------------------------
	bool CreateDirs(const std::string& dir)
	{
		if (dir.empty())
			return false;
		std::error_code ec;
		if (stdfs::is_directory(ToPath(dir), ec))
			return true;
		stdfs::create_directories(ToPath(dir), ec);
		return !ec && IsDirectory(dir);
	}

	bool Remove(const std::string& path)
	{
		std::error_code ec;
		return stdfs::remove(ToPath(path), ec) && !ec;
	}

	bool RemoveAll(const std::string& path)
	{
		std::error_code ec;
		stdfs::remove_all(ToPath(path), ec);
		return !ec;
	}

	bool Rename(const std::string& from, const std::string& to)
	{
		std::error_code ec;
		stdfs::rename(ToPath(from), ToPath(to), ec);
		return !ec;
	}

	bool CopyFile(const std::string& from, const std::string& to, bool overwrite)
	{
		const std::string parent = Parent(to);
		if (!parent.empty() && !IsDirectory(parent))
			CreateDirs(parent);
		std::error_code ec;
		const auto opts = overwrite ? stdfs::copy_options::overwrite_existing : stdfs::copy_options::skip_existing;
		stdfs::copy_file(ToPath(from), ToPath(to), opts, ec);
		return !ec;
	}

	bool CopyDir(const std::string& from, const std::string& to, bool overwrite, const std::function<bool(const std::string&)>& filter)
	{
		if (!IsDirectory(from))
			return false;
		if (!CreateDirs(to))
			return false;
		bool ok = true;
		ListOptions opts;
		opts.recursive = true;
		Walk(from, opts, [&](const DirEntry& e) {
			const std::string rel = Relative(e.path, from);
			if (filter && !filter(rel))
				return true;
			const std::string dst = Join(to, rel);
			if (e.isDirectory)
			{
				if (!CreateDirs(dst))
					ok = false;
			}
			else if (!CopyFile(e.path, dst, overwrite))
			{
				ok = false;
			}
			return true;
		});
		return ok;
	}

	std::string UniqueName(const std::string& prefix)
	{
		static std::atomic<uint32_t> counter{0};
		const uint64_t t = platform::UnixTimeMs();
		const uint64_t h = str::Hash64(&t, sizeof(t), (uint64_t)platform::CurrentProcessId() * 1315423911ull + counter.fetch_add(1));
		return prefix + "-" + str::ToHex(h).substr(0, 12);
	}

	std::string CreateTempDir(const std::string& prefix)
	{
		const std::string base = platform::TempDir();
		for (int attempt = 0; attempt < 16; ++attempt)
		{
			const std::string dir = Join(base, UniqueName(prefix));
			std::error_code ec;
			if (stdfs::create_directories(ToPath(dir), ec) && !ec)
				return dir;
		}
		return {};
	}

	// ----------------------------------------------------------------------------------------------
	// listing
	// ----------------------------------------------------------------------------------------------
	static bool IsHiddenName(const std::string& name)
	{
		return !name.empty() && name[0] == '.' && name != "." && name != "..";
	}

	static bool IsIgnoredName(const ListOptions& options, const std::string& name)
	{
		for (const std::string& pattern : options.ignore)
		{
			if (GlobMatch(pattern, name))
				return true;
		}
		return false;
	}

	static bool WalkImpl(const std::string& dir, const ListOptions& options, int depth, const std::function<bool(const DirEntry&)>& visitor)
	{
		std::error_code ec;
		stdfs::directory_options dopts = stdfs::directory_options::skip_permission_denied;
		if (options.followSymlinks)
			dopts |= stdfs::directory_options::follow_directory_symlink;

		std::vector<DirEntry> entries;
		for (stdfs::directory_iterator it(ToPath(dir), dopts, ec), end; !ec && it != end; it.increment(ec))
		{
			const std::string full = Normalize(FromPath(it->path()));
			const std::string name = FileName(full);
			if (!options.includeHidden && IsHiddenName(name))
				continue;
			if (IsIgnoredName(options, name))
				continue;
			DirEntry e;
			e.path = full;
			std::error_code ec2;
			e.isDirectory = it->is_directory(ec2);
			if (!e.isDirectory)
			{
				e.size = (uint64_t)it->file_size(ec2);
				if (ec2)
					e.size = 0;
			}
			const auto lwt = it->last_write_time(ec2);
			e.mtimeMs = ec2 ? 0 : ToUnixMs(lwt);
			entries.push_back(std::move(e));
		}

		// Directories first, then files, each group sorted case-insensitively (stable listing for UI).
		std::sort(entries.begin(), entries.end(), [](const DirEntry& a, const DirEntry& b) {
			if (a.isDirectory != b.isDirectory)
				return a.isDirectory;
			return str::ToLower(FileName(a.path)) < str::ToLower(FileName(b.path));
		});

		for (const DirEntry& e : entries)
		{
			if (!visitor(e))
				return false;
			if (e.isDirectory && options.recursive && depth < options.maxDepth)
			{
				if (!WalkImpl(e.path, options, depth + 1, visitor))
					return false;
			}
		}
		return true;
	}

	void Walk(const std::string& dir, const ListOptions& options, const std::function<bool(const DirEntry&)>& visitor)
	{
		if (!IsDirectory(dir))
			return;
		WalkImpl(Normalize(dir), options, 0, visitor);
	}

	std::vector<DirEntry> ListDir(const std::string& dir, const ListOptions& options)
	{
		std::vector<DirEntry> out;
		Walk(dir, options, [&](const DirEntry& e) {
			out.push_back(e);
			return true;
		});
		return out;
	}

	// ----------------------------------------------------------------------------------------------
	// glob
	// ----------------------------------------------------------------------------------------------
	static bool MatchClass(std::string_view pattern, size_t& pi, char c)
	{
		// pattern[pi] == '['
		size_t i = pi + 1;
		bool negate = false;
		if (i < pattern.size() && (pattern[i] == '!' || pattern[i] == '^'))
		{
			negate = true;
			++i;
		}
		bool matched = false;
		bool first = true;
		while (i < pattern.size() && (pattern[i] != ']' || first))
		{
			first = false;
			char lo = pattern[i];
			char hi = lo;
			if (i + 2 < pattern.size() && pattern[i + 1] == '-' && pattern[i + 2] != ']')
			{
				hi = pattern[i + 2];
				i += 2;
			}
			if (c >= lo && c <= hi)
				matched = true;
			++i;
		}
		pi = i; // points at ']' (or end)
		return matched != negate;
	}

	static bool GlobImpl(std::string_view p, size_t pi, std::string_view s, size_t si)
	{
		while (pi < p.size())
		{
			const char pc = p[pi];
			if (pc == '*')
			{
				const bool doubleStar = pi + 1 < p.size() && p[pi + 1] == '*';
				size_t next = pi + 1;
				if (doubleStar)
				{
					while (next < p.size() && p[next] == '*')
						++next;
					// "**/" also matches zero directories.
					if (next < p.size() && p[next] == '/')
					{
						if (GlobImpl(p, next + 1, s, si))
							return true;
					}
					for (size_t k = si; k <= s.size(); ++k)
					{
						if (GlobImpl(p, next, s, k))
							return true;
					}
					return false;
				}
				for (size_t k = si; k <= s.size(); ++k)
				{
					if (GlobImpl(p, next, s, k))
						return true;
					if (k < s.size() && s[k] == '/')
						break;
				}
				return false;
			}
			if (si >= s.size())
				return false;
			if (pc == '?')
			{
				if (s[si] == '/')
					return false;
				++pi;
				++si;
				continue;
			}
			if (pc == '[')
			{
				size_t end = pi;
				if (!MatchClass(p, end, s[si]))
					return false;
				pi = end + 1;
				++si;
				continue;
			}
#if defined(LUMEN_PLATFORM_WINDOWS)
			if (std::tolower((unsigned char)pc) != std::tolower((unsigned char)s[si]))
				return false;
#else
			if (pc != s[si])
				return false;
#endif
			++pi;
			++si;
		}
		return si == s.size();
	}

	bool GlobMatch(std::string_view pattern, std::string_view path)
	{
		return GlobImpl(pattern, 0, path, 0);
	}

	bool MatchesAny(const std::vector<std::string>& patterns, std::string_view path)
	{
		for (const std::string& p : patterns)
		{
			if (GlobMatch(p, path))
				return true;
		}
		return false;
	}

	std::string FindUp(const std::string& startDir, const std::string& fileName)
	{
		std::string dir = Absolute(startDir);
		while (!dir.empty())
		{
			const std::string candidate = Join(dir, fileName);
			if (Exists(candidate))
				return candidate;
			const std::string parent = Parent(dir);
			if (parent.empty() || parent == dir)
				break;
			dir = parent;
		}
		return {};
	}

	std::string FindFileWithExtension(const std::string& dir, const std::string& extension)
	{
		ListOptions opts;
		for (const DirEntry& e : ListDir(dir, opts))
		{
			if (!e.isDirectory && str::EqualsIgnoreCase(Extension(e.path), extension))
				return e.path;
		}
		return {};
	}

} // namespace lumen::fs
