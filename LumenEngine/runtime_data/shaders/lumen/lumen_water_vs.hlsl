// Lumen water vertex shader: Gerstner displacement of every vertex of the water mesh.
#define LUMEN_USE_VERTEX_HOOK
#include "lumen_object.hlsli"
#include "lumen_water.hlsli"

void LumenVertexHook(inout LumenVertex v)
{
	LumenWaveResult w = lumen_water_waves(v.position.xz, v.time);
	// Waves fade out over vertex colour alpha (paint shorelines with alpha 0 to calm the water there).
	float calm = saturate(v.color.a);
	v.position.xyz += w.offset * calm;
	v.normal = normalize(lerp(float3(0, 1, 0), w.normal, calm));
	v.color.rgb = saturate(w.foam * calm).xxx; // crest foam factor travels to the pixel shader in colour.rgb
}
#include "lumen_object_vs.hlsli"
