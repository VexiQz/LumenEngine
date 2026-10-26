// Lumen dissolve: noise driven burn-away. Progress comes from the material's alpha-test slider (0 = intact,
// 1 = gone) so it can be animated from Lua with material:SetAlphaCutoff(). The edge glows with the
// material's emissive colour. Use with alpha testing enabled on the material.
#define LUMEN_USE_SURFACE_HOOK
#include "lumen_object.hlsli"

#ifndef LUMEN_DISSOLVE_EDGE
#define LUMEN_DISSOLVE_EDGE 0.06
#endif

void LumenSurface(inout LumenContext c)
{
	float progress = saturate(c.material.GetAlphaTest());
	float n = lumen_fbm(c.P * 3.1, 4) * 0.8 + lumen_value_noise(c.P * 17.0) * 0.2;
	float edge = n - progress * (1.0 + LUMEN_DISSOLVE_EDGE);
	// discard (alpha to coverage compatible when alpha test is on)
	if (edge < 0)
		c.surface.baseColor.a = 0;
	float glow = smoothstep(LUMEN_DISSOLVE_EDGE, 0.0, edge) * step(0.0, edge);
	float3 emissive = c.material.GetEmissive();
	emissive = any(emissive) ? emissive : float3(1.0, 0.45, 0.1);
	c.surface.emissiveColor += emissive * glow * 6.0;
	c.surface.albedo = lerp(c.surface.albedo, c.surface.albedo * 0.15, glow);
}
#include "lumen_object_ps.hlsli"
