#include "lumen/core/Project.h"

#include "lumen/core/Fs.h"
#include "lumen/core/Image.h"
#include "lumen/core/Log.h"
#include "lumen/core/Platform.h"
#include "lumen/core/Strings.h"

#include <algorithm>
#include <random>

namespace lumen
{
	// ------------------------------------------------------------------------------------------------
	// helpers
	// ------------------------------------------------------------------------------------------------
	std::string GenerateGuid()
	{
		static std::mt19937_64 rng{std::random_device{}() ^ (uint64_t)platform::UnixTimeMs()};
		const uint64_t a = rng();
		const uint64_t b = rng();
		uint8_t bytes[16];
		for (int i = 0; i < 8; ++i)
		{
			bytes[i] = (uint8_t)(a >> (8 * i));
			bytes[8 + i] = (uint8_t)(b >> (8 * i));
		}
		bytes[6] = (bytes[6] & 0x0F) | 0x40; // version 4
		bytes[8] = (bytes[8] & 0x3F) | 0x80; // variant
		const std::string hex = str::ToHex(bytes, 16);
		return hex.substr(0, 8) + "-" + hex.substr(8, 4) + "-" + hex.substr(12, 4) + "-" + hex.substr(16, 4) + "-" + hex.substr(20, 12);
	}

	namespace
	{
		Json StringArray(const std::vector<std::string>& v)
		{
			Json a = Json::MakeArray();
			for (const std::string& s : v)
				a.Push(s);
			return a;
		}

		std::vector<std::string> ReadStringArray(const Json& j, const char* key, const std::vector<std::string>& def)
		{
			const Json* p = j.Find(key);
			if (!p || !p->IsArray())
				return def;
			return j.GetStringArray(key);
		}

		Json Vec3Json(const Float3& v)
		{
			Json a = Json::MakeArray();
			a.Push(v.x);
			a.Push(v.y);
			a.Push(v.z);
			return a;
		}

		Float3 ReadVec3(const Json& j, const Float3& def)
		{
			if (!j.IsArray() || j.Size() < 3)
				return def;
			return Float3{(float)j.At(0).AsDouble(def.x), (float)j.At(1).AsDouble(def.y), (float)j.At(2).AsDouble(def.z)};
		}

		const char* SourceName(PackageSource s)
		{
			switch (s)
			{
			case PackageSource::Git: return "git";
			case PackageSource::Archive: return "archive";
			case PackageSource::Path: return "path";
			default: return "registry";
			}
		}

		PackageSource SourceFromName(const std::string& n)
		{
			if (n == "git") return PackageSource::Git;
			if (n == "archive" || n == "url") return PackageSource::Archive;
			if (n == "path" || n == "local") return PackageSource::Path;
			return PackageSource::Registry;
		}

		bool LooksLikeArchiveUrl(const std::string& url)
		{
			const std::string l = str::ToLower(url);
			for (const char* ext : {".zip", ".tar.gz", ".tgz", ".tar.xz", ".tar.bz2", ".tar"})
			{
				if (str::EndsWith(l, ext))
					return true;
			}
			return false;
		}
	} // namespace

	// ------------------------------------------------------------------------------------------------
	// DependencySpec
	// ------------------------------------------------------------------------------------------------
	Json DependencySpec::ToJson() const
	{
		Json j = Json::MakeObject();
		j["name"] = name;
		if (!version.empty())
			j["version"] = version;
		j["source"] = SourceName(source);
		if (!url.empty())
			j["url"] = url;
		if (!ref.empty())
			j["ref"] = ref;
		if (!sha256.empty())
			j["sha256"] = sha256;
		if (!subdir.empty())
			j["subdir"] = subdir;
		if (!lua.empty())
			j["lua"] = StringArray(lua);
		if (!include.empty())
			j["include"] = StringArray(include);
		if (!shaders.empty())
			j["shaders"] = StringArray(shaders);
		if (!defines.empty())
			j["defines"] = StringArray(defines);
		if (!cmakeTargets.empty())
			j["cmakeTargets"] = StringArray(cmakeTargets);
		if (!platforms.empty())
			j["platforms"] = StringArray(platforms);
		if (optional)
			j["optional"] = true;
		if (linkInPlace)
			j["linkInPlace"] = true;
		return j;
	}

	DependencySpec DependencySpec::FromJson(const Json& j)
	{
		DependencySpec d;
		d.name = j.GetString("name");
		d.version = j.GetString("version");
		d.url = j.GetString("url");
		d.ref = j.GetString("ref");
		d.sha256 = j.GetString("sha256");
		d.subdir = j.GetString("subdir");
		d.lua = j.GetStringArray("lua");
		d.include = j.GetStringArray("include");
		d.shaders = j.GetStringArray("shaders");
		d.defines = j.GetStringArray("defines");
		d.cmakeTargets = j.GetStringArray("cmakeTargets");
		d.platforms = j.GetStringArray("platforms");
		d.optional = j.GetBool("optional", false);
		d.linkInPlace = j.GetBool("linkInPlace", false);
		const std::string src = j.GetString("source");
		if (src == "git" || src == "archive" || src == "url" || src == "path" || src == "local" || src == "registry")
		{
			d.source = SourceFromName(src);
		}
		else if (!src.empty())
		{
			// v1 style: "source" holds the url itself
			d.url = src;
			d.source = LooksLikeArchiveUrl(src) ? PackageSource::Archive : PackageSource::Git;
		}
		else if (!d.url.empty())
		{
			d.source = LooksLikeArchiveUrl(d.url) ? PackageSource::Archive : PackageSource::Git;
		}
		return d;
	}

	DependencySpec DependencySpec::FromLegacy(const Json& j)
	{
		return FromJson(j);
	}

	// ------------------------------------------------------------------------------------------------
	// manifest (de)serialization
	// ------------------------------------------------------------------------------------------------
	Json Project::ToJson() const
	{
		const ProjectSettings& s = settings_;
		Json j = Json::MakeObject();
		j["format"] = kProjectFormatName;
		j["version"] = kProjectFormatVersion;
		j["name"] = s.name;
		j["id"] = s.id;
		if (!s.description.empty())
			j["description"] = s.description;
		if (!s.company.empty())
			j["company"] = s.company;
		j["gameVersion"] = s.gameVersion;
		j["engine"] = s.engineVersion;
		j["startupScene"] = s.startupScene;
		j["mainScript"] = s.mainScript;

		Json g = Json::MakeObject();
		g["preferred"] = s.graphics.preferredApi;
		g["apis"] = StringArray(s.graphics.apis);
		g["vsync"] = s.graphics.vsync;
		g["culling"] = s.graphics.culling;
		g["occlusionCulling"] = s.graphics.occlusionCulling;
		g["meshletOcclusionCulling"] = s.graphics.meshletOcclusionCulling;
		g["msaa"] = s.graphics.msaa;
		g["shadows"] = s.graphics.shadows;
		g["reflections"] = s.graphics.reflections;
		g["resolutionScale"] = s.graphics.resolutionScale;
		g["antiAliasing"] = s.graphics.antiAliasing;
		g["tonemap"] = s.graphics.tonemap;
		g["exposure"] = s.graphics.exposure;
		g["saturation"] = s.graphics.saturation;
		g["contrast"] = s.graphics.contrast;
		g["bloom"] = s.graphics.bloom;
		g["bloomThreshold"] = s.graphics.bloomThreshold;
		g["dither"] = s.graphics.dither;
		g["eyeAdaptation"] = s.graphics.eyeAdaptation;
		j["graphics"] = std::move(g);

		Json w = Json::MakeObject();
		w["title"] = s.window.title.empty() ? s.name : s.window.title;
		w["width"] = s.window.width;
		w["height"] = s.window.height;
		w["fullscreen"] = s.window.fullscreen;
		w["borderless"] = s.window.borderless;
		w["resizable"] = s.window.resizable;
		j["window"] = std::move(w);

		Json sc = Json::MakeObject();
		sc["dirs"] = StringArray(s.scripts.dirs);
		sc["hotReload"] = s.scripts.hotReload;
		sc["strip"] = s.scripts.strip;
		sc["debug"] = s.scripts.debug;
		j["scripts"] = std::move(sc);

		Json sh = Json::MakeObject();
		sh["dirs"] = StringArray(s.shaders.dirs);
		sh["include"] = StringArray(s.shaders.includeDirs);
		sh["defines"] = StringArray(s.shaders.defines);
		sh["optimize"] = s.shaders.optimize;
		sh["debugInfo"] = s.shaders.debugInfo;
		sh["builtin"] = s.shaders.builtinLibrary;
		sh["disabledBuiltins"] = StringArray(s.shaders.disabledBuiltins);
		j["shaders"] = std::move(sh);

		Json n = Json::MakeObject();
		n["enabled"] = s.native.enabled;
		n["module"] = s.native.moduleName;
		n["sources"] = StringArray(s.native.sources);
		n["include"] = StringArray(s.native.includeDirs);
		n["defines"] = StringArray(s.native.defines);
		n["libs"] = StringArray(s.native.linkLibraries);
		n["compileOptions"] = StringArray(s.native.compileOptions);
		n["linkOptions"] = StringArray(s.native.linkOptions);
		n["std"] = s.native.cxxStandard;
		n["buildType"] = s.native.buildType;
		n["hotReload"] = s.native.hotReload;
		j["native"] = std::move(n);
		// v1 compatibility key, so older tools still see the switch.
		j["nativeModules"] = s.native.enabled;

		Json p = Json::MakeObject();
		p["gravity"] = Vec3Json(s.physics.gravity);
		p["frameRate"] = s.physics.frameRate;
		p["accuracy"] = s.physics.accuracy;
		p["interpolation"] = s.physics.interpolation;
		p["debugDraw"] = s.physics.debugDraw;
		p["layers"] = StringArray(s.physics.layers);
		Json matrix = Json::MakeArray();
		for (uint32_t m : s.physics.layerMatrix)
			matrix.Push((long long)m);
		p["layerMatrix"] = std::move(matrix);
		j["physics"] = std::move(p);

		Json a = Json::MakeObject();
		a["master"] = s.audio.master;
		a["music"] = s.audio.music;
		a["sfx"] = s.audio.sfx;
		a["ui"] = s.audio.ui;
		a["voice"] = s.audio.voice;
		j["audio"] = std::move(a);

		Json as = Json::MakeObject();
		as["ignore"] = StringArray(s.assets.ignore);
		j["assets"] = std::move(as);

		Json b = Json::MakeObject();
		b["outputDir"] = s.build.outputDir;
		b["packScripts"] = s.build.packScripts;
		b["packScenes"] = s.build.packScenes;
		b["packShaders"] = s.build.packShaders;
		b["includeRuntime"] = s.build.includeRuntime;
		b["copyDirs"] = StringArray(s.build.copyDirs);
		j["build"] = std::move(b);

		Json packages = Json::MakeArray();
		for (const DependencySpec& d : s.packages)
			packages.Push(d.ToJson());
		j["packages"] = std::move(packages);

		for (const Json::Member& m : s.extra.Members())
			j[m.first] = m.second;
		return j;
	}

	Status Project::FromJson(const Json& j, ProjectSettings& out)
	{
		if (!j.IsObject())
			return Status::Fail("project file is not a JSON object");
		const std::string format = j.GetString("format");
		if (format != kProjectFormatName)
			return Status::Fail("not a lumen-project file (format = \"" + format + "\")");
		const int version = (int)j.GetInt("version", 1);
		if (version > kProjectFormatVersion)
			LUMEN_WARN("project", "project format v%d is newer than this engine understands (v%d); unknown data is preserved", version, kProjectFormatVersion);

		ProjectSettings s;
		s.formatVersion = version;
		s.name = j.GetString("name", s.name);
		s.id = j.GetString("id");
		s.description = j.GetString("description");
		s.company = j.GetString("company");
		s.gameVersion = j.GetString("gameVersion", s.gameVersion);
		s.engineVersion = j.GetString("engine", s.engineVersion);
		s.startupScene = j.GetString("startupScene", s.startupScene);
		s.mainScript = j.GetString("mainScript", s.mainScript);

		const Json& g = j.Get("graphics");
		s.graphics.preferredApi = g.GetString("preferred", s.graphics.preferredApi);
		s.graphics.apis = ReadStringArray(g, "apis", s.graphics.apis);
		s.graphics.vsync = g.GetBool("vsync", s.graphics.vsync);
		s.graphics.culling = g.GetBool("culling", s.graphics.culling);
		s.graphics.occlusionCulling = g.GetBool("occlusionCulling", s.graphics.occlusionCulling);
		s.graphics.meshletOcclusionCulling = g.GetBool("meshletOcclusionCulling", s.graphics.meshletOcclusionCulling);
		s.graphics.msaa = (int)g.GetInt("msaa", s.graphics.msaa);
		s.graphics.shadows = g.GetBool("shadows", s.graphics.shadows);
		s.graphics.reflections = g.GetBool("reflections", s.graphics.reflections);
		s.graphics.resolutionScale = (float)g.GetDouble("resolutionScale", s.graphics.resolutionScale);
		s.graphics.antiAliasing = g.GetString("antiAliasing", s.graphics.antiAliasing);
		s.graphics.tonemap = g.GetString("tonemap", s.graphics.tonemap);
		s.graphics.exposure = (float)g.GetDouble("exposure", s.graphics.exposure);
		s.graphics.saturation = (float)g.GetDouble("saturation", s.graphics.saturation);
		s.graphics.contrast = (float)g.GetDouble("contrast", s.graphics.contrast);
		s.graphics.bloom = g.GetBool("bloom", s.graphics.bloom);
		s.graphics.bloomThreshold = (float)g.GetDouble("bloomThreshold", s.graphics.bloomThreshold);
		s.graphics.dither = g.GetBool("dither", s.graphics.dither);
		s.graphics.eyeAdaptation = g.GetBool("eyeAdaptation", s.graphics.eyeAdaptation);

		const Json& w = j.Get("window");
		s.window.title = w.GetString("title", s.name);
		s.window.width = (int)w.GetInt("width", s.window.width);
		s.window.height = (int)w.GetInt("height", s.window.height);
		s.window.fullscreen = w.GetBool("fullscreen", s.window.fullscreen);
		s.window.borderless = w.GetBool("borderless", s.window.borderless);
		s.window.resizable = w.GetBool("resizable", s.window.resizable);

		const Json& sc = j.Get("scripts");
		s.scripts.dirs = ReadStringArray(sc, "dirs", s.scripts.dirs);
		s.scripts.hotReload = sc.GetBool("hotReload", s.scripts.hotReload);
		s.scripts.strip = sc.GetBool("strip", s.scripts.strip);
		s.scripts.debug = sc.GetBool("debug", s.scripts.debug);

		const Json& sh = j.Get("shaders");
		s.shaders.dirs = ReadStringArray(sh, "dirs", s.shaders.dirs);
		s.shaders.includeDirs = ReadStringArray(sh, "include", s.shaders.includeDirs);
		s.shaders.defines = ReadStringArray(sh, "defines", s.shaders.defines);
		s.shaders.optimize = sh.GetBool("optimize", s.shaders.optimize);
		s.shaders.debugInfo = sh.GetBool("debugInfo", s.shaders.debugInfo);
		s.shaders.builtinLibrary = sh.GetBool("builtin", s.shaders.builtinLibrary);
		s.shaders.disabledBuiltins = ReadStringArray(sh, "disabledBuiltins", s.shaders.disabledBuiltins);

		const Json& n = j.Get("native");
		s.native.enabled = n.Contains("enabled") ? n.GetBool("enabled", true) : j.GetBool("nativeModules", s.native.enabled);
		s.native.moduleName = n.GetString("module", s.native.moduleName);
		s.native.sources = ReadStringArray(n, "sources", s.native.sources);
		s.native.includeDirs = ReadStringArray(n, "include", s.native.includeDirs);
		s.native.defines = ReadStringArray(n, "defines", s.native.defines);
		s.native.linkLibraries = ReadStringArray(n, "libs", s.native.linkLibraries);
		s.native.compileOptions = ReadStringArray(n, "compileOptions", s.native.compileOptions);
		s.native.linkOptions = ReadStringArray(n, "linkOptions", s.native.linkOptions);
		s.native.cxxStandard = n.GetString("std", s.native.cxxStandard);
		s.native.buildType = n.GetString("buildType", s.native.buildType);
		s.native.hotReload = n.GetBool("hotReload", s.native.hotReload);

		const Json& p = j.Get("physics");
		s.physics.gravity = ReadVec3(p.Get("gravity"), s.physics.gravity);
		s.physics.frameRate = (float)p.GetDouble("frameRate", s.physics.frameRate);
		s.physics.accuracy = (int)p.GetInt("accuracy", s.physics.accuracy);
		s.physics.interpolation = p.GetBool("interpolation", s.physics.interpolation);
		s.physics.debugDraw = p.GetBool("debugDraw", s.physics.debugDraw);
		s.physics.layers = ReadStringArray(p, "layers", s.physics.layers);
		s.physics.layerMatrix.clear();
		for (const Json& m : p.Get("layerMatrix").Items())
			s.physics.layerMatrix.push_back((uint32_t)m.AsInt(~0u));

		const Json& a = j.Get("audio");
		s.audio.master = (float)a.GetDouble("master", s.audio.master);
		s.audio.music = (float)a.GetDouble("music", s.audio.music);
		s.audio.sfx = (float)a.GetDouble("sfx", s.audio.sfx);
		s.audio.ui = (float)a.GetDouble("ui", s.audio.ui);
		s.audio.voice = (float)a.GetDouble("voice", s.audio.voice);

		s.assets.ignore = ReadStringArray(j.Get("assets"), "ignore", s.assets.ignore);

		const Json& b = j.Get("build");
		s.build.outputDir = b.GetString("outputDir", s.build.outputDir);
		s.build.packScripts = b.GetBool("packScripts", s.build.packScripts);
		s.build.packScenes = b.GetBool("packScenes", s.build.packScenes);
		s.build.packShaders = b.GetBool("packShaders", s.build.packShaders);
		s.build.includeRuntime = b.GetBool("includeRuntime", s.build.includeRuntime);
		s.build.copyDirs = ReadStringArray(b, "copyDirs", s.build.copyDirs);

		for (const Json& pk : j.Get("packages").Items())
		{
			DependencySpec d = DependencySpec::FromJson(pk);
			if (!d.name.empty())
				s.packages.push_back(std::move(d));
		}

		static const char* known[] = {"format", "version", "name", "id", "description", "company", "gameVersion", "engine", "startupScene",
			"mainScript", "graphics", "window", "scripts", "shaders", "native", "nativeModules", "physics", "audio", "assets", "build", "packages"};
		s.extra = Json::MakeObject();
		for (const Json::Member& m : j.Members())
		{
			bool isKnown = false;
			for (const char* k : known)
			{
				if (m.first == k)
				{
					isKnown = true;
					break;
				}
			}
			if (!isKnown)
				s.extra[m.first] = m.second;
		}
		if (s.id.empty())
			s.id = GenerateGuid();
		out = std::move(s);
		return Status::Ok();
	}

	// ------------------------------------------------------------------------------------------------
	// load / save
	// ------------------------------------------------------------------------------------------------
	Status Project::Load(const std::string& pathOrDirectory, Project& out)
	{
		std::string file = pathOrDirectory;
		if (fs::IsDirectory(file))
		{
			file = fs::FindFileWithExtension(file, kProjectFileExtension);
			if (file.empty())
				return Status::Fail("no .lum project file in " + pathOrDirectory);
		}
		if (!fs::IsFile(file))
			return Status::Fail("project file not found: " + pathOrDirectory);

		std::string text;
		if (!fs::ReadText(file, text))
			return Status::Fail("cannot read " + file);
		Json j;
		std::string err;
		Json::ParseOptions po;
		po.allowComments = true;
		po.allowTrailingCommas = true;
		if (!Json::Parse(text, j, &err, po))
			return Status::Fail("invalid .lum (" + err + ")");

		Project p;
		Status st = FromJson(j, p.settings_);
		if (!st)
			return st;
		p.file_ = fs::Absolute(file);
		p.root_ = fs::Parent(p.file_);
		if (p.settings_.window.title.empty())
			p.settings_.window.title = p.settings_.name;
		out = std::move(p);
		return Status::Ok();
	}

	Status Project::Save() const
	{
		if (file_.empty())
			return Status::Fail("project has no file path");
		const std::string text = ToJson().Dump(2) + "\n";
		if (!fs::WriteTextIfChanged(file_, text))
			return Status::Fail("cannot write " + file_);
		return Status::Ok();
	}

	std::vector<std::string> Project::Validate() const
	{
		std::vector<std::string> problems;
		const ProjectSettings& s = settings_;
		if (s.name.empty())
			problems.push_back("project name is empty");
		if (!fs::IsFile(Resolve(s.mainScript)))
			problems.push_back("main script not found: " + s.mainScript);
		if (!fs::IsFile(Resolve(s.startupScene)))
			problems.push_back("startup scene not found: " + s.startupScene);
		for (const std::string& d : s.scripts.dirs)
		{
			if (!fs::IsDirectory(Resolve(d)))
				problems.push_back("scripts directory missing: " + d);
		}
		if (s.window.width < 64 || s.window.height < 64)
			problems.push_back("window size is too small");
		if (s.graphics.msaa != 1 && s.graphics.msaa != 2 && s.graphics.msaa != 4 && s.graphics.msaa != 8)
			problems.push_back("graphics.msaa must be 1, 2, 4 or 8");
		if (s.graphics.resolutionScale <= 0.0f || s.graphics.resolutionScale > 4.0f)
			problems.push_back("graphics.resolutionScale must be in (0, 4]");
		if (s.native.enabled && s.native.moduleName.empty())
			problems.push_back("native.module is empty");
		for (const char* bad : {"/", "\\", " ", ":"})
		{
			if (str::Contains(s.native.moduleName, bad))
			{
				problems.push_back("native.module must be a simple identifier");
				break;
			}
		}
		std::vector<std::string> seen;
		for (const DependencySpec& d : s.packages)
		{
			if (d.name.empty())
			{
				problems.push_back("a package has no name");
				continue;
			}
			if (std::find(seen.begin(), seen.end(), d.name) != seen.end())
				problems.push_back("duplicate package: " + d.name);
			seen.push_back(d.name);
			if ((d.source == PackageSource::Git || d.source == PackageSource::Archive || d.source == PackageSource::Path) && d.url.empty())
				problems.push_back("package " + d.name + " has no url/path");
		}
		return problems;
	}

	// ------------------------------------------------------------------------------------------------
	// paths
	// ------------------------------------------------------------------------------------------------
	std::string Project::Resolve(const std::string& relative) const
	{
		return fs::Join(root_, relative);
	}

	std::string Project::ToRelative(const std::string& absolute) const
	{
		if (!fs::IsSubPath(absolute, root_))
			return {};
		const std::string rel = fs::Relative(absolute, root_);
		return rel == "." ? std::string() : rel;
	}

	bool Project::Contains(const std::string& absolute) const
	{
		return fs::IsSubPath(absolute, root_);
	}

	std::string Project::MetaDir() const { return fs::Join(root_, ".lumen"); }
	std::string Project::PackagesDir() const { return fs::Join(root_, "packages"); }
	std::string Project::NativeBuildDir() const { return fs::Join(root_, ".lumen/native"); }
	std::string Project::NativeBinDir() const { return fs::Join(root_, ".lumen/bin"); }
	std::string Project::ShaderCacheDir() const { return fs::Join(root_, ".lumen/shader_cache"); }
	std::string Project::LuaCacheDir() const { return fs::Join(root_, ".lumen/lua_cache"); }
	std::string Project::LockFilePath() const { return fs::Join(root_, "lumen.lock.json"); }
	std::string Project::BuildDir() const { return fs::Join(root_, settings_.build.outputDir); }

	// ------------------------------------------------------------------------------------------------
	// creation from a template
	// ------------------------------------------------------------------------------------------------
	namespace
	{
		bool IsTextTemplateFile(const std::string& path)
		{
			static const char* exts[] = {".lua", ".cpp", ".cc", ".c", ".h", ".hpp", ".hlsl", ".hlsli", ".glsl", ".vert", ".frag", ".comp",
				".geom", ".tesc", ".tese", ".mesh", ".task", ".scene", ".json", ".lmat", ".prefab", ".anim", ".animctl", ".md", ".txt",
				".luarc", ".gitignore", ".lshader"};
			const std::string ext = fs::ExtensionLower(path);
			for (const char* e : exts)
			{
				if (ext == e)
					return true;
			}
			return fs::FileName(path) == ".luarc.json" || fs::FileName(path) == ".gitignore";
		}

		std::string Substitute(std::string text, const std::vector<std::pair<std::string, std::string>>& vars)
		{
			for (const auto& v : vars)
				text = str::ReplaceAll(std::move(text), "${" + v.first + "}", v.second);
			return text;
		}

		void RunGenerator(const Json& gen, const std::string& root)
		{
			const std::string type = gen.GetString("type");
			const std::string path = fs::Join(root, gen.GetString("path"));
			if (path.empty() || fs::Exists(path))
				return;
			const int size = (int)std::max<int64_t>(4, std::min<int64_t>(4096, gen.GetInt("size", 256)));
			image::Image img;
			if (type == "png.grid")
				img = image::MakeGrid(size, (int)gen.GetInt("cells", 8));
			else if (type == "png.checker")
				img = image::MakeChecker(size, (int)gen.GetInt("cells", 8));
			else if (type == "png.solid")
				img = image::MakeSolid(size, size, (uint8_t)gen.GetInt("r", 255), (uint8_t)gen.GetInt("g", 255), (uint8_t)gen.GetInt("b", 255), (uint8_t)gen.GetInt("a", 255));
			else if (type == "png.flat_normal")
				img = image::MakeSolid(size, size, 128, 128, 255, 255);
			else if (type == "png.water_normal")
				img = image::MakeWaterNormalMap(size, (uint32_t)gen.GetInt("seed", 1), (float)gen.GetDouble("strength", 1.0));
			else if (type == "png.noise")
				img = image::MakeValueNoise(size, (uint32_t)gen.GetInt("seed", 7), (int)gen.GetInt("octaves", 5));
			else
			{
				LUMEN_WARN("project", "unknown template generator '%s'", type.c_str());
				return;
			}
			if (!image::WritePng(path, img))
				LUMEN_WARN("project", "could not write generated texture %s", path.c_str());
		}
	} // namespace

	Status Project::Create(const ProjectCreateOptions& options, Project& out)
	{
		if (options.directory.empty() || options.name.empty())
			return Status::Fail("project directory and name are required");
		const std::string root = fs::Absolute(options.directory);
		const std::string fileName = fs::SanitizeFileName(options.name) + kProjectFileExtension;
		const std::string lumFile = fs::Join(root, fileName);
		if (fs::Exists(lumFile))
			return Status::Fail("project already exists: " + lumFile);
		if (!fs::CreateDirs(root))
			return Status::Fail("cannot create directory " + root);

		Project p;
		p.root_ = root;
		p.file_ = lumFile;
		p.settings_.name = options.name;
		p.settings_.id = GenerateGuid();
		p.settings_.window.title = options.name;
		p.settings_.native.enabled = options.nativeModule;
		std::string module = options.name;
		for (char& c : module)
		{
			if (!std::isalnum((unsigned char)c))
				c = '_';
		}
		if (module.empty() || std::isdigit((unsigned char)module[0]))
			module = "game_" + module;
		p.settings_.native.moduleName = str::ToLower(module);

		const std::vector<std::pair<std::string, std::string>> vars = {
			{"PROJECT_NAME", options.name},
			{"PROJECT_ID", p.settings_.id},
			{"MODULE_NAME", p.settings_.native.moduleName},
			{"ENGINE_VERSION", LUMEN_VERSION_STRING},
			{"YEAR", platform::UtcTimestamp().substr(0, 4)},
		};

		// Template: <runtime>/templates/<name>/ with an optional template.json
		const std::string templateDir = options.runtimeRoot.empty() ? std::string() : fs::Join(options.runtimeRoot, "templates", options.templateName);
		Json manifest;
		if (!templateDir.empty() && fs::IsDirectory(templateDir))
		{
			std::string text;
			if (fs::ReadText(fs::Join(templateDir, "template.json"), text))
				Json::Parse(text, manifest, nullptr);

			fs::ListOptions lo;
			lo.recursive = true;
			fs::Walk(templateDir, lo, [&](const fs::DirEntry& e) {
				const std::string rel = fs::Relative(e.path, templateDir);
				if (rel == "template.json")
					return true;
				if (!options.nativeModule && str::StartsWith(rel, "native/"))
					return true;
				const std::string dst = fs::Join(root, Substitute(rel, vars));
				if (e.isDirectory)
				{
					fs::CreateDirs(dst);
					return true;
				}
				if (fs::Exists(dst))
					return true;
				if (IsTextTemplateFile(e.path))
				{
					std::string content;
					if (fs::ReadText(e.path, content))
						fs::WriteText(dst, Substitute(content, vars));
				}
				else
				{
					fs::CopyFile(e.path, dst, false);
				}
				return true;
			});
		}

		// Folders every project has, whether or not the template ships files for them.
		static const char* standardDirs[] = {"scenes", "scripts", "shaders", "materials", "textures", "models", "audio", "video", "prefabs",
			"animations", "fonts", "packages"};
		for (const char* d : standardDirs)
			fs::CreateDirs(fs::Join(root, d));
		if (options.nativeModule)
			fs::CreateDirs(fs::Join(root, "native"));

		for (const Json& dir : manifest.Get("directories").Items())
		{
			if (dir.IsString())
				fs::CreateDirs(fs::Join(root, dir.AsString()));
		}
		for (const Json& gen : manifest.Get("generate").Items())
			RunGenerator(gen, root);

		// Fallbacks when no template is available (first run without runtime data, tests):
		const std::string mainScript = fs::Join(root, p.settings_.mainScript);
		if (!fs::Exists(mainScript))
		{
			fs::WriteText(mainScript,
				"-- " + options.name + " entry script.\n"
				"-- start() runs once after the startup scene is loaded, update(dt) every frame,\n"
				"-- fixed_update(dt) at the physics rate and shutdown() when the game closes.\n"
				"local game = {}\n\n"
				"function game.start()\n"
				"end\n\n"
				"function game.update(dt)\n"
				"end\n\n"
				"function game.fixed_update(dt)\n"
				"end\n\n"
				"function game.shutdown()\n"
				"end\n\n"
				"return game\n");
		}
		const std::string scenePath = fs::Join(root, p.settings_.startupScene);
		if (!fs::Exists(scenePath))
		{
			fs::WriteText(scenePath,
				"{\n"
				"  \"format\": \"lumen-scene\",\n"
				"  \"version\": 2,\n"
				"  \"name\": \"main\",\n"
				"  \"environment\": {\n"
				"    \"sun\": { \"azimuth\": 35, \"elevation\": 48, \"intensity\": 6.0 },\n"
				"    \"sky\": { \"mode\": \"procedural\" },\n"
				"    \"fog\": { \"density\": 0.012 }\n"
				"  },\n"
				"  \"entities\": []\n"
				"}\n");
		}

		// Editor configuration for external Lua language servers / editors.
		const std::string luarc = fs::Join(root, ".luarc.json");
		if (!fs::Exists(luarc))
		{
			Json rc = Json::MakeObject();
			rc["runtime"]["version"] = "Lua 5.4";
			Json library = Json::MakeArray();
			if (!options.runtimeRoot.empty())
				library.Push(fs::Join(options.runtimeRoot, "lua/api"));
			rc["workspace"]["library"] = std::move(library);
			rc["workspace"]["checkThirdParty"] = false;
			rc["diagnostics"]["globals"] = Json(Json::Array{Json("lumen")});
			fs::WriteText(luarc, rc.Dump(2) + "\n");
		}
		const std::string gitignore = fs::Join(root, ".gitignore");
		if (!fs::Exists(gitignore))
			fs::WriteText(gitignore, ".lumen/\nbuild/\npackages/\n*.tmp\n");

		out = std::move(p);
		Status st = out.Save();
		if (!st)
			return st;
		LUMEN_INFO("project", "created project '%s' in %s", options.name.c_str(), root.c_str());
		return Status::Ok();
	}

} // namespace lumen
