#include "Panels.h"
#include "lumen/core/Strings.h"

#include <algorithm>

namespace lumen
{
	namespace
	{
		const char* IconFor(const SceneEntity& e)
		{
			if (e.vehicle) return ICON_MD_DIRECTIONS_CAR;
			if (e.joint) return ICON_MD_LINK;
			if (e.rigidbody && e.rigidbody->trigger) return ICON_MD_CROP_FREE;
			if (e.camera) return ICON_MD_VIDEOCAM;
			if (e.light) return ICON_MD_LIGHTBULB;
			if (e.audio) return ICON_MD_SPEAKER;
			if (e.mesh) return ICON_MD_VIEW_IN_AR;
			return ICON_MD_CIRCLE;
		}

		struct Ctx { Ui& ui; std::string filter; std::string renaming; bool focusRename = false; };

		bool Matches(const SceneDoc& d, const SceneEntity& e, const std::string& f)
		{
			if (f.empty()) return true;
			if (str::FuzzyScore(f, e.name) > 0) return true;
			for (const SceneEntity* c : d.Children(e.id))
				if (Matches(d, *c, f)) return true;
			return false;
		}

		void DrawNode(Ctx& c, const std::string& id)
		{
			Editor& ed = *c.ui.ed;
			SceneEntity* e = ed.scene.Find(id);
			if (!e || !Matches(ed.scene, *e, c.filter))
				return;
			const std::vector<const SceneEntity*> kids = ed.scene.Children(id);
			ImGuiTreeNodeFlags fl = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
			if (kids.empty()) fl |= ImGuiTreeNodeFlags_Leaf;
			if (ed.IsSelected(id)) fl |= ImGuiTreeNodeFlags_Selected;
			if (!c.filter.empty()) ImGui::SetNextItemOpen(true);
			ImGui::PushID(id.c_str());
			if (!e->active) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
			std::string label = std::string(IconFor(*e)) + "  " + e->name;
			const bool open = ImGui::TreeNodeEx("##n", fl, "%s", c.renaming == id ? "" : label.c_str());
			ImGui::PopStyleVar();
			if (!e->active) ImGui::PopStyleColor();
			// visibility toggle at the right edge of the row
			bool eyeClicked = false;
			{
				const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
				const ImVec2 a(mx.x - 26, mn.y), b(mx.x - 2, mx.y);
				const bool hot = ImGui::IsMouseHoveringRect(a, b) && ImGui::IsWindowHovered();
				const ImU32 col = hot ? IM_COL32(255, 160, 70, 255) : (e->active ? IM_COL32(200, 205, 215, 150) : IM_COL32(120, 125, 135, 150));
				const char* ic = e->active ? ICON_MD_VISIBILITY : ICON_MD_VISIBILITY_OFF;
				const ImVec2 sz = ImGui::CalcTextSize(ic);
				ImGui::GetWindowDrawList()->AddText(ImVec2(a.x + (b.x - a.x - sz.x) * 0.5f, a.y + (b.y - a.y - sz.y) * 0.5f), col, ic);
				if (hot && ImGui::IsMouseClicked(0)) { e->active = !e->active; ed.Touch(); eyeClicked = true; }
			}

			if (c.renaming == id)
			{
				ImGui::SameLine();
				char buf[128];
				snprintf(buf, sizeof(buf), "%s", e->name.c_str());
				if (c.focusRename) { ImGui::SetKeyboardFocusHere(); c.focusRename = false; }
				ImGui::SetNextItemWidth(-1);
				if (ImGui::InputText("##rename", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
				{
					if (buf[0] && e->name != buf) { e->name = ed.scene.UniqueName(buf, e->parent); ed.Touch(); }
					c.renaming.clear();
				}
				else if (ImGui::IsItemDeactivated())
					c.renaming.clear();
			}
			else
			{
				if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen() && !eyeClicked)
					ed.Select(id, ImGui::GetIO().KeyCtrl);
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) { ed.Select(id); ed.FocusSelection(); }
			}
			if (ImGui::BeginPopupContextItem("ctx"))
			{
				if (!ed.IsSelected(id)) ed.Select(id);
				if (ImGui::MenuItem("Rename", "F2")) { c.renaming = id; c.focusRename = true; }
				if (ImGui::MenuItem("Duplicate", "Ctrl+D")) ed.DuplicateSelection();
				if (ImGui::MenuItem("Copy", "Ctrl+C")) ed.CopySelection();
				if (ImGui::MenuItem("Cut", "Ctrl+X")) ed.CutSelection();
				if (ImGui::MenuItem("Paste", "Ctrl+V", false, ed.CanPaste())) ed.Paste();
				if (ImGui::MenuItem("Paste as child", "Ctrl+Shift+V", false, ed.CanPaste())) ed.Paste(id);
				if (ImGui::MenuItem("Save as prefab"))
				{
					std::string path;
					const Status s = ed.SaveAsPrefab(id, &path);
					ed.Toast(s ? "Prefab saved: " + path : s.message, !s);
				}
				if (ImGui::MenuItem("Delete", "Del")) ed.DeleteSelection();
				ImGui::Separator();
				if (ImGui::MenuItem(e->active ? "Deactivate" : "Activate")) { e->active = !e->active; ed.Touch(); }
				if (ImGui::MenuItem("Create child")) { SceneEntity n = MakeEmptyEntity("Empty"); n.parent = id; ed.CreateEntity(n); }
				if (ImGui::MenuItem("Unparent", nullptr, false, !e->parent.empty())) { ed.scene.SetParent(id, ""); ed.Touch(); }
				if (ImGui::MenuItem("Focus", "F")) { ed.Select(id); ed.FocusSelection(); }
				ImGui::EndPopup();
			}
			// drag & drop reparenting
			if (ImGui::BeginDragDropSource())
			{
				ImGui::SetDragDropPayload("LUMEN_ENTITY", id.c_str(), id.size() + 1);
				ImGui::TextUnformatted(e->name.c_str());
				ImGui::EndDragDropSource();
			}
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("LUMEN_ENTITY"))
				{
					const std::string dragged((const char*)pl->Data);
					if (ed.scene.SetParent(dragged, id))
					{
						// keep the world position: convert the transform through the Wicked world matrices
						SceneEntity* de = ed.scene.Find(dragged);
						const wi::ecs::Entity we = c.ui.bridge->Find(dragged);
						if (de && we != wi::ecs::INVALID_ENTITY)
							if (const auto* t = c.ui.wscene->transforms.GetComponent(we))
								de->transform = c.ui.bridge->LocalFromWorld(*c.ui.wscene, id, t->world);
						ed.Touch();
					}
				}
				ImGui::EndDragDropTarget();
			}
			if (open)
			{
				for (const SceneEntity* k : kids)
					DrawNode(c, k->id);
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
	}

	void DrawHierarchy(Ui& ui)
	{
		Editor& ed = *ui.ed;
		if (!ImGui::Begin("Hierarchy", &ed.panel.hierarchy))
		{
			ImGui::End();
			return;
		}
		static char filter[64] = "";
		static Ctx* dummy = nullptr; (void)dummy;
		static std::string renaming;
		static bool focusRename = false;
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 34);
		ImGui::InputTextWithHint("##f", ICON_MD_SEARCH "  filter", filter, sizeof(filter));
		ImGui::SameLine();
		if (ImGui::Button(ICON_MD_ADD, ImVec2(26, 0))) ImGui::OpenPopup("add");
		if (ImGui::BeginPopup("add"))
		{
			if (ImGui::MenuItem("Empty")) ed.CreateEntity(MakeEmptyEntity("Empty"));
			for (const char* p : {"cube", "sphere", "plane", "cylinder", "capsule", "cone"})
			{
				std::string l = p; l[0] = (char)toupper(l[0]);
				if (ImGui::MenuItem(l.c_str())) ed.CreateEntity(MakePrimitiveEntity(p, l));
			}
			if (ImGui::MenuItem("Point light")) ed.CreateEntity(MakeLightEntity("point", "Point Light"));
			if (ImGui::MenuItem("Spot light")) ed.CreateEntity(MakeLightEntity("spot", "Spot Light"));
			if (ImGui::MenuItem("Camera")) ed.CreateEntity(MakeCameraEntity("Camera", false));
			if (ImGui::BeginMenu("Environment"))
			{
				if (ImGui::MenuItem("Terrain"))
				{
					SceneEntity n = MakeEmptyEntity("Terrain");
					n.terrain = SceneTerrain{};
					n.isStatic = true;
					ed.CreateEntity(n);
				}
				if (ImGui::MenuItem("Particle emitter"))
				{
					SceneEntity n = MakeEmptyEntity("Particles");
					n.particles = SceneParticles{};
					ed.CreateEntity(n);
				}
				if (ImGui::MenuItem("Navigation mesh volume"))
				{
					SceneEntity n = MakeEmptyEntity("NavMesh");
					n.navmesh = SceneNavMesh{};
					ed.CreateEntity(n);
				}
				if (ImGui::MenuItem("Navigation agent (capsule)"))
				{
					SceneEntity n = MakePrimitiveEntity("capsule", "Agent");
					n.transform.position = {0, 1, 0};
					n.navagent = SceneNavAgent{};
					ed.CreateEntity(n);
				}
				ImGui::EndMenu();
			}
			ImGui::Separator();
			if (ImGui::BeginMenu("Physics"))
			{
				if (ImGui::MenuItem("Physics box (dynamic)"))
				{
					SceneEntity n = MakePrimitiveEntity("cube", "Box");
					n.rigidbody = SceneRigidBody{};
					ed.CreateEntity(n);
				}
				if (ImGui::MenuItem("Trigger volume"))
				{
					SceneEntity n = MakePrimitiveEntity("cube", "Trigger");
					n.transform.scale = {3, 3, 3};
					if (n.material) { n.material->color = {0.3f, 0.7f, 1.0f, 0.18f}; n.material->alphaBlend = true; }
					SceneRigidBody rb;
					rb.type = "static";
					rb.trigger = true;
					n.rigidbody = rb;
					ed.CreateEntity(n);
				}
				if (ImGui::MenuItem("Car"))
				{
					SceneEntity n = MakePrimitiveEntity("cube", "Car");
					n.transform.scale = {1.8f, 0.5f, 4.0f};
					n.transform.position = {0, 1.5f, 0};
					n.rigidbody = SceneRigidBody{};
					n.rigidbody->mass = 1200;
					n.vehicle = SceneVehicle{};
					ed.CreateEntity(n);
				}
				if (ImGui::MenuItem("Hinge joint (to world)"))
				{
					SceneEntity n = MakePrimitiveEntity("cube", "Hinged box");
					n.rigidbody = SceneRigidBody{};
					n.joint = SceneJoint{};
					n.joint->type = "hinge";
					ed.CreateEntity(n);
				}
				ImGui::EndMenu();
			}
			ImGui::EndPopup();
		}
		ImGui::Separator();
		ImGui::BeginChild("tree", ImVec2(0, 0), false);
		Ctx c{ui, filter, renaming, focusRename};
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && ImGui::IsKeyPressed(ImGuiKey_F2) && !ed.primary.empty())
		{
			c.renaming = ed.primary;
			c.focusRename = true;
		}
		for (const SceneEntity& e : std::vector<SceneEntity>(ed.scene.entities))
			if (e.parent.empty())
				DrawNode(c, e.id);
		renaming = c.renaming;
		focusRename = c.focusRename;
		// drop on empty space: unparent
		ImGui::Dummy(ImGui::GetContentRegionAvail());
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("LUMEN_ENTITY"))
			{
				const std::string dragged((const char*)pl->Data);
				SceneEntity* de = ed.scene.Find(dragged);
				const wi::ecs::Entity we = ui.bridge->Find(dragged);
				if (de && !de->parent.empty() && ed.scene.SetParent(dragged, ""))
				{
					if (we != wi::ecs::INVALID_ENTITY)
						if (const auto* t = ui.wscene->transforms.GetComponent(we))
							de->transform = ui.bridge->LocalFromWorld(*ui.wscene, "", t->world);
					ed.Touch();
				}
			}
			ImGui::EndDragDropTarget();
		}
		if (ImGui::IsItemClicked(0)) ed.Select("");
		ImGui::EndChild();
		ImGui::End();
	}
}
