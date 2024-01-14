#include "Shared.h"
#include "Binding.h"
#include "Common.h"

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

// Generate screen space triangle
// From https://anteru.net/blog/2012/minimal-setup-screen-space-quads-no-buffers-layouts-required/
float4 ScreenspaceTriangleVS(uint id : SV_VertexID) : SV_POSITION
{
	float x = float ((id & 2) << 1) - 1.0;
	float y = 1.0 - float ((id & 1) << 2);
	return float4 (x, y, 0, 1);
}

[RootSignature(ROOT_SIGNATURE_COMMON)]
float4 CompositePS(float4 position : SV_POSITION) : SV_TARGET
{
	RWTexture2D<float4> screen_color = ResourceDescriptorHeap[(int)ViewDescriptorIndex::ScreenColorUAV];

	int2 coords = (int2)position.xy;
	float4 color = screen_color[position.xy];
	bool debug_pixel = all(coords == mConstants.mPixelDebugCoord);
	if (debug_pixel)
		BufferDebugUAV[0].mPixelValue = color;
	
	// For visualization
	switch (mConstants.mDebugMode)
	{
	case DebugMode::None:						color.xyz = LuminanceToColor(color.xyz, mConstants); break;
	case DebugMode::Barycentrics: 				break;
	case DebugMode::Position: 					break;
	case DebugMode::Normal: 					color.xyz = color.xyz * 0.5 + 0.5; break;
	case DebugMode::UV:							break;
	case DebugMode::Albedo: 					break;
	case DebugMode::Reflectance: 				break;
	case DebugMode::Emission: 					break;
	case DebugMode::RoughnessAlpha: 			break;
	case DebugMode::Transmittance:				break;
	case DebugMode::InScattering:				break;
	case DebugMode::RecursionDepth:				color.xyz = hsv2rgb(float3(color.x / 8.0, 1, 1)); break;
	default:									break;
	}
	
	if (!debug_pixel)
	{
		if (coords.y == mConstants.mPixelDebugCoord.y)
			if (abs(coords.x - mConstants.mPixelDebugCoord.x) < 10)
				color.xyz = float3(1, 0, 1);
		if (coords.x == mConstants.mPixelDebugCoord.x)
			if (abs(coords.y - mConstants.mPixelDebugCoord.y) < 10)
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
		BufferDebugUAV[0].mPixelValue = 0;
		BufferDebugUAV[0].mPixelInstanceID = -1;
	}

	if (inDispatchThreadID.x < Debug::kValueArraySize)
	{
		BufferDebugUAV[0].mPixelValueArray[inDispatchThreadID.x] = 0;
	}
}