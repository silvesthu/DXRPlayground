#pragma once
#include "Shared.h"
#include "Binding.h"
#include "Common.h"

// https://github.com/wdas/brdf/blob/main/src/shaderTemplates/brdftemplateImageSlice.frag
// https://github.com/wdas/brdf/blob/main/src/brdfs/disney.brdf
// https://patapom.com/topics/WebGL/BRDF/
namespace BRDFExplorer
{
#define vec3 float3
#define mix lerp

	// Parameters
	static const float3 baseColor				= mConstants.mBRDFExplorer.mBaseColor;
	static const float metallic					= mConstants.mBRDFExplorer.mMetallic;
	static const float subsurface				= mConstants.mBRDFExplorer.mSubsurface;
	static const float specular					= mConstants.mBRDFExplorer.mSpecular;
	static const float roughness				= mConstants.mBRDFExplorer.mRoughness;
	static const float specularTint				= mConstants.mBRDFExplorer.mSpecularTint;
	static const float anisotropic				= mConstants.mBRDFExplorer.mAnisotropic;
	static const float sheen					= mConstants.mBRDFExplorer.mSheen;
	static const float sheenTint				= mConstants.mBRDFExplorer.mSheenTint;
	static const float clearcoat				= mConstants.mBRDFExplorer.mClearcoat;
	static const float clearcoatGloss			= mConstants.mBRDFExplorer.mClearcoatGloss;

	static const float phiD						= mConstants.mBRDFExplorer.mPhiD;
	static const float gamma					= mConstants.mBRDFExplorer.mGamma;

	static const float PI						= MATH_PI;

	float sqr(float x) { return x * x; }

	float SchlickFresnel(float u)
	{
		float m = clamp(1 - u, 0, 1);
		float m2 = m * m;
		return m2 * m2 * m; // pow(m,5)
	}

	float GTR1(float NdotH, float a)
	{
		if (a >= 1) return 1 / PI;
		float a2 = a * a;
		float t = 1 + (a2 - 1) * NdotH * NdotH;
		return (a2 - 1) / (PI * log(a2) * t);
	}

	float GTR2(float NdotH, float a)
	{
		float a2 = a * a;
		float t = 1 + (a2 - 1) * NdotH * NdotH;
		return a2 / (PI * t * t);
	}

	float GTR2_aniso(float NdotH, float HdotX, float HdotY, float ax, float ay)
	{
		return 1 / (PI * ax * ay * sqr(sqr(HdotX / ax) + sqr(HdotY / ay) + NdotH * NdotH));
	}

	float smithG_GGX(float NdotV, float alphaG)
	{
		float a = alphaG * alphaG;
		float b = NdotV * NdotV;
		return 1 / (NdotV + sqrt(a + b - a * b));
	}

	float smithG_GGX_aniso(float NdotV, float VdotX, float VdotY, float ax, float ay)
	{
		return 1 / (NdotV + sqrt(sqr(VdotX * ax) + sqr(VdotY * ay) + sqr(NdotV)));
	}

	vec3 mon2lin(vec3 x)
	{
		return vec3(pow(x[0], 2.2), pow(x[1], 2.2), pow(x[2], 2.2));
	}


	vec3 BRDF(vec3 L, vec3 V, vec3 N, vec3 X, vec3 Y)
	{
		float NdotL = dot(N, L);
		float NdotV = dot(N, V);
		if (NdotL < 0 || NdotV < 0) return vec3(0.xxx);

		vec3 H = normalize(L + V);
		float NdotH = dot(N, H);
		float LdotH = dot(L, H);

		vec3 Cdlin = mon2lin(baseColor);
		float Cdlum = .3 * Cdlin[0] + .6 * Cdlin[1] + .1 * Cdlin[2]; // luminance approx.

		vec3 Ctint = Cdlum > 0 ? Cdlin / Cdlum : vec3(1.xxx); // normalize lum. to isolate hue+sat
		vec3 Cspec0 = mix(specular * .08 * mix(vec3(1.xxx), Ctint, specularTint), Cdlin, metallic);
		vec3 Csheen = mix(vec3(1.xxx), Ctint, sheenTint);

		// Diffuse fresnel - go from 1 at normal incidence to .5 at grazing
		// and mix in diffuse retro-reflection based on roughness
		float FL = SchlickFresnel(NdotL), FV = SchlickFresnel(NdotV);
		float Fd90 = 0.5 + 2 * LdotH * LdotH * roughness;
		float Fd = mix(1.0, Fd90, FL) * mix(1.0, Fd90, FV);

		// Based on Hanrahan-Krueger brdf approximation of isotropic bssrdf
		// 1.25 scale is used to (roughly) preserve albedo
		// Fss90 used to "flatten" retroreflection based on roughness
		float Fss90 = LdotH * LdotH * roughness;
		float Fss = mix(1.0, Fss90, FL) * mix(1.0, Fss90, FV);
		float ss = 1.25 * (Fss * (1 / (NdotL + NdotV) - .5) + .5);

		// specular
		float aspect = sqrt(1 - anisotropic * .9);
		float ax = max(.001, sqr(roughness) / aspect);
		float ay = max(.001, sqr(roughness) * aspect);
		float Ds = GTR2_aniso(NdotH, dot(H, X), dot(H, Y), ax, ay);
		float FH = SchlickFresnel(LdotH);
		vec3 Fs = mix(Cspec0, vec3(1.xxx), FH);
		float Gs;
		Gs = smithG_GGX_aniso(NdotL, dot(L, X), dot(L, Y), ax, ay);
		Gs *= smithG_GGX_aniso(NdotV, dot(V, X), dot(V, Y), ax, ay);

		// sheen
		vec3 Fsheen = FH * sheen * Csheen;

		// clearcoat (ior = 1.5 -> F0 = 0.04)
		float Dr = GTR1(NdotH, mix(.1, .001, clearcoatGloss));
		float Fr = mix(.04, 1.0, FH);
		float Gr = smithG_GGX(NdotL, .25) * smithG_GGX(NdotV, .25);

		return ((1 / PI) * mix(Fd, ss, subsurface) * Cdlin + Fsheen)
			* (1 - metallic)
			+ Gs * Fs * Ds + .25 * clearcoat * Gr * Fr * Dr;
	}

	////////////////////////////////////////////////////////////

	float Sqr(float x)
	{
		return x * x;
	}

	float3 rotate_vector(float3 v, float3 axis, float angle)
	{
		float3 n;
		axis = normalize(axis);
		n = axis * dot(axis, v);
		return n + cos(angle) * (v - n) + sin(angle) * cross(axis, v);
	}

	void BRDFSlice(float2 texCoord, out float4 fragColor)
	{
		// control
		float useThetaHSquared = 0;
		float incidentPhi = 0;
		float useNDotL = 0;
		float showChroma = 0;
		float brightness = 1.0f;
		float exposure = 0;

		// orthonormal vectors
		float3 normal = float3(0, 0, 1);
		float3 tangent = float3(1, 0, 0);
		float3 bitangent = float3(0, 1, 0);

		// thetaH and thetaD vary from [0 - pi/2]
		const float M_PI = 3.1415926535897932384626433832795;

		float thetaH = texCoord.r;
		if (useThetaHSquared != 0) thetaH = Sqr(thetaH) / (M_PI * 0.5);

		float thetaD = texCoord.g;

		// compute H from thetaH,phiH where (phiH = incidentPhi)
		float phiH = incidentPhi;
		float sinThetaH = sin(thetaH), cosThetaH = cos(thetaH);
		float sinPhiH = sin(phiH), cosPhiH = cos(phiH);
		float3 H = float3(sinThetaH * cosPhiH, sinThetaH * sinPhiH, cosThetaH);

		// compute D from thetaD,phiD
		float sinThetaD = sin(thetaD), cosThetaD = cos(thetaD);
		float sinPhiD = sin(phiD), cosPhiD = cos(phiD);
		float3 D = float3(sinThetaD * cosPhiD, sinThetaD * sinPhiD, cosThetaD);

		// compute L by rotating D into incident frame
		float3 L = rotate_vector(rotate_vector(D, bitangent, thetaH),
			normal, phiH);

		// compute V by reflecting L across H
		float3 V = 2 * dot(H, L) * H - L;

		float3 b = BRDF(L, V, normal, tangent, bitangent);

		// apply N . L
		if (useNDotL != 0) b *= clamp(L[2], 0, 1);

		if (showChroma != 0) {
			float norm = max(b[0], max(b[1], b[2]));
			if (norm > 0) b /= norm;
		}

		// brightness
		b *= brightness;

		// exposure
		b *= pow(2.0, exposure);

		// gamma
		b = pow(b, float3(1.0.xxx / gamma));

		fragColor = float4(clamp(b, float3(0.0.xxx), float3(1.0.xxx)), 1.0);
	}

#undef vec3
#undef mix
}

/*
BRDF Explorer
Copyright Disney Enterprises, Inc. All rights reserved.

This license governs use of the accompanying software. If you use the software, you
accept this license. If you do not accept the license, do not use the software.

1. Definitions
The terms "reproduce," "reproduction," "derivative works," and "distribution" have
the same meaning here as under U.S. copyright law. A "contribution" is the original
software, or any additions or changes to the software. A "contributor" is any person
that distributes its contribution under this license. "Licensed patents" are a
contributor's patent claims that read directly on its contribution.

2. Grant of Rights
(A) Copyright Grant- Subject to the terms of this license, including the license
conditions and limitations in section 3, each contributor grants you a non-exclusive,
worldwide, royalty-free copyright license to reproduce its contribution, prepare
derivative works of its contribution, and distribute its contribution or any derivative
works that you create.
(B) Patent Grant- Subject to the terms of this license, including the license
conditions and limitations in section 3, each contributor grants you a non-exclusive,
worldwide, royalty-free license under its licensed patents to make, have made,
use, sell, offer for sale, import, and/or otherwise dispose of its contribution in the
software or derivative works of the contribution in the software.

3. Conditions and Limitations
(A) No Trademark License- This license does not grant you rights to use any
contributors' name, logo, or trademarks.
(B) If you bring a patent claim against any contributor over patents that you claim
are infringed by the software, your patent license from such contributor to the
software ends automatically.
(C) If you distribute any portion of the software, you must retain all copyright,
patent, trademark, and attribution notices that are present in the software.
(D) If you distribute any portion of the software in source code form, you may do
so only under this license by including a complete copy of this license with your
distribution. If you distribute any portion of the software in compiled or object code
form, you may only do so under a license that complies with this license.
(E) The software is licensed "as-is." You bear the risk of using it. The contributors
give no express warranties, guarantees or conditions. You may have additional
consumer rights under your local laws which this license cannot change.
To the extent permitted under your local laws, the contributors exclude the
implied warranties of merchantability, fitness for a particular purpose and non-
infringement.
*/
