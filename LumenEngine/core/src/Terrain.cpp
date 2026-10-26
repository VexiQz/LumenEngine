#include "lumen/core/Terrain.h"
#include "lumen/core/Fs.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <random>

namespace lumen::terrain
{
	namespace
	{
		struct Perlin
		{
			unsigned char p[512];
			explicit Perlin(int seed)
			{
				std::vector<unsigned char> perm(256);
				std::iota(perm.begin(), perm.end(), 0);
				std::mt19937 rng((unsigned)seed * 2654435761u + 12345u);
				std::shuffle(perm.begin(), perm.end(), rng);
				for (int i = 0; i < 512; ++i) p[i] = perm[(size_t)i & 255];
			}
			static float Fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
			static float Grad(int h, float x, float z)
			{
				switch (h & 7)
				{
				case 0: return x + z;
				case 1: return -x + z;
				case 2: return x - z;
				case 3: return -x - z;
				case 4: return x;
				case 5: return -x;
				case 6: return z;
				default: return -z;
				}
			}
			float Noise(float x, float z) const // roughly -1..1
			{
				const int xi = (int)std::floor(x) & 255, zi = (int)std::floor(z) & 255;
				const float xf = x - std::floor(x), zf = z - std::floor(z);
				const float u = Fade(xf), v = Fade(zf);
				const int aa = p[p[xi] + zi], ab = p[p[xi] + zi + 1], ba = p[p[xi + 1] + zi], bb = p[p[xi + 1] + zi + 1];
				const float x1 = Grad(aa, xf, zf) + u * (Grad(ba, xf - 1, zf) - Grad(aa, xf, zf));
				const float x2 = Grad(ab, xf, zf - 1) + u * (Grad(bb, xf - 1, zf - 1) - Grad(ab, xf, zf - 1));
				return (x1 + v * (x2 - x1)) * 0.9f;
			}
		};

		float Smooth01(float t) { t = std::min(1.0f, std::max(0.0f, t)); return t * t * (3 - 2 * t); }

		// 1 inside [lo,hi], falling to 0 over `blend` outside
		float RangeWeight(float v, float lo, float hi, float blend)
		{
			if (blend <= 1e-5f) return (v >= lo && v <= hi) ? 1.0f : 0.0f;
			const float a = Smooth01((v - (lo - blend)) / blend);
			const float b = Smooth01(((hi + blend) - v) / blend);
			return std::min(a, b);
		}

		float Hash01(int x, int z, int seed)
		{
			uint32_t h = (uint32_t)x * 374761393u + (uint32_t)z * 668265263u + (uint32_t)seed * 2246822519u;
			h = (h ^ (h >> 13)) * 1274126177u;
			return (float)((h ^ (h >> 16)) & 0xFFFFFF) / (float)0xFFFFFF;
		}
	}

	float Heightfield::Sample(float u, float v) const
	{
		if (!Valid()) return 0;
		const float fx = std::min(1.0f, std::max(0.0f, u)) * (float)(res - 1), fz = std::min(1.0f, std::max(0.0f, v)) * (float)(res - 1);
		const int x0 = std::min(res - 2, (int)fx), z0 = std::min(res - 2, (int)fz);
		const float tx = fx - (float)x0, tz = fz - (float)z0;
		const float a = At(x0, z0) + (At(x0 + 1, z0) - At(x0, z0)) * tx;
		const float b = At(x0, z0 + 1) + (At(x0 + 1, z0 + 1) - At(x0, z0 + 1)) * tx;
		return a + (b - a) * tz;
	}

	int ClampResolution(int res) { return std::min(1025, std::max(17, res)); }

	std::vector<SceneTerrain::Layer> DefaultLayers()
	{
		std::vector<SceneTerrain::Layer> l;
		SceneTerrain::Layer sand; sand.name = "sand"; sand.color = {0.76f, 0.70f, 0.50f, 1}; sand.minHeight = 0.0f; sand.maxHeight = 0.12f; sand.maxSlope = 22; sand.blend = 0.05f;
		SceneTerrain::Layer grass; grass.name = "grass"; grass.color = {0.28f, 0.45f, 0.20f, 1}; grass.minHeight = 0.10f; grass.maxHeight = 0.65f; grass.maxSlope = 34; grass.blend = 0.10f;
		SceneTerrain::Layer rock; rock.name = "rock"; rock.color = {0.42f, 0.40f, 0.38f, 1}; rock.minHeight = 0.0f; rock.maxHeight = 1.0f; rock.minSlope = 30; rock.maxSlope = 90; rock.blend = 0.10f;
		SceneTerrain::Layer snow; snow.name = "snow"; snow.color = {0.94f, 0.96f, 0.98f, 1}; snow.minHeight = 0.78f; snow.maxHeight = 1.0f; snow.maxSlope = 42; snow.blend = 0.06f;
		l.push_back(sand); l.push_back(grass); l.push_back(rock); l.push_back(snow);
		return l;
	}

	void Generate(const SceneTerrain& t, Heightfield& out)
	{
		const int res = ClampResolution(t.resolution);
		out.res = res;
		out.h.assign((size_t)res * (size_t)res, 0.0f);
		const Perlin perlin(t.seed);
		const int octaves = std::min(10, std::max(1, t.octaves));
		const float worldX = std::max(1.0f, t.size.x), worldZ = std::max(1.0f, t.size.y);
		float amp = 1.0f, norm = 0.0f;
		for (int o = 0; o < octaves; ++o) { norm += amp; amp *= t.persistence; }
		for (int z = 0; z < res; ++z)
			for (int x = 0; x < res; ++x)
			{
				const float u = (float)x / (float)(res - 1), v = (float)z / (float)(res - 1);
				float f = t.frequency, a = 1.0f, n = 0.0f;
				for (int o = 0; o < octaves; ++o)
				{
					n += perlin.Noise(u * worldX * f + 17.3f * (float)o, v * worldZ * f - 9.1f * (float)o) * a;
					f *= t.lacunarity;
					a *= t.persistence;
				}
				n /= std::max(norm, 1e-4f);
				float h = 0.5f + n * t.noiseAmount * 1.1f;
				h = std::min(1.0f, std::max(0.0f, h));
				if (t.edgeFalloff > 0)
				{
					const float d = std::max(std::fabs(u - 0.5f), std::fabs(v - 0.5f)) * 2.0f;
					h *= 1.0f - Smooth01((d - (1.0f - t.edgeFalloff)) / std::max(1e-3f, t.edgeFalloff));
				}
				out.At(x, z) = h;
			}
	}

	bool LoadRaw(const std::string& path, int res, Heightfield& out)
	{
		std::string data;
		if (!fs::ReadText(path, data) || data.size() != (size_t)res * (size_t)res * sizeof(float))
			return false;
		out.res = res;
		out.h.resize((size_t)res * (size_t)res);
		std::memcpy(out.h.data(), data.data(), data.size());
		for (float& v : out.h)
			v = std::min(1.0f, std::max(0.0f, std::isfinite(v) ? v : 0.0f));
		return true;
	}

	bool SaveRaw(const std::string& path, const Heightfield& hf)
	{
		if (!hf.Valid()) return false;
		fs::CreateDirs(fs::Parent(path));
		return fs::WriteText(path, std::string((const char*)hf.h.data(), hf.h.size() * sizeof(float)));
	}

	void Resolve(const SceneTerrain& t, const std::string& root, Heightfield& out, bool* fromFile)
	{
		const int res = ClampResolution(t.resolution);
		if (fromFile) *fromFile = false;
		if (!t.heightmap.empty() && LoadRaw(fs::Join(root, t.heightmap), res, out))
		{
			if (fromFile) *fromFile = true;
			return;
		}
		Generate(t, out);
	}

	bool ApplyBrush(Heightfield& hf, const SceneTerrain& t, Brush brush, float u, float v, float radius, float strength, float flattenTarget, int* rect)
	{
		if (!hf.Valid() || radius <= 0) return false;
		const float cellX = std::max(1.0f, t.size.x) / (float)(hf.res - 1), cellZ = std::max(1.0f, t.size.y) / (float)(hf.res - 1);
		const float cx = u * (float)(hf.res - 1), cz = v * (float)(hf.res - 1);
		const int rx = (int)std::ceil(radius / cellX), rz = (int)std::ceil(radius / cellZ);
		const int x0 = std::max(0, (int)std::floor(cx) - rx), x1 = std::min(hf.res - 1, (int)std::ceil(cx) + rx);
		const int z0 = std::max(0, (int)std::floor(cz) - rz), z1 = std::min(hf.res - 1, (int)std::ceil(cz) + rz);
		if (x0 > x1 || z0 > z1) return false;
		const std::vector<float> src = brush == Brush::Smooth ? hf.h : std::vector<float>();
		const float dh = strength * 0.02f / std::max(0.05f, t.height / 40.0f); // normalised height per dab, independent of terrain height
		for (int z = z0; z <= z1; ++z)
			for (int x = x0; x <= x1; ++x)
			{
				const float dx = ((float)x - cx) * cellX, dz = ((float)z - cz) * cellZ;
				const float d = std::sqrt(dx * dx + dz * dz);
				if (d > radius) continue;
				const float fall = Smooth01(1.0f - d / radius);
				float& h = hf.At(x, z);
				switch (brush)
				{
				case Brush::Raise: h += dh * fall; break;
				case Brush::Lower: h -= dh * fall; break;
				case Brush::Flatten: h += (flattenTarget - h) * std::min(1.0f, strength * fall * 0.6f); break;
				case Brush::Smooth:
				{
					float sum = 0; int n = 0;
					for (int oz = -1; oz <= 1; ++oz)
						for (int ox = -1; ox <= 1; ++ox)
						{
							const int sx = x + ox, sz = z + oz;
							if (sx < 0 || sz < 0 || sx >= hf.res || sz >= hf.res) continue;
							sum += src[(size_t)sz * (size_t)hf.res + (size_t)sx];
							++n;
						}
					h += (sum / (float)std::max(1, n) - h) * std::min(1.0f, strength * fall * 0.8f);
					break;
				}
				case Brush::Noise: h += (Hash01(x, z, t.seed) - 0.5f) * dh * 2.0f * fall; break;
				}
				h = std::min(1.0f, std::max(0.0f, h));
			}
		if (rect) { rect[0] = x0; rect[1] = z0; rect[2] = x1; rect[3] = z1; }
		return true;
	}

	float HeightAt(const SceneTerrain& t, const Heightfield& hf, float x, float z)
	{
		const float u = x / std::max(1.0f, t.size.x) + 0.5f, v = z / std::max(1.0f, t.size.y) + 0.5f;
		return hf.Sample(u, v) * t.height;
	}

	Float3 NormalAt(const SceneTerrain& t, const Heightfield& hf, float x, float z)
	{
		const float e = std::max(t.size.x, t.size.y) / (float)std::max(1, hf.res - 1);
		const float hl = HeightAt(t, hf, x - e, z), hr = HeightAt(t, hf, x + e, z), hd = HeightAt(t, hf, x, z - e), hu = HeightAt(t, hf, x, z + e);
		Float3 n{hl - hr, 2 * e, hd - hu};
		const float l = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
		return {n.x / l, n.y / l, n.z / l};
	}

	void BuildMesh(const SceneTerrain& t, const Heightfield& hf, Mesh& m, int step)
	{
		m = {};
		if (!hf.Valid()) return;
		step = std::max(1, step);
		const int n = (hf.res - 1) / step + 1;
		if (n < 2) return;
		const std::vector<SceneTerrain::Layer> layers = t.layers.empty() ? DefaultLayers() : t.layers;
		const float sx = std::max(1.0f, t.size.x), sz = std::max(1.0f, t.size.y);
		m.positions.reserve((size_t)n * (size_t)n);
		for (int j = 0; j < n; ++j)
			for (int i = 0; i < n; ++i)
			{
				const int hx = std::min(hf.res - 1, i * step), hz = std::min(hf.res - 1, j * step);
				const float u = (float)hx / (float)(hf.res - 1), v = (float)hz / (float)(hf.res - 1);
				m.positions.push_back({(u - 0.5f) * sx, hf.At(hx, hz) * t.height, (v - 0.5f) * sz});
				m.uvs.push_back({u * t.uvTiling, v * t.uvTiling});
			}
		// normals from central differences on the grid
		m.normals.resize(m.positions.size());
		auto P = [&](int i, int j) -> const Float3& { return m.positions[(size_t)std::min(n - 1, std::max(0, j)) * (size_t)n + (size_t)std::min(n - 1, std::max(0, i))]; };
		for (int j = 0; j < n; ++j)
			for (int i = 0; i < n; ++i)
			{
				const Float3 l = P(i - 1, j), r = P(i + 1, j), d = P(i, j - 1), u = P(i, j + 1);
				const float dx = r.x - l.x, dz = u.z - d.z;
				Float3 nrm{(l.y - r.y) * dz, dx * dz, (d.y - u.y) * dx};
				const float len = std::sqrt(nrm.x * nrm.x + nrm.y * nrm.y + nrm.z * nrm.z);
				m.normals[(size_t)j * (size_t)n + (size_t)i] = len > 1e-8f ? Float3{nrm.x / len, nrm.y / len, nrm.z / len} : Float3{0, 1, 0};
			}
		// layer colours
		m.colors.resize(m.positions.size());
		for (size_t k = 0; k < m.positions.size(); ++k)
		{
			const float hn = t.height > 0 ? m.positions[k].y / t.height : 0.0f;
			const float slope = std::acos(std::min(1.0f, std::max(-1.0f, m.normals[k].y))) * 57.2957795f;
			float r = 0, g = 0, b = 0, wsum = 0;
			for (const SceneTerrain::Layer& L : layers)
			{
				const float w = RangeWeight(hn, L.minHeight, L.maxHeight, L.blend) * RangeWeight(slope, L.minSlope, L.maxSlope, L.blend * 90.0f);
				r += L.color.x * w; g += L.color.y * w; b += L.color.z * w; wsum += w;
			}
			if (wsum < 1e-4f && !layers.empty()) { r = layers[0].color.x; g = layers[0].color.y; b = layers[0].color.z; wsum = 1; }
			const int ix = (int)(k % (size_t)n), iz = (int)(k / (size_t)n);
			const float var = 0.93f + 0.14f * Hash01(ix, iz, t.seed); // breaks up banding
			m.colors[k] = {r / wsum * var, g / wsum * var, b / wsum * var, 1.0f};
		}
		// indices: same winding as the engine's primitive plane (upward facing)
		m.indices.reserve((size_t)(n - 1) * (size_t)(n - 1) * 6);
		for (int j = 0; j + 1 < n; ++j)
			for (int i = 0; i + 1 < n; ++i)
			{
				const uint32_t a = (uint32_t)(j * n + i), b = a + 1, c = (uint32_t)((j + 1) * n + i), d = c + 1;
				// (x0,z1) (x1,z1) (x1,z0) and (x0,z1) (x1,z0) (x0,z0)
				m.indices.insert(m.indices.end(), {c, d, b, c, b, a});
			}
	}

} // namespace lumen::terrain
