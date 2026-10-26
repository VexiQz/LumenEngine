// Timeline: keyframe editor for `.lanim` clips. Preview poses the scene document (and so the Wicked scene through the
// bridge) without touching the undo history; "Reset pose" restores the transforms captured before the first preview.
#include "Panels.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace lumen
{
	namespace
	{
		const char* kProps[] = {"position", "rotation", "scale"};

		SceneEntity* ChildByName(Editor& ed, const SceneEntity* parent, const std::string& name)
		{
			for (const SceneEntity* c : ed.scene.Children(parent->id))
				if (c->name == name) return ed.scene.Find(c->id);
			return nullptr;
		}

		SceneEntity* ResolveTarget(Editor& ed, const std::string& rootId, const std::string& path)
		{
			SceneEntity* cur = ed.scene.Find(rootId);
			size_t pos = 0;
			while (cur && pos < path.size())
			{
				size_t e = path.find('/', pos);
				if (e == std::string::npos) e = path.size();
				const std::string seg = path.substr(pos, e - pos);
				pos = e + 1;
				if (!seg.empty()) cur = ChildByName(ed, cur, seg);
			}
			return cur;
		}

		void CollectPaths(Editor& ed, const SceneEntity& e, const std::string& prefix, std::vector<std::string>& out, int depth)
		{
			if (depth > 8) return;
			for (const SceneEntity* c : ed.scene.Children(e.id))
			{
				const std::string p = prefix.empty() ? c->name : prefix + "/" + c->name;
				out.push_back(p);
				CollectPaths(ed, *c, p, out, depth + 1);
			}
		}

		Float3* Field(SceneTransform& t, const std::string& prop)
		{
			if (prop == "position") return &t.position;
			if (prop == "rotation") return &t.rotation;
			if (prop == "scale") return &t.scale;
			return nullptr;
		}

		void ResetPose(Editor& ed)
		{
			auto& tl = ed.timeline;
			if (!tl.posed) return;
			for (auto& [id, t] : tl.saved)
				if (SceneEntity* e = ed.scene.Find(id)) e->transform = t;
			tl.saved.clear();
			tl.posed = false;
			++ed.version;
		}

		void ApplyPose(Editor& ed, float time)
		{
			auto& tl = ed.timeline;
			if (!tl.loaded || tl.rootId.empty()) return;
			for (const anim::Track& tr : tl.clip.tracks)
			{
				if (tr.keys.empty()) continue;
				SceneEntity* e = tr.target.empty() ? ed.scene.Find(tl.rootId) : ResolveTarget(ed, tl.rootId, tr.target);
				if (!e) continue;
				if (!tl.saved.count(e->id)) tl.saved[e->id] = e->transform;
				if (Float3* f = Field(e->transform, tr.path)) *f = anim::Sample(tr, time);
				tl.posed = true;
			}
			++ed.version;
		}

		void LoadClip(Editor& ed, const std::string& rel)
		{
			auto& tl = ed.timeline;
			ResetPose(ed);
			anim::Clip c;
			std::string err;
			if (!anim::Load(ed.AbsPath(rel), c, &err)) { ed.Toast("Cannot open clip: " + err, true); return; }
			tl.clip = c;
			tl.path = rel;
			tl.loaded = true;
			tl.dirty = false;
			tl.time = 0;
			tl.playing = false;
			tl.loop = c.loop;
			tl.track = tl.key = tl.event = -1;
			tl.rootId = ed.primary;
		}

		bool SaveClip(Editor& ed)
		{
			auto& tl = ed.timeline;
			tl.clip.loop = tl.loop;
			tl.clip.duration = std::max(tl.clip.duration, anim::ComputeDuration(tl.clip));
			anim::SortEvents(tl.clip);
			if (!anim::Save(ed.AbsPath(tl.path), tl.clip)) { ed.Toast("Cannot save " + tl.path, true); return false; }
			tl.dirty = false;
			ed.assets.Refresh();
			return true;
		}

		float Snap(const anim::Clip& c, float t)
		{
			const float step = 1.0f / std::max(1.0f, c.fps);
			return std::max(0.0f, std::round(t / step) * step);
		}

		void NewClip(Editor& ed)
		{
			std::string folder = ed.assetFolder;
			std::string rel;
			for (int i = 0; i < 1000; ++i)
			{
				rel = (folder.empty() ? "" : folder + "/") + "clip" + (i ? std::to_string(i) : "") + ".lanim";
				if (!ed.assets.Find(rel)) break;
			}
			anim::Clip c;
			c.name = fs::Stem(rel);
			Status s = ed.assets.CreateAssetFile(rel, anim::ToJson(c).Dump(2));
			if (!s) { ed.Toast(s.message, true); return; }
			ed.assets.Refresh();
			LoadClip(ed, rel);
		}
	}

	void DrawTimeline(Ui& ui)
	{
		Editor& ed = *ui.ed;
		auto& tl = ed.timeline;
		if (!ImGui::Begin(ICON_MD_TIMELINE "  Timeline", &ed.panel.timeline))
		{
			ImGui::End();
			return;
		}
		const bool editing = ed.play == PlayState::Edit;
		if (!ed.timelineOpen.empty()) { LoadClip(ed, ed.timelineOpen); ed.timelineOpen.clear(); }

		// ---- clip selector ------------------------------------------------------------------------------
		ImGui::SetNextItemWidth(220);
		if (ImGui::BeginCombo("##clip", tl.loaded ? tl.path.c_str() : "Select clip..."))
		{
			for (const AssetEntry* a : ed.assets.OfType(AssetType::Animation))
				if (a->extension == ".lanim" && ImGui::Selectable(a->path.c_str(), a->path == tl.path)) LoadClip(ed, a->path);
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		if (ImGui::Button(ICON_MD_ADD "  New")) NewClip(ed);
		if (!tl.loaded)
		{
			ImGui::TextDisabled("Create or pick a .lanim clip, select the entity it plays on in the Hierarchy and add tracks.");
			ImGui::End();
			return;
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(!tl.dirty);
		if (ImGui::Button(ICON_MD_SAVE "  Save")) SaveClip(ed);
		ImGui::EndDisabled();
		ImGui::SameLine();
		const SceneEntity* root = ed.scene.Find(tl.rootId);
		if (ImGui::Button(ICON_MD_MY_LOCATION)) { tl.rootId = ed.primary; ResetPose(ed); }
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play on the selected entity");
		ImGui::SameLine();
		ImGui::TextDisabled("Root: %s", root ? root->name.c_str() : "(none, select an entity)");

		// ---- transport -----------------------------------------------------------------------------------
		ImGui::BeginDisabled(!editing || !root);
		if (ImGui::Button(tl.playing ? ICON_MD_PAUSE : ICON_MD_PLAY_ARROW)) tl.playing = !tl.playing;
		ImGui::SameLine();
		if (ImGui::Button(ICON_MD_STOP)) { tl.playing = false; tl.time = 0; ApplyPose(ed, 0); }
		ImGui::SameLine();
		if (ImGui::Button(ICON_MD_RESTORE "  Reset pose")) { tl.playing = false; ResetPose(ed); }
		ImGui::EndDisabled();
		ImGui::SameLine(); ImGui::SetNextItemWidth(80);
		if (ImGui::DragFloat("##t", &tl.time, 0.01f, 0, tl.clip.duration, "%.2fs") && root) { tl.playing = false; ApplyPose(ed, tl.time); }
		ImGui::SameLine(); ImGui::SetNextItemWidth(70);
		if (ImGui::DragFloat("Length", &tl.clip.duration, 0.05f, 0.05f, 600.0f, "%.2fs")) tl.dirty = true;
		ImGui::SameLine(); ImGui::SetNextItemWidth(55);
		if (ImGui::DragFloat("FPS", &tl.clip.fps, 0.5f, 1.0f, 240.0f, "%.0f")) tl.dirty = true;
		ImGui::SameLine(); ImGui::SetNextItemWidth(55);
		ImGui::DragFloat("Speed", &tl.speed, 0.01f, 0.05f, 8.0f, "%.2f");
		ImGui::SameLine();
		if (ImGui::Checkbox("Loop", &tl.loop)) tl.dirty = true;
		if (tl.posed) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.3f, 1), ICON_MD_WARNING " pose preview is in the scene, use Reset pose before editing"); }

		if (tl.playing && editing && root)
		{
			tl.time += ImGui::GetIO().DeltaTime * tl.speed;
			if (tl.time >= tl.clip.duration)
			{
				if (tl.loop) tl.time = std::fmod(tl.time, std::max(0.001f, tl.clip.duration));
				else { tl.time = tl.clip.duration; tl.playing = false; }
			}
			ApplyPose(ed, tl.time);
		}

		// ---- add track ----------------------------------------------------------------------------------
		static int addProp = 0;
		static std::string addTarget;
		if (root)
		{
			ImGui::SetNextItemWidth(200);
			if (ImGui::BeginCombo("##tgt", addTarget.empty() ? "(root entity)" : addTarget.c_str()))
			{
				if (ImGui::Selectable("(root entity)", addTarget.empty())) addTarget.clear();
				std::vector<std::string> paths;
				CollectPaths(ed, *root, "", paths, 0);
				for (const std::string& p : paths) if (ImGui::Selectable(p.c_str(), p == addTarget)) addTarget = p;
				ImGui::EndCombo();
			}
			ImGui::SameLine(); ImGui::SetNextItemWidth(100);
			ImGui::Combo("##prop", &addProp, "position\0rotation\0scale\0");
			ImGui::SameLine();
			if (ImGui::Button(ICON_MD_ADD "  Add track"))
			{
				anim::EnsureTrack(tl.clip, addTarget, kProps[addProp]);
				tl.dirty = true;
			}
			ImGui::SameLine();
			ImGui::BeginDisabled(tl.track < 0 || tl.track >= (int)tl.clip.tracks.size());
			if (ImGui::Button(ICON_MD_DELETE "  Remove track")) { tl.clip.tracks.erase(tl.clip.tracks.begin() + tl.track); tl.track = tl.key = -1; tl.dirty = true; }
			ImGui::EndDisabled();
		}

		// ---- tracks + canvas --------------------------------------------------------------------------------
		const float labelW = 210, rowH = 24, rulerH = 26;
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const float availW = ImGui::GetContentRegionAvail().x;
		const float availH = std::max(120.0f, ImGui::GetContentRegionAvail().y - 110);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImU32 cBg = IM_COL32(24, 25, 29, 255), cRow = IM_COL32(32, 34, 40, 255), cGrid = IM_COL32(255, 255, 255, 18),
			cText = IM_COL32(190, 194, 205, 255), cAccent = IM_COL32(94, 160, 255, 255), cKey = IM_COL32(236, 190, 90, 255);
		const float cx = origin.x + labelW, cw = std::max(50.0f, availW - labelW);
		dl->AddRectFilled(origin, ImVec2(origin.x + availW, origin.y + availH), cBg, 4);

		// ruler: click / drag scrubs, wheel zooms, middle-drag pans
		ImGui::SetCursorScreenPos(ImVec2(cx, origin.y));
		ImGui::InvisibleButton("##ruler", ImVec2(cw, rulerH));
		if (ImGui::IsItemActive() && ImGui::IsMouseDown(0) && root)
		{
			tl.playing = false;
			tl.time = std::clamp((ImGui::GetIO().MousePos.x - cx) / tl.zoom + tl.scroll, 0.0f, tl.clip.duration);
			if (ImGui::GetIO().KeyShift == false) tl.time = Snap(tl.clip, tl.time);
			ApplyPose(ed, tl.time);
		}
		const float step = tl.zoom > 260 ? 0.25f : tl.zoom > 120 ? 0.5f : tl.zoom > 50 ? 1.0f : 5.0f;
		dl->PushClipRect(ImVec2(cx, origin.y), ImVec2(cx + cw, origin.y + availH), true);
		for (float t = std::floor(tl.scroll / step) * step; (t - tl.scroll) * tl.zoom < cw; t += step)
		{
			const float x = cx + (t - tl.scroll) * tl.zoom;
			dl->AddLine(ImVec2(x, origin.y + rulerH - 8), ImVec2(x, origin.y + availH), cGrid);
			char b[24];
			snprintf(b, sizeof(b), "%.2g", t);
			dl->AddText(ImVec2(x + 3, origin.y + 4), cText, b);
		}
		{ // end of clip
			const float xe = cx + (tl.clip.duration - tl.scroll) * tl.zoom;
			dl->AddRectFilled(ImVec2(xe, origin.y + rulerH), ImVec2(cx + cw, origin.y + availH), IM_COL32(0, 0, 0, 90));
		}

		const bool canvasHover = ImGui::IsMouseHoveringRect(ImVec2(cx, origin.y), ImVec2(cx + cw, origin.y + availH));
		if (canvasHover)
		{
			const float wheel = ImGui::GetIO().MouseWheel;
			if (wheel != 0 && ImGui::GetIO().KeyCtrl)
			{
				const float tAt = (ImGui::GetIO().MousePos.x - cx) / tl.zoom + tl.scroll;
				tl.zoom = std::clamp(tl.zoom * (1.0f + wheel * 0.12f), 20.0f, 1200.0f);
				tl.scroll = std::max(0.0f, tAt - (ImGui::GetIO().MousePos.x - cx) / tl.zoom);
			}
			else if (wheel != 0) tl.scroll = std::max(0.0f, tl.scroll - wheel * 40.0f / tl.zoom);
			if (ImGui::IsMouseDragging(2)) tl.scroll = std::max(0.0f, tl.scroll - ImGui::GetIO().MouseDelta.x / tl.zoom);
		}

		// track rows
		int pendingAddTrack = -1; float pendingAddTime = 0;
		for (int i = 0; i < (int)tl.clip.tracks.size(); ++i)
		{
			anim::Track& tr = tl.clip.tracks[i];
			const float y = origin.y + rulerH + i * rowH;
			if (y + rowH > origin.y + availH) break;
			const bool sel = tl.track == i;
			dl->AddRectFilled(ImVec2(origin.x, y), ImVec2(origin.x + availW, y + rowH - 1), sel ? IM_COL32(44, 52, 70, 255) : cRow);
			std::string label = (tr.target.empty() ? std::string("(root)") : tr.target) + "  " + tr.path;
			dl->AddText(ImVec2(origin.x + 8, y + 5), cText, label.c_str());

			ImGui::SetCursorScreenPos(ImVec2(origin.x, y));
			ImGui::PushID(i);
			if (ImGui::InvisibleButton("##lbl", ImVec2(labelW, rowH - 1))) { tl.track = i; tl.key = -1; }
			ImGui::SetCursorScreenPos(ImVec2(cx, y));
			ImGui::InvisibleButton("##lane", ImVec2(cw, rowH - 1), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
			const bool laneHover = ImGui::IsItemHovered();
			if (laneHover && ImGui::IsMouseDoubleClicked(0))
			{
				pendingAddTrack = i;
				pendingAddTime = Snap(tl.clip, (ImGui::GetIO().MousePos.x - cx) / tl.zoom + tl.scroll);
			}

			int hovered = -1;
			for (int k = 0; k < (int)tr.keys.size(); ++k)
			{
				const float x = cx + (tr.keys[k].time - tl.scroll) * tl.zoom, my = y + rowH * 0.5f;
				const bool ks = sel && tl.key == k;
				if (laneHover && std::fabs(ImGui::GetIO().MousePos.x - x) < 7) hovered = k;
				const float r = ks ? 6.5f : 5.5f;
				const ImU32 col = ks ? cAccent : cKey;
				dl->AddQuadFilled(ImVec2(x, my - r), ImVec2(x + r, my), ImVec2(x, my + r), ImVec2(x - r, my), col);
			}
			if (laneHover && ImGui::IsMouseClicked(0)) { tl.track = i; tl.key = hovered; }
			if (sel && tl.key >= 0 && tl.key < (int)tr.keys.size() && ImGui::IsMouseDragging(0) && ImGui::IsItemActive())
			{
				const float t = Snap(tl.clip, (ImGui::GetIO().MousePos.x - cx) / tl.zoom + tl.scroll);
				if (std::fabs(t - tr.keys[tl.key].time) > 1e-4f) { tl.key = anim::MoveKey(tr, tl.key, t); tl.dirty = true; }
			}
			if (laneHover && ImGui::IsMouseClicked(1)) { tl.track = i; tl.key = hovered; ImGui::OpenPopup("keyctx"); }
			if (ImGui::BeginPopup("keyctx"))
			{
				const float tAt = Snap(tl.clip, (ImGui::GetIO().MousePos.x - cx) / tl.zoom + tl.scroll);
				if (ImGui::MenuItem("Add key here (capture current)")) { pendingAddTrack = i; pendingAddTime = tAt; }
				if (tl.key >= 0 && tl.key < (int)tr.keys.size() && ImGui::MenuItem("Delete key")) { anim::RemoveKey(tr, tl.key); tl.key = -1; tl.dirty = true; }
				if (ImGui::BeginMenu("Interpolation"))
				{
					for (const char* m : {"linear", "step", "smooth"})
						if (ImGui::MenuItem(m, nullptr, tr.interp == m)) { tr.interp = m; tl.dirty = true; }
					ImGui::EndMenu();
				}
				ImGui::EndPopup();
			}
			ImGui::PopID();
		}
		if (pendingAddTrack >= 0)
		{
			anim::Track& tr = tl.clip.tracks[pendingAddTrack];
			Float3 v = anim::Sample(tr, pendingAddTime);
			if (tr.keys.empty())
			{
				v = anim::DefaultValue(tr.path);
				const SceneEntity* te = tr.target.empty() ? root : ResolveTarget(ed, tl.rootId, tr.target);
				if (te) { SceneTransform t = te->transform; if (const Float3* f = Field(t, tr.path)) v = *f; }
			}
			else if (const SceneEntity* te = tr.target.empty() ? root : ResolveTarget(ed, tl.rootId, tr.target); te && !tl.posed)
			{
				SceneTransform t = te->transform;
				if (const Float3* f = Field(t, tr.path)) v = *f;
			}
			tl.key = anim::AddKey(tr, pendingAddTime, v);
			tl.track = pendingAddTrack;
			tl.clip.duration = std::max(tl.clip.duration, pendingAddTime);
			tl.dirty = true;
		}

		// events row
		{
			const float y = origin.y + rulerH + (float)tl.clip.tracks.size() * rowH + 4;
			if (y + rowH < origin.y + availH)
			{
				dl->AddRectFilled(ImVec2(origin.x, y), ImVec2(origin.x + availW, y + rowH - 1), cRow);
				dl->AddText(ImVec2(origin.x + 8, y + 5), cText, ICON_MD_FLAG "  Events");
				ImGui::SetCursorScreenPos(ImVec2(cx, y));
				ImGui::InvisibleButton("##events", ImVec2(cw, rowH - 1));
				const bool hov = ImGui::IsItemHovered();
				int hit = -1;
				for (int k = 0; k < (int)tl.clip.events.size(); ++k)
				{
					const float x = cx + (tl.clip.events[k].time - tl.scroll) * tl.zoom;
					if (hov && std::fabs(ImGui::GetIO().MousePos.x - x) < 7) hit = k;
					dl->AddTriangleFilled(ImVec2(x - 5, y + 4), ImVec2(x + 5, y + 4), ImVec2(x, y + rowH - 5), tl.event == k ? cAccent : IM_COL32(120, 210, 150, 255));
				}
				if (hov && ImGui::IsMouseClicked(0)) tl.event = hit;
				if (hov && ImGui::IsMouseDoubleClicked(0) && hit < 0)
				{
					tl.clip.events.push_back({Snap(tl.clip, (ImGui::GetIO().MousePos.x - cx) / tl.zoom + tl.scroll), "event"});
					anim::SortEvents(tl.clip);
					tl.event = (int)tl.clip.events.size() - 1;
					tl.dirty = true;
				}
			}
		}

		// playhead
		{
			const float x = cx + (tl.time - tl.scroll) * tl.zoom;
			dl->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + availH), cAccent, 1.5f);
			dl->AddTriangleFilled(ImVec2(x - 6, origin.y), ImVec2(x + 6, origin.y), ImVec2(x, origin.y + 10), cAccent);
		}
		dl->PopClipRect();
		ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + availH + 6));

		// ---- selected key / event -------------------------------------------------------------------------
		if (tl.track >= 0 && tl.track < (int)tl.clip.tracks.size() && tl.key >= 0 && tl.key < (int)tl.clip.tracks[tl.track].keys.size())
		{
			anim::Track& tr = tl.clip.tracks[tl.track];
			anim::Key& k = tr.keys[tl.key];
			ImGui::SetNextItemWidth(80);
			if (ImGui::DragFloat("Time##k", &k.time, 0.01f, 0, 600, "%.3f")) { tl.key = anim::MoveKey(tr, tl.key, std::max(0.0f, k.time)); tl.dirty = true; }
			ImGui::SameLine(); ImGui::SetNextItemWidth(260);
			float v[3] = {tr.keys[tl.key].value.x, tr.keys[tl.key].value.y, tr.keys[tl.key].value.z};
			if (ImGui::DragFloat3("Value##k", v, tr.path == "rotation" ? 0.5f : 0.02f))
			{
				tr.keys[tl.key].value = {v[0], v[1], v[2]};
				tl.dirty = true;
				if (root) ApplyPose(ed, tl.time);
			}
			ImGui::SameLine();
			if (ImGui::Button("Capture from scene"))
			{
				const SceneEntity* te = tr.target.empty() ? root : ResolveTarget(ed, tl.rootId, tr.target);
				if (te) { SceneTransform t = te->transform; if (const Float3* f = Field(t, tr.path)) { tr.keys[tl.key].value = *f; tl.dirty = true; } }
			}
			ImGui::SameLine();
			if (ImGui::Button(ICON_MD_DELETE "##dk")) { anim::RemoveKey(tr, tl.key); tl.key = -1; tl.dirty = true; }
		}
		if (tl.event >= 0 && tl.event < (int)tl.clip.events.size())
		{
			anim::Event& e = tl.clip.events[tl.event];
			char buf[96];
			snprintf(buf, sizeof(buf), "%s", e.name.c_str());
			ImGui::SetNextItemWidth(200);
			if (ImGui::InputText("Event##e", buf, sizeof(buf))) { e.name = buf; tl.dirty = true; }
			ImGui::SameLine(); ImGui::SetNextItemWidth(80);
			if (ImGui::DragFloat("At##e", &e.time, 0.01f, 0, 600, "%.3f")) { anim::SortEvents(tl.clip); tl.dirty = true; }
			ImGui::SameLine();
			if (ImGui::Button(ICON_MD_DELETE "##de")) { tl.clip.events.erase(tl.clip.events.begin() + tl.event); tl.event = -1; tl.dirty = true; }
		}
		ImGui::TextDisabled("Double-click a lane to add a key, double-click Events to add an event, drag keys to move, Ctrl+wheel zoom, middle-drag pan, Shift disables snapping.");
		ImGui::End();
	}
}
