// Lumen Engine — object shader framework.
//
// A Lumen material shader is a small .hlsl file that
//   1. #defines which hooks it implements (LUMEN_USE_VERTEX_HOOK / _UV_HOOK / _SURFACE_HOOK / _LIGHT_HOOK / _COLOR_HOOK),
//   2. includes this file, then defines the hook functions,
//   3. includes lumen_object_vs.hlsli (vertex stage) or lumen_object_ps.hlsli (pixel stage).
//
// Pipeline order in the pixel shader:
//   vertex hook  -> (VS) displace position / normal / colour
//   UV hook      -> animate or warp uvsets before any texture is sampled
//   (textures, decals, normal map, surface.create)
//   surface hook -> change albedo / normal / roughness / metalness / emissive / alpha, add procedural detail
//   (lights, SSR, AO, GI)
//   light hook   -> add effects that depend on lighting results (rim, translucency, glints, foam, toon ramp)
//   (fog, saturation, alpha coverage)
//   color hook   -> last chance to touch the final colour
//
// All hooks receive a LumenContext, which exposes everything Wicked computed so far.
#ifndef LUMEN_OBJECT_HLSLI
#define LUMEN_OBJECT_HLSLI

#define OBJECTSHADER_LAYOUT_COMMON
#ifndef LUMEN_NO_TILEDFORWARD
#define TILEDFORWARD
#endif
#ifdef LUMEN_TRANSPARENT
#define TRANSPARENT
#endif
#ifdef LUMEN_PLANAR_REFLECTION
#define PLANARREFLECTION
#endif

#include "objectHF.hlsli"
#include "lumen_common.hlsli"

// Per-pixel data visible to hooks.
struct LumenContext
{
	PixelInput input;
	ShaderCamera camera;
	ShaderMaterial material;
	ShaderMeshInstance instance;
	Surface surface;
	float4 uvsets;          // uv set 0 in xy, uv set 1 in zw
	float3x3 TBN;           // unnormalised tangent frame (valid when the layout has tangents)
	float3 P;               // world position
	float3 V;               // normalised vector to the eye
	float viewDistance;     // distance from the eye in metres
	float2 screenUV;        // 0..1
	uint2 pixel;
	bool frontFace;
	float time;             // seconds
	float3 sunDirection;    // direction *towards* the sun
	float3 sunColor;        // sun colour with intensity applied
	half3 addEmissive;      // added to the final colour after lighting, not affected by lights
	half3 addSpecular;      // added after lighting (glints); multiplied by nothing
	half4 color;            // lit colour (valid in light / colour hooks)
};

#ifndef LUMEN_VERTEX_CONTEXT_DEFINED
#define LUMEN_VERTEX_CONTEXT_DEFINED
// Per-vertex data visible to the vertex hook.
struct LumenVertex
{
	float4 position;   // world space position (w = 1)
	float3 normal;     // world space normal
	float4 tangent;
	float4 color;
	float4 uvsets;
	float time;
	float ao;
	float wet;
	float2 atlas;
	uint cameraIndex;
};
#endif

#endif // LUMEN_OBJECT_HLSLI
