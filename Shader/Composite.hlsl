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

		float kSaturationBasedSpeedConstant = 78.0f;
		float kISO = 100;
		float kVignettingAttenuation = 0.78f; // To cancel out saturation. Typically 0.65 for real lens, see https://www.unrealengine.com/en-US/tech-blog/how-epic-games-is-handling-auto-exposure-in-4-25
		float kLensSaturation = kSaturationBasedSpeedConstant / kISO / kVignettingAttenuation;

		float exposure_normalization_factor = 1.0 / (pow(2.0, inConstants.mEV100) * kLensSaturation); // = 1.0 / luminance_max
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

	float4 color = screen_color[position.xy];
	if ((int)position.x == mConstants.mPixelDebugCoord.x && (int)position.y == mConstants.mPixelDebugCoord.y)
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
	case DebugMode::RecursionCount:				color.xyz = hsv2rgb(float3(color.x / 8.0, 1, 1)); break;
	default:									break;
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
		BufferDebugUAV[0].mPixelValue = 0;

	BufferDebugUAV[0].mValueArray[inDispatchThreadID.x] = 0;
}