#include "Panels.h"
#include "lumen/core/GameBuilder.h"

#include "NativeBuild.h"
#include "lumen/core/Process.h"

#include <algorithm>

namespace lumen
{
	namespace
	{
		bool Row(const char* label, const std::function<bool()>& widget)
		{
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label);
			ImGui::SameLine(150);
			ImGui::SetNextItemWidth(-1);
			ImGui::PushID(label);
			const bool c = widget();
			ImGui::PopID();
			return c;
		}
		bool Slider(const char* label, float& v, float a, float b, const char* fmt = "%.2f") { return Row(label, [&] { return ImGui::SliderFloat("##s", &v, a, b, fmt); }); }
		bool Check(const char* label, bool& v) { return Row(label, [&] { return ImGui::Checkbox("##c", &v); }); }
		bool Combo(const char* label, std::string& cur, const std::vector<std::string>& opts)
		{
			bool ch = false;
			Row(label, [&] {
				if (ImGui::BeginCombo("##co", cur.c_str()))
				{
					for (const std::string& o : opts) if (ImGui::Selectable(o.c_str(), o == cur)) { cur = o; ch = true; }
					ImGui::EndCombo();
				}
				return false;
			});
			return ch;
		}
		struct BuildLog { std::mutex m; std::vector<std::string> lines; std::vector<std::string> diags; std::string status; bool running = false; };
		BuildLog& GetBuildLog() { static BuildLog b; return b; }
	}

	// ---------------------------------------------------------------------------------------------------------------------
	void DrawEnvironment(Ui& ui)
	{
		Editor& ed = *ui.ed;
		if (!ImGui::Begin("Environment", &ed.panel.environment))
		{
			ImGui::End();
			return;
		}
		SceneEnvironment& e = ed.scene.environment;
		const std::string before = ed.scene.EnvironmentToJson().Dump(0);
		SectionLabel("Sun");
		Slider("Azimuth", e.sunAzimuth, 0, 360, "%.0f deg");
		Slider("Elevation", e.sunElevation, -10, 90, "%.0f deg");
		Slider("Intensity", e.sunIntensity, 0, 20);
		Row("Colour", [&] { return ColorEdit3("##sc", e.sunColor); });
		Check("Shadows", e.sunShadows);
		SectionLabel("Sky");
		Combo("Mode", e.skyMode, {"procedural", "color", "cubemap"});
		if (e.skyMode == "procedural")
		{
			Slider("Clouds", e.cloudCoverage, 0, 1);
			Slider("Softness", e.skySoftness, 0, 1);
			Slider("Haze", e.haze, 0, 3);
			Slider("Exposure", e.skyExposure, 0.1f, 4);
		}
		else if (e.skyMode == "color")
			Row("Colour", [&] { return ColorEdit3("##sk", e.skyColor); });
		else
			PathField("Cubemap", e.skyCubemap, ".hdr;.png;.jpg;.dds;.ktx", ui);
		SectionLabel("Fog");
		Slider("Density", e.fogDensity, 0, 0.1f, "%.4f");
		Slider("Height", e.fogHeight, -50, 200, "%.0f m");
		Slider("Falloff", e.fogHeightFalloff, 0, 1);
		Row("Colour", [&] { return ColorEdit3("##fc", e.fogColor); });
		SectionLabel("Light & wind");
		Row("Ambient", [&] { return ColorEdit3("##am", e.ambientColor); });
		Slider("Wind speed", e.windSpeed, 0, 30, "%.1f");
		Slider("Wind direction", e.windDirection, 0, 360, "%.0f deg");
		SectionLabel("Ocean");
		Check("Enabled", e.water);
		if (e.water)
		{
			Slider("Height", e.waterHeight, -50, 50, "%.1f");
			Slider("Wave amplitude", e.waterWaveAmplitude, 0, 2);
			Row("Colour", [&] { return ColorEdit3("##wc", e.waterColor); });
		}
		// change detection (the environment is a plain value struct)
		if (ed.scene.EnvironmentToJson().Dump(0) != before) ed.Touch();
		ImGui::End();
	}

	// ---------------------------------------------------------------------------------------------------------------------
	void DrawBuild(Ui& ui)
	{
		Editor& ed = *ui.ed;
		if (!ImGui::Begin("Build", &ed.panel.build))
		{
			ImGui::End();
			return;
		}
		BuildLog& bl = GetBuildLog();
		SectionLabel("Toolchain");
		const Toolchain& tc = ed.toolchain;
		auto tool = [&](const char* label, const ToolInfo& t) {
			ImGui::TextDisabled("%s", label);
			ImGui::SameLine(110);
			if (t.Found()) { ImGui::TextColored(ui::col::Good(), ICON_MD_CHECK " %s", t.version.empty() ? t.path.c_str() : t.version.c_str()); if (t.managed) { ImGui::SameLine(); ImGui::TextDisabled("(managed)"); } }
			else ImGui::TextColored(ui::col::Bad(), ICON_MD_CLOSE " missing");
		};
		tool("CMake", tc.cmake);
		tool("Ninja", tc.ninja);
		tool("C compiler", tc.cc);
		tool("C++ compiler", tc.cxx);
		ImGui::TextDisabled("%s", tc.description.c_str());
		ImGui::TextDisabled("Lumen uses its own cmake, ninja and compiler (downloaded automatically, system tools are ignored).");
		if (!tc.Ready() && ImGui::Button(ICON_MD_CLOUD_DOWNLOAD "  Download missing tools") && ed.toolchainMgr)
			ed.EnsureToolchainAsync(true);
		if (!tc.Ready()) ImGui::SameLine();
		if (ImGui::Button(ICON_MD_REFRESH "  Re-detect") && ed.toolchainMgr)
		{
			ed.toolchain = ed.toolchainMgr->Detect();
			const Status save = ed.PersistToolchain();
			if (!save) ed.Toast(save.message, true);
		}

		SectionLabel("Export game");
		{
			static char outDir[260] = "";
			static bool strip = true;
			static std::string lastOut, lastStatus;
			ImGui::TextDisabled("Standalone folder: <Game>.exe + data.lpak (Lua precompiled, assets packed)");
			ImGui::SetNextItemWidth(-90);
			ImGui::InputTextWithHint("##exportdir", ("default: " + ed.project.Settings().build.outputDir + "/" + ed.project.Settings().name).c_str(), outDir, sizeof(outDir));
			ImGui::SameLine();
			if (ImGui::Button("Reveal##exp") && !lastOut.empty()) platform::OpenInFileManager(lastOut);
			ImGui::Checkbox("Strip Lua debug info", &strip);
			ImGui::SameLine();
			ImGui::Checkbox("Include native module", &ed.project.Settings().native.enabled);
			if (ImGui::Button(ICON_MD_ROCKET_LAUNCH "  Export game", ImVec2(160, 0)))
			{
				ed.SaveAllFiles();
				ed.SaveScene();
				const Project proj = ed.project;
				BuildOptions bo;
				bo.outputDir = outDir;
				bo.stripLua = strip;
				bo.playerExe = fs::Join(platform::ExecutableDir(), platform::ExecutableFileName("lumen-player"));
				for (const std::string& d : {fs::Join(platform::ExecutableDir(), "runtime_data"), std::string(LUMEN_RUNTIME_DATA_DIR)})
					if (fs::IsDirectory(d)) { bo.runtimeDataDir = d; break; }
				ed.StartJob("Export game", [proj, bo, &bl](Job& j) mutable {
					bo.onLog = [&](const std::string& l) { std::lock_guard<std::mutex> g(bl.m); bl.lines.push_back(l); j.SetStatus(l); };
					bo.onProgress = [&](float f) { j.progress = f; };
					BuildReport rep;
					const Status s = BuildGame(proj, bo, rep);
					std::lock_guard<std::mutex> g(bl.m);
					for (const std::string& w : rep.warnings) bl.lines.push_back("warning: " + w);
					bl.status = s.ok ? "exported: " + rep.outputDir : "export failed: " + s.message;
					lastOut = rep.outputDir;
					return s;
				});
			}
		}

		SectionLabel("Native module (C++)");
		NativeSettings& ns = ed.project.Settings().native;
		ImGui::Checkbox("Enabled", &ns.enabled);
		ImGui::SameLine();
		ImGui::TextDisabled("module: %s   sources: native/**", ns.moduleName.c_str());
		const std::string lib = fs::Join(ed.project.NativeBinDir(), platform::SharedLibraryFileName(ns.moduleName));
		const bool built = fs::Exists(lib);
		ImGui::TextColored(built ? ui::col::Good() : ui::col::Muted(), "%s", built ? ("built: " + lib).c_str() : "not built yet");
		const bool running = bl.running;
		if (running) ImGui::BeginDisabled();
		auto startBuild = [&](bool release) {
			ed.SaveAllFiles();
			ed.assets.Refresh();
			{ std::lock_guard<std::mutex> l(bl.m); bl.lines.clear(); bl.diags.clear(); bl.running = true; bl.status = "building..."; }
			const Project proj = ed.project;
			Toolchain toolchain = ed.toolchain;
			ToolchainManager* mgr = ed.toolchainMgr.get();
			const std::string inc = fs::Join(ed.runtimeRoot, "include");
			const PackageEnvironment penv = ed.packages.Environment();
			ed.StartJob(release ? "Native build (release)" : "Native build", [proj, toolchain, mgr, inc, penv, release, &bl](Job& j) mutable {
				if (!toolchain.Ready() && mgr)
				{
					const Status es = mgr->Ensure(toolchain, true, [&](const ToolchainProgress& p) {
						j.SetStatus(p.step);
						if (p.fraction >= 0) j.progress = p.fraction;
						std::lock_guard<std::mutex> l(bl.m);
						bl.status = p.step;
					}, &j.cancel);
					if (!es)
					{
						std::lock_guard<std::mutex> l(bl.m);
						bl.status = es.message;
						bl.running = false;
						return es;
					}
				}
				NativeBuildOptions o;
				o.release = release;
				o.includeDir = inc;
				o.packages = penv;
				o.cancel = &j.cancel;
				o.onOutput = [&](const std::string& chunk) {
					std::lock_guard<std::mutex> l(bl.m);
					for (const std::string& line : str::SplitLines(chunk)) bl.lines.push_back(line);
					if (bl.lines.size() > 4000) bl.lines.erase(bl.lines.begin(), bl.lines.begin() + 1000);
				};
				NativeBuildResult r = BuildNativeModule(proj, toolchain, o);
				std::lock_guard<std::mutex> l(bl.m);
				bl.diags = r.errors;
				bl.status = r.status.ok ? "ok: " + r.library : r.status.message;
				bl.running = false;
				return r.status;
			});
		};
		if (ImGui::Button(ICON_MD_BUILD "  Build (debug)")) startBuild(false);
		ImGui::SameLine();
		if (ImGui::Button("Build (release)")) startBuild(true);
		if (running) ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Open native/ in code editor"))
			for (const AssetEntry* a : ed.assets.OfType(AssetType::Native)) { ed.panel.code = true; ed.OpenFile(a->path); break; }
		{
			std::lock_guard<std::mutex> l(bl.m);
			if (!bl.status.empty()) ImGui::TextDisabled("%s", bl.status.c_str());
			if (!bl.diags.empty())
			{
				ImGui::BeginChild("diags", ImVec2(0, 90), true);
				for (const std::string& d : bl.diags)
				{
					const bool err = d.find("error") != std::string::npos;
					ImGui::PushStyleColor(ImGuiCol_Text, err ? ui::col::Bad() : ui::col::Warn());
					if (ImGui::Selectable(d.c_str()))
					{
						// "path:line:col: ..." -> open the file at that line
						const size_t a = d.find(':', 2);
						if (a != std::string::npos)
						{
							std::string file = d.substr(0, a);
							const int line = atoi(d.c_str() + a + 1);
							const std::string rel = ed.project.ToRelative(file);
							if (!rel.empty()) { ed.panel.code = true; ed.OpenFile(rel, line); }
						}
					}
					ImGui::PopStyleColor();
				}
				ImGui::EndChild();
			}
			ImGui::PushFont(ui::MonoFont());
			ImGui::BeginChild("buildlog", ImVec2(0, 0), true);
			for (const std::string& line : bl.lines) ImGui::TextUnformatted(line.c_str());
			if (running) ImGui::SetScrollHereY(1.0f);
			ImGui::EndChild();
			ImGui::PopFont();
		}
		ImGui::End();
	}

	// ---------------------------------------------------------------------------------------------------------------------
	void DrawPackages(Ui& ui)
	{
		Editor& ed = *ui.ed;
		if (!ImGui::Begin("Packages", &ed.panel.packages))
		{
			ImGui::End();
			return;
		}
		static char query[64] = "";
		static char url[256] = "";
		static char pname[64] = "";
		auto cb = [](Job* j) {
			return [j](const PackageEvent& ev) {
				j->SetStatus(ev.package + (ev.message.empty() ? std::string() : ": " + ev.message));
				if (ev.total) j->progress = float(ev.done) / float(ev.total);
			};
		};
		if (ImGui::Button(ICON_MD_CLOUD_DOWNLOAD "  Sync all"))
			ed.StartJob("Packages sync", [&ed, cb](Job& j) { ed.packages.Open(ed.project); return ed.packages.Sync(cb(&j), false, &j.cancel); });
		ImGui::SameLine();
		if (ImGui::Button(ICON_MD_CHECK "  Verify"))
		{
			const auto problems = ed.packages.Verify();
			ed.Toast(problems.empty() ? "All packages match the lock file" : problems.front(), !problems.empty());
		}
		SectionLabel("Installed");
		if (ed.packages.Installed().empty()) ImGui::TextDisabled("No packages. Add one from the registry or by URL below.");
		for (const InstalledPackage& p : ed.packages.Installed())
		{
			ImGui::PushID(p.spec.name.c_str());
			ImGui::TextUnformatted((std::string(ICON_MD_INVENTORY) + "  " + p.spec.name).c_str());
			ImGui::SameLine();
			ImGui::TextDisabled("%s  %s", p.spec.version.c_str(), p.resolvedRef.substr(0, 8).c_str());
			if (!p.description.empty()) { ImGui::SameLine(); ImGui::TextDisabled("- %s", p.description.c_str()); }
			ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 150);
			if (ImGui::SmallButton("Update")) { const std::string n = p.spec.name; ed.StartJob("Update " + n, [&ed, n, cb](Job& j) { return ed.packages.Update(n, cb(&j)); }); }
			ImGui::SameLine();
			if (ImGui::SmallButton("Remove")) { const std::string n = p.spec.name; ed.StartJob("Remove " + n, [&ed, n, cb](Job& j) { return ed.packages.Remove(n, cb(&j)); }); }
			ImGui::PopID();
		}
		SectionLabel("Registry");
		ImGui::SetNextItemWidth(220);
		ImGui::InputTextWithHint("##pq", ICON_MD_SEARCH "  search", query, sizeof(query));
		ImGui::BeginChild("registry", ImVec2(0, 150), true);
		for (const RegistryEntry& r : ed.packages.SearchRegistry(query))
		{
			ImGui::PushID(r.name.c_str());
			ImGui::TextUnformatted(r.name.c_str());
			ImGui::SameLine(160);
			ImGui::TextDisabled("%s  [%s]", r.description.c_str(), r.kind.c_str());
			ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 50);
			if (ImGui::SmallButton("Add")) { const std::string n = r.name; ed.StartJob("Install " + n, [&ed, n, cb](Job& j) { return ed.packages.AddFromRegistry(n, cb(&j)); }); }
			ImGui::PopID();
		}
		ImGui::EndChild();
		SectionLabel("Add from git / archive URL");
		ImGui::SetNextItemWidth(110);
		ImGui::InputTextWithHint("##pn", "name", pname, sizeof(pname));
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70);
		ImGui::InputTextWithHint("##pu", "https://github.com/user/repo.git", url, sizeof(url));
		ImGui::SameLine();
		if (ImGui::Button("Add") && pname[0] && url[0])
		{
			DependencySpec spec;
			spec.name = pname;
			spec.url = url;
			ed.StartJob("Install " + spec.name, [&ed, spec, cb](Job& j) { return ed.packages.Add(spec, cb(&j)); });
			pname[0] = url[0] = 0;
		}
		ImGui::End();
	}

	// ---------------------------------------------------------------------------------------------------------------------
	void DrawLibraries(Ui& ui)
	{
		Editor& ed = *ui.ed;
		if (!ImGui::Begin("Runtime libraries", &ed.panel.libraries))
		{
			ImGui::End();
			return;
		}
		if (!ed.nativeLibs)
		{
			ImGui::TextDisabled("Runtime folder is not initialised.");
			ImGui::End();
			return;
		}
		ImGui::TextDisabled("Render, window and audio libraries are downloaded next to the engine (%s)", ed.nativeLibs->HostDir().c_str());
		if (ImGui::Button(ICON_MD_CLOUD_DOWNLOAD "  Install required"))
			ed.StartJob("Runtime libraries", [&ed](Job& j) {
				return ed.nativeLibs->EnsureRequired({}, [&j](const std::string& step, uint64_t d, uint64_t t) { j.SetStatus(step); if (t) j.progress = float(d) / float(t); }, nullptr, &j.cancel);
			});
		ImGui::Separator();
		if (ImGui::BeginTable("libs", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Library");
			ImGui::TableSetupColumn("State");
			ImGui::TableSetupColumn("Where");
			ImGui::TableSetupColumn("");
			ImGui::TableHeadersRow();
			for (const LibEntry& l : ed.nativeLibs->Catalog())
			{
				const LibStatus st = ed.nativeLibs->Probe(l.id);
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(l.id.c_str());
				ImGui::TableNextColumn();
				const char* names[] = {"not needed", "present", "managed", "missing", "unsupported"};
				const ImVec4 c = st.state == LibState::Missing ? ui::col::Bad() : st.state == LibState::Unsupported || st.state == LibState::NotNeeded ? ui::col::Muted() : ui::col::Good();
				ImGui::TextColored(c, "%s", names[(int)st.state]);
				ImGui::TableNextColumn();
				ImGui::TextDisabled("%s", st.where.empty() ? st.hint.c_str() : st.where.c_str());
				ImGui::TableNextColumn();
				if (st.state == LibState::Missing)
				{
					ImGui::PushID(l.id.c_str());
					if (ImGui::SmallButton("Install")) { const std::string id = l.id; ed.StartJob("Install " + id, [&ed, id](Job& j) { return ed.nativeLibs->Install(id, [&j](const std::string& s, uint64_t d, uint64_t t) { j.SetStatus(s); if (t) j.progress = float(d) / float(t); }, &j.cancel); }); }
					ImGui::PopID();
				}
			}
			ImGui::EndTable();
		}
		ImGui::End();
	}

	// ---------------------------------------------------------------------------------------------------------------------
	void DrawShaders(Ui& ui)
	{
		Editor& ed = *ui.ed;
		if (!ImGui::Begin("Shaders", &ed.panel.shaders))
		{
			ImGui::End();
			return;
		}
		ShaderSettings& sh = ed.project.Settings().shaders;
		ImGui::Checkbox("Use the Lumen shader library", &sh.builtinLibrary);
		ImGui::TextDisabled("Materials, light model, water, foliage, toon, glass, ice, force field, dissolve and the colour grade.");
		ImGui::TextDisabled("Currently material types are rendered with Wicked's matching built-in shader types.");
		ImGui::Separator();
		const char* names[] = {"pbr", "water", "toon", "foliage", "terrain", "glass", "unlit", "forcefield", "dissolve", "ice", "post_grade"};
		for (const char* n : names)
		{
			bool off = std::find(sh.disabledBuiltins.begin(), sh.disabledBuiltins.end(), n) != sh.disabledBuiltins.end();
			bool on = !off;
			ImGui::BeginDisabled(!sh.builtinLibrary);
			if (ImGui::Checkbox(n, &on))
			{
				if (on) sh.disabledBuiltins.erase(std::remove(sh.disabledBuiltins.begin(), sh.disabledBuiltins.end(), n), sh.disabledBuiltins.end());
				else sh.disabledBuiltins.push_back(n);
			}
			ImGui::EndDisabled();
		}
		ImGui::Separator();
		ImGui::Checkbox("Optimise", &sh.optimize);
		ImGui::SameLine();
		ImGui::Checkbox("Debug info", &sh.debugInfo);
		SectionLabel("Project shaders");
		for (const AssetEntry* a : ed.assets.OfType(AssetType::Shader))
		{
			if (a->inPackage) continue;
			if (ImGui::Selectable((std::string(ICON_MD_PALETTE) + "  " + a->path + "   (" + ShaderKindName(a->shader.kind) + ")").c_str())) { ed.panel.code = true; ed.OpenFile(a->path); }
		}
		if (ImGui::Button("Save to project")) { Status s = ed.SaveProjectSettings(); ed.Toast(s ? "Saved" : s.message, !s); }
		ImGui::End();
	}

	// ---------------------------------------------------------------------------------------------------------------------
	void DrawProjectSettings(Ui& ui)
	{
		Editor& ed = *ui.ed;
		if (!ImGui::Begin("Project settings", &ed.panel.settings))
		{
			ImGui::End();
			return;
		}
		ProjectSettings& s = ed.project.Settings();
		if (ImGui::BeginTabBar("##ps"))
		{
			if (ImGui::BeginTabItem("General"))
			{
				char buf[256];
				auto text = [&](const char* label, std::string& v) {
					snprintf(buf, sizeof(buf), "%s", v.c_str());
					Row(label, [&] { if (ImGui::InputText("##t", buf, sizeof(buf))) v = buf; return false; });
				};
				text("Name", s.name); text("Company", s.company); text("Version", s.gameVersion); text("Description", s.description);
				text("Startup scene", s.startupScene); text("Main script", s.mainScript);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Graphics"))
			{
				GraphicsSettings& g = s.graphics;
				bool ch = false;
				ch |= Combo("API", g.preferredApi, {"vulkan", "dx12"});
				ch |= Check("VSync", g.vsync);
				ch |= Check("Culling", g.culling);
				ch |= Check("Occlusion culling", g.occlusionCulling);
				ch |= Check("Shadows", g.shadows);
				ch |= Check("Reflections", g.reflections);
				ch |= Combo("Anti-aliasing", g.antiAliasing, {"none", "fxaa", "taa"});
				std::string msaa = std::to_string(g.msaa);
				if (Combo("MSAA", msaa, {"1", "2", "4", "8"})) { g.msaa = atoi(msaa.c_str()); ch = true; }
				ch |= Combo("Tonemap", g.tonemap, {"aces", "uchimura", "reinhard"});
				ch |= Slider("Exposure", g.exposure, 0.2f, 3);
				ch |= Slider("Saturation", g.saturation, 0, 1.5f);
				ch |= Slider("Contrast", g.contrast, 0.5f, 1.5f);
				ch |= Check("Bloom", g.bloom);
				ch |= Slider("Bloom threshold", g.bloomThreshold, 0.2f, 4);
				ch |= Check("Dither (anti banding)", g.dither);
				ch |= Check("Eye adaptation", g.eyeAdaptation);
				ch |= Slider("Resolution scale", g.resolutionScale, 0.5f, 2.0f);
				if (ch) ui.graphicsDirty = true;
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Physics"))
			{
				PhysicsSettings& p = s.physics;
				Row("Gravity", [&] { float g[3] = {p.gravity.x, p.gravity.y, p.gravity.z}; if (ImGui::DragFloat3("##g", g, 0.1f)) p.gravity = {g[0], g[1], g[2]}; return false; });
				Row("Fixed rate (Hz)", [&] { return ImGui::SliderFloat("##fr", &p.frameRate, 30, 240, "%.0f"); });
				Row("Accuracy", [&] { return ImGui::SliderInt("##ac", &p.accuracy, 1, 32); });
				Check("Interpolation", p.interpolation);
				Check("Debug draw", p.debugDraw);
				ImGui::TextDisabled("Layers: %s", str::Join(p.layers, ", ").c_str());
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Scripts"))
			{
				Check("Hot reload", s.scripts.hotReload);
				Check("Strip debug info (release)", s.scripts.strip);
				Check("Allow debugger", s.scripts.debug);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Window"))
			{
				char buf[128];
				snprintf(buf, sizeof(buf), "%s", s.window.title.c_str());
				Row("Title", [&] { if (ImGui::InputText("##wt", buf, sizeof(buf))) s.window.title = buf; return false; });
				Row("Width", [&] { return ImGui::InputInt("##ww", &s.window.width); });
				Row("Height", [&] { return ImGui::InputInt("##wh", &s.window.height); });
				Check("Fullscreen", s.window.fullscreen);
				Check("Borderless", s.window.borderless);
				Check("Resizable", s.window.resizable);
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
		ImGui::Separator();
		if (ImGui::Button("Save")) { Status st = ed.SaveProjectSettings(); ed.Toast(st ? "Project saved" : st.message, !st); }
		ImGui::End();
	}
}

namespace lumen
{
	void DrawPreferences(Ui& ui)
	{
		Editor& ed = *ui.ed;
		ImGui::SetNextWindowSize(ImVec2(460, 420), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Preferences", &ed.panel.prefs))
		{
			ImGui::End();
			return;
		}
		Editor::Prefs& p = ed.prefs;
		bool changed = false;
		SectionLabel("Interface");
		changed |= ImGui::SliderFloat("UI scale", &p.uiScale, 0.7f, 2.0f, "%.2fx");
		ImGui::SameLine();
		if (ImGui::SmallButton("Reset##s")) { p.uiScale = 1.0f; changed = true; }
		SectionLabel("Saving");
		changed |= ImGui::SliderInt("Autosave (minutes, 0 = off)", &p.autosaveMinutes, 0, 30);
		ImGui::TextDisabled("Autosave writes the scene and all open code files.");
		SectionLabel("Viewport camera");
		changed |= ImGui::SliderFloat("Mouse sensitivity", &p.mouseSensitivity, 0.2f, 3.0f, "%.2f");
		changed |= ImGui::SliderFloat("Field of view", &p.cameraFov, 30.0f, 110.0f, "%.0f");
		changed |= ImGui::SliderFloat("Fly speed", &p.cameraSpeed, 1.0f, 80.0f, "%.0f");
		changed |= ImGui::Checkbox("Invert vertical look", &p.invertY);
		if (changed)
		{
			const Status s = ed.SavePrefs();
			if (!s) ed.Toast(s.message, true);
			ui.prefsChanged = true;
		}
		SectionLabel("Shortcuts");
		ImGui::TextDisabled("Fly: RMB + WASD/QE (Shift fast, Ctrl slow, wheel = speed)   Orbit: Alt+LMB   Pan: MMB   Focus: F");
		ImGui::TextDisabled("Gizmo: W move, E rotate, R scale   Snap: hold Ctrl   Duplicate: Ctrl+D   Copy/Paste: Ctrl+C / Ctrl+V");
		ImGui::TextDisabled("Play: F5   Stop: Esc   Command palette: Ctrl+P   Select all: Ctrl+A   Deselect: Esc");
		ImGui::End();
	}
}
