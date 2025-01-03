// Sample at center of each segment
// #define HILLAIRE20_ADJUST_INTEGRATION

// Adapter
#if !defined(ENTRY_POINT_NewMultiScatCS)
#define MULTISCATAPPROX_ENABLED 1							// Sample MultiScatTexture
#else
#define MULTISCATAPPROX_ENABLED 0
#endif // !defined(ENTRY_POINT_NewMultiScatCS)
// #define FASTAERIALPERSPECTIVE_ENABLED 1					// [TODO] Mie not too weak ?
#define PI MATH_PI
// #define ILLUMINANCE_IS_ONE								// Use SolarIrradiance

#define MultiScatteringLUTRes 32

#define PLANET_RADIUS_OFFSET 0.01f							// ?
#define RayMarchMinMaxSPP float2(4, 14)						// ?

#define AP_SLICE_COUNT 32.0f
#define AP_KM_PER_SLICE 4.0f

float AerialPerspectiveDepthToSlice(float depth)
{
	return depth * (1.0f / AP_KM_PER_SLICE);
}
float AerialPerspectiveSliceToDepth(float slice)
{
	return slice * AP_KM_PER_SLICE;
}

#define TransmittanceLutTexture TransmittanceTexSRV
#define MultiScatTexture MultiScattTexSRV
#define samplerLinearClamp BilinearClampSampler

// We should precompute those terms from resolutions (Or set resolution as #defined constants)
float fromUnitToSubUvs(float u, float resolution) { return (u + 0.5f / resolution) * (resolution / (resolution + 1.0f)); }
float fromSubUvsToUnit(float u, float resolution) { return (u - 0.5f / resolution) * (resolution / (resolution - 1.0f)); }

float getAlbedo(float scattering, float extinction)
{
	return scattering / max(0.001, extinction);
}
float3 getAlbedo(float3 scattering, float3 extinction)
{
	return scattering / max(0.001, extinction);
}

struct AtmosphereParameters
{
	// Radius of the planet (center to ground)
	float BottomRadius;
	// Maximum considered atmosphere height (center to atmosphere top)
	float TopRadius;

	// Rayleigh scattering exponential distribution scale in the atmosphere
	float RayleighDensityExpScale;
	// Rayleigh scattering coefficients
	float3 RayleighScattering;

	// Mie scattering exponential distribution scale in the atmosphere
	float MieDensityExpScale;
	// Mie scattering coefficients
	float3 MieScattering;
	// Mie extinction coefficients
	float3 MieExtinction;
	// Mie absorption coefficients
	float3 MieAbsorption;
	// Mie phase function excentricity
	float MiePhaseG;

	// Another medium type in the atmosphere
	float AbsorptionDensity0LayerWidth;
	float AbsorptionDensity0ConstantTerm;
	float AbsorptionDensity0LinearTerm;
	float AbsorptionDensity1ConstantTerm;
	float AbsorptionDensity1LinearTerm;
	// This other medium only absorb light, e.g. useful to represent ozone in the earth atmosphere
	float3 AbsorptionExtinction;

	// The albedo of the ground.
	float3 GroundAlbedo;
};

struct MediumSampleRGB
{
	float3 scattering;
	float3 absorption;
	float3 extinction;

	float3 scatteringMie;
	float3 absorptionMie;
	float3 extinctionMie;

	float3 scatteringRay;
	float3 absorptionRay;
	float3 extinctionRay;

	float3 scatteringOzo;
	float3 absorptionOzo;
	float3 extinctionOzo;

	float3 albedo;
};

MediumSampleRGB sampleMediumRGB(in float3 WorldPos, in AtmosphereParameters Atmosphere)
{
	const float viewHeight = length(WorldPos) - Atmosphere.BottomRadius;

	const float densityMie = exp(Atmosphere.MieDensityExpScale * viewHeight);
	const float densityRay = exp(Atmosphere.RayleighDensityExpScale * viewHeight);
	const float densityOzo = saturate(viewHeight < Atmosphere.AbsorptionDensity0LayerWidth ?
		Atmosphere.AbsorptionDensity0LinearTerm * viewHeight + Atmosphere.AbsorptionDensity0ConstantTerm :
		Atmosphere.AbsorptionDensity1LinearTerm * viewHeight + Atmosphere.AbsorptionDensity1ConstantTerm);

	MediumSampleRGB s;

	s.scatteringMie = densityMie * Atmosphere.MieScattering;
	s.absorptionMie = densityMie * Atmosphere.MieAbsorption;
	s.extinctionMie = densityMie * Atmosphere.MieExtinction;

	s.scatteringRay = densityRay * Atmosphere.RayleighScattering;
	s.absorptionRay = 0.0f;
	s.extinctionRay = s.scatteringRay + s.absorptionRay;

	s.scatteringOzo = 0.0;
	s.absorptionOzo = densityOzo * Atmosphere.AbsorptionExtinction;
	s.extinctionOzo = s.scatteringOzo + s.absorptionOzo;

	s.scattering = s.scatteringMie + s.scatteringRay + s.scatteringOzo;
	s.absorption = s.absorptionMie + s.absorptionRay + s.absorptionOzo;
	s.extinction = s.extinctionMie + s.extinctionRay + s.extinctionOzo;
	s.albedo = getAlbedo(s.scattering, s.extinction);

	return s;
}

float RayleighPhase(float cosTheta)
{
	float factor = 3.0f / (16.0f * PI);
	return factor * (1.0f + cosTheta * cosTheta);
}

float CornetteShanksMiePhaseFunction(float g, float cosTheta)
{
	float k = 3.0 / (8.0 * PI) * (1.0 - g * g) / (2.0 + g * g);
	return k * (1.0 + cosTheta * cosTheta) / pow(1.0 + g * g - 2.0 * g * -cosTheta, 1.5); // sign of cosTheta?
}

float hgPhase(float g, float cosTheta)
{
	return CornetteShanksMiePhaseFunction(g, cosTheta);
}

// - r0: ray origin
// - rd: normalized ray direction
// - s0: sphere center
// - sR: sphere radius
// - Returns distance from r0 to first intersecion with sphere,
//   or -1.0 if no intersection.
float raySphereIntersectNearest(float3 r0, float3 rd, float3 s0, float sR)
{
	float a = dot(rd, rd);
	float3 s0_r0 = r0 - s0;
	float b = 2.0 * dot(rd, s0_r0);
	float c = dot(s0_r0, s0_r0) - (sR * sR);
	float delta = b * b - 4.0 * a * c;
	if (delta < 0.0 || a == 0.0)
	{
		return -1.0;
	}
	float sol0 = (-b - sqrt(delta)) / (2.0 * a);
	float sol1 = (-b + sqrt(delta)) / (2.0 * a);
	if (sol0 < 0.0 && sol1 < 0.0)
	{
		return -1.0;
	}
	if (sol0 < 0.0)
	{
		return max(0.0, sol1);
	}
	else if (sol1 < 0.0)
	{
		return max(0.0, sol0);
	}
	return max(0.0, min(sol0, sol1));
}

void LutTransmittanceParamsToUv(AtmosphereParameters Atmosphere, in float viewHeight, in float viewZenithCosAngle, out float2 uv)
{
	float H = sqrt(max(0.0f, Atmosphere.TopRadius * Atmosphere.TopRadius - Atmosphere.BottomRadius * Atmosphere.BottomRadius));
	float rho = sqrt(max(0.0f, viewHeight * viewHeight - Atmosphere.BottomRadius * Atmosphere.BottomRadius));

	float discriminant = viewHeight * viewHeight * (viewZenithCosAngle * viewZenithCosAngle - 1.0) + Atmosphere.TopRadius * Atmosphere.TopRadius;
	float d = max(0.0, (-viewHeight * viewZenithCosAngle + sqrt(discriminant))); // Distance to atmosphere boundary

	float d_min = Atmosphere.TopRadius - viewHeight;
	float d_max = rho + H;
	float x_mu = (d - d_min) / (d_max - d_min);
	float x_r = rho / H;

	uv = float2(x_mu, x_r);
	//uv = float2(fromUnitToSubUvs(uv.x, TRANSMITTANCE_TEXTURE_WIDTH), fromUnitToSubUvs(uv.y, TRANSMITTANCE_TEXTURE_HEIGHT)); // No real impact so off
}


#define NONLINEARSKYVIEWLUT 1
void UvToSkyViewLutParams(AtmosphereParameters Atmosphere, out float viewZenithCosAngle, out float lightViewCosAngle, in float viewHeight, in float2 uv)
{
	// Constrain uvs to valid sub texel range (avoid zenith derivative issue making LUT usage visible)
	uv = float2(fromSubUvsToUnit(uv.x, 192.0f), fromSubUvsToUnit(uv.y, 108.0f));

	float Vhorizon = sqrt(viewHeight * viewHeight - Atmosphere.BottomRadius * Atmosphere.BottomRadius);
	float CosBeta = Vhorizon / viewHeight;				// GroundToHorizonCos
	float Beta = acos(CosBeta);
	float ZenithHorizonAngle = PI - Beta;

	if (uv.y < 0.5f)
	{
		float coord = 2.0 * uv.y;
		coord = 1.0 - coord;
#if NONLINEARSKYVIEWLUT
		coord *= coord;
#endif
		coord = 1.0 - coord;
		viewZenithCosAngle = cos(ZenithHorizonAngle * coord);
	}
	else
	{
		float coord = uv.y * 2.0 - 1.0;
#if NONLINEARSKYVIEWLUT
		coord *= coord;
#endif
		viewZenithCosAngle = cos(ZenithHorizonAngle + Beta * coord);
	}

	float coord = uv.x;
	coord *= coord;
	lightViewCosAngle = -(coord * 2.0 - 1.0);
}

void SkyViewLutParamsToUv(AtmosphereParameters Atmosphere, in bool IntersectGround, in float viewZenithCosAngle, in float lightViewCosAngle, in float viewHeight, out float2 uv)
{
	float Vhorizon = sqrt(viewHeight * viewHeight - Atmosphere.BottomRadius * Atmosphere.BottomRadius);
	float CosBeta = Vhorizon / viewHeight;				// GroundToHorizonCos
	float Beta = acos(CosBeta);
	float ZenithHorizonAngle = PI - Beta;

	if (!IntersectGround)
	{
		float coord = acos(viewZenithCosAngle) / ZenithHorizonAngle;
		coord = 1.0 - coord;
#if NONLINEARSKYVIEWLUT
		coord = sqrt(coord);
#endif
		coord = 1.0 - coord;
		uv.y = coord * 0.5f;
	}
	else
	{
		float coord = (acos(viewZenithCosAngle) - ZenithHorizonAngle) / Beta;
#if NONLINEARSKYVIEWLUT
		coord = sqrt(coord);
#endif
		uv.y = coord * 0.5f + 0.5f;
	}

	{
		float coord = -lightViewCosAngle * 0.5f + 0.5f;
		coord = sqrt(coord);
		uv.x = coord;
	}

	// Constrain uvs to valid sub texel range (avoid zenith derivative issue making LUT usage visible)
	uv = float2(fromUnitToSubUvs(uv.x, 192.0f), fromUnitToSubUvs(uv.y, 108.0f));
}

bool MoveToTopAtmosphere(inout float3 WorldPos, in float3 WorldDir, in float AtmosphereTopRadius)
{
	float viewHeight = length(WorldPos);
	if (viewHeight > AtmosphereTopRadius)
	{
		float tTop = raySphereIntersectNearest(WorldPos, WorldDir, float3(0.0f, 0.0f, 0.0f), AtmosphereTopRadius);
		if (tTop >= 0.0f)
		{
			float3 UpVector = WorldPos / viewHeight;
			float3 UpOffset = UpVector * -PLANET_RADIUS_OFFSET;
			WorldPos = WorldPos + WorldDir * tTop + UpOffset;
		}
		else
		{
			// Ray is not intersecting the atmosphere
			return false;
		}
	}
	return true; // ok to start tracing
}

float3 GetMultipleScattering(AtmosphereParameters Atmosphere, float3 scattering, float3 extinction, float3 worlPos, float viewZenithCosAngle)
{
	float2 uv = saturate(float2(viewZenithCosAngle * 0.5f + 0.5f, (length(worlPos) - Atmosphere.BottomRadius) / (Atmosphere.TopRadius - Atmosphere.BottomRadius)));
	uv = float2(fromUnitToSubUvs(uv.x, MultiScatteringLUTRes), fromUnitToSubUvs(uv.y, MultiScatteringLUTRes));

	float3 multiScatteredLuminance = MultiScatTexture.SampleLevel(samplerLinearClamp, uv, 0).rgb;
	return multiScatteredLuminance;
}

struct SingleScatteringResult
{
	float3 L;						// Scattered light (luminance)
	float3 OpticalDepth;			// Optical depth (1/m)
	float3 Transmittance;			// Transmittance in [0,1] (unitless)
	float3 MultiScatAs1;

	float3 NewMultiScatStep0Out;
	float3 NewMultiScatStep1Out;
};

SingleScatteringResult IntegrateScatteredLuminance(
	in float2 pixPos, in float3 WorldPos, in float3 WorldDir, in float3 SunDir, in AtmosphereParameters Atmosphere,
	in bool ground, in float SampleCountIni, in float DepthBufferValue, in bool VariableSampleCount,
	in bool MieRayPhase, in float tMaxMax = 9000000.0f)
{
	// Adapter
	float3 gSunIlluminance = 1.0;

	SingleScatteringResult result = (SingleScatteringResult)0;

#if 0
	float3 ClipSpace = float3((pixPos / float2(gResolution)) * float2(2.0, -2.0) - float2(1.0, -1.0), 1.0);
#endif

	// Compute next intersection with atmosphere or ground 
	float3 earthO = float3(0.0f, 0.0f, 0.0f);
	float tBottom = raySphereIntersectNearest(WorldPos, WorldDir, earthO, Atmosphere.BottomRadius);
	float tTop = raySphereIntersectNearest(WorldPos, WorldDir, earthO, Atmosphere.TopRadius);
	float tMax = 0.0f;
	if (tBottom < 0.0f)
	{
		if (tTop < 0.0f)
		{
			tMax = 0.0f; // No intersection with earth nor atmosphere: stop right away  
			return result;
		}
		else
		{
			tMax = tTop;
		}
	}
	else
	{
		if (tTop > 0.0f)
		{
			tMax = min(tTop, tBottom);
		}
	}

#if 0
	if (DepthBufferValue >= 0.0f)
	{
		ClipSpace.z = DepthBufferValue;
		if (ClipSpace.z < 1.0f)
		{
			float4 DepthBufferWorldPos = mul(gSkyInvViewProjMat, float4(ClipSpace, 1.0));
			DepthBufferWorldPos /= DepthBufferWorldPos.w;

			float tDepth = length(DepthBufferWorldPos.xyz - (WorldPos + float3(0.0, 0.0, -AtmosphereConstants.BottomRadius))); // apply earth offset to go back to origin as top of earth mode. 
			if (tDepth < tMax)
			{
				tMax = tDepth;
			}
		}
		//		if (VariableSampleCount && ClipSpace.z == 1.0f)
		//			return result;
	}
#endif

	tMax = min(tMax, tMaxMax);

	// Sample count 
	float SampleCount = SampleCountIni;
	float SampleCountFloor = SampleCountIni;
	float tMaxFloor = tMax;
	if (VariableSampleCount)
	{
		SampleCount = lerp(RayMarchMinMaxSPP.x, RayMarchMinMaxSPP.y, saturate(tMax * 0.01));
		SampleCountFloor = floor(SampleCount);
		tMaxFloor = tMax * SampleCountFloor / SampleCount;	// rescale tMax to map to the last entire step segment.
	}
	float dt = tMax / SampleCount;

	// Phase functions
	const float uniformPhase = 1.0 / (4.0 * PI);
	const float3 wi = SunDir;
	const float3 wo = WorldDir;
	float cosTheta = dot(wi, wo);
	float MiePhaseValue = hgPhase(Atmosphere.MiePhaseG, -cosTheta);	// mnegate cosTheta because due to WorldDir being a "in" direction. 
	float RayleighPhaseValue = RayleighPhase(cosTheta);

#ifdef ILLUMINANCE_IS_ONE
	// When building the scattering factor, we assume light illuminance is 1 to compute a transfert function relative to identity illuminance of 1.
	// This make the scattering factor independent of the light. It is now only linked to the atmosphere properties.
	float3 globalL = 1.0f;
#else
	float3 globalL = gSunIlluminance;
#endif

	// Ray march the atmosphere to integrate optical depth
	float3 L = 0.0f;
	float3 throughput = 1.0;
	float3 OpticalDepth = 0.0;
	float t = 0.0f;
	float tPrev = 0.0;
#ifdef HILLAIRE20_ADJUST_INTEGRATION
	const float SampleSegmentT = 0.5f;
#else
	const float SampleSegmentT = 0.3f;
#endif // const float SampleSegmentT = 0.3f;
	for (float s = 0.0f; s < SampleCount; s += 1.0f)
	{
		if (VariableSampleCount)
		{
			// More expenssive but artefact free
			float t0 = (s) / SampleCountFloor;
			float t1 = (s + 1.0f) / SampleCountFloor;
			// Non linear distribution of sample within the range.
			t0 = t0 * t0;
			t1 = t1 * t1;
			// Make t0 and t1 world space distances.
			t0 = tMaxFloor * t0;
			if (t1 > 1.0)
			{
				t1 = tMax;
				//	t1 = tMaxFloor;	// this reveal depth slices
			}
			else
			{
				t1 = tMaxFloor * t1;
			}
			//t = t0 + (t1 - t0) * (whangHashNoise(pixPos.x, pixPos.y, gFrameId * 1920 * 1080)); // With dithering required to hide some sampling artefact relying on TAA later? This may even allow volumetric shadow?
			t = t0 + (t1 - t0) * SampleSegmentT;
			dt = t1 - t0;
		}
		else
		{
			//t = tMax * (s + SampleSegmentT) / SampleCount;
			// Exact difference, important for accuracy of multiple scattering
			float NewT = tMax * (s + SampleSegmentT) / SampleCount;
			dt = NewT - t;
			t = NewT;

#ifdef HILLAIRE20_ADJUST_INTEGRATION
			dt = tMax / SampleCount;
#endif // HILLAIRE20_ADJUST_INTEGRATION
		}
		float3 P = WorldPos + t * WorldDir;

		MediumSampleRGB medium = sampleMediumRGB(P, Atmosphere);
		const float3 SampleOpticalDepth = medium.extinction * dt;
		const float3 SampleTransmittance = exp(-SampleOpticalDepth);
		OpticalDepth += SampleOpticalDepth;

		float pHeight = length(P);
		float3 UpVector = P / pHeight;
		float SunZenithCosAngle = dot(SunDir, UpVector);
		float2 uv;
		LutTransmittanceParamsToUv(Atmosphere, pHeight, SunZenithCosAngle, uv);
		float3 TransmittanceToSun = TransmittanceLutTexture.SampleLevel(samplerLinearClamp, uv, 0).rgb;

		float3 PhaseTimesScattering;
		if (MieRayPhase)
		{
			PhaseTimesScattering = medium.scatteringMie * MiePhaseValue + medium.scatteringRay * RayleighPhaseValue;
		}
		else
		{
			PhaseTimesScattering = medium.scattering * uniformPhase;
		}

		// Earth shadow 
		float tEarth = raySphereIntersectNearest(P, SunDir, earthO + PLANET_RADIUS_OFFSET * UpVector, Atmosphere.BottomRadius);
		float earthShadow = tEarth >= 0.0f ? 0.0f : 1.0f;

		// Dual scattering for multi scattering 

		float3 multiScatteredLuminance = 0.0f;
#if MULTISCATAPPROX_ENABLED
		multiScatteredLuminance = GetMultipleScattering(Atmosphere, medium.scattering, medium.extinction, P, SunZenithCosAngle);
#endif

		float shadow = 1.0f;
#if SHADOWMAP_ENABLED
		// First evaluate opaque shadow
		shadow = getShadow(AtmosphereConstants, P);
#endif

		float3 S = globalL * (earthShadow * shadow * TransmittanceToSun * PhaseTimesScattering + multiScatteredLuminance * medium.scattering);

		// When using the power serie to accumulate all sattering order, serie r must be <1 for a serie to converge.
		// Under extreme coefficient, MultiScatAs1 can grow larger and thus result in broken visuals.
		// The way to fix that is to use a proper analytical integration as proposed in slide 28 of http://www.frostbite.com/2015/08/physically-based-unified-volumetric-rendering-in-frostbite/
		// However, it is possible to disable as it can also work using simple power serie sum unroll up to 5th order. The rest of the orders has a really low contribution.
#define MULTI_SCATTERING_POWER_SERIE 1

#if MULTI_SCATTERING_POWER_SERIE==0
		// 1 is the integration of luminance over the 4pi of a sphere, and assuming an isotropic phase function of 1.0/(4*PI)
		result.MultiScatAs1 += throughput * medium.scattering * 1 * dt;
#else
		float3 MS = medium.scattering * 1;
		float3 MSint = (MS - MS * SampleTransmittance) / medium.extinction;
		result.MultiScatAs1 += throughput * MSint;
#endif

		// Evaluate input to multi scattering 
		{
			float3 newMS;

			newMS = earthShadow * TransmittanceToSun * medium.scattering * uniformPhase * 1;
			result.NewMultiScatStep0Out += throughput * (newMS - newMS * SampleTransmittance) / medium.extinction;
			//	result.NewMultiScatStep0Out += SampleTransmittance * throughput * newMS * dt;

			newMS = medium.scattering * uniformPhase * multiScatteredLuminance;
			result.NewMultiScatStep1Out += throughput * (newMS - newMS * SampleTransmittance) / medium.extinction;
			//	result.NewMultiScatStep1Out += SampleTransmittance * throughput * newMS * dt;
		}

#if 0
		L += throughput * S * dt;
		throughput *= SampleTransmittance;
#else
		// See slide 28 at http://www.frostbite.com/2015/08/physically-based-unified-volumetric-rendering-in-frostbite/ 
		float3 Sint = (S - S * SampleTransmittance) / medium.extinction;	// integrate along the current step segment 
		L += throughput * Sint;														// accumulate and also take into account the transmittance from previous steps
		throughput *= SampleTransmittance;
#endif

		tPrev = t;
	}

	if (ground && tMax == tBottom && tBottom > 0.0)
	{
		// Account for bounced light off the earth
		float3 P = WorldPos + tBottom * WorldDir;
		float pHeight = length(P);

		float3 UpVector = P / pHeight;
		float SunZenithCosAngle = dot(SunDir, UpVector);
		float2 uv;
		LutTransmittanceParamsToUv(Atmosphere, pHeight, SunZenithCosAngle, uv);
		float3 TransmittanceToSun = TransmittanceLutTexture.SampleLevel(samplerLinearClamp, uv, 0).rgb;

		const float NdotL = saturate(dot(normalize(UpVector), normalize(SunDir)));
		L += globalL * TransmittanceToSun * throughput * NdotL * Atmosphere.GroundAlbedo / PI;
	}

	result.L = L;
	result.OpticalDepth = OpticalDepth;
	result.Transmittance = throughput;
	return result;
}

void UvToLutTransmittanceParams(AtmosphereParameters Atmosphere, out float viewHeight, out float viewZenithCosAngle, in float2 uv)
{
	//uv = float2(fromSubUvsToUnit(uv.x, TRANSMITTANCE_TEXTURE_WIDTH), fromSubUvsToUnit(uv.y, TRANSMITTANCE_TEXTURE_HEIGHT)); // No real impact so off
	float x_mu = uv.x;
	float x_r = uv.y;

	float H = sqrt(Atmosphere.TopRadius * Atmosphere.TopRadius - Atmosphere.BottomRadius * Atmosphere.BottomRadius);
	float rho = H * x_r;
	viewHeight = sqrt(rho * rho + Atmosphere.BottomRadius * Atmosphere.BottomRadius);

	float d_min = Atmosphere.TopRadius - viewHeight;
	float d_max = rho + H;
	float d = d_min + x_mu * (d_max - d_min);
	viewZenithCosAngle = d == 0.0 ? 1.0f : (H * H - rho * rho - d * d) / (2.0 * viewHeight * d);
	viewZenithCosAngle = clamp(viewZenithCosAngle, -1.0, 1.0);
}

AtmosphereParameters GetAtmosphereParameters()
{
	AtmosphereParameters Parameters;
	Parameters.AbsorptionExtinction = mConstants.mAtmosphere.mOzoneExtinction;

	// Traslation from Bruneton2017 parameterisation.
	Parameters.RayleighDensityExpScale = mConstants.mAtmosphere.mRayleighDensity.mLayer1.mExpScale;
	Parameters.MieDensityExpScale = mConstants.mAtmosphere.mMieDensity.mLayer1.mExpScale;
	Parameters.AbsorptionDensity0LayerWidth = mConstants.mAtmosphere.mOzoneDensity.mLayer0.mWidth;
	Parameters.AbsorptionDensity0ConstantTerm = mConstants.mAtmosphere.mOzoneDensity.mLayer0.mConstantTerm;
	Parameters.AbsorptionDensity0LinearTerm = mConstants.mAtmosphere.mOzoneDensity.mLayer0.mLinearTerm;
	Parameters.AbsorptionDensity1ConstantTerm = mConstants.mAtmosphere.mOzoneDensity.mLayer1.mConstantTerm;
	Parameters.AbsorptionDensity1LinearTerm = mConstants.mAtmosphere.mOzoneDensity.mLayer1.mLinearTerm;

	Parameters.MiePhaseG = mConstants.mAtmosphere.mMiePhaseFunctionG;
	Parameters.RayleighScattering = mConstants.mAtmosphere.mRayleighScattering;
	Parameters.MieScattering = mConstants.mAtmosphere.mMieScattering;
	Parameters.MieAbsorption = mConstants.mAtmosphere.mMieExtinction - mConstants.mAtmosphere.mMieScattering;
	Parameters.MieExtinction = mConstants.mAtmosphere.mMieExtinction;
	Parameters.GroundAlbedo = mConstants.mAtmosphere.mGroundAlbedo;
	Parameters.BottomRadius = mConstants.mAtmosphere.mBottomRadius;
	Parameters.TopRadius = mConstants.mAtmosphere.mTopRadius;
	return Parameters;
}

[RootSignature(ROOT_SIGNATURE_COMMON)]
[numthreads(8, 8, 1)]
void TransLUT(
	uint3 inGroupThreadID : SV_GroupThreadID,
	uint3 inGroupID : SV_GroupID,
	uint3 inDispatchThreadID : SV_DispatchThreadID,
	uint inGroupIndex : SV_GroupIndex)
{
	// Adapter
	uint TRANSMITTANCE_TEXTURE_WIDTH = 256;
	uint TRANSMITTANCE_TEXTURE_HEIGHT = 64;
	// TransmittanceTexUAV.GetDimensions(TRANSMITTANCE_TEXTURE_WIDTH, TRANSMITTANCE_TEXTURE_HEIGHT); // calculate in shader introduce extra error
	float2 pixPos = inDispatchThreadID.xy + 0.5; // half pixel offset
	float3 sun_direction = GetSunDirection();

	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	// float2 pixPos = Input.position.xy;
	AtmosphereParameters Atmosphere = GetAtmosphereParameters();

	// Compute camera position from LUT coords
	float2 uv = (pixPos) / float2(TRANSMITTANCE_TEXTURE_WIDTH, TRANSMITTANCE_TEXTURE_HEIGHT);

// #define APPLY_SUB_UVS_TO_UNIT
#ifdef APPLY_SUB_UVS_TO_UNIT
	// uv = float2(fromSubUvsToUnit(uv.x, TRANSMITTANCE_TEXTURE_WIDTH), fromSubUvsToUnit(uv.y, TRANSMITTANCE_TEXTURE_HEIGHT));

	// w/o Adjust
	//	viewHeight			= [6360, 6460]
	//	viewZenithCosAngle	= [0.97949, -0.00107] [0.25928, -0.17358]
	// 
	// w/ Adjust - discontinuity on viewZenithCosAngle, also first and last row is not correct from ray-sphere intersection
	//	viewHeight			= [6360, 6460]
	//	viewZenithCosAngle	= [1.0, 0.0] [1.0, -0.00069, ..., -0.17517] 
#endif // APPLY_SUB_UVS_TO_UNIT

	float viewHeight;
	float viewZenithCosAngle;
	UvToLutTransmittanceParams(Atmosphere, viewHeight, viewZenithCosAngle, uv);

	//  A few extra needed constants
	float3 WorldPos = float3(0.0f, 0.0f, viewHeight);
	float3 WorldDir = float3(0.0f, sqrt(1.0 - viewZenithCosAngle * viewZenithCosAngle), viewZenithCosAngle);

	const bool ground = false;
	const float SampleCountIni = 40.0;	// Can go a low as 10 sample but energy lost starts to be visible.
	const float DepthBufferValue = -1.0;
	const bool VariableSampleCount = false;
	const bool MieRayPhase = false;
	float3 transmittance = exp(-IntegrateScatteredLuminance(pixPos, WorldPos, WorldDir, sun_direction, Atmosphere, ground, SampleCountIni, DepthBufferValue, VariableSampleCount, MieRayPhase).OpticalDepth);

	TransmittanceTexUAV[inDispatchThreadID.xy] = float4(transmittance, 1);

	// Debug
	// TransmittanceTexUAV[inDispatchThreadID.xy] = float4(viewZenithCosAngle, viewHeight, 0, 1);
	// TransmittanceTexUAV[inDispatchThreadID.xy] = float4(WorldDir, 1);
	// TransmittanceTexUAV[inDispatchThreadID.xy] = float4(uv, 0, 1);
	// TransmittanceTexUAV[inDispatchThreadID.xy] = float4(pixPos, 0, 1);
}

groupshared float3 MultiScatAs1SharedMem[64];
groupshared float3 LSharedMem[64];

[RootSignature(ROOT_SIGNATURE_COMMON)]
[numthreads(1, 1, 64)]
void NewMultiScatCS(
	uint3 inGroupThreadID : SV_GroupThreadID,
	uint3 inGroupID : SV_GroupID,
	uint3 inDispatchThreadID : SV_DispatchThreadID,
	uint inGroupIndex : SV_GroupIndex)
{
	// Adapter
	uint3 ThreadId = inDispatchThreadID.xyz;
	float MultipleScatteringFactor = 1.0;

	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	float2 pixPos = float2(ThreadId.xy) + 0.5f;
	float2 uv = pixPos / MultiScatteringLUTRes;


	uv = float2(fromSubUvsToUnit(uv.x, MultiScatteringLUTRes), fromSubUvsToUnit(uv.y, MultiScatteringLUTRes));

	AtmosphereParameters Atmosphere = GetAtmosphereParameters();

	float cosSunZenithAngle = uv.x * 2.0 - 1.0;
	float3 sunDir = float3(0.0, sqrt(saturate(1.0 - cosSunZenithAngle * cosSunZenithAngle)), cosSunZenithAngle);
	// We adjust again viewHeight according to PLANET_RADIUS_OFFSET to be in a valid range.
	float viewHeight = Atmosphere.BottomRadius + saturate(uv.y + PLANET_RADIUS_OFFSET) * (Atmosphere.TopRadius - Atmosphere.BottomRadius - PLANET_RADIUS_OFFSET);

	float3 WorldPos = float3(0.0f, 0.0f, viewHeight);
	float3 WorldDir = float3(0.0f, 0.0f, 1.0f);


	const bool ground = true;
	const float SampleCountIni = 20;// a minimum set of step is required for accuracy unfortunately
	const float DepthBufferValue = -1.0;
	const bool VariableSampleCount = false;
	const bool MieRayPhase = false;

	const float SphereSolidAngle = 4.0 * PI;
	const float IsotropicPhase = 1.0 / SphereSolidAngle;


	// Reference. Since there are many sample, it requires MULTI_SCATTERING_POWER_SERIE to be true for accuracy and to avoid divergences (see declaration for explanations)
#define SQRTSAMPLECOUNT 8
	const float sqrtSample = float(SQRTSAMPLECOUNT);
	float i = 0.5f + float(ThreadId.z / SQRTSAMPLECOUNT);
	float j = 0.5f + float(ThreadId.z - float((ThreadId.z / SQRTSAMPLECOUNT) * SQRTSAMPLECOUNT));
	{
		float randA = i / sqrtSample;
		float randB = j / sqrtSample;
		float theta = 2.0f * PI * randA;
		float phi = PI * randB;
		float cosPhi = cos(phi);
		float sinPhi = sin(phi);
		float cosTheta = cos(theta);
		float sinTheta = sin(theta);
		WorldDir.x = cosTheta * sinPhi;
		WorldDir.y = sinTheta * sinPhi;
		WorldDir.z = cosPhi;
		SingleScatteringResult result = IntegrateScatteredLuminance(pixPos, WorldPos, WorldDir, sunDir, Atmosphere, ground, SampleCountIni, DepthBufferValue, VariableSampleCount, MieRayPhase);

		// Debug
		// MultiScattTexUAV[inDispatchThreadID.xy] = float4(sunDir, 1.0f);
		// return;

		MultiScatAs1SharedMem[ThreadId.z] = result.MultiScatAs1 * SphereSolidAngle / (sqrtSample * sqrtSample);
		LSharedMem[ThreadId.z] = result.L * SphereSolidAngle / (sqrtSample * sqrtSample);
	}
#undef SQRTSAMPLECOUNT

	GroupMemoryBarrierWithGroupSync();

	// 64 to 32
	if (ThreadId.z < 32)
	{
		MultiScatAs1SharedMem[ThreadId.z] += MultiScatAs1SharedMem[ThreadId.z + 32];
		LSharedMem[ThreadId.z] += LSharedMem[ThreadId.z + 32];
	}
	GroupMemoryBarrierWithGroupSync();

	// 32 to 16
	if (ThreadId.z < 16)
	{
		MultiScatAs1SharedMem[ThreadId.z] += MultiScatAs1SharedMem[ThreadId.z + 16];
		LSharedMem[ThreadId.z] += LSharedMem[ThreadId.z + 16];
	}
	GroupMemoryBarrierWithGroupSync();

	// 16 to 8 (16 is thread group min hardware size with intel, no sync required from there)
	if (ThreadId.z < 8)
	{
		MultiScatAs1SharedMem[ThreadId.z] += MultiScatAs1SharedMem[ThreadId.z + 8];
		LSharedMem[ThreadId.z] += LSharedMem[ThreadId.z + 8];
	}
	GroupMemoryBarrierWithGroupSync();
	if (ThreadId.z < 4)
	{
		MultiScatAs1SharedMem[ThreadId.z] += MultiScatAs1SharedMem[ThreadId.z + 4];
		LSharedMem[ThreadId.z] += LSharedMem[ThreadId.z + 4];
	}
	GroupMemoryBarrierWithGroupSync();
	if (ThreadId.z < 2)
	{
		MultiScatAs1SharedMem[ThreadId.z] += MultiScatAs1SharedMem[ThreadId.z + 2];
		LSharedMem[ThreadId.z] += LSharedMem[ThreadId.z + 2];
	}
	GroupMemoryBarrierWithGroupSync();
	if (ThreadId.z < 1)
	{
		MultiScatAs1SharedMem[ThreadId.z] += MultiScatAs1SharedMem[ThreadId.z + 1];
		LSharedMem[ThreadId.z] += LSharedMem[ThreadId.z + 1];
	}
	GroupMemoryBarrierWithGroupSync();
	if (ThreadId.z > 0)
		return;

	float3 MultiScatAs1 = MultiScatAs1SharedMem[0] * IsotropicPhase;			// Equation 7 f_ms
	float3 InScatteredLuminance = LSharedMem[0] * IsotropicPhase;				// Equation 5 L_2ndOrder

	// MultiScatAs1 represents the amount of luminance scattered as if the integral of scattered luminance over the sphere would be 1.
	//  - 1st order of scattering: one can ray-march a straight path as usual over the sphere. That is InScatteredLuminance.
	//  - 2nd order of scattering: the inscattered luminance is InScatteredLuminance at each of samples of fist order integration. Assuming a uniform phase function that is represented by MultiScatAs1,
	//  - 3nd order of scattering: the inscattered luminance is (InScatteredLuminance * MultiScatAs1 * MultiScatAs1)
	//  - etc.
#if	MULTI_SCATTERING_POWER_SERIE==0
	float3 MultiScatAs1SQR = MultiScatAs1 * MultiScatAs1;
	float3 L = InScatteredLuminance * (1.0 + MultiScatAs1 + MultiScatAs1SQR + MultiScatAs1 * MultiScatAs1SQR + MultiScatAs1SQR * MultiScatAs1SQR);
#else
	// For a serie, sum_{n=0}^{n=+inf} = 1 + r + r^2 + r^3 + ... + r^n = 1 / (1.0 - r), see https://en.wikipedia.org/wiki/Geometric_series 
	const float3 r = MultiScatAs1;
	const float3 SumOfAllMultiScatteringEventsContribution = 1.0f / (1.0 - r);
	float3 L = InScatteredLuminance * SumOfAllMultiScatteringEventsContribution;// Equation 10 Psi_ms
#endif

	MultiScattTexUAV[ThreadId.xy] = float4(MultipleScatteringFactor * L, 1.0f);

	// Debug
}

[RootSignature(ROOT_SIGNATURE_COMMON)]
[numthreads(8, 8, 1)]
void SkyViewLut(
	uint3 inGroupThreadID : SV_GroupThreadID,
	uint3 inGroupID : SV_GroupID,
	uint3 inDispatchThreadID : SV_DispatchThreadID,
	uint inGroupIndex : SV_GroupIndex)
{
	AtmosphereParameters Atmosphere = GetAtmosphereParameters();

	float2 dimensions = float2(192, 108);
	float3 sun_direction = GetSunDirection();

	float2 pixPos = inDispatchThreadID.xy + 0.5; 		// half pixel offset
	float2 uv = pixPos / dimensions;

	float3 camera_position = mConstants.CameraPosition().xyz * mConstants.mAtmosphere.mSceneScale;
	camera_position.y = max(camera_position.y, 1.0 * mConstants.mAtmosphere.mSceneScale); // Keep observer position above ground
	float3 WorldPos = camera_position - PlanetCenterPositionPS();
	float viewHeight = length(WorldPos);

	float viewZenithCosAngle;
	float lightViewCosAngle;
	UvToSkyViewLutParams(Atmosphere, viewZenithCosAngle, lightViewCosAngle, viewHeight, uv);

	float3 SunDir;
	{
		float3 UpVector = WorldPos / viewHeight;
		float sunZenithCosAngle = dot(UpVector, sun_direction);

		// Debug - Validation
		// viewHeight = 6360.5;
		// sunZenithCosAngle = 0.5;

		SunDir = normalize(float3(sqrt(1.0 - sunZenithCosAngle * sunZenithCosAngle), 0.0, sunZenithCosAngle));
	}

	WorldPos = float3(0.0f, 0.0f, viewHeight);
	float viewZenithSinAngle = sqrt(1 - viewZenithCosAngle * viewZenithCosAngle);
	float3 WorldDir = float3(
		viewZenithSinAngle * lightViewCosAngle,
		viewZenithSinAngle * sqrt(1.0 - lightViewCosAngle * lightViewCosAngle),
		viewZenithCosAngle);

	// Move to top atmospehre
	if (!MoveToTopAtmosphere(WorldPos, WorldDir, Atmosphere.TopRadius))
	{
		SkyViewLutTexUAV[inDispatchThreadID.xy] = float4(0, 0, 0, 1);
		return;
	}

	const bool ground = false;
	const float SampleCountIni = 30;
	const float DepthBufferValue = -1.0;
	const bool VariableSampleCount = true;
	const bool MieRayPhase = true;
	SingleScatteringResult ss = IntegrateScatteredLuminance(pixPos, WorldPos, WorldDir, SunDir, Atmosphere, ground, SampleCountIni, DepthBufferValue, VariableSampleCount, MieRayPhase);

	float3 L = ss.L;
	if (mConstants.mAtmosphere.mHillaire20SkyViewInLuminance)
		L *= kSolarKW2LM * kPreExposure * mConstants.mAtmosphere.mSolarIrradiance;
	SkyViewLutTexUAV[inDispatchThreadID.xy] = float4(L, 1); // match

	// Debug
	// SkyViewLutTexUAV[inDispatchThreadID.xy] = float4(pixPos, 0, 1); // match
	// SkyViewLutTexUAV[inDispatchThreadID.xy] = float4(RelativeWorldPos, 1); // match
	// SkyViewLutTexUAV[inDispatchThreadID.xy] = float4(WorldDir, 1); // tiny error
	// SkyViewLutTexUAV[inDispatchThreadID.xy] = float4(viewZenithSinAngle, viewZenithCosAngle, 0, 1);	// tiny error, from UvToSkyViewLutParams
	// SkyViewLutTexUAV[inDispatchThreadID.xy] = float4(uv, viewHeight, 1); // match
	// SkyViewLutTexUAV[inDispatchThreadID.xy] = float4(SunDir, 1); // match, 0.90052, 0.00, 0.43482
	// SkyViewLutTexUAV[inDispatchThreadID.xy] = MultiScatTexture.SampleLevel(samplerLinearClamp, uv, 0); // match

	// SkyViewLutTexUAV[inDispatchThreadID.xy] = float4(WorldPos, 1);
	// SkyViewLutTexUAV[inDispatchThreadID.xy] = float4(WorldDir, 1);
	// SkyViewLutTexUAV[inDispatchThreadID.xy] = float4(SunDir, 1);
}

[RootSignature(ROOT_SIGNATURE_COMMON)]
[numthreads(8, 8, 1)]
void CameraVolumes(
	uint3 inGroupThreadID : SV_GroupThreadID,
	uint3 inGroupID : SV_GroupID,
	uint3 inDispatchThreadID : SV_DispatchThreadID,
	uint inGroupIndex : SV_GroupIndex)
{
	AtmosphereParameters Atmosphere = GetAtmosphereParameters();

	float2 pixPos = inDispatchThreadID.xy + 0.5; // half pixel offset
	float2 dims = float2(32, 32);

	float2 ndc_xy								= ((pixPos / dims) * 2.f - 1.f);										// [0,1] => [-1,1]
	ndc_xy.y									= -ndc_xy.y;															// Flip y
	float4 point_on_near_plane					= mul(mConstants.mInverseProjectionMatrix, float4(ndc_xy, 0.0, 1.0));
	float3 ray_direction_vs						= normalize(point_on_near_plane.xyz / point_on_near_plane.w);
	float3 ray_direction_ws						= mul(mConstants.mInverseViewMatrix, float4(ray_direction_vs, 0.0)).xyz;
	
	float3 WorldDir = ray_direction_ws;
	WorldDir.xyz = WorldDir.xzy; // Y-up to Z-up
	float3 SunDir = mConstants.mSunDirection.xzy; // Y-up to Z-up

	// Debug
	float3 WorldDir_Raw = WorldDir;

	float3 camera = mConstants.CameraPosition().xzy; // Y-up to Z-up

	// Debug
	camera.xy = 0; // flat

	float3 camPos = camera + float3(0, 0, Atmosphere.BottomRadius);

	///////////////

#if 0
	float2 pixPos = Input.position.xy;
	AtmosphereParameters AtmosphereConstants = GetAtmosphereParameters();
	float3 ClipSpace = float3((pixPos / float2(gResolution)) * float2(2.0, -2.0) - float2(1.0, -1.0), 0.5);
	float4 HPos = mul(gSkyInvViewProjMat, float4(ClipSpace, 1.0));
	float3 WorldDir = normalize(HPos.xyz / HPos.w - camera);
	float earthR = AtmosphereConstants.BottomRadius;
	float3 earthO = float3(0.0, 0.0, -earthR);
	float3 camPos = camera + float3(0, 0, earthR);
	float3 SunDir = sun_direction;
	float3 SolarLuminance = 0.0;
#endif

#if 0
	float Slice = ((float(Input.sliceId) + 0.5f) / AP_SLICE_COUNT);
#else
	float Slice = ((float(inDispatchThreadID.z) + 0.5f) / AP_SLICE_COUNT);
#endif
	Slice *= Slice;	// squared distribution
	Slice *= AP_SLICE_COUNT;

	float3 WorldPos = camPos;
	float viewHeight;


	// Compute position from froxel information
	float tMax = AerialPerspectiveSliceToDepth(Slice);
	float3 newWorldPos = WorldPos + tMax * WorldDir;


	// If the voxel is under the ground, make sure to offset it out on the ground.
	viewHeight = length(newWorldPos);
	if (viewHeight <= (Atmosphere.BottomRadius + PLANET_RADIUS_OFFSET))
	{
		// Apply a position offset to make sure no artefact are visible close to the earth boundaries for large voxel.
		newWorldPos = normalize(newWorldPos) * (Atmosphere.BottomRadius + PLANET_RADIUS_OFFSET + 0.001f);
		WorldDir = normalize(newWorldPos - camPos);
		tMax = length(newWorldPos - camPos);
	}
	float tMaxMax = tMax;


	// Move ray marching start up to top atmosphere.
	viewHeight = length(WorldPos);
	if (viewHeight >= Atmosphere.TopRadius)
	{
		float3 prevWorlPos = WorldPos;
		if (!MoveToTopAtmosphere(WorldPos, WorldDir, Atmosphere.TopRadius))
		{
			// Ray is not intersecting the atmosphere
			AtmosphereCameraScatteringVolumeUAV[inDispatchThreadID.xyz] = float4(0.0, 0.0, 0.0, 1.0);
			return;
		}
		float LengthToAtmosphere = length(prevWorlPos - WorldPos);
		if (tMaxMax < LengthToAtmosphere)
		{
			// tMaxMax for this voxel is not within earth atmosphere
			AtmosphereCameraScatteringVolumeUAV[inDispatchThreadID.xyz] = float4(0.0, 0.0, 0.0, 1.0);
			return;
		}
		// Now world position has been moved to the atmosphere boundary: we need to reduce tMaxMax accordingly. 
		tMaxMax = max(0.0, tMaxMax - LengthToAtmosphere);
	}


	const bool ground = false;
#if 0
	const float SampleCountIni = max(1.0, float(Input.sliceId + 1.0) * 2.0f);
#else
	const float SampleCountIni = max(1.0, float(inDispatchThreadID.z + 1.0) * 2.0f); // ?
#endif
	const float DepthBufferValue = -1.0;
	const bool VariableSampleCount = false;
	const bool MieRayPhase = true;
	SingleScatteringResult ss = IntegrateScatteredLuminance(pixPos, WorldPos, WorldDir, SunDir, Atmosphere, ground, SampleCountIni, DepthBufferValue, VariableSampleCount, MieRayPhase, tMaxMax);

	const float Transmittance = dot(ss.Transmittance, float3(1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f)); // ?
	AtmosphereCameraScatteringVolumeUAV[inDispatchThreadID.xyz] = float4(ss.L, 1.0 - Transmittance); // almost match

	// Debug
	//AtmosphereCameraScatteringVolumeUAV[inDispatchThreadID.xyz] = float4(inDispatchThreadID.xyz, 1.0); // match
	//AtmosphereCameraScatteringVolumeUAV[inDispatchThreadID.xyz] = float4(pixPos, 0.0, 1.0); // match
	//AtmosphereCameraScatteringVolumeUAV[inDispatchThreadID.xyz] = float4(WorldPos, 1.0); // match
	//AtmosphereCameraScatteringVolumeUAV[inDispatchThreadID.xyz] = float4(SunDir, 1.0); // match
	//AtmosphereCameraScatteringVolumeUAV[inDispatchThreadID.xyz] = float4(SampleCountIni.xxx, 1.0); // match
	//AtmosphereCameraScatteringVolumeUAV[inDispatchThreadID.xyz] = float4(mConstants.CameraFront().xzy, 1.0); // match
	//AtmosphereCameraScatteringVolumeUAV[inDispatchThreadID.xyz] = float4(WorldDir_Raw, 1.0); // match	
	//AtmosphereCameraScatteringVolumeUAV[inDispatchThreadID.xyz] = float4(newWorldPos, 1.0); // almost match
	//AtmosphereCameraScatteringVolumeUAV[inDispatchThreadID.xyz] = float4(WorldDir, 1.0); // match
	//AtmosphereCameraScatteringVolumeUAV[inDispatchThreadID.xyz] = float4(tMax.xxx, 1.0); // match
}

namespace AtmosphereIntegration { namespace Hillaire20 {

void GetSkyRadiance(Ray inRayPS, out float3 outSkyRadiance, out float3 outTransmittanceToTop)
{
	outSkyRadiance = 0;
	outTransmittanceToTop = 1; // [TODO]

	float3 camera = inRayPS.mOrigin - PlanetCenterPositionPS();
	float3 view_ray = inRayPS.mDirection;
	float3 sun_direction = GetSunDirection();

	float r = length(camera);
	float rmu = dot(camera, view_ray);
	float distance_to_top_atmosphere_boundary = -rmu - sqrt(rmu * rmu - r * r + mConstants.mAtmosphere.mTopRadius * mConstants.mAtmosphere.mTopRadius);

	if (distance_to_top_atmosphere_boundary > 0.0) 
	{
		// Outer space

		// Move camera to top of atmosphere along view direction
		camera = camera + view_ray * distance_to_top_atmosphere_boundary;
		r = mConstants.mAtmosphere.mTopRadius;
		rmu += distance_to_top_atmosphere_boundary;
	}
	else if (r > mConstants.mAtmosphere.mTopRadius) 
	{
		// No hit
		return;
	}

	float mu = rmu / r;
	float mu_s = dot(camera, sun_direction) / r;
	float nu = dot(view_ray, sun_direction);
	bool ray_r_mu_intersects_ground = RayIntersectsGround(r, mu);

	AtmosphereParameters Atmosphere = GetAtmosphereParameters();

	float3 UpVector = normalize(camera);
	float3 WorldDir = view_ray;

	// Lat/Long mapping in SkyViewLut()
	float3 sideVector = normalize(cross(UpVector, WorldDir));		// assumes non parallel vectors
	float3 forwardVector = normalize(cross(sideVector, UpVector));	// aligns toward the sun light but perpendicular to up vector
	float2 lightOnPlane = float2(dot(sun_direction, forwardVector), dot(sun_direction, sideVector));
	lightOnPlane = normalize(lightOnPlane);
	float lightViewCosAngle = lightOnPlane.x;

	float2 uv;
	LutTransmittanceParamsToUv(Atmosphere, r, dot(WorldDir, UpVector), uv);
	outTransmittanceToTop = TransmittanceLutTexture.SampleLevel(samplerLinearClamp, uv, 0).rgb;

	SkyViewLutParamsToUv(Atmosphere, ray_r_mu_intersects_ground, mu, lightViewCosAngle, r, uv);
	outSkyRadiance = SkyViewLutTexSRV.SampleLevel(samplerLinearClamp, uv, 0).rgb;

	if (mConstants.mAtmosphere.mHillaire20SkyViewInLuminance)
		outSkyRadiance = outSkyRadiance / kPreExposure * kSolarLM2KW; // PreExposed lm/m^2 -> kW/m^2
	else
		outSkyRadiance *= mConstants.mAtmosphere.mSolarIrradiance;

	// Debug
	// outSkyRadiance = 0.1234;
	// outSkyRadiance = float3(lightViewCosAngle, 0, 0);
}

}} // namespace AtmosphereIntegration { namespace Hillaire20 {
