// Lumen Engine — terrain heightfield: procedural generation (fractal Perlin), sculpting brushes, raw height files and
// mesh generation with height/slope based vertex colours. Pure CPU, no engine dependencies.
//
// Heights are normalised 0..1 (multiplied by SceneTerrain::height for world units). The mesh is generated in the
// entity's local space, centred on the origin: x in [-size.x/2, size.x/2], z in [-size.y/2, size.y/2].
#pragma once

#include "lumen/core/SceneDoc.h"

namespace lumen::terrain
{
	struct Heightfield
	{
		int res = 0;               // samples per side
		std::vector<float> h;      // res*res, row = z
		bool Valid() const { return res >= 2 && h.size() == (size_t)res * (size_t)res; }
		float At(int x, int z) const { return h[(size_t)z * (size_t)res + (size_t)x]; }
		float& At(int x, int z) { return h[(size_t)z * (size_t)res + (size_t)x]; }
		float Sample(float u, float v) const; // bilinear, u,v in 0..1
	};

	int ClampResolution(int res);
	std::vector<SceneTerrain::Layer> DefaultLayers();

	// Fractal noise heightfield (+ island falloff) from the terrain settings.
	void Generate(const SceneTerrain& t, Heightfield& out);
	// Raw little-endian float32 file with res*res samples; false on size mismatch / IO error.
	bool LoadRaw(const std::string& path, int res, Heightfield& out);
	bool SaveRaw(const std::string& path, const Heightfield& hf);
	// Loads the painted heightmap when present and valid, otherwise generates.
	void Resolve(const SceneTerrain& t, const std::string& projectRoot, Heightfield& out, bool* fromFile = nullptr);

	enum class Brush { Raise, Lower, Smooth, Flatten, Noise };
	// Applies one brush dab at (u,v) in 0..1 terrain space. `radius` in meters, `strength` 0..1 (per dab),
	// `flattenTarget` normalised height used by Flatten. Returns the touched sample rectangle (x0,z0,x1,z1) or false.
	bool ApplyBrush(Heightfield& hf, const SceneTerrain& t, Brush brush, float u, float v, float radius, float strength, float flattenTarget, int* rect = nullptr);

	struct Mesh
	{
		std::vector<Float3> positions, normals;
		std::vector<Float2> uvs;
		std::vector<Float4> colors;
		std::vector<uint32_t> indices;
	};
	// `step` > 1 builds a lower resolution mesh (every n-th sample).
	void BuildMesh(const SceneTerrain& t, const Heightfield& hf, Mesh& out, int step = 1);
	// Local space height at (x,z) (bilinear on the heightfield), x/z in meters from the centre.
	float HeightAt(const SceneTerrain& t, const Heightfield& hf, float x, float z);
	// Local space surface normal at (x,z).
	Float3 NormalAt(const SceneTerrain& t, const Heightfield& hf, float x, float z);

} // namespace lumen::terrain
