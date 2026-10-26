// Lumen cinematic grade: one compute pass for vignette, film grain, contrast, saturation, white balance,
// lift and a light sharpen. Designed to run after Wicked's tone mapping on the LDR image (one read of the
// neighbourhood, one write), costing well under 0.2 ms at 1080p on integrated GPUs.
//
// PostProcess push constants:
//   params0 = (vignette strength, grain amount, time, saturation)
//   params1 = (contrast, warmth -1..1, lift, sharpen amount)
#include "globals.hlsli"
#include "ShaderInterop_Postprocess.h"

PUSHCONSTANT(postprocess, PostProcess);

Texture2D<float4> input : register(t0);
RWTexture2D<float4> output : register(u0);

static const float3 LUMA = float3(0.2126, 0.7152, 0.0722);

float interleaved_gradient_noise(float2 p)
{
	return frac(52.9829189 * frac(dot(p, float2(0.06711056, 0.00583715))));
}

[numthreads(POSTPROCESS_BLOCKSIZE, POSTPROCESS_BLOCKSIZE, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	if (DTid.x >= postprocess.resolution.x || DTid.y >= postprocess.resolution.y)
		return;

	const float2 uv = (DTid.xy + 0.5) * postprocess.resolution_rcp;
	const float vignette = postprocess.params0.x;
	const float grain = postprocess.params0.y;
	const float time = postprocess.params0.z;
	const float saturation = postprocess.params0.w;
	const float contrast = postprocess.params1.x;
	const float warmth = postprocess.params1.y;
	const float lift = postprocess.params1.z;
	const float sharpen = postprocess.params1.w;

	float3 c = input.SampleLevel(sampler_linear_clamp, uv, 0).rgb;

	[branch]
	if (sharpen > 0.001)
	{
		float2 o = postprocess.resolution_rcp;
		float3 blur = input.SampleLevel(sampler_linear_clamp, uv + float2(o.x, 0), 0).rgb
					+ input.SampleLevel(sampler_linear_clamp, uv - float2(o.x, 0), 0).rgb
					+ input.SampleLevel(sampler_linear_clamp, uv + float2(0, o.y), 0).rgb
					+ input.SampleLevel(sampler_linear_clamp, uv - float2(0, o.y), 0).rgb;
		blur *= 0.25;
		// limit the correction so bright edges do not ring
		float3 delta = clamp(c - blur, -0.08, 0.08);
		c += delta * sharpen * 2.0;
	}

	// white balance: warm adds red / removes blue, cool does the opposite
	c *= float3(1.0 + warmth * 0.12, 1.0 + warmth * 0.02, 1.0 - warmth * 0.12);

	// lift raises blacks with a tint toward the shadow colour
	c += lift * 0.06 * (1.0 - c);

	// contrast pivoting around mid grey
	c = (c - 0.18) * contrast + 0.18;
	c = max(c, 0.0);

	float l = dot(c, LUMA);
	c = lerp((float3)l, c, saturation);

	// vignette: smooth falloff, aspect corrected so it stays circular
	float2 d = (uv - 0.5) * float2(postprocess.resolution.x * postprocess.resolution_rcp.y, 1.0);
	float v = saturate(1.0 - dot(d, d) * vignette * 1.6);
	c *= lerp(1.0, v * v * (3.0 - 2.0 * v), saturate(vignette));

	// grain, stronger in mid tones; also acts as dither against banding
	float n = interleaved_gradient_noise(DTid.xy + frac(time) * 97.0) - 0.5;
	float n2 = interleaved_gradient_noise(DTid.yx + frac(time * 1.37) * 53.0) - 0.5;
	float grainMask = 1.0 - abs(l * 2.0 - 1.0) * 0.6;
	c += (n + n2) * (grain * 0.04 * grainMask + 1.0 / 255.0);

	output[DTid.xy] = float4(saturate(c), 1);
}
