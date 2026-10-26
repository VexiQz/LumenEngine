#include "Panels.h"

#include "lumen/core/Fs.h"
#include "lumen/core/Terrain.h"

#include <cmath>

namespace lumen
{
	namespace
	{
		struct State
		{
			std::string id, key;
			terrain::Heightfield hf;
			bool stroking = false, dirty = false;
			double lastDab = 0, lastMesh = 0;
			float flattenTarget = 0.5f;
		};
		State& St()
		{
			static State s;
			return s;
		}
		std::string ParamKey(const SceneTerrain& t)
		{
			SceneTerrain c = t;
			c.heightmap.clear();
			return Json(SceneDoc::EntityToJson([&] { SceneEntity e; e.terrain = c; return e; }())).Dump(0);
		}
	}

	// Ray marches the heightfield; `world` is the terrain entity's world matrix. Returns false when the ray misses.
	static bool Intersect(const SceneTerrain& t, const terrain::Heightfield& hf, const XMFLOAT4X4& world, const wi::primitive::Ray& ray, float& lx, float& lz, XMFLOAT3& worldHit)
	{
		const XMMATRIX inv = XMMatrixInverse(nullptr, XMLoadFloat4x4(&world));
		XMFLOAT3 o, d;
		XMStoreFloat3(&o, XMVector3Transform(XMLoadFloat3(&ray.origin), inv));
		XMStoreFloat3(&d, XMVector3Normalize(XMVector3TransformNormal(XMLoadFloat3(&ray.direction), inv)));
		const float cell = std::max(t.size.x, t.size.y) / (float)std::max(1, hf.res - 1);
		const float step = std::max(0.25f, cell * 0.7f);
		const float maxT = 4.0f * std::max(t.size.x, t.size.y) + t.height * 4.0f;
		auto above = [&](float tt, float& h) {
			const float x = o.x + d.x * tt, z = o.z + d.z * tt, y = o.y + d.y * tt;
			if (std::fabs(x) > t.size.x * 0.5f || std::fabs(z) > t.size.y * 0.5f) { h = -1e9f; return true; }
			h = terrain::HeightAt(t, hf, x, z);
			return y > h;
		};
		float prev = 0, h;
		bool wasAbove = above(0, h);
		for (float tt = step; tt < maxT; tt += step)
		{
			const bool a = above(tt, h);
			if (wasAbove && !a)
			{
				float lo = prev, hi = tt;
				for (int i = 0; i < 14; ++i)
				{
					const float mid = (lo + hi) * 0.5f;
					float hh;
					if (above(mid, hh)) lo = mid; else hi = mid;
				}
				const float tt2 = (lo + hi) * 0.5f;
				lx = o.x + d.x * tt2;
				lz = o.z + d.z * tt2;
				const XMFLOAT3 lp(lx, terrain::HeightAt(t, hf, lx, lz), lz);
				XMStoreFloat3(&worldHit, XMVector3Transform(XMLoadFloat3(&lp), XMLoadFloat4x4(&world)));
				return true;
			}
			wasAbove = a;
			prev = tt;
		}
		return false;
	}

	void TerrainSculpt(Ui& ui, float originX, float originY, float availW, float availH, bool hovered)
	{
		Editor& ed = *ui.ed;
		State& st = St();
		SceneEntity* e = ed.PrimaryEntity();
		if (!e || !e->terrain || !ed.terrainTool.active || ed.play != PlayState::Edit)
		{
			st.stroking = false;
			return;
		}
		SceneTerrain& t = *e->terrain;
		const std::string key = ParamKey(t);
		if (st.id != e->id || st.key != key || !st.hf.Valid())
		{
			if (st.dirty && st.id != e->id) st.dirty = false;
			st.id = e->id;
			st.key = key;
			terrain::Resolve(t, ed.project.Root(), st.hf);
		}
		ImGuiIO& io = ImGui::GetIO();
		const wi::ecs::Entity ent = ui.bridge->Find(e->id);
		const wi::scene::TransformComponent* tr = ui.wscene->transforms.GetComponent(ent);
		if (!tr)
			return;
		const bool mouseDown = ImGui::IsMouseDown(0);
		if (!hovered && !st.stroking)
			return;
		const float sx = (io.MousePos.x - originX) * (float)ui.vp->GetPhysicalWidth() / availW;
		const float sy = (io.MousePos.y - originY) * (float)ui.vp->GetPhysicalHeight() / availH;
		const wi::primitive::Ray ray = wi::renderer::GetPickRay((long)sx, (long)sy, *ui.vp, *ui.editCam);
		float lx, lz;
		XMFLOAT3 wh;
		const bool hit = Intersect(t, st.hf, tr->world, ray, lx, lz, wh);
		if (hit)
		{
			// brush ring
			XMFLOAT3 prev;
			for (int i = 0; i <= 48; ++i)
			{
				const float a = (float)i / 48.0f * 6.2831853f;
				const float px = lx + std::cos(a) * ed.terrainTool.radius, pz = lz + std::sin(a) * ed.terrainTool.radius;
				const XMFLOAT3 lp(px, terrain::HeightAt(t, st.hf, px, pz) + 0.15f, pz);
				XMFLOAT3 w;
				XMStoreFloat3(&w, XMVector3Transform(XMLoadFloat3(&lp), XMLoadFloat4x4(&tr->world)));
				if (i > 0)
				{
					wi::renderer::RenderableLine l;
					l.start = prev;
					l.end = w;
					l.color_start = l.color_end = XMFLOAT4(1.0f, 0.75f, 0.2f, 1);
					wi::renderer::DrawLine(l);
				}
				prev = w;
			}
		}
		if (hit && mouseDown && (hovered || st.stroking) && !io.KeyAlt)
		{
			if (!st.stroking)
			{
				st.stroking = true;
				st.flattenTarget = t.height > 0 ? terrain::HeightAt(t, st.hf, lx, lz) / t.height : 0.5f;
			}
			if (ed.now - st.lastDab > 0.03)
			{
				st.lastDab = ed.now;
				terrain::Brush b = (terrain::Brush)std::min(4, std::max(0, ed.terrainTool.brush));
				if (io.KeyShift) b = terrain::Brush::Smooth;
				else if (io.KeyCtrl && b == terrain::Brush::Raise) b = terrain::Brush::Lower;
				else if (io.KeyCtrl && b == terrain::Brush::Lower) b = terrain::Brush::Raise;
				const float u = lx / std::max(1.0f, t.size.x) + 0.5f, v = lz / std::max(1.0f, t.size.y) + 0.5f;
				if (terrain::ApplyBrush(st.hf, t, b, u, v, ed.terrainTool.radius, ed.terrainTool.strength, st.flattenTarget))
					st.dirty = true;
			}
			if (st.dirty && ed.now - st.lastMesh > 0.05)
			{
				st.lastMesh = ed.now;
				ui.bridge->UpdateTerrainMesh(*ui.wscene, e->id, t, st.hf);
			}
		}
		else if (st.stroking && !mouseDown)
		{
			st.stroking = false;
			if (st.dirty)
			{
				st.dirty = false;
				const std::string rel = "terrain/" + e->id + ".height";
				if (terrain::SaveRaw(ed.project.Resolve(rel), st.hf))
				{
					t.heightmap = rel;
					st.key = ParamKey(t); // heightmap path is not part of the key
					ui.bridge->UpdateTerrainMesh(*ui.wscene, e->id, t, st.hf);
					ui.bridge->Invalidate(e->id); // rebuilds the collider
					ed.Touch();
				}
				else ed.Toast("Cannot save the terrain heightmap", true);
			}
		}
	}
}
