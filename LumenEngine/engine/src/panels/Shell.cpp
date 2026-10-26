#include "Panels.h"

#include "lumen/core/Fs.h"
#include "lumen/core/Strings.h"

#include <algorithm>

namespace lumen
{
	bool IconButton(const char* icon, const char* tooltip, bool active, ImVec2 size)
	{
		if (active)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ui::col::Accent());
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.08f, 0.08f, 0.09f, 1));
		}
		const bool r = ImGui::Button(icon, size);
		if (active)
			ImGui::PopStyleColor(2);
		if (tooltip && ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", tooltip);
		return r;
	}

	void SectionLabel(const char* text)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ui::col::Muted());
		ImGui::Separator();
		ImGui::TextUnformatted(text);
		ImGui::PopStyleColor();
	}

	bool Vec3Control(const char* label, Float3& v, float speed, float resetTo, const char* fmt)
	{
		bool changed = false;
		ImGui::PushID(label);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(label);
		ImGui::SameLine(92);
		const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3.0f;
		static const ImVec4 axis[3] = {ImVec4(0.85f, 0.35f, 0.33f, 1), ImVec4(0.45f, 0.75f, 0.40f, 1), ImVec4(0.38f, 0.58f, 0.88f, 1)};
		float* f[3] = {&v.x, &v.y, &v.z};
		const char* names[3] = {"X", "Y", "Z"};
		for (int i = 0; i < 3; ++i)
		{
			ImGui::PushID(i);
			ImGui::SetNextItemWidth(w);
			ImVec2 p = ImGui::GetCursorScreenPos();
			changed |= ImGui::DragFloat("##v", f[i], speed, 0, 0, fmt);
			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) { *f[i] = resetTo; changed = true; }
			ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, p.y + 3), ImVec2(p.x + 2, p.y + ImGui::GetFrameHeight() - 3), ImGui::ColorConvertFloat4ToU32(axis[i]));
			(void)names;
			ImGui::PopID();
			if (i < 2) ImGui::SameLine();
		}
		ImGui::PopID();
		return changed;
	}

	bool ColorEdit3(const char* label, Float3& c)
	{
		float v[3] = {c.x, c.y, c.z};
		const bool ch = ImGui::ColorEdit3(label, v, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
		if (ch) c = {v[0], v[1], v[2]};
		return ch;
	}

	std::string FormatBytes(uint64_t b)
	{
		const char* u[] = {"B", "KB", "MB", "GB"};
		double v = (double)b;
		int i = 0;
		while (v >= 1024 && i < 3) { v /= 1024; ++i; }
		char buf[32];
		snprintf(buf, sizeof(buf), i == 0 ? "%.0f %s" : "%.1f %s", v, u[i]);
		return buf;
	}

	const char* IconForAsset(AssetType t)
	{
		switch (t)
		{
		case AssetType::Folder: return ICON_MD_FOLDER;
		case AssetType::Scene: return ICON_MD_LANDSCAPE;
		case AssetType::Script: return ICON_MD_CODE;
		case AssetType::Native: return ICON_MD_TERMINAL;
		case AssetType::Shader: return ICON_MD_PALETTE;
		case AssetType::Material: return ICON_MD_LAYERS;
		case AssetType::Texture: return ICON_MD_IMAGE;
		case AssetType::Model: return ICON_MD_VIEW_IN_AR;
		case AssetType::Audio: return ICON_MD_AUDIOTRACK;
		case AssetType::Video: return ICON_MD_MOVIE;
		case AssetType::Font: return ICON_MD_FONT_DOWNLOAD;
		case AssetType::Animation: return ICON_MD_ANIMATION;
		case AssetType::Package: return ICON_MD_INVENTORY;
		case AssetType::Prefab: return ICON_MD_EXTENSION;
		default: return ICON_MD_INSERT_DRIVE_FILE;
		}
	}

	bool PathField(const char* label, std::string& path, const char* filter, Ui& ui)
	{
		bool changed = false;
		ImGui::PushID(label);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(label);
		ImGui::SameLine(92);
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 28);
		char buf[512];
		snprintf(buf, sizeof(buf), "%s", path.c_str());
		if (ImGui::InputText("##p", buf, sizeof(buf)))
		{
			path = buf;
			changed = true;
		}
		// drop target: assets dragged from the asset browser
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("LUMEN_ASSET"))
			{
				path = std::string((const char*)pl->Data, pl->DataSize - 1);
				changed = true;
			}
			ImGui::EndDragDropTarget();
		}
		ImGui::SameLine();
		if (ImGui::Button(ICON_MD_SEARCH "##pick"))
			ImGui::OpenPopup("pick");
		if (ImGui::BeginPopup("pick"))
		{
			static char q[64] = "";
			ImGui::SetNextItemWidth(260);
			ImGui::InputTextWithHint("##q", "search", q, sizeof(q));
			ImGui::BeginChild("list", ImVec2(320, 220));
			for (const AssetEntry& e : ui.ed->assets.Entries())
			{
				if (e.isDirectory) continue;
				if (filter && *filter)
				{
					bool ok = false;
					for (const std::string& ext : str::Split(filter, ';'))
						if (e.extension == ext) ok = true;
					if (!ok) continue;
				}
				if (q[0] && str::FuzzyScore(q, e.path) == 0) continue;
				if (ImGui::Selectable((std::string(IconForAsset(e.type)) + "  " + e.path).c_str()))
				{
					path = e.path;
					changed = true;
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::EndChild();
			ImGui::EndPopup();
		}
		ImGui::PopID();
		return changed;
	}

	void DrawMainMenu(Ui& ui)
	{
		Editor& ed = *ui.ed;
		if (!ImGui::BeginMainMenuBar())
			return;
		ImGui::PushStyleColor(ImGuiCol_Text, ui::col::Accent());
		ImGui::TextUnformatted("LUMEN");
		ImGui::PopStyleColor();
		ImGui::Dummy(ImVec2(6, 0));
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("New project...")) ui.showNewProject = true;
			if (ImGui::MenuItem("Save scene", "Ctrl+S", false, ed.HasProject())) { Status s = ed.SaveScene(); ed.Toast(s ? "Scene saved" : s.message, !s); }
			if (ImGui::MenuItem("Save all files", nullptr, false, ed.HasProject())) ed.SaveAllFiles();
			if (ImGui::MenuItem("New scene...", nullptr, false, ed.HasProject()))
			{
				Status s = ed.NewScene("scenes/scene_" + std::to_string(ed.assets.OfType(AssetType::Scene).size() + 1) + ".scene");
				if (!s) ed.Toast(s.message, true);
			}
			ImGui::Separator();
			if (ImGui::BeginMenu("Recent projects"))
			{
				for (const std::string& r : ed.Recent())
					if (ImGui::MenuItem(r.c_str()))
					{
						Status s = ed.OpenProject(r);
						if (!s) ed.Toast(s.message, true);
					}
				ImGui::EndMenu();
			}
			if (ImGui::MenuItem("Close project", nullptr, false, ed.HasProject())) ed.CloseProject();
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Edit"))
		{
			if (ImGui::MenuItem("Undo", "Ctrl+Z", false, ed.CanUndo())) ed.Undo();
			if (ImGui::MenuItem("Redo", "Ctrl+Y", false, ed.CanRedo())) ed.Redo();
			ImGui::Separator();
			if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, !ed.selection.empty())) ed.DuplicateSelection();
			if (ImGui::MenuItem("Delete", "Del", false, !ed.selection.empty())) ed.DeleteSelection();
			if (ImGui::MenuItem("Focus selection", "F", false, !ed.primary.empty())) ed.FocusSelection();
			ImGui::Separator();
			if (ImGui::MenuItem("Copy", "Ctrl+C", false, !ed.selection.empty())) ed.CopySelection();
			if (ImGui::MenuItem("Cut", "Ctrl+X", false, !ed.selection.empty())) ed.CutSelection();
			if (ImGui::MenuItem("Paste", "Ctrl+V", false, ed.CanPaste())) ed.Paste();
			if (ImGui::MenuItem("Select all", "Ctrl+A")) ed.SelectAll();
			ImGui::Separator();
			if (ImGui::MenuItem("Command palette", "Ctrl+P")) ui.showCommandPalette = true;
			if (ImGui::MenuItem("Preferences...")) ed.panel.prefs = true;
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Create"))
		{
			auto add = [&](const char* label, SceneEntity e) { if (ImGui::MenuItem(label)) ed.CreateEntity(std::move(e)); };
			add("Empty", MakeEmptyEntity("Empty"));
			ImGui::Separator();
			for (const char* p : {"cube", "sphere", "plane", "cylinder", "capsule", "cone"})
			{
				std::string label = p;
				label[0] = (char)toupper(label[0]);
				add(label.c_str(), MakePrimitiveEntity(p, label));
			}
			ImGui::Separator();
			add("Point light", MakeLightEntity("point", "Point Light"));
			add("Spot light", MakeLightEntity("spot", "Spot Light"));
			add("Directional light", MakeLightEntity("directional", "Directional Light"));
			add("Camera", MakeCameraEntity("Camera", false));
			ImGui::Separator();
			if (ImGui::MenuItem("Water surface"))
			{
				SceneEntity e = MakePrimitiveEntity("plane", "Water");
				e.mesh->size = {40, 1, 40};
				e.material->shader = "water";
				e.material->alphaBlend = true;
				e.material->color = {0.05f, 0.28f, 0.38f, 0.85f};
				e.material->roughness = 0.04f;
				e.material->normalMap = "textures/water_normal.png";
				ed.CreateEntity(std::move(e));
			}
			if (ImGui::MenuItem("Physics crate"))
			{
				SceneEntity e = MakePrimitiveEntity("cube", "Crate");
				e.transform.position = {0, 2, 0};
				e.rigidbody = SceneRigidBody{};
				ed.CreateEntity(std::move(e));
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("View"))
		{
			auto item = [&](const char* label, bool& v) { ImGui::MenuItem(label, nullptr, &v); };
			item("Viewport", ed.panel.viewport); item("Hierarchy", ed.panel.hierarchy); item("Inspector", ed.panel.inspector);
			item("Environment", ed.panel.environment); item("Assets", ed.panel.assets); item("Console", ed.panel.console);
			item("Code", ed.panel.code); item("Debugger", ed.panel.debug); item("Timeline", ed.panel.timeline); item("Build", ed.panel.build); item("Packages", ed.panel.packages);
			item("Runtime libraries", ed.panel.libraries); item("Shaders", ed.panel.shaders); item("Project settings", ed.panel.settings);
			ImGui::Separator();
			ImGui::MenuItem("Grid", nullptr, &ed.showGrid);
			ImGui::MenuItem("Selection bounds", nullptr, &ed.showBounds);
			ImGui::MenuItem("Statistics", nullptr, &ed.showStats);
			if (ImGui::MenuItem("Reset layout"))
			{
				ImGui::LoadIniSettingsFromMemory("");
				ImGui::DockBuilderRemoveNode(ImGui::GetID("LumenDock"));
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Help"))
		{
			if (ImGui::MenuItem("About Lumen")) ui.showAbout = true;
			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
		if (ui.showAbout)
		{
			ImGui::OpenPopup("About Lumen");
			ui.showAbout = false;
		}
		if (ImGui::BeginPopupModal("About Lumen", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::PushFont(ui::BoldFont());
			ImGui::TextUnformatted("Lumen Engine " LUMEN_VERSION_STRING);
			ImGui::PopFont();
			ImGui::TextDisabled("Lua + C++ game engine on top of Wicked Engine");
			ImGui::Spacing();
			ImGui::TextUnformatted("Rendering, audio and physics: Wicked Engine (MIT)\nInterface: Dear ImGui, ImGuizmo (MIT)");
			ImGui::Spacing();
			if (ImGui::Button("Close", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
	}

	void DrawToolbar(Ui& ui)
	{
		Editor& ed = *ui.ed;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 6));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyle().Colors[ImGuiCol_MenuBarBg]);
		ImGui::Begin("##toolbar", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
			ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
		const bool editing = ed.play == PlayState::Edit;
		if (IconButton(ICON_MD_OPEN_WITH, "Move (W)", ed.gizmo == GizmoMode::Translate)) ed.gizmo = GizmoMode::Translate;
		ImGui::SameLine();
		if (IconButton(ICON_MD_3D_ROTATION, "Rotate (E)", ed.gizmo == GizmoMode::Rotate)) ed.gizmo = GizmoMode::Rotate;
		ImGui::SameLine();
		if (IconButton(ICON_MD_ZOOM_OUT_MAP, "Scale (R)", ed.gizmo == GizmoMode::Scale)) ed.gizmo = GizmoMode::Scale;
		ImGui::SameLine();
		if (IconButton(ed.gizmoWorld ? "World" : "Local", "Gizmo space")) ed.gizmoWorld = !ed.gizmoWorld;
		ImGui::SameLine();
		if (IconButton(ICON_MD_CROP_FREE, "Snap", ed.snap)) ed.snap = !ed.snap;
		ImGui::SameLine();
		ImGui::TextDisabled("|");
		ImGui::SameLine();
		if (ImGui::Button(ICON_MD_UNDO)) ed.Undo();
		ImGui::SameLine();
		if (ImGui::Button(ICON_MD_REDO)) ed.Redo();
		ImGui::SameLine();
		if (ImGui::Button(ICON_MD_SAVE)) { Status s = ed.SaveScene(); ed.SaveAllFiles(); ed.Toast(s ? "Saved" : s.message, !s); }

		// play controls centred
		const float cx = ImGui::GetWindowWidth() * 0.5f - 62;
		ImGui::SameLine(cx);
		if (editing)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.55f, 0.34f, 1));
			if (ImGui::Button(ICON_MD_PLAY_ARROW "  Play", ImVec2(96, 0))) ed.wantPlay = true;
			ImGui::PopStyleColor();
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("F5");
		}
		else
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.62f, 0.28f, 0.26f, 1));
			if (ImGui::Button(ICON_MD_STOP "  Stop", ImVec2(96, 0))) ed.wantStop = true;
			ImGui::PopStyleColor();
			ImGui::SameLine();
			if (ImGui::Button(ed.play == PlayState::Paused ? ICON_MD_PLAY_ARROW : ICON_MD_PAUSE))
			{
				ed.play = ed.play == PlayState::Paused ? PlayState::Play : PlayState::Paused;
				wi::physics::SetSimulationEnabled(ed.play == PlayState::Play);
			}
		}
		// right side: scene name
		const std::string title = ed.project.Settings().name + "  /  " + fs::FileName(ed.scenePath) + (ed.sceneDirty ? " *" : "");
		const float tw = ImGui::CalcTextSize(title.c_str()).x;
		ImGui::SameLine(ImGui::GetWindowWidth() - tw - 16);
		ImGui::TextColored(ui::col::Muted(), "%s", title.c_str());
		ImGui::End();
		ImGui::PopStyleColor();
		ImGui::PopStyleVar(2);
	}

	void DrawStatusBar(Ui& ui)
	{
		Editor& ed = *ui.ed;
		const ImGuiViewport* vp = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - 26));
		ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, 26));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 4));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyle().Colors[ImGuiCol_MenuBarBg]);
		ImGui::Begin("##status", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
			ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
		const ImVec4 stateCol = ed.play == PlayState::Edit ? ui::col::Muted() : ui::col::Good();
		ImGui::TextColored(stateCol, "%s", ed.play == PlayState::Edit ? "EDIT" : ed.play == PlayState::Play ? "PLAYING" : "PAUSED");
		ImGui::SameLine();
		ImGui::TextDisabled("|  %zu entities", ed.scene.entities.size());
		ImGui::SameLine();
		const auto& hostErr = ui.host->Errors();
		if (!hostErr.empty())
		{
			ImGui::TextColored(ui::col::Bad(), "|  %zu script error(s)", hostErr.size());
			ImGui::SameLine();
		}
		for (auto& j : ed.jobs)
		{
			ImGui::TextDisabled("|");
			ImGui::SameLine();
			ImGui::TextUnformatted(j->title.c_str());
			ImGui::SameLine();
			ImGui::ProgressBar(j->progress, ImVec2(110, 12), "");
			ImGui::SameLine();
			ImGui::TextDisabled("%s", j->Status_().c_str());
			ImGui::SameLine();
		}
		char fps[64];
		snprintf(fps, sizeof(fps), "%.0f fps", ImGui::GetIO().Framerate);
		ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(fps).x - 12);
		ImGui::TextDisabled("%s", fps);
		ImGui::End();
		ImGui::PopStyleColor();
		ImGui::PopStyleVar(2);
	}

	void DrawToasts(Ui& ui)
	{
		Editor& ed = *ui.ed;
		const ImGuiViewport* vp = ImGui::GetMainViewport();
		float y = vp->WorkPos.y + vp->WorkSize.y - 44;
		for (auto it = ed.toasts.rbegin(); it != ed.toasts.rend(); ++it)
		{
			const ImVec2 sz = ImGui::CalcTextSize(it->text.c_str());
			ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - sz.x - 40, y - sz.y - 14));
			ImGui::SetNextWindowBgAlpha(0.96f);
			ImGui::PushStyleColor(ImGuiCol_Border, it->error ? ui::col::Bad() : ui::col::Accent());
			ImGui::Begin(("##toast" + std::to_string((uintptr_t)&*it)).c_str(), nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
				ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoDocking);
			ImGui::TextUnformatted(it->text.c_str());
			ImGui::End();
			ImGui::PopStyleColor();
			y -= sz.y + 22;
		}
	}

	void DrawCommandPalette(Ui& ui)
	{
		Editor& ed = *ui.ed;
		struct Cmd { std::string label; std::function<void()> run; };
		std::vector<Cmd> cmds = {
			{"Play / stop", [&] { (ed.play == PlayState::Edit ? ed.wantPlay : ed.wantStop) = true; }},
			{"Save scene", [&] { ed.SaveScene(); ed.SaveAllFiles(); }},
			{"Undo", [&] { ed.Undo(); }}, {"Redo", [&] { ed.Redo(); }},
			{"Duplicate selection", [&] { ed.DuplicateSelection(); }}, {"Delete selection", [&] { ed.DeleteSelection(); }},
			{"Focus selection", [&] { ed.FocusSelection(); }},
			{"Create: cube", [&] { ed.CreateEntity(MakePrimitiveEntity("cube", "Cube")); }},
			{"Create: sphere", [&] { ed.CreateEntity(MakePrimitiveEntity("sphere", "Sphere")); }},
			{"Create: point light", [&] { ed.CreateEntity(MakeLightEntity("point", "Point Light")); }},
			{"Toggle statistics", [&] { ed.showStats = !ed.showStats; }},
			{"Open runtime libraries", [&] { ed.panel.libraries = true; }},
			{"Open shader library", [&] { ed.panel.shaders = true; }},
			{"Open project settings", [&] { ed.panel.settings = true; }},
		};
		for (const AssetEntry& a : ed.assets.Entries())
			if (!a.isDirectory && (a.type == AssetType::Script || a.type == AssetType::Shader || a.type == AssetType::Native || a.type == AssetType::Text || a.type == AssetType::Data))
			{
				const std::string p = a.path;
				cmds.push_back({"Open file: " + p, [&ed, p] { ed.panel.code = true; ed.OpenFile(p); }});
			}
		for (const AssetEntry* a : ed.assets.OfType(AssetType::Scene))
		{
			const std::string p = a->path;
			cmds.push_back({"Open scene: " + p, [&ed, p] { ed.OpenScene(p); }});
		}
		for (const SceneEntity& e : ed.scene.entities)
		{
			const std::string id = e.id;
			cmds.push_back({"Select: " + e.name, [&ed, id] { ed.Select(id); ed.FocusSelection(); }});
		}
		const ImGuiViewport* vp = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + 90), ImGuiCond_Always, ImVec2(0.5f, 0));
		ImGui::SetNextWindowSize(ImVec2(560, 0));
		static char q[128];
		static int sel = 0;
		static bool first = true;
		bool open = true;
		ImGui::Begin("##palette", &open, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking);
		if (first) { ImGui::SetKeyboardFocusHere(); q[0] = 0; sel = 0; first = false; }
		ImGui::SetNextItemWidth(-1);
		const bool changed = ImGui::InputTextWithHint("##q", ICON_MD_SEARCH "  type a command, file or entity", q, sizeof(q));
		if (changed) sel = 0;
		std::vector<std::pair<int, size_t>> scored;
		for (size_t i = 0; i < cmds.size(); ++i)
		{
			const int s = q[0] ? str::FuzzyScore(q, cmds[i].label) : 1;
			if (s > 0) scored.push_back({s, i});
		}
		std::stable_sort(scored.begin(), scored.end(), [](auto& a, auto& b) { return a.first > b.first; });
		if (scored.size() > 12) scored.resize(12);
		if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) sel = std::min<int>(sel + 1, (int)scored.size() - 1);
		if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) sel = std::max(0, sel - 1);
		bool run = ImGui::IsKeyPressed(ImGuiKey_Enter);
		for (int i = 0; i < (int)scored.size(); ++i)
		{
			if (ImGui::Selectable(cmds[scored[i].second].label.c_str(), i == sel)) { sel = i; run = true; }
		}
		if (run && sel >= 0 && sel < (int)scored.size())
		{
			cmds[scored[sel].second].run();
			open = false;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_Escape)) open = false;
		if (!open) { ui.showCommandPalette = false; first = true; }
		ImGui::End();
	}
}
