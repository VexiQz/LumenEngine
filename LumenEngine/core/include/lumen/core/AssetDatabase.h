// Lumen Engine — project asset database.
//
// Indexes every file of a project (including downloaded packages) so the editor's library can show them,
// gives each asset a stable GUID stored in a `<file>.meta` sidecar, tracks changes by polling, supports
// fuzzy search, and keeps `.meta` files in sync when assets are renamed, moved or deleted through the API.
#pragma once

#include "lumen/core/Common.h"
#include "lumen/core/Json.h"
#include "lumen/core/Project.h"

#include <map>
#include <unordered_map>

namespace lumen
{
	enum class AssetType : uint8_t
	{
		Folder,
		Scene,
		Prefab,
		Script,      // .lua
		Native,      // C / C++ sources and headers of the game module
		Shader,
		Material,    // .lmat
		Texture,
		Model,
		Audio,
		Video,
		Font,
		Animation,   // .lanim clips, .lanimctl animator controllers
		Data,        // json / csv / ini / yaml / ...
		Package,     // package manifest (.lpkg) or file inside packages/
		Text,
		Other
	};

	const char* AssetTypeName(AssetType type);
	AssetType AssetTypeFromName(const std::string& name);
	AssetType ClassifyAsset(const std::string& path, bool isDirectory);

	enum class ShaderLanguage : uint8_t { Unknown, Hlsl, Glsl, Lumen /* .lshader descriptor */ };
	enum class ShaderKind : uint8_t
	{
		Unknown, Include, Vertex, Pixel, Compute, Geometry, Hull, Domain, Mesh, Amplification,
		RayGen, ClosestHit, AnyHit, Miss, Intersection, Callable, Library, Descriptor
	};
	struct ShaderInfo
	{
		ShaderLanguage language = ShaderLanguage::Unknown;
		ShaderKind kind = ShaderKind::Unknown;
	};
	// Works on the file name only: ".vert/.frag/.comp/.geom/.tesc/.tese/.mesh/.task/.rgen/..." (GLSL),
	// "*VS.hlsl / name.ps.hlsl / name_cs.hlsl" (HLSL), ".hlsli/.glsli/.inc" includes, ".lshader" descriptors.
	ShaderInfo ClassifyShader(const std::string& path);
	const char* ShaderKindName(ShaderKind kind);

	struct AssetEntry
	{
		std::string path;      // project relative, '/' separated ("textures/grass.png"); "" is the project root
		std::string name;      // file name with extension
		std::string extension; // lower case including the dot ("" for folders)
		AssetType type = AssetType::Other;
		bool isDirectory = false;
		bool inPackage = false;  // lives under packages/
		uint64_t size = 0;
		uint64_t mtimeMs = 0;
		std::string guid;        // empty for folders without .meta and for the root
		ShaderInfo shader;       // valid when type == Shader
	};

	struct AssetChange
	{
		enum class Kind { Added, Removed, Modified, Moved };
		Kind kind = Kind::Added;
		std::string path;     // new path (Added/Modified/Moved) or old path (Removed)
		std::string oldPath;  // Moved only
		AssetType type = AssetType::Other;
		std::string guid;
	};

	struct AssetSearchOptions
	{
		std::string query;
		std::vector<AssetType> types;   // empty: all
		bool includePackages = true;
		bool includeFolders = false;
		size_t limit = 200;
	};

	class AssetDatabase
	{
	public:
		// Scans the project and creates missing .meta files. Call Refresh() later to pick up changes.
		Status Open(const Project& project);
		void Close();
		bool IsOpen() const { return open_; }
		const Project& GetProject() const { return project_; }

		// Re-scans the disk. Returns the changes since the last scan (also on first call after Open: empty).
		std::vector<AssetChange> Refresh();

		const std::vector<AssetEntry>& Entries() const { return entries_; }
		const AssetEntry* Find(const std::string& path) const;
		const AssetEntry* FindByGuid(const std::string& guid) const;
		std::string GuidOf(const std::string& path) const;
		std::string PathOfGuid(const std::string& guid) const;

		// Direct children of a folder ("" = project root); folders first, then by name.
		std::vector<const AssetEntry*> Children(const std::string& folder, bool includePackages = true) const;
		std::vector<const AssetEntry*> OfType(AssetType type) const;
		std::vector<const AssetEntry*> Search(const AssetSearchOptions& options) const;

		// --- file operations that keep .meta sidecars consistent ---------------------------------------
		Status CreateFolder(const std::string& path);
		Status CreateAssetFile(const std::string& path, const std::string& content);
		Status Rename(const std::string& path, const std::string& newName);
		Status Move(const std::string& path, const std::string& newFolder);
		Status Duplicate(const std::string& path, std::string* newPath = nullptr);
		Status Delete(const std::string& path);
		Status Import(const std::string& externalFile, const std::string& folder, std::string* newPath = nullptr);

		// --- importer settings stored in .meta -------------------------------------------------------------
		Json GetImportSettings(const std::string& path) const;
		Status SetImportSettings(const std::string& path, const Json& settings);

		// --- references -----------------------------------------------------------------------------------
		// Assets referenced by `path` (scene/material/prefab/animation JSON, Lua require/load calls, shader
		// includes), project relative. Unresolvable strings are ignored.
		std::vector<std::string> Dependencies(const std::string& path) const;
		// Assets that reference `path`.
		std::vector<std::string> Dependents(const std::string& path) const;
		// Assets nothing references (candidates for cleanup); scripts/scenes/shaders are never reported.
		std::vector<std::string> Unreferenced() const;

		struct Stats
		{
			size_t files = 0;
			size_t folders = 0;
			uint64_t bytes = 0;
			std::map<AssetType, size_t> perType;
		};
		Stats GetStats() const;

	private:
		struct Fingerprint
		{
			uint64_t size = 0;
			uint64_t mtime = 0;
			bool dir = false;
		};
		void Scan(std::vector<AssetEntry>& out) const;
		std::string EnsureMeta(const AssetEntry& e) const;
		static std::string MetaPathFor(const std::string& absolute) { return absolute + ".meta"; }
		void RebuildIndex();
		std::vector<std::string> ScanDependencies(const AssetEntry& e) const;
		void BuildReverseDeps() const;

		bool open_ = false;
		Project project_;
		std::vector<AssetEntry> entries_;
		std::unordered_map<std::string, size_t> byPath_;
		std::unordered_map<std::string, size_t> byGuid_;
		mutable std::unordered_map<std::string, std::vector<std::string>> reverseDeps_;
		mutable bool reverseDepsValid_ = false;
	};

} // namespace lumen
