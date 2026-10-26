// Lumen Engine — plays `.lanim` keyframe clips on Wicked entities (editor preview and game runtime).
#pragma once
#include "WickedEngine.h"
#include "lumen/core/Animation.h"

#include <functional>
#include <memory>

namespace lumen
{
	class ClipSystem
	{
	public:
		struct Instance
		{
			int id = 0;
			std::string path;               // project relative asset path (for the cache / debugging)
			anim::Clip clip;
			wi::ecs::Entity root = wi::ecs::INVALID_ENTITY;
			std::vector<wi::ecs::Entity> targets; // per track, resolved once
			float time = 0;
			float speed = 1;
			float weight = 1;
			bool playing = true;
			bool loop = false;
			bool removeWhenDone = true;
			bool ended = false;
		};
		struct Callbacks
		{
			std::function<void(const Instance&, const std::string& eventName)> onEvent;
			std::function<void(const Instance&)> onEnd;
		};

		// Resolves child paths by entity names below `root`. INVALID_ENTITY when a segment is missing.
		static wi::ecs::Entity Resolve(wi::scene::Scene& scene, wi::ecs::Entity root, const std::string& path);
		// Poses the targets of `clip` at `time` (weight 1 = replace, <1 blends with the current pose).
		static void Pose(wi::scene::Scene& scene, wi::ecs::Entity root, const anim::Clip& clip, float time, float weight = 1.0f);

		int Play(wi::scene::Scene& scene, wi::ecs::Entity root, const anim::Clip& clip, const std::string& path, bool loop, float speed, float weight);
		Instance* Find(int id);
		void Stop(int id);
		void StopAll();
		void StopOn(wi::ecs::Entity root);
		void Update(wi::scene::Scene& scene, float dt, const Callbacks& cb);
		size_t Count() const { return instances_.size(); }

	private:
		std::vector<std::unique_ptr<Instance>> instances_;
		int nextId_ = 1;
	};
}
