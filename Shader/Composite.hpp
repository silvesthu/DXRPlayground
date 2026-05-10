#include "Shared.h"
#include "Binding.h"
#include "Common.h"
#include "DebugUtils.h"
#include "BRDFExplorer.h"
#include "NanoVDB.h"

#include "ShaderToHuman/s2h.hlsl"

// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
float3 ToneMapping_ACES_Knarkowicz(float3 x)
{
	float a = 2.51f;
	float b = 0.03f;
	float c = 2.43f;
	float d = 0.59f;
	float e = 0.14f;
	return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float3 LuminanceToColor(float3 inLuminance, Constants inConstants)
{
	// Exposure
	float3 normalized_luminance = 0;
	{
		// https://google.github.io/filament/Filament.htmdl#physicallybasedcamera

		// Typically 0.65 for real lens, gives a kLensSaturation of 1.2
		// Use 0.78 here to make the kLensSaturation 1.0 for simplicity, assuming virtual lens does not lose light, as in UE 4.25
		// https://google.github.io/filament/Filament.html#imagingpipeline/physicallybasedcamera/exposure
		// https://www.unrealengine.com/en-US/tech-blog/how-epic-games-is-handling-auto-exposure-in-4-25 See Lens Transmittance (LensAttenuation)
		const float kVignettingAttenuation = 0.78f;
		const float kSaturationBasedSpeedConstant = 78.0f;
		const float kISO = 100;
		const float kLensSaturation = kSaturationBasedSpeedConstant / kISO / kVignettingAttenuation;

		// As kLensSaturation is 1.0, when mEV100 is 0.0 (Aperture = 1.0, Shutter Speed = 1.0, ISO = 100),
		// this normalization factor becomes 1.0, that is, luminance value is used as it is.
		float exposure_normalization_factor = 1.0 / (pow(2.0, inConstants.mEV100) * kLensSaturation);
		normalized_luminance = inLuminance * (exposure_normalization_factor / kPreExposure);

		// [Reference]
		// https://en.wikipedia.org/wiki/Exposure_value
		// https://knarkowicz.wordpress.com/2016/01/09/automatic-exposure/
		// https://seblagarde.files.wordpress.com/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf
		// https://docs.unrealengine.com/en-US/RenderingAndGraphics/PostProcessEffects/ColorGrading/index.html
	}

	float3 tone_mapped_color = 0;
	// Tone Mapping
	{
		switch (inConstants.mToneMappingMode)
		{
			case ToneMappingMode::Knarkowicz:	tone_mapped_color = ToneMapping_ACES_Knarkowicz(normalized_luminance); break;
			case ToneMappingMode::Passthrough:	// fallthrough
			default:							tone_mapped_color = normalized_luminance; break;
		}

		// [Reference]
		// https://github.com/ampas/aces-dev
		// https://docs.unrealengine.com/en-US/RenderingAndGraphics/PostProcessEffects/ColorGrading/index.html
	}

	return tone_mapped_color;
}

// https://github.com/microsoft/DirectX-Graphics-Samples/blob/master/MiniEngine/Core/Shaders/ColorSpaceUtility.hlsli
float3 ApplySRGBCurve( float3 x )
{
	// Approximately pow(x, 1.0 / 2.2)
	return select(x < 0.0031308, 12.92 * x, 1.055 * pow(x, 1.0 / 2.4) - 0.055);
}

// https://github.com/microsoft/DirectX-Graphics-Samples/blob/master/MiniEngine/Core/Shaders/ColorSpaceUtility.hlsli
float3 RemoveSRGBCurve( float3 x )
{
	// Approximately pow(x, 2.2)
	return select(x < 0.04045, x / 12.92, pow( (x + 0.055) / 1.055, 2.4 ));
}

float s2h_floatLookupFloat(uint functionId, float x)
{
	if (functionId == 0)
	{
		return x;
	}

	return 0;
}

float4 CompositePS(float4 position : SV_POSITION) : SV_TARGET
{
	USING_RESOURCE(RWTexture2D<float4>, ScreenColorUAV);
	USING_RESOURCE(RWTexture2D<float4>, ScreenDebugUAV);
	USING_RESOURCE(RWStructuredBuffer<InspectData>, InspectDataUAV);

	// USING_RESOURCE(RWTexture2D<uint4>, ScreenReservoirUAV);
	// ScreenReservoirUAV[position.xy] = uint4(0xffffffff, 0xffff0000, 0x0000ffff, 0x00000000);

	uint2 coords								= (uint2)position.xy;
	float4 color								= ScreenColorUAV[position.xy];
	
	switch (mConstants.mVisualizeMode)
	{
	case VisualizeMode::None:					color.xyz = LuminanceToColor(color.xyz, mConstants); break;
	case VisualizeMode::Barycentrics: 			break;
	case VisualizeMode::Position: 				break;
	case VisualizeMode::Normal: 				color.xyz = color.xyz * 0.5 + 0.5; break;
	case VisualizeMode::UV:						break;
	case VisualizeMode::Albedo: 				break;
	case VisualizeMode::Reflectance: 			break;
	case VisualizeMode::Emission: 				break;
	case VisualizeMode::RoughnessAlpha: 		break;
	case VisualizeMode::Transmittance:			break;
	case VisualizeMode::InScattering:			break;
	case VisualizeMode::RecursionDepth:			color.xyz = GetDebugRecursion() == 0 ? HSVToRGB(float3(color.x / 8.0, 1, 1)) : color.xxx; break;
	default:									break;
	}

	if (all(coords == (uint2)mConstants.mPixelDebugCoord))
	{
		InspectDataUAV[0].mScreenColor = color;
		InspectDataUAV[0].mScreenDebug = ScreenDebugUAV[position.xy];
	}
	else
	{
		// Crosshair
		if (coords.y == mConstants.mPixelDebugCoord.y)
			if (abs((int)coords.x - mConstants.mPixelDebugCoord.x) < 10)
				color.xyz = float3(1, 0, 1);
		if (coords.x == mConstants.mPixelDebugCoord.x)
			if (abs((int)coords.y - mConstants.mPixelDebugCoord.y) < 10)
				color.xyz = float3(1, 0, 1);
	}
	
	color.xyz = ApplySRGBCurve(color.xyz);

#if 0 // ShaderToHuman
	{
		// https://github.com/electronicarts/ShaderToHuman

		struct ContextGather ui;
		s2h_init(ui, position.xy);

		s2h_setCursor(ui, float2(50, 50));

		s2h_setScale(ui, 3.0f);
		s2h_printTxt(ui, _H, _e, _l, _l, _o);
		s2h_printLF(ui);
		s2h_printTxt(ui, _S, _c, _r, _e);
		s2h_printTxt(ui, _e, _n);

		s2h_drawSRGBRamp(ui, float2(50, 150));

		s2h_setCursor(ui, float2(50, 250));
		float2 rangeX = float2(0.0, 1.0);
		float2 rangeY = float2(0.0, 1.0);
		float aspect_ratio = mConstants.mScreenWidth * 1.0 / mConstants.mScreenHeight;
		s2h_function(ui, 0, float4(0, 0, 0, 0.45f), int2(10, 10), rangeX, rangeY);

		color.xyz = lerp(color.xyz, ui.dstColor.xyz, ui.dstColor.a);
	}
#endif // ShaderToHuman

	return float4(color.xyz, 1);
}

[numthreads(64, 1, 1)]
void ClearCS(COMPUTE_SHADER_INPUT)
{
	USING_RESOURCE(RWStructuredBuffer<InspectData>, InspectDataUAV);
	USING_RESOURCE(RWStructuredBuffer<uint>, ShaderPrintUAV);

#if SHADER_DEBUG
	ShaderPrintUAV[0] = 1; // 0 stores count
#endif // SHADER_DEBUG

	Inspect::Clear(inDispatchThreadID.x);
}

// Replacement for complex ClearUnorderedAccessViewUint/Float
// https://asawicki.info/news_1795_secrets_of_direct3d_12_the_behavior_of_clearunorderedaccessviewuintfloat
[numthreads(64, 1, 1)]
void ClearBufferCS(COMPUTE_SHADER_INPUT)
{
	RWStructuredBuffer<uint4> buffer = ResourceDescriptorHeap[mRootConstants.mData0.x];
	buffer[inDispatchThreadID.x] = mRootConstants.mData1;
}

[numthreads(8, 8, 1)]
void GeneratTextureCS(COMPUTE_SHADER_INPUT)
{
	USING_RESOURCE(RWTexture2D<float4>, GeneratedUAV);

	// this shader is solely used to generate texture 
	float3 color = inDispatchThreadID.x % 2 == inDispatchThreadID.y % 2 ? 0.8 : 0.2;
	
	GeneratedUAV[inDispatchThreadID.xy] = float4(color, 1.0); 
}

[numthreads(8, 8, 1)]
void BRDFSliceCS(COMPUTE_SHADER_INPUT)
{
	USING_RESOURCE(RWTexture2D<float4>, BRDFSliceUAV);

	uint2 dimensions;
	BRDFSliceUAV.GetDimensions(dimensions.x, dimensions.y);

	float2 texCoord = (inDispatchThreadID.xy + 0.5) / dimensions;	// [0, 1]
	texCoord.y = 1.0 - texCoord.y;									// match GLSL
	texCoord *= (MATH_PI / 2.0);									// thetaH, thetaD
	float4 fragColor = 0;
	BRDFExplorer::BRDFSlice(texCoord, fragColor);

	BRDFSliceUAV[inDispatchThreadID.xy] = fragColor;
}

[numthreads(8, 8, 1)]
void ReadbackCS(COMPUTE_SHADER_INPUT)
{
	USING_RESOURCE(Texture2D<float4>, ScreenColorSRV);
	USING_RESOURCE(RWTexture2D<float4>, ScreenReadbackUAV);

	float3 luminance = ScreenColorSRV[inDispatchThreadID.xy].xyz;
	float3 color = luminance;
	if (true)
	{
		color = LuminanceToColor(luminance, mConstants);
		color = ApplySRGBCurve(color);
	}
	ScreenReadbackUAV[inDispatchThreadID.xy] = float4(color, 1.0);
}

float4 LineVS(uint inVertexID : SV_VertexID, out float4 outColor : COLOR) : SV_POSITION
{
	USING_RESOURCE(RWStructuredBuffer<InspectData>, InspectDataUAV);

	float4 position_ws = 0;
	outColor = 1.0;

	if (inVertexID < InspectData::kPathLength * 1 * 2)
	{
		// Position (BSDF Rays)
		uint group = inVertexID / 2;
		uint index = inVertexID % 2;

		float4 position_0 = InspectDataUAV[0].mPositionWS[group + 0];
		float4 position_1 = InspectDataUAV[0].mPositionWS[group + 1];

		position_ws = float4(index == 0 ? position_0.xyz : position_1.xyz, 1.0);

		float distance_along_ray = index == 0 ? 0.0 : length(position_1.xyz - position_0.xyz);

		if (group == 0)
			outColor = float4(1.0, 1.0, 1.0, distance_along_ray); // Camera Ray in White
		// else if (group == 1)
		//	outColor = float4(1.0, 0.0, 0.0, distance_along_ray); // First Bounce in Red
		else
			outColor = index == 0 ? float4(0.0, 1.0, 0.0, distance_along_ray) : float4(1.0, 1.0, 0.0, distance_along_ray); // Secondary Bounce in Green -> Yellow

		if (index == 1 && position_1.w == 0) // Miss Ray
			outColor = float4(0.0, 0.0, 0.0, distance_along_ray); // -> Black
	}
	else if (inVertexID < InspectData::kPathLength * 2 * 2)
	{
		// Normal

		// [TODO]
	}
	else if (inVertexID < InspectData::kPathLength * 3 * 2)
	{
		// LightPosition (Light Rays)
		uint group = (inVertexID - InspectData::kPathLength * 2 * 2) / 2;
		uint index = (inVertexID - InspectData::kPathLength * 2 * 2) % 2;

		float4 position_0 = InspectDataUAV[0].mPositionWS[group];
		float4 position_1 = InspectDataUAV[0].mLightPositionWS[group];

		position_ws = float4(index == 0 ? position_0.xyz : position_1.xyz, 1.0); 

		float distance_along_ray = index == 0 ? 0.0 : length(position_1.xyz - position_0.xyz);

		outColor = index == 0 ? float4(0.0, 0.0, 1.0, distance_along_ray) : float4(1.0, 0.0, 1.0, distance_along_ray); // Blue -> Magenta
	}
	
	return mul(mConstants.mViewProjectionMatrix, position_ws);
}

float4 LinePS(float4 position : SV_POSITION, in float4 inColor : COLOR) : SV_TARGET
{
	return float4(inColor.xyz, 1.0);
}

float4 LineHiddenPS(float4 position : SV_POSITION, in float4 inColor : COLOR) : SV_TARGET
{
	// Dashed line
	if (frac(inColor.w * 16.0) < 0.5)
	{
		discard;
		return 0;
	}

	return float4(inColor.xyz, 1.0);
}

[numthreads(8, 8, 1)]
void NanoVDBVisualizeCS(COMPUTE_SHADER_INPUT)
{
	USING_RESOURCE(StructuredBuffer<InstanceData>, RaytraceInstanceDataSRV);

	uint instance_index							= mRootConstants.mData0.x;
	uint uav_index								= mRootConstants.mData0.y;

	InstanceData instance_data					= RaytraceInstanceDataSRV[instance_index];

	RWTexture3D<float> output					= ResourceDescriptorHeap[uav_index];
	if (any(inDispatchThreadID >= instance_data.mMediumNanoVBD.mSize))
		return;

	NanoVDBContext context;
	context.Initialize(instance_data.mMediumNanoVBD);
	float density								= context.SampleCoords(inDispatchThreadID.xyz);
	output[inDispatchThreadID.xyz]				= density;
}
