// Lumen Engine — object vertex shader entry with an optional vertex hook (wind, waves, bending, wobble).
// Include after lumen_object.hlsli. Define LUMEN_USE_VERTEX_HOOK and implement
//     void LumenVertexHook(inout LumenVertex v);
#ifndef LUMEN_OBJECT_VS_HLSLI
#define LUMEN_OBJECT_VS_HLSLI

PixelInput lumen_vertex_to_pixel(VertexInput input)
{
	ShaderMaterial material = GetMaterial();

	VertexSurface surface;
	surface.create(material, input);

#ifdef LUMEN_USE_VERTEX_HOOK
	LumenVertex v;
	v.position = surface.position;
	v.normal = surface.normal;
	v.tangent = surface.tangent;
	v.color = surface.color;
	v.uvsets = surface.uvsets;
	v.time = GetTime();
	v.ao = surface.ao;
	v.wet = surface.wet;
	v.atlas = surface.atlas;
	v.cameraIndex = input.GetInstancePointer().GetCameraIndex();
	LumenVertexHook(v);
	surface.position = v.position;
	surface.normal = v.normal;
	surface.tangent = v.tangent;
	surface.color = v.color;
	surface.uvsets = v.uvsets;
#endif

	PixelInput Out;
	Out.pos = surface.position;

	ShaderCamera camera = GetCameraIndexed(input.GetInstancePointer().GetCameraIndex());
	Out.pos = mul(camera.view_projection, Out.pos);

	Out.color = surface.color;
	Out.uvsets = surface.uvsets;
	Out.nor = surface.normal;
	Out.atl = surface.atlas;
	Out.ao_wet = half2(surface.ao, surface.wet);
	Out.tan = surface.tangent;
	Out.poi = input.GetInstancePointer().data;
	return Out;
}

PixelInput main(VertexInput input)
{
	return lumen_vertex_to_pixel(input);
}

#endif // LUMEN_OBJECT_VS_HLSLI
