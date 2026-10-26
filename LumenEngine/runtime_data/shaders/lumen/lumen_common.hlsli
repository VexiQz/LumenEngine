// Lumen Engine — shared shader helpers (HLSL, Wicked Engine conventions).
// Everything here is cheap: a few ALU operations per call, no texture reads unless stated.
#ifndef LUMEN_COMMON_HLSLI
#define LUMEN_COMMON_HLSLI

// --------------------------------------------------------------------------------------------------
// Hashing / noise. Integer hashes (PCG) are stable across GPUs; no sin() based hashes.
// --------------------------------------------------------------------------------------------------
inline uint lumen_pcg(uint v)
{
	uint state = v * 747796405u + 2891336453u;
	uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
	return (word >> 22u) ^ word;
}
inline uint lumen_pcg3(uint3 v)
{
	return lumen_pcg(v.x + lumen_pcg(v.y + lumen_pcg(v.z)));
}
inline float lumen_hash11(float p)
{
	return (lumen_pcg((uint)asint(p)) & 0xFFFFFFu) * (1.0 / 16777216.0);
}
inline float lumen_hash21(float2 p)
{
	return (lumen_pcg3(uint3(asuint(p.x), asuint(p.y), 0x9E3779B9u)) & 0xFFFFFFu) * (1.0 / 16777216.0);
}
inline float lumen_hash31(float3 p)
{
	return (lumen_pcg3(uint3(asuint(p.x), asuint(p.y), asuint(p.z))) & 0xFFFFFFu) * (1.0 / 16777216.0);
}
inline float lumen_ihash(int3 c)
{
	return (lumen_pcg3(uint3(c)) & 0xFFFFFFu) * (1.0 / 16777216.0);
}

// Quintic interpolation: C2 continuous, avoids visible grid creases in normals derived from noise.
inline float3 lumen_fade(float3 t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }

float lumen_value_noise(float3 p)
{
	int3 i = (int3)floor(p);
	float3 f = frac(p);
	float3 u = lumen_fade(f);
	float n000 = lumen_ihash(i + int3(0, 0, 0));
	float n100 = lumen_ihash(i + int3(1, 0, 0));
	float n010 = lumen_ihash(i + int3(0, 1, 0));
	float n110 = lumen_ihash(i + int3(1, 1, 0));
	float n001 = lumen_ihash(i + int3(0, 0, 1));
	float n101 = lumen_ihash(i + int3(1, 0, 1));
	float n011 = lumen_ihash(i + int3(0, 1, 1));
	float n111 = lumen_ihash(i + int3(1, 1, 1));
	return lerp(lerp(lerp(n000, n100, u.x), lerp(n010, n110, u.x), u.y),
				lerp(lerp(n001, n101, u.x), lerp(n011, n111, u.x), u.y), u.z);
}

float lumen_value_noise(float2 p)
{
	int2 i = (int2)floor(p);
	float2 f = frac(p);
	float2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
	float a = lumen_ihash(int3(i, 0));
	float b = lumen_ihash(int3(i + int2(1, 0), 0));
	float c = lumen_ihash(int3(i + int2(0, 1), 0));
	float d = lumen_ihash(int3(i + int2(1, 1), 0));
	return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

// Fractal Brownian motion. `octaves` is a compile time constant at the call site so the loop unrolls.
float lumen_fbm(float3 p, int octaves, float lacunarity = 2.0, float gain = 0.5)
{
	float sum = 0.0;
	float amp = 0.5;
	[unroll]
	for (int i = 0; i < octaves; ++i)
	{
		sum += amp * lumen_value_noise(p);
		p = p * lacunarity + 17.13;
		amp *= gain;
	}
	return sum;
}
float lumen_fbm(float2 p, int octaves, float lacunarity = 2.0, float gain = 0.5)
{
	float sum = 0.0;
	float amp = 0.5;
	[unroll]
	for (int i = 0; i < octaves; ++i)
	{
		sum += amp * lumen_value_noise(p);
		p = p * lacunarity + 31.7;
		amp *= gain;
	}
	return sum;
}

// --------------------------------------------------------------------------------------------------
// Math helpers
// --------------------------------------------------------------------------------------------------
inline float lumen_saturate_pow(float x, float e) { return pow(saturate(x), e); }
inline float lumen_remap(float v, float a, float b, float c, float d) { return c + (saturate((v - a) / max(b - a, 1e-5)) * (d - c)); }
inline float lumen_smoothpulse(float x, float a, float b, float w) { return smoothstep(a - w, a + w, x) - smoothstep(b - w, b + w, x); }
inline float3 lumen_srgb_to_linear(float3 c) { return select(c <= 0.04045, c / 12.92, pow((c + 0.055) / 1.055, 2.4)); }
inline float lumen_luma(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }

inline float3 lumen_fresnel_schlick(float cosTheta, float3 f0)
{
	float f = pow(1.0 - saturate(cosTheta), 5.0);
	return f0 + (1.0 - f0) * f;
}
// Roughness aware variant used for environment reflections.
inline float3 lumen_fresnel_schlick_roughness(float cosTheta, float3 f0, float roughness)
{
	float f = pow(1.0 - saturate(cosTheta), 5.0);
	return f0 + (max((float3)(1.0 - roughness), f0) - f0) * f;
}

// Rotates a unit vector `v` so that +Z maps to `n` (branchless orthonormal basis, Duff et al. 2017).
float3x3 lumen_basis(float3 n)
{
	float s = n.z >= 0 ? 1.0 : -1.0;
	float a = -1.0 / (s + n.z);
	float b = n.x * n.y * a;
	float3 t = float3(1.0 + s * n.x * n.x * a, s * b, -s * n.x);
	float3 bt = float3(b, s + n.y * n.y * a, -n.y);
	return float3x3(t, bt, n);
}

// Reorients a tangent space normal onto a base normal without a tangent frame ("whiteout" blend).
float3 lumen_blend_normals(float3 baseN, float3 detailTS)
{
	float3 t = float3(baseN.xy, baseN.z + 1.0);
	float3 u = float3(-detailTS.xy, detailTS.z);
	return normalize(t * dot(t, u) - u * t.z);
}

// Tangent space normal from a height gradient (used for procedural bump / ripples).
inline float3 lumen_normal_from_gradient(float2 grad, float strength)
{
	return normalize(float3(-grad * strength, 1.0));
}

// --------------------------------------------------------------------------------------------------
// Triplanar mapping weights from a world normal. Sharpness > 1 narrows the blend zones.
// --------------------------------------------------------------------------------------------------
inline float3 lumen_triplanar_weights(float3 n, float sharpness)
{
	float3 w = pow(abs(n), sharpness);
	return w / max(w.x + w.y + w.z, 1e-4);
}

// --------------------------------------------------------------------------------------------------
// Wind: two cheap travelling waves plus a noise gust; amplitude is expected to be scaled by the
// caller with vertex height / vertex colour. Returns a world space displacement direction * amount.
// --------------------------------------------------------------------------------------------------
float3 lumen_wind_offset(float3 worldPos, float time, float3 windDir, float strength, float frequency)
{
	float phase = dot(worldPos.xz, windDir.xz) * frequency;
	float sway = sin(phase + time * 1.7) * 0.6 + sin(phase * 2.31 + time * 3.1) * 0.4;
	float gust = lumen_value_noise(float2(phase * 0.35 - time * 0.45, worldPos.y * 0.05)) * 2.0 - 1.0;
	float flutter = sin(time * 9.0 + dot(worldPos, float3(12.9898, 78.233, 37.719)));
	float3 dir = normalize(windDir + float3(0, 0.0001, 0));
	return dir * (sway * 0.65 + gust * 0.8) * strength + float3(flutter * 0.04, flutter * 0.02, -flutter * 0.04) * strength;
}

// --------------------------------------------------------------------------------------------------
// Sun glint ("sparkle"): micro-facet glitter on wet / frosted / sandy / metallic flake surfaces.
// A hashed cell grid in world space picks a random micro normal; the cell sparkles only when that
// normal is close to the half vector between view and sun. Cheap (one hash + a few dots), temporally
// stable because it depends on world position, and automatically fades with distance.
// --------------------------------------------------------------------------------------------------
float lumen_sun_glint(float3 worldPos, float3 N, float3 V, float3 L, float density, float sharpness, float distanceFade)
{
	float3 cellP = floor(worldPos * density);
	float3 h = normalize(L + V);
	float acc = 0.0;
	// two jittered layers hide the grid
	[unroll]
	for (int i = 0; i < 2; ++i)
	{
		float3 c = cellP + (i == 0 ? 0.0 : 0.5);
		float3 r = float3(lumen_hash31(c), lumen_hash31(c + 7.1), lumen_hash31(c + 13.7)) * 2.0 - 1.0;
		float3 microN = normalize(N + r * 0.85);
		float spec = pow(saturate(dot(microN, h)), sharpness);
		float flicker = lumen_hash31(c + 29.0);
		acc += spec * smoothstep(0.55, 1.0, flicker);
	}
	return acc * saturate(1.0 - distanceFade);
}

// --------------------------------------------------------------------------------------------------
// Gerstner waves: analytic displacement + normal for up to 4 waves, shared by water vertex and pixel
// shaders so geometry and shading agree. wave = (dirX, dirZ, steepness, wavelength)
// --------------------------------------------------------------------------------------------------
struct LumenWaveResult
{
	float3 offset;
	float3 normal;
	float foam; // 0..1 crest indicator
};

LumenWaveResult lumen_gerstner(float2 xz, float time, float4 w0, float4 w1, float4 w2, float4 w3, float amplitude)
{
	LumenWaveResult r;
	r.offset = 0;
	float3 tangent = float3(1, 0, 0);
	float3 binormal = float3(0, 0, 1);
	float foam = 0;
	float4 waves[4] = {w0, w1, w2, w3};
	[unroll]
	for (int i = 0; i < 4; ++i)
	{
		float4 w = waves[i];
		float2 d = normalize(w.xy + 1e-5);
		float k = 6.2831853 / max(w.w, 0.01);
		float c = sqrt(9.81 / k);
		float f = k * (dot(d, xz) - c * time);
		float a = amplitude * w.z / k; // steepness * wavelength / 2pi == classical q/k
		float s, co;
		sincos(f, s, co);
		r.offset += float3(d.x * a * co, a * s, d.y * a * co);
		tangent += float3(-d.x * d.x * w.z * s, d.x * w.z * co, -d.x * d.y * w.z * s);
		binormal += float3(-d.x * d.y * w.z * s, d.y * w.z * co, -d.y * d.y * w.z * s);
		foam += saturate(-s) * w.z;
	}
	r.normal = normalize(cross(binormal, tangent));
	r.foam = saturate(foam * 0.8);
	return r;
}

#endif // LUMEN_COMMON_HLSLI
