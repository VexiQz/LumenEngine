#include "Panels.h"

#include <algorithm>

using namespace wi::scene;

namespace lumen
{
	static ImGuizmo::OPERATION Op(GizmoMode m)
	{
		return m == GizmoMode::Translate ? ImGuizmo::TRANSLATE : m == GizmoMode::Rotate ? ImGuizmo::ROTATE : ImGuizmo::SCALE;
	}

	void DrawViewport(Ui& ui)
	{
		Editor& ed = *ui.ed;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		const bool open = ImGui::Begin("Viewport", &ed.panel.viewport, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		ImGui::PopStyleVar();
		if (!open)
		{
			ImGui::End();
			return;
		}
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		ImVec2 avail = ImGui::GetContentRegionAvail();
		avail.x = std::max(32.0f, avail.x);
		avail.y = std::max(32.0f, avail.y);
		ui.vpX = origin.x;
		ui.vpY = origin.y;
		ui.vpW = avail.x;
		ui.vpH = avail.y;
		ui.vpHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
		ui.vpFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);

		const wi::graphics::Texture* tex = ui.vp->GetLastPostprocessRT();
		if (tex && tex->IsValid())
			ImGui::Image(ui::TextureId(tex), avail);
		else
			ImGui::Dummy(avail);
		const bool imageHovered = ImGui::IsItemHovered();
		ImDrawList* dl = ImGui::GetWindowDrawList();

		static bool toolbarHot = false; // mouse was over the toolbar last frame
		// ---- selection gizmo -------------------------------------------------------------------
		bool usingGizmo = false;
		SceneEntity* sel = ed.PrimaryEntity();
		if (sel && ed.play == PlayState::Edit)
		{
			const wi::ecs::Entity we = ui.bridge->Find(sel->id);
			TransformComponent* t = we != wi::ecs::INVALID_ENTITY ? ui.wscene->transforms.GetComponent(we) : nullptr;
			if (t)
			{
				float view[16], proj[16];
				XMStoreFloat4x4((XMFLOAT4X4*)view, ui.editCam->GetView());
				XMStoreFloat4x4((XMFLOAT4X4*)proj, ui.editCam->GetProjection());
				ImGuizmo::SetOrthographic(false);
				ImGuizmo::SetDrawlist(dl);
				ImGuizmo::SetRect(origin.x, origin.y, avail.x, avail.y);
				XMFLOAT4X4 world = t->world;
				float snap[3];
				const float sv = ed.gizmo == GizmoMode::Translate ? ed.snapMove : ed.gizmo == GizmoMode::Rotate ? ed.snapRotate : ed.snapScale;
				snap[0] = snap[1] = snap[2] = sv;
				const bool snapNow = ed.snap || ImGui::GetIO().KeyCtrl;
				ImGuizmo::Manipulate(view, proj, Op(ed.gizmo), ed.gizmoWorld && ed.gizmo != GizmoMode::Scale ? ImGuizmo::WORLD : ImGuizmo::LOCAL, &world._11,
					nullptr, snapNow ? snap : nullptr);
				if (ImGuizmo::IsUsing())
				{
					usingGizmo = true;
					sel->transform = ui.bridge->LocalFromWorld(*ui.wscene, sel->parent, world);
					ed.Touch();
				}
			}
		}

		// ---- picking ------------------------------------------------------------------------------
		ImGuiIO& io = ImGui::GetIO();
		if (imageHovered && ed.play == PlayState::Edit && !usingGizmo && !toolbarHot && !ImGuizmo::IsOver() && ImGui::IsMouseReleased(0) && !io.KeyAlt && !ed.terrainTool.active &&
			ImGui::GetMouseDragDelta(0).x * ImGui::GetMouseDragDelta(0).x + ImGui::GetMouseDragDelta(0).y * ImGui::GetMouseDragDelta(0).y < 16)
		{
			const float sx = (io.MousePos.x - origin.x) * (float)ui.vp->GetPhysicalWidth() / avail.x;
			const float sy = (io.MousePos.y - origin.y) * (float)ui.vp->GetPhysicalHeight() / avail.y;
			const wi::primitive::Ray ray = wi::renderer::GetPickRay((long)sx, (long)sy, *ui.vp, *ui.editCam);
			const auto hit = wi::scene::Pick(ray, wi::enums::FILTER_OBJECT_ALL, ~0u, *ui.wscene);
			const std::string id = hit.entity != wi::ecs::INVALID_ENTITY ? ui.bridge->IdOfOrAncestor(*ui.wscene, hit.entity) : std::string();
			ed.Select(id, io.KeyShift);
		}

		TerrainSculpt(ui, origin.x, origin.y, avail.x, avail.y, imageHovered && !usingGizmo && !toolbarHot);

		// ---- selection bounds + grid -----------------------------------------------------------------
		if (ed.play == PlayState::Edit)
		{
			if (ed.showBounds)
				for (const std::string& id : ed.selection)
				{
					wi::primitive::AABB bb;
					if (ui.bridge->Bounds(*ui.wscene, ed.scene, id, bb))
					{
						XMFLOAT4X4 m;
						XMStoreFloat4x4(&m, bb.getAsBoxMatrix());
						wi::renderer::DrawBox(m, id == ed.primary ? XMFLOAT4(1.0f, 0.62f, 0.25f, 1) : XMFLOAT4(1, 1, 1, 0.5f));
					}
				}
			if (ed.showGrid)
			{
				const int n = 40;
				const float c = 1.0f;
				for (int i = -n; i <= n; ++i)
				{
					const float a = i == 0 ? 0.55f : (i % 5 == 0 ? 0.28f : 0.12f);
					wi::renderer::DrawLine(wi::renderer::RenderableLine{XMFLOAT3(i * c, 0, -n * c), XMFLOAT3(i * c, 0, n * c), XMFLOAT4(0.8f, 0.8f, 0.85f, a), XMFLOAT4(0.8f, 0.8f, 0.85f, a)});
					wi::renderer::DrawLine(wi::renderer::RenderableLine{XMFLOAT3(-n * c, 0, i * c), XMFLOAT3(n * c, 0, i * c), XMFLOAT4(0.8f, 0.8f, 0.85f, a), XMFLOAT4(0.8f, 0.8f, 0.85f, a)});
				}
			}
		}

		// ---- viewport toolbar (top right) -----------------------------------------------------------
		{
			struct Btn { const char* icon; const char* tip; bool on; };
			const bool wire = wi::renderer::IsWireRender();
			const bool phys = wi::physics::IsDebugDrawEnabled();
			const Btn btns[] = {
				{ICON_MD_GRID_ON, "Grid", ed.showGrid},
				{ICON_MD_CROP_FREE, "Selection bounds", ed.showBounds},
				{ICON_MD_BORDER_ALL, "Wireframe", wire},
				{ICON_MD_VIEW_IN_AR, "Physics debug (Jolt shapes and joints)", phys},
				{ICON_MD_MONITOR_HEART, "Statistics", ed.showStats},
			};
			const float bs = 26.0f, gap = 2.0f;
			const float w = (float)(sizeof(btns) / sizeof(btns[0])) * (bs + gap) + 6 + 120;
			ImGui::SetCursorScreenPos(ImVec2(origin.x + avail.x - w - 8, origin.y + 8));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, 0));
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.09f, 0.11f, 0.72f));
			int idx = 0;
			for (const Btn& b : btns)
			{
				if (idx++) ImGui::SameLine();
				const bool on = b.on;
				if (on) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.62f, 0.25f, 1));
				ImGui::PushID(idx);
				const bool clicked = ImGui::Button(b.icon, ImVec2(bs, bs));
				ImGui::PopID();
				if (on) ImGui::PopStyleColor();
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", b.tip);
				if (clicked)
				{
					if (idx == 1) ed.showGrid = !ed.showGrid;
					else if (idx == 2) ed.showBounds = !ed.showBounds;
					else if (idx == 3)
					{
						const auto mode = wi::renderer::GetWireframeMode();
						wi::renderer::SetWireframeMode(mode == wi::renderer::WIREFRAME_DISABLED ? wi::renderer::WIREFRAME_ONLY : wi::renderer::WIREFRAME_DISABLED);
					}
					else if (idx == 4) wi::physics::SetDebugDrawEnabled(!phys);
					else if (idx == 5) ed.showStats = !ed.showStats;
				}
			}
			ImGui::SameLine(0, 8);
			ImGui::TextDisabled(ICON_MD_SPEED);
			ImGui::SameLine(0, 4);
			ImGui::SetNextItemWidth(88);
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.09f, 0.11f, 0.72f));
			ImGui::SliderFloat("##camspeed", &ui.cam->speed, 1.0f, 80.0f, "%.0f");
			ImGui::PopStyleColor();
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Camera speed (mouse wheel while flying)");
			ImGui::PopStyleColor();
			ImGui::PopStyleVar(2);
			toolbarHot = ImGui::IsMouseHoveringRect(ImVec2(origin.x + avail.x - w - 12, origin.y + 4), ImVec2(origin.x + avail.x, origin.y + 8 + bs + 4));
		}

		// ---- overlays ------------------------------------------------------------------------------
		if (ui.host && ed.play != PlayState::Edit)
		{
			dl->PushClipRect(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), true);
			for (const UiCmd& c : ui.host->UiCommands())
			{
				const ImU32 col = IM_COL32((int)(c.color[0] * 255), (int)(c.color[1] * 255), (int)(c.color[2] * 255), (int)(c.color[3] * 255));
				const ImVec2 a(origin.x + c.x, origin.y + c.y);
				switch (c.kind)
				{
				case UiCmd::Kind::Rect:
					if (c.thickness > 0) dl->AddRect(a, ImVec2(a.x + c.w, a.y + c.h), col, c.rounding, 0, c.thickness);
					else dl->AddRectFilled(a, ImVec2(a.x + c.w, a.y + c.h), col, c.rounding);
					break;
				case UiCmd::Kind::Line: dl->AddLine(a, ImVec2(origin.x + c.w, origin.y + c.h), col, c.thickness); break;
				case UiCmd::Kind::Image:
					if (const wi::graphics::Texture* t = ui.host->UiTexture(c.text)) dl->AddImage(ui::TextureId(t), a, ImVec2(a.x + c.w, a.y + c.h), ImVec2(0, 0), ImVec2(1, 1), col);
					break;
				case UiCmd::Kind::Text:
				{
					ImFont* f = ImGui::GetFont();
					const ImVec2 sz = f->CalcTextSizeA(c.size, FLT_MAX, 0, c.text.c_str());
					ImVec2 p = a;
					if (c.align == 1) p.x -= sz.x * 0.5f; else if (c.align == 2) p.x -= sz.x;
					if (c.shadow) dl->AddText(f, c.size, ImVec2(p.x + 1, p.y + 1), IM_COL32(0, 0, 0, (int)(c.color[3] * 180)), c.text.c_str());
					dl->AddText(f, c.size, p, col, c.text.c_str());
					break;
				}
				}
			}
			dl->PopClipRect();
		}
		if (ui.debugTexts && ed.play != PlayState::Edit)
			for (const auto& t : *ui.debugTexts)
				dl->AddText(ImVec2(origin.x + t.first.first, origin.y + t.first.second), IM_COL32(255, 255, 255, 235), t.second.c_str());
		if (ed.showStats)
		{
			char buf[160];
			snprintf(buf, sizeof(buf), "%.1f ms  |  %dx%d  |  %zu objects  |  %zu lights", io.DeltaTime * 1000.0f, (int)ui.vp->GetPhysicalWidth(), (int)ui.vp->GetPhysicalHeight(),
				ui.wscene->objects.GetCount(), ui.wscene->lights.GetCount());
			dl->AddText(ImVec2(origin.x + 10, origin.y + 8), IM_COL32(255, 255, 255, 200), buf);
		}
		if (ed.play == PlayState::Edit)
		{
			// small hint line + axis legend of the camera speed
			char buf[96];
			snprintf(buf, sizeof(buf), "RMB fly  |  MMB pan  |  Alt+LMB orbit  |  speed %.0f", ui.cam->speed);
			dl->AddText(ImVec2(origin.x + 10, origin.y + avail.y - 22), IM_COL32(255, 255, 255, 110), buf);
		}
		else if (ed.play == PlayState::Play && ui.host->CursorLocked())
			dl->AddText(ImVec2(origin.x + 10, origin.y + 8), IM_COL32(255, 255, 255, 140), "Cursor locked - Esc releases it");

		// asset drop: models / materials onto the viewport create entities
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("LUMEN_ASSET"))
			{
				const std::string path((const char*)pl->Data, pl->DataSize - 1);
				const AssetEntry* a = ed.assets.Find(path);
				if (a && a->type == AssetType::Model)
				{
					SceneEntity e = MakeEmptyEntity(fs::Stem(path));
					SceneMesh m;
					m.kind = SceneMeshKind::Model;
					m.model = path;
					e.mesh = m;
					ed.CreateEntity(std::move(e));
				}
				else if (a && a->type == AssetType::Prefab)
				{
					// drop point: first surface under the cursor, else the ground plane, else 8 m in front of the camera
					const ImVec2 mp = ImGui::GetMousePos();
					const float sx = (mp.x - origin.x) * (float)ui.vp->GetPhysicalWidth() / avail.x;
					const float sy = (mp.y - origin.y) * (float)ui.vp->GetPhysicalHeight() / avail.y;
					const wi::primitive::Ray ray = wi::renderer::GetPickRay((long)sx, (long)sy, *ui.vp, *ui.editCam);
					const auto hit = wi::scene::Pick(ray, wi::enums::FILTER_OBJECT_ALL, ~0u, *ui.wscene);
					XMFLOAT3 p;
					if (hit.entity != wi::ecs::INVALID_ENTITY) p = hit.position;
					else if (ray.direction.y < -0.001f)
					{
						const float t = -ray.origin.y / ray.direction.y;
						p = XMFLOAT3(ray.origin.x + ray.direction.x * t, 0, ray.origin.z + ray.direction.z * t);
					}
					else p = XMFLOAT3(ray.origin.x + ray.direction.x * 8, ray.origin.y + ray.direction.y * 8, ray.origin.z + ray.direction.z * 8);
					const Float3 pos{p.x, p.y, p.z};
					ed.InstantiatePrefab(path, "", &pos);
				}
				else if (a && a->type == AssetType::Script && ed.PrimaryEntity())
				{
					ed.PrimaryEntity()->scripts.push_back({path, true, Json()});
					ed.Touch();
				}
				else if (a && a->type == AssetType::Audio)
				{
					SceneEntity e = MakeEmptyEntity(fs::Stem(path));
					SceneAudio au;
					au.file = path;
					e.audio = au;
					ed.CreateEntity(std::move(e));
				}
			}
			ImGui::EndDragDropTarget();
		}
		ImGui::End();
	}
}
