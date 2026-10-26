// Lumen glass: refraction with chromatic dispersion, fresnel reflection, edge tint and thickness
// approximation. Transparent; use with a material that has transmission > 0 for the engine refraction
// target to be bound, tint comes from the base colour.
#define LUMEN_TRANSPARENT
#define LUMEN_USE_SURFACE_HOOK
#define LUMEN_USE_LIGHT_HOOK
#include "lumen_object.hlsli"

#ifndef LUMEN_GLASS_DISPERSION
#define LUMEN_GLASS_DISPERSION 0.012
#endif

void LumenSurface(inout LumenContext c)
{
	c.surface.roughness = clamp(c.surface.roughness, 0.02, 0.6);
	c.surface.f0 = (half3)0.04;
	const ShaderCamera camera = c.camera;
	[branch]
	if (camera.texture_refraction_index >= 0)
	{
		Texture2D<half4> tex = bindless_textures_half4[descriptor_index(camera.texture_refraction_index)];
		float2 size; float mips;
		tex.GetDimensions(0, size.x, size.y, mips);
		float2 n2 = mul((float3x3)camera.view, c.surface.N).xy;
		float strength = max(c.material.GetRefraction(), 0.03);
		float2 uvR = c.screenUV + n2 * strength * (1.0 + LUMEN_GLASS_DISPERSION * 12.0);
		float2 uvG = c.screenUV + n2 * strength;
		float2 uvB = c.screenUV + n2 * strength * (1.0 - LUMEN_GLASS_DISPERSION * 12.0);
		float mip = c.surface.roughness * mips;
		float3 refr = float3(
			tex.SampleLevel(sampler_linear_clamp, uvR, mip).r,
			tex.SampleLevel(sampler_linear_clamp, uvG, mip).g,
			tex.SampleLevel(sampler_linear_clamp, uvB, mip).b);
		// thicker at grazing angles: more tint
		float thick = 1.0 / max(saturate(dot(c.surface.N, c.V)), 0.15);
		float3 tint = pow(max(c.material.GetBaseColor().rgb, 0.0001), thick * 0.6);
		c.surface.refraction.rgb = refr * tint;
		c.surface.refraction.a = 1.0;
	}
	c.surface.baseColor.a = 1;
}

void LumenLight(inout LumenContext c)
{
	// Sharp sun highlight streak
	float3 H = normalize(c.sunDirection + c.V);
	float s = pow(saturate(dot(c.surface.N, H)), 700.0);
	c.addSpecular += c.sunColor * s * 3.0;
}
#include "lumen_object_ps.hlsli"
