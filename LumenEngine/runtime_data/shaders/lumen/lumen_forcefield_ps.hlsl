// Lumen force field: hex-ish energy grid, animated scan line, fresnel rim and contact glow against geometry.
// Additive transparent. Emissive colour of the material is the field colour.
#define LUMEN_TRANSPARENT
#define LUMEN_USE_SURFACE_HOOK
#define LUMEN_USE_COLOR_HOOK
#include "lumen_object.hlsli"

static float g_ff_mask;

float lumen_hex_grid(float2 p)
{
	const float2 s = float2(1.7320508, 1.0);
	float2 a = fmod(p, s) - s * 0.5;
	float2 b = fmod(p + s * 0.5, s) - s * 0.5;
	float2 g = dot(a, a) < dot(b, b) ? a : b;
	float2 q = abs(g);
	float d = max(dot(q, normalize(float2(1.0, 1.7320508))), q.x);
	return smoothstep(0.42, 0.5, d);
}

void LumenSurface(inout LumenContext c)
{
	float fres = pow(1.0 - saturate(dot(c.surface.N, c.V)), 2.5);
	float hex = lumen_hex_grid(c.P.xz * 2.2 + c.P.y * 1.3);
	float scan = smoothstep(0.92, 1.0, sin(c.P.y * 3.0 - c.time * 1.6) * 0.5 + 0.5);
	float pulse = 0.65 + 0.35 * sin(c.time * 2.2 + lumen_value_noise(c.P.xz * 0.6) * 6.28);
	// contact glow where the field intersects scene geometry
	float contact = 0;
	[branch]
	if (c.camera.texture_depth_index >= 0)
	{
		float sceneZ = texture_depth.SampleLevel(sampler_point_clamp, c.screenUV, 0);
		float3 sp = reconstruct_position(c.screenUV, sceneZ);
		contact = saturate(1.0 - length(sp - c.P) * 2.5);
	}
	g_ff_mask = saturate(fres * 0.8 + hex * 0.55 * pulse + scan * 0.5 + contact * 1.2);
	c.surface.baseColor.a = g_ff_mask;
	c.surface.emissiveColor = c.material.GetEmissive() * (0.4 + g_ff_mask * 2.2);
	c.surface.albedo = 0;
	c.surface.f0 = 0;
}

void LumenColor(inout LumenContext c)
{
	c.color.rgb = c.surface.emissiveColor;
	c.color.a = g_ff_mask;
}
#include "lumen_object_ps.hlsli"
