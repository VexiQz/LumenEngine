#include "ClipSystem.h"

#include <algorithm>
#include <cmath>

using namespace wi::scene;
using namespace wi::ecs;

namespace lumen
{
	namespace
	{
		constexpr float kDeg = 0.017453292519943295f;

		void Apply(Scene& scene, Entity e, const std::string& path, const Float3& v, float weight)
		{
			TransformComponent* t = scene.transforms.GetComponent(e);
			if (!t)
				return;
			if (path == "position")
			{
				const XMFLOAT3 target(v.x, v.y, v.z);
				if (weight >= 0.999f) t->translation_local = target;
				else XMStoreFloat3(&t->translation_local, XMVectorLerp(XMLoadFloat3(&t->translation_local), XMLoadFloat3(&target), weight));
			}
			else if (path == "scale")
			{
				const XMFLOAT3 target(v.x, v.y, v.z);
				if (weight >= 0.999f) t->scale_local = target;
				else XMStoreFloat3(&t->scale_local, XMVectorLerp(XMLoadFloat3(&t->scale_local), XMLoadFloat3(&target), weight));
			}
			else if (path == "rotation")
			{
				XMFLOAT4 q;
				XMStoreFloat4(&q, XMQuaternionRotationRollPitchYaw(v.x * kDeg, v.y * kDeg, v.z * kDeg));
				if (weight >= 0.999f) t->rotation_local = q;
				else XMStoreFloat4(&t->rotation_local, XMQuaternionSlerp(XMLoadFloat4(&t->rotation_local), XMLoadFloat4(&q), weight));
			}
			t->SetDirty();
		}
	}

	Entity ClipSystem::Resolve(Scene& scene, Entity root, const std::string& path)
	{
		Entity cur = root;
		size_t pos = 0;
		while (cur != INVALID_ENTITY && pos < path.size())
		{
			size_t e = path.find('/', pos);
			if (e == std::string::npos) e = path.size();
			const std::string seg = path.substr(pos, e - pos);
			pos = e + 1;
			if (seg.empty()) continue;
			Entity found = INVALID_ENTITY;
			for (size_t i = 0; i < scene.hierarchy.GetCount() && found == INVALID_ENTITY; ++i)
			{
				if (scene.hierarchy[i].parentID != cur) continue;
				const Entity c = scene.hierarchy.GetEntity(i);
				const NameComponent* n = scene.names.GetComponent(c);
				if (n && n->name == seg) found = c;
			}
			cur = found;
		}
		return cur;
	}

	void ClipSystem::Pose(Scene& scene, Entity root, const anim::Clip& clip, float time, float weight)
	{
		for (const anim::Track& tr : clip.tracks)
		{
			const Entity e = tr.target.empty() ? root : Resolve(scene, root, tr.target);
			if (e == INVALID_ENTITY || tr.keys.empty())
				continue;
			Apply(scene, e, tr.path, anim::Sample(tr, time), weight);
		}
	}

	int ClipSystem::Play(Scene& scene, Entity root, const anim::Clip& clip, const std::string& path, bool loop, float speed, float weight)
	{
		auto in = std::make_unique<Instance>();
		in->id = nextId_++;
		in->path = path;
		in->clip = clip;
		in->root = root;
		in->loop = loop;
		in->speed = speed;
		in->weight = weight;
		for (const anim::Track& tr : clip.tracks)
			in->targets.push_back(tr.target.empty() ? root : Resolve(scene, root, tr.target));
		const int id = in->id;
		// a new clip replaces another instance on the same root that animates the same properties
		instances_.erase(std::remove_if(instances_.begin(), instances_.end(), [&](const std::unique_ptr<Instance>& o) { return o->root == root && o->path == path; }), instances_.end());
		instances_.push_back(std::move(in));
		return id;
	}

	ClipSystem::Instance* ClipSystem::Find(int id)
	{
		for (auto& i : instances_)
			if (i->id == id) return i.get();
		return nullptr;
	}

	void ClipSystem::Stop(int id)
	{
		instances_.erase(std::remove_if(instances_.begin(), instances_.end(), [&](const std::unique_ptr<Instance>& o) { return o->id == id; }), instances_.end());
	}

	void ClipSystem::StopAll() { instances_.clear(); }

	void ClipSystem::StopOn(Entity root)
	{
		instances_.erase(std::remove_if(instances_.begin(), instances_.end(), [&](const std::unique_ptr<Instance>& o) { return o->root == root; }), instances_.end());
	}

	void ClipSystem::Update(Scene& scene, float dt, const Callbacks& cb)
	{
		for (size_t idx = 0; idx < instances_.size();)
		{
			Instance& in = *instances_[idx];
			if (!scene.transforms.Contains(in.root))
			{
				instances_.erase(instances_.begin() + (std::ptrdiff_t)idx);
				continue;
			}
			if (in.playing)
			{
				const float dur = std::max(0.001f, in.clip.duration);
				const float prev = in.time;
				in.time += dt * in.speed;
				bool end = false;
				if (in.speed >= 0)
				{
					if (in.time >= dur)
					{
						if (in.loop) in.time = std::fmod(in.time, dur);
						else { in.time = dur; end = true; }
					}
				}
				else if (in.time <= 0)
				{
					if (in.loop) in.time = dur + std::fmod(in.time, dur);
					else { in.time = 0; end = true; }
				}
				if (cb.onEvent && in.speed > 0)
				{
					const float to = end ? dur : in.time;
					for (const anim::Event* e : anim::EventsBetween(in.clip, prev, to))
						cb.onEvent(in, e->name);
				}
				for (size_t k = 0; k < in.clip.tracks.size() && k < in.targets.size(); ++k)
				{
					const anim::Track& tr = in.clip.tracks[k];
					if (in.targets[k] == INVALID_ENTITY || tr.keys.empty()) continue;
					Apply(scene, in.targets[k], tr.path, anim::Sample(tr, in.time), in.weight);
				}
				if (end)
				{
					in.playing = false;
					in.ended = true;
					if (cb.onEnd) cb.onEnd(in);
				}
			}
			if (in.ended && in.removeWhenDone)
			{
				instances_.erase(instances_.begin() + (std::ptrdiff_t)idx);
				continue;
			}
			++idx;
		}
	}
}
