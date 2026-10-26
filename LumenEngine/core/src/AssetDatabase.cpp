#include "lumen/core/AssetDatabase.h"

#include "lumen/core/Fs.h"
#include "lumen/core/Log.h"
#include "lumen/core/Strings.h"

#include <algorithm>
#include <set>

namespace lumen
{
	namespace
	{
		struct ExtRow
		{
			const char* ext;
			AssetType type;
		};

		const ExtRow kExtensions[] = {
			{".scene", AssetType::Scene}, {".lum", AssetType::Data}, {".prefab", AssetType::Prefab},
			{".lua", AssetType::Script}, {".luau", AssetType::Script},
			{".c", AssetType::Native}, {".cc", AssetType::Native}, {".cpp", AssetType::Native}, {".cxx", AssetType::Native},
			{".h", AssetType::Native}, {".hh", AssetType::Native}, {".hpp", AssetType::Native}, {".hxx", AssetType::Native}, {".inl", AssetType::Native},
			{".hlsl", AssetType::Shader}, {".hlsli", AssetType::Shader}, {".glsl", AssetType::Shader}, {".glsli", AssetType::Shader},
			{".vert", AssetType::Shader}, {".frag", AssetType::Shader}, {".comp", AssetType::Shader}, {".geom", AssetType::Shader},
			{".tesc", AssetType::Shader}, {".tese", AssetType::Shader}, {".mesh", AssetType::Shader}, {".task", AssetType::Shader},
			{".rgen", AssetType::Shader}, {".rchit", AssetType::Shader}, {".rahit", AssetType::Shader}, {".rmiss", AssetType::Shader},
			{".rint", AssetType::Shader}, {".rcall", AssetType::Shader}, {".lshader", AssetType::Shader}, {".fx", AssetType::Shader},
			{".spv", AssetType::Shader}, {".wgsl", AssetType::Shader},
			{".lmat", AssetType::Material}, {".mtl", AssetType::Material},
			{".png", AssetType::Texture}, {".jpg", AssetType::Texture}, {".jpeg", AssetType::Texture}, {".tga", AssetType::Texture},
			{".bmp", AssetType::Texture}, {".dds", AssetType::Texture}, {".ktx", AssetType::Texture}, {".ktx2", AssetType::Texture},
			{".hdr", AssetType::Texture}, {".exr", AssetType::Texture}, {".psd", AssetType::Texture}, {".gif", AssetType::Texture},
			{".webp", AssetType::Texture}, {".tif", AssetType::Texture}, {".tiff", AssetType::Texture},
			{".gltf", AssetType::Model}, {".glb", AssetType::Model}, {".obj", AssetType::Model}, {".fbx", AssetType::Model},
			{".dae", AssetType::Model}, {".ply", AssetType::Model}, {".stl", AssetType::Model}, {".usd", AssetType::Model},
			{".usda", AssetType::Model}, {".usdc", AssetType::Model}, {".usdz", AssetType::Model}, {".3ds", AssetType::Model},
			{".blend", AssetType::Model}, {".wiscene", AssetType::Model},
			{".wav", AssetType::Audio}, {".ogg", AssetType::Audio}, {".mp3", AssetType::Audio}, {".flac", AssetType::Audio},
			{".opus", AssetType::Audio}, {".m4a", AssetType::Audio}, {".aif", AssetType::Audio}, {".aiff", AssetType::Audio},
			{".mp4", AssetType::Video}, {".webm", AssetType::Video}, {".mkv", AssetType::Video}, {".avi", AssetType::Video},
			{".mov", AssetType::Video}, {".ogv", AssetType::Video},
			{".ttf", AssetType::Font}, {".otf", AssetType::Font}, {".woff", AssetType::Font}, {".woff2", AssetType::Font},
			{".lanim", AssetType::Animation}, {".lanimctl", AssetType::Animation}, {".lanimmask", AssetType::Animation},
			{".json", AssetType::Data}, {".csv", AssetType::Data}, {".ini", AssetType::Data}, {".yaml", AssetType::Data},
			{".yml", AssetType::Data}, {".toml", AssetType::Data}, {".xml", AssetType::Data}, {".cfg", AssetType::Data},
			{".lpkg", AssetType::Package},
			{".txt", AssetType::Text}, {".md", AssetType::Text}, {".log", AssetType::Text}, {".cmake", AssetType::Text},
		};

		bool IsMeta(const std::string& name)
		{
			return str::EndsWith(name, ".meta");
		}

		std::string Dir(const std::string& rel)
		{
			const size_t slash = rel.rfind('/');
			return slash == std::string::npos ? std::string() : rel.substr(0, slash);
		}

		std::string JoinRel(const std::string& a, const std::string& b)
		{
			if (a.empty()) return b;
			if (b.empty()) return a;
			return a + "/" + b;
		}
	} // namespace

	const char* AssetTypeName(AssetType type)
	{
		switch (type)
		{
		case AssetType::Folder: return "folder";
		case AssetType::Scene: return "scene";
		case AssetType::Prefab: return "prefab";
		case AssetType::Script: return "script";
		case AssetType::Native: return "native";
		case AssetType::Shader: return "shader";
		case AssetType::Material: return "material";
		case AssetType::Texture: return "texture";
		case AssetType::Model: return "model";
		case AssetType::Audio: return "audio";
		case AssetType::Video: return "video";
		case AssetType::Font: return "font";
		case AssetType::Animation: return "animation";
		case AssetType::Data: return "data";
		case AssetType::Package: return "package";
		case AssetType::Text: return "text";
		default: return "other";
		}
	}

	AssetType AssetTypeFromName(const std::string& name)
	{
		for (int i = 0; i <= (int)AssetType::Other; ++i)
			if (name == AssetTypeName((AssetType)i))
				return (AssetType)i;
		return AssetType::Other;
	}

	AssetType ClassifyAsset(const std::string& path, bool isDirectory)
	{
		if (isDirectory)
			return AssetType::Folder;
		const std::string ext = fs::ExtensionLower(path);
		for (const ExtRow& row : kExtensions)
			if (ext == row.ext)
				return row.type;
		return AssetType::Other;
	}

	const char* ShaderKindName(ShaderKind kind)
	{
		switch (kind)
		{
		case ShaderKind::Include: return "include";
		case ShaderKind::Vertex: return "vertex";
		case ShaderKind::Pixel: return "pixel";
		case ShaderKind::Compute: return "compute";
		case ShaderKind::Geometry: return "geometry";
		case ShaderKind::Hull: return "hull/tess control";
		case ShaderKind::Domain: return "domain/tess eval";
		case ShaderKind::Mesh: return "mesh";
		case ShaderKind::Amplification: return "amplification/task";
		case ShaderKind::RayGen: return "ray generation";
		case ShaderKind::ClosestHit: return "closest hit";
		case ShaderKind::AnyHit: return "any hit";
		case ShaderKind::Miss: return "miss";
		case ShaderKind::Intersection: return "intersection";
		case ShaderKind::Callable: return "callable";
		case ShaderKind::Library: return "library";
		case ShaderKind::Descriptor: return "descriptor";
		default: return "unknown";
		}
	}

	namespace
	{
		ShaderKind KindFromSuffix(const std::string& s)
		{
			if (s == "vs" || s == "vert" || s == "vertex") return ShaderKind::Vertex;
			if (s == "ps" || s == "frag" || s == "fs" || s == "pixel" || s == "fragment") return ShaderKind::Pixel;
			if (s == "cs" || s == "comp" || s == "compute") return ShaderKind::Compute;
			if (s == "gs" || s == "geom" || s == "geometry") return ShaderKind::Geometry;
			if (s == "hs" || s == "tesc" || s == "hull") return ShaderKind::Hull;
			if (s == "ds" || s == "tese" || s == "domain") return ShaderKind::Domain;
			if (s == "ms" || s == "mesh") return ShaderKind::Mesh;
			if (s == "as" || s == "task" || s == "amplification") return ShaderKind::Amplification;
			if (s == "rgen") return ShaderKind::RayGen;
			if (s == "rchit") return ShaderKind::ClosestHit;
			if (s == "rahit") return ShaderKind::AnyHit;
			if (s == "rmiss") return ShaderKind::Miss;
			if (s == "rint") return ShaderKind::Intersection;
			if (s == "rcall") return ShaderKind::Callable;
			if (s == "lib" || s == "rt") return ShaderKind::Library;
			return ShaderKind::Unknown;
		}
	} // namespace

	ShaderInfo ClassifyShader(const std::string& path)
	{
		ShaderInfo info;
		const std::string ext = fs::ExtensionLower(path);
		const std::string name = fs::FileName(path);
		if (ext == ".lshader")
		{
			info.language = ShaderLanguage::Lumen;
			info.kind = ShaderKind::Descriptor;
			return info;
		}
		if (ext == ".hlsli" || ext == ".glsli" || ext == ".inc")
		{
			info.language = ext == ".hlsli" ? ShaderLanguage::Hlsl : ShaderLanguage::Glsl;
			info.kind = ShaderKind::Include;
			return info;
		}
		static const char* glslExt[] = {".vert", ".frag", ".comp", ".geom", ".tesc", ".tese", ".mesh", ".task", ".rgen", ".rchit", ".rahit", ".rmiss", ".rint", ".rcall"};
		for (const char* g : glslExt)
		{
			if (ext == g)
			{
				info.language = ShaderLanguage::Glsl;
				info.kind = KindFromSuffix(ext.substr(1));
				return info;
			}
		}
		if (ext == ".glsl" || ext == ".hlsl" || ext == ".fx")
		{
			info.language = ext == ".glsl" ? ShaderLanguage::Glsl : ShaderLanguage::Hlsl;
			const std::string stem = fs::Stem(name);
			// "name.ps.hlsl", "name_ps.hlsl"
			const size_t dot = stem.rfind('.');
			if (dot != std::string::npos)
				info.kind = KindFromSuffix(str::ToLower(stem.substr(dot + 1)));
			if (info.kind == ShaderKind::Unknown)
			{
				const size_t us = stem.rfind('_');
				if (us != std::string::npos)
					info.kind = KindFromSuffix(str::ToLower(stem.substr(us + 1)));
			}
			// Wicked style "objectPS.hlsl", "skyVS_dynamic.hlsl", "blur_gaussian_float1CS.hlsl": a token of the
			// stem that ends with two upper case stage letters.
			if (info.kind == ShaderKind::Unknown)
			{
				for (const std::string& token : str::Split(stem, '_', false))
				{
					if (token.size() > 2 && std::isupper((unsigned char)token[token.size() - 1]) && std::isupper((unsigned char)token[token.size() - 2]))
					{
						const ShaderKind k = KindFromSuffix(str::ToLower(token.substr(token.size() - 2)));
						if (k != ShaderKind::Unknown)
						{
							info.kind = k;
							break;
						}
					}
				}
			}
			if (info.kind == ShaderKind::Unknown && ext == ".glsl")
				info.kind = ShaderKind::Include;
			return info;
		}
		return info;
	}

	// ----------------------------------------------------------------------------------------------------
	// Scanning
	// ----------------------------------------------------------------------------------------------------
	void AssetDatabase::Scan(std::vector<AssetEntry>& out) const
	{
		out.clear();
		const std::string root = project_.Root();
		fs::ListOptions lo;
		lo.recursive = true;
		lo.includeHidden = false;
		lo.ignore = project_.Settings().assets.ignore;
		// The packages folder is scanned, but generated sub folders are not interesting.
		fs::Walk(root, lo, [&](const fs::DirEntry& e) {
			const std::string rel = fs::Relative(e.path, root);
			if (rel.empty() || rel == ".")
				return true;
			const std::string name = fs::FileName(rel);
			if (!e.isDirectory && IsMeta(name))
				return true;
			if (rel == project_.ToRelative(project_.FilePath()))
				return true; // the .lum itself is listed below with its own type
			AssetEntry a;
			a.path = rel;
			a.name = name;
			a.isDirectory = e.isDirectory;
			a.extension = e.isDirectory ? std::string() : fs::ExtensionLower(rel);
			a.type = ClassifyAsset(rel, e.isDirectory);
			a.size = e.size;
			a.mtimeMs = e.mtimeMs;
			a.inPackage = str::StartsWith(rel, "packages/") || rel == "packages";
			if (a.type == AssetType::Shader)
				a.shader = ClassifyShader(rel);
			out.push_back(std::move(a));
			return true;
		});
		// project file
		{
			AssetEntry a;
			a.path = project_.ToRelative(project_.FilePath());
			a.name = fs::FileName(a.path);
			a.extension = ".lum";
			a.type = AssetType::Data;
			a.size = fs::FileSize(project_.FilePath());
			a.mtimeMs = fs::LastWriteTimeMs(project_.FilePath());
			out.push_back(std::move(a));
		}
		std::sort(out.begin(), out.end(), [](const AssetEntry& a, const AssetEntry& b) { return a.path < b.path; });
	}

	std::string AssetDatabase::EnsureMeta(const AssetEntry& e) const
	{
		const std::string meta = MetaPathFor(project_.Resolve(e.path));
		std::string text;
		Json j;
		if (fs::ReadText(meta, text) && Json::Parse(text, j, nullptr) && j.IsObject())
		{
			const std::string guid = j.GetString("guid");
			if (!guid.empty())
				return guid;
		}
		else
		{
			j = Json::MakeObject();
		}
		const std::string guid = GenerateGuid();
		j["guid"] = guid;
		j["type"] = AssetTypeName(e.type);
		if (!j.Find("import"))
			j["import"] = Json::MakeObject();
		fs::WriteTextIfChanged(meta, j.Dump(2) + "\n");
		return guid;
	}

	void AssetDatabase::RebuildIndex()
	{
		byPath_.clear();
		byGuid_.clear();
		for (size_t i = 0; i < entries_.size(); ++i)
		{
			byPath_[entries_[i].path] = i;
			if (!entries_[i].guid.empty())
				byGuid_[entries_[i].guid] = i;
		}
		reverseDepsValid_ = false;
	}

	Status AssetDatabase::Open(const Project& project)
	{
		if (!project.IsValid())
			return Status::Fail("invalid project");
		project_ = project;
		open_ = true;
		Scan(entries_);
		for (AssetEntry& e : entries_)
		{
			if (e.inPackage)
				continue; // package files are managed by the package manager; no .meta pollution
			if (e.path == project_.ToRelative(project_.FilePath()))
				continue;
			e.guid = EnsureMeta(e);
		}
		RebuildIndex();
		return Status::Ok();
	}

	void AssetDatabase::Close()
	{
		open_ = false;
		entries_.clear();
		byPath_.clear();
		byGuid_.clear();
		reverseDeps_.clear();
		reverseDepsValid_ = false;
	}

	std::vector<AssetChange> AssetDatabase::Refresh()
	{
		std::vector<AssetChange> changes;
		if (!open_)
			return changes;
		std::vector<AssetEntry> fresh;
		Scan(fresh);

		std::unordered_map<std::string, const AssetEntry*> oldMap;
		for (const AssetEntry& e : entries_)
			oldMap[e.path] = &e;
		std::unordered_map<std::string, const AssetEntry*> newMap;
		for (const AssetEntry& e : fresh)
			newMap[e.path] = &e;

		// Carry GUIDs over / create missing ones.
		for (AssetEntry& e : fresh)
		{
			auto it = oldMap.find(e.path);
			if (it != oldMap.end() && !it->second->guid.empty())
				e.guid = it->second->guid;
			else if (!e.inPackage && e.path != project_.ToRelative(project_.FilePath()))
				e.guid = EnsureMeta(e);
		}

		std::unordered_map<std::string, const AssetEntry*> removedByGuid;
		for (const AssetEntry& e : entries_)
			if (!newMap.count(e.path) && !e.guid.empty())
				removedByGuid[e.guid] = &e;

		std::set<std::string> handledRemoved;
		for (const AssetEntry& e : fresh)
		{
			auto it = oldMap.find(e.path);
			if (it == oldMap.end())
			{
				auto mv = e.guid.empty() ? removedByGuid.end() : removedByGuid.find(e.guid);
				AssetChange c;
				c.path = e.path;
				c.type = e.type;
				c.guid = e.guid;
				if (mv != removedByGuid.end())
				{
					c.kind = AssetChange::Kind::Moved;
					c.oldPath = mv->second->path;
					handledRemoved.insert(mv->second->path);
				}
				else
					c.kind = AssetChange::Kind::Added;
				changes.push_back(std::move(c));
			}
			else if (!e.isDirectory && (it->second->size != e.size || it->second->mtimeMs != e.mtimeMs))
			{
				AssetChange c;
				c.kind = AssetChange::Kind::Modified;
				c.path = e.path;
				c.type = e.type;
				c.guid = e.guid;
				changes.push_back(std::move(c));
			}
		}
		for (const AssetEntry& e : entries_)
		{
			if (!newMap.count(e.path) && !handledRemoved.count(e.path))
			{
				AssetChange c;
				c.kind = AssetChange::Kind::Removed;
				c.path = e.path;
				c.type = e.type;
				c.guid = e.guid;
				changes.push_back(std::move(c));
			}
		}
		entries_ = std::move(fresh);
		RebuildIndex();
		return changes;
	}

	const AssetEntry* AssetDatabase::Find(const std::string& path) const
	{
		auto it = byPath_.find(fs::Normalize(path));
		return it == byPath_.end() ? nullptr : &entries_[it->second];
	}

	const AssetEntry* AssetDatabase::FindByGuid(const std::string& guid) const
	{
		auto it = byGuid_.find(guid);
		return it == byGuid_.end() ? nullptr : &entries_[it->second];
	}

	std::string AssetDatabase::GuidOf(const std::string& path) const
	{
		const AssetEntry* e = Find(path);
		return e ? e->guid : std::string();
	}

	std::string AssetDatabase::PathOfGuid(const std::string& guid) const
	{
		const AssetEntry* e = FindByGuid(guid);
		return e ? e->path : std::string();
	}

	std::vector<const AssetEntry*> AssetDatabase::Children(const std::string& folder, bool includePackages) const
	{
		std::vector<const AssetEntry*> out;
		const std::string f = fs::Normalize(folder) == "." ? std::string() : fs::Normalize(folder);
		for (const AssetEntry& e : entries_)
		{
			if (Dir(e.path) != f)
				continue;
			if (!includePackages && e.inPackage)
				continue;
			out.push_back(&e);
		}
		std::stable_sort(out.begin(), out.end(), [](const AssetEntry* a, const AssetEntry* b) {
			if (a->isDirectory != b->isDirectory)
				return a->isDirectory;
			return str::ToLower(a->name) < str::ToLower(b->name);
		});
		return out;
	}

	std::vector<const AssetEntry*> AssetDatabase::OfType(AssetType type) const
	{
		std::vector<const AssetEntry*> out;
		for (const AssetEntry& e : entries_)
			if (e.type == type)
				out.push_back(&e);
		return out;
	}

	std::vector<const AssetEntry*> AssetDatabase::Search(const AssetSearchOptions& options) const
	{
		struct Scored
		{
			const AssetEntry* e;
			int score;
		};
		std::vector<Scored> scored;
		for (const AssetEntry& e : entries_)
		{
			if (e.isDirectory && !options.includeFolders)
				continue;
			if (e.inPackage && !options.includePackages)
				continue;
			if (!options.types.empty() && std::find(options.types.begin(), options.types.end(), e.type) == options.types.end())
				continue;
			int score = 1;
			if (!options.query.empty())
			{
				score = str::FuzzyScore(options.query, e.name);
				if (score == 0)
				{
					const int s2 = str::FuzzyScore(options.query, e.path);
					score = s2 / 2;
				}
				if (score == 0)
					continue;
			}
			scored.push_back({&e, score});
		}
		std::stable_sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) {
			if (a.score != b.score)
				return a.score > b.score;
			return a.e->path < b.e->path;
		});
		std::vector<const AssetEntry*> out;
		for (const Scored& s : scored)
		{
			if (out.size() >= options.limit)
				break;
			out.push_back(s.e);
		}
		return out;
	}

	// ----------------------------------------------------------------------------------------------------
	// File operations
	// ----------------------------------------------------------------------------------------------------
	Status AssetDatabase::CreateFolder(const std::string& path)
	{
		const std::string abs = project_.Resolve(path);
		if (!project_.Contains(abs))
			return Status::Fail("path is outside the project");
		if (fs::Exists(abs))
			return Status::Fail("already exists: " + path);
		if (!fs::CreateDirs(abs))
			return Status::Fail("cannot create " + path);
		Refresh();
		return Status::Ok();
	}

	Status AssetDatabase::CreateAssetFile(const std::string& path, const std::string& content)
	{
		const std::string abs = project_.Resolve(path);
		if (!project_.Contains(abs))
			return Status::Fail("path is outside the project");
		if (fs::Exists(abs))
			return Status::Fail("already exists: " + path);
		fs::CreateDirs(fs::Parent(abs));
		if (!fs::WriteText(abs, content))
			return Status::Fail("cannot write " + path);
		Refresh();
		return Status::Ok();
	}

	Status AssetDatabase::Rename(const std::string& path, const std::string& newName)
	{
		const std::string cleanName = fs::SanitizeFileName(newName);
		if (cleanName.empty())
			return Status::Fail("invalid name");
		const std::string src = project_.Resolve(path);
		const std::string dst = fs::Join(fs::Parent(src), cleanName);
		if (!fs::Exists(src))
			return Status::Fail("not found: " + path);
		if (fs::Exists(dst))
			return Status::Fail("target exists: " + cleanName);
		if (!fs::Rename(src, dst))
			return Status::Fail("rename failed");
		if (fs::Exists(MetaPathFor(src)))
			fs::Rename(MetaPathFor(src), MetaPathFor(dst));
		Refresh();
		return Status::Ok();
	}

	Status AssetDatabase::Move(const std::string& path, const std::string& newFolder)
	{
		const std::string src = project_.Resolve(path);
		const std::string folder = project_.Resolve(newFolder);
		if (!project_.Contains(folder))
			return Status::Fail("target is outside the project");
		if (fs::IsSubPath(folder, src))
			return Status::Fail("cannot move a folder into itself");
		fs::CreateDirs(folder);
		const std::string dst = fs::Join(folder, fs::FileName(src));
		if (fs::Exists(dst))
			return Status::Fail("target exists");
		if (!fs::Rename(src, dst))
			return Status::Fail("move failed");
		if (fs::Exists(MetaPathFor(src)))
			fs::Rename(MetaPathFor(src), MetaPathFor(dst));
		Refresh();
		return Status::Ok();
	}

	Status AssetDatabase::Duplicate(const std::string& path, std::string* newPath)
	{
		const std::string src = project_.Resolve(path);
		if (!fs::Exists(src))
			return Status::Fail("not found: " + path);
		const std::string stem = fs::Stem(src);
		const std::string ext = fs::Extension(src);
		std::string dst;
		for (int i = 1; i < 10000; ++i)
		{
			dst = fs::Join(fs::Parent(src), stem + " (" + std::to_string(i) + ")" + ext);
			if (!fs::Exists(dst))
				break;
		}
		const bool ok = fs::IsDirectory(src) ? fs::CopyDir(src, dst, false, [](const std::string& rel) { return !IsMeta(rel); }) : fs::CopyFile(src, dst, false);
		if (!ok)
			return Status::Fail("copy failed");
		Refresh();
		if (newPath)
			*newPath = project_.ToRelative(dst);
		return Status::Ok();
	}

	Status AssetDatabase::Delete(const std::string& path)
	{
		const std::string abs = project_.Resolve(path);
		if (!project_.Contains(abs) || abs == project_.Root())
			return Status::Fail("refusing to delete " + path);
		if (abs == project_.FilePath())
			return Status::Fail("refusing to delete the project file");
		if (!fs::Exists(abs))
			return Status::Fail("not found: " + path);
		if (fs::IsDirectory(abs))
		{
			// Remove sidecars inside first so no orphan .meta files remain.
			fs::RemoveAll(abs);
		}
		else
			fs::Remove(abs);
		fs::Remove(MetaPathFor(abs));
		Refresh();
		return Status::Ok();
	}

	Status AssetDatabase::Import(const std::string& externalFile, const std::string& folder, std::string* newPath)
	{
		if (!fs::IsFile(externalFile))
			return Status::Fail("not a file: " + externalFile);
		const std::string dstFolder = project_.Resolve(folder);
		if (!project_.Contains(dstFolder))
			return Status::Fail("target is outside the project");
		fs::CreateDirs(dstFolder);
		std::string dst = fs::Join(dstFolder, fs::SanitizeFileName(fs::FileName(externalFile)));
		if (fs::Exists(dst))
		{
			const std::string stem = fs::Stem(dst);
			const std::string ext = fs::Extension(dst);
			for (int i = 1; i < 10000; ++i)
			{
				dst = fs::Join(dstFolder, stem + "_" + std::to_string(i) + ext);
				if (!fs::Exists(dst))
					break;
			}
		}
		if (!fs::CopyFile(externalFile, dst, false))
			return Status::Fail("copy failed");
		Refresh();
		if (newPath)
			*newPath = project_.ToRelative(dst);
		return Status::Ok();
	}

	Json AssetDatabase::GetImportSettings(const std::string& path) const
	{
		std::string text;
		Json j;
		if (fs::ReadText(MetaPathFor(project_.Resolve(path)), text) && Json::Parse(text, j, nullptr))
			return j.Get("import");
		return Json::MakeObject();
	}

	Status AssetDatabase::SetImportSettings(const std::string& path, const Json& settings)
	{
		const AssetEntry* e = Find(path);
		if (!e)
			return Status::Fail("unknown asset: " + path);
		const std::string meta = MetaPathFor(project_.Resolve(path));
		std::string text;
		Json j;
		if (!(fs::ReadText(meta, text) && Json::Parse(text, j, nullptr) && j.IsObject()))
			j = Json::MakeObject();
		if (j.GetString("guid").empty())
			j["guid"] = e->guid.empty() ? GenerateGuid() : e->guid;
		j["type"] = AssetTypeName(e->type);
		j["import"] = settings;
		if (!fs::WriteTextIfChanged(meta, j.Dump(2) + "\n"))
			return Status::Fail("cannot write " + meta);
		return Status::Ok();
	}

	// ----------------------------------------------------------------------------------------------------
	// Dependencies
	// ----------------------------------------------------------------------------------------------------
	namespace
	{
		// Collects every JSON string value that could be a path.
		void CollectJsonStrings(const Json& j, std::vector<std::string>& out, int depth = 0)
		{
			if (depth > 64)
				return;
			if (j.IsString())
			{
				out.push_back(j.AsString());
			}
			else if (j.IsArray())
			{
				for (const Json& c : j.Items())
					CollectJsonStrings(c, out, depth + 1);
			}
			else if (j.IsObject())
			{
				for (const Json::Member& m : j.Members())
					CollectJsonStrings(m.second, out, depth + 1);
			}
		}

		// Finds quoted string literals in source text: "..." and '...'.
		void CollectQuoted(const std::string& text, std::vector<std::string>& out)
		{
			size_t i = 0;
			while (i < text.size())
			{
				const char c = text[i];
				if (c == '"' || c == '\'')
				{
					size_t j = i + 1;
					std::string lit;
					while (j < text.size() && text[j] != c && text[j] != '\n')
					{
						if (text[j] == '\\' && j + 1 < text.size())
							++j;
						lit.push_back(text[j]);
						++j;
					}
					if (j < text.size() && text[j] == c && !lit.empty() && lit.size() < 260)
						out.push_back(lit);
					i = j + 1;
				}
				else
					++i;
			}
		}
	} // namespace

	std::vector<std::string> AssetDatabase::ScanDependencies(const AssetEntry& e) const
	{
		std::vector<std::string> candidates;
		std::string text;
		const bool isJson = e.type == AssetType::Scene || e.type == AssetType::Prefab || e.type == AssetType::Material || e.type == AssetType::Animation ||
			e.extension == ".lshader";
		const bool isSource = e.type == AssetType::Script || e.type == AssetType::Shader || e.type == AssetType::Native;
		if (!isJson && !isSource)
			return {};
		if (e.size > (32u << 20))
			return {};
		if (!fs::ReadText(project_.Resolve(e.path), text))
			return {};
		if (isJson)
		{
			Json j;
			if (Json::Parse(text, j, nullptr))
				CollectJsonStrings(j, candidates);
		}
		else
		{
			CollectQuoted(text, candidates);
			if (e.type == AssetType::Script)
			{
				// require("a.b.c") -> scripts/a/b/c.lua (and a/b/c/init.lua)
				std::vector<std::string> extra;
				for (const std::string& s : candidates)
				{
					if (s.find('/') != std::string::npos || s.find(' ') != std::string::npos)
						continue;
					std::string p = s;
					std::replace(p.begin(), p.end(), '.', '/');
					extra.push_back(p + ".lua");
					extra.push_back(p + "/init.lua");
				}
				candidates.insert(candidates.end(), extra.begin(), extra.end());
			}
		}
		std::set<std::string> found;
		const std::string base = Dir(e.path);
		const std::vector<std::string>& scriptDirs = project_.Settings().scripts.dirs;
		const std::vector<std::string>& shaderDirs = project_.Settings().shaders.dirs;
		for (const std::string& s : candidates)
		{
			if (s.empty() || s.size() > 260)
				continue;
			std::vector<std::string> tries;
			tries.push_back(fs::Normalize(s));
			tries.push_back(fs::Normalize(JoinRel(base, s)));
			for (const std::string& d : scriptDirs)
				tries.push_back(fs::Normalize(JoinRel(d, s)));
			for (const std::string& d : shaderDirs)
				tries.push_back(fs::Normalize(JoinRel(d, s)));
			for (const std::string& t : tries)
			{
				const AssetEntry* hit = Find(t);
				if (hit && !hit->isDirectory && hit->path != e.path)
				{
					found.insert(hit->path);
					break;
				}
			}
			// GUID references (e.g. "guid:xxxxxxxx-...")
			if (str::StartsWith(s, "guid:"))
			{
				const AssetEntry* hit = FindByGuid(s.substr(5));
				if (hit && hit->path != e.path)
					found.insert(hit->path);
			}
		}
		return std::vector<std::string>(found.begin(), found.end());
	}

	std::vector<std::string> AssetDatabase::Dependencies(const std::string& path) const
	{
		const AssetEntry* e = Find(path);
		if (!e || e->isDirectory)
			return {};
		return ScanDependencies(*e);
	}

	void AssetDatabase::BuildReverseDeps() const
	{
		reverseDeps_.clear();
		for (const AssetEntry& e : entries_)
		{
			if (e.isDirectory || e.inPackage)
				continue;
			for (const std::string& d : ScanDependencies(e))
				reverseDeps_[d].push_back(e.path);
		}
		reverseDepsValid_ = true;
	}

	std::vector<std::string> AssetDatabase::Dependents(const std::string& path) const
	{
		if (!reverseDepsValid_)
			BuildReverseDeps();
		auto it = reverseDeps_.find(fs::Normalize(path));
		return it == reverseDeps_.end() ? std::vector<std::string>() : it->second;
	}

	std::vector<std::string> AssetDatabase::Unreferenced() const
	{
		if (!reverseDepsValid_)
			BuildReverseDeps();
		std::vector<std::string> out;
		for (const AssetEntry& e : entries_)
		{
			if (e.isDirectory || e.inPackage)
				continue;
			switch (e.type)
			{
			case AssetType::Script:
			case AssetType::Scene:
			case AssetType::Native:
			case AssetType::Shader:
			case AssetType::Data:
			case AssetType::Text:
			case AssetType::Package:
				continue;
			default:
				break;
			}
			if (!reverseDeps_.count(e.path))
				out.push_back(e.path);
		}
		return out;
	}

	AssetDatabase::Stats AssetDatabase::GetStats() const
	{
		Stats s;
		for (const AssetEntry& e : entries_)
		{
			if (e.isDirectory)
			{
				++s.folders;
				continue;
			}
			++s.files;
			s.bytes += e.size;
			s.perType[e.type]++;
		}
		return s;
	}

} // namespace lumen
