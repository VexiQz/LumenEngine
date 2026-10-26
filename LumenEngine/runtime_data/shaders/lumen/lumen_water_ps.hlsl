// Lumen water pixel shader.
//
// Material mapping (so it works from the material inspector without extra parameters):
//   base colour rgb   shallow water tint           base colour a    density: higher = murkier
//   sheen colour      extinction (deep colour absorption per metre; Wicked repurposes this for water)
//   refraction        refraction strength          normal map strength   ripple strength
//   normal map        optional detail ripples (scrolled twice in opposite directions); procedural when absent
//   texMulAdd.zw      scroll speed of the ripples
// Requires the material to keep shaderType WATER so the engine renders planar reflection + refraction targets.
//
// Features: Gerstner normals (matching the vertex displacement), depth based absorption with scattering,
// shoreline + crest foam, planar reflection with fresnel, sky fallback reflection, sun glitter, subsurface
// glow through wave crests.
#define LUMEN_TRANSPARENT
#define LUMEN_USE_SURFACE_HOOK
#define LUMEN_USE_LIGHT_HOOK
#include "lumen_object.hlsli"
#include "lumen_water.hlsli"

#ifndef LUMEN_FOAM_DEPTH
#define LUMEN_FOAM_DEPTH 0.45
#endif
#ifndef LUMEN_WATER_SCATTER
#define LUMEN_WATER_SCATTER float3(0.02, 0.22, 0.26)
#endif

static float g_waterFoam;
static float g_waterDepth;
static float g_waterCrest;

// Normal of the ripple layer; either the material normal map (scrolled twice) or fBm gradients.
float3 lumen_water_ripples(LumenContext c, float2 scroll, float strength, float fade)
{
	float2 p = c.P.xz;
	float2 g = 0;
	ShaderMaterial m = c.material;
	[branch]
	if (m.textures[NORMALMAP].IsValid())
	{
		float2 uv0 = (m.textures[NORMALMAP].GetUVSet() == 0 ? c.uvsets.xy : c.uvsets.zw);
		float2 a = m.textures[NORMALMAP].Sample(sampler_objectshader, float4(uv0 + scroll * c.time, uv0 - scroll * c.time * 0.7).xyxy).rg * 2 - 1;
		float2 b = m.textures[NORMALMAP].Sample(sampler_objectshader, float4(uv0 * 2.17 - scroll.yx * c.time * 1.3, 0, 0).xyxy).rg * 2 - 1;
		g = a + b * 0.5;
	}
	else
	{
		float t = c.time;
		float e = 0.12;
		float2 q0 = p * 1.7 + scroll * t * 6.0;
		float2 q1 = p * 3.9 - scroll.yx * t * 7.0;
		float h00 = lumen_fbm(q0, 3) + lumen_fbm(q1, 2) * 0.5;
		float hx = lumen_fbm(q0 + float2(e, 0), 3) + lumen_fbm(q1 + float2(e, 0), 2) * 0.5;
		float hy = lumen_fbm(q0 + float2(0, e), 3) + lumen_fbm(q1 + float2(0, e), 2) * 0.5;
		g = float2(hx - h00, hy - h00) / e * 0.35;
	}
	return normalize(float3(-g * strength * fade, 1));
}

void LumenSurface(inout LumenContext c)
{
	const ShaderCamera camera = c.camera;
	float3 P = c.P;

	// --- normals ------------------------------------------------------------------------------------
	LumenWaveResult w = lumen_water_waves(P.xz, c.time);
	float rippleFade = saturate(1.0 - c.viewDistance / 80.0) * 0.8 + 0.2;
	float3 rippleTS = lumen_water_ripples(c, c.material.texMulAdd.zw + float2(0.015, 0.01), c.material.GetNormalMapStrength() * 1.0 + 0.0001, rippleFade);
	float3 wn = w.normal;
	// distant water flattens to avoid shimmering sparkle
	wn = normalize(lerp(wn, float3(0, 1, 0), saturate(c.viewDistance / 600.0)));
	float3 N = lumen_blend_normals(wn.xzy, float3(rippleTS.x, rippleTS.z, rippleTS.y)).xzy;
	N = normalize(N);
	if (c.surface.V.y < 0) // seen from below
		N = -N;
	c.surface.N = N;
	g_waterCrest = saturate(c.input.color.r);

	// --- refraction + absorption -----------------------------------------------------------------------
	float4 plane = camera.reflection_plane;
	float4 base = c.material.GetBaseColor();
	float density = max(base.a, 0.01) * 2.0;
	float3 extinction = max(c.material.GetSheenColor().rgb, 0.0001);
	extinction = any(extinction > 0.001) ? extinction : float3(0.35, 0.09, 0.05);

	float3 refracted = 0;
	float waterDepth = 4.0;
	[branch]
	if (camera.texture_refraction_index >= 0)
	{
		Texture2D<half4> texRefr = bindless_textures_half4[descriptor_index(camera.texture_refraction_index)];
		float2 distort = mul((float3x3)camera.view, N).xy * c.material.GetRefraction() * 0.5;
		float2 uv = c.screenUV + distort;
		float d = find_max_depth(uv, 2, 2);
		float3 refPos = reconstruct_position(uv, d);
		float thickness = abs(dot(plane.xyz, P - refPos));
		// Samples that lie in front of the water surface (objects above it) must not be distorted.
		float viewDist = length(camera.position - P);
		float sceneDist = length(camera.position - refPos);
		if (sceneDist < viewDist - 0.02)
		{
			uv = c.screenUV;
			d = texture_depth.SampleLevel(sampler_point_clamp, uv, 0);
			refPos = reconstruct_position(uv, d);
			thickness = abs(dot(plane.xyz, P - refPos));
			if (length(camera.position - refPos) < viewDist - 0.02)
				thickness = 0;
		}
		waterDepth = thickness;
		float lodBlur = saturate(thickness * 0.15) * 3.0;
		refracted = texRefr.SampleLevel(sampler_linear_mirror, uv, lodBlur).rgb;
	}
	g_waterDepth = waterDepth;

	float3 transmittance = exp(-waterDepth * extinction * density);
	float3 scatter = LUMEN_WATER_SCATTER * base.rgb * (1.0 - exp(-waterDepth * density * 0.5));
	// In-scattered light follows the sun elevation so night water goes dark.
	float daylight = saturate(c.sunDirection.y * 2.0 + 0.25);
	scatter *= (GetAmbientColor() * 0.6 + c.sunColor * 0.08) * daylight + 0.01;
	refracted = refracted * transmittance * lerp(1.0, base.rgb, 0.35) + scatter;

	// --- foam ------------------------------------------------------------------------------------------
	float2 fp = P.xz * 1.8 + c.time * float2(0.08, 0.05);
	float foamNoise = lumen_fbm(fp, 4);
	float shore = 1.0 - saturate(waterDepth / LUMEN_FOAM_DEPTH);
	float shoreFoam = smoothstep(0.35, 0.75, shore * (0.6 + foamNoise));
	// Foam lines that move with the waves near the shore.
	float lines = smoothstep(0.7, 1.0, sin(waterDepth * 22.0 - c.time * 2.4 + foamNoise * 6.0) * 0.5 + 0.5) * smoothstep(1.2, 0.2, waterDepth);
	float crestFoam = smoothstep(0.55, 0.95, g_waterCrest * (0.7 + foamNoise * 0.6));
	float foam = saturate(shoreFoam + lines * 0.5 + crestFoam * 0.8);
	g_waterFoam = foam;

	c.surface.refraction.rgb = refracted;
	c.surface.refraction.a = 1.0 - foam;           // foam replaces the see-through look with diffuse white
	c.surface.albedo = lerp((half3)0.0, (half3)float3(0.92, 0.95, 0.98), foam);
	c.surface.f0 = (half3)0.02;
	c.surface.roughness = lerp(0.03, 0.85, foam);
	c.surface.baseColor.a = 1;
	c.surface.occlusion = 1;
}

void LumenLight(inout LumenContext c)
{
	const ShaderCamera camera = c.camera;
	float3 N = c.surface.N;
	float3 V = c.V;
	float NdotV = saturate(dot(N, V));
	float3 F = lumen_fresnel_schlick(NdotV, float3(0.02, 0.02, 0.02));
	float calmFoam = 1.0 - g_waterFoam;

	// --- reflection --------------------------------------------------------------------------------------
	float3 reflection = 0;
	bool havePlanar = false;
	[branch]
	if (camera.texture_reflection_index >= 0)
	{
		float4 ruv = mul(camera.reflection_view_projection, float4(c.P, 1));
		float2 uv = clipspace_to_uv(ruv.xy / ruv.w);
		uv += (N.xz - c.input.nor.xz * 0.0) * 0.045 * saturate(1.0 - c.viewDistance / 250.0);
		reflection = bindless_textures_half4[descriptor_index(camera.texture_reflection_index)].SampleLevel(sampler_linear_mirror, uv, 0).rgb;
		havePlanar = true;
	}
	if (!havePlanar)
	{
		// Analytic sky: horizon -> zenith gradient along the reflection vector plus a sun disc.
		float3 R = reflect(-V, N);
		float t = saturate(R.y);
		reflection = lerp(GetHorizonColor(), GetZenithColor(), pow(t, 0.45));
		reflection += c.sunColor * pow(saturate(dot(R, c.sunDirection)), 900.0) * 6.0;
	}
	c.addSpecular += reflection * F * calmFoam;

	// --- sun glitter -------------------------------------------------------------------------------------
	float3 L = c.sunDirection;
	float3 H = normalize(L + V);
	float nh = saturate(dot(N, H));
	float rough = 0.02;
	float a2 = rough * rough;
	float dg = nh * nh * (a2 - 1.0) + 1.0;
	float D = a2 / (PI * dg * dg);
	float glitter = D * saturate(dot(N, L)) * 0.25;
	// stochastic sparkle on top of the analytic lobe
	float sparkle = lumen_sun_glint(c.P, N, V, L, 6.0, 90.0, c.viewDistance / 140.0);
	float sunVisible = saturate(L.y * 8.0);
	c.addSpecular += c.sunColor * (min(glitter, 40.0) + sparkle * 0.6) * F.g * 6.0 * sunVisible * calmFoam;

	// --- subsurface glow through crests -----------------------------------------------------------------
	float crest = saturate(g_waterCrest * 1.5);
	float backlight = pow(saturate(dot(V, -L + N * 0.35)), 4.0);
	c.addEmissive += LUMEN_WATER_SCATTER * c.sunColor * backlight * crest * 0.6 * sunVisible * calmFoam;
}
#include "lumen_object_ps.hlsli"
