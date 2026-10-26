#include "Editor.h"
#include "imgui.h"

#include "lumen/core/Fs.h"
#include "lumen/core/Strings.h"

#include <algorithm>

namespace lumen
{
	void Editor::LoadPrefs()
	{
		if (runtimeRoot.empty()) return;
		std::string text;
		Json j;
		if (!fs::ReadText(fs::Join(runtimeRoot, "config/editor.json"), text) || !Json::Parse(text, j)) return;
		prefs.uiScale = std::clamp((float)j.GetDouble("uiScale", prefs.uiScale), 0.7f, 2.5f);
		prefs.autosaveMinutes = std::clamp((int)j.GetInt("autosaveMinutes", prefs.autosaveMinutes), 0, 120);
		prefs.mouseSensitivity = std::clamp((float)j.GetDouble("mouseSensitivity", prefs.mouseSensitivity), 0.1f, 4.0f);
		prefs.cameraFov = std::clamp((float)j.GetDouble("cameraFov", prefs.cameraFov), 20.0f, 120.0f);
		prefs.cameraSpeed = std::clamp((float)j.GetDouble("cameraSpeed", prefs.cameraSpeed), 0.5f, 200.0f);
		prefs.invertY = j.GetBool("invertY", false);
	}

	Status Editor::SavePrefs() const
	{
		if (runtimeRoot.empty()) return Status::Fail("runtime folder not available");
		Json j = Json::MakeObject();
		j["uiScale"] = prefs.uiScale;
		j["autosaveMinutes"] = prefs.autosaveMinutes;
		j["mouseSensitivity"] = prefs.mouseSensitivity;
		j["cameraFov"] = prefs.cameraFov;
		j["cameraSpeed"] = prefs.cameraSpeed;
		j["invertY"] = prefs.invertY;
		fs::CreateDirs(fs::Join(runtimeRoot, "config"));
		return fs::WriteText(fs::Join(runtimeRoot, "config/editor.json"), j.Dump(1)) ? Status::Ok() : Status::Fail("cannot write editor.json");
	}

	Editor::Editor()
	{
		ring = Log::InstallRingSink(16384);
		if (Runtime::IsInitialized())
		{
			runtimeRoot = Runtime::Layout().root;
			nativeLibs = std::make_unique<NativeLibs>(fs::Join(runtimeRoot, "libs"));
			toolchainMgr = std::make_unique<ToolchainManager>(Runtime::Layout().toolchain, Runtime::Config().Get("toolchain"));
			toolchain = toolchainMgr->Detect();
			const Status s = PersistToolchain();
			if (!s)
				LUMEN_WARN("toolchain", "cannot save detected toolchain: %s", s.message.c_str());
			LUMEN_INFO("toolchain", "%s", toolchain.description.c_str());
			LoadPrefs();
			lsp.Init(Runtime::Layout().luaApi, {Runtime::Layout().lua});
			if (Runtime::Config().Get("toolchain").GetBool("autoDownload", true))
				EnsureToolchainAsync();
		}
	}

	void Editor::EnsureToolchainAsync(bool announce)
	{
		if (!toolchainMgr || toolchainBusy)
			return;
		if (toolchain.Ready())
			return;
		toolchainBusy = true;
		StartJob("Downloading build tools", [this, announce](Job& j) {
			Toolchain out;
			const Status s = toolchainMgr->Ensure(out, true, [&j](const ToolchainProgress& p) {
				j.SetStatus(p.step);
				if (p.fraction >= 0)
					j.progress = p.fraction;
			}, &j.cancel);
			{
				std::lock_guard<std::mutex> l(toolchainMutex);
				pendingToolchain = out;
			}
			hasPendingToolchain = true;
			(void)announce;
			return s;
		});
	}

	bool Editor::HasBreakpoint(const std::string& rel, int line) const
	{
		auto it = breakpoints.find(rel);
		if (it == breakpoints.end()) return false;
		for (const EdBreakpoint& b : it->second) if (b.line == line) return true;
		return false;
	}

	void Editor::ToggleBreakpoint(const std::string& rel, int line)
	{
		auto& list = breakpoints[rel];
		auto it = std::find_if(list.begin(), list.end(), [&](const EdBreakpoint& b) { return b.line == line; });
		if (it != list.end()) list.erase(it);
		else { EdBreakpoint b; b.line = line; list.push_back(b); }
		if (list.empty()) breakpoints.erase(rel);
		++bpVersion;
		SaveBreakpoints();
	}

	void Editor::SaveBreakpoints() const
	{
		if (!project.IsValid()) return;
		Json root = Json::MakeObject();
		Json files = Json::MakeObject();
		for (const auto& f : breakpoints)
		{
			Json arr = Json::MakeArray();
			for (const EdBreakpoint& b : f.second)
			{
				Json o = Json::MakeObject();
				o["line"] = b.line;
				if (!b.cond.empty()) o["cond"] = b.cond;
				o["enabled"] = b.enabled;
				arr.Push(o);
			}
			files[f.first] = arr;
		}
		root["files"] = files;
		Json w = Json::MakeArray();
		for (const std::string& s : dbg.watches) w.Push(s);
		root["watches"] = w;
		fs::CreateDirs(project.MetaDir());
		fs::WriteTextIfChanged(fs::Join(project.MetaDir(), "breakpoints.json"), root.Dump(1));
	}

	void Editor::LoadBreakpoints()
	{
		breakpoints.clear();
		dbg.watches.clear();
		std::string text;
		Json root;
		if (!project.IsValid() || !fs::ReadText(fs::Join(project.MetaDir(), "breakpoints.json"), text) || !Json::Parse(text, root))
			return;
		for (const auto& f : root.Get("files").Members())
			for (const Json& b : f.second.Items())
			{
				EdBreakpoint e;
				e.line = (int)b.GetInt("line");
				e.cond = b.GetString("cond");
				e.enabled = b.GetBool("enabled", true);
				if (e.line > 0) breakpoints[f.first].push_back(e);
			}
		for (const Json& w : root.Get("watches").Items()) dbg.watches.push_back(w.AsString());
		++bpVersion;
	}

	Status Editor::PersistToolchain()
	{
		if (runtimeRoot.empty())
			return Status::Fail("runtime is not initialized");
		const std::string path = fs::Join(runtimeRoot, "config/toolchain.json");
		if (!fs::WriteTextIfChanged(path, toolchain.ToJson().Dump(2) + "\n"))
			return Status::Fail("cannot write " + path);
		return Status::Ok();
	}

	Editor::~Editor()
	{
		for (auto& j : jobs)
			j->cancel = true;
		jobs.clear();
	}

	// ---- project ----------------------------------------------------------------------------------------------------

	std::vector<std::string> Editor::Recent() const
	{
		std::vector<std::string> out;
		for (const Json& j : Runtime::Config().Get("recentProjects").Items())
			if (j.IsString() && fs::Exists(j.AsString()))
				out.push_back(j.AsString());
		return out;
	}
	void Editor::AddRecent(const std::string& path)
	{
		std::vector<std::string> r = Recent();
		r.erase(std::remove(r.begin(), r.end(), path), r.end());
		r.insert(r.begin(), path);
		if (r.size() > 12) r.resize(12);
		Json arr = Json::MakeArray();
		for (const std::string& s : r) arr.Push(Json(s));
		Runtime::Config()["recentProjects"] = arr;
		Runtime::SaveConfig();
	}
	void Editor::RemoveRecent(const std::string& path)
	{
		std::vector<std::string> r = Recent();
		r.erase(std::remove(r.begin(), r.end(), path), r.end());
		Json arr = Json::MakeArray();
		for (const std::string& s : r) arr.Push(Json(s));
		Runtime::Config()["recentProjects"] = arr;
		Runtime::SaveConfig();
	}

	Status Editor::NewProject(const std::string& directory, const std::string& name, const std::string& templ, bool native)
	{
		ProjectCreateOptions o;
		o.directory = directory;
		o.name = name;
		o.templateName = templ;
		o.nativeModule = native;
		o.runtimeRoot = runtimeRoot;
		Project p;
		Status s = Project::Create(o, p);
		if (!s)
			return s;
		return OpenProject(p.FilePath());
	}

	Status Editor::OpenProject(const std::string& path)
	{
		Project p;
		Status s = Project::Load(path, p);
		if (!s)
			return s;
		CloseProject();
		project = std::move(p);
		const std::vector<std::string> problems = project.Validate();
		for (const std::string& pr : problems)
			LUMEN_WARN("project", "%s", pr.c_str());
		Status a = assets.Open(project);
		if (!a)
			LUMEN_WARN("assets", "%s", a.message.c_str());
		packages.Open(project);
		AddRecent(project.FilePath());
		LoadBreakpoints();
		Status sc = OpenScene(project.Settings().startupScene);
		if (!sc)
		{
			scene = SceneDoc();
			scenePath = project.Settings().startupScene;
			Toast("Startup scene could not be loaded: " + sc.message, true);
		}
		const std::string main = project.Settings().mainScript;
		if (fs::Exists(project.Resolve(main)))
			OpenFile(main);
		LUMEN_INFO("editor", "opened project '%s'", project.Settings().name.c_str());
		return Status::Ok();
	}

	void Editor::CloseProject()
	{
		if (play != PlayState::Edit)
			wantStop = true;
		assets.Close();
		docs.clear();
		activeDoc = -1;
		selection.clear();
		primary.clear();
		scene = SceneDoc();
		scenePath.clear();
		undo_.clear();
		redo_.clear();
		sceneDirty = false;
		assetFolder.clear();
		assetSelected.clear();
		project = Project();
	}

	Status Editor::SaveProjectSettings()
	{
		return project.Save();
	}

	// ---- scene ------------------------------------------------------------------------------------------------------

	Status Editor::OpenScene(const std::string& relPath)
	{
		SceneDoc d;
		Status s = SceneDoc::Load(project.Resolve(relPath), d);
		if (!s)
			return s;
		scene = std::move(d);
		scenePath = relPath;
		selection.clear();
		primary.clear();
		undo_.clear();
		redo_.clear();
		baseline_ = scene.ToJson().Dump(0);
		sceneDirty = false;
		++version;
		return Status::Ok();
	}

	Status Editor::NewScene(const std::string& relPath)
	{
		SceneDoc d;
		d.name = fs::Stem(relPath);
		d.Add(MakeCameraEntity("Main Camera", true));
		Status s = d.Save(project.Resolve(relPath));
		if (!s)
			return s;
		assets.Refresh();
		return OpenScene(relPath);
	}

	Status Editor::SaveScene()
	{
		if (scenePath.empty())
			return Status::Fail("no scene");
		Status s = scene.Save(project.Resolve(scenePath));
		if (s)
		{
			sceneDirty = false;
			assets.Refresh();
		}
		return s;
	}

	void Editor::Touch() { touched_ = true; ++version; }

	void Editor::CommitFrame()
	{
		++frame;
		if (!touched_)
			return;
		touched_ = false;
		sceneDirty = true;
		const bool gesture = frame - lastTouchFrame_ <= 2 && lastTouchFrame_ != 0;
		if (!gesture)
		{
			undo_.push_back(baseline_);
			if (undo_.size() > 200)
				undo_.erase(undo_.begin());
			redo_.clear();
		}
		baseline_ = scene.ToJson().Dump(0);
		lastTouchFrame_ = frame;
	}

	void Editor::SelectionAfterRestore()
	{
		std::set<std::string> keep;
		for (const std::string& id : selection)
			if (scene.Find(id))
				keep.insert(id);
		selection = keep;
		if (!scene.Find(primary))
			primary = selection.empty() ? std::string() : *selection.begin();
	}

	void Editor::Undo()
	{
		if (undo_.empty())
			return;
		redo_.push_back(baseline_);
		const std::string snap = undo_.back();
		undo_.pop_back();
		Json j;
		SceneDoc d;
		if (Json::Parse(snap, j) && SceneDoc::FromJson(j, d))
		{
			scene = std::move(d);
			baseline_ = snap;
			sceneDirty = true;
			++version;
			SelectionAfterRestore();
		}
	}

	void Editor::Redo()
	{
		if (redo_.empty())
			return;
		undo_.push_back(baseline_);
		const std::string snap = redo_.back();
		redo_.pop_back();
		Json j;
		SceneDoc d;
		if (Json::Parse(snap, j) && SceneDoc::FromJson(j, d))
		{
			scene = std::move(d);
			baseline_ = snap;
			sceneDirty = true;
			++version;
			SelectionAfterRestore();
		}
	}

	void Editor::Select(const std::string& id, bool additive)
	{
		if (!additive)
			selection.clear();
		if (id.empty())
		{
			primary.clear();
			return;
		}
		if (additive && selection.count(id))
		{
			selection.erase(id);
			primary = selection.empty() ? std::string() : *selection.begin();
			return;
		}
		selection.insert(id);
		primary = id;
	}

	void Editor::DeleteSelection()
	{
		if (selection.empty())
			return;
		for (const std::string& id : std::vector<std::string>(selection.begin(), selection.end()))
			scene.Remove(id);
		selection.clear();
		primary.clear();
		Touch();
	}

	void Editor::DuplicateSelection()
	{
		std::set<std::string> created;
		for (const std::string& id : std::vector<std::string>(selection.begin(), selection.end()))
		{
			const std::string n = scene.Duplicate(id);
			if (!n.empty())
				created.insert(n);
		}
		if (!created.empty())
		{
			selection = created;
			primary = *created.begin();
			Touch();
		}
	}

	namespace
	{
		// Only the top most selected entities are copied; their subtrees come along.
		std::vector<std::string> SelectionRoots(const SceneDoc& scene, const std::set<std::string>& sel)
		{
			std::vector<std::string> roots;
			for (const SceneEntity& e : scene.entities)
			{
				if (!sel.count(e.id)) continue;
				bool covered = false;
				for (const std::string& o : sel)
					if (o != e.id && scene.IsDescendant(e.id, o)) { covered = true; break; }
				if (!covered) roots.push_back(e.id);
			}
			return roots;
		}
	}

	void Editor::CopySelection()
	{
		const std::vector<std::string> roots = SelectionRoots(scene, selection);
		if (roots.empty())
			return;
		Json out;
		if (roots.size() == 1)
			out = scene.ExportSubtree(roots[0]);
		else
		{
			out = Json::MakeObject();
			out["format"] = "lumen-prefab-set";
			Json items = Json::MakeArray();
			for (const std::string& r : roots) items.Push(scene.ExportSubtree(r));
			out["items"] = std::move(items);
		}
		ImGui::SetClipboardText(out.Dump(0).c_str());
		Toast(std::to_string(roots.size()) + (roots.size() == 1 ? " object copied" : " objects copied"));
	}

	void Editor::CutSelection()
	{
		CopySelection();
		DeleteSelection();
	}

	bool Editor::CanPaste() const
	{
		const char* t = ImGui::GetClipboardText();
		return t && strstr(t, "lumen-prefab") != nullptr;
	}

	void Editor::Paste(const std::string& parent)
	{
		const char* t = ImGui::GetClipboardText();
		if (!t || !strstr(t, "lumen-prefab"))
			return;
		Json j;
		if (!Json::Parse(t, j))
			return;
		std::set<std::string> created;
		auto one = [&](const Json& prefab) {
			const std::string id = scene.ImportSubtree(prefab, parent);
			if (!id.empty()) created.insert(id);
		};
		if (SceneDoc::IsPrefabJson(j)) one(j);
		else if (j.GetString("format") == "lumen-prefab-set")
			for (const Json& item : j.Get("items").Items()) one(item);
		if (created.empty())
			return;
		selection = created;
		primary = *created.begin();
		Touch();
	}

	void Editor::SelectAll()
	{
		selection.clear();
		for (const SceneEntity& e : scene.entities) selection.insert(e.id);
		if (primary.empty() && !scene.entities.empty()) primary = scene.entities.front().id;
	}

	Status Editor::SaveAsPrefab(const std::string& id, std::string* outPath)
	{
		const SceneEntity* e = scene.Find(id);
		if (!e)
			return Status::Fail("no such entity");
		std::string base = fs::SanitizeFileName(e->name.empty() ? "Prefab" : e->name);
		std::string rel = "prefabs/" + base + ".prefab";
		for (int i = 2; fs::Exists(project.Resolve(rel)); ++i)
			rel = "prefabs/" + base + " " + std::to_string(i) + ".prefab";
		Json prefab = scene.ExportSubtree(id);
		// The prefab root is stored at the origin, detached from its parent.
		for (Json& je : prefab["entities"].ItemsMutable())
			if (je.GetString("id") == id)
			{
				je.Erase("parent");
				if (Json* tr = je.FindMutable("transform")) tr->Erase("position");
			}
		fs::CreateDirs(project.Resolve("prefabs"));
		if (!fs::WriteText(project.Resolve(rel), prefab.Dump(1)))
			return Status::Fail("cannot write " + rel);
		assets.Refresh();
		if (outPath) *outPath = rel;
		return Status::Ok();
	}

	std::string Editor::InstantiatePrefab(const std::string& relPath, const std::string& parent, const Float3* position)
	{
		std::string text;
		Json j;
		if (!fs::ReadText(project.Resolve(relPath), text) || !Json::Parse(text, j) || !SceneDoc::IsPrefabJson(j))
		{
			Toast("Not a valid prefab: " + relPath, true);
			return {};
		}
		const std::string id = scene.ImportSubtree(j, parent);
		if (id.empty())
			return {};
		if (position)
			if (SceneEntity* e = scene.Find(id))
				e->transform.position = *position;
		Select(id);
		Touch();
		return id;
	}

	std::string Editor::CreateEntity(SceneEntity e, bool selectIt)
	{
		if (!primary.empty() && selection.size() == 1 && e.parent.empty() && false)
			e.parent = primary;
		const std::string id = scene.Add(std::move(e));
		if (selectIt)
			Select(id);
		Touch();
		return id;
	}

	// ---- code files -------------------------------------------------------------------------------------------------

	CodeDoc* Editor::OpenFile(const std::string& rel, int line)
	{
		for (size_t i = 0; i < docs.size(); ++i)
			if (docs[i].path == rel)
			{
				activeDoc = (int)i;
				docs[i].goToLine = line;
				return &docs[i];
			}
		CodeDoc d;
		d.path = rel;
		if (!fs::ReadText(project.Resolve(rel), d.text))
		{
			Toast("Cannot read " + rel, true);
			return nullptr;
		}
		d.saved = d.text;
		d.diskMtime = fs::LastWriteTimeMs(project.Resolve(rel));
		d.lang = LangForPath(rel);
		d.lua = d.lang == CodeLang::Lua;
		d.goToLine = line;
		docs.push_back(std::move(d));
		activeDoc = (int)docs.size() - 1;
		return &docs.back();
	}

	void Editor::CloseFile(size_t i)
	{
		if (i >= docs.size())
			return;
		lsp.Close(AbsPath(docs[i].path));
		docs.erase(docs.begin() + i);
		if (activeDoc >= (int)docs.size())
			activeDoc = (int)docs.size() - 1;
	}

	Status Editor::SaveFile(CodeDoc& d)
	{
		if (!fs::WriteAtomic(project.Resolve(d.path), d.text.data(), d.text.size()))
			return Status::Fail("cannot write " + d.path);
		d.saved = d.text;
		d.diskMtime = fs::LastWriteTimeMs(project.Resolve(d.path));
		return Status::Ok();
	}

	void Editor::SaveAllFiles()
	{
		for (CodeDoc& d : docs)
			if (d.dirty())
				SaveFile(d);
	}

	void Editor::CheckExternalChanges()
	{
		for (CodeDoc& d : docs)
		{
			const uint64_t mt = fs::LastWriteTimeMs(project.Resolve(d.path));
			if (mt == d.diskMtime || mt == 0)
				continue;
			d.diskMtime = mt;
			std::string text;
			if (!fs::ReadText(project.Resolve(d.path), text) || text == d.text)
				continue;
			if (!d.dirty()) // untouched in the editor: follow the disk
			{
				d.text = d.saved = text;
				d.diagsStale = true;
			}
			else
				Toast(d.path + " changed on disk; your unsaved edits are kept", true);
		}
	}

	// ---- jobs / toast -------------------------------------------------------------------------------------------------

	Job* Editor::StartJob(const std::string& title, std::function<Status(Job&)> fn)
	{
		auto job = std::make_unique<Job>();
		job->title = title;
		Job* raw = job.get();
		raw->worker = std::thread([raw, fn]() {
			raw->result = fn(*raw);
			raw->progress = 1.0f;
			raw->done = true;
		});
		jobs.push_back(std::move(job));
		return raw;
	}

	void Editor::PollJobs()
	{
		if (hasPendingToolchain.exchange(false))
		{
			std::lock_guard<std::mutex> l(toolchainMutex);
			toolchain = pendingToolchain;
			toolchainBusy = false;
			const Status save = PersistToolchain();
			if (!save)
				LUMEN_WARN("toolchain", "%s", save.message.c_str());
			LUMEN_INFO("toolchain", "%s", toolchain.description.c_str());
		}
		for (auto it = jobs.begin(); it != jobs.end();)
		{
			if ((*it)->done)
			{
				Toast((*it)->title + (((*it)->result.ok) ? " finished" : ": " + (*it)->result.message), !(*it)->result.ok);
				if ((*it)->worker.joinable())
					(*it)->worker.join();
				it = jobs.erase(it);
			}
			else
				++it;
		}
		toasts.erase(std::remove_if(toasts.begin(), toasts.end(), [&](const ToastMsg& t) { return t.until < now; }), toasts.end());
	}

	void Editor::Toast(const std::string& text, bool error)
	{
		toasts.push_back({text, error, now + (error ? 7.0 : 3.5)});
		if (error)
			LUMEN_ERROR("editor", "%s", text.c_str());
	}
}
