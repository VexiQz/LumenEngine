#include "lumen/core/NativeLibs.h"

#include "lumen/core/Archive.h"
#include "lumen/core/Fs.h"
#include "lumen/core/Http.h"
#include "lumen/core/Log.h"
#include "lumen/core/Platform.h"
#include "lumen/core/Sha256.h"
#include "lumen/core/Strings.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef CopyFile
#undef CopyFile
#endif
#endif

namespace lumen
{
	namespace
	{
		// Built-in catalog. Hashes were computed from the official assets. Rolling releases (FFmpeg builds) are verified
		// against the checksum list published with the release instead.
		const char* kBuiltinLibs = R"JSON({
"libs": [
  {
    "id": "sdl2", "title": "SDL2", "version": "2.30.9", "license": "zlib", "category": "platform", "required": ["linux"],
    "description": "Window, input and audio device layer used by Wicked Engine on Linux.",
    "platforms": {
      "windows-x64": {"systemNames": ["SDL2.dll"], "download": {"url": "https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-2.30.9-win32-x64.zip",
                       "sha256": "42405cc9ca65d520f23bc62d99edcf836738426917cd1aa7a3c5f17bcaa95cbd", "sizeBytes": 604246, "files": [{"from": "SDL2.dll"}]}},
      "linux-*":     {"systemNames": ["libSDL2-2.0.so.0", "libSDL2.so"], "installHint": "sudo apt install libsdl2-2.0-0   (Fedora: sudo dnf install SDL2, Arch: sudo pacman -S sdl2)"},
      "macos-*":     {"systemNames": ["libSDL2-2.0.0.dylib", "libSDL2.dylib"], "installHint": "brew install sdl2"}
    }
  },
  {
    "id": "vulkan", "title": "Vulkan loader", "version": "1.3", "license": "Apache-2.0", "category": "render", "required": ["windows", "linux"],
    "description": "Vulkan runtime. Installed with the GPU driver on Windows and with the distribution's Vulkan package on Linux.",
    "platforms": {
      "windows-*": {"systemNames": ["vulkan-1.dll"], "installHint": "Install or update your GPU driver (NVIDIA / AMD / Intel); it ships vulkan-1.dll."},
      "linux-*":   {"systemNames": ["libvulkan.so.1"], "installHint": "sudo apt install libvulkan1 mesa-vulkan-drivers   (NVIDIA: the proprietary driver includes it)"},
      "macos-*":   {"systemNames": ["libMoltenVK.dylib", "libvulkan.1.dylib"], "installHint": "brew install molten-vk vulkan-loader"}
    }
  },
  {
    "id": "openal", "title": "OpenAL Soft", "version": "1.24.1", "license": "LGPL-2.0", "category": "audio",
    "description": "Optional 3D audio backend (HRTF, effects). Wicked's built-in FAudio and the miniaudio backend need nothing extra.",
    "platforms": {
      "windows-*": {"systemNames": ["OpenAL32.dll", "soft_oal.dll"], "package": "openal-soft"},
      "linux-*":   {"systemNames": ["libopenal.so.1"], "installHint": "sudo apt install libopenal1", "package": "openal-soft"},
      "macos-*":   {"systemNames": ["libopenal.1.dylib"], "installHint": "brew install openal-soft", "package": "openal-soft"}
    }
  },
  {
    "id": "pulseaudio", "title": "PulseAudio client", "version": "any", "license": "LGPL-2.1", "category": "audio",
    "description": "Linux sound server client used by the miniaudio backend (ALSA and JACK are probed as well).",
    "platforms": {
      "linux-*":   {"systemNames": ["libpulse.so.0", "libasound.so.2", "libjack.so.0"], "installHint": "sudo apt install libpulse0 libasound2"},
      "windows-*": {"supported": false},
      "macos-*":   {"supported": false}
    }
  },
  {
    "id": "ffmpeg", "title": "FFmpeg (LGPL shared)", "version": "master-latest", "license": "LGPL-2.1+", "category": "video",
    "description": "Video decoding for video textures and cutscenes (avcodec/avformat/swscale/swresample). LGPL build: no GPL codecs.",
    "platforms": {
      "windows-x64": {"systemNames": ["avcodec-62.dll", "avcodec-61.dll", "avcodec-60.dll"], "download": {
          "url": "https://github.com/BtbN/FFmpeg-Builds/releases/latest/download/ffmpeg-master-latest-win64-lgpl-shared.zip",
          "checksumUrl": "https://github.com/BtbN/FFmpeg-Builds/releases/latest/download/checksums.sha256", "checksumName": "ffmpeg-master-latest-win64-lgpl-shared.zip",
          "stripComponents": 1, "files": [{"from": "bin/*.dll"}]}},
      "linux-x64":   {"systemNames": ["libavcodec.so.62", "libavcodec.so.61", "libavcodec.so.60", "libavcodec.so"], "installHint": "sudo apt install ffmpeg libavcodec-dev", "download": {
          "url": "https://github.com/BtbN/FFmpeg-Builds/releases/latest/download/ffmpeg-master-latest-linux64-lgpl-shared.tar.xz",
          "checksumUrl": "https://github.com/BtbN/FFmpeg-Builds/releases/latest/download/checksums.sha256", "checksumName": "ffmpeg-master-latest-linux64-lgpl-shared.tar.xz",
          "stripComponents": 1, "files": [{"from": "lib/*.so*"}]}},
      "linux-arm64": {"systemNames": ["libavcodec.so.62", "libavcodec.so.61", "libavcodec.so.60"], "installHint": "sudo apt install ffmpeg", "download": {
          "url": "https://github.com/BtbN/FFmpeg-Builds/releases/latest/download/ffmpeg-master-latest-linuxarm64-lgpl-shared.tar.xz",
          "checksumUrl": "https://github.com/BtbN/FFmpeg-Builds/releases/latest/download/checksums.sha256", "checksumName": "ffmpeg-master-latest-linuxarm64-lgpl-shared.tar.xz",
          "stripComponents": 1, "files": [{"from": "lib/*.so*"}]}},
      "macos-*":     {"systemNames": ["libavcodec.dylib", "libavcodec.62.dylib", "libavcodec.61.dylib"], "installHint": "brew install ffmpeg"}
    }
  }
]})JSON";

		std::string OsKey()
		{
			const std::string t = platform::HostTriple();
			return t.substr(0, t.find('-'));
		}

		bool GlobName(const std::string& pattern, const std::string& name)
		{
			return fs::GlobMatch(pattern, name);
		}
	} // namespace

	const char* LibCategoryName(LibCategory c)
	{
		switch (c)
		{
		case LibCategory::Platform: return "platform";
		case LibCategory::Render: return "render";
		case LibCategory::Audio: return "audio";
		case LibCategory::Video: return "video";
		case LibCategory::Lua: return "lua";
		default: return "tool";
		}
	}

	static LibCategory CategoryFromName(const std::string& n)
	{
		for (LibCategory c : {LibCategory::Platform, LibCategory::Render, LibCategory::Audio, LibCategory::Video, LibCategory::Lua, LibCategory::Tool})
			if (n == LibCategoryName(c))
				return c;
		return LibCategory::Tool;
	}

	static LibPlatformInfo ParsePlatform(const Json& p)
	{
		LibPlatformInfo info;
		info.supported = p.Get("supported").IsBool() ? p.GetBool("supported") : true;
		info.systemNames = p.GetStringArray("systemNames");
		info.installHint = p.GetString("installHint");
		info.package = p.GetString("package");
		const Json& d = p.Get("download");
		if (d.IsObject())
		{
			info.hasDownload = true;
			info.download.url = d.GetString("url");
			info.download.sha256 = d.GetString("sha256");
			info.download.checksumUrl = d.GetString("checksumUrl");
			info.download.checksumName = d.GetString("checksumName");
			info.download.stripComponents = (int)d.GetInt("stripComponents", 0);
			info.download.sizeBytes = (uint64_t)d.GetInt("sizeBytes", 0);
			for (const Json& f : d.Get("files").Items())
			{
				LibFileMap m;
				if (f.IsString())
					m.from = f.AsString();
				else
				{
					m.from = f.GetString("from");
					m.to = f.GetString("to");
				}
				if (!m.from.empty())
					info.download.files.push_back(std::move(m));
			}
		}
		return info;
	}

	void NativeLibs::MergeUserCatalog(const Json& j)
	{
		for (const Json& e : j.Get("libs").Items())
		{
			LibEntry le;
			le.id = e.GetString("id");
			if (le.id.empty())
				continue;
			le.title = e.GetString("title", le.id);
			le.description = e.GetString("description");
			le.version = e.GetString("version");
			le.license = e.GetString("license");
			le.category = CategoryFromName(e.GetString("category"));
			le.requiredOn = e.GetStringArray("required");
			for (const Json::Member& m : e.Get("platforms").Members())
				le.platforms[m.first] = ParsePlatform(m.second);
			bool replaced = false;
			for (LibEntry& ex : catalog_)
				if (ex.id == le.id)
				{
					ex = le;
					replaced = true;
				}
			if (!replaced)
				catalog_.push_back(std::move(le));
		}
	}

	void NativeLibs::LoadBuiltinCatalog()
	{
		Json j;
		std::string err;
		if (!Json::Parse(kBuiltinLibs, j, &err))
		{
			LUMEN_ERROR("libs", "built-in catalog is invalid: %s", err.c_str());
			return;
		}
		MergeUserCatalog(j);
	}

	NativeLibs::NativeLibs(std::string libsDir, Json userCatalog) : libsDir_(std::move(libsDir))
	{
		LoadBuiltinCatalog();
		if (userCatalog.IsObject())
			MergeUserCatalog(userCatalog);
	}

	std::string NativeLibs::HostKey()
	{
		return platform::HostTriple();
	}

	std::string NativeLibs::HostDir() const
	{
		return fs::Join(libsDir_, HostKey());
	}

	const LibEntry* NativeLibs::Find(const std::string& id) const
	{
		for (const LibEntry& e : catalog_)
			if (e.id == id)
				return &e;
		return nullptr;
	}

	const LibPlatformInfo* NativeLibs::PlatformFor(const LibEntry& e) const
	{
		const std::string host = HostKey();
		auto it = e.platforms.find(host);
		if (it != e.platforms.end())
			return &it->second;
		it = e.platforms.find(OsKey() + "-*");
		if (it != e.platforms.end())
			return &it->second;
		return nullptr;
	}

	std::string NativeLibs::MarkerPath(const std::string& id) const
	{
		return fs::Join(HostDir(), ".installed", id + ".json");
	}

	bool NativeLibs::IsInstalled(const LibEntry& e, std::vector<std::string>* files) const
	{
		std::string text;
		Json j;
		if (!fs::ReadText(MarkerPath(e.id), text) || !Json::Parse(text, j, nullptr))
			return false;
		bool all = true;
		for (const std::string& f : j.GetStringArray("files"))
		{
			if (!fs::IsFile(fs::Join(HostDir(), f)))
				all = false;
			else if (files)
				files->push_back(f);
		}
		return all && !j.GetStringArray("files").empty();
	}

	LibStatus NativeLibs::Probe(const std::string& id) const
	{
		LibStatus st;
		const LibEntry* e = Find(id);
		if (!e)
		{
			st.state = LibState::Unsupported;
			st.hint = "unknown library";
			return st;
		}
		const LibPlatformInfo* p = PlatformFor(*e);
		if (!p || !p->supported)
		{
			st.state = LibState::NotNeeded;
			return st;
		}
		std::vector<std::string> files;
		if (IsInstalled(*e, &files))
		{
			st.state = LibState::Managed;
			st.where = fs::Join(HostDir(), files.front());
			return st;
		}
		for (const std::string& name : p->systemNames)
		{
			platform::SharedLibrary lib;
			if (lib.Open(name))
			{
				st.state = LibState::Present;
				st.where = name;
				return st;
			}
			// also look next to the executable
			const std::string beside = fs::Join(platform::ExecutableDir(), name);
			if (fs::IsFile(beside))
			{
				st.state = LibState::Present;
				st.where = beside;
				return st;
			}
		}
		st.state = LibState::Missing;
		st.hint = p->hasDownload ? "can be downloaded" : (!p->installHint.empty() ? p->installHint : (!p->package.empty() ? "build from source: package " + p->package : ""));
		return st;
	}

	Status NativeLibs::Install(const std::string& id, const Progress& progress, std::atomic<bool>* cancel)
	{
		const LibEntry* e = Find(id);
		if (!e)
			return Status::Fail("unknown library \"" + id + "\"");
		const LibPlatformInfo* p = PlatformFor(*e);
		if (!p || !p->supported)
			return Status::Ok(); // nothing to do on this platform
		if (IsInstalled(*e))
			return Status::Ok();
		if (!p->hasDownload)
		{
			if (Probe(id).state == LibState::Present)
				return Status::Ok();
			return Status::Fail(e->title + " is not installed. " + (p->installHint.empty() ? std::string("No download is available for this platform.") : "Install it with: " + p->installHint));
		}
		const LibSource& src = p->download;
		if (!http::IsSafeUrl(src.url))
			return Status::Fail("unsafe download url for " + id);

		std::string expected = src.sha256;
		if (expected.empty() && !src.checksumUrl.empty())
		{
			// Rolling release: take the hash from the checksum list that is published with it.
			http::GetResult r = http::Get(src.checksumUrl);
			if (!r.ok)
				return Status::Fail("cannot fetch checksums for " + id + ": " + r.error);
			for (const std::string& line : str::SplitLines(r.body))
			{
				const std::vector<std::string> parts = str::Split(std::string(str::Trim(line)), ' ', false);
				if (parts.size() >= 2 && parts.back() == src.checksumName && parts[0].size() == 64)
				{
					expected = parts[0];
					break;
				}
			}
			if (expected.empty())
				return Status::Fail("no checksum listed for " + src.checksumName);
		}
		if (expected.empty())
			return Status::Fail("refusing to install " + id + " without a checksum");

		const std::string cache = fs::Join(libsDir_, "downloads");
		fs::CreateDirs(cache);
		const std::string archiveFile = fs::Join(cache, expected.substr(0, 16) + "-" + fs::FileName(src.url));
		if (!(fs::IsFile(archiveFile) && str::EqualsIgnoreCase(Sha256::HashFileHex(archiveFile), expected)))
		{
			http::DownloadOptions d;
			d.url = src.url;
			d.destPath = archiveFile;
			d.sha256 = expected;
			d.cancel = cancel;
			d.progress = [&](uint64_t done, uint64_t total) {
				if (progress)
					progress("Downloading " + e->title, done, total ? total : src.sizeBytes);
			};
			Status s = http::Download(d);
			if (!s)
				return s;
		}
		if (progress)
			progress("Extracting " + e->title, 0, 0);
		const std::string tmp = fs::Join(libsDir_, "tmp-" + fs::UniqueName(id));
		archive::ExtractOptions eo;
		eo.stripComponents = src.stripComponents;
		Status s = archive::Extract(archiveFile, tmp, eo);
		if (!s)
		{
			fs::RemoveAll(tmp);
			return s;
		}

		fs::CreateDirs(HostDir());
		Json files = Json::MakeArray();
		size_t copied = 0;
		fs::ListOptions lo;
		lo.recursive = true;
		std::vector<fs::DirEntry> all;
		fs::Walk(tmp, lo, [&](const fs::DirEntry& de) {
			if (!de.isDirectory)
				all.push_back(de);
			return true;
		});
		for (const LibFileMap& m : src.files)
		{
			for (const fs::DirEntry& de : all)
			{
				const std::string rel = fs::Relative(de.path, tmp);
				if (!GlobName(m.from, rel))
					continue;
				const std::string outName = (m.to.empty() || m.from.find('*') != std::string::npos) ? fs::FileName(rel) : m.to;
				if (outName.find('/') != std::string::npos || outName.find("..") != std::string::npos)
					continue;
				if (!fs::CopyFile(de.path, fs::Join(HostDir(), outName), true))
				{
					fs::RemoveAll(tmp);
					return Status::Fail("cannot install " + outName);
				}
				files.Push(outName);
				++copied;
			}
		}
		fs::RemoveAll(tmp);
		if (copied == 0)
			return Status::Fail("archive for " + id + " did not contain the expected files");
		Json marker = Json::MakeObject();
		marker["id"] = id;
		marker["version"] = e->version;
		marker["sha256"] = expected;
		marker["installedAt"] = platform::UtcTimestamp();
		marker["files"] = std::move(files);
		fs::CreateDirs(fs::Parent(MarkerPath(id)));
		fs::WriteTextIfChanged(MarkerPath(id), marker.Dump(2) + "\n");
		LUMEN_INFO("libs", "installed %s (%zu files)", e->title.c_str(), copied);
		return Status::Ok();
	}

	Status NativeLibs::EnsureRequired(const std::vector<std::string>& optionalIds, const Progress& progress, std::vector<std::string>* report, std::atomic<bool>* cancel)
	{
		std::vector<std::string> ids;
		for (const LibEntry& e : catalog_)
		{
			const bool required = std::find(e.requiredOn.begin(), e.requiredOn.end(), OsKey()) != e.requiredOn.end();
			if (required)
				ids.push_back(e.id);
		}
		for (const std::string& id : optionalIds)
			if (std::find(ids.begin(), ids.end(), id) == ids.end())
				ids.push_back(id);
		Status first = Status::Ok();
		for (const std::string& id : ids)
		{
			if (cancel && cancel->load())
				return Status::Fail("cancelled");
			const LibStatus before = Probe(id);
			if (before.state == LibState::Present || before.state == LibState::Managed || before.state == LibState::NotNeeded)
			{
				if (report)
					report->push_back(id + ": ok (" + (before.state == LibState::Managed ? "managed" : before.state == LibState::Present ? "system" : "not needed") + ")");
				continue;
			}
			Status s = Install(id, progress, cancel);
			if (report)
				report->push_back(id + ": " + (s ? "installed" : "FAILED - " + s.message));
			if (!s && first)
				first = s;
		}
		return first;
	}

	std::vector<std::pair<std::string, std::string>> NativeLibs::LoaderEnvironment() const
	{
		std::vector<std::pair<std::string, std::string>> env;
		const std::string dir = fs::ToNative(HostDir());
#if defined(_WIN32)
		env.push_back({"PATH", dir + ";" + platform::GetEnv("PATH").value_or("")});
#elif defined(__APPLE__)
		env.push_back({"DYLD_FALLBACK_LIBRARY_PATH", dir + ":" + platform::GetEnv("DYLD_FALLBACK_LIBRARY_PATH").value_or("")});
#else
		env.push_back({"LD_LIBRARY_PATH", dir + ":" + platform::GetEnv("LD_LIBRARY_PATH").value_or("")});
#endif
		return env;
	}

	void NativeLibs::AddToLoaderPath() const
	{
#ifdef _WIN32
		const std::string dir = HostDir();
		if (fs::IsDirectory(dir))
		{
			const std::wstring w = platform::Utf8ToWide(fs::ToNative(dir));
			AddDllDirectory(w.c_str());
			SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS);
		}
#endif
	}

	Status NativeLibs::DeployTo(const std::string& directory, std::vector<std::string>* copied) const
	{
		fs::CreateDirs(directory);
		const std::string marks = fs::Join(HostDir(), ".installed");
		for (const fs::DirEntry& m : fs::ListDir(marks))
		{
			std::string text;
			Json j;
			if (!fs::ReadText(m.path, text) || !Json::Parse(text, j, nullptr))
				continue;
			for (const std::string& f : j.GetStringArray("files"))
			{
				const std::string src = fs::Join(HostDir(), f);
				if (!fs::IsFile(src))
					continue;
				if (!fs::CopyFile(src, fs::Join(directory, f), true))
					return Status::Fail("cannot copy " + f);
				if (copied)
					copied->push_back(f);
			}
		}
		return Status::Ok();
	}

	std::string NativeLibs::Describe() const
	{
		std::string out;
		for (const LibEntry& e : catalog_)
		{
			const LibStatus s = Probe(e.id);
			const char* st = s.state == LibState::Managed ? "managed" : s.state == LibState::Present ? "system" : s.state == LibState::NotNeeded ? "n/a" : "MISSING";
			out += StrFormat("%-12s %-8s %-8s %s\n", e.id.c_str(), LibCategoryName(e.category), st, s.where.empty() ? s.hint.c_str() : s.where.c_str());
		}
		return out;
	}

} // namespace lumen
