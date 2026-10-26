// Lumen water: shared wave definition so the vertex displacement and the pixel normals always agree.
#ifndef LUMEN_WATER_HLSLI
#define LUMEN_WATER_HLSLI

#include "lumen_common.hlsli"

// Four Gerstner waves: (dirX, dirZ, steepness, wavelength in metres). Override before including to
// change the sea state; the defaults are a moderate open water swell with a cross chop.
#ifndef LUMEN_WAVE_0
#define LUMEN_WAVE_0 float4( 1.00,  0.20, 0.22, 17.0)
#endif
#ifndef LUMEN_WAVE_1
#define LUMEN_WAVE_1 float4( 0.65,  0.85, 0.18,  9.5)
#endif
#ifndef LUMEN_WAVE_2
#define LUMEN_WAVE_2 float4(-0.40,  0.95, 0.12,  4.8)
#endif
#ifndef LUMEN_WAVE_3
#define LUMEN_WAVE_3 float4( 0.90, -0.55, 0.08,  2.3)
#endif
#ifndef LUMEN_WAVE_AMPLITUDE
#define LUMEN_WAVE_AMPLITUDE 0.35
#endif
#ifndef LUMEN_WAVE_SPEED
#define LUMEN_WAVE_SPEED 1.0
#endif

LumenWaveResult lumen_water_waves(float2 xz, float time)
{
	return lumen_gerstner(xz, time * LUMEN_WAVE_SPEED, LUMEN_WAVE_0, LUMEN_WAVE_1, LUMEN_WAVE_2, LUMEN_WAVE_3, LUMEN_WAVE_AMPLITUDE);
}

#endif // LUMEN_WATER_HLSLI
