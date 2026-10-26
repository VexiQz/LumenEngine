#include "lumen/core/SceneDoc.h"

#include "lumen/core/Fs.h"
#include "lumen/core/Log.h"
#include "lumen/core/Project.h"
#include "lumen/core/Strings.h"

#include <algorithm>
#include <set>

namespace lumen
{
	namespace
	{
		Json F3(const Float3& v) { return Json(Json::Array{Json(v.x), Json(v.y), Json(v.z)}); }
		Json F2(const Float2& v) { return Json(Json::Array{Json(v.x), Json(v.y)}); }
		Json F4(const Float4& v) { return Json(Json::Array{Json(v.x), Json(v.y), Json(v.z), Json(v.w)}); }
		Float3 RdF3(const Json& j, Float3 def)
		{
			if (!j.IsArray() || j.Size() < 3)
				return def;
			return {(float)j.At(0).AsDouble(def.x), (float)j.At(1).AsDouble(def.y), (float)j.At(2).AsDouble(def.z)};
		}
		Float2 RdF2(const Json& j, Float2 def)
		{
			if (!j.IsArray() || j.Size() < 2)
				return def;
			return {(float)j.At(0).AsDouble(def.x), (float)j.At(1).AsDouble(def.y)};
		}
		Float4 RdF4(const Json& j, Float4 def)
		{
			if (!j.IsArray() || j.Size() < 4)
				return def;
			return {(float)j.At(0).AsDouble(def.x), (float)j.At(1).AsDouble(def.y), (float)j.At(2).AsDouble(def.z), (float)j.At(3).AsDouble(def.w)};
		}
		float Rf(const Json& j, const char* k, float def) { return (float)j.GetDouble(k, def); }

		// copies keys of `src` that are not in `known` into `extra`
		Json Unknown(const Json& src, const std::set<std::string>& known)
		{
			Json extra;
			for (const Json::Member& m : src.Members())
				if (!known.count(m.first))
					extra[m.first] = m.second;
			return extra;
		}
		void MergeExtra(Json& dst, const Json& extra)
		{
			for (const Json::Member& m : extra.Members())
				if (!dst.Contains(m.first))
					dst[m.first] = m.second;
		}

		Json ToJson(const SceneTransform& t)
		{
			Json j = Json::MakeObject();
			if (t.position != Float3{0, 0, 0}) j["position"] = F3(t.position);
			if (t.rotation != Float3{0, 0, 0}) j["rotation"] = F3(t.rotation);
			if (t.scale != Float3{1, 1, 1}) j["scale"] = F3(t.scale);
			return j;
		}
		Json ToJson(const SceneMaterial& m)
		{
			Json j = Json::MakeObject();
			if (!m.name.empty()) j["name"] = m.name;
			j["shader"] = m.shader;
			j["color"] = F4(m.color);
			j["roughness"] = m.roughness;
			j["metalness"] = m.metalness;
			if (m.reflectance != 0.5f) j["reflectance"] = m.reflectance;
			if (m.emissiveStrength > 0) { j["emissive"] = F3(m.emissive); j["emissiveStrength"] = m.emissiveStrength; }
			const std::pair<const char*, const std::string*> maps[] = {{"baseColorMap", &m.baseColorMap}, {"normalMap", &m.normalMap}, {"roughnessMap", &m.roughnessMap},
				{"metalnessMap", &m.metalnessMap}, {"emissiveMap", &m.emissiveMap}, {"occlusionMap", &m.occlusionMap}};
			for (auto& mp : maps)
				if (!mp.second->empty()) j[mp.first] = *mp.second;
			if (m.tiling.x != 1 || m.tiling.y != 1) j["tiling"] = F2(m.tiling);
			if (m.shaderParams.IsObject() && !m.shaderParams.Empty()) j["shaderParams"] = m.shaderParams;
			if (m.doubleSided) j["doubleSided"] = true;
			if (!m.castShadows) j["castShadows"] = false;
			if (m.alphaBlend) j["alphaBlend"] = true;
			if (m.alphaCutoff > 0) j["alphaCutoff"] = m.alphaCutoff;
			return j;
		}
		SceneMaterial MaterialFrom(const Json& j)
		{
			SceneMaterial m;
			m.name = j.GetString("name");
			m.shader = j.GetString("shader", "pbr");
			m.color = RdF4(j.Get("color"), m.color);
			m.roughness = Rf(j, "roughness", m.roughness);
			m.metalness = Rf(j, "metalness", m.metalness);
			m.reflectance = Rf(j, "reflectance", m.reflectance);
			m.emissive = RdF3(j.Get("emissive"), m.emissive);
			m.emissiveStrength = Rf(j, "emissiveStrength", 0);
			m.baseColorMap = j.GetString("baseColorMap");
			m.normalMap = j.GetString("normalMap");
			m.roughnessMap = j.GetString("roughnessMap");
			m.metalnessMap = j.GetString("metalnessMap");
			m.emissiveMap = j.GetString("emissiveMap");
			m.occlusionMap = j.GetString("occlusionMap");
			m.tiling = RdF2(j.Get("tiling"), m.tiling);
			m.shaderParams = j.Get("shaderParams");
			m.doubleSided = j.GetBool("doubleSided", false);
			m.castShadows = j.GetBool("castShadows", true);
			m.alphaBlend = j.GetBool("alphaBlend", false);
			m.alphaCutoff = Rf(j, "alphaCutoff", 0);
			return m;
		}
		Json ToJson(const SceneMesh& m)
		{
			Json j = Json::MakeObject();
			if (m.kind == SceneMeshKind::Model)
				j["model"] = m.model;
			else
			{
				j["primitive"] = m.primitive;
				j["size"] = F3(m.size);
				if (m.segments != 32) j["segments"] = m.segments;
			}
			if (!m.castShadows) j["castShadows"] = false;
			if (!m.receiveShadows) j["receiveShadows"] = false;
			if (!m.visible) j["visible"] = false;
			if (m.cullDistance > 0) j["cullDistance"] = m.cullDistance;
			if (m.lodBias != 0) j["lodBias"] = m.lodBias;
			return j;
		}
		SceneMesh MeshFrom(const Json& j)
		{
			SceneMesh m;
			if (j.Contains("model"))
			{
				m.kind = SceneMeshKind::Model;
				m.model = j.GetString("model");
			}
			else
				m.primitive = j.GetString("primitive", "cube");
			m.size = RdF3(j.Get("size"), m.size);
			m.segments = (int)j.GetInt("segments", 32);
			m.castShadows = j.GetBool("castShadows", true);
			m.receiveShadows = j.GetBool("receiveShadows", true);
			m.visible = j.GetBool("visible", true);
			m.cullDistance = Rf(j, "cullDistance", 0);
			m.lodBias = (int)j.GetInt("lodBias", 0);
			return m;
		}
		Json ToJson(const SceneLight& l)
		{
			Json j = Json::MakeObject();
			j["type"] = l.type;
			j["color"] = F3(l.color);
			j["intensity"] = l.intensity;
			if (l.type != "directional") j["range"] = l.range;
			if (l.type == "spot") { j["innerAngle"] = l.innerAngle; j["outerAngle"] = l.outerAngle; }
			if (!l.castShadows) j["castShadows"] = false;
			if (l.volumetric > 0) j["volumetric"] = l.volumetric;
			return j;
		}
		SceneLight LightFrom(const Json& j)
		{
			SceneLight l;
			l.type = j.GetString("type", "point");
			l.color = RdF3(j.Get("color"), l.color);
			l.intensity = Rf(j, "intensity", l.intensity);
			l.range = Rf(j, "range", l.range);
			l.innerAngle = Rf(j, "innerAngle", l.innerAngle);
			l.outerAngle = Rf(j, "outerAngle", l.outerAngle);
			l.castShadows = j.GetBool("castShadows", true);
			l.volumetric = Rf(j, "volumetric", 0);
			return l;
		}
		Json ToJson(const SceneCamera& c)
		{
			Json j = Json::MakeObject();
			j["fov"] = c.fov;
			j["near"] = c.nearPlane;
			j["far"] = c.farPlane;
			if (c.main) j["main"] = true;
			return j;
		}
		SceneCamera CameraFrom(const Json& j)
		{
			SceneCamera c;
			c.fov = Rf(j, "fov", c.fov);
			c.nearPlane = Rf(j, "near", c.nearPlane);
			c.farPlane = Rf(j, "far", c.farPlane);
			c.main = j.GetBool("main", false);
			return c;
		}
		Json ToJson(const SceneRigidBody& r)
		{
			Json j = Json::MakeObject();
			j["type"] = r.type;
			j["shape"] = r.shape;
			j["mass"] = r.mass;
			j["friction"] = r.friction;
			j["restitution"] = r.restitution;
			if (r.linearDamping != 0.05f) j["linearDamping"] = r.linearDamping;
			if (r.angularDamping != 0.05f) j["angularDamping"] = r.angularDamping;
			if (r.trigger) j["trigger"] = true;
			if (r.lockRotation) j["lockRotation"] = true;
			if (r.continuous) j["continuous"] = true;
			if (r.shapeSize != Float3{0, 0, 0}) j["shapeSize"] = F3(r.shapeSize);
			if (r.shapeOffset != Float3{0, 0, 0}) j["shapeOffset"] = F3(r.shapeOffset);
			if (r.layer != 0) j["layer"] = r.layer;
			if (r.maxSlopeAngle != 50.0f) j["maxSlopeAngle"] = r.maxSlopeAngle;
			if (r.gravityFactor != 1.0f) j["gravityFactor"] = r.gravityFactor;
			if (r.buoyancy != 1.2f) j["buoyancy"] = r.buoyancy;
			if (r.lock2D) j["lock2D"] = true;
			if (r.startDeactivated) j["startDeactivated"] = true;
			if (r.alwaysActive) j["alwaysActive"] = true;
			return j;
		}
		Json ToJson(const SceneVehicle& v)
		{
			const SceneVehicle d;
			Json j = Json::MakeObject();
			j["type"] = v.type;
			if (v.collision != d.collision) j["collision"] = v.collision;
			j["chassis"] = F3(v.chassis);
			j["wheelRadius"] = v.wheelRadius;
			j["wheelWidth"] = v.wheelWidth;
			j["engineTorque"] = v.engineTorque;
			j["maxSteerAngle"] = v.maxSteerAngle;
			if (v.frontWheelOffset != 0) j["frontWheelOffset"] = v.frontWheelOffset;
			if (v.rearWheelOffset != 0) j["rearWheelOffset"] = v.rearWheelOffset;
			if (v.clutchStrength != d.clutchStrength) j["clutchStrength"] = v.clutchStrength;
			if (v.maxRollAngle != d.maxRollAngle) j["maxRollAngle"] = v.maxRollAngle;
			j["suspensionMin"] = v.suspensionMin;
			j["suspensionMax"] = v.suspensionMax;
			j["suspensionFrequency"] = v.suspensionFrequency;
			j["suspensionDamping"] = v.suspensionDamping;
			if (v.fourWheelDrive) j["fourWheelDrive"] = true;
			if (!v.leanControl) j["leanControl"] = false;
			if (!v.wheelFL.empty()) j["wheelFL"] = v.wheelFL;
			if (!v.wheelFR.empty()) j["wheelFR"] = v.wheelFR;
			if (!v.wheelRL.empty()) j["wheelRL"] = v.wheelRL;
			if (!v.wheelRR.empty()) j["wheelRR"] = v.wheelRR;
			return j;
		}
		SceneVehicle VehicleFrom(const Json& j)
		{
			SceneVehicle v;
			v.type = j.GetString("type", v.type);
			v.collision = j.GetString("collision", v.collision);
			v.chassis = RdF3(j.Get("chassis"), v.chassis);
			v.frontWheelOffset = Rf(j, "frontWheelOffset", v.frontWheelOffset);
			v.rearWheelOffset = Rf(j, "rearWheelOffset", v.rearWheelOffset);
			v.wheelRadius = Rf(j, "wheelRadius", v.wheelRadius);
			v.wheelWidth = Rf(j, "wheelWidth", v.wheelWidth);
			v.engineTorque = Rf(j, "engineTorque", v.engineTorque);
			v.clutchStrength = Rf(j, "clutchStrength", v.clutchStrength);
			v.maxSteerAngle = Rf(j, "maxSteerAngle", v.maxSteerAngle);
			v.maxRollAngle = Rf(j, "maxRollAngle", v.maxRollAngle);
			v.suspensionMin = Rf(j, "suspensionMin", v.suspensionMin);
			v.suspensionMax = Rf(j, "suspensionMax", v.suspensionMax);
			v.suspensionFrequency = Rf(j, "suspensionFrequency", v.suspensionFrequency);
			v.suspensionDamping = Rf(j, "suspensionDamping", v.suspensionDamping);
			v.fourWheelDrive = j.GetBool("fourWheelDrive", false);
			v.leanControl = j.GetBool("leanControl", true);
			v.wheelFL = j.GetString("wheelFL");
			v.wheelFR = j.GetString("wheelFR");
			v.wheelRL = j.GetString("wheelRL");
			v.wheelRR = j.GetString("wheelRR");
			return v;
		}
		Json ToJson(const SceneJoint& q)
		{
			const SceneJoint d;
			Json j = Json::MakeObject();
			j["type"] = q.type;
			if (!q.connected.empty()) j["connected"] = q.connected;
			auto put = [&](const char* k, float v, float def) { if (v != def) j[k] = v; };
			put("minDistance", q.minDistance, d.minDistance); put("maxDistance", q.maxDistance, d.maxDistance);
			put("minAngle", q.minAngle, d.minAngle); put("maxAngle", q.maxAngle, d.maxAngle);
			put("motorSpeed", q.motorSpeed, d.motorSpeed); put("coneAngle", q.coneAngle, d.coneAngle);
			put("swingNormal", q.swingNormal, d.swingNormal); put("swingPlane", q.swingPlane, d.swingPlane);
			put("twistMin", q.twistMin, d.twistMin); put("twistMax", q.twistMax, d.twistMax);
			put("sliderMin", q.sliderMin, d.sliderMin); put("sliderMax", q.sliderMax, d.sliderMax);
			put("sliderSpeed", q.sliderSpeed, d.sliderSpeed); put("sliderForce", q.sliderForce, d.sliderForce);
			put("breakDistance", q.breakDistance, d.breakDistance);
			if (q.lockX) j["lockX"] = true;
			if (q.lockY) j["lockY"] = true;
			if (q.lockZ) j["lockZ"] = true;
			if (q.lockRotX) j["lockRotX"] = true;
			if (q.lockRotY) j["lockRotY"] = true;
			if (q.lockRotZ) j["lockRotZ"] = true;
			if (q.disableSelfCollision) j["disableSelfCollision"] = true;
			return j;
		}
		SceneJoint JointFrom(const Json& j)
		{
			SceneJoint q;
			q.type = j.GetString("type", q.type);
			q.connected = j.GetString("connected");
			q.minDistance = Rf(j, "minDistance", q.minDistance); q.maxDistance = Rf(j, "maxDistance", q.maxDistance);
			q.minAngle = Rf(j, "minAngle", q.minAngle); q.maxAngle = Rf(j, "maxAngle", q.maxAngle);
			q.motorSpeed = Rf(j, "motorSpeed", q.motorSpeed); q.coneAngle = Rf(j, "coneAngle", q.coneAngle);
			q.swingNormal = Rf(j, "swingNormal", q.swingNormal); q.swingPlane = Rf(j, "swingPlane", q.swingPlane);
			q.twistMin = Rf(j, "twistMin", q.twistMin); q.twistMax = Rf(j, "twistMax", q.twistMax);
			q.sliderMin = Rf(j, "sliderMin", q.sliderMin); q.sliderMax = Rf(j, "sliderMax", q.sliderMax);
			q.sliderSpeed = Rf(j, "sliderSpeed", q.sliderSpeed); q.sliderForce = Rf(j, "sliderForce", q.sliderForce);
			q.breakDistance = Rf(j, "breakDistance", q.breakDistance);
			q.lockX = j.GetBool("lockX", false); q.lockY = j.GetBool("lockY", false); q.lockZ = j.GetBool("lockZ", false);
			q.lockRotX = j.GetBool("lockRotX", false); q.lockRotY = j.GetBool("lockRotY", false); q.lockRotZ = j.GetBool("lockRotZ", false);
			q.disableSelfCollision = j.GetBool("disableSelfCollision", false);
			return q;
		}
		SceneRigidBody RigidBodyFrom(const Json& j)
		{
			SceneRigidBody r;
			r.type = j.GetString("type", "dynamic");
			r.shape = j.GetString("shape", "box");
			r.mass = Rf(j, "mass", r.mass);
			r.friction = Rf(j, "friction", r.friction);
			r.restitution = Rf(j, "restitution", r.restitution);
			r.linearDamping = Rf(j, "linearDamping", r.linearDamping);
			r.angularDamping = Rf(j, "angularDamping", r.angularDamping);
			r.trigger = j.GetBool("trigger", false);
			r.lockRotation = j.GetBool("lockRotation", false);
			r.continuous = j.GetBool("continuous", false);
			r.shapeSize = RdF3(j.Get("shapeSize"), r.shapeSize);
			r.shapeOffset = RdF3(j.Get("shapeOffset"), r.shapeOffset);
			r.layer = (int)j.GetInt("layer", 0);
			r.maxSlopeAngle = Rf(j, "maxSlopeAngle", r.maxSlopeAngle);
			r.gravityFactor = Rf(j, "gravityFactor", r.gravityFactor);
			r.buoyancy = Rf(j, "buoyancy", r.buoyancy);
			r.lock2D = j.GetBool("lock2D", false);
			r.startDeactivated = j.GetBool("startDeactivated", false);
			r.alwaysActive = j.GetBool("alwaysActive", false);
			return r;
		}
		Json ToJson(const SceneAnimator& a)
		{
			Json j = Json::MakeObject();
			if (!a.autoplay.empty()) j["autoplay"] = a.autoplay;
		if (!a.autoplayClip.empty()) { j["autoplayClip"] = a.autoplayClip; j["autoplayLoop"] = a.autoplayLoop; }
			Json arr = Json::MakeArray();
			for (const SceneAnimator::Clip& c : a.clips)
			{
				Json o = Json::MakeObject();
				o["name"] = c.name;
				if (!c.loop) o["loop"] = false;
				if (c.pingPong) o["pingPong"] = true;
				if (c.speed != 1.0f) o["speed"] = c.speed;
				if (c.weight != 1.0f) o["weight"] = c.weight;
				arr.Push(std::move(o));
			}
			j["clips"] = std::move(arr);
			return j;
		}
		SceneAnimator AnimatorFrom(const Json& j)
		{
			SceneAnimator a;
			a.autoplay = j.GetString("autoplay");
			a.autoplayClip = j.GetString("autoplayClip");
			a.autoplayLoop = j.GetBool("autoplayLoop", true);
			for (const Json& o : j.Get("clips").Items())
			{
				SceneAnimator::Clip c;
				c.name = o.GetString("name");
				c.loop = o.GetBool("loop", true);
				c.pingPong = o.GetBool("pingPong", false);
				c.speed = Rf(o, "speed", 1.0f);
				c.weight = Rf(o, "weight", 1.0f);
				if (!c.name.empty()) a.clips.push_back(std::move(c));
			}
			return a;
		}
		Json ToJson(const SceneParticles& p)
		{
			const SceneParticles d;
			Json j = Json::MakeObject();
			j["maxParticles"] = p.maxParticles;
			j["rate"] = p.rate;
			if (p.burstOnCreate) j["burstOnCreate"] = p.burstOnCreate;
			j["size"] = p.size;
			j["life"] = p.life;
			j["randomLife"] = p.randomLife;
			j["randomFactor"] = p.randomFactor;
			if (p.normalFactor != d.normalFactor) j["normalFactor"] = p.normalFactor;
			j["velocity"] = F3(p.velocity);
			j["gravity"] = F3(p.gravity);
			if (p.drag != d.drag) j["drag"] = p.drag;
			if (p.restitution != d.restitution) j["restitution"] = p.restitution;
			if (p.mass != d.mass) j["mass"] = p.mass;
			if (p.rotation != 0) j["rotation"] = p.rotation;
			if (p.scaleX != 1) j["scaleX"] = p.scaleX;
			if (p.scaleY != 1) j["scaleY"] = p.scaleY;
			j["color"] = F4(p.color);
			if (p.randomColor != 0) j["randomColor"] = p.randomColor;
			if (!p.texture.empty()) j["texture"] = p.texture;
			if (p.shader != d.shader) j["shader"] = p.shader;
			if (p.framesX != 1) j["framesX"] = p.framesX;
			if (p.framesY != 1) j["framesY"] = p.framesY;
			if (p.frameCount != 1) j["frameCount"] = p.frameCount;
			if (p.frameRate != 0) j["frameRate"] = p.frameRate;
			if (p.frameBlending) j["frameBlending"] = true;
			if (!p.sorted) j["sorted"] = false;
			if (p.collisions) j["collisions"] = true;
			if (p.emitFromMesh) j["emitFromMesh"] = true;
			if (p.opacityPeakStart != d.opacityPeakStart) j["opacityPeakStart"] = p.opacityPeakStart;
			if (p.opacityPeakEnd != d.opacityPeakEnd) j["opacityPeakEnd"] = p.opacityPeakEnd;
			if (!p.playOnStart) j["playOnStart"] = false;
			return j;
		}
		SceneParticles ParticlesFrom(const Json& j)
		{
			SceneParticles p;
			p.maxParticles = (int)j.GetInt("maxParticles", p.maxParticles);
			p.rate = Rf(j, "rate", p.rate);
			p.burstOnCreate = (int)j.GetInt("burstOnCreate", 0);
			p.size = Rf(j, "size", p.size);
			p.life = Rf(j, "life", p.life);
			p.randomLife = Rf(j, "randomLife", p.randomLife);
			p.randomFactor = Rf(j, "randomFactor", p.randomFactor);
			p.normalFactor = Rf(j, "normalFactor", p.normalFactor);
			p.velocity = RdF3(j.Get("velocity"), p.velocity);
			p.gravity = RdF3(j.Get("gravity"), p.gravity);
			p.drag = Rf(j, "drag", p.drag);
			p.restitution = Rf(j, "restitution", p.restitution);
			p.mass = Rf(j, "mass", p.mass);
			p.rotation = Rf(j, "rotation", 0);
			p.scaleX = Rf(j, "scaleX", 1);
			p.scaleY = Rf(j, "scaleY", 1);
			p.color = RdF4(j.Get("color"), p.color);
			p.randomColor = Rf(j, "randomColor", 0);
			p.texture = j.GetString("texture");
			p.shader = j.GetString("shader", p.shader);
			p.framesX = (int)j.GetInt("framesX", 1);
			p.framesY = (int)j.GetInt("framesY", 1);
			p.frameCount = (int)j.GetInt("frameCount", 1);
			p.frameRate = Rf(j, "frameRate", 0);
			p.frameBlending = j.GetBool("frameBlending", false);
			p.sorted = j.GetBool("sorted", true);
			p.collisions = j.GetBool("collisions", false);
			p.emitFromMesh = j.GetBool("emitFromMesh", false);
			p.opacityPeakStart = Rf(j, "opacityPeakStart", p.opacityPeakStart);
			p.opacityPeakEnd = Rf(j, "opacityPeakEnd", p.opacityPeakEnd);
			p.playOnStart = j.GetBool("playOnStart", true);
			return p;
		}
		Json ToJson(const SceneTerrain& t)
		{
			Json j = Json::MakeObject();
			j["resolution"] = t.resolution;
			j["size"] = F2(t.size);
			j["height"] = t.height;
			j["seed"] = t.seed;
			j["frequency"] = t.frequency;
			j["octaves"] = t.octaves;
			j["persistence"] = t.persistence;
			j["lacunarity"] = t.lacunarity;
			j["noiseAmount"] = t.noiseAmount;
			if (t.edgeFalloff != 0) j["edgeFalloff"] = t.edgeFalloff;
			if (!t.heightmap.empty()) j["heightmap"] = t.heightmap;
			j["uvTiling"] = t.uvTiling;
			if (!t.texture.empty()) j["texture"] = t.texture;
			j["roughness"] = t.roughness;
			if (!t.collider) j["collider"] = false;
			Json layers = Json::MakeArray();
			for (const SceneTerrain::Layer& l : t.layers)
			{
				Json o = Json::MakeObject();
				o["name"] = l.name;
				o["color"] = F4(l.color);
				o["minHeight"] = l.minHeight;
				o["maxHeight"] = l.maxHeight;
				o["minSlope"] = l.minSlope;
				o["maxSlope"] = l.maxSlope;
				o["blend"] = l.blend;
				layers.Push(std::move(o));
			}
			if (!t.layers.empty()) j["layers"] = std::move(layers);
			return j;
		}
		SceneTerrain TerrainFrom(const Json& j)
		{
			SceneTerrain t;
			t.resolution = (int)j.GetInt("resolution", t.resolution);
			t.size = RdF2(j.Get("size"), t.size);
			t.height = Rf(j, "height", t.height);
			t.seed = (int)j.GetInt("seed", t.seed);
			t.frequency = Rf(j, "frequency", t.frequency);
			t.octaves = (int)j.GetInt("octaves", t.octaves);
			t.persistence = Rf(j, "persistence", t.persistence);
			t.lacunarity = Rf(j, "lacunarity", t.lacunarity);
			t.noiseAmount = Rf(j, "noiseAmount", t.noiseAmount);
			t.edgeFalloff = Rf(j, "edgeFalloff", 0);
			t.heightmap = j.GetString("heightmap");
			t.uvTiling = Rf(j, "uvTiling", t.uvTiling);
			t.texture = j.GetString("texture");
			t.roughness = Rf(j, "roughness", t.roughness);
			t.collider = j.GetBool("collider", true);
			for (const Json& o : j.Get("layers").Items())
			{
				SceneTerrain::Layer l;
				l.name = o.GetString("name");
				l.color = RdF4(o.Get("color"), l.color);
				l.minHeight = Rf(o, "minHeight", 0);
				l.maxHeight = Rf(o, "maxHeight", 1);
				l.minSlope = Rf(o, "minSlope", 0);
				l.maxSlope = Rf(o, "maxSlope", 90);
				l.blend = Rf(o, "blend", l.blend);
				t.layers.push_back(std::move(l));
			}
			return t;
		}
		Json ToJson(const SceneNavMesh& n)
		{
			Json j = Json::MakeObject();
			j["cellSize"] = n.cellSize;
			j["agentRadius"] = n.agentRadius;
			j["agentHeight"] = n.agentHeight;
			j["maxSlope"] = n.maxSlope;
			j["stepHeight"] = n.stepHeight;
			j["bounds"] = F3(n.bounds);
			if (!n.bakeOnStart) j["bakeOnStart"] = false;
			return j;
		}
		SceneNavMesh NavMeshFrom(const Json& j)
		{
			SceneNavMesh n;
			n.cellSize = Rf(j, "cellSize", n.cellSize);
			n.agentRadius = Rf(j, "agentRadius", n.agentRadius);
			n.agentHeight = Rf(j, "agentHeight", n.agentHeight);
			n.maxSlope = Rf(j, "maxSlope", n.maxSlope);
			n.stepHeight = Rf(j, "stepHeight", n.stepHeight);
			n.bounds = RdF3(j.Get("bounds"), n.bounds);
			n.bakeOnStart = j.GetBool("bakeOnStart", true);
			return n;
		}
		Json ToJson(const SceneNavAgent& a)
		{
			Json j = Json::MakeObject();
			j["speed"] = a.speed;
			j["acceleration"] = a.acceleration;
			j["turnSpeed"] = a.turnSpeed;
			j["stoppingDistance"] = a.stoppingDistance;
			j["radius"] = a.radius;
			if (!a.autoRotate) j["autoRotate"] = false;
			if (!a.autoMove) j["autoMove"] = false;
			return j;
		}
		SceneNavAgent NavAgentFrom(const Json& j)
		{
			SceneNavAgent a;
			a.speed = Rf(j, "speed", a.speed);
			a.acceleration = Rf(j, "acceleration", a.acceleration);
			a.turnSpeed = Rf(j, "turnSpeed", a.turnSpeed);
			a.stoppingDistance = Rf(j, "stoppingDistance", a.stoppingDistance);
			a.radius = Rf(j, "radius", a.radius);
			a.autoRotate = j.GetBool("autoRotate", true);
			a.autoMove = j.GetBool("autoMove", true);
			return a;
		}
		Json ToJson(const SceneAudio& a)
		{
			Json j = Json::MakeObject();
			j["file"] = a.file;
			j["volume"] = a.volume;
			if (a.loop) j["loop"] = true;
			if (a.playOnStart) j["playOnStart"] = true;
			if (!a.spatial) j["spatial"] = false;
			j["minDistance"] = a.minDistance;
			j["maxDistance"] = a.maxDistance;
			if (a.bus != "sfx") j["bus"] = a.bus;
			return j;
		}
		SceneAudio AudioFrom(const Json& j)
		{
			SceneAudio a;
			a.file = j.GetString("file");
			a.volume = Rf(j, "volume", 1);
			a.loop = j.GetBool("loop", false);
			a.playOnStart = j.GetBool("playOnStart", false);
			a.spatial = j.GetBool("spatial", true);
			a.minDistance = Rf(j, "minDistance", 1);
			a.maxDistance = Rf(j, "maxDistance", 50);
			a.bus = j.GetString("bus", "sfx");
			return a;
		}

		Json ToJson(const SceneEntity& e)
		{
			Json j = Json::MakeObject();
			j["id"] = e.id;
			j["name"] = e.name;
			if (!e.parent.empty()) j["parent"] = e.parent;
			if (!e.active) j["active"] = false;
			if (e.isStatic) j["static"] = true;
			if (!e.tag.empty()) j["tag"] = e.tag;
			if (e.layer != 0) j["layer"] = e.layer;
			Json t = ToJson(e.transform);
			if (!t.Empty()) j["transform"] = std::move(t);
			if (e.mesh) j["mesh"] = ToJson(*e.mesh);
			if (e.material) j["material"] = ToJson(*e.material);
			if (e.light) j["light"] = ToJson(*e.light);
			if (e.camera) j["camera"] = ToJson(*e.camera);
			if (e.rigidbody) j["rigidbody"] = ToJson(*e.rigidbody);
			if (e.vehicle) j["vehicle"] = ToJson(*e.vehicle);
			if (e.joint) j["joint"] = ToJson(*e.joint);
			if (e.audio) j["audio"] = ToJson(*e.audio);
			if (e.animator) j["animator"] = ToJson(*e.animator);
			if (e.particles) j["particles"] = ToJson(*e.particles);
			if (e.terrain) j["terrain"] = ToJson(*e.terrain);
			if (e.navmesh) j["navmesh"] = ToJson(*e.navmesh);
			if (e.navagent) j["navagent"] = ToJson(*e.navagent);
			if (!e.scripts.empty())
			{
				Json arr = Json::MakeArray();
				for (const SceneScript& s : e.scripts)
				{
					Json sj = Json::MakeObject();
					sj["file"] = s.file;
					if (!s.enabled) sj["enabled"] = false;
					if (s.properties.IsObject() && !s.properties.Empty()) sj["properties"] = s.properties;
					arr.Push(std::move(sj));
				}
				j["scripts"] = std::move(arr);
			}
			MergeExtra(j, e.extra);
			return j;
		}
		SceneEntity EntityFrom(const Json& j)
		{
			static const std::set<std::string> known = {"id", "name", "parent", "active", "static", "tag", "layer", "transform", "mesh", "material", "light",
				"camera", "rigidbody", "vehicle", "joint", "audio", "animator", "particles", "terrain", "navmesh", "navagent", "scripts"};
			SceneEntity e;
			e.id = j.GetString("id");
			e.name = j.GetString("name");
			e.parent = j.GetString("parent");
			e.active = j.GetBool("active", true);
			e.isStatic = j.GetBool("static", false);
			e.tag = j.GetString("tag");
			e.layer = (int)j.GetInt("layer", 0);
			const Json& t = j.Get("transform");
			e.transform.position = RdF3(t.Get("position"), e.transform.position);
			e.transform.rotation = RdF3(t.Get("rotation"), e.transform.rotation);
			e.transform.scale = RdF3(t.Get("scale"), e.transform.scale);
			if (j.Contains("mesh")) e.mesh = MeshFrom(j.Get("mesh"));
			if (j.Contains("material")) e.material = MaterialFrom(j.Get("material"));
			if (j.Contains("light")) e.light = LightFrom(j.Get("light"));
			if (j.Contains("camera")) e.camera = CameraFrom(j.Get("camera"));
			if (j.Contains("rigidbody")) e.rigidbody = RigidBodyFrom(j.Get("rigidbody"));
			if (j.Contains("vehicle")) e.vehicle = VehicleFrom(j.Get("vehicle"));
			if (j.Contains("joint")) e.joint = JointFrom(j.Get("joint"));
			if (j.Contains("audio")) e.audio = AudioFrom(j.Get("audio"));
			if (j.Contains("animator")) e.animator = AnimatorFrom(j.Get("animator"));
			if (j.Contains("particles")) e.particles = ParticlesFrom(j.Get("particles"));
			if (j.Contains("terrain")) e.terrain = TerrainFrom(j.Get("terrain"));
			if (j.Contains("navmesh")) e.navmesh = NavMeshFrom(j.Get("navmesh"));
			if (j.Contains("navagent")) e.navagent = NavAgentFrom(j.Get("navagent"));
			for (const Json& s : j.Get("scripts").Items())
			{
				SceneScript sc;
				sc.file = s.GetString("file");
				sc.enabled = s.GetBool("enabled", true);
				sc.properties = s.Get("properties");
				if (!sc.file.empty())
					e.scripts.push_back(std::move(sc));
			}
			e.extra = Unknown(j, known);
			return e;
		}

		Json ToJson(const SceneEnvironment& v)
		{
			Json j = Json::MakeObject();
			j["sun"]["azimuth"] = v.sunAzimuth;
			j["sun"]["elevation"] = v.sunElevation;
			j["sun"]["intensity"] = v.sunIntensity;
			j["sun"]["color"] = F3(v.sunColor);
			j["sun"]["shadows"] = v.sunShadows;
			j["sky"]["mode"] = v.skyMode;
			j["sky"]["color"] = F3(v.skyColor);
			if (!v.skyCubemap.empty()) j["sky"]["cubemap"] = v.skyCubemap;
			j["sky"]["exposure"] = v.skyExposure;
			j["sky"]["clouds"] = v.cloudCoverage;
			j["sky"]["softness"] = v.skySoftness;
			j["sky"]["haze"] = v.haze;
			j["fog"]["density"] = v.fogDensity;
			j["fog"]["color"] = F3(v.fogColor);
			j["fog"]["height"] = v.fogHeight;
			j["fog"]["heightFalloff"] = v.fogHeightFalloff;
			j["ambient"] = F3(v.ambientColor);
			j["wind"]["speed"] = v.windSpeed;
			j["wind"]["direction"] = v.windDirection;
			j["water"]["enabled"] = v.water;
			j["water"]["height"] = v.waterHeight;
			j["water"]["color"] = F3(v.waterColor);
			j["water"]["waveAmplitude"] = v.waterWaveAmplitude;
			return j;
		}
		SceneEnvironment EnvironmentFrom(const Json& j)
		{
			SceneEnvironment v;
			const Json &sun = j.Get("sun"), &sky = j.Get("sky"), &fog = j.Get("fog"), &wind = j.Get("wind"), &water = j.Get("water");
			v.sunAzimuth = Rf(sun, "azimuth", v.sunAzimuth);
			v.sunElevation = Rf(sun, "elevation", v.sunElevation);
			v.sunIntensity = Rf(sun, "intensity", v.sunIntensity);
			v.sunColor = RdF3(sun.Get("color"), v.sunColor);
			v.sunShadows = sun.GetBool("shadows", true);
			v.skyMode = sky.GetString("mode", v.skyMode);
			v.skyColor = RdF3(sky.Get("color"), v.skyColor);
			v.skyCubemap = sky.GetString("cubemap");
			v.skyExposure = Rf(sky, "exposure", v.skyExposure);
			v.cloudCoverage = Rf(sky, "clouds", v.cloudCoverage);
			v.skySoftness = Rf(sky, "softness", v.skySoftness);
			v.haze = Rf(sky, "haze", v.haze);
			v.fogDensity = Rf(fog, "density", v.fogDensity);
			v.fogColor = RdF3(fog.Get("color"), v.fogColor);
			v.fogHeight = Rf(fog, "height", v.fogHeight);
			v.fogHeightFalloff = Rf(fog, "heightFalloff", v.fogHeightFalloff);
			v.ambientColor = RdF3(j.Get("ambient"), v.ambientColor);
			v.windSpeed = Rf(wind, "speed", v.windSpeed);
			v.windDirection = Rf(wind, "direction", v.windDirection);
			v.water = water.GetBool("enabled", false);
			v.waterHeight = Rf(water, "height", v.waterHeight);
			v.waterColor = RdF3(water.Get("color"), v.waterColor);
			v.waterWaveAmplitude = Rf(water, "waveAmplitude", v.waterWaveAmplitude);
			return v;
		}
	} // namespace

	bool SceneTransform::IsIdentity() const
	{
		return position == Float3{0, 0, 0} && rotation == Float3{0, 0, 0} && scale == Float3{1, 1, 1};
	}

	Json SceneDoc::ToJson() const
	{
		Json j = Json::MakeObject();
		j["format"] = kSceneFormatName;
		j["version"] = kSceneFormatVersion;
		j["name"] = name;
		j["environment"] = lumen::ToJson(environment);
		Json arr = Json::MakeArray();
		for (const SceneEntity& e : entities)
			arr.Push(lumen::ToJson(e));
		j["entities"] = std::move(arr);
		MergeExtra(j, extra);
		return j;
	}

	Json SceneDoc::EnvironmentToJson() const { return lumen::ToJson(environment); }

	Json SceneDoc::EntityToJson(const SceneEntity& e) { return lumen::ToJson(e); }

	Status SceneDoc::FromJson(const Json& j, SceneDoc& out)
	{
		if (!j.IsObject())
			return Status::Fail("scene file is not a JSON object");
		const std::string format = j.GetString("format", kSceneFormatName);
		if (format != kSceneFormatName)
			return Status::Fail("not a Lumen scene (format \"" + format + "\")");
		const int version = (int)j.GetInt("version", 1);
		if (version > kSceneFormatVersion)
			return Status::Fail(StrFormat("scene format version %d is newer than this engine understands (%d)", version, kSceneFormatVersion));
		static const std::set<std::string> known = {"format", "version", "name", "environment", "entities"};
		SceneDoc d;
		d.formatVersion = kSceneFormatVersion;
		d.name = j.GetString("name", "main");
		d.environment = EnvironmentFrom(j.Get("environment"));
		for (const Json& e : j.Get("entities").Items())
		{
			SceneEntity se = EntityFrom(e);
			if (se.id.empty())
				se.id = GenerateGuid();
			d.entities.push_back(std::move(se));
		}
		d.extra = Unknown(j, known);
		const std::vector<std::string> problems = d.Validate();
		if (!problems.empty())
			return Status::Fail("invalid scene: " + problems.front());
		out = std::move(d);
		return Status::Ok();
	}

	Status SceneDoc::Load(const std::string& path, SceneDoc& out)
	{
		std::string text;
		if (!fs::ReadText(path, text))
			return Status::Fail("cannot read " + path);
		Json j;
		std::string err;
		if (!Json::Parse(text, j, &err, JsonParseOptions{true, true, 256}))
			return Status::Fail(path + ":" + err);
		Status s = FromJson(j, out);
		if (!s)
			s.message = path + ": " + s.message;
		return s;
	}

	Status SceneDoc::Save(const std::string& path) const
	{
		const std::string text = ToJson().Dump(2) + "\n";
		fs::CreateDirs(fs::Parent(fs::Absolute(path)));
		if (!fs::WriteAtomic(path, text.data(), text.size()))
			return Status::Fail("cannot write " + path);
		return Status::Ok();
	}

	SceneEntity* SceneDoc::Find(const std::string& id)
	{
		for (SceneEntity& e : entities)
			if (e.id == id)
				return &e;
		return nullptr;
	}
	const SceneEntity* SceneDoc::Find(const std::string& id) const { return const_cast<SceneDoc*>(this)->Find(id); }
	SceneEntity* SceneDoc::FindByName(const std::string& n)
	{
		for (SceneEntity& e : entities)
			if (e.name == n)
				return &e;
		return nullptr;
	}
	std::vector<const SceneEntity*> SceneDoc::Children(const std::string& parentId) const
	{
		std::vector<const SceneEntity*> out;
		for (const SceneEntity& e : entities)
			if (e.parent == parentId)
				out.push_back(&e);
		return out;
	}
	bool SceneDoc::IsDescendant(const std::string& id, const std::string& ancestor) const
	{
		const SceneEntity* e = Find(id);
		for (int guard = 0; e && guard < 4096; ++guard)
		{
			if (e->parent == ancestor && !ancestor.empty())
				return true;
			e = e->parent.empty() ? nullptr : Find(e->parent);
		}
		return false;
	}

	std::string SceneDoc::UniqueName(const std::string& base, const std::string& parentId) const
	{
		std::set<std::string> used;
		for (const SceneEntity& e : entities)
			if (e.parent == parentId)
				used.insert(e.name);
		if (!used.count(base))
			return base;
		std::string stem = base;
		while (!stem.empty() && std::isdigit((unsigned char)stem.back()))
			stem.pop_back();
		if (!stem.empty() && stem.back() == ' ')
			stem.pop_back();
		if (stem.empty())
			stem = base;
		for (int i = 1; i < 100000; ++i)
		{
			const std::string cand = stem + " " + std::to_string(i);
			if (!used.count(cand))
				return cand;
		}
		return base;
	}

	std::string SceneDoc::Add(SceneEntity entity)
	{
		if (entity.id.empty() || Find(entity.id))
			entity.id = GenerateGuid();
		if (!entity.parent.empty() && !Find(entity.parent))
			entity.parent.clear();
		entity.name = UniqueName(entity.name.empty() ? "Entity" : entity.name, entity.parent);
		const std::string id = entity.id;
		entities.push_back(std::move(entity));
		return id;
	}

	bool SceneDoc::Remove(const std::string& id)
	{
		if (!Find(id))
			return false;
		std::set<std::string> doomed = {id};
		for (bool grew = true; grew;)
		{
			grew = false;
			for (const SceneEntity& e : entities)
				if (!doomed.count(e.id) && doomed.count(e.parent))
				{
					doomed.insert(e.id);
					grew = true;
				}
		}
		entities.erase(std::remove_if(entities.begin(), entities.end(), [&](const SceneEntity& e) { return doomed.count(e.id) > 0; }), entities.end());
		return true;
	}

	bool SceneDoc::SetParent(const std::string& id, const std::string& parent)
	{
		SceneEntity* e = Find(id);
		if (!e || id == parent)
			return false;
		if (!parent.empty() && (!Find(parent) || IsDescendant(parent, id)))
			return false;
		e->parent = parent;
		e->name = UniqueName(e->name, parent); // names are unique among siblings
		return true;
	}

	namespace
	{
		// Gives every entity of `list` a new id and rewrites references that point inside the list.
		void Reidentify(std::vector<SceneEntity>& list)
		{
			std::vector<std::pair<std::string, std::string>> map;
			for (SceneEntity& e : list)
			{
				const std::string nid = GenerateGuid();
				map.push_back({e.id, nid});
				e.id = nid;
			}
			auto remap = [&](std::string& ref) {
				if (ref.empty()) return;
				for (const auto& m : map)
					if (m.first == ref) { ref = m.second; return; }
			};
			for (SceneEntity& e : list)
			{
				remap(e.parent);
				if (e.joint) remap(e.joint->connected);
				if (e.vehicle)
				{
					remap(e.vehicle->wheelFL); remap(e.vehicle->wheelFR);
					remap(e.vehicle->wheelRL); remap(e.vehicle->wheelRR);
				}
			}
		}
	}

	bool SceneDoc::IsPrefabJson(const Json& j)
	{
		return j.IsObject() && j.GetString("format") == "lumen-prefab" && j.Get("entities").IsArray();
	}

	Json SceneDoc::ExportSubtree(const std::string& id) const
	{
		Json out = Json::MakeObject();
		if (!Find(id))
			return out;
		out["format"] = "lumen-prefab";
		out["version"] = 1;
		out["root"] = id;
		Json arr = Json::MakeArray();
		std::vector<std::string> stack = {id};
		while (!stack.empty())
		{
			const std::string cur = stack.back();
			stack.pop_back();
			arr.Push(EntityToJson(*Find(cur)));
			const auto kids = Children(cur);
			for (size_t i = kids.size(); i-- > 0;)
				stack.push_back(kids[i]->id);
		}
		out["entities"] = std::move(arr);
		return out;
	}

	std::string SceneDoc::ImportSubtree(const Json& prefab, const std::string& parent, std::vector<std::string>* into)
	{
		if (!IsPrefabJson(prefab))
			return {};
		std::vector<SceneEntity> list;
		for (const Json& j : prefab.Get("entities").Items())
			list.push_back(EntityFrom(j));
		if (list.empty())
			return {};
		const std::string oldRoot = prefab.GetString("root", list.front().id);
		// root = the entity whose id matches `root`, else the first one
		size_t rootIdx = 0;
		for (size_t i = 0; i < list.size(); ++i)
			if (list[i].id == oldRoot) { rootIdx = i; break; }
		Reidentify(list);
		const std::string newRoot = list[rootIdx].id;
		for (SceneEntity& e : list)
			if (e.id == newRoot)
				e.parent = parent;
		for (SceneEntity& e : list)
		{
			if (e.id == newRoot)
				e.name = UniqueName(e.name, e.parent);
			if (into) into->push_back(e.id);
			entities.push_back(std::move(e));
		}
		return newRoot;
	}

	std::string SceneDoc::Duplicate(const std::string& id)
	{
		const SceneEntity* src = Find(id);
		if (!src)
			return {};
		return ImportSubtree(ExportSubtree(id), src->parent);
	}

	std::vector<std::string> SceneDoc::Validate() const
	{
		std::vector<std::string> problems;
		std::set<std::string> ids;
		for (const SceneEntity& e : entities)
		{
			if (!ids.insert(e.id).second)
				problems.push_back("duplicate entity id " + e.id);
		}
		for (const SceneEntity& e : entities)
		{
			if (!e.parent.empty() && !ids.count(e.parent))
				problems.push_back("entity \"" + e.name + "\" refers to a missing parent");
			if (e.parent == e.id)
				problems.push_back("entity \"" + e.name + "\" is its own parent");
		}
		// cycles
		for (const SceneEntity& e : entities)
		{
			const SceneEntity* cur = &e;
			for (size_t guard = 0; cur && !cur->parent.empty(); ++guard)
			{
				if (guard > entities.size())
				{
					problems.push_back("parent cycle at entity \"" + e.name + "\"");
					break;
				}
				cur = Find(cur->parent);
			}
		}
		return problems;
	}

	std::vector<std::string> SceneDoc::ReferencedAssets() const
	{
		std::set<std::string> out;
		auto add = [&](const std::string& p) {
			if (!p.empty()) out.insert(p);
		};
		add(environment.skyCubemap);
		for (const SceneEntity& e : entities)
		{
			if (e.mesh && e.mesh->kind == SceneMeshKind::Model) add(e.mesh->model);
			if (e.material)
			{
				const SceneMaterial& m = *e.material;
				for (const std::string* p : {&m.baseColorMap, &m.normalMap, &m.roughnessMap, &m.metalnessMap, &m.emissiveMap, &m.occlusionMap}) add(*p);
				if (str::StartsWith(m.shader, "custom:")) add(m.shader.substr(7));
				for (const Json::Member& pm : m.shaderParams.Members())
					if (pm.second.IsString() && pm.second.AsString().find('/') != std::string::npos) add(pm.second.AsString());
			}
			if (e.audio) add(e.audio->file);
			for (const SceneScript& s : e.scripts) add(s.file);
		}
		return std::vector<std::string>(out.begin(), out.end());
	}

	SceneEntity MakeEmptyEntity(const std::string& name)
	{
		SceneEntity e;
		e.id = GenerateGuid();
		e.name = name;
		return e;
	}

	SceneEntity MakePrimitiveEntity(const std::string& primitive, const std::string& name)
	{
		SceneEntity e = MakeEmptyEntity(name);
		SceneMesh m;
		m.primitive = primitive;
		if (primitive == "plane") m.size = {10, 1, 10};
		e.mesh = m;
		e.material = SceneMaterial{};
		return e;
	}

	SceneEntity MakeLightEntity(const std::string& type, const std::string& name)
	{
		SceneEntity e = MakeEmptyEntity(name);
		SceneLight l;
		l.type = type;
		if (type == "directional") { l.intensity = 6.0f; e.transform.rotation = {-48, 35, 0}; }
		else if (type == "spot") { l.intensity = 20.0f; l.range = 20.0f; e.transform.position = {0, 4, 0}; e.transform.rotation = {90, 0, 0}; }
		else e.transform.position = {0, 3, 0};
		e.light = l;
		return e;
	}

	SceneEntity MakeCameraEntity(const std::string& name, bool main)
	{
		SceneEntity e = MakeEmptyEntity(name);
		SceneCamera c;
		c.main = main;
		e.camera = c;
		e.transform.position = {0, 2, -6};
		e.transform.rotation = {10, 0, 0};
		return e;
	}

} // namespace lumen
