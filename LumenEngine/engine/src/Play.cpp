#include "Play.h"

#include "lumen/core/Fs.h"
#include "lumen/core/Animation.h"
#include "lumen/core/Platform.h"
#include "lumen/core/Log.h"
#include "lumen/core/LuaLibs.h"
#include "lumen/core/Runtime.h"
#include "lumen/core/Strings.h"
#include "LuaVec.h"

extern "C"
{
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

using namespace wi::scene;
using namespace wi::ecs;

namespace lumen
{
	namespace
	{
		PlayHost* g_host = nullptr;
		constexpr const char* kEntityMeta = "lumen.entity";
		constexpr const char* kLightMeta = "lumen.light";
		constexpr float kPi = 3.14159265358979f;

		struct EntityHandle { Entity e; };
		struct LightHandle { Entity e; };

		Scene& S() { return *g_host->Context().scene; }

		EntityHandle* ToEntity(lua_State* L, int idx) { return (EntityHandle*)luaL_checkudata(L, idx, kEntityMeta); }
		Entity CheckAlive(lua_State* L, int idx)
		{
			const Entity e = ToEntity(L, idx)->e;
			if (!S().transforms.Contains(e))
				luaL_error(L, "entity no longer exists");
			return e;
		}
		void PushEntity(lua_State* L, Entity e)
		{
			if (e == INVALID_ENTITY) { lua_pushnil(L); return; }
			EntityHandle* h = (EntityHandle*)lua_newuserdatauv(L, sizeof(EntityHandle), 0);
			h->e = e;
			luaL_setmetatable(L, kEntityMeta);
		}
		void V3(lua_State* L, const XMFLOAT3& v) { lua::detail::PushVec3(L, v.x, v.y, v.z); }
		XMFLOAT3 GetV3(lua_State* L, int idx)
		{
			double o[4];
			lua::detail::CheckVec(L, idx, 3, o);
			return XMFLOAT3((float)o[0], (float)o[1], (float)o[2]);
		}
		XMFLOAT3 EulerDeg(const XMFLOAT4& q)
		{
			// inverse of XMQuaternionRotationRollPitchYaw(pitch, yaw, roll)
			const XMMATRIX m = XMMatrixRotationQuaternion(XMLoadFloat4(&q));
			XMFLOAT4X4 r;
			XMStoreFloat4x4(&r, m);
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
			const float d = 180.0f / kPi;
			return XMFLOAT3(pitch * d, yaw * d, roll * d);
		}
		XMFLOAT4 QuatFromDeg(const XMFLOAT3& e)
		{
			XMFLOAT4 q;
			const float r = kPi / 180.0f;
			XMStoreFloat4(&q, XMQuaternionRotationRollPitchYaw(e.x * r, e.y * r, e.z * r));
			return q;
		}
		TransformComponent& T(Entity e) { return *S().transforms.GetComponent(e); }

		RigidBodyPhysicsComponent* Body(Entity e) { return S().rigidbodies.GetComponent(e); }

		// world <-> local helpers for entities with a parent
		void SetWorldPosition(Entity e, const XMFLOAT3& p)
		{
			TransformComponent& t = T(e);
			if (const HierarchyComponent* h = S().hierarchy.GetComponent(e))
			{
				const TransformComponent* pt = S().transforms.GetComponent(h->parentID);
				if (pt)
				{
					XMVECTOR det;
					const XMMATRIX inv = XMMatrixInverse(&det, XMLoadFloat4x4(&pt->world));
					XMStoreFloat3(&t.translation_local, XMVector3Transform(XMLoadFloat3(&p), inv));
					t.SetDirty();
					return;
				}
			}
			t.translation_local = p;
			t.SetDirty();
			if (RigidBodyPhysicsComponent* b = Body(e))
				wi::physics::SetPosition(*b, p);
		}
		void SetWorldRotation(Entity e, const XMFLOAT4& q)
		{
			TransformComponent& t = T(e);
			XMFLOAT4 local = q;
			if (const HierarchyComponent* h = S().hierarchy.GetComponent(e))
			{
				if (const TransformComponent* pt = S().transforms.GetComponent(h->parentID))
				{
					XMVECTOR s, r, tr;
					XMMatrixDecompose(&s, &r, &tr, XMLoadFloat4x4(&pt->world));
					XMStoreFloat4(&local, XMQuaternionMultiply(XMLoadFloat4(&q), XMQuaternionInverse(r)));
				}
			}
			t.rotation_local = local;
			t.SetDirty();
			if (RigidBodyPhysicsComponent* b = Body(e))
				if (!b->IsCharacterPhysics())
					wi::physics::SetPositionAndRotation(*b, t.GetPosition(), q);
		}
		XMFLOAT3 WorldPos(Entity e) { return T(e).GetPosition(); }
		XMFLOAT4 WorldRot(Entity e) { return T(e).GetRotation(); }
		XMFLOAT3 Axis(Entity e, float x, float y, float z)
		{
			XMFLOAT3 o;
			const XMFLOAT4 wr = WorldRot(e);
			XMStoreFloat3(&o, XMVector3Normalize(XMVector3Rotate(XMVectorSet(x, y, z, 0), XMLoadFloat4(&wr))));
			return o;
		}

		int En_name(lua_State* L)
		{
			const Entity e = CheckAlive(L, 1);
			const NameComponent* n = S().names.GetComponent(e);
			lua_pushstring(L, n ? n->name.c_str() : "");
			return 1;
		}
		int En_id(lua_State* L)
		{
			const Entity e = ToEntity(L, 1)->e;
			const std::string id = g_host->Context().bridge->IdOf(e);
			lua_pushstring(L, id.c_str());
			return 1;
		}
		int En_valid(lua_State* L)
		{
			lua_pushboolean(L, S().transforms.Contains(ToEntity(L, 1)->e));
			return 1;
		}
		int En_position(lua_State* L) { V3(L, WorldPos(CheckAlive(L, 1))); return 1; }
		int En_set_position(lua_State* L)
		{
			const Entity e = CheckAlive(L, 1);
			SetWorldPosition(e, GetV3(L, 2));
			return 0;
		}
		int En_local_position(lua_State* L) { V3(L, T(CheckAlive(L, 1)).translation_local); return 1; }
		int En_set_local_position(lua_State* L)
		{
			TransformComponent& t = T(CheckAlive(L, 1));
			t.translation_local = GetV3(L, 2);
			t.SetDirty();
			return 0;
		}
		int En_translate(lua_State* L)
		{
			const Entity e = CheckAlive(L, 1);
			const XMFLOAT3 d = GetV3(L, 2), p = WorldPos(e);
			SetWorldPosition(e, XMFLOAT3(p.x + d.x, p.y + d.y, p.z + d.z));
			return 0;
		}
		int En_rotation(lua_State* L) { V3(L, EulerDeg(WorldRot(CheckAlive(L, 1)))); return 1; }
		int En_set_rotation(lua_State* L)
		{
			SetWorldRotation(CheckAlive(L, 1), QuatFromDeg(GetV3(L, 2)));
			return 0;
		}
		int En_local_rotation(lua_State* L) { V3(L, EulerDeg(T(CheckAlive(L, 1)).rotation_local)); return 1; }
		int En_set_local_rotation(lua_State* L)
		{
			TransformComponent& t = T(CheckAlive(L, 1));
			t.rotation_local = QuatFromDeg(GetV3(L, 2));
			t.SetDirty();
			return 0;
		}
		int En_scale(lua_State* L) { V3(L, T(CheckAlive(L, 1)).scale_local); return 1; }
		int En_set_scale(lua_State* L)
		{
			TransformComponent& t = T(CheckAlive(L, 1));
			if (lua_isnumber(L, 2)) { const float s = (float)lua_tonumber(L, 2); t.scale_local = XMFLOAT3(s, s, s); }
			else t.scale_local = GetV3(L, 2);
			t.SetDirty();
			return 0;
		}
		int En_forward(lua_State* L) { V3(L, Axis(CheckAlive(L, 1), 0, 0, 1)); return 1; }
		int En_right(lua_State* L) { V3(L, Axis(CheckAlive(L, 1), 1, 0, 0)); return 1; }
		int En_up(lua_State* L) { V3(L, Axis(CheckAlive(L, 1), 0, 1, 0)); return 1; }
		int En_look_at(lua_State* L)
		{
			const Entity e = CheckAlive(L, 1);
			const XMFLOAT3 target = GetV3(L, 2), p = WorldPos(e);
			XMVECTOR d = XMVectorSet(target.x - p.x, target.y - p.y, target.z - p.z, 0);
			if (XMVectorGetX(XMVector3LengthSq(d)) < 1e-10f) return 0;
			const XMMATRIX m = XMMatrixInverse(nullptr, XMMatrixLookAtLH(XMLoadFloat3(&p), XMLoadFloat3(&target), XMVectorSet(0, 1, 0, 0)));
			XMFLOAT4 q;
			XMStoreFloat4(&q, XMQuaternionRotationMatrix(m));
			SetWorldRotation(e, q);
			return 0;
		}
		int En_set_active(lua_State* L)
		{
			const Entity e = CheckAlive(L, 1);
			const bool on = lua_toboolean(L, 2) != 0;
			if (ObjectComponent* o = S().objects.GetComponent(e)) o->SetRenderable(on);
			if (LightComponent* l = S().lights.GetComponent(e)) l->SetStatic(false), l->intensity = on ? l->intensity : l->intensity; // lights are toggled via intensity by the script
			return 0;
		}
		int En_parent(lua_State* L)
		{
			const Entity e = CheckAlive(L, 1);
			const HierarchyComponent* h = S().hierarchy.GetComponent(e);
			PushEntity(L, h ? h->parentID : INVALID_ENTITY);
			return 1;
		}
		int En_child(lua_State* L)
		{
			const Entity e = CheckAlive(L, 1);
			const std::string name = luaL_checkstring(L, 2);
			for (size_t i = 0; i < S().hierarchy.GetCount(); ++i)
				if (S().hierarchy[i].parentID == e)
				{
					const Entity c = S().hierarchy.GetEntity(i);
					const NameComponent* n = S().names.GetComponent(c);
					if (n && n->name == name) { PushEntity(L, c); return 1; }
				}
			lua_pushnil(L);
			return 1;
		}
		int En_children(lua_State* L)
		{
			const Entity e = CheckAlive(L, 1);
			lua_newtable(L);
			int k = 1;
			for (size_t i = 0; i < S().hierarchy.GetCount(); ++i)
				if (S().hierarchy[i].parentID == e)
				{
					PushEntity(L, S().hierarchy.GetEntity(i));
					lua_rawseti(L, -2, k++);
				}
			return 1;
		}
		int En_destroy(lua_State* L)
		{
			const Entity e = CheckAlive(L, 1);
			S().Entity_Remove(e);
			return 0;
		}
		int En_velocity(lua_State* L)
		{
			RigidBodyPhysicsComponent* b = Body(CheckAlive(L, 1));
			V3(L, b ? wi::physics::GetVelocity(*b) : XMFLOAT3(0, 0, 0));
			return 1;
		}
		int En_set_velocity(lua_State* L)
		{
			if (RigidBodyPhysicsComponent* b = Body(CheckAlive(L, 1)))
				wi::physics::SetLinearVelocity(*b, GetV3(L, 2));
			return 0;
		}
		int En_add_force(lua_State* L)
		{
			if (RigidBodyPhysicsComponent* b = Body(CheckAlive(L, 1)))
				wi::physics::ApplyForce(*b, GetV3(L, 2));
			return 0;
		}
		int En_add_impulse(lua_State* L)
		{
			if (RigidBodyPhysicsComponent* b = Body(CheckAlive(L, 1)))
				wi::physics::ApplyImpulse(*b, GetV3(L, 2));
			return 0;
		}
		int En_move(lua_State* L)
		{
			RigidBodyPhysicsComponent* b = Body(CheckAlive(L, 1));
			if (!b || !b->IsCharacterPhysics())
				return luaL_error(L, "entity:move() needs a rigidbody with lockRotation on a capsule (character controller)");
			wi::physics::MoveCharacter(*b, GetV3(L, 2), (float)luaL_optnumber(L, 3, 6), (float)luaL_optnumber(L, 4, 0), lua_toboolean(L, 5) != 0);
			return 0;
		}
		int En_grounded(lua_State* L)
		{
			RigidBodyPhysicsComponent* b = Body(CheckAlive(L, 1));
			bool g = false;
			if (b && b->IsCharacterPhysics())
				g = wi::physics::GetCharacterGroundState(*b) == wi::physics::CharacterGroundStates::OnGround;
			else if (b)
			{
				const XMFLOAT3 p = WorldPos(ToEntity(L, 1)->e);
				const auto r = wi::physics::Intersects(S(), wi::primitive::Ray(XMFLOAT3(p.x, p.y, p.z), XMFLOAT3(0, -1, 0), 0.0f, 1.1f));
				g = r.IsValid() && r.entity != ToEntity(L, 1)->e;
			}
			lua_pushboolean(L, g);
			return 1;
		}
		int En_angular_velocity_set(lua_State* L)
		{
			if (RigidBodyPhysicsComponent* b = Body(CheckAlive(L, 1)))
				wi::physics::SetAngularVelocity(*b, GetV3(L, 2));
			return 0;
		}
		int En_add_torque(lua_State* L)
		{
			if (RigidBodyPhysicsComponent* b = Body(CheckAlive(L, 1)))
				wi::physics::ApplyTorque(*b, GetV3(L, 2));
			return 0;
		}
		// add_force_at(force, point [, local = false])
		int En_add_force_at(lua_State* L)
		{
			if (RigidBodyPhysicsComponent* b = Body(CheckAlive(L, 1)))
				wi::physics::ApplyForceAt(*b, GetV3(L, 2), GetV3(L, 3), lua_toboolean(L, 4) != 0);
			return 0;
		}
		int En_add_impulse_at(lua_State* L)
		{
			if (RigidBodyPhysicsComponent* b = Body(CheckAlive(L, 1)))
				wi::physics::ApplyImpulseAt(*b, GetV3(L, 2), GetV3(L, 3), lua_toboolean(L, 4) != 0);
			return 0;
		}
		int En_wake(lua_State* L)
		{
			if (RigidBodyPhysicsComponent* b = Body(CheckAlive(L, 1)))
				wi::physics::SetActivationState(*b, wi::physics::ActivationState::Active);
			return 0;
		}
		int En_sleep(lua_State* L)
		{
			if (RigidBodyPhysicsComponent* b = Body(CheckAlive(L, 1)))
				wi::physics::SetActivationState(*b, wi::physics::ActivationState::Inactive);
			return 0;
		}
		// drive(throttle -1..1, steer -1..1 [, brake 0..1 [, handbrake 0..1]])
		int En_drive(lua_State* L)
		{
			RigidBodyPhysicsComponent* b = Body(CheckAlive(L, 1));
			if (!b || !b->IsVehicle())
				return luaL_error(L, "entity:drive() needs a vehicle component on a dynamic rigidbody");
			wi::physics::DriveVehicle(*b, (float)luaL_checknumber(L, 2), (float)luaL_optnumber(L, 3, 0), (float)luaL_optnumber(L, 4, 0), (float)luaL_optnumber(L, 5, 0));
			return 0;
		}
		// signed forward speed for vehicles, speed magnitude otherwise
		int En_speed(lua_State* L)
		{
			RigidBodyPhysicsComponent* b = Body(CheckAlive(L, 1));
			float v = 0;
			if (b && b->IsVehicle()) v = wi::physics::GetVehicleForwardVelocity(*b);
			else if (b) { const XMFLOAT3 q = wi::physics::GetVelocity(*b); v = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z); }
			lua_pushnumber(L, v);
			return 1;
		}
		int En_break_joint(lua_State* L)
		{
			if (PhysicsConstraintComponent* c = S().constraints.GetComponent(CheckAlive(L, 1)))
				wi::physics::SetConstraintBroken(*c, lua_gettop(L) < 2 || lua_toboolean(L, 2) != 0);
			return 0;
		}
		int En_joint_broken(lua_State* L)
		{
			PhysicsConstraintComponent* c = S().constraints.GetComponent(CheckAlive(L, 1));
			lua_pushboolean(L, c && wi::physics::IsConstraintBroken(*c));
			return 1;
		}
		// ---- lumen.physics ----------------------------------------------------------------------------------
		int Ph_raycast(lua_State* L) // physics bodies only: raycast(origin, direction [, max_distance])
		{
			const XMFLOAT3 o = GetV3(L, 1), d = GetV3(L, 2);
			const float maxd = (float)luaL_optnumber(L, 3, 1000.0);
			const auto hit = wi::physics::Intersects(S(), wi::primitive::Ray(o, d, 0, maxd));
			if (!hit.IsValid()) { lua_pushnil(L); return 1; }
			lua_newtable(L);
			PushEntity(L, hit.entity); lua_setfield(L, -2, "entity");
			V3(L, hit.position); lua_setfield(L, -2, "position");
			V3(L, hit.normal); lua_setfield(L, -2, "normal");
			const XMFLOAT3 dv = XMFLOAT3(hit.position.x - o.x, hit.position.y - o.y, hit.position.z - o.z);
			lua_pushnumber(L, sqrtf(dv.x * dv.x + dv.y * dv.y + dv.z * dv.z)); lua_setfield(L, -2, "distance");
			return 1;
		}
		int Ph_overlap_sphere(lua_State* L)
		{
			const wi::primitive::Sphere sp(GetV3(L, 1), (float)luaL_checknumber(L, 2));
			lua_newtable(L);
			int k = 1;
			for (size_t i = 0; i < S().aabb_objects.size(); ++i)
				if (S().aabb_objects[i].intersects(sp))
				{
					PushEntity(L, S().objects.GetEntity(i));
					lua_rawseti(L, -2, k++);
				}
			return 1;
		}
		int Ph_overlap_box(lua_State* L) // overlap_box(center, half_extents)
		{
			const XMFLOAT3 c = GetV3(L, 1), h = GetV3(L, 2);
			const wi::primitive::AABB box(XMFLOAT3(c.x - h.x, c.y - h.y, c.z - h.z), XMFLOAT3(c.x + h.x, c.y + h.y, c.z + h.z));
			lua_newtable(L);
			int k = 1;
			for (size_t i = 0; i < S().aabb_objects.size(); ++i)
				if (S().aabb_objects[i].intersects(box))
				{
					PushEntity(L, S().objects.GetEntity(i));
					lua_rawseti(L, -2, k++);
				}
			return 1;
		}
		// ---- animation --------------------------------------------------------------------------------------
		std::vector<Entity> ClipsOf(Entity e)
		{
			const Bridge& b = *g_host->Context().bridge;
			std::string id = b.IdOf(e);
			if (id.empty()) id = b.IdOfOrAncestor(S(), e);
			return b.Animations(id);
		}
		std::string ClipName(Entity c)
		{
			const NameComponent* n = S().names.GetComponent(c);
			return n ? n->name : std::string("clip");
		}
		// Clip argument: name (string), 1-based index (number) or nil = first clip.
		Entity FindClip(lua_State* L, Entity e, int idx)
		{
			const std::vector<Entity> clips = ClipsOf(e);
			if (lua_type(L, idx) == LUA_TNUMBER)
			{
				const int i = (int)lua_tointeger(L, idx) - 1;
				return i >= 0 && i < (int)clips.size() ? clips[(size_t)i] : INVALID_ENTITY;
			}
			if (lua_type(L, idx) == LUA_TSTRING)
			{
				const std::string want = lua_tostring(L, idx);
				for (Entity c : clips) if (ClipName(c) == want) return c;
				std::string lw = str::ToLower(want);
				for (Entity c : clips) if (str::ToLower(ClipName(c)).find(lw) != std::string::npos) return c; // partial, case-insensitive
				return INVALID_ENTITY;
			}
			return clips.empty() ? INVALID_ENTITY : clips[0];
		}
		int En_animations(lua_State* L)
		{
			const std::vector<Entity> clips = ClipsOf(CheckAlive(L, 1));
			lua_newtable(L);
			for (size_t i = 0; i < clips.size(); ++i)
			{
				lua_pushstring(L, ClipName(clips[i]).c_str());
				lua_rawseti(L, -2, (lua_Integer)i + 1);
			}
			return 1;
		}
		// play(clip [, {loop=true, speed=1, weight=1, fade=0, restart=false, exclusive=false, time=0}])
		int En_play(lua_State* L)
		{
			const Entity e = CheckAlive(L, 1);
			const Entity clip = FindClip(L, e, 2);
			AnimationComponent* a = clip != INVALID_ENTITY ? S().animations.GetComponent(clip) : nullptr;
			if (!a)
			{
				if (lua_type(L, 2) == LUA_TSTRING)
					return luaL_error(L, "no animation clip '%s' on this entity (entity:animations() lists them)", lua_tostring(L, 2));
				lua_pushboolean(L, 0);
				return 1;
			}
			bool loop = true, restart = false, exclusive = false;
			float speed = 1, weight = 1, fade = 0, time = -1;
			if (lua_istable(L, 3))
			{
				auto num = [&](const char* k, float& v) { lua_getfield(L, 3, k); if (lua_isnumber(L, -1)) v = (float)lua_tonumber(L, -1); lua_pop(L, 1); };
				auto flag = [&](const char* k, bool& v) { lua_getfield(L, 3, k); if (!lua_isnil(L, -1)) v = lua_toboolean(L, -1) != 0; lua_pop(L, 1); };
				num("speed", speed); num("weight", weight); num("fade", fade); num("time", time);
				flag("loop", loop); flag("restart", restart); flag("exclusive", exclusive);
			}
			if (exclusive)
				for (Entity other : ClipsOf(e))
					if (other != clip)
						if (AnimationComponent* o = S().animations.GetComponent(other))
							if (o->IsPlaying()) g_host->FadeAnimation(other, 0.0f, fade, true);
			if (restart || !a->IsPlaying()) a->timer = a->start;
			if (time >= 0) a->timer = a->start + time;
			a->SetLooped(loop);
			a->speed = speed;
			a->Play();
			if (fade > 0) { a->amount = 0; g_host->FadeAnimation(clip, weight, fade, false); }
			else { a->amount = weight; g_host->FadeAnimation(clip, weight, 0, false); }
			lua_pushboolean(L, 1);
			return 1;
		}
		// stop_animation([clip [, fade]]) - without a clip every clip of the entity
		int En_stop_animation(lua_State* L)
		{
			const Entity e = CheckAlive(L, 1);
			const float fade = (float)luaL_optnumber(L, 3, 0);
			std::vector<Entity> targets;
			if (lua_isnoneornil(L, 2)) targets = ClipsOf(e);
			else if (const Entity c = FindClip(L, e, 2); c != INVALID_ENTITY) targets.push_back(c);
			for (Entity c : targets)
				if (AnimationComponent* a = S().animations.GetComponent(c))
				{
					if (fade > 0) g_host->FadeAnimation(c, 0.0f, fade, true);
					else a->Stop();
				}
			return 0;
		}
		// animation([clip]) -> {name, time, length, playing, loop, speed, weight} ; without a clip: the first playing one
		int En_animation(lua_State* L)
		{
			const Entity e = CheckAlive(L, 1);
			Entity clip = INVALID_ENTITY;
			if (lua_isnoneornil(L, 2))
			{
				for (Entity c : ClipsOf(e))
					if (const AnimationComponent* a = S().animations.GetComponent(c); a && a->IsPlaying()) { clip = c; break; }
			}
			else
				clip = FindClip(L, e, 2);
			const AnimationComponent* a = clip != INVALID_ENTITY ? S().animations.GetComponent(clip) : nullptr;
			if (!a) { lua_pushnil(L); return 1; }
			lua_newtable(L);
			lua_pushstring(L, ClipName(clip).c_str()); lua_setfield(L, -2, "name");
			lua_pushnumber(L, a->timer - a->start); lua_setfield(L, -2, "time");
			lua_pushnumber(L, a->GetLength()); lua_setfield(L, -2, "length");
			lua_pushboolean(L, a->IsPlaying()); lua_setfield(L, -2, "playing");
			lua_pushboolean(L, a->IsLooped()); lua_setfield(L, -2, "loop");
			lua_pushnumber(L, a->speed); lua_setfield(L, -2, "speed");
			lua_pushnumber(L, a->amount); lua_setfield(L, -2, "weight");
			return 1;
		}
		// set_animation(clip, {time=, speed=, weight=, loop=, playing=})
		int En_set_animation(lua_State* L)
		{
			const Entity e = CheckAlive(L, 1);
			AnimationComponent* a = nullptr;
			if (const Entity c = FindClip(L, e, 2); c != INVALID_ENTITY) a = S().animations.GetComponent(c);
			if (!a) return luaL_error(L, "no such animation clip");
			luaL_checktype(L, 3, LUA_TTABLE);
			lua_getfield(L, 3, "time"); if (lua_isnumber(L, -1)) a->timer = a->start + (float)lua_tonumber(L, -1); lua_pop(L, 1);
			lua_getfield(L, 3, "speed"); if (lua_isnumber(L, -1)) a->speed = (float)lua_tonumber(L, -1); lua_pop(L, 1);
			lua_getfield(L, 3, "weight"); if (lua_isnumber(L, -1)) a->amount = (float)lua_tonumber(L, -1); lua_pop(L, 1);
			lua_getfield(L, 3, "loop"); if (!lua_isnil(L, -1)) a->SetLooped(lua_toboolean(L, -1) != 0); lua_pop(L, 1);
			lua_getfield(L, 3, "playing"); if (!lua_isnil(L, -1)) { if (lua_toboolean(L, -1)) a->Play(); else a->Pause(); } lua_pop(L, 1);
			return 0;
		}
		int En_light(lua_State* L)
		{
			const Entity e = CheckAlive(L, 1);
			if (!S().lights.Contains(e)) { lua_pushnil(L); return 1; }
			LightHandle* h = (LightHandle*)lua_newuserdatauv(L, sizeof(LightHandle), 0);
			h->e = e;
			luaL_setmetatable(L, kLightMeta);
			return 1;
		}
		int En_color(lua_State* L)
		{
			const Entity e = CheckAlive(L, 1);
			MaterialComponent* m = S().materials.GetComponent(e);
			if (!m) return 0;
			double o[4];
			int n = 0;
			lua::detail::CheckVec(L, 2, 3, o, &n);
			m->SetBaseColor(XMFLOAT4((float)o[0], (float)o[1], (float)o[2], n >= 4 ? (float)o[3] : m->baseColor.w));
			return 0;
		}
		int En_eq(lua_State* L)
		{
			lua_pushboolean(L, ToEntity(L, 1)->e == ToEntity(L, 2)->e);
			return 1;
		}
		int En_tostring(lua_State* L)
		{
			const Entity e = ToEntity(L, 1)->e;
			const NameComponent* n = S().names.GetComponent(e);
			lua_pushfstring(L, "entity(%s)", n ? n->name.c_str() : "?");
			return 1;
		}

		// light userdata: intensity / range / color as fields
		int Li_index(lua_State* L)
		{
			LightHandle* h = (LightHandle*)luaL_checkudata(L, 1, kLightMeta);
			LightComponent* l = S().lights.GetComponent(h->e);
			if (!l) return luaL_error(L, "light no longer exists");
			const std::string k = luaL_checkstring(L, 2);
			if (k == "intensity") lua_pushnumber(L, l->intensity);
			else if (k == "range") lua_pushnumber(L, l->range);
			else if (k == "color") V3(L, l->color);
			else lua_pushnil(L);
			return 1;
		}
		int Li_newindex(lua_State* L)
		{
			LightHandle* h = (LightHandle*)luaL_checkudata(L, 1, kLightMeta);
			LightComponent* l = S().lights.GetComponent(h->e);
			if (!l) return luaL_error(L, "light no longer exists");
			const std::string k = luaL_checkstring(L, 2);
			if (k == "intensity") l->intensity = (float)luaL_checknumber(L, 3);
			else if (k == "range") l->range = (float)luaL_checknumber(L, 3);
			else if (k == "color") l->color = GetV3(L, 3);
			else return luaL_error(L, "light has no field '%s'", k.c_str());
			return 0;
		}

		// ---- lumen.scene -------------------------------------------------------------------
		int Sc_find(lua_State* L)
		{
			const std::string name = luaL_checkstring(L, 1);
			for (size_t i = 0; i < S().names.GetCount(); ++i)
				if (S().names[i].name == name && S().transforms.Contains(S().names.GetEntity(i)))
				{
					PushEntity(L, S().names.GetEntity(i));
					return 1;
				}
			lua_pushnil(L);
			return 1;
		}
		int Sc_find_all(lua_State* L)
		{
			const std::string name = luaL_checkstring(L, 1);
			lua_newtable(L);
			int k = 1;
			for (size_t i = 0; i < S().names.GetCount(); ++i)
				if (S().names[i].name == name)
				{
					PushEntity(L, S().names.GetEntity(i));
					lua_rawseti(L, -2, k++);
				}
			return 1;
		}
		int Sc_find_by_tag(lua_State* L)
		{
			const std::string tag = luaL_checkstring(L, 1);
			lua_newtable(L);
			int k = 1;
			for (const SceneEntity& se : g_host->Context().doc->entities)
				if (se.tag == tag)
				{
					const Entity e = g_host->Context().bridge->Find(se.id);
					if (e != INVALID_ENTITY) { PushEntity(L, e); lua_rawseti(L, -2, k++); }
				}
			return 1;
		}
		int Sc_spawn(lua_State* L)
		{
			// spawn(primitive, position [, size]) -> entity with default material and a dynamic body when `physics` is true
			const std::string prim = luaL_checkstring(L, 1);
			const XMFLOAT3 pos = GetV3(L, 2);
			const Entity e = S().Entity_CreateTransform("spawned");
			SceneEntity se = MakePrimitiveEntity(prim, "spawned");
			if (lua_istable(L, 3) || lua_isuserdata(L, 3)) { const XMFLOAT3 sz = GetV3(L, 3); se.mesh->size = {sz.x, sz.y, sz.z}; }
			ObjectComponent& o = S().objects.Create(e);
			MeshComponent& m = S().meshes.Create(e);
			o.meshID = e;
			BuildPrimitiveMesh(m, prim, se.mesh->size, 32);
			m.subsets[0].materialID = e;
			S().materials.Create(e);
			m.CreateRenderData();
			T(e).translation_local = pos;
			T(e).SetDirty();
			if (lua_toboolean(L, 4))
			{
				RigidBodyPhysicsComponent& b = S().rigidbodies.Create(e);
				if (prim == "sphere") { b.shape = RigidBodyPhysicsComponent::SPHERE; b.sphere.radius = se.mesh->size.x * 0.5f; }
				else b.box.halfextents = XMFLOAT3(se.mesh->size.x * 0.5f, se.mesh->size.y * 0.5f, se.mesh->size.z * 0.5f);
				b.mass = 1.0f;
			}
			PushEntity(L, e);
			return 1;
		}
		// instantiate(prefab [, position [, rotation_degrees]]) -> root entity
		int Sc_instantiate(lua_State* L)
		{
			const std::string path = luaL_checkstring(L, 1);
			Float3 pos, rot;
			const bool hasPos = !lua_isnoneornil(L, 2), hasRot = !lua_isnoneornil(L, 3);
			if (hasPos) { const XMFLOAT3 p = GetV3(L, 2); pos = {p.x, p.y, p.z}; }
			if (hasRot) { const XMFLOAT3 r = GetV3(L, 3); rot = {r.x, r.y, r.z}; }
			const Entity e = g_host->Instantiate(path, hasPos ? &pos : nullptr, hasRot ? &rot : nullptr);
			if (e == INVALID_ENTITY) return luaL_error(L, "instantiate failed: %s", path.c_str());
			PushEntity(L, e);
			return 1;
		}
		int Sc_raycast(lua_State* L)
		{
			const XMFLOAT3 o = GetV3(L, 1), d = GetV3(L, 2);
			const float maxd = (float)luaL_optnumber(L, 3, 1000.0);
			const auto hit = wi::scene::Pick(wi::primitive::Ray(o, d, 0, maxd), wi::enums::FILTER_OBJECT_ALL, ~0u, S());
			if (hit.entity == INVALID_ENTITY) { lua_pushnil(L); return 1; }
			lua_newtable(L);
			PushEntity(L, hit.entity); lua_setfield(L, -2, "entity");
			V3(L, hit.position); lua_setfield(L, -2, "position");
			V3(L, hit.normal); lua_setfield(L, -2, "normal");
			lua_pushnumber(L, hit.distance); lua_setfield(L, -2, "distance");
			return 1;
		}
		int Sc_load(lua_State* L)
		{
			const std::string p = luaL_checkstring(L, 1);
			if (g_host->Context().loadScene)
				lua_pushboolean(L, g_host->Context().loadScene(p));
			else
				lua_pushboolean(L, 0);
			return 1;
		}
		int Sc_gravity(lua_State* L)
		{
			if (lua_gettop(L) == 0)
			{
				V3(L, S().weather.gravity);
				return 1;
			}
			S().weather.gravity = GetV3(L, 1);
			return 0;
		}

		// ---- lumen.nav ----------------------------------------------------------------------
		NavSystem& Nav() { return g_host->Nav(); }
		int Nv_baked(lua_State* L) { lua_pushboolean(L, Nav().Baked()); return 1; }
		int Nv_bake(lua_State* L)
		{
			const PlayContext& c = g_host->Context();
			lua_pushboolean(L, Nav().Bake(*c.scene, *c.bridge, *c.doc));
			return 1;
		}
		int Nv_find_path(lua_State* L)
		{
			const XMFLOAT3 a = GetV3(L, 1), b = GetV3(L, 2);
			std::vector<Float3> path;
			if (!Nav().Mesh().FindPath({a.x, a.y, a.z}, {b.x, b.y, b.z}, path, lua_toboolean(L, 3) != 0))
			{
				lua_pushnil(L);
				return 1;
			}
			lua_createtable(L, (int)path.size(), 0);
			for (size_t i = 0; i < path.size(); ++i)
			{
				lua::detail::PushVec3(L, path[i].x, path[i].y, path[i].z);
				lua_rawseti(L, -2, (lua_Integer)i + 1);
			}
			return 1;
		}
		int Nv_project(lua_State* L)
		{
			const XMFLOAT3 a = GetV3(L, 1);
			Float3 out;
			if (!Nav().Mesh().Project({a.x, a.y, a.z}, out, (float)luaL_optnumber(L, 2, 2.0)))
			{
				lua_pushnil(L);
				return 1;
			}
			lua::detail::PushVec3(L, out.x, out.y, out.z);
			return 1;
		}
		int Nv_raycast(lua_State* L)
		{
			const XMFLOAT3 a = GetV3(L, 1), b = GetV3(L, 2);
			Float3 hit;
			const bool clear = Nav().Mesh().Raycast({a.x, a.y, a.z}, {b.x, b.y, b.z}, &hit);
			lua_pushboolean(L, clear);
			lua::detail::PushVec3(L, hit.x, hit.y, hit.z);
			return 2;
		}
		int Nv_random_point(lua_State* L)
		{
			const XMFLOAT3 c = GetV3(L, 1);
			Float3 out;
			if (!Nav().Mesh().RandomPoint({c.x, c.y, c.z}, (float)luaL_optnumber(L, 2, 10.0), Nav().Rng(), out))
			{
				lua_pushnil(L);
				return 1;
			}
			lua::detail::PushVec3(L, out.x, out.y, out.z);
			return 1;
		}
		int En_agent_move_to(lua_State* L)
		{
			const Entity e = CheckAlive(L, 1);
			const XMFLOAT3 d = GetV3(L, 2);
			lua_pushboolean(L, Nav().MoveTo(S(), e, {d.x, d.y, d.z}));
			return 1;
		}
		// ---- lumen.anim (keyframe clips, .lanim) -------------------------------------------------------
		void ClipOpts(lua_State* L, int idx, bool& loop, float& speed, float& weight)
		{
			if (!lua_istable(L, idx)) return;
			lua_getfield(L, idx, "loop");   if (!lua_isnil(L, -1)) loop = lua_toboolean(L, -1) != 0; lua_pop(L, 1);
			lua_getfield(L, idx, "speed");  if (lua_isnumber(L, -1)) speed = (float)lua_tonumber(L, -1); lua_pop(L, 1);
			lua_getfield(L, idx, "weight"); if (lua_isnumber(L, -1)) weight = (float)lua_tonumber(L, -1); lua_pop(L, 1);
		}
		int An_play(lua_State* L)
		{
			bool loop = false; float speed = 1, weight = 1;
			ClipOpts(L, 3, loop, speed, weight);
			const int id = g_host->PlayClip(CheckAlive(L, 1), luaL_checkstring(L, 2), loop, speed, weight);
			if (id == 0) lua_pushnil(L); else lua_pushinteger(L, id);
			return 1;
		}
		int An_stop(lua_State* L)
		{
			if (lua_isinteger(L, 1)) g_host->Clips().Stop((int)lua_tointeger(L, 1));
			else g_host->Clips().StopOn(CheckAlive(L, 1));
			return 0;
		}
		int An_set(lua_State* L)
		{
			ClipSystem::Instance* in = g_host->Clips().Find((int)luaL_checkinteger(L, 1));
			if (!in) return 0;
			if (lua_isnumber(L, 2)) in->speed = (float)lua_tonumber(L, 2);
			if (lua_isboolean(L, 3)) in->playing = lua_toboolean(L, 3) != 0;
			return 0;
		}
		int An_time(lua_State* L)
		{
			ClipSystem::Instance* in = g_host->Clips().Find((int)luaL_checkinteger(L, 1));
			if (!in) { lua_pushnil(L); return 1; }
			lua_pushnumber(L, in->time);
			lua_pushnumber(L, in->clip.duration);
			return 2;
		}
		int En_play_clip(lua_State* L)
		{
			bool loop = false; float speed = 1, weight = 1;
			ClipOpts(L, 3, loop, speed, weight);
			const int id = g_host->PlayClip(CheckAlive(L, 1), luaL_checkstring(L, 2), loop, speed, weight);
			if (id == 0) lua_pushnil(L); else lua_pushinteger(L, id);
			return 1;
		}
		int En_stop_clips(lua_State* L) { g_host->Clips().StopOn(CheckAlive(L, 1)); return 0; }
		int En_agent_stop(lua_State* L) { Nav().Stop(CheckAlive(L, 1)); return 0; }
		int En_agent_set_speed(lua_State* L)
		{
			if (NavSystem::Agent* a = Nav().Find(CheckAlive(L, 1))) a->speedOverride = (float)luaL_checknumber(L, 2);
			return 0;
		}
		int En_agent_moving(lua_State* L)
		{
			NavSystem::Agent* a = Nav().Find(CheckAlive(L, 1));
			lua_pushboolean(L, a && a->moving);
			return 1;
		}
		int En_agent_remaining(lua_State* L)
		{
			NavSystem::Agent* a = Nav().Find(CheckAlive(L, 1));
			lua_pushnumber(L, a ? Nav().Remaining(*a) : 0.0);
			return 1;
		}
		int En_agent_velocity(lua_State* L)
		{
			NavSystem::Agent* a = Nav().Find(CheckAlive(L, 1));
			lua::detail::PushVec3(L, a ? a->velocity.x : 0, a ? a->velocity.y : 0, a ? a->velocity.z : 0);
			return 1;
		}

		// ---- lumen.input -------------------------------------------------------------------
		wi::input::BUTTON KeyFor(const std::string& n)
		{
			using namespace wi::input;
			if (n.size() == 1 && n[0] >= 'a' && n[0] <= 'z') return (BUTTON)('A' + (n[0] - 'a'));
			if (n.size() == 1 && n[0] >= '0' && n[0] <= '9') return (BUTTON)('0' + (n[0] - '0'));
			static const std::pair<const char*, BUTTON> table[] = {
				{"space", KEYBOARD_BUTTON_SPACE}, {"shift", KEYBOARD_BUTTON_LSHIFT}, {"lshift", KEYBOARD_BUTTON_LSHIFT}, {"rshift", KEYBOARD_BUTTON_RSHIFT},
				{"ctrl", KEYBOARD_BUTTON_LCONTROL}, {"escape", KEYBOARD_BUTTON_ESCAPE}, {"esc", KEYBOARD_BUTTON_ESCAPE}, {"enter", KEYBOARD_BUTTON_ENTER},
				{"tab", KEYBOARD_BUTTON_TAB}, {"up", KEYBOARD_BUTTON_UP}, {"down", KEYBOARD_BUTTON_DOWN}, {"left", KEYBOARD_BUTTON_LEFT},
				{"right", KEYBOARD_BUTTON_RIGHT}, {"backspace", KEYBOARD_BUTTON_BACKSPACE}, {"delete", KEYBOARD_BUTTON_DELETE}, {"home", KEYBOARD_BUTTON_HOME},
				{"mouse1", MOUSE_BUTTON_LEFT}, {"lmb", MOUSE_BUTTON_LEFT}, {"mouse2", MOUSE_BUTTON_RIGHT}, {"rmb", MOUSE_BUTTON_RIGHT},
				{"mouse3", MOUSE_BUTTON_MIDDLE}, {"mmb", MOUSE_BUTTON_MIDDLE},
				{"f1", KEYBOARD_BUTTON_F1}, {"f12", KEYBOARD_BUTTON_F12}};
			for (const auto& p : table)
				if (n == p.first) return p.second;
			return BUTTON_NONE;
		}
		int In_down(lua_State* L)
		{
			const auto k = KeyFor(str::ToLower(luaL_checkstring(L, 1)));
			lua_pushboolean(L, k != wi::input::BUTTON_NONE && wi::input::Down(k));
			return 1;
		}
		int In_pressed(lua_State* L)
		{
			const auto k = KeyFor(str::ToLower(luaL_checkstring(L, 1)));
			lua_pushboolean(L, k != wi::input::BUTTON_NONE && wi::input::Press(k));
			return 1;
		}
		int In_mouse_delta(lua_State* L)
		{
			float dx, dy;
			g_host->MouseDelta(dx, dy);
			lua_pushnumber(L, dx);
			lua_pushnumber(L, dy);
			return 2;
		}
		int In_mouse_pos(lua_State* L)
		{
			const XMFLOAT4 p = wi::input::GetPointer();
			lua_pushnumber(L, p.x);
			lua_pushnumber(L, p.y);
			return 2;
		}
		int In_wheel(lua_State* L)
		{
			lua_pushnumber(L, wi::input::GetMouseState().delta_wheel);
			return 1;
		}
		int In_set_cursor_locked(lua_State* L)
		{
			g_host->SetCursorLocked(lua_toboolean(L, 1) != 0);
			return 0;
		}
		int In_cursor_locked(lua_State* L)
		{
			lua_pushboolean(L, g_host->CursorLocked());
			return 1;
		}

		// ---- lumen.log / debug / events / misc ---------------------------------------------------
		std::string JoinArgs(lua_State* L)
		{
			std::string out;
			const int n = lua_gettop(L);
			for (int i = 1; i <= n; ++i)
			{
				size_t len;
				const char* s = luaL_tolstring(L, i, &len);
				if (i > 1) out += '\t';
				out.append(s, len);
				lua_pop(L, 1);
			}
			return out;
		}
		int Lg_info(lua_State* L) { LUMEN_INFO("game", "%s", JoinArgs(L).c_str()); return 0; }
		int Lg_warn(lua_State* L) { LUMEN_WARN("game", "%s", JoinArgs(L).c_str()); return 0; }
		int Lg_error(lua_State* L) { LUMEN_ERROR("game", "%s", JoinArgs(L).c_str()); return 0; }
		int Lg_debug(lua_State* L) { LUMEN_DEBUG("game", "%s", JoinArgs(L).c_str()); return 0; }
		int Db_breakpoint(lua_State*)
		{
			if (g_host && g_host->Debugger().Attached())
				g_host->Debugger().BreakNow("lumen.debug.breakpoint()");
			return 0;
		}
		int Db_text(lua_State* L)
		{
			if (g_host->Context().debugText)
				g_host->Context().debugText((float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2), luaL_checkstring(L, 3));
			return 0;
		}
		int Db_line(lua_State* L)
		{
			const XMFLOAT3 a = GetV3(L, 1), b = GetV3(L, 2);
			wi::renderer::DrawLine(wi::renderer::RenderableLine{a, b, XMFLOAT4(1, 0.8f, 0.2f, 1), XMFLOAT4(1, 0.8f, 0.2f, 1)});
			return 0;
		}
		int Ev_on(lua_State* L)
		{
			const char* name = luaL_checkstring(L, 1);
			luaL_checktype(L, 2, LUA_TFUNCTION);
			lua_getfield(L, LUA_REGISTRYINDEX, "lumen.events");
			lua_getfield(L, -1, name);
			if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_newtable(L); lua_pushvalue(L, -1); lua_setfield(L, -3, name); }
			lua_pushvalue(L, 2);
			lua_rawseti(L, -2, (lua_Integer)lua_rawlen(L, -2) + 1);
			return 0;
		}
		int Ev_emit(lua_State* L)
		{
			const char* name = luaL_checkstring(L, 1);
			const int nargs = lua_gettop(L) - 1;
			lua_getfield(L, LUA_REGISTRYINDEX, "lumen.events");
			lua_getfield(L, -1, name);
			if (!lua_istable(L, -1)) return 0;
			const int n = (int)lua_rawlen(L, -1);
			for (int i = 1; i <= n; ++i)
			{
				lua_rawgeti(L, -1, i);
				for (int a = 0; a < nargs; ++a) lua_pushvalue(L, 2 + a);
				if (lua_pcall(L, nargs, 0, 0) != LUA_OK)
				{
					g_host->AddError(std::string("event '") + name + "': " + lua_tostring(L, -1));
					LUMEN_ERROR("game", "event '%s': %s", name, lua_tostring(L, -1));
					lua_pop(L, 1);
				}
			}
			return 0;
		}
		void EmitEvent(lua_State* L, const char* name, Entity a, Entity b)
		{
			lua_pushcfunction(L, Ev_emit);
			lua_pushstring(L, name);
			PushEntity(L, a);
			PushEntity(L, b);
			if (lua_pcall(L, 3, 0, 0) != LUA_OK) lua_pop(L, 1);
		}
		// ---- lumen.ui (immediate mode 2D overlay) -----------------------------------------------------------------
		int HexNibble(char c) { return c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0)); }
		// color: {r,g,b[,a]} in 0..1, or "#rrggbb" / "#rrggbbaa"
		void ReadColor(lua_State* L, int idx, float out[4], const float def[4])
		{
			for (int i = 0; i < 4; ++i) out[i] = def[i];
			if (lua_type(L, idx) == LUA_TSTRING)
			{
				const std::string h = lua_tostring(L, idx);
				const size_t o = !h.empty() && h[0] == '#' ? 1 : 0;
				if (h.size() - o >= 6)
				{
					for (int i = 0; i < 3; ++i) out[i] = (HexNibble(h[o + (size_t)i * 2]) * 16 + HexNibble(h[o + (size_t)i * 2 + 1])) / 255.0f;
					if (h.size() - o >= 8) out[3] = (HexNibble(h[o + 6]) * 16 + HexNibble(h[o + 7])) / 255.0f;
				}
			}
			else if (lua_istable(L, idx))
			{
				const int n = (int)lua_rawlen(L, idx);
				for (int i = 1; i <= std::min(4, n); ++i)
				{
					lua_rawgeti(L, idx, i);
					out[i - 1] = (float)lua_tonumber(L, -1);
					lua_pop(L, 1);
				}
			}
		}
		float OptField(lua_State* L, int tbl, const char* k, float def)
		{
			if (!lua_istable(L, tbl)) return def;
			lua_getfield(L, tbl, k);
			const float v = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : def;
			lua_pop(L, 1);
			return v;
		}
		bool OptFlag(lua_State* L, int tbl, const char* k, bool def)
		{
			if (!lua_istable(L, tbl)) return def;
			lua_getfield(L, tbl, k);
			const bool v = lua_isnil(L, -1) ? def : lua_toboolean(L, -1) != 0;
			lua_pop(L, 1);
			return v;
		}
		int Ui_screen(lua_State* L)
		{
			lua_pushnumber(L, g_host->ScreenW());
			lua_pushnumber(L, g_host->ScreenH());
			return 2;
		}
		// rect(x, y, w, h, color [, {rounding=0, outline=0}])
		int Ui_rect(lua_State* L)
		{
			UiCmd c;
			c.kind = UiCmd::Kind::Rect;
			c.x = (float)luaL_checknumber(L, 1); c.y = (float)luaL_checknumber(L, 2);
			c.w = (float)luaL_checknumber(L, 3); c.h = (float)luaL_checknumber(L, 4);
			static const float white[4] = {1, 1, 1, 1};
			ReadColor(L, 5, c.color, white);
			c.rounding = OptField(L, 6, "rounding", 0);
			c.thickness = OptField(L, 6, "outline", 0);
			g_host->PushUi(std::move(c));
			return 0;
		}
		// text(x, y, text [, {size=18, color=white, align="left"|"center"|"right", shadow=true}])
		int Ui_text(lua_State* L)
		{
			UiCmd c;
			c.kind = UiCmd::Kind::Text;
			c.x = (float)luaL_checknumber(L, 1); c.y = (float)luaL_checknumber(L, 2);
			c.text = luaL_tolstring(L, 3, nullptr);
			lua_pop(L, 1);
			c.size = OptField(L, 4, "size", 18);
			c.shadow = OptFlag(L, 4, "shadow", true);
			static const float white[4] = {1, 1, 1, 1};
			if (lua_istable(L, 4)) { lua_getfield(L, 4, "color"); ReadColor(L, -1, c.color, white); lua_pop(L, 1); }
			if (lua_istable(L, 4))
			{
				lua_getfield(L, 4, "align");
				const char* a = lua_isstring(L, -1) ? lua_tostring(L, -1) : "left";
				c.align = !strcmp(a, "center") ? 1 : (!strcmp(a, "right") ? 2 : 0);
				lua_pop(L, 1);
			}
			g_host->PushUi(std::move(c));
			return 0;
		}
		// image(path, x, y, w, h [, tint])
		int Ui_image(lua_State* L)
		{
			UiCmd c;
			c.kind = UiCmd::Kind::Image;
			c.text = luaL_checkstring(L, 1);
			c.x = (float)luaL_checknumber(L, 2); c.y = (float)luaL_checknumber(L, 3);
			c.w = (float)luaL_checknumber(L, 4); c.h = (float)luaL_checknumber(L, 5);
			static const float white[4] = {1, 1, 1, 1};
			ReadColor(L, 6, c.color, white);
			g_host->PushUi(std::move(c));
			return 0;
		}
		// line(x1, y1, x2, y2 [, color [, thickness]])
		int Ui_line(lua_State* L)
		{
			UiCmd c;
			c.kind = UiCmd::Kind::Line;
			c.x = (float)luaL_checknumber(L, 1); c.y = (float)luaL_checknumber(L, 2);
			c.w = (float)luaL_checknumber(L, 3); c.h = (float)luaL_checknumber(L, 4);
			static const float white[4] = {1, 1, 1, 1};
			ReadColor(L, 5, c.color, white);
			c.thickness = (float)luaL_optnumber(L, 6, 1);
			g_host->PushUi(std::move(c));
			return 0;
		}
		// mouse() -> x, y, left_down, left_pressed, right_down, right_pressed (x,y in game screen pixels)
		int Ui_mouse(lua_State* L)
		{
			float x, y;
			g_host->PointerLogical(x, y);
			lua_pushnumber(L, x);
			lua_pushnumber(L, y);
			lua_pushboolean(L, wi::input::Down(wi::input::MOUSE_BUTTON_LEFT));
			lua_pushboolean(L, wi::input::Press(wi::input::MOUSE_BUTTON_LEFT));
			lua_pushboolean(L, wi::input::Down(wi::input::MOUSE_BUTTON_RIGHT));
			lua_pushboolean(L, wi::input::Press(wi::input::MOUSE_BUTTON_RIGHT));
			return 6;
		}
		int Gm_quit(lua_State*) { if (g_host->Context().quit) g_host->Context().quit("quit"); return 0; }
		int Gm_time(lua_State* L) { lua_pushnumber(L, g_host->Elapsed()); return 1; }
		int Gm_dt(lua_State* L) { lua_pushnumber(L, g_host->Dt()); return 1; }

		void SetFuncs(lua_State* L, const luaL_Reg* fns) { luaL_setfuncs(L, fns, 0); }

		void OpenLumenApi(lua_State* L)
		{
			// metatables
			static const luaL_Reg entityMethods[] = {
				{"name", En_name}, {"id", En_id}, {"valid", En_valid}, {"position", En_position}, {"set_position", En_set_position},
				{"local_position", En_local_position}, {"set_local_position", En_set_local_position}, {"translate", En_translate},
				{"rotation", En_rotation}, {"set_rotation", En_set_rotation}, {"local_rotation", En_local_rotation},
				{"set_local_rotation", En_set_local_rotation}, {"scale", En_scale}, {"set_scale", En_set_scale}, {"forward", En_forward},
				{"right", En_right}, {"up", En_up}, {"look_at", En_look_at}, {"set_active", En_set_active}, {"parent", En_parent},
				{"child", En_child}, {"children", En_children}, {"destroy", En_destroy}, {"velocity", En_velocity},
				{"set_velocity", En_set_velocity}, {"add_force", En_add_force}, {"add_impulse", En_add_impulse}, {"move", En_move},
				{"grounded", En_grounded}, {"set_angular_velocity", En_angular_velocity_set}, {"add_torque", En_add_torque},
				{"add_force_at", En_add_force_at}, {"add_impulse_at", En_add_impulse_at}, {"wake", En_wake}, {"sleep", En_sleep}, {"drive", En_drive},
				{"speed", En_speed}, {"break_joint", En_break_joint}, {"joint_broken", En_joint_broken}, {"animations", En_animations}, {"play", En_play}, {"stop_animation", En_stop_animation}, {"animation", En_animation},
				{"set_animation", En_set_animation}, {"light", En_light}, {"set_color", En_color},
				{"play_clip", En_play_clip}, {"stop_clips", En_stop_clips}, {"agent_move_to", En_agent_move_to}, {"agent_stop", En_agent_stop}, {"agent_set_speed", En_agent_set_speed}, {"agent_moving", En_agent_moving},
				{"agent_remaining", En_agent_remaining}, {"agent_velocity", En_agent_velocity}, {nullptr, nullptr}};
			luaL_newmetatable(L, kEntityMeta);
			lua_newtable(L);
			SetFuncs(L, entityMethods);
			lua_setfield(L, -2, "__index");
			lua_pushcfunction(L, En_eq); lua_setfield(L, -2, "__eq");
			lua_pushcfunction(L, En_tostring); lua_setfield(L, -2, "__tostring");
			lua_pop(L, 1);
			luaL_newmetatable(L, kLightMeta);
			lua_pushcfunction(L, Li_index); lua_setfield(L, -2, "__index");
			lua_pushcfunction(L, Li_newindex); lua_setfield(L, -2, "__newindex");
			lua_pop(L, 1);

			lua_newtable(L); // events registry
			lua_setfield(L, LUA_REGISTRYINDEX, "lumen.events");

			lua_newtable(L); // lumen
			// math / random / noise / ... modules are exposed as lumen.<name>
			static const char* modules[] = {"mathx", "random", "noise", "geom", "color", "json", "hash", "regex", "utf8x", "fs", "http", "time", "net", "system", "zip", "compress", "process"};
			for (const char* m : modules)
			{
				const std::string req = std::string("lumen.") + m;
				lua_getglobal(L, "require");
				lua_pushstring(L, req.c_str());
				if (lua_pcall(L, 1, 1, 0) == LUA_OK)
					lua_setfield(L, -2, m);
				else
					lua_pop(L, 1);
			}
			lua_getfield(L, -1, "mathx");
			for (const char* f : {"vec2", "vec3", "vec4", "quat", "mat4"})
			{
				lua_getfield(L, -1, f);
				lua_setfield(L, -3, f);
			}
			lua_pop(L, 1);
			auto sub = [&](const char* name, const luaL_Reg* fns) {
				lua_newtable(L);
				SetFuncs(L, fns);
				lua_setfield(L, -2, name);
			};
			static const luaL_Reg scene[] = {{"find", Sc_find}, {"find_all", Sc_find_all}, {"find_by_tag", Sc_find_by_tag}, {"spawn", Sc_spawn},
				{"raycast", Sc_raycast}, {"instantiate", Sc_instantiate}, {"load", Sc_load}, {"gravity", Sc_gravity}, {nullptr, nullptr}};
			static const luaL_Reg input[] = {{"down", In_down}, {"pressed", In_pressed}, {"mouse_delta", In_mouse_delta}, {"mouse_pos", In_mouse_pos},
				{"wheel", In_wheel}, {"set_cursor_locked", In_set_cursor_locked}, {"cursor_locked", In_cursor_locked}, {nullptr, nullptr}};
			static const luaL_Reg logf[] = {{"info", Lg_info}, {"warn", Lg_warn}, {"error", Lg_error}, {"debug", Lg_debug}, {nullptr, nullptr}};
			static const luaL_Reg debug[] = {{"text", Db_text}, {"line", Db_line}, {"breakpoint", Db_breakpoint}, {nullptr, nullptr}};
			static const luaL_Reg events[] = {{"on", Ev_on}, {"emit", Ev_emit}, {nullptr, nullptr}};
			static const luaL_Reg game[] = {{"quit", Gm_quit}, {"elapsed", Gm_time}, {"delta_time", Gm_dt}, {nullptr, nullptr}};
			static const luaL_Reg physics[] = {{"raycast", Ph_raycast}, {"overlap_sphere", Ph_overlap_sphere}, {"overlap_box", Ph_overlap_box}, {"gravity", Sc_gravity}, {nullptr, nullptr}};
			static const luaL_Reg uif[] = {{"screen", Ui_screen}, {"rect", Ui_rect}, {"text", Ui_text}, {"image", Ui_image}, {"line", Ui_line}, {"mouse", Ui_mouse}, {nullptr, nullptr}};
			static const luaL_Reg navf[] = {{"is_baked", Nv_baked}, {"bake", Nv_bake}, {"find_path", Nv_find_path}, {"project", Nv_project},
				{"raycast", Nv_raycast}, {"random_point", Nv_random_point}, {nullptr, nullptr}};
			static const luaL_Reg animf[] = {{"play", An_play}, {"stop", An_stop}, {"set", An_set}, {"time", An_time}, {nullptr, nullptr}};
			sub("anim", animf);
			sub("nav", navf);
			sub("ui", uif);
			sub("physics", physics);
			sub("scene", scene);
			sub("input", input);
			sub("log", logf);
			sub("debug", debug);
			sub("events", events);
			SetFuncs(L, game);
			lua_pushstring(L, LUMEN_VERSION_STRING);
			lua_setfield(L, -2, "version");
			lua_setglobal(L, "lumen");
		}


		// ---- C ABI services for native modules (lumen_c.h) ------------------------------------------------
		struct CVec3 { float x, y, z; };
		struct CHost
		{
			uint32_t version;
			void (*log)(int, const char*);
			double (*time)();
			double (*delta_time)();
			int (*entity_find)(const char*, char*, size_t);
			int (*entity_get_position)(const char*, CVec3*);
			int (*entity_set_position)(const char*, CVec3);
			int (*entity_get_rotation)(const char*, CVec3*);
			int (*entity_set_rotation)(const char*, CVec3);
			int (*key_down)(const char*);
		};
		void C_log(int level, const char* m)
		{
			if (level <= 0) LUMEN_DEBUG("native", "%s", m);
			else if (level == 1) LUMEN_INFO("native", "%s", m);
			else if (level == 2) LUMEN_WARN("native", "%s", m);
			else LUMEN_ERROR("native", "%s", m);
		}
		double C_time() { return g_host->Elapsed(); }
		double C_dt() { return g_host->Dt(); }
		Entity EntityById(const char* id) { return g_host->Context().bridge->Find(id ? id : ""); }
		int C_find(const char* name, char* out, size_t cap)
		{
			for (const SceneEntity& e : g_host->Context().doc->entities)
				if (e.name == name && out && cap > e.id.size())
				{
					memcpy(out, e.id.c_str(), e.id.size() + 1);
					return 1;
				}
			return 0;
		}
		int C_getpos(const char* id, CVec3* o)
		{
			const Entity e = EntityById(id);
			if (e == INVALID_ENTITY || !S().transforms.Contains(e) || !o) return 0;
			const XMFLOAT3 p = WorldPos(e);
			*o = {p.x, p.y, p.z};
			return 1;
		}
		int C_setpos(const char* id, CVec3 v)
		{
			const Entity e = EntityById(id);
			if (e == INVALID_ENTITY || !S().transforms.Contains(e)) return 0;
			SetWorldPosition(e, XMFLOAT3(v.x, v.y, v.z));
			return 1;
		}
		int C_getrot(const char* id, CVec3* o)
		{
			const Entity e = EntityById(id);
			if (e == INVALID_ENTITY || !S().transforms.Contains(e) || !o) return 0;
			const XMFLOAT3 r = EulerDeg(WorldRot(e));
			*o = {r.x, r.y, r.z};
			return 1;
		}
		int C_setrot(const char* id, CVec3 v)
		{
			const Entity e = EntityById(id);
			if (e == INVALID_ENTITY || !S().transforms.Contains(e)) return 0;
			SetWorldRotation(e, QuatFromDeg(XMFLOAT3(v.x, v.y, v.z)));
			return 1;
		}
		int C_key(const char* k)
		{
			const auto b = KeyFor(str::ToLower(k ? k : ""));
			return b != wi::input::BUTTON_NONE && wi::input::Down(b);
		}
		const CHost kCHost = {1, C_log, C_time, C_dt, C_find, C_getpos, C_setpos, C_getrot, C_setrot, C_key};

		std::string PackagePath(const Project& p)
		{
			std::string path;
			auto add = [&](const std::string& dir) {
				path += dir + "/?.lua;" + dir + "/?/init.lua;";
			};
			add(p.Root());
			for (const std::string& d : p.Settings().scripts.dirs)
				add(p.Resolve(d));
			if (Runtime::IsInitialized())
				add(Runtime::Layout().lua);
			return path;
		}

		uint64_t Mtime(const std::string& p) { return fs::LastWriteTimeMs(p); }
	} // namespace

	void PlayHost::Report(const std::string& where, const char* msg)
	{
		errors_.push_back(where + ": " + (msg ? msg : "error"));
		LUMEN_ERROR("game", "%s: %s", where.c_str(), msg ? msg : "error");
	}

	void PlayHost::SetCursorLocked(bool v)
	{
		cursorLocked_ = v;
		wi::input::HidePointer(v);
		if (ctx_.lockCursor)
			ctx_.lockCursor(v);
	}

	static int Traceback(lua_State* L)
	{
		const char* msg = lua_tostring(L, 1);
		luaL_traceback(L, L, msg ? msg : "error", 1);
		return 1;
	}

	static bool PCall(lua_State* L, int nargs, int nres, std::string& err)
	{
		const int base = lua_gettop(L) - nargs;
		lua_pushcfunction(L, Traceback);
		lua_insert(L, base);
		const int rc = lua_pcall(L, nargs, nres, base);
		lua_remove(L, base);
		if (rc != LUA_OK)
		{
			err = lua_tostring(L, -1) ? lua_tostring(L, -1) : "error";
			lua_pop(L, 1);
			return false;
		}
		return true;
	}

	Status PlayHost::Start(const PlayContext& ctx)
	{
		Stop();
		ctx_ = ctx;
		errors_.clear();
		g_host = this;
		elapsed_ = 0;
		L_ = luaL_newstate();
		luaL_openlibs(L_);
		lua::LibsPolicy policy;
		policy.fsRoots = {ctx.project->Root()};
		lua::OpenLibs(L_, policy);
		const std::string pp = PackagePath(*ctx.project);
		lua_getglobal(L_, "package");
		lua_pushstring(L_, pp.c_str());
		lua_setfield(L_, -2, "path");
		lua_pop(L_, 1);
		OpenLumenApi(L_);
		if (ctx.project->Settings().scripts.debug)
		{
			debugger_.Attach(L_);
			if (ctx.debuggerSetup)
				ctx.debuggerSetup(debugger_);
		}
		// native module (built by the Build panel); a missing build is not an error
		if (ctx.project->Settings().native.enabled)
		{
			const std::string lib = fs::Join(ctx.project->NativeBinDir(), platform::SharedLibraryFileName(ctx.project->Settings().native.moduleName));
			if (fs::Exists(lib))
			{
				Status ns = native_.Open(lib, L_, &kCHost);
				if (!ns) Report("native module", ns.message.c_str());
			}
		}

		// main script
		const std::string mainFile = ctx.project->Resolve(ctx.project->Settings().mainScript);
		if (fs::Exists(mainFile))
		{
			mainMtime_ = Mtime(mainFile);
			std::string err;
			if (luaL_loadfile(L_, mainFile.c_str()) != LUA_OK)
			{
				Report(ctx.project->Settings().mainScript, lua_tostring(L_, -1));
				lua_pop(L_, 1);
			}
			else if (!PCall(L_, 0, 1, err))
				Report(ctx.project->Settings().mainScript, err.c_str());
			else if (lua_istable(L_, -1))
				gameRef_ = luaL_ref(L_, LUA_REGISTRYINDEX);
			else
				lua_pop(L_, 1);
		}
		// navigation mesh and agents
		nav_.Reset();
		for (const SceneEntity& se : ctx.doc->entities)
			if (se.navmesh && se.navmesh->bakeOnStart)
			{
				nav_.Bake(*ctx.scene, *ctx.bridge, *ctx.doc);
				break;
			}
		// entity scripts and trigger volumes
		triggers_.clear();
		for (const SceneEntity& se : ctx.doc->entities)
		{
			AddNavAgent(se);
			AttachScripts(se);
			AddTrigger(se);
		}
		// animator autoplay
		for (const SceneEntity& se : ctx.doc->entities)
		{
			if (!se.animator || se.animator->autoplay.empty() || !se.active)
				continue;
			for (Entity clip : ctx.bridge->Animations(se.id))
			{
				const NameComponent* n = ctx.scene->names.GetComponent(clip);
				AnimationComponent* a = ctx.scene->animations.GetComponent(clip);
				if (a && n && n->name == se.animator->autoplay)
				{
					a->timer = a->start;
					a->Play();
				}
			}
		}
		for (const SceneEntity& se : ctx.doc->entities)
			if (se.animator && !se.animator->autoplayClip.empty() && se.active)
				if (Entity e = ctx.bridge->Find(se.id); e != INVALID_ENTITY)
					PlayClip(e, se.animator->autoplayClip, se.animator->autoplayLoop, 1.0f, 1.0f);
		// start callbacks: game first, then entity scripts
		Call(gameRef_, "start", 0, 0, false);
		for (Instance& in : instances_)
		{
			Call(in.ref, "start", 0);
			in.started = true;
		}
		return errors_.empty() ? Status::Ok() : Status::Fail(errors_.front());
	}

	// Creates the script instances of one entity (play start and runtime instantiation). `start` is called by the caller.
	size_t PlayHost::AttachScripts(const SceneEntity& se)
	{
		const size_t before = instances_.size();
		for (const SceneScript& sc : se.scripts)
		{
			if (!sc.enabled)
				continue;
			const Entity ent = ctx_.bridge->Find(se.id);
			if (ent == INVALID_ENTITY)
				continue;
			const std::string file = ctx_.project->Resolve(sc.file);
			std::string err;
			if (luaL_loadfile(L_, file.c_str()) != LUA_OK)
			{
				Report(sc.file, lua_tostring(L_, -1));
				lua_pop(L_, 1);
				continue;
			}
			if (!PCall(L_, 0, 1, err))
			{
				Report(sc.file, err.c_str());
				continue;
			}
			if (!lua_istable(L_, -1))
			{
				Report(sc.file, "script must return a table");
				lua_pop(L_, 1);
				continue;
			}
			// instance = copy of class defaults + inspector properties, metatable falls back to the class
			const int cls = lua_gettop(L_);
			lua_newtable(L_);
			for (lua_pushnil(L_); lua_next(L_, cls) != 0; lua_pop(L_, 1))
				if (!lua_isfunction(L_, -1))
				{
					lua_pushvalue(L_, -2);
					lua_pushvalue(L_, -2);
					lua_settable(L_, -5);
				}
			for (const Json::Member& m : sc.properties.Members())
			{
				lua_pushstring(L_, m.first.c_str());
				if (m.second.IsNumber()) lua_pushnumber(L_, m.second.AsDouble());
				else if (m.second.IsBool()) lua_pushboolean(L_, m.second.AsBool());
				else if (m.second.IsString()) lua_pushstring(L_, m.second.AsString().c_str());
				else { lua_pop(L_, 1); continue; }
				lua_settable(L_, -3);
			}
			lua_newtable(L_);
			lua_pushvalue(L_, cls);
			lua_setfield(L_, -2, "__index");
			lua_setmetatable(L_, -2);
			PushEntity(L_, ent);
			lua_setfield(L_, -2, "entity");
			lua_pushstring(L_, se.id.c_str());
			lua_setfield(L_, -2, "entity_id");
			const int ref = luaL_ref(L_, LUA_REGISTRYINDEX);
			lua_pop(L_, 1); // class table
			instances_.push_back({se.id, sc.file, ref, Mtime(file), false, false});
		}
		return instances_.size() - before;
	}

	void PlayHost::AddTrigger(const SceneEntity& se)
	{
		if (se.rigidbody && se.rigidbody->trigger)
		{
			Trigger t;
			t.id = se.id;
			t.e = ctx_.bridge->Find(se.id);
			Float3 ext = se.rigidbody->shapeSize;
			if (ext == Float3{0, 0, 0}) ext = se.mesh ? se.mesh->size : Float3{1, 1, 1};
			t.half = Float3{ext.x * 0.5f, ext.y * 0.5f, ext.z * 0.5f};
			t.offset = se.rigidbody->shapeOffset;
			t.sphere = se.rigidbody->shape == "sphere";
			if (t.e != INVALID_ENTITY) triggers_.push_back(std::move(t));
		}
	}

	Entity PlayHost::Instantiate(const std::string& prefabPath, const Float3* position, const Float3* rotation)
	{
		std::string text;
		Json j;
		if (!fs::ReadText(ctx_.project->Resolve(prefabPath), text) || !Json::Parse(text, j) || !SceneDoc::IsPrefabJson(j))
		{
			AddError("instantiate: cannot read prefab " + prefabPath);
			return INVALID_ENTITY;
		}
		SceneDoc tmp;
		const std::string rootId = tmp.ImportSubtree(j, "");
		if (rootId.empty())
			return INVALID_ENTITY;
		if (SceneEntity* r = tmp.Find(rootId))
		{
			if (position) r->transform.position = *position;
			if (rotation) r->transform.rotation = *rotation;
		}
		const std::vector<Entity> made = ctx_.bridge->Instantiate(*ctx_.scene, tmp.entities);
		const size_t firstInstance = instances_.size();
		for (const SceneEntity& se : tmp.entities)
		{
			AddNavAgent(se);
			AttachScripts(se);
			AddTrigger(se);
		}
		for (size_t i = firstInstance; i < instances_.size(); ++i)
		{
			Call(instances_[i].ref, "start", 0);
			instances_[i].started = true;
		}
		return ctx_.bridge->Find(rootId);
	}

	void PlayHost::Call(int ref, const char* method, int, double a, bool pushSelf)
	{
		if (!L_ || ref == 0)
			return;
		lua_rawgeti(L_, LUA_REGISTRYINDEX, ref);
		lua_getfield(L_, -1, method);
		if (!lua_isfunction(L_, -1))
		{
			lua_pop(L_, 2);
			return;
		}
		int n = 0;
		if (pushSelf) { lua_pushvalue(L_, -2); ++n; }
		const bool wantsDt = std::string(method).find("update") != std::string::npos;
		if (wantsDt) { lua_pushnumber(L_, a); ++n; }
		std::string err;
		if (!PCall(L_, n, 0, err))
			Report(method, err.c_str());
		lua_pop(L_, 1);
	}

	void PlayHost::Update(float dt)
	{
		if (!L_)
			return;
		dt_ = dt;
		elapsed_ += dt;
		if (cursorLocked_)
		{
			const XMFLOAT4 p = wi::input::GetPointer();
			mdx_ = p.x - ctx_.viewportCenterX;
			mdy_ = p.y - ctx_.viewportCenterY;
			wi::input::SetPointer(XMFLOAT4(ctx_.viewportCenterX, ctx_.viewportCenterY, 0, 0));
		}
		else
			mdx_ = mdy_ = 0;
		UpdateFades(dt);
		UpdateNav(dt);
		UpdateClips(dt);
		ui_.clear();
		native_.Update(dt);
		Call(gameRef_, "update", 0, dt, false);
		for (Instance& in : instances_)
		{
			if (in.failed)
				continue;
			const size_t before = errors_.size();
			Call(in.ref, "update", 0, dt);
			if (errors_.size() != before)
				in.failed = true; // a script that raised an error is paused until reloaded
		}
	}

	void PlayHost::FadeAnimation(Entity clip, float target, float duration, bool stopAtEnd)
	{
		for (Fade& f : fades_) if (f.clip == clip) { f = fades_.back(); fades_.pop_back(); break; }
		AnimationComponent* a = ctx_.scene->animations.GetComponent(clip);
		if (!a) return;
		if (duration <= 0)
		{
			a->amount = target;
			if (stopAtEnd && target <= 0.0f) a->Stop();
			return;
		}
		fades_.push_back({clip, a->amount, target, duration, 0.0f, stopAtEnd});
	}

	void PlayHost::UpdateFades(float dt)
	{
		for (size_t i = 0; i < fades_.size();)
		{
			Fade& f = fades_[i];
			AnimationComponent* a = ctx_.scene->animations.GetComponent(f.clip);
			if (!a) { fades_[i] = fades_.back(); fades_.pop_back(); continue; }
			f.t += dt;
			const float k = std::min(1.0f, f.t / f.duration);
			const float s = k * k * (3.0f - 2.0f * k); // smoothstep
			a->amount = f.from + (f.to - f.from) * s;
			if (k >= 1.0f)
			{
				if (f.stopAtEnd && f.to <= 0.0f) a->Stop();
				fades_[i] = fades_.back();
				fades_.pop_back();
				continue;
			}
			++i;
		}
	}

	void PlayHost::PointerLogical(float& x, float& y) const
	{
		const XMFLOAT4 p = wi::input::GetPointer();
		const float d = dpi_ > 0 ? dpi_ : 1.0f;
		x = p.x / d - originX_;
		y = p.y / d - originY_;
	}

	const wi::graphics::Texture* PlayHost::UiTexture(const std::string& path)
	{
		auto it = uiImages_.find(path);
		if (it == uiImages_.end())
		{
			const std::string abs = ctx_.bridge ? ctx_.bridge->ResolveAsset(path) : path;
			wi::Resource r;
			if (fs::Exists(abs)) r = wi::resourcemanager::Load(abs);
			it = uiImages_.emplace(path, std::move(r)).first;
		}
		return it->second.IsValid() ? &it->second.GetTexture() : nullptr;
	}

	void PlayHost::NotifyTrigger(const char* method, Entity self, Entity other)
	{
		const std::string id = ctx_.bridge->IdOf(self);
		for (Instance& in : instances_)
		{
			if (in.failed || in.entityId != id) continue;
			lua_rawgeti(L_, LUA_REGISTRYINDEX, in.ref);
			lua_getfield(L_, -1, method);
			if (lua_isfunction(L_, -1))
			{
				lua_pushvalue(L_, -2);
				PushEntity(L_, other);
				std::string err;
				if (!PCall(L_, 2, 0, err)) Report(method, err.c_str());
			}
			else
				lua_pop(L_, 1);
			lua_pop(L_, 1);
		}
	}

	void PlayHost::AddNavAgent(const SceneEntity& se)
	{
		if (!se.navagent || !se.active)
			return;
		const Entity e = ctx_.bridge->Find(se.id);
		if (e != INVALID_ENTITY)
			nav_.AddAgent(*ctx_.scene, e, *se.navagent);
	}

	void PlayHost::NotifyEntity(const char* method, Entity self)
	{
		const std::string id = ctx_.bridge->IdOf(self);
		for (Instance& in : instances_)
		{
			if (in.failed || in.entityId != id) continue;
			lua_rawgeti(L_, LUA_REGISTRYINDEX, in.ref);
			lua_getfield(L_, -1, method);
			if (lua_isfunction(L_, -1))
			{
				lua_pushvalue(L_, -2);
				std::string err;
				if (!PCall(L_, 1, 0, err)) Report(method, err.c_str());
			}
			else
				lua_pop(L_, 1);
			lua_pop(L_, 1);
		}
	}

	void PlayHost::NotifyEntityStr(const char* method, Entity self, const std::string& arg)
	{
		const std::string id = ctx_.bridge->IdOf(self);
		for (Instance& in : instances_)
		{
			if (in.failed || in.entityId != id) continue;
			lua_rawgeti(L_, LUA_REGISTRYINDEX, in.ref);
			lua_getfield(L_, -1, method);
			if (lua_isfunction(L_, -1))
			{
				lua_pushvalue(L_, -2);
				lua_pushstring(L_, arg.c_str());
				std::string err;
				if (!PCall(L_, 2, 0, err)) Report(method, err.c_str());
			}
			else
				lua_pop(L_, 1);
			lua_pop(L_, 1);
		}
	}

	int PlayHost::PlayClip(Entity root, const std::string& path, bool loop, float speed, float weight)
	{
		if (!ctx_.project || !ctx_.scene) return 0;
		anim::Clip clip;
		std::string err;
		if (!anim::Load(ctx_.project->Resolve(path), clip, &err))
		{
			LUMEN_WARN("anim", "cannot load '%s': %s", path.c_str(), err.c_str());
			return 0;
		}
		return clips_.Play(*ctx_.scene, root, clip, path, loop || clip.loop, speed, weight);
	}

	void PlayHost::UpdateClips(float dt)
	{
		if (clips_.Count() == 0 || !ctx_.scene) return;
		ClipSystem::Callbacks cb;
		cb.onEvent = [this](const ClipSystem::Instance& in, const std::string& name) { NotifyEntityStr("on_anim_event", in.root, name); };
		cb.onEnd = [this](const ClipSystem::Instance& in) { NotifyEntityStr("on_anim_end", in.root, in.path); };
		clips_.Update(*ctx_.scene, dt, cb);
	}

	void PlayHost::UpdateNav(float dt)
	{
		NavSystem::Hooks h;
		h.setPosition = [](Entity e, const XMFLOAT3& p) { SetWorldPosition(e, p); };
		h.setYaw = [](Entity e, float yaw) { XMFLOAT4 q; XMStoreFloat4(&q, XMQuaternionRotationRollPitchYaw(0, yaw, 0)); SetWorldRotation(e, q); };
		h.arrived = [this](Entity e) { NotifyEntity("on_nav_arrive", e); EmitEvent(L_, "nav_arrive", e, INVALID_ENTITY); };
		h.failed = [this](Entity e) { NotifyEntity("on_nav_failed", e); EmitEvent(L_, "nav_failed", e, INVALID_ENTITY); };
		nav_.Update(*ctx_.scene, dt, h);
		if (navDebug)
			nav_.DrawDebug(*ctx_.scene, true, true);
	}

	void PlayHost::UpdateTriggers()
	{
		if (triggers_.empty()) return;
		Scene& sc = *ctx_.scene;
		const std::string evName = "trigger_enter";
		for (Trigger& t : triggers_)
		{
			const TransformComponent* tr = sc.transforms.GetComponent(t.e);
			if (!tr) continue;
			const XMFLOAT3 c(t.offset.x, t.offset.y, t.offset.z);
			wi::primitive::AABB local(XMFLOAT3(c.x - t.half.x, c.y - t.half.y, c.z - t.half.z), XMFLOAT3(c.x + t.half.x, c.y + t.half.y, c.z + t.half.z));
			const wi::primitive::AABB box = local.transform(tr->world);
			std::unordered_set<Entity> now;
			for (size_t i = 0; i < sc.rigidbodies.GetCount(); ++i)
			{
				const Entity o = sc.rigidbodies.GetEntity(i);
				if (o == t.e) continue;
				bool hit = false;
				const int oi = (int)sc.objects.GetIndex(o);
				if (oi >= 0 && (size_t)oi < sc.aabb_objects.size()) hit = box.intersects(sc.aabb_objects[oi]);
				else if (const TransformComponent* ot = sc.transforms.GetComponent(o)) hit = box.intersects(ot->GetPosition());
				if (hit) now.insert(o);
			}
			for (Entity o : now)
				if (!t.inside.count(o))
				{
					NotifyTrigger("on_trigger_enter", t.e, o);
					NotifyTrigger("on_trigger_enter", o, t.e);
					EmitEvent(L_, "trigger_enter", t.e, o);
				}
			for (Entity o : t.inside)
				if (!now.count(o))
				{
					NotifyTrigger("on_trigger_exit", t.e, o);
					NotifyTrigger("on_trigger_exit", o, t.e);
					EmitEvent(L_, "trigger_exit", t.e, o);
				}
			t.inside = std::move(now);
		}
	}

	void PlayHost::FixedUpdate(float dt)
	{
		if (!L_)
			return;
		native_.FixedUpdate(dt);
		UpdateTriggers();
		Call(gameRef_, "fixed_update", 0, dt, false);
		for (Instance& in : instances_)
			if (!in.failed)
				Call(in.ref, "fixed_update", 0, dt);
	}

	void PlayHost::HotReload()
	{
		if (!L_ || !ctx_.project->Settings().scripts.hotReload)
			return;
		for (Instance& in : instances_)
		{
			const std::string file = ctx_.project->Resolve(in.file);
			const uint64_t mt = Mtime(file);
			if (mt == in.mtime)
				continue;
			in.mtime = mt;
			std::string err;
			if (luaL_loadfile(L_, file.c_str()) != LUA_OK)
			{
				Report(in.file, lua_tostring(L_, -1));
				lua_pop(L_, 1);
				continue;
			}
			if (!PCall(L_, 0, 1, err) || !lua_istable(L_, -1))
			{
				if (err.empty()) { err = "script must return a table"; lua_pop(L_, 1); }
				Report(in.file, err.c_str());
				continue;
			}
			// replace the class the instance falls back to; instance fields (state) are preserved
			lua_rawgeti(L_, LUA_REGISTRYINDEX, in.ref);
			lua_getmetatable(L_, -1);
			lua_pushvalue(L_, -3);
			lua_setfield(L_, -2, "__index");
			lua_pop(L_, 3);
			in.failed = false;
			LUMEN_INFO("game", "reloaded %s", in.file.c_str());
		}
	}

	void PlayHost::Stop()
	{
		if (!L_)
			return;
		Call(gameRef_, "shutdown", 0, 0, false);
		for (Instance& in : instances_)
			Call(in.ref, "on_destroy", 0);
		SetCursorLocked(false);
		triggers_.clear();
		nav_.Reset();
		clips_.StopAll();
		fades_.clear();
		ui_.clear();
		uiImages_.clear();
		native_.Close(L_);
		debugger_.Detach();
		lua_close(L_);
		L_ = nullptr;
		instances_.clear();
		gameRef_ = 0;
		if (g_host == this)
			g_host = nullptr;
	}
}
