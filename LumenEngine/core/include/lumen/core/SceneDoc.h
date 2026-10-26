// Lumen Engine — scene document: the text based, engine independent description of a scene (`.scene`, JSON).
//
// The editor edits a SceneDoc; saving writes it back to the file (stable key order, diff friendly) and the runtime builds the
// renderer's scene from it. Unknown keys are preserved so newer files survive older tools.
#pragma once

#include "lumen/core/Common.h"
#include "lumen/core/Json.h"

#include <optional>

namespace lumen
{
	constexpr const char* kSceneFormatName = "lumen-scene";
	constexpr int kSceneFormatVersion = 2;

	struct SceneTransform
	{
		Float3 position{0, 0, 0};
		Float3 rotation{0, 0, 0}; // euler degrees: pitch (X), yaw (Y), roll (Z)
		Float3 scale{1, 1, 1};
		bool IsIdentity() const;
	};

	struct SceneMaterial
	{
		std::string name;
		Float4 color{1, 1, 1, 1};
		float roughness = 0.5f;
		float metalness = 0.0f;
		float reflectance = 0.5f;
		Float3 emissive{0, 0, 0};
		float emissiveStrength = 0.0f;
		std::string baseColorMap, normalMap, roughnessMap, metalnessMap, emissiveMap, occlusionMap;
		Float2 tiling{1, 1};
		std::string shader = "pbr"; // pbr | water | toon | foliage | terrain | glass | unlit | forcefield | dissolve | ice | custom:<shader file>
		Json shaderParams;          // numbers / colours / texture paths consumed by the shader
		bool doubleSided = false;
		bool castShadows = true;
		bool alphaBlend = false;
		float alphaCutoff = 0.0f;
	};

	enum class SceneMeshKind { Primitive, Model };
	struct SceneMesh
	{
		SceneMeshKind kind = SceneMeshKind::Primitive;
		std::string primitive = "cube"; // cube | plane | sphere | cylinder | capsule | cone | torus
		std::string model;              // project relative path of a model (kind == Model)
		Float3 size{1, 1, 1};           // primitive extents
		int segments = 32;
		bool castShadows = true;
		bool receiveShadows = true;
		bool visible = true;
		float cullDistance = 0.0f;      // 0: never culled by distance
		int lodBias = 0;
	};

	struct SceneLight
	{
		std::string type = "point"; // directional | point | spot
		Float3 color{1, 1, 1};
		float intensity = 5.0f;
		float range = 12.0f;
		float innerAngle = 20.0f;   // spot, degrees
		float outerAngle = 40.0f;
		bool castShadows = true;
		float volumetric = 0.0f;
	};

	struct SceneCamera
	{
		float fov = 60.0f; // vertical, degrees
		float nearPlane = 0.1f;
		float farPlane = 2000.0f;
		bool main = false;
	};

	struct SceneRigidBody
	{
		std::string type = "dynamic"; // static | dynamic | kinematic
		std::string shape = "box";    // box | sphere | capsule | cylinder | mesh | convex
		float mass = 1.0f;
		float friction = 0.5f;
		float restitution = 0.2f;
		float linearDamping = 0.05f;
		float angularDamping = 0.05f;
		bool trigger = false;
		bool lockRotation = false;
		bool continuous = false;
		Float3 shapeSize{0, 0, 0};    // 0: derive from the mesh bounds
		Float3 shapeOffset{0, 0, 0};
		int layer = 0;
		// Character controller (lockRotation on a capsule) and general tuning.
		float maxSlopeAngle = 50.0f;   // degrees, characters only
		float gravityFactor = 1.0f;    // characters only
		float buoyancy = 1.2f;         // >1 floats in water
		bool lock2D = false;           // keeps the body on the XY plane
		bool startDeactivated = false; // sleeps until touched
		bool alwaysActive = false;     // never falls asleep
	};

	// Wheeled vehicle simulated by Jolt (needs a rigidbody on the same entity; the chassis box replaces its shape).
	struct SceneVehicle
	{
		std::string type = "car"; // car | motorcycle
		std::string collision = "ray"; // ray | sphere | cylinder (wheel collision test)
		Float3 chassis{1.8f, 0.5f, 4.0f}; // full width/height/length
		float frontWheelOffset = 0.0f;
		float rearWheelOffset = 0.0f;
		float wheelRadius = 0.35f;
		float wheelWidth = 0.2f;
		float engineTorque = 500.0f;
		float clutchStrength = 10.0f;
		float maxSteerAngle = 30.0f;   // degrees
		float maxRollAngle = 60.0f;    // degrees (motorcycle)
		float suspensionMin = 0.3f, suspensionMax = 0.5f, suspensionFrequency = 1.5f, suspensionDamping = 0.5f;
		bool fourWheelDrive = false;   // car
		bool leanControl = true;       // motorcycle
		std::string wheelFL, wheelFR, wheelRL, wheelRR; // entity ids whose transforms follow the wheels
	};

	// Constraint between this entity's body and another one (or the world). The joint frame is this entity's transform:
	// hinge axis / cone axis = local Y, slider axis = local X.
	struct SceneJoint
	{
		std::string type = "fixed"; // fixed | point | distance | hinge | cone | sixdof | swingtwist | slider
		std::string connected;      // id of the other entity ("" = the world)
		float minDistance = 0, maxDistance = 1;      // distance
		float minAngle = -180, maxAngle = 180;       // hinge (degrees)
		float motorSpeed = 0;                        // hinge: rad/s target angular velocity
		float coneAngle = 30;                        // cone: half angle (degrees)
		float swingNormal = 30, swingPlane = 30, twistMin = -30, twistMax = 30; // swingtwist (degrees)
		float sliderMin = -1, sliderMax = 1, sliderSpeed = 0, sliderForce = 0;  // slider
		bool lockX = false, lockY = false, lockZ = false, lockRotX = false, lockRotY = false, lockRotZ = false; // sixdof
		float breakDistance = 0;    // 0 = unbreakable
		bool disableSelfCollision = false;
	};

	struct SceneAudio
	{
		std::string file;
		float volume = 1.0f;
		bool loop = false;
		bool playOnStart = false;
		bool spatial = true;
		float minDistance = 1.0f;
		float maxDistance = 50.0f;
		std::string bus = "sfx";
	};

	// Preset animation behaviour for the clips that came with an entity's model (glTF/FBX animations).
	struct SceneAnimator
	{
		struct Clip
		{
			std::string name;      // clip name as in the model
			bool loop = true;
			bool pingPong = false;
			float speed = 1.0f;
			float weight = 1.0f;   // blend amount
		};
		std::string autoplay;      // clip started when the game starts ("" = none)
		std::string autoplayClip;  // .lanim keyframe clip started when the game starts ("" = none)
		bool autoplayLoop = true;
		std::vector<Clip> clips;   // per clip overrides (clips not listed keep the importer defaults)
		const Clip* Find(const std::string& name) const
		{
			for (const Clip& c : clips)
				if (c.name == name)
					return &c;
			return nullptr;
		}
	};

	// GPU particle emitter (Wicked EmittedParticleSystem). The entity's transform is the emitter origin.
	struct SceneParticles
	{
		int maxParticles = 1000;
		float rate = 100.0f;           // particles per second
		int burstOnCreate = 0;
		float size = 0.25f;
		float life = 2.0f;
		float randomLife = 0.3f;       // 0..1 variation
		float randomFactor = 0.5f;     // random velocity
		float normalFactor = 0.0f;     // velocity along the emitting mesh normal (shape = mesh)
		Float3 velocity{0, 2, 0};
		Float3 gravity{0, -2, 0};
		float drag = 1.0f;
		float restitution = 0.8f;
		float mass = 1.0f;
		float rotation = 0.0f;
		float scaleX = 1.0f, scaleY = 1.0f;
		Float4 color{1, 1, 1, 1};
		float randomColor = 0.0f;
		std::string texture;           // sprite / sprite sheet
		std::string shader = "soft";   // soft | simple | distortion | lit
		int framesX = 1, framesY = 1, frameCount = 1;
		float frameRate = 0.0f;
		bool frameBlending = false;
		bool sorted = true;
		bool collisions = false;       // depth buffer collisions
		bool emitFromMesh = false;     // emit from the surface of the entity's mesh
		float opacityPeakStart = 0.1f, opacityPeakEnd = 0.5f;
		bool playOnStart = true;
	};

	// Heightfield terrain generated by the engine: procedural fractal noise optionally overridden by a painted height file.
	struct SceneTerrain
	{
		struct Layer
		{
			std::string name;
			Float4 color{1, 1, 1, 1};
			float minHeight = 0.0f, maxHeight = 1.0f; // normalised height range
			float minSlope = 0.0f, maxSlope = 90.0f;  // degrees
			float blend = 0.08f;                       // softness of the range borders
		};
		int resolution = 129;            // vertices per side (clamped 17..1025)
		Float2 size{200.0f, 200.0f};     // meters (x, z); the entity position is the terrain centre
		float height = 40.0f;            // meters at normalised height 1
		int seed = 1337;
		float frequency = 0.012f;        // base noise frequency (1/m)
		int octaves = 5;
		float persistence = 0.5f;
		float lacunarity = 2.0f;
		float noiseAmount = 1.0f;        // 0 = flat base
		float edgeFalloff = 0.0f;        // 0..1: push the border down (islands)
		std::string heightmap;           // project relative raw float32 file (resolution^2 samples); overrides noise when present
		float uvTiling = 60.0f;
		std::string texture;             // optional detail texture multiplied with the layer colours
		float roughness = 0.9f;
		bool collider = true;
		std::vector<Layer> layers;       // empty = default sand/grass/rock/snow
	};

	// Navigation mesh volume. The mesh is built from static meshes inside the volume (centred on the entity).
	struct SceneNavMesh
	{
		float cellSize = 0.35f;
		float agentRadius = 0.4f;
		float agentHeight = 1.8f;
		float maxSlope = 45.0f;          // degrees
		float stepHeight = 0.45f;
		Float3 bounds{80.0f, 30.0f, 80.0f};
		bool bakeOnStart = true;         // (re)bake when the game starts
	};

	struct SceneNavAgent
	{
		float speed = 3.5f;
		float acceleration = 14.0f;
		float turnSpeed = 540.0f;        // deg/s
		float stoppingDistance = 0.25f;
		float radius = 0.4f;
		bool autoRotate = true;          // face the movement direction
		bool autoMove = true;            // agent moves the entity itself (otherwise scripts read agent:velocity())
	};

	struct SceneScript
	{
		std::string file;
		bool enabled = true;
		Json properties; // values exposed to the script (`self.props`)
	};

	struct SceneEntity
	{
		std::string id;      // stable GUID, referenced by `parent` and by scripts
		std::string name;
		std::string parent;  // id of the parent entity ("" = root)
		bool active = true;
		bool isStatic = false;
		std::string tag;
		int layer = 0;
		SceneTransform transform;
		std::optional<SceneMesh> mesh;
		std::optional<SceneMaterial> material;
		std::optional<SceneLight> light;
		std::optional<SceneCamera> camera;
		std::optional<SceneRigidBody> rigidbody;
		std::optional<SceneVehicle> vehicle;
		std::optional<SceneJoint> joint;
		std::optional<SceneAudio> audio;
		std::optional<SceneAnimator> animator;
		std::optional<SceneParticles> particles;
		std::optional<SceneTerrain> terrain;
		std::optional<SceneNavMesh> navmesh;
		std::optional<SceneNavAgent> navagent;
		std::vector<SceneScript> scripts;
		Json extra; // unknown keys of the entity
	};

	struct SceneEnvironment
	{
		float sunAzimuth = 35.0f;
		float sunElevation = 48.0f;
		float sunIntensity = 6.0f;
		Float3 sunColor{1.0f, 0.96f, 0.88f};
		bool sunShadows = true;
		std::string skyMode = "procedural"; // procedural | color | cubemap
		Float3 skyColor{0.35f, 0.55f, 0.85f};
		std::string skyCubemap;
		float skyExposure = 1.0f;
		float skySoftness = 0.65f;   // 0: physically crisp sky, 1: wide soft glow, hazy horizon, gentle colours
		float haze = 1.0f;           // aerial perspective strength
		float fogDensity = 0.012f;
		Float3 fogColor{0.65f, 0.75f, 0.88f};
		float fogHeight = 0.0f;       // 0: uniform fog
		float fogHeightFalloff = 0.1f;
		Float3 ambientColor{0.2f, 0.22f, 0.26f};
		float cloudCoverage = 0.35f;
		float windSpeed = 1.0f;
		float windDirection = 0.0f;
		bool water = false;           // global ocean plane
		float waterHeight = 0.0f;
		Float3 waterColor{0.02f, 0.18f, 0.24f};
		float waterWaveAmplitude = 0.35f;
	};

	struct SceneDoc
	{
		int formatVersion = kSceneFormatVersion;
		std::string name = "main";
		SceneEnvironment environment;
		std::vector<SceneEntity> entities;
		Json extra;

		static Status Load(const std::string& path, SceneDoc& out);
		static Status FromJson(const Json& j, SceneDoc& out);
		Status Save(const std::string& path) const;
		Json ToJson() const;
		static Json EntityToJson(const SceneEntity& e);
		Json EnvironmentToJson() const;

		SceneEntity* Find(const std::string& id);
		const SceneEntity* Find(const std::string& id) const;
		SceneEntity* FindByName(const std::string& name);
		std::vector<const SceneEntity*> Children(const std::string& parentId) const; // "" = roots
		bool IsDescendant(const std::string& id, const std::string& ancestor) const;

		// Adds an entity (id generated when empty, name made unique among siblings). Returns its id.
		std::string Add(SceneEntity entity);
		// Removes the entity and everything below it.
		bool Remove(const std::string& id);
		// Re-parents; refuses cycles. The local transform is kept as is.
		bool SetParent(const std::string& id, const std::string& parent);
		// Deep copy of an entity and its subtree; returns the id of the copy of `id`.
		std::string Duplicate(const std::string& id);
		std::string UniqueName(const std::string& base, const std::string& parentId) const;

		// Prefabs / clipboard: a subtree as a self contained document, and the way back with fresh ids.
		// Format: {"format":"lumen-prefab","version":1,"root":"<id>","entities":[...]}  (references between the entities - parent,
		// joint.connected, vehicle wheels - are kept consistent on import).
		Json ExportSubtree(const std::string& id) const;
		// Adds the prefab below `parent` ("" = root) and returns the id of the new root ("" on error). `into` receives all new ids.
		std::string ImportSubtree(const Json& prefab, const std::string& parent, std::vector<std::string>* into = nullptr);
		static bool IsPrefabJson(const Json& j);

		// Problems that would break loading: duplicate ids, missing parents, cycles, missing script files are NOT checked here
		// (the editor does that against the asset database).
		std::vector<std::string> Validate() const;
		// Project relative asset paths referenced by the scene (models, textures, scripts, audio, cubemap).
		std::vector<std::string> ReferencedAssets() const;
	};

	// Factories for new entities with sensible defaults.
	SceneEntity MakePrimitiveEntity(const std::string& primitive, const std::string& name);
	SceneEntity MakeLightEntity(const std::string& type, const std::string& name);
	SceneEntity MakeCameraEntity(const std::string& name, bool main);
	SceneEntity MakeEmptyEntity(const std::string& name);

} // namespace lumen
