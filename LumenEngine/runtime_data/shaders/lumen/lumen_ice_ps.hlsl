// Lumen ice / frosted crystal: internal parallax-ish depth layers, frost sparkle and sun glints.
#define LUMEN_USE_SURFACE_HOOK
#define LUMEN_USE_LIGHT_HOOK
#include "lumen_object.hlsli"

void LumenSurface(inout LumenContext c)
{
	// Fake internal structure: sample noise along the view ray at three depths and combine.
	float3 ray = -c.V;
	float3 p0 = c.P;
	float cracks = 0;
	[unroll]
	for (int i = 1; i <= 3; ++i)
	{
		float3 p = p0 + ray * (i * 0.06);
		float n = lumen_fbm(p * 4.0, 3);
		cracks += smoothstep(0.52, 0.5, abs(n - 0.5) + 0.02 * i) * (1.0 / i);
	}
	float frost = lumen_fbm(c.P * 14.0, 3);
	float3 deep = float3(0.45, 0.72, 0.9);
	float3 shallow = float3(0.85, 0.95, 1.0);
	float fres = pow(1.0 - saturate(dot(c.surface.N, c.V)), 3.0);
	c.surface.albedo = lerp(deep, shallow, saturate(frost * 1.2 + fres));
	c.surface.roughness = lerp(0.06, 0.5, saturate(frost * frost * 2.0));
	c.surface.f0 = (half3)0.028;
	c.surface.emissiveColor += float3(0.12, 0.22, 0.3) * cracks * 0.5;
	c.surface.N = lumen_blend_normals(c.surface.N, lumen_normal_from_gradient(float2(lumen_value_noise(c.P.xz * 30.0) - 0.5, lumen_value_noise(c.P.zy * 30.0) - 0.5), 0.25));
}

void LumenLight(inout LumenContext c)
{
	float sparkle = lumen_sun_glint(c.P, c.surface.N, c.V, c.sunDirection, 55.0, 400.0, c.viewDistance / 40.0);
	c.addSpecular += c.sunColor * sparkle * 0.5;
}
#include "lumen_object_ps.hlsli"
