// Lumen PBR+ : Wicked PBR with sun glints, snow/dust accumulation and a cheap detail normal.
// Material parameters are read from the standard material fields so the shader works with any
// imported model:
//   texMulAdd.zw   -> unused here
//   GetSaturation  -> colour saturation (engine standard)
#define LUMEN_USE_SURFACE_HOOK
#define LUMEN_USE_LIGHT_HOOK
#include "lumen_object.hlsli"

#ifndef LUMEN_GLINT_DENSITY
#define LUMEN_GLINT_DENSITY 38.0
#endif
// Define LUMEN_SNOW (and optionally LUMEN_SNOW_AMOUNT 0..1) to enable snow accumulation.
#ifndef LUMEN_SNOW_AMOUNT
#define LUMEN_SNOW_AMOUNT 1.0
#endif

void LumenSurface(inout LumenContext c)
{
	// Micro detail so low-res normal maps keep sparkling up close. Fades out with distance.
	float detailFade = saturate(1.0 - c.viewDistance / 18.0);
	[branch]
	if (detailFade > 0.001)
	{
		float2 p = c.P.xz * 9.0 + c.P.y * 3.0;
		float h0 = lumen_value_noise(p);
		float hx = lumen_value_noise(p + float2(0.07, 0));
		float hy = lumen_value_noise(p + float2(0, 0.07));
		float3 detail = lumen_normal_from_gradient(float2(hx - h0, hy - h0) * 14.0, 0.5 * detailFade);
		c.surface.N = lumen_blend_normals(c.surface.N, detail);
	}

	// Snow settles on upward facing surfaces.
#ifdef LUMEN_SNOW
	float up = saturate(c.surface.N.y * 1.2 - 0.25);
	float snowMask = smoothstep(0.35, 0.75, up * (0.65 + 0.7 * lumen_value_noise(c.P.xz * 2.3))) * LUMEN_SNOW_AMOUNT;
	c.surface.albedo = lerp(c.surface.albedo, float3(0.93, 0.95, 1.0), snowMask);
	c.surface.roughness = lerp(c.surface.roughness, 0.55, snowMask);
	c.surface.f0 = lerp(c.surface.f0, (half3)0.04, snowMask);
#endif
}

void LumenLight(inout LumenContext c)
{
	// Sun glints: sparkling micro facets on rough dielectrics (sand, snow, frost, wet stone).
	float3 L = c.sunDirection;
	float sunVisible = saturate(dot(c.surface.N, L) * 4.0);
	float sparkle = lumen_sun_glint(c.P, c.surface.N, c.V, L, LUMEN_GLINT_DENSITY, 260.0, c.viewDistance / 60.0);
	float glossy = saturate(1.2 - c.surface.roughness);
	c.addSpecular += c.sunColor * sparkle * sunVisible * glossy * 0.35;
}
#include "lumen_object_ps.hlsli"
