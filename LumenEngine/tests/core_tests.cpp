#include "TestFramework.h"

#include "lumen/core/Archive.h"
#include "lumen/core/AssetDatabase.h"
#include "lumen/core/Fs.h"
#include "lumen/core/Image.h"
#include "lumen/core/Json.h"
#include "lumen/core/Lsp.h"
#include "lumen/core/GameBuilder.h"
#include "lumen/core/LuaCompiler.h"
#include "lumen/core/Pak.h"
#include "lumen/core/Packages.h"
#include "lumen/core/Project.h"
#include "lumen/core/Runtime.h"
#include "lumen/core/NativeLibs.h"
#include "lumen/core/SceneDoc.h"
#include "lumen/core/Sha256.h"
#include "lumen/core/LuaDebug.h"
#include "lumen/core/Navigation.h"
#include "lumen/core/Terrain.h"
#include "lumen/core/Animation.h"
extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}
#include "lumen/core/Toolchain.h"
#include "lumen/core/Strings.h"

#include "fixtures.inc"

using namespace lumen;

namespace
{
	struct TempDir
	{
		std::string path;
		TempDir() : path(fs::CreateTempDir("lumen-test")) {}
		~TempDir() { fs::RemoveAll(path); }
	};
}

// ------------------------------------------------------------------------------------------------ strings
TEST(Strings, TrimSplitJoin)
{
	CHECK_EQ(std::string(str::Trim("  hi \n")), std::string("hi"));
	CHECK_EQ(str::Split("a,b,,c", ',').size(), (size_t)4);
	CHECK_EQ(str::Split("a,b,,c", ',', false).size(), (size_t)3);
	CHECK_EQ(str::Join({"a", "b"}, "--"), std::string("a--b"));
	CHECK(str::StartsWith("lumen", "lum"));
	CHECK(str::EndsWith("lumen", "men"));
	CHECK_EQ(str::ReplaceAll("a.b.c", ".", "/"), std::string("a/b/c"));
}

TEST(Strings, ParseNumbers)
{
	int64_t i = 0;
	CHECK(str::ParseInt("-42", i) && i == -42);
	CHECK(!str::ParseInt("12x", i));
	double d = 0;
	CHECK(str::ParseDouble("3.5e2", d) && d == 350.0);
	CHECK(!str::ParseDouble("", d));
}

TEST(Strings, Utf16Offsets)
{
	// "a" + U+1F600 (4 bytes in UTF-8, 2 UTF-16 units) + "b"
	const std::string s = std::string("a") + "\xF0\x9F\x98\x80" + "b";
	CHECK_EQ(str::Utf16Length(s), (size_t)4);
	CHECK_EQ(str::Utf16ToByteOffset(s, 3), (size_t)5);
	CHECK_EQ(str::ByteToUtf16Offset(s, 5), (size_t)3);
}

TEST(Strings, Fuzzy)
{
	CHECK(str::FuzzyScore("gEBN", "getEntityByName") > 0);
	CHECK_EQ(str::FuzzyScore("zzz", "getEntityByName"), 0);
	CHECK(str::FuzzyScore("tex", "texture.png") > str::FuzzyScore("tex", "a_long_name_with_t_e_x.png"));
}

// ------------------------------------------------------------------------------------------------ json
TEST(Json, RoundTrip)
{
	Json j;
	std::string err;
	CHECK(Json::Parse(R"({"a":1,"b":[true,null,2.5,"x\u00e9\n"],"c":{"d":-9007199254740993}})", j, &err));
	CHECK_EQ(j.GetInt("a"), 1);
	CHECK(j.Get("b").Items().size() == 4);
	CHECK_EQ(j.Get("c").GetInt("d"), (int64_t)-9007199254740993ll);
	const std::string dumped = j.Dump(2);
	Json k;
	CHECK(Json::Parse(dumped, k, &err));
	CHECK_EQ(k.Dump(), j.Dump());
}

TEST(Json, ErrorsAndExtensions)
{
	Json j;
	std::string err;
	CHECK(!Json::Parse("{\"a\":", j, &err));
	CHECK(!err.empty());
	CHECK(!Json::Parse("{\"a\":1,}", j, &err));
	Json::ParseOptions o;
	o.allowComments = true;
	o.allowTrailingCommas = true;
	CHECK(Json::Parse("{\"a\":1, // c\n/* d */ \"b\":2,}", j, &err, o));
	CHECK_EQ(j.GetInt("b"), 2);
	// depth bomb must fail instead of overflowing the stack
	std::string deep(100000, '[');
	CHECK(!Json::Parse(deep, j, &err));
}

TEST(Json, SurrogatePairs)
{
	Json j;
	CHECK(Json::Parse(R"("\ud83d\ude00")", j));
	CHECK_EQ(j.AsString(), std::string("\xF0\x9F\x98\x80"));
	CHECK_EQ(Json(std::string("\xF0\x9F\x98\x80")).Dump(), std::string("\"\xF0\x9F\x98\x80\""));
}

// ------------------------------------------------------------------------------------------------ fs
TEST(Fs, PathOps)
{
	CHECK_EQ(fs::Normalize("a//b/./c/../d/"), std::string("a/b/d"));
	CHECK_EQ(fs::Join("a", "b", "c.txt"), std::string("a/b/c.txt"));
	CHECK_EQ(fs::Extension("x/y.tar.gz"), std::string(".gz"));
	CHECK_EQ(fs::Stem("x/y.tar.gz"), std::string("y.tar"));
	CHECK_EQ(fs::Relative("/p/a/b.txt", "/p"), std::string("a/b.txt"));
	CHECK(fs::IsSubPath("/p/a/b", "/p"));
	CHECK(!fs::IsSubPath("/p2/a", "/p"));
}

TEST(Fs, GlobAndFiles)
{
	CHECK(fs::GlobMatch("**/*.lua", "a/b/c.lua"));
	CHECK(fs::GlobMatch("*.lua", "c.lua"));
	CHECK(!fs::GlobMatch("*.lua", "a/c.lua"));
	CHECK(fs::GlobMatch("sh?der.[ph]s", "shader.ps"));
	TempDir t;
	const std::string f = fs::Join(t.path, "x/y/z.txt");
	CHECK(fs::WriteText(f, "hello"));
	std::string text;
	CHECK(fs::ReadText(f, text) && text == "hello");
	bool changed = true;
	CHECK(fs::WriteTextIfChanged(f, "hello", &changed) && !changed);
	CHECK(fs::WriteTextIfChanged(f, "hello2", &changed) && changed);
	CHECK(fs::WriteAtomic(f, "abc", 3));
	CHECK_EQ(fs::FileSize(f), (uint64_t)3);
	fs::ListOptions lo;
	lo.recursive = true;
	CHECK_EQ(fs::ListDir(t.path, lo).size(), (size_t)3); // x, x/y, z.txt
}

// ------------------------------------------------------------------------------------------------ hashing
TEST(Sha256, KnownVectors)
{
	CHECK_EQ(Sha256::HashHex(std::string_view("")), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
	CHECK_EQ(Sha256::HashHex(std::string_view("abc")), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
	const std::string million(1000000, 'a');
	CHECK_EQ(Sha256::HashHex(million), std::string("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
}

TEST(Image, CrcAdlerPng)
{
	const uint8_t data[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
	CHECK_EQ(image::Crc32(data, 9), 0xCBF43926u);
	CHECK_EQ(image::Adler32(data, 9), 0x091E01DEu);
	image::Image img = image::MakeChecker(64, 8);
	const std::vector<uint8_t> png = image::EncodePng(img);
	CHECK(png.size() > 100 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G');
	// water normal map is tileable: first and last column differ by about one pixel step
	image::Image n = image::MakeWaterNormalMap(64, 7, 1.0f);
	CHECK(n.Valid());
	int minZ = 255;
	for (int y = 0; y < 64; ++y)
		for (int x = 0; x < 64; ++x)
			minZ = std::min<int>(minZ, n.Pixel(x, y)[2]);
	CHECK(minZ > 120); // normals point mostly up
}

// ------------------------------------------------------------------------------------------------ archives
TEST(Archive, InflateDynamicBlock)
{
	std::vector<uint8_t> out;
	CHECK(archive::Inflate(kRawDeflate, sizeof(kRawDeflate), out));
	CHECK_EQ(out.size(), kBigTextSize);
	CHECK(std::string(out.begin(), out.begin() + 9) == "line 0000");
	// truncated stream must fail, not crash
	std::vector<uint8_t> bad;
	CHECK(!archive::Inflate(kRawDeflate, sizeof(kRawDeflate) / 2, bad));
}

TEST(Archive, ZipExtractAndStrip)
{
	TempDir t;
	const std::string zip = fs::Join(t.path, "a.zip");
	CHECK(fs::WriteBinary(zip, kZipFixture, sizeof(kZipFixture)));
	archive::ExtractOptions o;
	o.stripComponents = 1;
	Status s = archive::ExtractZip(zip, fs::Join(t.path, "out"), o);
	CHECK(s.ok);
	std::string text;
	CHECK(fs::ReadText(fs::Join(t.path, "out/readme.txt"), text));
	CHECK_EQ(text.size(), kBigTextSize);
	CHECK(fs::ReadText(fs::Join(t.path, "out/lua/init.lua"), text) && text == "return {version='1.0'}\n");
	CHECK(fs::IsDirectory(fs::Join(t.path, "out/empty")));
}

TEST(Archive, ZipExtractCanBeCancelled)
{
	archive::ZipWriter w;
	w.AddFile("first.txt", "first", 5);
	w.AddFile("second.txt", "second", 6);
	TempDir t;
	const std::string zip = fs::Join(t.path, "cancel.zip");
	CHECK(w.Save(zip).ok);

	std::atomic<bool> cancel{false};
	archive::ExtractOptions o;
	o.cancel = &cancel;
	o.onFile = [&](const std::string&) { cancel = true; };
	const std::string out = fs::Join(t.path, "out");
	const Status s = archive::ExtractZip(zip, out, o);

	CHECK(!s.ok);
	CHECK_EQ(s.message, std::string("cancelled"));
	CHECK(fs::IsFile(fs::Join(out, "first.txt")));
	CHECK(!fs::Exists(fs::Join(out, "second.txt")));
}

TEST(Archive, TarGz)
{
	TempDir t;
	const std::string f = fs::Join(t.path, "a.tar.gz");
	CHECK(fs::WriteBinary(f, kTgzFixture, sizeof(kTgzFixture)));
	archive::ExtractOptions o;
	o.stripComponents = 1;
	Status s = archive::Extract(f, fs::Join(t.path, "o"), o);
	CHECK(s.ok);
	std::string text;
	CHECK(fs::ReadText(fs::Join(t.path, "o/sub/b.lua"), text) && text == "x=1\n");
	CHECK(fs::FileSize(fs::Join(t.path, "o/a.txt")) == kBigTextSize);
}

TEST(Archive, ZipSlipRejected)
{
	archive::ZipWriter w;
	w.AddFile("../evil.txt", "x", 1);
	TempDir t;
	const std::string zip = fs::Join(t.path, "evil.zip");
	CHECK(w.Save(zip).ok);
	Status s = archive::ExtractZip(zip, fs::Join(t.path, "out"));
	CHECK(!s.ok);
	CHECK(!fs::Exists(fs::Join(t.path, "evil.txt")));
	CHECK(archive::SafeEntryPath("a/../b", 0).empty());
	CHECK(archive::SafeEntryPath("/abs", 0).empty());
	CHECK(archive::SafeEntryPath("C:/x", 0).empty());
	CHECK(archive::SafeEntryPath("nul.txt", 0).empty());
	CHECK_EQ(archive::SafeEntryPath("top/x/y.txt", 1), std::string("x/y.txt"));
}

TEST(Archive, ZipWriterRoundTrip)
{
	archive::ZipWriter w;
	w.AddFile("a/b.txt", "hello", 5);
	w.AddFile("c.bin", "\0\1\2", 3, true);
	archive::ZipReader r;
	CHECK(r.OpenMemory(w.Finish()).ok);
	CHECK_EQ(r.Entries().size(), (size_t)2);
	std::vector<uint8_t> data;
	CHECK(r.Read(*r.Find("a/b.txt"), data).ok);
	CHECK_EQ(std::string(data.begin(), data.end()), std::string("hello"));
	CHECK(r.Find("c.bin")->IsExecutable());
}

// ------------------------------------------------------------------------------------------------ pak
TEST(Pak, RoundTripAndCorruption)
{
	PakWriter w;
	w.Add("scripts/main.lua", "print('x')", 10, PakEntry_Text);
	std::vector<uint8_t> blob(100000);
	for (size_t i = 0; i < blob.size(); ++i)
		blob[i] = (uint8_t)(i * 31);
	w.Add("data/blob.bin", blob);
	std::vector<uint8_t> bytes = w.Serialize();
	PakReader r;
	CHECK(r.OpenMemory(bytes).ok);
	CHECK(r.Exists("scripts/main.lua"));
	std::string text;
	CHECK(r.ReadText("scripts/main.lua", text) && text == "print('x')");
	std::vector<uint8_t> back;
	CHECK(r.Read("data/blob.bin", back) && back == blob);
	CHECK(r.Verify().empty());
	bytes[bytes.size() / 3] ^= 0xFF;
	PakReader bad;
	const Status s = bad.OpenMemory(bytes);
	if (s.ok)
	{
		std::vector<uint8_t> tmp;
		CHECK(!bad.Verify().empty() || !bad.Read("data/blob.bin", tmp));
	}
}

// ------------------------------------------------------------------------------------------------ lua
TEST(Lua, CompileAndDiagnose)
{
	std::vector<uint8_t> bc;
	lua::Diagnostic d;
	CHECK(lua::CompileString("local x = 1\nreturn x + 1", "t.lua", {}, bc, &d));
	CHECK(lua::IsBytecode(bc.data(), bc.size()));
	CHECK(!lua::CompileString("local x = = 1", "t.lua", {}, bc, &d));
	CHECK_EQ(d.line, 1);
	CHECK(!d.message.empty());
	CHECK(!lua::CheckSyntax("function f(\n", "t.lua", &d));
	CHECK(lua::CheckSyntax("goto a; ::a::", "t.lua"));
	CHECK(lua::VersionString().find("5.4") != std::string::npos);
}

TEST(Lua, BytecodeCache)
{
	TempDir t;
	lua::BytecodeCache cache(t.path, false);
	std::vector<uint8_t> a, b;
	bool hit = true;
	CHECK(cache.GetOrCompileSource("return 1", "c", a, nullptr, &hit) && !hit);
	CHECK(cache.GetOrCompileSource("return 1", "c", b, nullptr, &hit) && hit);
	CHECK(a == b);
}

// ------------------------------------------------------------------------------------------------ project / assets
TEST(Project, CreateLoadSave)
{
	TempDir t;
	ProjectCreateOptions o;
	o.directory = fs::Join(t.path, "Game");
	o.name = "Game";
	Project p;
	Status s = Project::Create(o, p);
	CHECK(s.ok);
	if (!s.ok)
		std::printf("  create: %s\n", s.message.c_str());
	CHECK(fs::IsFile(p.FilePath()));
	CHECK(fs::IsFile(p.Resolve("scenes/main.scene")));
	CHECK(fs::IsFile(p.Resolve("scripts/main.lua")));
	Project q;
	CHECK(Project::Load(o.directory, q).ok);
	CHECK_EQ(q.Settings().name, std::string("Game"));
	q.Settings().window.width = 1234;
	CHECK(q.Save().ok);
	Project r;
	CHECK(Project::Load(q.FilePath(), r).ok);
	CHECK_EQ(r.Settings().window.width, 1234);
	CHECK(r.Validate().empty());
}

TEST(GameBuilder, BuildAndExtract)
{
	TempDir t;
	ProjectCreateOptions o;
	o.directory = fs::Join(t.path, "Game");
	o.name = "My Game";
	o.nativeModule = false;
	Project p;
	CHECK(Project::Create(o, p).ok);
	fs::CreateDirs(p.Resolve("textures"));
	fs::WriteText(p.Resolve("textures/a.txt"), "texture-bytes");
	fs::WriteText(p.Resolve("scripts/util.lua"), "return {answer = 42}\n");
	fs::CreateDirs(p.Resolve("build/junk"));
	fs::WriteText(p.Resolve("build/junk/x.txt"), "old build");
	fs::WriteText(p.Resolve("scripts/broken.lua"), "local = = 1");
	BuildOptions bo;
	bo.outputDir = fs::Join(t.path, "out");
	bo.runtimeDataDir = LUMEN_TEST_RUNTIME_DATA;
	BuildReport r;
	Status s = BuildGame(p, bo, r);
	CHECK(!s.ok); // syntax errors stop the build and name the file
	CHECK(s.message.find("broken.lua") != std::string::npos);
	fs::Remove(p.Resolve("scripts/broken.lua"));
	s = BuildGame(p, bo, r);
	CHECK(s.ok);
	if (!s.ok) std::printf("  build: %s\n", s.message.c_str());
	CHECK(fs::IsFile(r.pakPath));
	CHECK(r.scripts >= 2);
	CHECK(r.scenes >= 1);

	PakReader pak;
	CHECK(pak.Open(r.pakPath).ok);
	CHECK(pak.Exists("game/game.lum"));
	CHECK(pak.Exists("game/textures/a.txt"));
	CHECK(!pak.Exists("game/build/junk/x.txt"));
	CHECK(pak.Exists("runtime/lua/lumen/tween.lua"));
	const PakEntry* e = pak.Find("game/scripts/util.lua");
	CHECK(e && (e->flags & PakEntry_LuaBytecode));
	CHECK(pak.Verify().empty());

	std::string lum;
	const std::string dst = fs::Join(t.path, "cache");
	CHECK(ExtractGamePackage(pak, dst, lum).ok);
	CHECK(fs::IsFile(lum));
	CHECK(fs::IsFile(fs::Join(dst, "game/textures/a.txt")));
	CHECK(fs::IsFile(fs::Join(dst, "runtime/lua/lumen/tween.lua")));
	Project q;
	CHECK(Project::Load(lum, q).ok);
	CHECK_EQ(q.Settings().name, std::string("My Game"));
	CHECK(fs::IsFile(q.Resolve(q.Settings().startupScene)));
	std::string again;
	CHECK(ExtractGamePackage(pak, dst, again).ok); // second launch: nothing to rewrite
}

TEST(Project, LegacyV1)
{
	TempDir t;
	fs::WriteText(fs::Join(t.path, "Old.lum"),
		R"({"format":"lumen-project","version":1,"name":"Old","engine":"0.0.1","startupScene":"scenes/a.scene",
		"packages":[{"name":"foo","version":"1.0","source":"https://example.com/foo.git"}]})");
	Project p;
	CHECK(Project::Load(t.path, p).ok);
	CHECK_EQ(p.Settings().name, std::string("Old"));
	CHECK_EQ(p.Settings().packages.size(), (size_t)1);
	CHECK(p.Settings().packages[0].source == PackageSource::Git);
	CHECK_EQ(p.Settings().packages[0].url, std::string("https://example.com/foo.git"));
}

TEST(AssetDatabase, ScanSearchMoveDeps)
{
	TempDir t;
	ProjectCreateOptions o;
	o.directory = fs::Join(t.path, "G");
	o.name = "G";
	Project p;
	CHECK(Project::Create(o, p).ok);
	fs::WriteText(p.Resolve("shaders/sky.ps.hlsl"), "float4 main():SV_Target{return 1;}");
	fs::WriteText(p.Resolve("shaders/water.frag"), "void main(){}");
	fs::WriteText(p.Resolve("scripts/util.lua"), "return {}");
	fs::WriteText(p.Resolve("scripts/main.lua"), "local u = require('util')\nreturn u");
	AssetDatabase db;
	CHECK(db.Open(p).ok);
	const AssetEntry* sky = db.Find("shaders/sky.ps.hlsl");
	CHECK(sky != nullptr);
	if (sky)
	{
		CHECK(sky->type == AssetType::Shader);
		CHECK(sky->shader.kind == ShaderKind::Pixel);
		CHECK(!sky->guid.empty());
	}
	CHECK(db.Find("shaders/water.frag")->shader.language == ShaderLanguage::Glsl);
	CHECK(fs::IsFile(p.Resolve("shaders/sky.ps.hlsl.meta")));
	AssetSearchOptions so;
	so.query = "util";
	auto hits = db.Search(so);
	CHECK(!hits.empty() && hits[0]->name == "util.lua");
	// dependencies from require()
	auto deps = db.Dependencies("scripts/main.lua");
	CHECK(std::find(deps.begin(), deps.end(), "scripts/util.lua") != deps.end());
	CHECK(!db.Dependents("scripts/util.lua").empty());
	// move keeps the GUID
	const std::string guid = db.GuidOf("scripts/util.lua");
	CHECK(db.CreateFolder("scripts/lib").ok);
	CHECK(db.Move("scripts/util.lua", "scripts/lib").ok);
	CHECK_EQ(db.GuidOf("scripts/lib/util.lua"), guid);
	CHECK(db.Find("scripts/util.lua") == nullptr);
	// external change detection
	fs::WriteText(p.Resolve("scripts/new.lua"), "return 1");
	auto changes = db.Refresh();
	bool added = false;
	for (const AssetChange& c : changes)
		added |= c.kind == AssetChange::Kind::Added && c.path == "scripts/new.lua";
	CHECK(added);
	CHECK(db.Delete("scripts/new.lua").ok);
	CHECK(!fs::Exists(p.Resolve("scripts/new.lua.meta")));
	CHECK(ClassifyShader("objectPS_hologram.hlsl").kind == ShaderKind::Pixel);
	CHECK(ClassifyShader("skyVS.hlsl").kind == ShaderKind::Vertex);
	CHECK(ClassifyShader("blur_gaussianCS.hlsl").kind == ShaderKind::Compute);
	CHECK(ClassifyShader("common.hlsli").kind == ShaderKind::Include);
}


TEST(Packages, LocalPathTransitiveAndLock)
{
	TempDir t;
	ProjectCreateOptions o;
	o.directory = fs::Join(t.path, "G");
	o.name = "G";
	Project p;
	CHECK(Project::Create(o, p).ok);
	// package "alpha" depends on "beta"
	fs::WriteText(fs::Join(t.path, "alpha/lumen-package.json"),
		R"({"name":"alpha","version":"1.2.0","lua":["lib"],"include":["inc"],"defines":["ALPHA=1"],
		"dependencies":[{"name":"beta","source":"path","url":")" + fs::Join(t.path, "beta") + R"("}]})");
	fs::WriteText(fs::Join(t.path, "alpha/lib/alpha.lua"), "return 'alpha'");
	fs::WriteText(fs::Join(t.path, "alpha/inc/alpha.h"), "#pragma once");
	fs::WriteText(fs::Join(t.path, "beta/beta.lua"), "return 'beta'");
	DependencySpec d;
	d.name = "alpha";
	d.source = PackageSource::Path;
	d.url = fs::Join(t.path, "alpha");
	p.Settings().packages.push_back(d);
	PackageManager pm;
	CHECK(pm.Open(p).ok);
	std::vector<PackageEvent> events;
	Status s = pm.Sync([&](const PackageEvent& e) { events.push_back(e); }, true);
	CHECK(s.ok);
	if (!s.ok)
		std::printf("  sync: %s\n", s.message.c_str());
	CHECK(pm.Find("alpha") != nullptr);
	CHECK(pm.Find("beta") != nullptr);
	CHECK(fs::IsFile(p.Resolve("packages/alpha/lib/alpha.lua")));
	CHECK(fs::IsFile(p.Resolve("packages/beta/beta.lua")));
	PackageEnvironment env = pm.Environment();
	CHECK_EQ(env.luaPaths.size(), (size_t)2);
	CHECK_EQ(env.includeDirs.size(), (size_t)1);
	CHECK(std::find(env.defines.begin(), env.defines.end(), "ALPHA=1") != env.defines.end());
	CHECK(fs::IsFile(p.LockFilePath()));
	CHECK(pm.Verify().empty());
	// tampering is detected, a second sync repairs it
	fs::WriteText(p.Resolve("packages/alpha/lib/alpha.lua"), "return 'evil'");
	CHECK(!pm.Verify().empty());
	CHECK(pm.Sync({}, true).ok);
	std::string text;
	CHECK(fs::ReadText(p.Resolve("packages/alpha/lib/alpha.lua"), text) && text == "return 'alpha'");
	// removing the dependency prunes the transitive one
	p.Settings().packages.clear();
	PackageManager pm2;
	CHECK(pm2.Open(p).ok);
	CHECK(pm2.Sync({}, true).ok);
	CHECK(!fs::Exists(p.Resolve("packages/alpha")));
	CHECK(!fs::Exists(p.Resolve("packages/beta")));
}

TEST(Packages, NamesAndRegistry)
{
	CHECK(PackageManager::IsValidName("lume"));
	CHECK(!PackageManager::IsValidName("../evil"));
	CHECK(!PackageManager::IsValidName("Up"));
	CHECK(!PackageManager::IsValidName(""));
	TempDir t;
	ProjectCreateOptions o;
	o.directory = fs::Join(t.path, "G");
	o.name = "G";
	Project p;
	CHECK(Project::Create(o, p).ok);
	PackageManager pm;
	CHECK(pm.Open(p).ok);
	CHECK(pm.Registry().size() >= 10);
	CHECK(!pm.SearchRegistry("json").empty());
	DependencySpec bad;
	bad.name = "evil";
	bad.source = PackageSource::Git;
	bad.url = "https://user:pw@example.com/x.git";
	CHECK(!pm.Add(bad).ok);
	bad.url = "file:///etc";
	CHECK(!pm.Add(bad).ok);
}

TEST(Packages, GitRegistryNetwork)
{
	TempDir t;
	ProjectCreateOptions o;
	o.directory = fs::Join(t.path, "G");
	o.name = "G";
	Project p;
	CHECK(Project::Create(o, p).ok);
	PackageManager pm;
	CHECK(pm.Open(p).ok);
	Status s = pm.AddFromRegistry("lume");
	if (!s.ok)
	{
		std::printf("  (network test skipped: %s)\n", s.message.c_str());
		return;
	}
	CHECK(fs::IsFile(p.Resolve("packages/lume/lume.lua")));
	CHECK(!fs::Exists(p.Resolve("packages/lume/.git")));
	CHECK(!pm.Environment().luaPaths.empty());
	Project reloaded;
	CHECK(Project::Load(p.FilePath(), reloaded).ok);
	CHECK_EQ(reloaded.Settings().packages.size(), (size_t)1);
}

TEST(Toolchain, DetectSystem)
{
	TempDir t;
	ToolchainManager tm(fs::Join(t.path, "toolchain"));
	Toolchain tc = tm.Detect();
	std::printf("  %s\n", tc.description.c_str());
	CHECK(!tm.Recipes().empty());
	for (const auto& r : tm.Recipes())
		CHECK(r.sha256.size() == 64 && str::StartsWith(r.url, "https://"));
}

TEST(Toolchain, DownloadManagedCMakeNinja)
{
	TempDir t;
	Json cfg = Json::MakeObject();
	cfg["useSystemTools"] = false;
	ToolchainManager tm(fs::Join(t.path, "toolchain"), cfg);
	CHECK(!tm.Detect().cmake.Found());
	// Only CMake + Ninja here (a compiler download is ~150 MB on Windows and a system package elsewhere).
	Toolchain tc;
	Status s = tm.Ensure(tc, true, [](const ToolchainProgress&) {});
	if (tc.cmake.Found() && tc.ninja.Found())
	{
		CHECK(tc.cmake.managed && tc.ninja.managed);
		CHECK(!tc.cmake.version.empty());
		std::printf("  managed: cmake %s, ninja %s\n", tc.cmake.version.c_str(), tc.ninja.version.c_str());
	}
	else
		std::printf("  (network test skipped: %s)\n", s.message.c_str());
}

TEST(Runtime, FirstRunAndUpdate)
{
	TempDir t;
	const uint8_t a[] = {'a', '1'};
	const uint8_t b[] = {'b', '2'};
	EmbeddedFile files[] = {
		{"lua/lumen/a.lua", a, 2, str::Hash64(a, 2)},
		{"include/b.h", b, 2, str::Hash64(b, 2)},
	};
	RuntimeOptions o;
	o.explicitRoot = fs::Join(t.path, "lumen_runtime");
	o.embedded = {files, 2};
	o.installLogFile = false;
	CHECK(Runtime::Initialize(o).ok);
	CHECK(Runtime::Info().firstRun);
	CHECK_EQ(Runtime::Info().filesInstalled, 2);
	CHECK(fs::IsFile(fs::Join(o.explicitRoot, "lua/lumen/a.lua")));
	CHECK(fs::IsDirectory(Runtime::Layout().packages));
	CHECK(fs::IsFile(fs::Join(o.explicitRoot, "config/engine.json")));
	// user edit is preserved, new version stored beside it
	fs::WriteText(fs::Join(o.explicitRoot, "include/b.h"), "user");
	const uint8_t b2[] = {'b', '3'};
	files[1] = {"include/b.h", b2, 2, str::Hash64(b2, 2)};
	CHECK(Runtime::Initialize(o).ok);
	CHECK(!Runtime::Info().firstRun);
	CHECK_EQ(Runtime::Info().filesKeptModified, 1);
	std::string text;
	CHECK(fs::ReadText(fs::Join(o.explicitRoot, "include/b.h"), text) && text == "user");
	CHECK(fs::IsFile(fs::Join(o.explicitRoot, "include/b.h.lumen-new")));
	// unmodified file is updated silently
	const uint8_t a2[] = {'a', '9'};
	files[0] = {"lua/lumen/a.lua", a2, 2, str::Hash64(a2, 2)};
	CHECK(Runtime::Initialize(o).ok);
	CHECK_EQ(Runtime::Info().filesUpdated, 1);
	CHECK(fs::ReadText(fs::Join(o.explicitRoot, "lua/lumen/a.lua"), text) && text == "a9");
}

TEST(NativeLibs, CatalogProbeAndDeploy)
{
	const std::string dir = fs::CreateTempDir("nl");
	NativeLibs libs(fs::Join(dir, "libs"));
	CHECK(libs.Find("sdl2") != nullptr);
	CHECK(libs.Find("vulkan") != nullptr);
	CHECK(libs.Find("openal") != nullptr);
	CHECK(libs.Find("ffmpeg") != nullptr);
	CHECK(libs.Find("nope") == nullptr);
	CHECK(!libs.Describe().empty());
	CHECK(libs.Install("nope").ok == false);
	// user catalog overrides / extends
	Json user;
	CHECK(Json::Parse(R"({"libs":[{"id":"mylib","title":"My","platforms":{"*-*":{"installHint":"x"}}}]})", user, nullptr));
	NativeLibs libs2(fs::Join(dir, "libs"), user);
	CHECK(libs2.Find("mylib") != nullptr);
	// fake managed install: marker + file, then probe and deploy
	fs::CreateDirs(fs::Join(libs.HostDir(), ".installed"));
	fs::WriteText(fs::Join(libs.HostDir(), "fake.so"), "x");
	fs::WriteText(fs::Join(libs.HostDir(), ".installed/sdl2.json"), R"({"id":"sdl2","files":["fake.so"]})");
	CHECK(libs.Probe("sdl2").state == LibState::Managed);
	std::vector<std::string> copied;
	CHECK(libs.DeployTo(fs::Join(dir, "out"), &copied).ok);
	CHECK_EQ(copied.size(), (size_t)1);
	CHECK(fs::IsFile(fs::Join(dir, "out/fake.so")));
	CHECK(!libs.LoaderEnvironment().empty());
	fs::RemoveAll(dir);
}

TEST(SceneDoc, PhysicsComponentsRoundTrip)
{
	SceneDoc d;
	SceneEntity e = MakePrimitiveEntity("cube", "Car");
	e.rigidbody = SceneRigidBody{};
	e.rigidbody->gravityFactor = 0.5f;
	e.rigidbody->lock2D = true;
	e.vehicle = SceneVehicle{};
	e.vehicle->type = "motorcycle";
	e.vehicle->wheelFL = "w1";
	e.joint = SceneJoint{};
	e.joint->type = "hinge";
	e.joint->connected = "other";
	e.joint->motorSpeed = 2.5f;
	e.joint->breakDistance = 3;
	d.Add(e);
	SceneDoc back;
	CHECK(SceneDoc::FromJson(d.ToJson(), back).ok);
	const SceneEntity& r = back.entities[0];
	CHECK(r.rigidbody && r.vehicle && r.joint);
	CHECK_EQ(r.rigidbody->gravityFactor, 0.5f);
	CHECK(r.rigidbody->lock2D);
	CHECK_EQ(r.vehicle->type, std::string("motorcycle"));
	CHECK_EQ(r.vehicle->wheelFL, std::string("w1"));
	CHECK_EQ(r.joint->type, std::string("hinge"));
	CHECK_EQ(r.joint->connected, std::string("other"));
	CHECK_EQ(r.joint->motorSpeed, 2.5f);
	CHECK_EQ(r.joint->breakDistance, 3.0f);
}

TEST(SceneDoc, GameplayComponentsRoundTrip)
{
	SceneDoc d;
	SceneEntity e = MakePrimitiveEntity("cube", "Thing");
	e.animator = SceneAnimator{};
	e.animator->autoplay = "Run";
	e.animator->autoplayClip = "anim/door.lanim";
	e.animator->autoplayLoop = false;
	e.animator->clips.push_back({"Run", false, true, 1.5f, 0.5f});
	e.particles = SceneParticles{};
	e.particles->rate = 321;
	e.particles->shader = "simple";
	e.particles->emitFromMesh = true;
	e.particles->texture = "fx/spark.png";
	e.terrain = SceneTerrain{};
	e.terrain->resolution = 257;
	e.terrain->seed = 42;
	e.terrain->heightmap = "terrain/a.height";
	SceneTerrain::Layer l;
	l.name = "snow";
	l.minHeight = 0.8f;
	e.terrain->layers.push_back(l);
	e.navmesh = SceneNavMesh{};
	e.navmesh->agentRadius = 0.6f;
	e.navmesh->bakeOnStart = false;
	e.navagent = SceneNavAgent{};
	e.navagent->speed = 7;
	e.navagent->autoMove = false;
	d.Add(e);
	SceneDoc back;
	CHECK(SceneDoc::FromJson(d.ToJson(), back).ok);
	const SceneEntity& r = back.entities[0];
	CHECK(r.animator && r.particles && r.terrain && r.navmesh && r.navagent);
	CHECK_EQ(r.animator->autoplay, std::string("Run"));
	CHECK_EQ(r.animator->autoplayClip, std::string("anim/door.lanim"));
	CHECK(!r.animator->autoplayLoop);
	CHECK(r.animator->clips.size() == 1 && !r.animator->clips[0].loop && r.animator->clips[0].pingPong);
	CHECK_EQ(r.animator->clips[0].speed, 1.5f);
	CHECK_EQ(r.particles->rate, 321.0f);
	CHECK_EQ(r.particles->shader, std::string("simple"));
	CHECK(r.particles->emitFromMesh);
	CHECK_EQ(r.terrain->resolution, 257);
	CHECK_EQ(r.terrain->heightmap, std::string("terrain/a.height"));
	CHECK(r.terrain->layers.size() == 1 && r.terrain->layers[0].name == "snow");
	CHECK_EQ(r.navmesh->agentRadius, 0.6f);
	CHECK(!r.navmesh->bakeOnStart);
	CHECK_EQ(r.navagent->speed, 7.0f);
	CHECK(!r.navagent->autoMove);
}

TEST(SceneDoc, PrefabExportImport)
{
	SceneDoc d;
	const std::string root = d.Add(MakePrimitiveEntity("cube", "Door"));
	SceneEntity frame = MakePrimitiveEntity("cube", "Frame");
	frame.parent = root;
	const std::string frameId = d.Add(frame);
	SceneEntity leaf = MakePrimitiveEntity("cube", "Leaf");
	leaf.parent = root;
	leaf.rigidbody = SceneRigidBody{};
	leaf.joint = SceneJoint{};
	leaf.joint->type = "hinge";
	leaf.joint->connected = frameId;
	const std::string leafId = d.Add(leaf);
	const Json prefab = d.ExportSubtree(root);
	CHECK(SceneDoc::IsPrefabJson(prefab));
	CHECK_EQ(prefab.Get("entities").Items().size(), (size_t)3);

	SceneDoc other;
	const std::string holder = other.Add(MakeEmptyEntity("Level"));
	std::vector<std::string> ids;
	const std::string nr = other.ImportSubtree(prefab, holder, &ids);
	CHECK(!nr.empty());
	CHECK_EQ(ids.size(), (size_t)3);
	CHECK_EQ(other.Find(nr)->parent, holder);
	CHECK(other.Validate().empty());
	const SceneEntity* newLeaf = nullptr;
	for (const std::string& id : ids) if (other.Find(id)->name == "Leaf") newLeaf = other.Find(id);
	CHECK(newLeaf && newLeaf->joint);
	CHECK(newLeaf->joint->connected != frameId); // reference rewritten to the copy
	CHECK(other.Find(newLeaf->joint->connected) && other.Find(newLeaf->joint->connected)->name == "Frame");
	(void)leafId;
	const std::string dup = d.Duplicate(root);
	CHECK(!dup.empty() && d.Children(dup).size() == 2);
	CHECK(d.Validate().empty());
	CHECK(!d.ImportSubtree(Json::MakeObject(), "").size());
}

TEST(SceneDoc, RoundTripAndHierarchy)
{
	SceneDoc d;
	d.name = "t";
	d.environment.sunIntensity = 3.5f;
	d.environment.water = true;
	const std::string a = d.Add(MakePrimitiveEntity("cube", "Cube"));
	SceneEntity child = MakePrimitiveEntity("sphere", "Cube");
	child.parent = a;
	child.material->color = {1, 0.5f, 0.25f, 1};
	child.material->shaderParams = Json::MakeObject();
	child.material->shaderParams["glow"] = 2.5;
	child.scripts.push_back({"scripts/spin.lua", true, Json()});
	child.extra["custom"] = 7;
	const std::string b = d.Add(child);
	d.Add(MakePrimitiveEntity("cube", "Cube")); // unique among siblings: "Cube 1"
	CHECK_EQ(d.entities[2].name, std::string("Cube 1"));
	CHECK(d.Validate().empty());
	CHECK(d.IsDescendant(b, a));
	CHECK(!d.SetParent(a, b)); // would create a cycle
	const std::string dup = d.Duplicate(a);
	CHECK(!dup.empty());
	CHECK_EQ(d.Children(dup).size(), (size_t)1);
	CHECK(d.Validate().empty());

	const std::string dir = fs::CreateTempDir("scene");
	const std::string file = fs::Join(dir, "a.scene");
	CHECK(d.Save(file).ok);
	SceneDoc r;
	Status s = SceneDoc::Load(file, r);
	CHECK(s.ok);
	CHECK_EQ(r.entities.size(), d.entities.size());
	CHECK(r.environment.water);
	CHECK(r.Find(b) != nullptr);
	CHECK_EQ(r.Find(b)->material->shaderParams.GetDouble("glow"), 2.5);
	CHECK_EQ(r.Find(b)->scripts.size(), (size_t)1);
	CHECK_EQ(r.Find(b)->extra.GetInt("custom"), (int64_t)7);
	CHECK(r.ToJson() == d.ToJson());
	std::vector<std::string> refs = r.ReferencedAssets();
	CHECK_EQ(refs.size(), (size_t)1);
	CHECK(r.Remove(a));
	CHECK_EQ(r.Find(b), nullptr); // children go with their parent
	CHECK(r.Validate().empty());
	fs::WriteText(fs::Join(dir, "bad.scene"), R"({"format":"lumen-scene","version":2,"entities":[{"id":"x","name":"n","parent":"missing"}]})");
	SceneDoc bad;
	CHECK(!SceneDoc::Load(fs::Join(dir, "bad.scene"), bad).ok);
	fs::WriteText(fs::Join(dir, "new.scene"), R"({"format":"lumen-scene","version":99})");
	CHECK(!SceneDoc::Load(fs::Join(dir, "new.scene"), bad).ok);
	fs::RemoveAll(dir);
}

namespace
{
	Json Req(int id, const char* method, Json params)
	{
		Json j = Json::MakeObject();
		j["jsonrpc"] = "2.0";
		j["id"] = id;
		j["method"] = method;
		j["params"] = std::move(params);
		return j;
	}
	Json Note(const char* method, Json params)
	{
		Json j = Json::MakeObject();
		j["jsonrpc"] = "2.0";
		j["method"] = method;
		j["params"] = std::move(params);
		return j;
	}
	Json Pos(const std::string& uri, int line, int ch)
	{
		Json p = Json::MakeObject();
		p["textDocument"]["uri"] = uri;
		p["position"]["line"] = line;
		p["position"]["character"] = ch;
		return p;
	}
	bool HasLabel(const Json& items, const std::string& label)
	{
		for (const Json& it : items.Items())
			if (it.GetString("label") == label) return true;
		return false;
	}
}

TEST(Lsp, StubsCompletionHoverDiagnostics)
{
	const std::string api = fs::Join(LUMEN_TEST_RUNTIME_DATA, "lua/api");
	CHECK(fs::IsDirectory(api));
	TempDir tmp;
	lsp::Server server({api}, {});
	Json init = Json::MakeObject();
	init["rootUri"] = lua::PathToUri(tmp.path);
	auto r = server.Handle(Req(1, "initialize", init));
	CHECK_EQ(r.size(), (size_t)1);
	CHECK(r[0].Get("result").Get("capabilities").GetBool("hoverProvider"));
	server.Handle(Note("initialized", Json::MakeObject()));

	const std::string uri = lua::PathToUri(fs::Join(tmp.path, "scripts/a.lua"));
	const std::string src =
		"local M = { speed = 5 }\n"            // 0
		"function M:update(dt)\n"              // 1
		"  local e = lumen.scene.find(\"Player\")\n" // 2
		"  e:\n"                                // 3
		"  local v = lumen.vec3(1, 2, 3)\n"    // 4
		"  v:\n"                                // 5
		"  lumen.sc\n"                          // 6
		"  print(undefined_thing, dt)\n"       // 7
		"  local unused = 1\n"                 // 8
		"end\n"
		"return M\n";
	Json open = Json::MakeObject();
	open["textDocument"]["uri"] = uri;
	open["textDocument"]["text"] = src;
	open["textDocument"]["version"] = 1;
	r = server.Handle(Note("textDocument/didOpen", open));
	CHECK_EQ(r.size(), (size_t)1);
	// the file has a syntax error ("e:" + newline) -> an error diagnostic and nothing else
	CHECK_EQ(r[0].Get("params").Get("diagnostics").At(0).GetInt("severity"), (int64_t)1);

	// fix the syntax: complete the member accesses so that the semantic checks run
	const std::string good =
		"local M = { speed = 5 }\n"
		"function M:update(dt)\n"
		"  local e = lumen.scene.find(\"Player\")\n"
		"  e:position()\n"
		"  local v = lumen.vec3(1, 2, 3)\n"
		"  v:length()\n"
		"  print(undefined_thing, dt)\n"
		"  local unused = 1\n"
		"end\n"
		"return M\n";
	Json change = Json::MakeObject();
	change["textDocument"]["uri"] = uri;
	change["textDocument"]["version"] = 2;
	Json ch = Json::MakeObject();
	ch["text"] = good;
	change["contentChanges"].Push(ch);
	r = server.Handle(Note("textDocument/didChange", change));
	const Json& diags = r[0].Get("params").Get("diagnostics");
	bool undefinedGlobal = false, unusedLocal = false, other = false;
	for (const Json& d : diags.Items())
	{
		const std::string m = d.GetString("message");
		if (m == "Undefined global 'undefined_thing'") undefinedGlobal = true;
		else if (m == "Unused local 'unused'") unusedLocal = true;
		else other = true;
	}
	CHECK(undefinedGlobal);
	CHECK(unusedLocal);
	CHECK(!other);

	// completion after "e:" resolves the type through lumen.scene.find -> Entity
	auto setText = [&](const std::string& text, int version) {
		Json c = Json::MakeObject();
		c["textDocument"]["uri"] = uri;
		c["textDocument"]["version"] = version;
		Json x = Json::MakeObject();
		x["text"] = text;
		c["contentChanges"].Push(x);
		server.Handle(Note("textDocument/didChange", c));
	};
	setText("local e = lumen.scene.find(\"Player\")\ne:\n", 3);
	r = server.Handle(Req(2, "textDocument/completion", Pos(uri, 1, 2)));
	const Json& items = r[0].Get("result");
	CHECK(HasLabel(items, "position"));
	CHECK(HasLabel(items, "set_position"));
	CHECK(HasLabel(items, "look_at"));
	CHECK(!HasLabel(items, "print"));

	setText("local v = lumen.vec3(1, 2, 3)\nv:\n", 4);
	r = server.Handle(Req(3, "textDocument/completion", Pos(uri, 1, 2)));
	CHECK(HasLabel(r[0].Get("result"), "normalize"));
	CHECK(HasLabel(r[0].Get("result"), "dot"));

	// namespaces
	setText("lumen.\n", 5);
	r = server.Handle(Req(4, "textDocument/completion", Pos(uri, 0, 6)));
	CHECK(HasLabel(r[0].Get("result"), "scene"));
	CHECK(HasLabel(r[0].Get("result"), "input"));
	CHECK(HasLabel(r[0].Get("result"), "quit"));

	// general completion: locals, globals, keywords
	setText("local speed = 1\nlocal function jump(h) end\nsp\n", 6);
	r = server.Handle(Req(5, "textDocument/completion", Pos(uri, 2, 2)));
	CHECK(HasLabel(r[0].Get("result"), "speed"));
	CHECK(HasLabel(r[0].Get("result"), "jump"));
	CHECK(HasLabel(r[0].Get("result"), "print"));
	CHECK(HasLabel(r[0].Get("result"), "function"));

	// hover on an API function
	setText("local p = lumen.scene.find(\"x\")\n", 7);
	r = server.Handle(Req(6, "textDocument/hover", Pos(uri, 0, 22)));
	const std::string hover = r[0].Get("result").Get("contents").GetString("value");
	CHECK(hover.find("lumen.scene.find(name: string)") != std::string::npos);

	// signature help
	setText("lumen.scene.raycast(a, \n", 8);
	r = server.Handle(Req(7, "textDocument/signatureHelp", Pos(uri, 0, 24)));
	CHECK_EQ(r[0].Get("result").Get("activeParameter").AsInt(), (int64_t)1);

	// definition / references / rename of a local
	setText("local count = 1\ncount = count + 1\nprint(count)\n", 9);
	r = server.Handle(Req(8, "textDocument/definition", Pos(uri, 2, 8)));
	CHECK_EQ(r[0].Get("result").Get("range").Get("start").GetInt("line"), (int64_t)0);
	r = server.Handle(Req(9, "textDocument/references", Pos(uri, 0, 7)));
	CHECK_EQ(r[0].Get("result").Size(), (size_t)4);
	Json rn = Pos(uri, 0, 7);
	rn["newName"] = "total";
	r = server.Handle(Req(10, "textDocument/rename", rn));
	CHECK_EQ(r[0].Get("result").Get("changes").Get(uri).Size(), (size_t)4);
	rn["newName"] = "end";
	r = server.Handle(Req(11, "textDocument/rename", rn));
	CHECK(r[0].Get("result").IsNull());

	// require of a project module gives members of the returned table
	fs::WriteText(fs::Join(tmp.path, "scripts/util.lua"), "local U = {}\nfunction U.double(x) return x * 2 end\nU.limit = 10\nreturn U\n");
	server.Handle(Note("workspace/didChangeWatchedFiles", Json::MakeObject()));
	setText("local u = require \"util\"\nu.\n", 10);
	r = server.Handle(Req(12, "textDocument/completion", Pos(uri, 1, 2)));
	CHECK(HasLabel(r[0].Get("result"), "double"));
	CHECK(HasLabel(r[0].Get("result"), "limit"));

	// document symbols
	setText("local function a() end\nfunction b() end\n", 11);
	r = server.Handle(Req(13, "textDocument/documentSymbol", [&] { Json p = Json::MakeObject(); p["textDocument"]["uri"] = uri; return p; }()));
	CHECK(r[0].Get("result").Size() >= 2);

	r = server.Handle(Req(14, "shutdown", Json()));
	CHECK(r[0].Contains("result"));
	server.Handle(Note("exit", Json()));
	CHECK(server.ShouldExit());
	CHECK_EQ(server.ExitCode(), 0);
}

TEST(Lsp, AnalyzerScopes)
{
	lua::DocInfo d;
	lua::Analyze(d, "file:///x.lua",
		"local a = 1\n"
		"do local a = 2; print(a) end\n"
		"local function f(p, q) return p + a end\n"
		"for i, v in ipairs({}) do print(i, v) end\n"
		"x = 5\n");
	int shadow = -1, outer = -1;
	for (size_t i = 0; i < d.symbols.size(); ++i)
		if (d.symbols[i].name == "a")
		{
			if (outer < 0) outer = (int)i;
			else shadow = (int)i;
		}
	CHECK(outer >= 0 && shadow >= 0);
	CHECK_EQ(d.symbols[(size_t)outer].uses, 1); // used inside f
	CHECK_EQ(d.symbols[(size_t)shadow].uses, 1); // used in print(a)
	CHECK(d.assignedGlobals.count("x") == 1);
	CHECK_EQ(lua::ByteColToUtf16("a\xC3\xA9b", 3), 2);
	CHECK_EQ(lua::Utf16ToByteCol("a\xC3\xA9b", 2), 3);
}

namespace
{
	void NavQuad(std::vector<lumen::nav::Triangle>& t, float x0, float z0, float x1, float z1, float y)
	{
		// upward facing: (b-a)x(c-a) points to +Y
		t.push_back({{x0, y, z0}, {x1, y, z1}, {x1, y, z0}});
		t.push_back({{x0, y, z0}, {x0, y, z1}, {x1, y, z1}});
	}
	void NavWall(std::vector<lumen::nav::Triangle>& t, float x0, float z0, float x1, float z1, float y0, float y1)
	{
		t.push_back({{x0, y0, z0}, {x1, y0, z1}, {x1, y1, z1}});
		t.push_back({{x0, y0, z0}, {x1, y1, z1}, {x0, y1, z0}});
	}
}

TEST(Navigation, PathAroundWallAndBlockers)
{
	using namespace lumen;
	std::vector<nav::Triangle> tris;
	NavQuad(tris, -10, -10, 10, 10, 0);
	NavWall(tris, 0, -6, 0, 6, 0, 3); // wall along z at x=0 from z=-6..6, gap beyond
	nav::BuildSettings s;
	s.cellSize = 0.25f;
	s.agentRadius = 0.3f;
	s.boundsMin = {-10, -2, -10};
	s.boundsMax = {10, 6, 10};
	nav::NavMesh m;
	CHECK(m.Build(tris, s));
	CHECK(m.GetStats().walkable > 1000);
	Float3 p;
	CHECK(m.Project({-5, 0.3f, 0}, p));
	CHECK(std::fabs(p.y) < 0.01f);
	// straight line is blocked by the wall
	CHECK(!m.Raycast({-5, 0, 0}, {5, 0, 0}));
	CHECK(m.Raycast({-5, 0, 8}, {5, 0, 8}));
	std::vector<Float3> path;
	CHECK(m.FindPath({-5, 0, 0}, {5, 0, 0}, path));
	CHECK(path.size() >= 3);
	float len = 0;
	bool crossesGap = false;
	for (size_t i = 1; i < path.size(); ++i)
	{
		const float dx = path[i].x - path[i - 1].x, dz = path[i].z - path[i - 1].z;
		len += std::sqrt(dx * dx + dz * dz);
		if ((path[i - 1].x < 0) != (path[i].x < 0))
		{
			const float t = (0 - path[i - 1].x) / (path[i].x - path[i - 1].x);
			crossesGap |= std::fabs(path[i - 1].z + (path[i].z - path[i - 1].z) * t) >= 5.5f; // crosses x=0 beyond the wall end
		}
	}
	CHECK(len > 10.5f && len < 20.0f); // longer than straight, shorter than a lap
	for (const Float3& q : path) CHECK(!(std::fabs(q.x) < 0.2f && std::fabs(q.z) < 5.7f)); // never inside the wall
	CHECK(crossesGap);
	// a blocker across the gap makes the goal unreachable
	const int b = m.AddBlocker({-0.5f, -1, 5.5f}, {0.5f, 2, 10});
	const int b2 = m.AddBlocker({-0.5f, -1, -10}, {0.5f, 2, -5.5f});
	CHECK(!m.FindPath({-5, 0, 0}, {5, 0, 0}, path));
	bool partial = false;
	CHECK(m.FindPath({-5, 0, 0}, {5, 0, 0}, path, true, &partial) && partial);
	m.RemoveBlocker(b);
	m.RemoveBlocker(b2);
	CHECK(m.FindPath({-5, 0, 0}, {5, 0, 0}, path));
	std::mt19937 rng(5);
	Float3 r;
	CHECK(m.RandomPoint({5, 0, 5}, 3.0f, rng, r));
	CHECK(std::fabs(r.x - 5) <= 3.1f);
}

TEST(Navigation, StepsSlopesAndClearance)
{
	using namespace lumen;
	std::vector<nav::Triangle> tris;
	NavQuad(tris, -6, -6, 6, 6, 0);
	// low ledge (0.3) is climbable, high ledge (1.0) is not: island at x in [2,4]
	NavQuad(tris, 2, -2, 4, 2, 1.0f);
	NavWall(tris, 2, -2, 2, 2, 0, 1.0f);
	// ceiling at 1.2 over x in [-4,-2] leaves too little head room for a 1.8 agent
	NavQuad(tris, -4, -2, -2, 2, 1.2f);
	nav::BuildSettings s;
	s.cellSize = 0.25f;
	s.agentRadius = 0.25f;
	s.boundsMin = {-6, -2, -6};
	s.boundsMax = {6, 4, 6};
	nav::NavMesh m;
	CHECK(m.Build(tris, s));
	Float3 p;
	CHECK(m.Project({3, 1.0f, 0}, p, 0.5f)); // top of the ledge is a surface...
	std::vector<Float3> path;
	CHECK(!m.FindPath({-5, 0, 0}, {3, 1.0f, 0}, path)); // ...but not reachable (step too high)
	// under the low ceiling: no span at floor level within the covered area
	Float3 q;
	const bool under = m.Project({-3, 0, 0}, q, 0.3f) && std::fabs(q.x + 3) < 0.4f && std::fabs(q.y) < 0.1f;
	CHECK(!under);
}

TEST(Terrain, GenerateBrushRawMesh)
{
	using namespace lumen;
	SceneTerrain t;
	t.resolution = 65;
	t.size = {64, 64};
	t.height = 20;
	terrain::Heightfield a, b;
	terrain::Generate(t, a);
	terrain::Generate(t, b);
	CHECK(a.Valid() && a.res == 65);
	CHECK(a.h == b.h); // deterministic
	float mn = 2, mx = -1;
	for (float v : a.h) { mn = std::min(mn, v); mx = std::max(mx, v); }
	CHECK(mn >= 0 && mx <= 1 && mx - mn > 0.1f); // real relief
	t.seed = 7;
	terrain::Heightfield c;
	terrain::Generate(t, c);
	CHECK(c.h != a.h);
	// island falloff pulls the border down
	SceneTerrain isl = t;
	isl.edgeFalloff = 0.4f;
	terrain::Heightfield i2;
	terrain::Generate(isl, i2);
	CHECK(i2.At(0, 0) < 0.01f && i2.At(64, 32) < 0.01f);
	// brushes
	terrain::Heightfield f;
	f.res = 65;
	f.h.assign(65 * 65, 0.5f);
	int rect[4];
	CHECK(terrain::ApplyBrush(f, t, terrain::Brush::Raise, 0.5f, 0.5f, 6.0f, 1.0f, 0, rect));
	CHECK(f.At(32, 32) > 0.5f && f.At(0, 0) == 0.5f);
	const float peak = f.At(32, 32);
	terrain::ApplyBrush(f, t, terrain::Brush::Smooth, 0.5f, 0.5f, 6.0f, 1.0f, 0);
	CHECK(f.At(32, 32) < peak);
	terrain::ApplyBrush(f, t, terrain::Brush::Flatten, 0.5f, 0.5f, 6.0f, 1.0f, 0.5f);
	terrain::ApplyBrush(f, t, terrain::Brush::Lower, 0.5f, 0.5f, 6.0f, 1.0f, 0);
	CHECK(f.At(32, 32) < 0.5f);
	// raw file
	TempDir tmp;
	const std::string path = fs::Join(tmp.path, "a.height");
	CHECK(terrain::SaveRaw(path, f));
	terrain::Heightfield g;
	CHECK(terrain::LoadRaw(path, 65, g) && g.h == f.h);
	CHECK(!terrain::LoadRaw(path, 33, g));
	// mesh: flat terrain faces up, tall peak gets rock/snow colours
	terrain::Heightfield flat;
	flat.res = 17;
	flat.h.assign(17 * 17, 0.3f);
	SceneTerrain ft; ft.resolution = 17; ft.size = {16, 16}; ft.height = 10;
	terrain::Mesh m;
	terrain::BuildMesh(ft, flat, m);
	CHECK_EQ((int)m.positions.size(), 17 * 17);
	CHECK_EQ((int)m.indices.size(), 16 * 16 * 6);
	CHECK(m.normals[100].y > 0.99f);
	CHECK(std::fabs(terrain::HeightAt(ft, flat, 0, 0) - 3.0f) < 1e-3f);
	// triangle winding agrees with the shading normal under the nav convention ((b-a)x(c-a) up)
	const Float3 &p0 = m.positions[m.indices[0]], &p1 = m.positions[m.indices[1]], &p2 = m.positions[m.indices[2]];
	const Float3 e1{p1.x - p0.x, p1.y - p0.y, p1.z - p0.z}, e2{p2.x - p0.x, p2.y - p0.y, p2.z - p0.z};
	const float ny = e1.z * e2.x - e1.x * e2.z; // y of the right-handed cross product
	CHECK(ny != 0);
	terrain::Heightfield hi;
	hi.res = 17;
	hi.h.assign(17 * 17, 0.95f);
	terrain::BuildMesh(ft, hi, m);
	CHECK(m.colors[100].x > 0.8f && m.colors[100].z > 0.8f); // snow
	terrain::Heightfield lo;
	lo.res = 17;
	lo.h.assign(17 * 17, 0.03f);
	terrain::BuildMesh(ft, lo, m);
	CHECK(m.colors[100].x > m.colors[100].z); // sand is warm
}

TEST(Animation, ClipRoundTripSamplingEditing)
{
	using namespace lumen;
	anim::Clip c;
	c.name = "open";
	c.loop = false;
	anim::Track& p = anim::EnsureTrack(c, "", "position");
	anim::AddKey(p, 0.0f, {0, 0, 0});
	anim::AddKey(p, 2.0f, {0, 4, 0});
	anim::AddKey(p, 1.0f, {0, 1, 0}); // out of order insert
	CHECK_EQ((int)p.keys.size(), 3);
	CHECK_EQ(p.keys[1].time, 1.0f);
	anim::Track& r = anim::EnsureTrack(c, "Hinge", "rotation");
	r.interp = "step";
	anim::AddKey(r, 0, {0, 0, 0});
	anim::AddKey(r, 1, {0, 90, 0});
	c.events.push_back({1.5f, "creak"});
	c.events.push_back({0.5f, "start"});
	anim::SortEvents(c);
	c.duration = anim::ComputeDuration(c);
	CHECK_EQ(c.duration, 2.0f);
	// sampling
	CHECK_EQ(anim::Sample(p, 0.5f).y, 0.5f);
	CHECK_EQ(anim::Sample(p, 1.5f).y, 2.5f);
	CHECK_EQ(anim::Sample(p, -1).y, 0.0f);
	CHECK_EQ(anim::Sample(p, 9).y, 4.0f);
	CHECK_EQ(anim::Sample(r, 0.99f).y, 0.0f); // step
	CHECK_EQ(anim::Sample(r, 1.0f).y, 90.0f);
	anim::Track sm;
	sm.interp = "smooth";
	anim::AddKey(sm, 0, {0, 0, 0});
	anim::AddKey(sm, 1, {1, 0, 0});
	CHECK(std::fabs(anim::Sample(sm, 0.25f).x - 0.15625f) < 1e-5f);
	CHECK_EQ(anim::Sample(anim::Track{"", "scale", "linear", {}}, 0).x, 1.0f); // default scale
	// editing
	CHECK_EQ(anim::FindKey(p, 1.001f), 1);
	CHECK_EQ(anim::FindKey(p, 1.5f), -1);
	const int moved = anim::MoveKey(p, 0, 3.0f);
	CHECK_EQ(moved, 2);
	CHECK_EQ(p.keys[0].time, 1.0f);
	CHECK(anim::RemoveKey(p, 0) && p.keys.size() == 2);
	anim::AddKey(p, 0.0f, {0, 0, 0});
	// events
	CHECK_EQ((int)anim::EventsBetween(c, 0.4f, 1.6f).size(), 2);
	CHECK_EQ((int)anim::EventsBetween(c, 1.6f, 0.6f).size(), 1); // wrapped: only "start" at 0.5? (0.5 <= 0.6)
	// IO
	lumen::Json j = anim::ToJson(c);
	anim::Clip back;
	std::string err;
	CHECK(anim::FromJson(j, back, &err));
	CHECK_EQ(back.name, std::string("open"));
	CHECK_EQ((int)back.tracks.size(), 2);
	CHECK_EQ(back.tracks[1].target, std::string("Hinge"));
	CHECK_EQ(back.tracks[1].interp, std::string("step"));
	CHECK_EQ((int)back.events.size(), 2);
	CHECK_EQ(back.events[0].name, std::string("start"));
	TempDir tmp;
	const std::string path = fs::Join(tmp.path, "a/open.lanim");
	CHECK(anim::Save(path, c));
	anim::Clip disk;
	CHECK(anim::Load(path, disk, &err));
	CHECK_EQ((int)disk.tracks[0].keys.size(), (int)c.tracks[0].keys.size());
	CHECK(!anim::Load(fs::Join(tmp.path, "missing.lanim"), disk, &err));
	CHECK(!anim::FromJson(Json::ParseOrNull("{\"format\":\"x\"}"), disk, &err));
}

TEST(LuaDebug, BreakpointsStepsInspectEval)
{
	lua_State* L = luaL_newstate();
	luaL_openlibs(L);
	const std::string src =
		"local function add(a, b)\n"      // 1
		"  local s = a + b\n"              // 2
		"  return s\n"                     // 3
		"end\n"                            // 4
		"local t = {x = 1, y = {z = 2}}\n" // 5
		"local r = add(1, 2)\n"            // 6
		"r = r + add(r, 10)\n"             // 7
		"return r\n";                      // 8
	lua::Debugger dbg;
	dbg.Attach(L);
	dbg.SetBreakpoint("/proj/a.lua", 2, "a == 3");

	std::vector<std::string> events;
	dbg.onPause = [&](lua::Debugger& d) {
		const auto stack = d.Stack();
		events.push_back(d.PauseReason() + "@" + std::to_string(d.CurrentLine()));
		if (events.size() == 1)
		{
			// breakpoint with condition: only the second call (a == 3)
			CHECK(stack.size() >= 2);
			CHECK_EQ(stack[0].name, std::string("add"));
			bool found = false;
			for (const auto& v : d.Locals(0)) if (v.name == "a") { found = true; CHECK_EQ(v.value, std::string("3")); }
			CHECK(found);
			bool ok = false;
			CHECK_EQ(d.Eval(0, "a * 100 + b", ok), std::string("310"));
			CHECK(ok);
			d.Eval(0, "nosuch.field", ok);
			CHECK(!ok);
			d.StepOut();
		}
		else if (events.size() == 2)
		{
			// after step out we are back in the main chunk
			bool ok = false;
			CHECK_EQ(d.Eval(0, "t.y.z", ok), std::string("2"));
			bool expandable = false;
			for (const auto& v : d.Locals(0))
				if (v.name == "t" && v.ref > 0)
				{
					expandable = true;
					bool hasY = false;
					for (const auto& c : d.Expand(v.ref)) if (c.name == "y" && c.ref > 0) hasY = true;
					CHECK(hasY);
				}
			CHECK(expandable);
			d.Resume();
		}
		else
			d.Resume();
	};
	const std::string chunk = "@/proj/a.lua";
	CHECK(luaL_loadbuffer(L, src.data(), src.size(), chunk.c_str()) == LUA_OK);
	CHECK(lua_pcall(L, 0, 1, 0) == LUA_OK);
	CHECK_EQ((int)lua_tointeger(L, -1), 16);
	CHECK_EQ((int)events.size(), 2);
	CHECK(events.size() >= 1 && events[0] == "breakpoint@2");
	// no breakpoints and Run mode: the hook is removed again
	dbg.ClearBreakpoints();
	CHECK(lua_gethook(L) == nullptr);
	dbg.Detach();
	lua_close(L);
}

TEST(LuaDebug, StepInAndPauseRequest)
{
	lua_State* L = luaL_newstate();
	luaL_openlibs(L);
	lua::Debugger dbg;
	dbg.Attach(L);
	dbg.BreakNow("manual");
	std::vector<int> lines;
	dbg.onPause = [&](lua::Debugger& d) {
		lines.push_back(d.CurrentLine());
		if (lines.size() < 4) d.StepIn(); else d.Resume();
	};
	const std::string src = "local function f()\n local x = 1\n return x\nend\nlocal v = f()\nreturn v\n";
	CHECK(luaL_loadbuffer(L, src.data(), src.size(), "@/p/b.lua") == LUA_OK);
	CHECK(lua_pcall(L, 0, 1, 0) == LUA_OK);
	CHECK_EQ((int)lines.size(), 4);
	if (lines.size() == 4)
	{
		CHECK_EQ(lines[0], 4); // closure creation is attributed to the `end` line
		CHECK_EQ(lines[1], 5);
		CHECK_EQ(lines[2], 2); // stepped into f
		CHECK_EQ(lines[3], 3);
	}
	dbg.Detach();
	lua_close(L);
}

int main(int argc, char** argv)
{
	return lumentest::RunAll(argc > 1 ? argv[1] : nullptr);
}
