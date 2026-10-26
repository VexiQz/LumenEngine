// Lumen unlit: base colour + emissive, still fogged. For UI in world space, markers, light shafts cards.
#define LUMEN_USE_LIGHT_HOOK
#include "lumen_object.hlsli"

void LumenLight(inout LumenContext c)
{
	c.color.rgb = c.surface.baseColor.rgb + c.surface.emissiveColor;
	c.color.a = c.surface.baseColor.a;
}
#include "lumen_object_ps.hlsli"
