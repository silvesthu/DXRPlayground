#include "Shared.h"
#include "Binding.h"
#include "Common.h"
#include "BRDFExplorer.h"
#include "NanoVDB.h"

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

[RootSignature(ROOT_SIGNATURE_COMMON)]
float4 CompositePS(float4 position : SV_POSITION) : SV_TARGET
{
	RWTexture2D<float4> screen_color = ResourceDescriptorHeap[(int)ViewDescriptorIndex::ScreenColorUAV];
	RWTexture2D<float4> screen_debug = ResourceDescriptorHeap[(int)ViewDescriptorIndex::ScreenDebugUAV];

	uint2 coords = (uint2)position.xy;
	float4 color = screen_color[position.xy];
	bool debug_pixel = all(coords == mConstants.mPixelDebugCoord);
	if (debug_pixel)
	{
		PixelInspectionUAV[0].mPixelValue = color;
		PixelInspectionUAV[0].mDebugValue = screen_debug[position.xy];
	}
	
	// For visualization
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
	
	if (!debug_pixel)
	{
		if (coords.y == mConstants.mPixelDebugCoord.y)
			if (abs((int)coords.x - mConstants.mPixelDebugCoord.x) < 10)
				color.xyz = float3(1, 0, 1);
		if (coords.x == mConstants.mPixelDebugCoord.x)
			if (abs((int)coords.y - mConstants.mPixelDebugCoord.y) < 10)
				color.xyz = float3(1, 0, 1);
	}
	
	color.xyz = ApplySRGBCurve(color.xyz);
	return float4(color.xyz, 1);
}

[RootSignature(ROOT_SIGNATURE_COMMON)]
[numthreads(64, 1, 1)]
void ClearCS(
	uint3 inGroupThreadID : SV_GroupThreadID,
	uint3 inGroupID : SV_GroupID,
	uint3 inDispatchThreadID : SV_DispatchThreadID,
	uint inGroupIndex : SV_GroupIndex)
{
	if (inDispatchThreadID.x == 0)
	{
		PixelInspectionUAV[0].mPixelValue = 0;
		PixelInspectionUAV[0].mDebugValue = 0;
		PixelInspectionUAV[0].mPixelInstanceID = -1;
	}

	if (inDispatchThreadID.x < PixelInspection::kArraySize)
	{
		PixelInspectionUAV[0].mPixelValueArray[inDispatchThreadID.x] = 0;
	}

	if (GetDebugFlag() & DebugFlag::UpdateRayInspection)
		if (inDispatchThreadID.x < RayInspection::kArraySize)
		{
			// Initialize position as NaN to kill vertices those are not updated
			RayInspectionUAV[0].mPositionWS[inDispatchThreadID.x] = sqrt(-1.0);
			RayInspectionUAV[0].mNormalWS[inDispatchThreadID.x] = sqrt(-1.0);
			RayInspectionUAV[0].mLightPositionWS[inDispatchThreadID.x] = sqrt(-1.0);
		}
}

[RootSignature(ROOT_SIGNATURE_COMMON)]
[numthreads(8, 8, 1)]
void GeneratTextureCS(
	uint3 inGroupThreadID : SV_GroupThreadID,
	uint3 inGroupID : SV_GroupID,
	uint3 inDispatchThreadID : SV_DispatchThreadID,
	uint inGroupIndex : SV_GroupIndex)
{
	RWTexture2D<float4> texture = ResourceDescriptorHeap[(int)ViewDescriptorIndex::GeneratedUAV];

	// this shader is solely used to generate texture 
	float3 color = inDispatchThreadID.x % 2 == inDispatchThreadID.y % 2 ? 0.8 : 0.2;
	
	texture[inDispatchThreadID.xy] = float4(color, 1.0); 
}

[RootSignature(ROOT_SIGNATURE_COMMON)]
[numthreads(8, 8, 1)]
void BRDFSliceCS(
	uint3 inGroupThreadID : SV_GroupThreadID,
	uint3 inGroupID : SV_GroupID,
	uint3 inDispatchThreadID : SV_DispatchThreadID,
	uint inGroupIndex : SV_GroupIndex)
{
	RWTexture2D<float4> texture = ResourceDescriptorHeap[(int)ViewDescriptorIndex::BRDFSliceUAV];

	uint2 dimensions;
	texture.GetDimensions(dimensions.x, dimensions.y);

	float2 texCoord = (inDispatchThreadID.xy + 0.5) / dimensions;	// [0, 1]
	texCoord.y = 1.0 - texCoord.y;									// match GLSL
	texCoord *= (MATH_PI / 2.0);									// thetaH, thetaD
	float4 fragColor = 0;
	BRDFExplorer::BRDFSlice(texCoord, fragColor);

	texture[inDispatchThreadID.xy] = fragColor;
}

[RootSignature(ROOT_SIGNATURE_COMMON)]
[numthreads(8, 8, 1)]
void ReadbackCS(
	uint3 inGroupThreadID : SV_GroupThreadID,
	uint3 inGroupID : SV_GroupID,
	uint3 inDispatchThreadID : SV_DispatchThreadID,
	uint inGroupIndex : SV_GroupIndex)
{
	Texture2D<float4> Input = ResourceDescriptorHeap[(int)ViewDescriptorIndex::ScreenColorSRV];
	RWTexture2D<float4> Output = ResourceDescriptorHeap[(int)ViewDescriptorIndex::ScreenReadbackUAV];

	float3 luminance = Input[inDispatchThreadID.xy].xyz;
	float3 color = luminance;
	if (true)
	{
		color = LuminanceToColor(luminance, mConstants);
		color = ApplySRGBCurve(color);
	}
	Output[inDispatchThreadID.xy] = float4(color, 1.0);
}

float4 LineVS(uint inVertexID : SV_VertexID, out float4 outColor : COLOR) : SV_POSITION
{
	float4 position_ws = 0;
	outColor = 1.0;

	if (inVertexID < RayInspection::kArraySize * 1 * 2)
	{
		// Position (BSDF Rays)
		uint group = inVertexID / 2;
		uint index = inVertexID % 2;

		float4 position_0 = RayInspectionUAV[0].mPositionWS[group + 0];
		float4 position_1 = RayInspectionUAV[0].mPositionWS[group + 1];

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
	else if (inVertexID < RayInspection::kArraySize * 2 * 2)
	{
		// Normal

		// [TODO]
	}
	else if (inVertexID < RayInspection::kArraySize * 3 * 2)
	{
		// LightPosition (Light Rays)
		uint group = (inVertexID - RayInspection::kArraySize * 2 * 2) / 2;
		uint index = (inVertexID - RayInspection::kArraySize * 2 * 2) % 2;

		float4 position_0 = RayInspectionUAV[0].mPositionWS[group];
		float4 position_1 = RayInspectionUAV[0].mLightPositionWS[group];

		position_ws = float4(index == 0 ? position_0.xyz : position_1.xyz, 1.0); 

		float distance_along_ray = index == 0 ? 0.0 : length(position_1.xyz - position_0.xyz);

		outColor = index == 0 ? float4(0.0, 0.0, 1.0, distance_along_ray) : float4(1.0, 0.0, 1.0, distance_along_ray); // Blue -> Magenta
	}
	
	return mul(mConstants.mViewProjectionMatrix, position_ws);
}

[RootSignature(ROOT_SIGNATURE_COMMON)]
float4 LinePS(float4 position : SV_POSITION, in float4 inColor : COLOR) : SV_TARGET
{
	return float4(inColor.xyz, 1.0);
}

[RootSignature(ROOT_SIGNATURE_COMMON)]
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

ConstantBuffer<RootConstantsNanoVDBVisualize> mRootConstantsNanoVDBVisualize : register(b0, space1);
#define ROOT_SIGNATURE_NANOVDB_VISUALIZE \
ROOT_SIGNATURE_BASE ", RootConstants(num32BitConstants=4, b0, space = 1)"

[RootSignature(ROOT_SIGNATURE_NANOVDB_VISUALIZE)]
[numthreads(8, 8, 1)]
void NanoVDBVisualizeCS(
	uint3 inGroupThreadID : SV_GroupThreadID,
	uint3 inGroupID : SV_GroupID,
	uint3 inDispatchThreadID : SV_DispatchThreadID,
	uint inGroupIndex : SV_GroupIndex)
{
	InstanceData instance_data					= InstanceDatas[mRootConstantsNanoVDBVisualize.mInstanceIndex];

	RWTexture3D<float> output					= ResourceDescriptorHeap[mRootConstantsNanoVDBVisualize.mTexutureUAVIndex];
	if (any(inDispatchThreadID >= instance_data.mMediumNanoVBD.mSize))
		return;

	NanoVDBContext context;
	context.Initialize(instance_data.mMediumNanoVBD);
	float density								= context.SampleCoords(inDispatchThreadID.xyz);
	output[inDispatchThreadID.xyz]				= density;
}
