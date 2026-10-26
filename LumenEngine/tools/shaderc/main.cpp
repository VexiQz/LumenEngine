// lumen-shaderc — offline shader compiler/validator built on the DXC integration of Wicked Engine.
//
//   lumen-shaderc [options] <file.hlsl|dir> ...
//     -I <dir>          include directory (repeatable)
//     -D NAME[=VALUE]   define (repeatable)
//     -o <dir>          write compiled blobs (<name>.<format>.bin) here
//     --format spirv|hlsl6   (default spirv)
//     --stage vs|ps|cs|gs|hs|ds|ms|as   force stage (otherwise derived from the file name)
//     --sm 6_0 ... 6_6  minimum shader model (default 6_0)
//     --entry main
//     -q                only print failures
//
// The stage is derived from the file name: *VS.hlsl, *PS.hlsl, *CS.hlsl, ... or "name.<stage>.hlsl".
#include "WickedEngine.h"
#include "wiShaderCompiler.h"

#include "lumen/core/Fs.h"
#include "lumen/core/Strings.h"

#include <cstdio>
#include <cstring>

using namespace lumen;
using wi::graphics::ShaderStage;

namespace
{
	struct Options
	{
		std::vector<std::string> inputs;
		std::vector<std::string> includes;
		std::vector<std::string> defines;
		std::string outDir;
		wi::graphics::ShaderFormat format = wi::graphics::ShaderFormat::SPIRV;
		wi::graphics::ShaderModel sm = wi::graphics::ShaderModel::SM_6_0;
		std::string entry = "main";
		bool forceStage = false;
		ShaderStage stage = ShaderStage::PS;
		bool quiet = false;
	};

	bool ParseStage(std::string s, ShaderStage& out)
	{
		s = str::ToLower(s);
		if (s == "vs") out = ShaderStage::VS;
		else if (s == "ps") out = ShaderStage::PS;
		else if (s == "cs") out = ShaderStage::CS;
		else if (s == "gs") out = ShaderStage::GS;
		else if (s == "hs") out = ShaderStage::HS;
		else if (s == "ds") out = ShaderStage::DS;
		else if (s == "ms") out = ShaderStage::MS;
		else if (s == "as") out = ShaderStage::AS;
		else return false;
		return true;
	}

	bool DeriveStage(const std::string& path, ShaderStage& out)
	{
		const std::string stem = fs::Stem(path);
		// "water.ps" style
		const size_t dot = stem.rfind('.');
		if (dot != std::string::npos && ParseStage(stem.substr(dot + 1), out))
			return true;
		// "...PS" style (case sensitive on the last two letters, e.g. objectPS_xxx is handled by the underscore rule)
		const size_t us = stem.find('_');
		const std::string head = us == std::string::npos ? stem : stem.substr(0, us);
		if (head.size() > 2 && ParseStage(head.substr(head.size() - 2), out) && std::isupper((unsigned char)head[head.size() - 1]))
			return true;
		const size_t last = stem.rfind('_');
		if (last != std::string::npos && ParseStage(stem.substr(last + 1), out))
			return true;
		if (stem.size() > 2 && std::isupper((unsigned char)stem[stem.size() - 1]) && ParseStage(stem.substr(stem.size() - 2), out))
			return true;
		return false;
	}

	void Collect(const std::string& in, std::vector<std::string>& files)
	{
		if (fs::IsDirectory(in))
		{
			fs::ListOptions lo;
			lo.recursive = true;
			fs::Walk(in, lo, [&](const fs::DirEntry& e) {
				if (!e.isDirectory && str::EndsWith(e.path, ".hlsl"))
					files.push_back(e.path);
				return true;
			});
		}
		else
			files.push_back(in);
	}
} // namespace

int main(int argc, char** argv)
{
	Options o;
	for (int i = 1; i < argc; ++i)
	{
		const std::string a = argv[i];
		const auto next = [&]() -> std::string { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
		if (a == "-I") o.includes.push_back(next());
		else if (str::StartsWith(a, "-I") && a.size() > 2) o.includes.push_back(a.substr(2));
		else if (a == "-D") o.defines.push_back(next());
		else if (str::StartsWith(a, "-D") && a.size() > 2) o.defines.push_back(a.substr(2));
		else if (a == "-o") o.outDir = next();
		else if (a == "-q") o.quiet = true;
		else if (a == "--entry") o.entry = next();
		else if (a == "--format")
		{
			const std::string f = next();
			o.format = f == "hlsl6" ? wi::graphics::ShaderFormat::HLSL6 : wi::graphics::ShaderFormat::SPIRV;
		}
		else if (a == "--sm")
		{
			const std::string v = next();
			if (v == "6_1") o.sm = wi::graphics::ShaderModel::SM_6_1;
			else if (v == "6_2") o.sm = wi::graphics::ShaderModel::SM_6_2;
			else if (v == "6_3") o.sm = wi::graphics::ShaderModel::SM_6_3;
			else if (v == "6_4") o.sm = wi::graphics::ShaderModel::SM_6_4;
			else if (v == "6_5") o.sm = wi::graphics::ShaderModel::SM_6_5;
			else if (v == "6_6") o.sm = wi::graphics::ShaderModel::SM_6_6;
			else o.sm = wi::graphics::ShaderModel::SM_6_0;
		}
		else if (a == "--stage")
		{
			o.forceStage = ParseStage(next(), o.stage);
		}
		else if (!a.empty() && a[0] != '-') o.inputs.push_back(a);
		else
		{
			std::fprintf(stderr, "unknown option %s\n", a.c_str());
			return 2;
		}
	}
	if (o.inputs.empty())
	{
		std::fprintf(stderr, "usage: lumen-shaderc [-I dir] [-D X] [-o outdir] [--format spirv|hlsl6] <files|dirs>\n");
		return 2;
	}

	std::vector<std::string> files;
	for (const std::string& in : o.inputs)
		Collect(in, files);

	int failed = 0, ok = 0, skipped = 0;
	for (const std::string& file : files)
	{
		// Header-only includes and shared fragments are not entry points.
		ShaderStage stage = o.stage;
		if (!o.forceStage && !DeriveStage(file, stage))
		{
			++skipped;
			continue;
		}
		wi::shadercompiler::CompilerInput in;
		in.format = o.format;
		in.stage = stage;
		in.minshadermodel = o.sm;
		in.shadersourcefilename = fs::Absolute(file);
		in.entrypoint = o.entry;
		in.include_directories = {fs::Parent(fs::Absolute(file))};
		for (const std::string& d : o.includes)
			in.include_directories.push_back(fs::Absolute(d));
		for (const std::string& d : o.defines)
			in.defines.push_back(d);
		wi::shadercompiler::CompilerOutput out;
		wi::shadercompiler::Compile(in, out);
		if (!out.IsValid())
		{
			++failed;
			std::printf("FAIL %s\n%s\n", file.c_str(), out.error_message.c_str());
			continue;
		}
		++ok;
		if (!o.quiet)
			std::printf("ok   %-60s %zu bytes%s\n", file.c_str(), out.shadersize, out.error_message.empty() ? "" : " (warnings)");
		if (!out.error_message.empty() && !o.quiet)
			std::printf("%s\n", out.error_message.c_str());
		if (!o.outDir.empty())
		{
			fs::CreateDirs(o.outDir);
			const std::string name = fs::Stem(file) + (o.format == wi::graphics::ShaderFormat::SPIRV ? ".spv" : ".dxil");
			fs::WriteBinary(fs::Join(o.outDir, name), out.shaderdata, out.shadersize);
		}
	}
	std::printf("%d compiled, %d failed, %d skipped\n", ok, failed, skipped);
	return failed ? 1 : 0;
}
