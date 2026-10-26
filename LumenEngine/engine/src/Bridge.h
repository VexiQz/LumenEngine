// Lumen Engine — builds and incrementally synchronises a Wicked scene from a SceneDoc.
#pragma once
#include "lumen/core/SceneDoc.h"
#include "lumen/core/Terrain.h"
#include "WickedEngine.h"
#include <unordered_map>

namespace lumen
{
	class Bridge
	{
	public:
		// Removes everything this bridge created from the scene.
		void Clear(wi::scene::Scene& scene);
		// Clear + create all entities and the environment.
		void Build(wi::scene::Scene& scene, const SceneDoc& doc, const std::string& projectRoot);
		// Applies differences between the document and the live scene (edit mode).
		void Sync(wi::scene::Scene& scene, const SceneDoc& doc);

		// Creates entities of a document fragment at runtime (prefab instances); parents must be inside the fragment.
		std::vector<wi::ecs::Entity> Instantiate(wi::scene::Scene& scene, const std::vector<SceneEntity>& entities);
		wi::ecs::Entity Find(const std::string& id) const;
		// Animation clips (Wicked animation entities) that came with the entity's model, in file order.
		std::vector<wi::ecs::Entity> Animations(const std::string& id) const;
		std::string IdOf(wi::ecs::Entity e) const;
		// Walks up the Wicked hierarchy until an entity that belongs to the document is found.
		std::string IdOfOrAncestor(const wi::scene::Scene& scene, wi::ecs::Entity e) const;
		// World space bounds of an entity and its descendants; false when it has nothing renderable.
		bool Bounds(const wi::scene::Scene& scene, const SceneDoc& doc, const std::string& id, wi::primitive::AABB& out) const;
		// Converts a world matrix into a transform relative to the Wicked entity of `parentId` ("" = world).
		SceneTransform LocalFromWorld(const wi::scene::Scene& scene, const std::string& parentId, const XMFLOAT4X4& world) const;
		const std::string& ProjectRoot() const { return root_; }
		// Rewrites the vertex buffers of an existing terrain entity (live sculpting); the collider is refreshed by Invalidate().
		bool UpdateTerrainMesh(wi::scene::Scene& scene, const std::string& id, const SceneTerrain& t, const terrain::Heightfield& hf);
		// Forces the entity to be rebuilt on the next Sync().
		void Invalidate(const std::string& id);
		wi::ecs::Entity MainCamera() const { return mainCamera_; }
		std::string ResolveAsset(const std::string& path) const;

	private:
		struct Record
		{
			wi::ecs::Entity entity = wi::ecs::INVALID_ENTITY;
			uint64_t structHash = 0;
			uint64_t transformHash = 0;
			std::string parent;
			bool jointDirty = true;
			std::vector<wi::ecs::Entity> animations; // clips imported with the model of this entity
			std::vector<wi::ecs::Entity> extra; // helper entities (imported model roots, sounds)
		};
		wi::ecs::Entity Create(wi::scene::Scene& scene, const SceneEntity& e, Record& rec);
		void Populate(wi::scene::Scene& scene, wi::ecs::Entity entity, const SceneEntity& e, Record& rec);
		void StripComponents(wi::scene::Scene& scene, wi::ecs::Entity entity, Record& rec);
		void SyncJoints(wi::scene::Scene& scene, const SceneDoc& doc, bool bodiesChanged);
		void ApplyEnvironment(wi::scene::Scene& scene, const SceneEnvironment& env);

		std::unordered_map<std::string, Record> records_;
		std::unordered_map<wi::ecs::Entity, std::string> byEntity_;
		wi::ecs::Entity environment_ = wi::ecs::INVALID_ENTITY;
		wi::ecs::Entity sun_ = wi::ecs::INVALID_ENTITY;
		wi::ecs::Entity mainCamera_ = wi::ecs::INVALID_ENTITY;
		uint64_t envHash_ = 0;
		std::string root_;
	};

	Float3 EulerDegFromQuat(const XMFLOAT4& q);

	// Primitive mesh generators (positions, normals, UVs, tangents, indices) shared with tools.
	void BuildPrimitiveMesh(wi::scene::MeshComponent& mesh, const std::string& primitive, const Float3& size, int segments);
}
