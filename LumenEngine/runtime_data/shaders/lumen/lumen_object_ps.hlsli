// Lumen Engine — object pixel shader entry. Include after lumen_object.hlsli and your hooks.
// Mirrors Wicked's tiled forward object shader (decals, normal mapping, wetness, lightmaps, SSR, SSAO,
// SSGI, refraction for transparent materials, fog, aerial perspective, alpha to coverage) and inserts
// the Lumen hooks at the documented points.
#ifndef LUMEN_OBJECT_PS_HLSLI
#define LUMEN_OBJECT_PS_HLSLI

#ifdef DISABLE_ALPHATEST
#define LUMEN_APPEND_COVERAGE
#else
#define LUMEN_APPEND_COVERAGE , out uint coverage : SV_Coverage
#endif

#ifdef EARLY_DEPTH_STENCIL
[earlydepthstencil]
#endif
float4 main(PixelInput input, in bool is_frontface : SV_IsFrontFace LUMEN_APPEND_COVERAGE) : SV_Target
{
	LumenContext c;
	c.input = input;
	c.frontFace = is_frontface;

	const uint cameraIndex = WaveReadLaneFirst(input.GetCameraIndex());
	c.camera = GetCameraIndexed(cameraIndex);
	const ShaderCamera camera = c.camera;

	c.pixel = (uint2)input.pos.xy;
	c.screenUV = input.pos.xy * camera.internal_resolution_rcp;
	c.screenUV = input.pos.xy * camera.internal_resolution_rcp;
	c.time = GetTime();
	c.sunDirection = GetSunDirection();
	c.sunColor = GetSunColor();
	c.addEmissive = 0;
	c.addSpecular = 0;
	c.color = 0;

	Surface surface;
	surface.init();
	surface.P = input.GetPos3D();
	surface.V = input.GetViewVector();
	float dist = length(surface.V);
	surface.V /= dist;
	c.P = surface.P;
	c.V = surface.V;
	c.viewDistance = dist;

	float4 uvsets = input.GetUVSets();
	c.uvsets = uvsets;
	c.instance = load_instance(input.GetInstanceIndex());
	c.material = GetMaterial();
	c.TBN = float3x3(1, 0, 0, 0, 1, 0, 0, 0, 1);
	ShaderMaterial material = c.material;
	ShaderMeshInstance meshinstance = c.instance;

	c.surface = surface;

#ifdef LUMEN_USE_UV_HOOK
	LumenUV(c);
	uvsets = c.uvsets;
#endif

#ifdef TILEDFORWARD
	write_mipmap_feedback(push.materialIndex, ddx_coarse(uvsets), ddy_coarse(uvsets));
#endif

	surface = c.surface;

	if (!is_frontface)
		input.nor = -input.nor;
	surface.N = normalize(input.nor);
	surface.facenormal = surface.N;

	surface.occlusion = input.ao_wet.x;

	surface.T = input.tan;
	surface.T.w = surface.T.w < 0 ? -1 : 1;
	half3 bitangent = cross(surface.T.xyz, input.nor) * surface.T.w;
	float3x3 TBN = float3x3(surface.T.xyz, bitangent, input.nor);
	surface.T.xyz = normalize(surface.T.xyz);
	c.TBN = TBN;

	[branch]
	if (material.textures[BASECOLORMAP].IsValid() && (GetFrame().options & OPTION_BIT_DISABLE_ALBEDO_MAPS) == 0)
		surface.baseColor *= material.textures[BASECOLORMAP].Sample(sampler_objectshader, uvsets);

#ifdef TRANSPARENT
	[branch]
	if (material.textures[TRANSPARENCYMAP].IsValid())
		surface.baseColor.a *= material.textures[TRANSPARENCYMAP].Sample(sampler_objectshader, uvsets).r;
#endif

	surface.baseColor *= input.color;

	[branch]
	if (material.textures[NORMALMAP].IsValid())
	{
		surface.bumpColor = half3(material.textures[NORMALMAP].Sample(sampler_objectshader, uvsets).rg, 1);
		surface.bumpColor = surface.bumpColor * 2 - 1;
		surface.bumpColor.rg *= material.GetNormalMapStrength();
	}

	surface.layerMask = material.layerMask & meshinstance.layerMask;

	half4 surfaceMap = 1;
	[branch]
	if (material.textures[SURFACEMAP].IsValid())
		surfaceMap = material.textures[SURFACEMAP].Sample(sampler_objectshader, uvsets);

	surface.emissiveColor = material.GetEmissive();
	[branch]
	if (any(surface.emissiveColor) && material.textures[EMISSIVEMAP].IsValid())
	{
		half4 emissiveMap = material.textures[EMISSIVEMAP].Sample(sampler_objectshader, uvsets);
		surface.emissiveColor *= emissiveMap.rgb * emissiveMap.a;
	}
	surface.emissiveColor *= meshinstance.GetEmissive();

	[branch]
	if (!material.IsUsingSpecularGlossinessWorkflow())
	{
		surfaceMap.g *= material.GetRoughness();
		surfaceMap.b *= material.GetMetalness();
		surfaceMap.a *= material.GetReflectance();
	}

#ifdef TILEDFORWARD
	const uint flat_tile_index = GetFlatTileIndex(c.pixel, camera);
	TiledDecals(surface, surfaceMap, sampler_objectshader, flat_tile_index, camera);
#endif

	[branch]
	if (any(surface.bumpColor))
		surface.N = normalize(mul(surface.bumpColor, TBN));

	half4 specularMap = 1;
	[branch]
	if (material.textures[SPECULARMAP].IsValid())
		specularMap = material.textures[SPECULARMAP].Sample(sampler_objectshader, uvsets);

	surface.create(material, surface.baseColor, surfaceMap, specularMap);

	half wet = input.ao_wet.y;
	if (wet > 0)
	{
		surface.albedo = lerp(surface.albedo, 0, wet);
		surface.roughness = clamp(surface.roughness * saturate(sqr((1 - wet) * 2 - 1)), 0.01, 1);
		surface.f0 = lerp(surface.f0, 0, wet);
		surface.N = normalize(lerp(surface.N, input.nor, wet));
	}

	[branch]
	if (material.IsOcclusionEnabled_Secondary() && material.textures[OCCLUSIONMAP].IsValid())
		surface.occlusion *= material.textures[OCCLUSIONMAP].Sample(sampler_objectshader, uvsets).r;

	surface.sss = material.GetSSS();
	surface.sss_inv = material.GetSSSInverse();
	surface.pixel = c.pixel;
	surface.screenUV = c.screenUV;

	// --- surface hook -------------------------------------------------------------------------------
	c.surface = surface;
	c.uvsets = uvsets;
#ifdef LUMEN_USE_SURFACE_HOOK
	LumenSurface(c);
#endif
	surface = c.surface;

	surface.update();

	half3 ambient = GetAmbient(surface.N);
	ambient = lerp(ambient, ambient * surface.sss.rgb, saturate(surface.sss.a));

	Lighting lighting;
	lighting.create(0, 0, ambient, 0);

	half4 color = surface.baseColor;

#ifdef TRANSPARENT
	surface.transmission = lerp(material.GetTransmission(), 1, material.GetCloak());
	[branch]
	if (surface.transmission > 0)
	{
		[branch]
		if (material.textures[TRANSMISSIONMAP].IsValid())
			surface.transmission *= (half)material.textures[TRANSMISSIONMAP].Sample(sampler_objectshader, uvsets).r;

		[branch]
		if (camera.texture_refraction_index >= 0)
		{
			Texture2D<half4> texture_refraction = bindless_textures_half4[descriptor_index(camera.texture_refraction_index)];
			float2 size;
			float mipLevels;
			texture_refraction.GetDimensions(0, size.x, size.y, mipLevels);
			const float2 normal2D = mul((float3x3)camera.view, surface.N.xyz).xy;
			float2 perturbed = c.screenUV + normal2D * lerp(material.GetRefraction(), 0.1, material.GetCloak());
			float mip = lerp(surface.roughness, 0.1, material.GetCloak()) * mipLevels;
			float chromatic = material.GetChromaticAberration() / size;
			half r = texture_refraction.SampleLevel(sampler_linear_clamp, perturbed + float2(1, 1) * chromatic, mip).r;
			half g = texture_refraction.SampleLevel(sampler_linear_clamp, perturbed, mip).g;
			half b = texture_refraction.SampleLevel(sampler_linear_clamp, perturbed - float2(1, 1) * chromatic, mip).b;
			surface.refraction.rgb = lerp(surface.albedo, 1, material.GetCloak()) * half3(r, g, b);
			surface.refraction.a = surface.transmission;
		}
	}
#endif

	LightMapping(meshinstance.lightmap, input.atl, lighting, surface);

#ifdef PLANARREFLECTION
	lighting.indirect.specular += PlanarReflection(surface, surface.bumpColor.rg) * surface.F;
#endif

#ifdef TILEDFORWARD
	TiledLighting(surface, lighting, flat_tile_index, camera);
#endif

#ifndef TRANSPARENT
	[branch]
	if (camera.texture_ssr_index >= 0)
	{
		half4 ssr = bindless_textures_half4[descriptor_index(camera.texture_ssr_index)].SampleLevel(sampler_linear_clamp, c.screenUV, 0);
		lighting.indirect.specular = lerp(lighting.indirect.specular, ssr.rgb * surface.F, ssr.a);
	}
	[branch]
	if (camera.texture_ssgi_index >= 0)
		surface.ssgi = bindless_textures_half4[descriptor_index(camera.texture_ssgi_index)].SampleLevel(sampler_linear_clamp, c.screenUV, 0).rgb;
	[branch]
	if (camera.texture_ao_index >= 0)
		surface.occlusion *= bindless_textures_half4[descriptor_index(camera.texture_ao_index)].SampleLevel(sampler_linear_clamp, c.screenUV, 0).r;
#endif

	ApplyLighting(surface, lighting, color);

	half4 rimHighlight = meshinstance.GetRimHighlight();
	color.rgb += rimHighlight.rgb * pow(1 - surface.NdotV, rimHighlight.w);

	// --- light hook -----------------------------------------------------------------------------------
	c.surface = surface;
	c.color = color;
#ifdef LUMEN_USE_LIGHT_HOOK
	LumenLight(c);
#endif
	color = c.color;
	color.rgb += c.addEmissive + c.addSpecular;
	surface = c.surface;

#ifdef TRANSPARENT
	ApplyAerialPerspective(c.screenUV, surface.P, color);
#endif

	ApplyFog(dist, surface.V, color);

	color.rgb = mul(saturationMatrix(material.GetSaturation()), color.rgb);
	color = saturateMediump(color);

#ifdef LUMEN_USE_COLOR_HOOK
	c.color = color;
	LumenColor(c);
	color = saturateMediump(c.color);
#endif

#ifndef DISABLE_ALPHATEST
	half alphatest = material.GetAlphaTest() + meshinstance.GetAlphaTest();
	half dithering = 0;
#ifdef OBJECTSHADER_USE_DITHERING
	dithering = input.GetDither();
#endif
	coverage = AlphaToCoverage(color.a, alphatest, dithering, input.pos);
#endif

	return color;
}

#endif // LUMEN_OBJECT_PS_HLSLI
