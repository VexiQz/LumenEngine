// Lumen foliage pixel shader: alpha tested leaves with translucency (light passing through the leaf),
// two sided normal handling and a subtle top-down colour gradient. Enable alpha test on the material.
#define LUMEN_USE_SURFACE_HOOK
#define LUMEN_USE_LIGHT_HOOK
#include "lumen_object.hlsli"

#ifndef LUMEN_LEAF_TRANSLUCENCY
#define LUMEN_LEAF_TRANSLUCENCY 0.9
#endif

void LumenSurface(inout LumenContext c)
{
	// Fake ambient occlusion from the vertex bend weight (darker towards the trunk).
	float ao = lerp(0.55, 1.0, saturate(c.input.color.a));
	c.surface.occlusion *= ao;
	// Leaves reflect little; keep specular wide and soft.
	c.surface.roughness = max(c.surface.roughness, 0.55);
	c.surface.f0 *= 0.5;
	// Sunlit side turns slightly yellow-green.
	float tint = saturate(dot(c.surface.N, c.sunDirection) * 0.5 + 0.5);
	c.surface.albedo *= lerp(float3(0.92, 0.98, 0.9), float3(1.06, 1.04, 0.9), tint);
}

void LumenLight(inout LumenContext c)
{
	// Back lighting: the sun behind the leaf lights it from the other side.
	float3 L = c.sunDirection;
	float3 N = c.surface.N;
	float back = saturate(dot(-N, L));
	float viewAlign = pow(saturate(dot(c.V, -L)), 3.0);
	float3 leafColor = c.surface.albedo * float3(1.0, 1.15, 0.55);
	c.addEmissive += leafColor * c.sunColor * (back * 0.4 + viewAlign * 0.6) * LUMEN_LEAF_TRANSLUCENCY * saturate(L.y * 4.0) * 0.6;
}
#include "lumen_object_ps.hlsli"
