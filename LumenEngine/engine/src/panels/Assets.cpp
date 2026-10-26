#include "Panels.h"
#include <unordered_map>

#include <algorithm>

namespace lumen
{
	void DrawAssets(Ui& ui)
	{
		Editor& ed = *ui.ed;
		if (!ImGui::Begin("Assets", &ed.panel.assets))
		{
			ImGui::End();
			return;
		}
		AssetDatabase& db = ed.assets;
		static char search[64] = "";
		static float thumb = 84;
		static std::string renaming;
		static char renameBuf[128];
		static bool focusRename = false;

		// breadcrumbs + tools
		if (ImGui::Button(ICON_MD_REFRESH)) db.Refresh();
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rescan");
		ImGui::SameLine();
		if (ImGui::Button(ICON_MD_ADD " New")) ImGui::OpenPopup("newasset");
		if (ImGui::BeginPopup("newasset"))
		{
			auto make = [&](const char* label, const std::string& name, const std::string& content) {
				if (ImGui::MenuItem(label))
				{
					std::string p = ed.assetFolder.empty() ? name : ed.assetFolder + "/" + name;
					for (int i = 1; db.Find(p); ++i)
						p = (ed.assetFolder.empty() ? std::string() : ed.assetFolder + "/") + fs::Stem(name) + std::to_string(i) + fs::Extension(name);
					Status s = db.CreateAssetFile(p, content);
					if (s) { db.Refresh(); if (str::EndsWith(p, ".lua") || str::EndsWith(p, ".hlsl")) ed.OpenFile(p); }
					else ed.Toast(s.message, true);
				}
			};
			if (ImGui::MenuItem("Folder"))
			{
				std::string p = ed.assetFolder.empty() ? "New Folder" : ed.assetFolder + "/New Folder";
				for (int i = 1; db.Find(p); ++i) p += std::to_string(i);
				Status s = db.CreateFolder(p);
				if (!s) ed.Toast(s.message, true);
				db.Refresh();
			}
			ImGui::Separator();
			make("Lua script (component)", "new_script.lua",
				"-- Attach this script to an entity in the inspector.\nlocal M = { speed = 1.0 }\n\nfunction M:start()\nend\n\nfunction M:update(dt)\n  -- self.entity is the entity this script is attached to\nend\n\nreturn M\n");
			make("Lua module", "module.lua", "local M = {}\n\nreturn M\n");
			make("HLSL pixel shader", "custom_ps.hlsl", "// Custom pixel shader. Use `lumen_object.hlsli` for the shared surface helpers.\n#include \"lumen_object.hlsli\"\n");
			make("Text file", "notes.txt", "");
			ImGui::EndPopup();
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth(160);
		ImGui::InputTextWithHint("##s", ICON_MD_SEARCH "  search", search, sizeof(search));
		ImGui::SameLine();
		ImGui::SetNextItemWidth(90);
		ImGui::SliderFloat("##z", &thumb, 48, 160, "");
		ImGui::SameLine();
		// breadcrumb
		{
			std::vector<std::string> parts = str::Split(ed.assetFolder, '/', false);
			if (ImGui::SmallButton(ed.project.Settings().name.c_str())) ed.assetFolder.clear();
			std::string acc;
			for (const std::string& p : parts)
			{
				acc += (acc.empty() ? "" : "/") + p;
				ImGui::SameLine(); ImGui::TextDisabled("/"); ImGui::SameLine();
				if (ImGui::SmallButton(p.c_str())) ed.assetFolder = acc;
			}
		}
		ImGui::Separator();

		// left: folder tree
		ImGui::BeginChild("folders", ImVec2(170, 0), true);
		std::function<void(const std::string&)> tree = [&](const std::string& folder) {
			for (const AssetEntry* c : db.Children(folder))
			{
				if (!c->isDirectory) continue;
				bool hasSub = false;
				for (const AssetEntry* s : db.Children(c->path)) if (s->isDirectory) { hasSub = true; break; }
				ImGuiTreeNodeFlags fl = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
				if (!hasSub) fl |= ImGuiTreeNodeFlags_Leaf;
				if (ed.assetFolder == c->path) fl |= ImGuiTreeNodeFlags_Selected;
				const std::string label = std::string(ed.assetFolder == c->path ? ICON_MD_FOLDER_OPEN : ICON_MD_FOLDER) + "  " + c->name;
				const bool open = ImGui::TreeNodeEx(c->path.c_str(), fl, "%s", label.c_str());
				if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) ed.assetFolder = c->path;
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("LUMEN_ASSET"))
					{
						Status s = db.Move(std::string((const char*)pl->Data, pl->DataSize - 1), c->path);
						if (!s) ed.Toast(s.message, true);
						db.Refresh();
					}
					ImGui::EndDragDropTarget();
				}
				if (open) { tree(c->path); ImGui::TreePop(); }
			}
		};
		if (ImGui::Selectable(ICON_MD_FOLDER_OPEN "  Project", ed.assetFolder.empty())) ed.assetFolder.clear();
		tree("");
		ImGui::EndChild();
		ImGui::SameLine();

		// right: grid
		ImGui::BeginChild("grid", ImVec2(0, 0), false);
		std::vector<const AssetEntry*> items;
		if (search[0])
		{
			AssetSearchOptions o;
			o.query = search;
			o.includeFolders = true;
			o.limit = 400;
			items = db.Search(o);
		}
		else
			items = db.Children(ed.assetFolder);
		const float cell = thumb + 14;
		const int cols = std::max(1, (int)(ImGui::GetContentRegionAvail().x / cell));
		int col = 0;
		for (const AssetEntry* a : items)
		{
			if (a->name.size() > 5 && a->name.substr(a->name.size() - 5) == ".meta") continue;
			ImGui::PushID(a->path.c_str());
			ImGui::BeginGroup();
			const bool selected = ed.assetSelected == a->path;
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(1, 0.62f, 0.25f, 0.22f));
			if (ImGui::Selectable("##a", selected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(thumb + 6, thumb + 30)))
			{
				ed.assetSelected = a->path;
				if (ImGui::IsMouseDoubleClicked(0))
				{
					if (a->isDirectory) { ed.assetFolder = a->path; search[0] = 0; }
					else if (a->type == AssetType::Scene) { Status s = ed.OpenScene(a->path); if (!s) ed.Toast(s.message, true); }
					else if (a->type == AssetType::Prefab) ed.InstantiatePrefab(a->path);
					else if (a->type == AssetType::Animation && a->extension == ".lanim") { ed.panel.timeline = true; ed.timelineOpen = a->path; }
					else if (a->type == AssetType::Script || a->type == AssetType::Shader || a->type == AssetType::Native || a->type == AssetType::Text || a->type == AssetType::Data)
					{ ed.panel.code = true; ed.OpenFile(a->path); }
				}
			}
			ImGui::PopStyleColor();
			if (ImGui::BeginDragDropSource())
			{
				ImGui::SetDragDropPayload("LUMEN_ASSET", a->path.c_str(), a->path.size() + 1);
				ImGui::Text("%s  %s", IconForAsset(a->type), a->name.c_str());
				ImGui::EndDragDropSource();
			}
			if (ImGui::BeginPopupContextItem("actx"))
			{
				ed.assetSelected = a->path;
				if (a->type == AssetType::Prefab && ImGui::MenuItem("Instantiate in scene")) ed.InstantiatePrefab(a->path);
				if (!a->isDirectory && ImGui::MenuItem("Open")) { ed.panel.code = true; ed.OpenFile(a->path); }
				if (ImGui::MenuItem("Rename", "F2")) { renaming = a->path; snprintf(renameBuf, sizeof(renameBuf), "%s", a->name.c_str()); focusRename = true; }
				if (ImGui::MenuItem("Duplicate")) { std::string np; Status s = db.Duplicate(a->path, &np); if (!s) ed.Toast(s.message, true); db.Refresh(); }
				if (ImGui::MenuItem("Delete")) { Status s = db.Delete(a->path); if (!s) ed.Toast(s.message, true); db.Refresh(); }
				ImGui::Separator();
				const std::vector<std::string> deps = db.Dependents(a->path);
				if (ImGui::BeginMenu(("Used by (" + std::to_string(deps.size()) + ")").c_str(), !deps.empty()))
				{
					for (const std::string& d : deps) ImGui::MenuItem(d.c_str());
					ImGui::EndMenu();
				}
				ImGui::EndPopup();
			}
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const ImVec2 iconBoxMin(pos.x + 3, pos.y + 3), iconBoxMax(pos.x + 3 + thumb, pos.y + 3 + thumb);
			dl->AddRectFilled(iconBoxMin, iconBoxMax, IM_COL32(255, 255, 255, 10), 4);
			const char* icon = IconForAsset(a->type);
			ImGui::PushFont(ui::BoldFont());
			const ImVec2 isz = ImGui::CalcTextSize(icon);
			ImU32 tint = a->isDirectory ? IM_COL32(232, 190, 110, 255) : a->type == AssetType::Script ? IM_COL32(120, 180, 240, 255) :
				a->type == AssetType::Texture ? IM_COL32(150, 210, 150, 255) : a->type == AssetType::Shader ? IM_COL32(210, 140, 220, 255) : IM_COL32(190, 195, 205, 255);
			dl->AddText(ImVec2(pos.x + 3 + (thumb - isz.x) * 0.5f, pos.y + 3 + (thumb - isz.y) * 0.5f), tint, icon);
			ImGui::PopFont();
			if (a->type == AssetType::Texture && !a->isDirectory)
			{
				// real thumbnail for images (loaded lazily, at most a few per frame)
				static std::unordered_map<std::string, wi::Resource> thumbs;
				static int loadedThisFrame = 0;
				static uint64_t lastFrame = 0;
				if (lastFrame != ed.frame) { lastFrame = ed.frame; loadedThisFrame = 0; }
				auto it = thumbs.find(a->path);
				if (it == thumbs.end() && loadedThisFrame < 4)
				{
					++loadedThisFrame;
					const std::string ext = str::ToLower(fs::Extension(a->path));
					wi::Resource r;
					if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" || ext == ".dds" || ext == ".webp")
						r = wi::resourcemanager::Load(ed.project.Resolve(a->path));
					it = thumbs.emplace(a->path, std::move(r)).first;
				}
				if (it != thumbs.end() && it->second.IsValid())
				{
					const wi::graphics::Texture& t = it->second.GetTexture();
					if (t.IsValid())
					{
						const float tw = (float)t.desc.width, th = (float)t.desc.height;
						const float k = std::min((thumb - 6) / std::max(1.0f, tw), (thumb - 6) / std::max(1.0f, th));
						const ImVec2 sz(tw * k, th * k);
						const ImVec2 c((iconBoxMin.x + iconBoxMax.x) * 0.5f, (iconBoxMin.y + iconBoxMax.y) * 0.5f);
						dl->AddRectFilled(iconBoxMin, iconBoxMax, IM_COL32(24, 26, 31, 255), 4);
						dl->AddImage(ui::TextureId(&t), ImVec2(c.x - sz.x * 0.5f, c.y - sz.y * 0.5f), ImVec2(c.x + sz.x * 0.5f, c.y + sz.y * 0.5f));
					}
				}
			}
			if (renaming == a->path)
			{
				ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + thumb + 6));
				ImGui::SetNextItemWidth(thumb + 6);
				if (focusRename) { ImGui::SetKeyboardFocusHere(); focusRename = false; }
				if (ImGui::InputText("##rn", renameBuf, sizeof(renameBuf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
				{
					Status s = db.Rename(a->path, renameBuf);
					if (!s) ed.Toast(s.message, true);
					renaming.clear();
					db.Refresh();
				}
				else if (ImGui::IsItemDeactivated()) renaming.clear();
			}
			else
			{
				std::string n = a->name;
				const float maxw = thumb + 4;
				while (n.size() > 3 && ImGui::CalcTextSize(n.c_str()).x > maxw) n = n.substr(0, n.size() - 2);
				if (n.size() < a->name.size()) n += "..";
				dl->AddText(ImVec2(pos.x + 4, pos.y + thumb + 8), IM_COL32(220, 222, 228, 255), n.c_str());
			}
			ImGui::EndGroup();
			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(a->path.c_str());
				ImGui::TextDisabled("%s  %s", AssetTypeName(a->type), a->isDirectory ? "" : FormatBytes(a->size).c_str());
				ImGui::EndTooltip();
			}
			ImGui::PopID();
			if (++col < cols) ImGui::SameLine(); else col = 0;
		}
		// external files dropped / imported through the button
		if (items.empty())
		{
			ImGui::TextDisabled("This folder is empty.");
		}
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && ImGui::IsKeyPressed(ImGuiKey_F2) && !ed.assetSelected.empty())
		{
			renaming = ed.assetSelected;
			const AssetEntry* a = db.Find(ed.assetSelected);
			snprintf(renameBuf, sizeof(renameBuf), "%s", a ? a->name.c_str() : "");
			focusRename = true;
		}
		ImGui::EndChild();
		ImGui::End();
	}
}
