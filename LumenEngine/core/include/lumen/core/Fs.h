// Lumen Engine — file system helpers.
//
// All paths handled by Lumen are UTF-8 std::string with forward slashes, also on Windows.
// Functions never throw: failures are reported through the return value.
#pragma once

#include "lumen/core/Common.h"

#include <filesystem>

namespace lumen::fs
{
	// --- path <-> string -------------------------------------------------------------------------
	std::filesystem::path ToPath(const std::string& utf8);
	std::string FromPath(const std::filesystem::path& p);
	// Path in the native separator style (backslashes on Windows) for passing to external tools.
	std::string ToNative(const std::string& path);

	// --- lexical path operations -----------------------------------------------------------------
	// Converts separators to '/', removes "." and resolves ".." lexically, strips trailing separators.
	std::string Normalize(const std::string& path);
	std::string Join(const std::string& a, const std::string& b);
	template <typename... Rest>
	std::string Join(const std::string& a, const std::string& b, const Rest&... rest)
	{
		return Join(Join(a, b), rest...);
	}
	std::string Parent(const std::string& path);
	std::string FileName(const std::string& path);      // "dir/a.tar.gz" -> "a.tar.gz"
	std::string Stem(const std::string& path);          // "dir/a.tar.gz" -> "a.tar"
	std::string Extension(const std::string& path);     // "dir/a.tar.gz" -> ".gz"
	std::string ExtensionLower(const std::string& path);
	std::string ReplaceExtension(const std::string& path, const std::string& newExtension);
	bool IsAbsolute(const std::string& path);
	std::string Absolute(const std::string& path);      // makes absolute relative to the current directory
	std::string Canonical(const std::string& path);     // resolves symlinks; falls back to Absolute
	std::string CurrentDir();
	// Relative path of `path` with respect to `base` (lexical). Returns `path` if no relative form exists.
	std::string Relative(const std::string& path, const std::string& base);
	// True when `path` equals `base` or lies inside it (lexical, case-insensitive on Windows).
	bool IsSubPath(const std::string& path, const std::string& base);
	bool PathEquals(const std::string& a, const std::string& b);
	// Replaces characters that are invalid in file names.
	std::string SanitizeFileName(const std::string& name);

	// --- queries -----------------------------------------------------------------------------------
	bool Exists(const std::string& path);
	bool IsFile(const std::string& path);
	bool IsDirectory(const std::string& path);
	bool IsEmptyDirectory(const std::string& path);
	uint64_t FileSize(const std::string& path);
	// Last modification time in milliseconds since the Unix epoch (0 on failure).
	uint64_t LastWriteTimeMs(const std::string& path);
	bool IsWritableDirectory(const std::string& dir);

	// --- reading / writing -------------------------------------------------------------------------
	bool ReadText(const std::string& path, std::string& out);
	bool ReadBinary(const std::string& path, std::vector<uint8_t>& out);
	bool WriteText(const std::string& path, std::string_view text);
	bool WriteBinary(const std::string& path, const void* data, size_t size);
	// Writes to a temporary sibling file and renames it over the target.
	bool WriteAtomic(const std::string& path, const void* data, size_t size);
	// Writes only when the content differs from what is on disk. Returns true on success;
	// `changed` (optional) tells whether the file was touched.
	bool WriteTextIfChanged(const std::string& path, std::string_view text, bool* changed = nullptr);

	// --- directories ---------------------------------------------------------------------------------
	bool CreateDirs(const std::string& dir);
	bool Remove(const std::string& path);            // file or empty directory
	bool RemoveAll(const std::string& path);         // recursive
	bool Rename(const std::string& from, const std::string& to);
	bool CopyFile(const std::string& from, const std::string& to, bool overwrite = true);
	// Copies a directory tree. `filter` may veto individual files (relative path passed).
	bool CopyDir(const std::string& from, const std::string& to, bool overwrite = true,
		const std::function<bool(const std::string& relativePath)>& filter = {});
	std::string CreateTempDir(const std::string& prefix);
	// "<prefix>-<hex>" unique for this process and time.
	std::string UniqueName(const std::string& prefix);

	struct DirEntry
	{
		std::string path;   // normalized full path
		bool isDirectory = false;
		uint64_t size = 0;
		uint64_t mtimeMs = 0;
	};

	struct ListOptions
	{
		bool recursive = false;
		bool includeHidden = true;
		bool followSymlinks = false;
		int maxDepth = 64;
		// Directories whose file name matches one of these globs are not entered / listed.
		std::vector<std::string> ignore;
	};

	std::vector<DirEntry> ListDir(const std::string& dir, const ListOptions& options = {});
	// Calls `visitor` for every entry; return false to stop the walk.
	void Walk(const std::string& dir, const ListOptions& options, const std::function<bool(const DirEntry&)>& visitor);

	// Glob matching with '*', '**', '?' and '[abc]' / '[a-z]' classes. Matching is performed on '/'-separated
	// paths; '*' does not cross a '/', '**' does.
	bool GlobMatch(std::string_view pattern, std::string_view path);
	bool MatchesAny(const std::vector<std::string>& patterns, std::string_view path);

	// Searches `startDir` and its parents for a file called `fileName`; returns "" when not found.
	std::string FindUp(const std::string& startDir, const std::string& fileName);
	// First regular file in `dir` (non recursive) with the given extension (".lum").
	std::string FindFileWithExtension(const std::string& dir, const std::string& extension);

} // namespace lumen::fs
