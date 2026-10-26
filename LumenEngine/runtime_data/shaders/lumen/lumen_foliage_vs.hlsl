// Lumen foliage vertex shader: hierarchical wind (trunk sway, branch bend, leaf flutter).
// Vertex colour convention (same as most tree tools): R = branch phase, G = edge flutter weight, B = unused,
// A = bend weight (0 at the root, 1 at the tips). Meshes without painted vertex colours (A == 1 everywhere) bend
// uniformly; paint the alpha channel of the trunk base to 0 to pin it.
#define LUMEN_USE_VERTEX_HOOK
#include "lumen_object.hlsli"

#ifndef LUMEN_WIND_DIR
#define LUMEN_WIND_DIR float3(0.8, 0.0, 0.6)
#endif
#ifndef LUMEN_WIND_STRENGTH
#define LUMEN_WIND_STRENGTH 0.35
#endif
#ifndef LUMEN_WIND_FREQUENCY
#define LUMEN_WIND_FREQUENCY 0.22
#endif

void LumenVertexHook(inout LumenVertex v)
{
	float3 world = v.position.xyz;
	float bend = v.color.a;
	bend = lerp(bend, bend * bend, 0.5);

	float3 sway = lumen_wind_offset(world, v.time, normalize(LUMEN_WIND_DIR), LUMEN_WIND_STRENGTH, LUMEN_WIND_FREQUENCY);
	float flutter = v.color.g;
	float phase = v.color.r * 6.2831853;
	float3 leaf = float3(sin(v.time * 7.3 + phase + world.x), sin(v.time * 6.1 + phase * 1.7 + world.z), sin(v.time * 8.9 + phase)) * 0.045 * flutter * LUMEN_WIND_STRENGTH * 3.0;

	v.position.xyz += (sway * bend + leaf);
	// keep branch lengths: re-project the displaced tip back so tall plants bend instead of stretching
	v.position.y -= length(sway) * bend * bend * 0.25;
	// slightly tilt normals with the bend so lighting reacts
	v.normal = normalize(v.normal + sway * bend * 0.35);
}
#include "lumen_object_vs.hlsli"
