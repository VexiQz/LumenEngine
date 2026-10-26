// Lumen Engine — runtime navigation: bakes the scene's static geometry into a nav::NavMesh and drives nav agents.
#pragma once
#include "Bridge.h"
#include "lumen/core/Navigation.h"

#include <functional>
#include <unordered_map>

namespace lumen
{
	class NavSystem
	{
	public:
		struct Agent
		{
			wi::ecs::Entity entity = wi::ecs::INVALID_ENTITY;
			SceneNavAgent cfg;
			std::vector<Float3> path;
			size_t next = 0;           // index of the waypoint being approached
			Float3 goal;
			Float3 velocity;
			float groundOffset = 0.0f; // entity origin above the surface
			float speedOverride = -1.0f;
			bool moving = false;
			bool partial = false;
		};
		struct Hooks
		{
			std::function<void(wi::ecs::Entity, const XMFLOAT3&)> setPosition;
			std::function<void(wi::ecs::Entity, float yawRadians)> setYaw;
			std::function<void(wi::ecs::Entity)> arrived;
			std::function<void(wi::ecs::Entity)> failed;
		};

		void Reset();
		// Bakes the first navmesh entity of the document. Returns false when there is none or nothing walkable.
		bool Bake(wi::scene::Scene& scene, const Bridge& bridge, const SceneDoc& doc);
		bool Baked() const { return mesh_.Built(); }
		nav::NavMesh& Mesh() { return mesh_; }
		const nav::NavMesh& Mesh() const { return mesh_; }

		void AddAgent(wi::scene::Scene& scene, wi::ecs::Entity e, const SceneNavAgent& cfg);
		void RemoveAgent(wi::ecs::Entity e);
		Agent* Find(wi::ecs::Entity e);
		bool MoveTo(wi::scene::Scene& scene, wi::ecs::Entity e, const Float3& destination);
		void Stop(wi::ecs::Entity e);
		void Update(wi::scene::Scene& scene, float dt, const Hooks& hooks);
		float Remaining(const Agent& a) const;

		// Debug overlay (walkable borders, agent paths) through Wicked's debug line renderer.
		void DrawDebug(wi::scene::Scene& scene, bool paths, bool mesh);
		std::mt19937& Rng() { return rng_; }

	private:
		nav::NavMesh mesh_;
		std::vector<Agent> agents_;
		std::mt19937 rng_{12345};
		std::vector<Float3> borderCache_;
		bool borderDirty_ = true;
	};
}
