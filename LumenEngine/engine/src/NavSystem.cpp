#include "NavSystem.h"
#include "lumen/core/Log.h"

#include <algorithm>
#include <cmath>

using namespace wi::scene;
using namespace wi::ecs;

namespace lumen
{
	namespace
	{
		Float3 F(const XMFLOAT3& v) { return {v.x, v.y, v.z}; }
		XMFLOAT3 X(const Float3& v) { return {v.x, v.y, v.z}; }
		float Dist2D(const Float3& a, const Float3& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z)); }
	}

	void NavSystem::Reset()
	{
		mesh_.Clear();
		agents_.clear();
		borderCache_.clear();
		borderDirty_ = true;
	}

	bool NavSystem::Bake(Scene& scene, const Bridge& bridge, const SceneDoc& doc)
	{
		const SceneEntity* nav = nullptr;
		for (const SceneEntity& e : doc.entities)
			if (e.navmesh && e.active) { nav = &e; break; }
		if (!nav)
			return false;
		const Entity ne = bridge.Find(nav->id);
		XMFLOAT3 centre = nav->transform.position.x == 0 && nav->transform.position.y == 0 && nav->transform.position.z == 0 ? XMFLOAT3(0, 0, 0) : X(nav->transform.position);
		if (const TransformComponent* t = scene.transforms.GetComponent(ne))
			centre = t->GetPosition();
		nav::BuildSettings s;
		s.cellSize = nav->navmesh->cellSize;
		s.agentRadius = nav->navmesh->agentRadius;
		s.agentHeight = nav->navmesh->agentHeight;
		s.maxSlope = nav->navmesh->maxSlope;
		s.stepHeight = nav->navmesh->stepHeight;
		const Float3 half{nav->navmesh->bounds.x * 0.5f, nav->navmesh->bounds.y * 0.5f, nav->navmesh->bounds.z * 0.5f};
		s.boundsMin = {centre.x - half.x, centre.y - half.y, centre.z - half.z};
		s.boundsMax = {centre.x + half.x, centre.y + half.y, centre.z + half.z};

		std::vector<nav::Triangle> tris;
		for (size_t oi = 0; oi < scene.objects.GetCount(); ++oi)
		{
			const Entity oe = scene.objects.GetEntity(oi);
			const ObjectComponent& obj = scene.objects[oi];
			if (!obj.IsRenderable() || obj.meshID == INVALID_ENTITY)
				continue;
			const std::string id = bridge.IdOfOrAncestor(scene, oe);
			const SceneEntity* de = id.empty() ? nullptr : doc.Find(id);
			if (!de || !de->isStatic || !de->active)
				continue;
			const MeshComponent* mesh = scene.meshes.GetComponent(obj.meshID);
			const TransformComponent* tr = scene.transforms.GetComponent(oe);
			if (!mesh || !tr || mesh->vertex_positions.empty() || mesh->indices.empty())
				continue;
			const XMMATRIX W = XMLoadFloat4x4(&tr->world);
			std::vector<XMFLOAT3> pos(mesh->vertex_positions.size());
			for (size_t i = 0; i < pos.size(); ++i)
				XMStoreFloat3(&pos[i], XMVector3Transform(XMLoadFloat3(&mesh->vertex_positions[i]), W));
			const bool haveNormals = mesh->vertex_normals.size() == mesh->vertex_positions.size();
			for (size_t i = 0; i + 2 < mesh->indices.size(); i += 3)
			{
				const uint32_t i0 = mesh->indices[i], i1 = mesh->indices[i + 1], i2 = mesh->indices[i + 2];
				if (i0 >= pos.size() || i1 >= pos.size() || i2 >= pos.size())
					continue;
				nav::Triangle t{F(pos[i0]), F(pos[i1]), F(pos[i2])};
				if (haveNormals)
				{
					// orient the triangle along the shading normal so "up" means the visible side
					XMVECTOR n = XMVectorAdd(XMVectorAdd(XMLoadFloat3(&mesh->vertex_normals[i0]), XMLoadFloat3(&mesh->vertex_normals[i1])), XMLoadFloat3(&mesh->vertex_normals[i2]));
					n = XMVector3TransformNormal(n, W);
					const XMVECTOR e1 = XMVectorSet(t.b.x - t.a.x, t.b.y - t.a.y, t.b.z - t.a.z, 0), e2 = XMVectorSet(t.c.x - t.a.x, t.c.y - t.a.y, t.c.z - t.a.z, 0);
					const XMVECTOR g = XMVector3Cross(e1, e2); // right handed cross, as the nav code uses
					if (XMVectorGetX(XMVector3Dot(g, n)) < 0)
						std::swap(t.b, t.c);
				}
				tris.push_back(t);
			}
		}
		borderDirty_ = true;
		const bool ok = mesh_.Build(tris, s);
		if (ok)
			LUMEN_INFO("nav", "baked %d walkable cells in %.1f ms (cell %.2f m)", mesh_.GetStats().walkable, mesh_.GetStats().bakeMilliseconds, mesh_.GetStats().cellSize);
		else
			LUMEN_WARN("nav", "navmesh bake found no walkable surface (are the floors marked static?)");
		return ok;
	}

	// ---- agents ---------------------------------------------------------------------------------------------------
	void NavSystem::AddAgent(Scene& scene, Entity e, const SceneNavAgent& cfg)
	{
		Agent a;
		a.entity = e;
		a.cfg = cfg;
		if (const TransformComponent* t = scene.transforms.GetComponent(e))
		{
			Float3 p, pos = F(t->GetPosition());
			if (mesh_.Built() && mesh_.Project(pos, p, 3.0f))
				a.groundOffset = std::min(3.0f, std::max(0.0f, pos.y - p.y));
		}
		agents_.push_back(std::move(a));
	}

	void NavSystem::RemoveAgent(Entity e)
	{
		agents_.erase(std::remove_if(agents_.begin(), agents_.end(), [&](const Agent& a) { return a.entity == e; }), agents_.end());
	}

	NavSystem::Agent* NavSystem::Find(Entity e)
	{
		for (Agent& a : agents_)
			if (a.entity == e)
				return &a;
		return nullptr;
	}

	bool NavSystem::MoveTo(Scene& scene, Entity e, const Float3& dest)
	{
		Agent* a = Find(e);
		const TransformComponent* t = scene.transforms.GetComponent(e);
		if (!a || !t || !mesh_.Built())
			return false;
		Float3 start = F(t->GetPosition());
		start.y -= a->groundOffset;
		std::vector<Float3> path;
		bool partial = false;
		if (!mesh_.FindPath(start, dest, path, true, &partial) || path.size() < 2)
		{
			a->moving = false;
			return false;
		}
		a->path = std::move(path);
		a->next = 1;
		a->goal = a->path.back();
		a->moving = true;
		a->partial = partial;
		return true;
	}

	void NavSystem::Stop(Entity e)
	{
		if (Agent* a = Find(e))
		{
			a->moving = false;
			a->path.clear();
		}
	}

	float NavSystem::Remaining(const Agent& a) const
	{
		if (!a.moving || a.path.empty())
			return 0;
		float d = 0;
		for (size_t i = a.next; i < a.path.size(); ++i)
		{
			const Float3& p0 = i == a.next ? a.path[a.next > 0 ? a.next - 1 : 0] : a.path[i - 1];
			d += Dist2D(p0, a.path[i]);
		}
		return d;
	}

	void NavSystem::Update(Scene& scene, float dt, const Hooks& hooks)
	{
		if (dt <= 0)
			return;
		for (size_t ai = 0; ai < agents_.size(); ++ai)
		{
			Agent& a = agents_[ai];
			const TransformComponent* tr = scene.transforms.GetComponent(a.entity);
			if (!tr)
				continue;
			Float3 pos = F(tr->GetPosition());
			const float maxSpeed = a.speedOverride >= 0 ? a.speedOverride : a.cfg.speed;
			Float3 desired{0, 0, 0};
			bool arrived = false;
			if (a.moving && a.next < a.path.size())
			{
				// advance through reached waypoints
				while (a.next < a.path.size())
				{
					const Float3& wp = a.path[a.next];
					const bool last = a.next + 1 == a.path.size();
					const float reach = last ? std::max(0.05f, a.cfg.stoppingDistance) : std::max(0.25f, a.cfg.radius);
					if (Dist2D(pos, wp) <= reach)
					{
						if (last) { arrived = true; break; }
						++a.next;
					}
					else break;
				}
				if (!arrived && a.next < a.path.size())
				{
					const Float3& wp = a.path[a.next];
					float dx = wp.x - pos.x, dz = wp.z - pos.z;
					const float len = std::sqrt(dx * dx + dz * dz);
					if (len > 1e-4f)
					{
						dx /= len; dz /= len;
						// slow down when approaching the final point
						float speed = maxSpeed;
						if (a.next + 1 == a.path.size())
							speed = std::min(speed, std::max(0.5f, len * 3.0f));
						desired = {dx * speed, 0, dz * speed};
					}
				}
			}
			// local avoidance: push away from overlapping agents
			if (a.moving)
				for (size_t bi = 0; bi < agents_.size(); ++bi)
				{
					if (bi == ai) continue;
					const TransformComponent* ot = scene.transforms.GetComponent(agents_[bi].entity);
					if (!ot) continue;
					const Float3 op = F(ot->GetPosition());
					float dx = pos.x - op.x, dz = pos.z - op.z;
					const float d = std::sqrt(dx * dx + dz * dz), minD = a.cfg.radius + agents_[bi].cfg.radius;
					if (d < minD && d > 1e-4f && std::fabs(pos.y - op.y) < 2.5f)
					{
						const float push = (minD - d) / minD * maxSpeed * 1.5f;
						desired.x += dx / d * push;
						desired.z += dz / d * push;
					}
				}
			// acceleration limited velocity
			const float maxDv = a.cfg.acceleration * dt;
			float dvx = desired.x - a.velocity.x, dvz = desired.z - a.velocity.z;
			const float dv = std::sqrt(dvx * dvx + dvz * dvz);
			if (dv > maxDv && dv > 1e-6f) { dvx = dvx / dv * maxDv; dvz = dvz / dv * maxDv; }
			a.velocity.x += dvx;
			a.velocity.z += dvz;
			if (!a.moving && std::fabs(a.velocity.x) + std::fabs(a.velocity.z) < 1e-3f)
				a.velocity = {0, 0, 0};

			if (a.cfg.autoMove && (a.velocity.x != 0 || a.velocity.z != 0))
			{
				Float3 np{pos.x + a.velocity.x * dt, pos.y, pos.z + a.velocity.z * dt};
				// stick to the surface
				Float3 ground;
				if (mesh_.Built() && mesh_.Project({np.x, pos.y - a.groundOffset, np.z}, ground, 1.5f))
				{
					const float targetY = ground.y + a.groundOffset;
					np.y = pos.y + (targetY - pos.y) * std::min(1.0f, dt * 14.0f);
				}
				if (hooks.setPosition) hooks.setPosition(a.entity, X(np));
				if (a.cfg.autoRotate && hooks.setYaw)
				{
					const float sp = std::sqrt(a.velocity.x * a.velocity.x + a.velocity.z * a.velocity.z);
					if (sp > 0.2f) hooks.setYaw(a.entity, std::atan2(a.velocity.x, a.velocity.z));
				}
			}
			if (arrived)
			{
				a.moving = false;
				a.path.clear();
				const bool wasPartial = a.partial;
				if (wasPartial) { if (hooks.failed) hooks.failed(a.entity); }
				else if (hooks.arrived) hooks.arrived(a.entity);
			}
		}
	}

	void NavSystem::DrawDebug(Scene&, bool paths, bool mesh)
	{
		if (mesh && mesh_.Built())
		{
			if (borderDirty_)
			{
				borderCache_.clear();
				mesh_.DebugBorderLines(borderCache_);
				borderDirty_ = false;
			}
			wi::renderer::RenderableLine line;
			line.color_start = line.color_end = XMFLOAT4(0.2f, 0.9f, 0.5f, 1);
			for (size_t i = 0; i + 1 < borderCache_.size(); i += 2)
			{
				line.start = X(borderCache_[i]);
				line.end = X(borderCache_[i + 1]);
				wi::renderer::DrawLine(line);
			}
		}
		if (paths)
			for (const Agent& a : agents_)
			{
				if (!a.moving) continue;
				wi::renderer::RenderableLine line;
				line.color_start = line.color_end = XMFLOAT4(1.0f, 0.75f, 0.2f, 1);
				for (size_t i = a.next; i < a.path.size(); ++i)
				{
					line.start = X(a.path[i > 0 ? i - 1 : 0]);
					line.start.y += 0.1f;
					line.end = X(a.path[i]);
					line.end.y += 0.1f;
					wi::renderer::DrawLine(line);
				}
			}
	}
}
