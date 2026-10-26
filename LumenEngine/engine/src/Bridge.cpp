#include "Bridge.h"

#include "lumen/core/Fs.h"
#include "lumen/core/Log.h"
#include "lumen/core/Strings.h"
#include "lumen/core/Terrain.h"

#include "../../Editor/ModelImporter.h"

#include <algorithm>
#include <cmath>

using namespace wi::scene;
using namespace wi::ecs;

namespace lumen
{
	namespace
	{
		constexpr float kPi = 3.14159265358979f;

		struct MeshBuilder
		{
			MeshComponent& m;
			void Vertex(XMFLOAT3 p, XMFLOAT3 n, XMFLOAT2 uv, XMFLOAT3 t)
			{
				m.vertex_positions.push_back(p);
				m.vertex_normals.push_back(n);
				m.vertex_uvset_0.push_back(uv);
				m.vertex_tangents.push_back(XMFLOAT4(t.x, t.y, t.z, 1));
			}
			// Triangles are oriented so that the front face (clockwise, left handed) looks along the vertex normal.
			void Tri(uint32_t a, uint32_t b, uint32_t c)
			{
				const XMFLOAT3 &pa = m.vertex_positions[a], &pb = m.vertex_positions[b], &pc = m.vertex_positions[c];
				const XMFLOAT3 &na = m.vertex_normals[a], &nb = m.vertex_normals[b], &nc = m.vertex_normals[c];
				const float ux = pb.x - pa.x, uy = pb.y - pa.y, uz = pb.z - pa.z;
				const float vx = pc.x - pa.x, vy = pc.y - pa.y, vz = pc.z - pa.z;
				const float cx = uy * vz - uz * vy, cy = uz * vx - ux * vz, cz = ux * vy - uy * vx;
				const float nx = na.x + nb.x + nc.x, ny = na.y + nb.y + nc.y, nz = na.z + nb.z + nc.z;
				const bool ok = cx * nx + cy * ny + cz * nz >= 0;
				m.indices.push_back(a);
				m.indices.push_back(ok ? b : c);
				m.indices.push_back(ok ? c : b);
			}
			void Quad(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
			{
				Tri(a, b, c);
				Tri(a, c, d);
			}
		};

		void Cube(MeshBuilder& b, Float3 s)
		{
			const float hx = s.x * 0.5f, hy = s.y * 0.5f, hz = s.z * 0.5f;
			struct Face { XMFLOAT3 n, t, bt; };
			const Face faces[6] = {
				{{0, 0, -1}, {1, 0, 0}, {0, 1, 0}}, {{0, 0, 1}, {-1, 0, 0}, {0, 1, 0}},
				{{1, 0, 0}, {0, 0, 1}, {0, 1, 0}},  {{-1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
				{{0, 1, 0}, {1, 0, 0}, {0, 0, 1}},  {{0, -1, 0}, {1, 0, 0}, {0, 0, -1}},
			};
			for (const Face& f : faces)
			{
				const uint32_t base = (uint32_t)b.m.vertex_positions.size();
				const float su[4] = {-1, 1, 1, -1}, sv[4] = {1, 1, -1, -1};
				for (int i = 0; i < 4; ++i)
				{
					const float ht = fabsf(f.t.x) * hx + fabsf(f.t.y) * hy + fabsf(f.t.z) * hz;
					const float hb = fabsf(f.bt.x) * hx + fabsf(f.bt.y) * hy + fabsf(f.bt.z) * hz;
					const float hn = fabsf(f.n.x) * hx + fabsf(f.n.y) * hy + fabsf(f.n.z) * hz;
					const XMFLOAT3 p(f.n.x * hn + f.t.x * su[i] * ht + f.bt.x * sv[i] * hb, f.n.y * hn + f.t.y * su[i] * ht + f.bt.y * sv[i] * hb,
						f.n.z * hn + f.t.z * su[i] * ht + f.bt.z * sv[i] * hb);
					b.Vertex(p, f.n, XMFLOAT2((su[i] + 1) * 0.5f, (1 - sv[i]) * 0.5f), f.t);
				}
				b.Quad(base, base + 1, base + 2, base + 3);
			}
		}

		void Plane(MeshBuilder& b, Float3 s)
		{
			const float hx = s.x * 0.5f, hz = s.z * 0.5f;
			const XMFLOAT3 n(0, 1, 0), t(1, 0, 0);
			b.Vertex({-hx, 0, hz}, n, {0, 0}, t);
			b.Vertex({hx, 0, hz}, n, {1, 0}, t);
			b.Vertex({hx, 0, -hz}, n, {1, 1}, t);
			b.Vertex({-hx, 0, -hz}, n, {0, 1}, t);
			b.Quad(0, 1, 2, 3);
		}

		// Surface of revolution style generator used by sphere/capsule/cylinder/cone.
		// ring(i) returns {radius, y, normalAngleY (0 = horizontal), v}
		struct Ring { float r, y, ny, nr, v; };
		void Revolve(MeshBuilder& b, const std::vector<Ring>& rings, int seg)
		{
			const uint32_t base = (uint32_t)b.m.vertex_positions.size();
			for (const Ring& ring : rings)
				for (int j = 0; j <= seg; ++j)
				{
					const float a = 2 * kPi * j / seg;
					const float ca = cosf(a), sa = sinf(a);
					const XMFLOAT3 n(ca * ring.nr, ring.ny, sa * ring.nr);
					const float len = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
					b.Vertex({ca * ring.r, ring.y, sa * ring.r}, {n.x / len, n.y / len, n.z / len}, {(float)j / seg, ring.v}, {-sa, 0, ca});
				}
			const uint32_t stride = (uint32_t)seg + 1;
			for (size_t i = 0; i + 1 < rings.size(); ++i)
				for (int j = 0; j < seg; ++j)
				{
					const uint32_t a = base + (uint32_t)i * stride + j, c = a + stride;
					if (rings[i].r < 1e-6f)
						b.Tri(a, c, c + 1);
					else if (rings[i + 1].r < 1e-6f)
						b.Tri(a, c + 1, a + 1);
					else
						b.Quad(a, a + 1, c + 1, c);
				}
		}

		void Cap(MeshBuilder& b, float r, float y, float ny, int seg)
		{
			const uint32_t base = (uint32_t)b.m.vertex_positions.size();
			b.Vertex({0, y, 0}, {0, ny, 0}, {0.5f, 0.5f}, {1, 0, 0});
			for (int j = 0; j <= seg; ++j)
			{
				const float a = 2 * kPi * j / seg;
				b.Vertex({cosf(a) * r, y, sinf(a) * r}, {0, ny, 0}, {0.5f + 0.5f * cosf(a), 0.5f + 0.5f * sinf(a)}, {1, 0, 0});
			}
			for (int j = 0; j < seg; ++j)
				b.Tri(base, base + 1 + j, base + 2 + j);
		}

		void HemisphereRings(std::vector<Ring>& out, float r, float yCenter, bool top, int stacks, float vBase, float vSpan)
		{
			for (int i = 0; i <= stacks; ++i)
			{
				const float t = (float)i / stacks * (kPi * 0.5f); // 0 = equator, pi/2 = pole
				const float rr = r * cosf(t), yy = r * sinf(t);
				const float y = yCenter + (top ? yy : -yy);
				const float v = top ? vBase - vSpan * (float)i / stacks : vBase + vSpan * (float)i / stacks;
				out.push_back({rr, y, top ? sinf(t) : -sinf(t), cosf(t), v});
			}
		}
	} // namespace

	void BuildPrimitiveMesh(MeshComponent& mesh, const std::string& primitive, const Float3& size, int segments)
	{
		mesh.vertex_positions.clear();
		mesh.vertex_normals.clear();
		mesh.vertex_uvset_0.clear();
		mesh.vertex_tangents.clear();
		mesh.indices.clear();
		MeshBuilder b{mesh};
		const int seg = std::max(6, std::min(128, segments));
		const float r = std::max(1e-4f, size.x * 0.5f), h = std::max(1e-4f, size.y);
		if (primitive == "plane")
			Plane(b, size);
		else if (primitive == "sphere")
		{
			// ellipsoid with per axis diameters
			std::vector<Ring> rings;
			const int stacks = std::max(4, seg / 2);
			for (int i = 0; i <= stacks; ++i)
			{
				const float t = kPi * 0.5f - kPi * i / stacks;
				rings.push_back({cosf(t) * r, sinf(t) * size.y * 0.5f, sinf(t), cosf(t), (float)i / stacks});
			}
			Revolve(b, rings, seg);
		}
		else if (primitive == "cylinder")
		{
			Revolve(b, {{r, h * 0.5f, 0, 1, 0}, {r, -h * 0.5f, 0, 1, 1}}, seg);
			Cap(b, r, h * 0.5f, 1, seg);
			Cap(b, r, -h * 0.5f, -1, seg);
		}
		else if (primitive == "cone")
		{
			const float slope = r / h;
			Revolve(b, {{0, h * 0.5f, 1, slope, 0}, {r, -h * 0.5f, 1, slope, 1}}, seg);
			Cap(b, r, -h * 0.5f, -1, seg);
		}
		else if (primitive == "capsule")
		{
			const float rad = std::min(r, h * 0.5f);
			const float half = std::max(0.0f, h * 0.5f - rad);
			std::vector<Ring> rings;
			const int stacks = std::max(3, seg / 4);
			HemisphereRings(rings, rad, half, true, stacks, 0.35f, 0.35f);
			std::reverse(rings.begin(), rings.end()); // pole -> equator
			std::vector<Ring> lower;
			HemisphereRings(lower, rad, -half, false, stacks, 0.65f, 0.35f);
			rings.insert(rings.end(), lower.begin(), lower.end());
			Revolve(b, rings, seg);
		}
		else
			Cube(b, size);

		MeshComponent::MeshSubset& subset = mesh.subsets.emplace_back();
		subset.indexOffset = 0;
		subset.indexCount = (uint32_t)mesh.indices.size();
	}

	namespace
	{
		XMFLOAT3 F3(const Float3& v) { return XMFLOAT3(v.x, v.y, v.z); }
		float Rad(float deg) { return deg * (kPi / 180.0f); }

		void SetTransform(TransformComponent& t, const SceneTransform& s)
		{
			t.translation_local = F3(s.position);
			XMStoreFloat4(&t.rotation_local, XMQuaternionRotationRollPitchYaw(Rad(s.rotation.x), Rad(s.rotation.y), Rad(s.rotation.z)));
			t.scale_local = F3(s.scale);
			t.SetDirty();
		}

		uint64_t TransformHash(const SceneEntity& e)
		{
			const float v[9] = {e.transform.position.x, e.transform.position.y, e.transform.position.z, e.transform.rotation.x, e.transform.rotation.y,
				e.transform.rotation.z, e.transform.scale.x, e.transform.scale.y, e.transform.scale.z};
			return str::Hash64(v, sizeof(v));
		}
		// Everything except the transform and the parent link.
		uint64_t StructHash(const SceneEntity& e)
		{
			SceneEntity c = e;
			c.transform = SceneTransform{};
			c.parent.clear();
			c.id.clear();
			return str::Hash64(lumen::SceneDoc::EntityToJson(c).Dump(0));
		}

		wi::enums::BLENDMODE BlendFor(const SceneMaterial& m) { return m.alphaBlend || m.color.w < 0.999f || m.shader == "glass" ? wi::enums::BLENDMODE_ALPHA : wi::enums::BLENDMODE_OPAQUE; }

		MaterialComponent::SHADERTYPE ShaderTypeFor(const std::string& s)
		{
			if (s == "water") return MaterialComponent::SHADERTYPE_WATER;
			if (s == "toon") return MaterialComponent::SHADERTYPE_CARTOON;
			if (s == "unlit") return MaterialComponent::SHADERTYPE_UNLIT;
			if (s == "terrain") return MaterialComponent::SHADERTYPE_PBR_TERRAINBLENDED;
			return MaterialComponent::SHADERTYPE_PBR;
		}
	} // namespace

	std::string Bridge::ResolveAsset(const std::string& path) const
	{
		if (path.empty())
			return {};
		if (fs::IsAbsolute(path))
			return path;
		return fs::Join(root_, path);
	}

	Entity Bridge::Find(const std::string& id) const
	{
		auto it = records_.find(id);
		return it == records_.end() ? INVALID_ENTITY : it->second.entity;
	}
	std::string Bridge::IdOf(Entity e) const
	{
		auto it = byEntity_.find(e);
		return it == byEntity_.end() ? std::string() : it->second;
	}
	std::string Bridge::IdOfOrAncestor(const Scene& scene, Entity e) const
	{
		for (int guard = 0; e != INVALID_ENTITY && guard < 256; ++guard)
		{
			const std::string id = IdOf(e);
			if (!id.empty())
				return id;
			const HierarchyComponent* h = scene.hierarchy.GetComponent(e);
			if (!h)
				break;
			e = h->parentID;
		}
		return {};
	}

	bool Bridge::UpdateTerrainMesh(Scene& scene, const std::string& id, const SceneTerrain& st, const terrain::Heightfield& hf)
	{
		const Entity e = Find(id);
		MeshComponent* mesh = scene.meshes.GetComponent(e);
		if (!mesh)
			return false;
		terrain::Mesh tm;
		terrain::BuildMesh(st, hf, tm);
		if (tm.positions.size() != mesh->vertex_positions.size())
			return false;
		for (size_t i = 0; i < tm.positions.size(); ++i)
		{
			mesh->vertex_positions[i] = F3(tm.positions[i]);
			mesh->vertex_normals[i] = F3(tm.normals[i]);
			mesh->vertex_colors[i] = wi::Color::fromFloat4(XMFLOAT4(tm.colors[i].x, tm.colors[i].y, tm.colors[i].z, 1)).rgba;
		}
		mesh->CreateRenderData();
		return true;
	}

	void Bridge::Invalidate(const std::string& id)
	{
		auto it = records_.find(id);
		if (it != records_.end())
			it->second.structHash = 0;
	}

	void Bridge::StripComponents(Scene& scene, Entity e, Record& rec)
	{
		rec.animations.clear();
		scene.objects.Remove(e);
		scene.meshes.Remove(e);
		scene.materials.Remove(e);
		scene.lights.Remove(e);
		scene.cameras.Remove(e);
		scene.rigidbodies.Remove(e);
		scene.constraints.Remove(e);
		rec.jointDirty = true;
		scene.sounds.Remove(e);
		scene.emitters.Remove(e);
		for (Entity x : rec.extra)
			scene.Entity_Remove(x);
		rec.extra.clear();
	}

	void Bridge::Populate(Scene& scene, Entity entity, const SceneEntity& e, Record& rec)
	{
		StripComponents(scene, entity, rec);
		const bool hasMesh = e.mesh.has_value();
		if (hasMesh && e.mesh->kind == SceneMeshKind::Primitive)
		{
			ObjectComponent& object = scene.objects.Create(entity);
			MeshComponent& mesh = scene.meshes.Create(entity);
			object.meshID = entity;
			BuildPrimitiveMesh(mesh, e.mesh->primitive, e.mesh->size, e.mesh->segments);
			mesh.subsets[0].materialID = entity;
			scene.materials.Create(entity);
			mesh.SetDoubleSided(e.material && e.material->doubleSided);
			mesh.CreateRenderData();
			object.SetCastShadow(e.mesh->castShadows && (!e.material || e.material->castShadows));
			object.SetRenderable(e.mesh->visible);
			if (e.mesh->cullDistance > 0)
				object.draw_distance = e.mesh->cullDistance;
		}
		else if (hasMesh)
		{
			const std::string file = ResolveAsset(e.mesh->model);
			Scene imported;
			const std::string ext = str::ToLower(fs::Extension(file));
			if (ext == ".obj") ImportModel_OBJ(file, imported);
			else if (ext == ".gltf" || ext == ".glb") ImportModel_GLTF(file, imported);
			else if (ext == ".fbx") ImportModel_FBX(file, imported);
			else if (ext == ".ply") ImportModel_PLY(file, imported);
			else LUMEN_WARN("scene", "unsupported model format: %s", e.mesh->model.c_str());
			if (imported.transforms.GetCount() > 0 || imported.objects.GetCount() > 0)
			{
				// Attach all root entities of the import below this entity.
				std::vector<Entity> roots;
				for (size_t i = 0; i < imported.transforms.GetCount(); ++i)
				{
					const Entity t = imported.transforms.GetEntity(i);
					if (!imported.hierarchy.Contains(t))
						roots.push_back(t);
				}
				std::vector<Entity> clips;
				for (size_t i = 0; i < imported.animations.GetCount(); ++i)
					clips.push_back(imported.animations.GetEntity(i));
				scene.Merge(imported);
				rec.animations = clips;
				for (Entity c : clips)
					rec.extra.push_back(c);
				for (Entity r : roots)
				{
					scene.Component_Attach(r, entity);
					rec.extra.push_back(r);
				}
			}
		}
		if (e.material && hasMesh && e.mesh->kind == SceneMeshKind::Primitive)
		{
			const SceneMaterial& sm = *e.material;
			MaterialComponent& mat = *scene.materials.GetComponent(entity);
			mat.shaderType = ShaderTypeFor(sm.shader);
			mat.userBlendMode = BlendFor(sm);
			mat.SetBaseColor(XMFLOAT4(sm.color.x, sm.color.y, sm.color.z, sm.color.w));
			mat.SetRoughness(sm.roughness);
			mat.SetMetalness(sm.metalness);
			mat.SetReflectance(sm.reflectance * 0.08f);
			mat.SetEmissiveColor(XMFLOAT4(sm.emissive.x, sm.emissive.y, sm.emissive.z, sm.emissiveStrength));
			mat.SetDoubleSided(sm.doubleSided);
			if (sm.alphaCutoff > 0) mat.SetAlphaRef(1.0f - sm.alphaCutoff);
			mat.texMulAdd = XMFLOAT4(sm.tiling.x, sm.tiling.y, 0, 0);
			if (sm.shader == "glass") mat.SetTransmissionAmount(0.9f);
			auto tex = [&](MaterialComponent::TEXTURESLOT slot, const std::string& p) {
				if (p.empty()) return;
				const std::string abs = ResolveAsset(p);
				if (!fs::Exists(abs)) { LUMEN_WARN("scene", "texture not found: %s", p.c_str()); return; }
				mat.textures[slot].name = abs;
				mat.textures[slot].resource = wi::resourcemanager::Load(abs);
			};
			tex(MaterialComponent::BASECOLORMAP, sm.baseColorMap);
			tex(MaterialComponent::NORMALMAP, sm.normalMap);
			tex(MaterialComponent::SURFACEMAP, sm.roughnessMap.empty() ? sm.metalnessMap : sm.roughnessMap);
			tex(MaterialComponent::EMISSIVEMAP, sm.emissiveMap);
			tex(MaterialComponent::OCCLUSIONMAP, sm.occlusionMap);
			if (sm.shader == "water")
			{
				mat.shaderType = MaterialComponent::SHADERTYPE_WATER;
				mat.SetDoubleSided(true);
			}
			mat.SetDirty();
		}
		if (e.terrain)
		{
			const SceneTerrain& st = *e.terrain;
			terrain::Heightfield hf;
			terrain::Resolve(st, root_, hf);
			terrain::Mesh tm;
			terrain::BuildMesh(st, hf, tm);
			ObjectComponent& object = scene.objects.Create(entity);
			MeshComponent& mesh = scene.meshes.Create(entity);
			object.meshID = entity;
			mesh.vertex_positions.resize(tm.positions.size());
			mesh.vertex_normals.resize(tm.positions.size());
			mesh.vertex_uvset_0.resize(tm.positions.size());
			mesh.vertex_colors.resize(tm.positions.size());
			mesh.vertex_tangents.resize(tm.positions.size());
			for (size_t i = 0; i < tm.positions.size(); ++i)
			{
				mesh.vertex_positions[i] = F3(tm.positions[i]);
				mesh.vertex_normals[i] = F3(tm.normals[i]);
				mesh.vertex_uvset_0[i] = XMFLOAT2(tm.uvs[i].x, tm.uvs[i].y);
				mesh.vertex_tangents[i] = XMFLOAT4(1, 0, 0, 1);
				mesh.vertex_colors[i] = wi::Color::fromFloat4(XMFLOAT4(tm.colors[i].x, tm.colors[i].y, tm.colors[i].z, 1)).rgba;
			}
			mesh.indices = tm.indices;
			mesh.subsets.emplace_back();
			mesh.subsets.back().materialID = entity;
			mesh.subsets.back().indexOffset = 0;
			mesh.subsets.back().indexCount = (uint32_t)mesh.indices.size();
			MaterialComponent& mat = scene.materials.Create(entity);
			mat.SetBaseColor(XMFLOAT4(1, 1, 1, 1));
			mat.SetRoughness(st.roughness);
			mat.SetMetalness(0.0f);
			mat.SetUseVertexColors(true);
			if (!st.texture.empty())
			{
				const std::string abs = ResolveAsset(st.texture);
				if (fs::Exists(abs))
				{
					mat.textures[MaterialComponent::BASECOLORMAP].name = abs;
					mat.textures[MaterialComponent::BASECOLORMAP].resource = wi::resourcemanager::Load(abs);
				}
			}
			mat.SetDirty();
			mesh.CreateRenderData();
			object.SetCastShadow(true);
			if (st.collider)
			{
				RigidBodyPhysicsComponent& p = scene.rigidbodies.Create(entity);
				p.shape = RigidBodyPhysicsComponent::TRIANGLE_MESH;
				p.mass = 0.0f;
				p.friction = 0.8f;
			}
		}
		if (e.particles)
		{
			const SceneParticles& sp = *e.particles;
			wi::EmittedParticleSystem& em = scene.emitters.Create(entity);
			em.SetMaxParticleCount((uint32_t)std::max(16, std::min(1000000, sp.maxParticles)));
			em.count = sp.rate;
			em.burst_on_create = sp.burstOnCreate;
			em.size = sp.size;
			em.life = sp.life;
			em.random_life = sp.randomLife;
			em.random_factor = sp.randomFactor;
			em.normal_factor = sp.normalFactor;
			em.velocity = F3(sp.velocity);
			em.gravity = F3(sp.gravity);
			em.drag = sp.drag;
			em.restitution = sp.restitution;
			em.mass = sp.mass;
			em.rotation = sp.rotation;
			em.scaleX = sp.scaleX;
			em.scaleY = sp.scaleY;
			em.random_color = sp.randomColor;
			em.framesX = (uint32_t)std::max(1, sp.framesX);
			em.framesY = (uint32_t)std::max(1, sp.framesY);
			em.frameCount = (uint32_t)std::max(1, sp.frameCount);
			em.frameRate = sp.frameRate;
			em.shaderType = sp.shader == "simple" ? wi::EmittedParticleSystem::SIMPLE : sp.shader == "distortion" ? wi::EmittedParticleSystem::SOFT_DISTORTION
				: sp.shader == "lit" ? wi::EmittedParticleSystem::SOFT_LIGHTING : wi::EmittedParticleSystem::SOFT;
			em.SetFrameBlendingEnabled(sp.frameBlending);
			em.SetSorted(sp.sorted);
			em.SetDepthCollisionEnabled(sp.collisions);
			em.SetOpacityCurveControl(sp.opacityPeakStart, sp.opacityPeakEnd);
			em.SetPaused(!sp.playOnStart);
			if (sp.emitFromMesh && hasMesh && scene.meshes.Contains(entity))
				em.meshID = entity;
			if (!scene.materials.Contains(entity))
			{
				MaterialComponent& mat = scene.materials.Create(entity);
				mat.SetBaseColor(XMFLOAT4(sp.color.x, sp.color.y, sp.color.z, sp.color.w));
				mat.userBlendMode = sp.shader == "simple" ? wi::enums::BLENDMODE_ADDITIVE : wi::enums::BLENDMODE_ALPHA;
				if (!sp.texture.empty())
				{
					const std::string abs = ResolveAsset(sp.texture);
					if (fs::Exists(abs))
					{
						mat.textures[MaterialComponent::BASECOLORMAP].name = abs;
						mat.textures[MaterialComponent::BASECOLORMAP].resource = wi::resourcemanager::Load(abs);
					}
					else LUMEN_WARN("scene", "particle texture not found: %s", sp.texture.c_str());
				}
				mat.SetDirty();
			}
		}
		if (e.animator)
		{
			for (Entity clip : rec.animations)
			{
				AnimationComponent* a = scene.animations.GetComponent(clip);
				const NameComponent* n = scene.names.GetComponent(clip);
				if (!a || !n) continue;
				if (const SceneAnimator::Clip* c = e.animator->Find(n->name))
				{
					if (c->pingPong) a->SetPingPong(true);
					else a->SetLooped(c->loop);
					a->speed = c->speed;
					a->amount = c->weight;
				}
			}
		}
		if (e.light)
		{
			const SceneLight& sl = *e.light;
			LightComponent& l = scene.lights.Create(entity);
			l.type = sl.type == "directional" ? LightComponent::DIRECTIONAL : sl.type == "spot" ? LightComponent::SPOT : LightComponent::POINT;
			l.color = F3(sl.color);
			l.intensity = sl.intensity;
			l.range = sl.range;
			l.outerConeAngle = Rad(sl.outerAngle) * 0.5f;
			l.innerConeAngle = Rad(sl.innerAngle) * 0.5f;
			l.SetCastShadow(sl.castShadows);
			l.SetVolumetricsEnabled(sl.volumetric > 0);
			l.volumetric_boost = sl.volumetric;
		}
		if (e.camera)
		{
			CameraComponent& c = scene.cameras.Create(entity);
			c.CreatePerspective(1280, 720, e.camera->nearPlane, e.camera->farPlane, Rad(e.camera->fov));
			if (e.camera->main)
				mainCamera_ = entity;
		}
		if (e.rigidbody && !e.rigidbody->trigger) // triggers are overlap volumes handled by the play host, they have no Jolt body
		{
			const SceneRigidBody& rb = *e.rigidbody;
			RigidBodyPhysicsComponent& p = scene.rigidbodies.Create(entity);
			Float3 ext = rb.shapeSize;
			if (ext == Float3{0, 0, 0})
				ext = hasMesh ? e.mesh->size : Float3{1, 1, 1};
			const bool dynamic = rb.type == "dynamic";
			if (e.vehicle && dynamic)
			{
				// Wheeled vehicle: the chassis box drives the Jolt vehicle constraint.
				const SceneVehicle& v = *e.vehicle;
				p.shape = RigidBodyPhysicsComponent::BOX;
				p.box.halfextents = XMFLOAT3(v.chassis.x * 0.5f, v.chassis.y * 0.5f, v.chassis.z * 0.5f);
				auto& d = p.vehicle;
				d.type = v.type == "motorcycle" ? RigidBodyPhysicsComponent::Vehicle::Type::Motorcycle : RigidBodyPhysicsComponent::Vehicle::Type::Car;
				d.collision_mode = v.collision == "sphere" ? RigidBodyPhysicsComponent::Vehicle::CollisionMode::Sphere
					: v.collision == "cylinder" ? RigidBodyPhysicsComponent::Vehicle::CollisionMode::Cylinder
					: RigidBodyPhysicsComponent::Vehicle::CollisionMode::Ray;
				d.chassis_half_width = v.chassis.x * 0.5f;
				d.chassis_half_height = v.chassis.y * 0.5f;
				d.chassis_half_length = v.chassis.z * 0.5f;
				d.front_wheel_offset = v.frontWheelOffset;
				d.rear_wheel_offset = v.rearWheelOffset;
				d.wheel_radius = v.wheelRadius;
				d.wheel_width = v.wheelWidth;
				d.max_engine_torque = v.engineTorque;
				d.clutch_strength = v.clutchStrength;
				d.max_steering_angle = Rad(v.maxSteerAngle);
				d.max_roll_angle = Rad(v.maxRollAngle);
				for (auto* sus : {&d.front_suspension, &d.rear_suspension})
				{
					sus->min_length = v.suspensionMin;
					sus->max_length = v.suspensionMax;
					sus->frequency = v.suspensionFrequency;
					sus->damping = v.suspensionDamping;
				}
				d.car.four_wheel_drive = v.fourWheelDrive;
				d.motorcycle.lean_control = v.leanControl;
			}
			else if (rb.shape == "sphere") { p.shape = RigidBodyPhysicsComponent::SPHERE; p.sphere.radius = ext.x * 0.5f; }
			else if (rb.shape == "capsule" || rb.shape == "cylinder")
			{
				p.shape = rb.shape == "capsule" ? RigidBodyPhysicsComponent::CAPSULE : RigidBodyPhysicsComponent::CYLINDER;
				p.capsule.radius = ext.x * 0.5f;
				p.capsule.height = rb.shape == "capsule" ? std::max(0.0f, ext.y - ext.x) : ext.y;
			}
			else if (rb.shape == "mesh") p.shape = RigidBodyPhysicsComponent::TRIANGLE_MESH;
			else if (rb.shape == "convex") p.shape = RigidBodyPhysicsComponent::CONVEX_HULL;
			else { p.shape = RigidBodyPhysicsComponent::BOX; p.box.halfextents = XMFLOAT3(ext.x * 0.5f, ext.y * 0.5f, ext.z * 0.5f); }
			p.mass = dynamic ? std::max(0.01f, rb.mass) : 0.0f;
			if (rb.type == "kinematic") p.SetKinematic(true);
			p.friction = rb.friction;
			p.restitution = rb.restitution;
			p.damping_linear = rb.linearDamping;
			p.damping_angular = rb.angularDamping;
			p.buoyancy = rb.buoyancy;
			p.local_offset = F3(rb.shapeOffset);
			p.SetLocked2D(rb.lock2D);
			p.SetStartDeactivated(rb.startDeactivated);
			p.SetDisableDeactivation(rb.alwaysActive);
			p.character.maxSlopeAngle = Rad(rb.maxSlopeAngle);
			p.character.gravityFactor = rb.gravityFactor;
			if (rb.lockRotation && p.shape == RigidBodyPhysicsComponent::CAPSULE && dynamic)
				p.SetCharacterPhysics(true); // capsule character controller (ground state, slopes, steps)
		}
		if (e.audio && !e.audio->file.empty())
		{
			const std::string file = ResolveAsset(e.audio->file);
			if (fs::Exists(file))
			{
				const Entity s = scene.Entity_CreateSound(e.name + " (audio)", file);
				scene.Component_Attach(s, entity, true);
				if (SoundComponent* sc = scene.sounds.GetComponent(s))
				{
					sc->volume = e.audio->volume;
					sc->SetLooped(e.audio->loop);
					sc->SetDisable3D(!e.audio->spatial);
					if (e.audio->playOnStart) sc->Play();
				}
				rec.extra.push_back(s);
			}
		}
	}

	Entity Bridge::Create(Scene& scene, const SceneEntity& e, Record& rec)
	{
		const Entity entity = scene.Entity_CreateTransform(e.name);
		rec.entity = entity;
		SetTransform(*scene.transforms.GetComponent(entity), e.transform);
		scene.layers.Create(entity).layerMask = 1u << std::min(e.layer, 31);
		Populate(scene, entity, e, rec);
		rec.structHash = StructHash(e);
		rec.transformHash = TransformHash(e);
		return entity;
	}

	std::vector<Entity> Bridge::Instantiate(Scene& scene, const std::vector<SceneEntity>& ents)
	{
		std::vector<Entity> out;
		for (const SceneEntity& e : ents)
		{
			Record rec;
			const Entity ent = Create(scene, e, rec);
			rec.parent.clear();
			byEntity_[ent] = e.id;
			records_[e.id] = rec;
			out.push_back(ent);
		}
		for (const SceneEntity& e : ents)
		{
			Record& rec = records_[e.id];
			if (!e.parent.empty())
			{
				auto p = records_.find(e.parent);
				if (p != records_.end())
					scene.Component_Attach(rec.entity, p->second.entity, true);
			}
			SetTransform(*scene.transforms.GetComponent(rec.entity), e.transform);
			rec.parent = e.parent;
		}
		SceneDoc fragment;
		fragment.entities = ents;
		SyncJoints(scene, fragment, false);
		return out;
	}

	std::vector<Entity> Bridge::Animations(const std::string& id) const
	{
		auto it = records_.find(id);
		return it == records_.end() ? std::vector<Entity>{} : it->second.animations;
	}

	void Bridge::Clear(Scene& scene)
	{
		for (auto& kv : records_)
			scene.Entity_Remove(kv.second.entity, false);
		if (environment_ != INVALID_ENTITY) scene.Entity_Remove(environment_);
		if (sun_ != INVALID_ENTITY) scene.Entity_Remove(sun_);
		records_.clear();
		byEntity_.clear();
		environment_ = sun_ = mainCamera_ = INVALID_ENTITY;
		envHash_ = 0;
	}

	void Bridge::Build(Scene& scene, const SceneDoc& doc, const std::string& projectRoot)
	{
		Clear(scene);
		root_ = projectRoot;
		Sync(scene, doc);
	}

	void Bridge::Sync(Scene& scene, const SceneDoc& doc)
	{
		std::unordered_map<std::string, bool> alive;
		bool bodiesChanged = false;
		for (const SceneEntity& e : doc.entities)
		{
			alive[e.id] = true;
			auto it = records_.find(e.id);
			if (it == records_.end())
			{
				Record rec;
				const Entity ent = Create(scene, e, rec);
				if (e.rigidbody) bodiesChanged = true;
				rec.parent.clear();
				byEntity_[ent] = e.id;
				records_[e.id] = rec;
			}
			else
			{
				Record& rec = it->second;
				const uint64_t sh = StructHash(e);
				if (sh != rec.structHash)
				{
					if (NameComponent* n = scene.names.GetComponent(rec.entity)) n->name = e.name;
					scene.layers.Create(rec.entity).layerMask = 1u << std::min(e.layer, 31);
					Populate(scene, rec.entity, e, rec);
					rec.structHash = sh;
					bodiesChanged = true;
				}
				const uint64_t th = TransformHash(e);
				if (th != rec.transformHash)
				{
					SetTransform(*scene.transforms.GetComponent(rec.entity), e.transform);
					rec.transformHash = th;
				}
			}
		}
		// Hierarchy (second pass: every entity exists now).
		for (const SceneEntity& e : doc.entities)
		{
			Record& rec = records_[e.id];
			if (rec.parent != e.parent || (!e.parent.empty() && !scene.hierarchy.Contains(rec.entity)))
			{
				if (scene.hierarchy.Contains(rec.entity))
					scene.Component_Detach(rec.entity);
				if (!e.parent.empty())
				{
					auto p = records_.find(e.parent);
					if (p != records_.end())
						scene.Component_Attach(rec.entity, p->second.entity, true);
				}
				SetTransform(*scene.transforms.GetComponent(rec.entity), e.transform);
				rec.parent = e.parent;
			}
		}
		for (auto it = records_.begin(); it != records_.end();)
		{
			if (!alive.count(it->first))
			{
				StripComponents(scene, it->second.entity, it->second);
				byEntity_.erase(it->second.entity);
				if (mainCamera_ == it->second.entity) mainCamera_ = INVALID_ENTITY;
				scene.Entity_Remove(it->second.entity, false);
				it = records_.erase(it);
				bodiesChanged = true;
			}
			else
				++it;
		}
		SyncJoints(scene, doc, bodiesChanged);
		const uint64_t eh = str::Hash64(doc.EnvironmentToJson().Dump(0));
		if (eh != envHash_)
		{
			ApplyEnvironment(scene, doc.environment);
			envHash_ = eh;
		}
	}

	// Constraints and vehicle wheel links need every entity to exist, so they are built after the main pass.
	void Bridge::SyncJoints(Scene& scene, const SceneDoc& doc, bool bodiesChanged)
	{
		using CT = PhysicsConstraintComponent::Type;
		auto entityOf = [&](const std::string& id) -> Entity {
			if (id.empty()) return INVALID_ENTITY;
			auto it = records_.find(id);
			return it == records_.end() ? INVALID_ENTITY : it->second.entity;
		};
		for (const SceneEntity& e : doc.entities)
		{
			auto it = records_.find(e.id);
			if (it == records_.end()) continue;
			Record& rec = it->second;
			if (e.vehicle)
				if (RigidBodyPhysicsComponent* b = scene.rigidbodies.GetComponent(rec.entity))
				{
					b->vehicle.wheel_entity_front_left = entityOf(e.vehicle->wheelFL);
					b->vehicle.wheel_entity_front_right = entityOf(e.vehicle->wheelFR);
					b->vehicle.wheel_entity_rear_left = entityOf(e.vehicle->wheelRL);
					b->vehicle.wheel_entity_rear_right = entityOf(e.vehicle->wheelRR);
				}
			if (!rec.jointDirty && !(bodiesChanged && e.joint))
				continue;
			scene.constraints.Remove(rec.entity);
			rec.jointDirty = false;
			if (!e.joint || !scene.rigidbodies.Contains(rec.entity))
				continue;
			const SceneJoint& j = *e.joint;
			PhysicsConstraintComponent& c = scene.constraints.Create(rec.entity);
			c.bodyA = rec.entity;
			c.bodyB = entityOf(j.connected);
			if (j.type == "point") c.type = CT::Point;
			else if (j.type == "distance") c.type = CT::Distance;
			else if (j.type == "hinge") c.type = CT::Hinge;
			else if (j.type == "cone") c.type = CT::Cone;
			else if (j.type == "sixdof") c.type = CT::SixDOF;
			else if (j.type == "swingtwist") c.type = CT::SwingTwist;
			else if (j.type == "slider") c.type = CT::Slider;
			else c.type = CT::Fixed;
			c.distance_constraint.min_distance = j.minDistance;
			c.distance_constraint.max_distance = std::max(j.minDistance, j.maxDistance);
			c.hinge_constraint.min_angle = Rad(j.minAngle);
			c.hinge_constraint.max_angle = Rad(j.maxAngle);
			c.hinge_constraint.target_angular_velocity = j.motorSpeed;
			c.cone_constraint.half_cone_angle = Rad(j.coneAngle);
			c.swing_twist.normal_half_cone_angle = Rad(j.swingNormal);
			c.swing_twist.plane_half_cone_angle = Rad(j.swingPlane);
			c.swing_twist.min_twist_angle = Rad(j.twistMin);
			c.swing_twist.max_twist_angle = Rad(j.twistMax);
			c.slider_constraint.min_limit = j.sliderMin;
			c.slider_constraint.max_limit = j.sliderMax;
			c.slider_constraint.target_velocity = j.sliderSpeed;
			c.slider_constraint.max_force = j.sliderForce;
			if (j.lockX) c.six_dof.SetFixedX();
			if (j.lockY) c.six_dof.SetFixedY();
			if (j.lockZ) c.six_dof.SetFixedZ();
			if (j.lockRotX) c.six_dof.SetFixedRotationX();
			if (j.lockRotY) c.six_dof.SetFixedRotationY();
			if (j.lockRotZ) c.six_dof.SetFixedRotationZ();
			c.break_distance = j.breakDistance > 0 ? j.breakDistance : FLT_MAX;
			if (j.disableSelfCollision) c._flags |= PhysicsConstraintComponent::DISABLE_SELF_COLLISION;
		}
	}

	void Bridge::ApplyEnvironment(Scene& scene, const SceneEnvironment& env)
	{
		if (environment_ == INVALID_ENTITY)
		{
			environment_ = CreateEntity();
			scene.names.Create(environment_) = "Environment";
			scene.weathers.Create(environment_);
		}
		if (sun_ == INVALID_ENTITY)
		{
			sun_ = scene.Entity_CreateLight("Sun", XMFLOAT3(0, 0, 0), XMFLOAT3(1, 1, 1), 1, 100, LightComponent::DIRECTIONAL);
		}
		// Sun direction from azimuth / elevation (degrees); +Y up, azimuth 0 = +Z.
		const float az = Rad(env.sunAzimuth), el = Rad(env.sunElevation);
		TransformComponent& st = *scene.transforms.GetComponent(sun_);
		// light shines along -toSun; rotate so the forward axis (+Z) points away from the sun.
		XMStoreFloat4(&st.rotation_local, XMQuaternionRotationRollPitchYaw(el, az + kPi, 0));
		st.SetDirty();
		LightComponent& sun = *scene.lights.GetComponent(sun_);
		sun.color = F3(env.sunColor);
		sun.intensity = env.sunIntensity;
		sun.SetCastShadow(env.sunShadows);
		sun.SetVolumetricsEnabled(true);

		WeatherComponent& w = *scene.weathers.GetComponent(environment_);
		w.skyExposure = env.skyExposure;
		w.ambient = F3(env.ambientColor);
		if (env.skyMode == "procedural")
		{
			w.SetRealisticSky(true);
			w.SetRealisticSkyAerialPerspective(true);
			// Softness: wider sun glow (lower Mie anisotropy), more haze, desaturated Rayleigh blue, brighter ground bounce,
			// finer ray march (less banding) and gentler clouds. 0 keeps Wicked's physically crisp defaults.
			{
				const float s = std::clamp(env.skySoftness, 0.0f, 1.0f);
				AtmosphereParameters a;
				a.miePhaseG = 0.82f + (0.58f - 0.82f) * s;
				const float k = 1.0f + 0.9f * s;
				a.mieScattering = XMFLOAT3(a.mieScattering.x * k, a.mieScattering.y * k, a.mieScattering.z * k);
				a.mieExtinction = XMFLOAT3(a.mieExtinction.x * k, a.mieExtinction.y * k, a.mieExtinction.z * k);
				a.mieAbsorption = XMFLOAT3(a.mieExtinction.x - a.mieScattering.x, a.mieExtinction.y - a.mieScattering.y, a.mieExtinction.z - a.mieScattering.z);
				const float mean = (a.rayleighScattering.x + a.rayleighScattering.y + a.rayleighScattering.z) / 3.0f;
				const float d = 0.3f * s;
				a.rayleighScattering = XMFLOAT3(a.rayleighScattering.x + (mean - a.rayleighScattering.x) * d, a.rayleighScattering.y + (mean - a.rayleighScattering.y) * d,
					a.rayleighScattering.z + (mean - a.rayleighScattering.z) * d);
				const float g = 0.3f + 0.12f * s;
				a.groundAlbedo = XMFLOAT3(g, g, g);
				a.aerialPerspectiveScale = std::max(0.0f, env.haze);
				a.rayMarchMinMaxSPP = XMFLOAT2(8.0f, 28.0f);
				w.atmosphereParameters = a;
				VolumetricCloudParameters c;
				c.phaseG = 0.5f - 0.2f * s;
				c.phaseG2 = -0.5f + 0.2f * s;
				c.ambientGroundMultiplier = 0.75f + 0.2f * s;
				c.beerPowder = 20.0f - 10.0f * s;
				w.volumetricCloudParameters = c;
				w.stars = 0.5f;
			}
			w.SetVolumetricClouds(env.cloudCoverage > 0.01f);
			w.SetVolumetricCloudsCastShadow(true);
			w.volumetricCloudParameters.layerFirst.coverageAmount = env.cloudCoverage;
			w.skyMapName.clear();
			w.skyMap = {};
		}
		else
		{
			w.SetRealisticSky(false);
			w.SetVolumetricClouds(false);
			// Gradient sky: a pale, slightly warm horizon fading into the chosen colour, deeper overhead.
			const Float3 c = env.skyColor;
			w.horizon = XMFLOAT3(std::min(1.0f, c.x * 0.55f + 0.42f), std::min(1.0f, c.y * 0.55f + 0.42f), std::min(1.0f, c.z * 0.55f + 0.40f));
			w.zenith = XMFLOAT3(c.x * 0.45f, c.y * 0.62f, c.z * 0.92f);
			if (env.skyMode == "cubemap" && !env.skyCubemap.empty())
			{
				w.skyMapName = ResolveAsset(env.skyCubemap);
				w.skyMap = wi::resourcemanager::Load(w.skyMapName);
			}
		}
		w.fogStart = 0;
		w.fogDensity = env.fogDensity;
		w.SetHeightFog(env.fogHeightFalloff > 0);
		w.fogHeightStart = env.fogHeight;
		w.fogHeightEnd = env.fogHeight + 1.0f / std::max(0.001f, env.fogHeightFalloff);
		if (env.fogDensity > 0 && env.skyMode != "procedural")
		{
			w.SetOverrideFogColor(true);
			w.horizon = F3(env.fogColor);
		}
		const float wd = Rad(env.windDirection);
		w.windDirection = XMFLOAT3(sinf(wd) * env.windSpeed * 0.1f, 0, cosf(wd) * env.windSpeed * 0.1f);
		w.windSpeed = env.windSpeed;
		w.SetOceanEnabled(env.water);
		w.oceanParameters.waterHeight = env.waterHeight;
		w.oceanParameters.waterColor = XMFLOAT4(env.waterColor.x, env.waterColor.y, env.waterColor.z, 0.6f);
		w.oceanParameters.wave_amplitude = std::max(1.0f, env.waterWaveAmplitude * 1000.0f);
	}

	bool Bridge::Bounds(const Scene& scene, const SceneDoc& doc, const std::string& id, wi::primitive::AABB& out) const
	{
		bool any = false;
		std::vector<std::string> stack = {id};
		while (!stack.empty())
		{
			const std::string cur = stack.back();
			stack.pop_back();
			auto it = records_.find(cur);
			if (it == records_.end())
				continue;
			std::vector<Entity> ents = {it->second.entity};
			for (Entity x : it->second.extra) ents.push_back(x);
			for (size_t k = 0; k < ents.size(); ++k)
			{
				const Entity ent = ents[k];
				const size_t objectIndex = scene.objects.GetIndex(ent);
				if (objectIndex < scene.aabb_objects.size())
				{
					const wi::primitive::AABB& bb = scene.aabb_objects[objectIndex];
					out = any ? wi::primitive::AABB::Merge(out, bb) : bb;
					any = true;
				}
				// imported model hierarchy
				for (size_t i = 0; i < scene.hierarchy.GetCount(); ++i)
					if (scene.hierarchy[i].parentID == ent)
						ents.push_back(scene.hierarchy.GetEntity(i));
			}
			for (const SceneEntity* c : doc.Children(cur))
				stack.push_back(c->id);
		}
		return any;
	}
}

namespace lumen
{
	Float3 EulerDegFromQuat(const XMFLOAT4& q)
	{
		XMFLOAT4X4 r;
		XMStoreFloat4x4(&r, XMMatrixRotationQuaternion(XMLoadFloat4(&q)));
		const float pitch = asinf(std::max(-1.0f, std::min(1.0f, -r._32)));
		float yaw, roll;
		if (fabsf(r._32) < 0.9999f)
		{
			yaw = atan2f(r._31, r._33);
			roll = atan2f(r._12, r._22);
		}
		else
		{
			yaw = atan2f(-r._13, r._11);
			roll = 0;
		}
		const float d = 180.0f / 3.14159265358979f;
		return {pitch * d, yaw * d, roll * d};
	}

	SceneTransform Bridge::LocalFromWorld(const Scene& scene, const std::string& parentId, const XMFLOAT4X4& world) const
	{
		XMMATRIX m = XMLoadFloat4x4(&world);
		if (!parentId.empty())
		{
			const Entity p = Find(parentId);
			if (const TransformComponent* pt = scene.transforms.GetComponent(p))
				m = XMMatrixMultiply(m, XMMatrixInverse(nullptr, XMLoadFloat4x4(&pt->world)));
		}
		XMVECTOR s, r, t;
		XMMatrixDecompose(&s, &r, &t, m);
		SceneTransform out;
		XMFLOAT3 pos, scl;
		XMFLOAT4 q;
		XMStoreFloat3(&pos, t);
		XMStoreFloat3(&scl, s);
		XMStoreFloat4(&q, r);
		out.position = {pos.x, pos.y, pos.z};
		out.scale = {scl.x, scl.y, scl.z};
		out.rotation = EulerDegFromQuat(q);
		return out;
	}
}
