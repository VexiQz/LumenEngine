// Lumen toon / cel shading: quantised sun diffuse, rim light and specular highlight blob.
// The lit colour of Wicked is re-ramped so all lights (point, spot, shadows) take part.
#define LUMEN_USE_LIGHT_HOOK
#include "lumen_object.hlsli"

#ifndef LUMEN_TOON_BANDS
#define LUMEN_TOON_BANDS 3.0
#endif
#ifndef LUMEN_TOON_RIM
#define LUMEN_TOON_RIM 0.6
#endif

void LumenLight(inout LumenContext c)
{
	// Re-ramp the luminance of the lit colour into hard bands; chroma is preserved.
	float l = max(lumen_luma(c.color.rgb), 1e-4);
	float x = l * LUMEN_TOON_BANDS;
	float aa = max(fwidth(x) * 0.75, 0.02);
	float banded = (floor(x) + smoothstep(0.5 - aa, 0.5 + aa, frac(x)) + 0.0) / LUMEN_TOON_BANDS;
	float3 col = (c.color.rgb / l) * banded;

	float3 N = c.surface.N;
	float rim = pow(1.0 - saturate(dot(N, c.V)), 3.0) * saturate(dot(N, c.sunDirection) + 0.4);
	col += c.sunColor * rim * LUMEN_TOON_RIM * 0.25;

	float3 H = normalize(c.sunDirection + c.V);
	float spec = smoothstep(0.96, 0.965, dot(N, H)) * (1.0 - c.surface.roughness);
	col += c.sunColor * spec * 0.15;

	c.color.rgb = col;
}
#include "lumen_object_ps.hlsli"
