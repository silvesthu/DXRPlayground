#include "Constant.h"
#define CONSTANT_DEFAULT(x)
#include "Shared.inl"
#include "Binding.h"

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

float3 LuminanceToColor(float3 luminance)
{
	// Exposure
	float3 normalized_luminance = 0;
	{
		// https://google.github.io/filament/Filament.htmdl#physicallybasedcamera

		float kSaturationBasedSpeedConstant = 78.0f;
		float kISO = 100;
		float kVignettingAttenuation = 0.78f; // To cancel out saturation. Typically 0.65 for real lens, see https://www.unrealengine.com/en-US/tech-blog/how-epic-games-is-handling-auto-exposure-in-4-25
		float kLensSaturation = kSaturationBasedSpeedConstant / kISO / kVignettingAttenuation;

		float exposure_normalization_factor = 1.0 / (pow(2.0, mPerFrameConstants.mEV100) * kLensSaturation); // = 1.0 / luminance_max
		normalized_luminance = luminance * (exposure_normalization_factor / kPreExposure);

		// [Reference]
		// https://en.wikipedia.org/wiki/Exposure_value
		// https://knarkowicz.wordpress.com/2016/01/09/automatic-exposure/
		// https://seblagarde.files.wordpress.com/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf
		// https://docs.unrealengine.com/en-US/RenderingAndGraphics/PostProcessEffects/ColorGrading/index.html
	}

	float3 tone_mapped_color = 0;
	// Tone Mapping
	{
		switch (mPerFrameConstants.mToneMappingMode)
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

// From https://github.com/microsoft/DirectX-Graphics-Samples/blob/master/MiniEngine/Core/Shaders/ColorSpaceUtility.hlsli
float3 ApplySRGBCurve( float3 x )
{
    // Approximately pow(x, 1.0 / 2.2)
    return x < 0.0031308 ? 12.92 * x : 1.055 * pow(x, 1.0 / 2.4) - 0.055;
}

float3 RemoveSRGBCurve( float3 x )
{
    // Approximately pow(x, 2.2)
    return x < 0.04045 ? x / 12.92 : pow( (x + 0.055) / 1.055, 2.4 );
}

float4 ScreenspaceTriangleVS(uint id : SV_VertexID) : SV_POSITION
{
	// From https://anteru.net/blog/2012/minimal-setup-screen-space-quads-no-buffers-layouts-required/
	// Generate screen space triangle
	float x = float ((id & 2) << 1) - 1.0;
	float y = 1.0 - float ((id & 1) << 2);
	return float4 (x, y, 0, 1);
}

Texture2D<float4> InputTexture : register(t0, space1);
[RootSignature("DescriptorTable(CBV(b0, numDescriptors = 1, space = 0), SRV(t0, numDescriptors = 1, space = 1))")]
float4 CompositePS(float4 position : SV_POSITION) : SV_TARGET
{
	float3 color = InputTexture.Load(int3(position.xy, 0)).xyz;

	if (mPerFrameConstants.mDebugMode == DebugMode::None)
		color = LuminanceToColor(color /* as luminance */);

	color = ApplySRGBCurve(color);
	return float4(color, 1);
}