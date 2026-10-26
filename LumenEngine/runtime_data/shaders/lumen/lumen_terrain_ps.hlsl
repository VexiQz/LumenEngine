// Lumen triplanar terrain: layers chosen by slope and height with noise breakup, no UVs needed.
// Uses the material's base colour map as the rock layer (triplanar), procedural colours for the rest so a
// terrain mesh looks good even with no textures assigned.
//   texMulAdd.xy  texture tiling (metres per repeat = 1 / value)
#define LUMEN_USE_SURFACE_HOOK
#include "lumen_object.hlsli"

#ifndef LUMEN_GRASS_COLOR
#define LUMEN_GRASS_COLOR float3(0.22, 0.34, 0.09)
#endif
#ifndef LUMEN_DIRT_COLOR
#define LUMEN_DIRT_COLOR float3(0.30, 0.22, 0.14)
#endif
#ifndef LUMEN_ROCK_COLOR
#define LUMEN_ROCK_COLOR float3(0.36, 0.35, 0.34)
#endif
#ifndef LUMEN_SNOW_LINE
#define LUMEN_SNOW_LINE 120.0
#endif

float3 lumen_triplanar_sample(ShaderMaterial m, float3 P, float3 N, float tiling)
{
	float3 w = lumen_triplanar_weights(N, 4.0);
	float3 cx = m.textures[BASECOLORMAP].SampleLevel(sampler_objectshader, (P.zy * tiling).xyxy, 0).rgb;
	float3 cy = m.textures[BASECOLORMAP].SampleLevel(sampler_objectshader, (P.xz * tiling).xyxy, 0).rgb;
	float3 cz = m.textures[BASECOLORMAP].SampleLevel(sampler_objectshader, (P.xy * tiling).xyxy, 0).rgb;
	return cx * w.x + cy * w.y + cz * w.z;
}

void LumenSurface(inout LumenContext c)
{
	float3 N = normalize(c.input.nor);
	float3 P = c.P;
	float slope = 1.0 - saturate(N.y);
	float breakup = lumen_fbm(P.xz * 0.07, 4);
	float detail = lumen_fbm(P.xz * 1.9, 3);
	float tiling = 1.0 / max(c.material.texMulAdd.x, 0.001);

	float3 rock = LUMEN_ROCK_COLOR * (0.75 + 0.5 * detail);
	[branch]
	if (c.material.textures[BASECOLORMAP].IsValid())
		rock = lumen_triplanar_sample(c.material, P, N, tiling * 0.2) * (0.8 + 0.4 * detail);
	float3 grass = LUMEN_GRASS_COLOR * (0.7 + 0.6 * detail) * (0.8 + 0.4 * breakup);
	float3 dirt = LUMEN_DIRT_COLOR * (0.8 + 0.4 * detail);

	float rockMask = smoothstep(0.28, 0.45, slope + (breakup - 0.5) * 0.18);
	float dirtMask = smoothstep(0.55, 0.8, breakup + slope * 0.4) * (1.0 - rockMask);
	float3 albedo = lerp(grass, dirt, dirtMask);
	albedo = lerp(albedo, rock, rockMask);

	float snow = smoothstep(LUMEN_SNOW_LINE, LUMEN_SNOW_LINE + 25.0, P.y + (breakup - 0.5) * 30.0) * (1.0 - rockMask * 0.7) * smoothstep(0.7, 0.3, slope);
	albedo = lerp(albedo, float3(0.92, 0.94, 0.98), snow);

	c.surface.albedo = albedo;
	c.surface.roughness = lerp(lerp(0.9, 0.75, rockMask), 0.5, snow);
	c.surface.f0 = lerp((half3)0.02, (half3)0.04, rockMask + snow);
	// Detail normal from the same noise, cheap and stable in world space.
	float e = 0.15;
	float h0 = lumen_fbm(P.xz * 3.0, 3);
	float hx = lumen_fbm(P.xz * 3.0 + float2(e, 0), 3);
	float hy = lumen_fbm(P.xz * 3.0 + float2(0, e), 3);
	float fade = saturate(1.0 - c.viewDistance / 60.0);
	float3 dn = lumen_normal_from_gradient(float2(hx - h0, hy - h0) / e, 0.35 * fade);
	c.surface.N = lumen_blend_normals(c.surface.N, dn);
}
#include "lumen_object_ps.hlsli"
