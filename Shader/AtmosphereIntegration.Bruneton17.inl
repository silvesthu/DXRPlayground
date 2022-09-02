// Sample at center of each segment
#define BRUNETON17_ADJUST_INTEGRATION

const static int DENSITY_SAMPLE_COUNT = 500;
// const static int DENSITY_SAMPLE_COUNT = 40; // match Hillaire20

// [Bruneton08] 4. Precomputations.Parameterization
float Encode_R_Transmittance(float r)
{
	float R_g = mPerFrameConstants.mAtmosphere.mBottomRadius;					// Ground
	float R_t = mPerFrameConstants.mAtmosphere.mTopRadius;						// Top

	float H = sqrt(R_t * R_t - R_g * R_g);					// Ground to Horizon
	float rho = sqrt(max(0, r * r - R_g * R_g));			// Ground towards Horizon [0, H]

	return rho / H;
}

// Inverse of Encode_R
float Decode_R_Transmittance(float u_r)
{
	float R_g = mPerFrameConstants.mAtmosphere.mBottomRadius;
	float R_t = mPerFrameConstants.mAtmosphere.mTopRadius;

	float H = sqrt(R_t * R_t - R_g * R_g);
	float rho = H * u_r;

	return sqrt(rho * rho + R_g * R_g);
}

// Try to reduce artifact near mBottomRadius where transmittance for Aerial Perspective is calculated by (tiny float / tiny float)
const static bool kUseMuTransmittancePower = false;
const static float kMuTransmittancePower = kUseMuTransmittancePower ? 0.4 : 1.0;

// [Bruneton08] 4. Precomputations.Parameterization ?
float Encode_Mu_Transmittance(float mu, float r)
{
	float R_g = mPerFrameConstants.mAtmosphere.mBottomRadius;
	float R_t = mPerFrameConstants.mAtmosphere.mTopRadius;

	float H = sqrt(R_t * R_t - R_g * R_g);					// Ground to Horizon
	float rho = sqrt(max(0, r * r - R_g * R_g));			// Ground towards Horizon [0, H]

	float d = DistanceToTopAtmosphereBoundary(r, mu);		// Distance to AtmosphereBoundary

	float d_min = R_t - r;									// P to Top = Shortest possible distance to AtmosphereBoundary
	float d_max = rho + H;									// Projected-P to Ground towards Horizon = Longest possible distance to AtmosphereBoundary

	// i.e. Encode View Zenith as P to Top distance

	float u_mu = (d - d_min) / (d_max - d_min);				// d to [0, 1]

	u_mu = pow(u_mu, 1.0 / kMuTransmittancePower);

	return u_mu;
}

// Inverse of Encode_Mu
float Decode_Mu_Transmittance(float u_mu, float u_r)
{
	u_mu = pow(u_mu, kMuTransmittancePower);

	float R_g = mPerFrameConstants.mAtmosphere.mBottomRadius;
	float R_t = mPerFrameConstants.mAtmosphere.mTopRadius;

	float r = Decode_R_Transmittance(u_r);

	float H = sqrt(R_t * R_t - R_g * R_g);
	float rho = H * u_r;

	float d_min = R_t - r;
	float d_max = rho + H;

	float d = d_min + u_mu * (d_max - d_min);

	float mu = InvDistanceToTopAtmosphereBoundary(r, d);
	return ClampCosine(mu);
}

float2 Decode2D_Transmittance(float2 u_mu_u_r)
{
	float mu = Decode_Mu_Transmittance(u_mu_u_r.x, u_mu_u_r.y);
	float r = Decode_R_Transmittance(u_mu_u_r.y);

	return float2(mu, r);
}

// Inverse of Decode2D_Transmittance
float2 Encode2D_Transmittance(float2 mu_r)
{
	float u_mu = Encode_Mu_Transmittance(mu_r.x, mu_r.y);
	float u_r = Encode_R_Transmittance(mu_r.y);

	return float2(u_mu, u_r);
}

float2 Decode2D_Irradiance(float2 u_mu_u_r)
{
	float mu = lerp(-1, 1, u_mu_u_r.x);
	float r = lerp(mPerFrameConstants.mAtmosphere.mBottomRadius, mPerFrameConstants.mAtmosphere.mTopRadius, u_mu_u_r.y);

	return float2(mu, r);
}

// Inverse of Decode2D_Irradiance
float2 Encode2D_Irradiance(float2 mu_r)
{
	float u_mu = invlerp(-1, 1, mu_r.x);
	float u_r = invlerp(mPerFrameConstants.mAtmosphere.mBottomRadius, mPerFrameConstants.mAtmosphere.mTopRadius, mu_r.y);

	return float2(u_mu, u_r);
}

float4 Decode4D(uint3 inTexCoords, RWTexture3D<float4> inTexture, out bool intersects_ground)
{
	// https://www.desmos.com/calculator/y2vvrrb6yr

	float u_mu_s;
	float u_mu;
	float u_r;
	float u_nu;

	if (true)
	{
		// [Bruneton17]

		uint3 size;
		inTexture.GetDimensions(size.x, size.y, size.z);
		uint slice_count = mPerFrameConstants.mAtmosphere.mSliceCount;
		uint slice_size = size.x / slice_count;

		u_mu_s = (inTexCoords.x % slice_size) / (slice_size - 1.0);		// X
		intersects_ground = inTexCoords.y < size.y / 2;
		if (intersects_ground)
			u_mu = (size.y / 2.0 - 1 - inTexCoords.y) / (size.y / 2.0 - 1.0);	// Y - [0.0 ~ 0.5] in coordinates -> [1.0 -> 0.0]
		else
			u_mu = (inTexCoords.y - size.y / 2.0) / (size.y / 2.0 - 1.0);		// Y - [0.5 ~ 1.0] in coordinates -> [0.0 -> 1.0]
		u_r = (inTexCoords.z) / (size.z - 1.0);							// Z
		u_nu = (inTexCoords.x / slice_size) / (slice_count - 1.0);		// X
	}
	else
	{
		// [Yusov13]

		uint3 size;
		inTexture.GetDimensions(size.x, size.y, size.z);
		uint slice_count = mPerFrameConstants.mAtmosphere.mSliceCount;
		uint slice_size = size.z / slice_count;

		u_mu_s = (inTexCoords.z % slice_size) / (slice_size - 1.0);		// Z
		intersects_ground = inTexCoords.y < size.y / 2;
		if (intersects_ground)
			u_mu = (size.y / 2.0 - 1 - inTexCoords.y) / (size.y / 2.0 - 1.0);	// Y - [0.0 ~ 0.5] in coordinates -> [1.0 -> 0.0]
		else
			u_mu = (inTexCoords.y - size.y / 2.0) / (size.y / 2.0 - 1.0);		// Y - [0.5 ~ 1.0] in coordinates -> [0.0 -> 1.0]
		u_r = (inTexCoords.x) / (size.x - 1.0);							// X
		u_nu = (inTexCoords.z / slice_size) / (slice_count - 1.0);		// Z
	}

	float r;	// Height
	float mu;	// Cosine of view zenith
	float mu_s; // Cosine of sun zenith
	float nu;	// Cosine of view sun angle

	// [Bruneton08]
	float R_t = mPerFrameConstants.mAtmosphere.mTopRadius;				// Top
	float R_g = mPerFrameConstants.mAtmosphere.mBottomRadius;			// Ground
	float H_squared = R_t * R_t - R_g * R_g;		// [NOTE] Avoid precision loss on sqrt(squared)
	float H = sqrt(H_squared);						// Ground to Horizon
	float rho = H * u_r;							// Ground towards Horizon [0, H]

	// r - Height
	{
		// [Mapping] Height -> Ground towards Horizon

		// Same as
		// r = Decode_R(u_r, false);

		// Non-linear mapping, Figure 3
		r = sqrt(rho * rho + R_g * R_g);
	}

	// mu - View Zenith
	{
		float rho = H * u_r;
		if (intersects_ground)
		{
			// [Mapping] View Zenith -> P to Ground distance

			float d_min = r - R_g;
			float d_max = rho;
			float d = d_min + (d_max - d_min) * u_mu; // Distance to Ground

			mu = d == 0.0 ? float(-1.0) : ClampCosine((-rho * rho - d * d) / (2.0 * r * d));
		}
		else
		{
			// [Mapping] View Zenith -> P to Top distance

			// Same as
			// mu = Decode_Mu(y, u_r, false);

			float d_min = R_t - r;
			float d_max = rho + H;
			float d = d_min + (d_max - d_min) * u_mu; // Distance to AtmosphereBoundary

			mu = d == 0.0 ? float(1.0) : ClampCosine((H_squared - rho * rho - d * d) / (2.0 * r * d));
		}
	}

	// mu_s - Sun Zenith
	{
		// [Mapping] Sun Zenith -> ?

		// [NOTE] Formulas other than Bruneton17 are likely fitted curve with max_sun_zenith_angle = 102.0 degrees
		switch (mPerFrameConstants.mAtmosphere.mMuSEncodingMode)
		{
		case AtmosphereMuSEncodingMode::Bruneton17:
			{
				bool use_half_precision = true;
				float max_sun_zenith_angle = (use_half_precision ? 102.0 : 120.0) / 180.0 * MATH_PI;
				float mu_s_min = cos(max_sun_zenith_angle);

				float d_min = R_t - R_g;
				float d_max = H;

				float D = DistanceToTopAtmosphereBoundary(R_g, mu_s_min);

				float A = (D - d_min) / (d_max - d_min);
				float a = (A - u_mu_s * A) / (1.0 + u_mu_s * A);

				float d = d_min + min(a, A) * (d_max - d_min);

				mu_s = d == 0.0 ? float(1.0) : ClampCosine((H * H - d * d) / (2.0 * R_g * d));
			}
			break;
		case AtmosphereMuSEncodingMode::Bruneton08:
			{
				mu_s = (log(1.0 - (1 - exp(-3.6)) * u_mu_s) + 0.6) / -3.0;
			}
			break;
		case AtmosphereMuSEncodingMode::Elek09:
			{
				mu_s = (log(1.0 - (1 - exp(-3.6)) * u_mu_s) + 0.8) / -2.8;
			}
			break;
		case AtmosphereMuSEncodingMode::Yusov13:
			{
				mu_s = tan((2.0 * u_mu_s - 1.0 + 0.26) * 1.1) / tan(1.26 * 1.1);
				mu_s = ClampCosine(mu_s); // [NOTE] without clamp, mu_s might be slightly larger than 1, which cause clamp on nu fail to work
			}
			break;
		default:
			break;
        }
	}

	// nu - View Sun Angle
	{
		// [Mapping] View Sun Angle -> View Sun Angle (trivial)

		nu = ClampCosine(u_nu * 2.0 - 1.0);
	}

	// [Note] View Sun Angle is bounded by View Zenith and Sun Zenith
	nu = clamp(nu, mu * mu_s - sqrt((1.0 - mu * mu) * (1.0 - mu_s * mu_s)), mu * mu_s + sqrt((1.0 - mu * mu) * (1.0 - mu_s * mu_s)));

	return float4(r, mu, mu_s, nu);
}

// Inverse of Decode4D
void Encode4D(float4 r_mu_mu_s_nu, bool intersects_ground, Texture3D<float4> inTexture, out float3 outUVW0, out float3 outUVW1, out float outS)
{
	float r = r_mu_mu_s_nu.x;
	float mu = r_mu_mu_s_nu.y;
	float mu_s = r_mu_mu_s_nu.z;
	float nu = r_mu_mu_s_nu.w;

	// [Bruneton08]
	float R_t = mPerFrameConstants.mAtmosphere.mTopRadius;				// Top
	float R_g = mPerFrameConstants.mAtmosphere.mBottomRadius;			// Ground
	float H_squared = R_t * R_t - R_g * R_g;		// [NOTE] Avoid precision loss on sqrt(squared)
	float H = sqrt(H_squared);
	float rho = SafeSqrt(r * r - mPerFrameConstants.mAtmosphere.mBottomRadius * mPerFrameConstants.mAtmosphere.mBottomRadius);

	// r - Height
	float u_r = rho / H;

	// mu - View Zenith
	float r_mu = r * mu;
	float discriminant = r_mu * r_mu - r * r + mPerFrameConstants.mAtmosphere.mBottomRadius * mPerFrameConstants.mAtmosphere.mBottomRadius;
	float u_mu;
	if (intersects_ground)
	{
		float d = -r_mu - SafeSqrt(discriminant);
		float d_min = r - mPerFrameConstants.mAtmosphere.mBottomRadius;
		float d_max = rho;
		u_mu = d_max == d_min ? 0.0 : (d - d_min) / (d_max - d_min);
	}
	else
	{
		float d = -r_mu + SafeSqrt(discriminant + H_squared);
		float d_min = mPerFrameConstants.mAtmosphere.mTopRadius - r;
		float d_max = rho + H;
		u_mu = (d - d_min) / (d_max - d_min);
	}

	// mu_s - Sun Zenith
	float u_mu_s = 0.0;

	// [NOTE] Formulas other than Bruneton17 are likely fitted curve with max_sun_zenith_angle = 102.0 degrees
	switch (mPerFrameConstants.mAtmosphere.mMuSEncodingMode)
	{
	default: // fallthrough
	case AtmosphereMuSEncodingMode::Bruneton17:
	{
		bool use_half_precision = true;
		float max_sun_zenith_angle = (use_half_precision ? 102.0 : 120.0) / 180.0 * MATH_PI;
		float mu_s_min = cos(max_sun_zenith_angle);

		float d = DistanceToTopAtmosphereBoundary(mPerFrameConstants.mAtmosphere.mBottomRadius, mu_s);
		float d_min = mPerFrameConstants.mAtmosphere.mTopRadius - mPerFrameConstants.mAtmosphere.mBottomRadius;
		float d_max = H;
		float a = (d - d_min) / (d_max - d_min);
		float D = DistanceToTopAtmosphereBoundary(mPerFrameConstants.mAtmosphere.mBottomRadius, mu_s_min);
		float A = (D - d_min) / (d_max - d_min);
		u_mu_s = max(1.0 - a / A, 0.0) / (1.0 + a);
	}
	break;
	case AtmosphereMuSEncodingMode::Bruneton08:
	{
		u_mu_s = max((1.0 - exp(-3.0 * mu_s - 0.6)) / (1.0 - exp(-3.6)), 0.0);
	}
	break;
	case AtmosphereMuSEncodingMode::Elek09:
	{
		u_mu_s = max((1.0 - exp(-2.8 * mu_s - 0.8)) / (1.0 - exp(-3.6)), 0.0);
	}
	break;
	case AtmosphereMuSEncodingMode::Yusov13:
	{
		u_mu_s = 0.5 * (atan(max(mu_s, -0.1975) * tan(1.26 * 1.1)) / 1.1 + (1.0 - 0.26));
	}
	break;
	}

	// nu - View Sun Angle
	float u_nu = (nu + 1.0) / 2.0;

	//////////////////////////////////////////////////////////////////////////////////

	if (true)
	{
		// [Bruneton17]

		uint3 size;
		inTexture.GetDimensions(size.x, size.y, size.z);
		uint slice_count = mPerFrameConstants.mAtmosphere.mSliceCount;
		uint slice_size = size.x / slice_count;

		float offset = u_mu_s * (slice_size - 1.0);
		float step = u_nu * (slice_count - 1.0);

		outS = frac(step);														// For interpolation between slices

		float3 tex_coords = 0;
		tex_coords.x = floor(step) * slice_size + offset;						// X
		if (intersects_ground)
			tex_coords.y = (size.y / 2.0 - 1.0) - u_mu * (size.y / 2.0 - 1.0);	// Y - [1.0 ~ 0.0] -> [0.0 ~ 0.5] in coodinates
		else
			tex_coords.y = u_mu * (size.y / 2.0 - 1.0) + (size.y / 2.0);		// Y - [0.0 ~ 1.0] -> [0.5 ~ 1.0] in coodinates
		tex_coords.z = u_r * (size.z - 1.0);									// Z

		// Coordinates to UV
		outUVW0.x = X_to_U((tex_coords.x + 0) / (size.x - 1), size.x);
		outUVW1.x = X_to_U((tex_coords.x + slice_size) / (size.x - 1), size.x);
		outUVW0.y = outUVW1.y = X_to_U(tex_coords.y / (size.y - 1), size.y);
		outUVW0.z = outUVW1.z = X_to_U(tex_coords.z / (size.z - 1), size.z);
	}
	else
	{
		// [Yusov13]

		uint3 size;
		inTexture.GetDimensions(size.x, size.y, size.z);
		uint slice_count = mPerFrameConstants.mAtmosphere.mSliceCount;
		uint slice_size = size.z / slice_count;

		float offset = u_mu_s * (slice_size - 1.0);
		float step = u_nu * (slice_count - 1.0);

		outS = frac(step);														// For interpolation between slices

		float3 tex_coords = 0;
		tex_coords.z = floor(step) * slice_size + offset;						// Z
		if (intersects_ground)
			tex_coords.y = (size.y / 2.0 - 1.0) - u_mu * (size.y / 2.0 - 1.0);	// Y - [1.0 ~ 0.0] -> [0.0 ~ 0.5] in coodinates
		else
			tex_coords.y = u_mu * (size.y / 2.0 - 1.0) + (size.y / 2.0);		// Y - [0.0 ~ 1.0] -> [0.5 ~ 1.0] in coodinates
		tex_coords.x = u_r * (size.x - 1.0);									// X

		// Coordinates to UV
		outUVW0.z = X_to_U((tex_coords.z + 0) / (size.z - 1), size.z);
		outUVW1.z = X_to_U((tex_coords.z + slice_size) / (size.z - 1), size.z);
		outUVW0.y = outUVW1.y = X_to_U(tex_coords.y / (size.y - 1), size.y);
		outUVW0.x = outUVW1.x = X_to_U(tex_coords.x / (size.x - 1), size.x);
	}
}

float IntegrateDensity(DensityProfile inProfile, float2 mu_r)
{
	const int SAMPLE_COUNT = DENSITY_SAMPLE_COUNT;

	float mu = mu_r.x;
	float r = mu_r.y;

	float step_distance = DistanceToTopAtmosphereBoundary(r, mu) / float(SAMPLE_COUNT);
	float result = 0.0;
#ifdef BRUNETON17_ADJUST_INTEGRATION
	for (int i = 0; i < SAMPLE_COUNT; ++i)
#else
	for (int i = 0; i <= SAMPLE_COUNT; ++i)
#endif // BRUNETON17_ADJUST_INTEGRATION
	{
#ifdef BRUNETON17_ADJUST_INTEGRATION
		float d_i = float(i + 0.5) * step_distance;
#else
		float d_i = float(i) * step_distance;
#endif // BRUNETON17_ADJUST_INTEGRATION

		// Distance between the current sample point and the planet center.
		float r_i = sqrt(d_i * d_i + 2.0 * r * mu * d_i + r * r);

		// Extinction Coefficient Multiplier (Density) based on altitude.
		float altitude = r_i - mPerFrameConstants.mAtmosphere.mBottomRadius;
		float beta_i = GetProfileDensity(inProfile, altitude);

		// Sample weight (from the trapezoidal rule).
#ifdef BRUNETON17_ADJUST_INTEGRATION
		float weight_i = 1.0;
#else
		float weight_i = (i == 0 || i == SAMPLE_COUNT) ? 0.5 : 1.0;
#endif // BRUNETON17_ADJUST_INTEGRATION

		result += beta_i * weight_i * step_distance;
	}

	return result;
}

float3 ComputeTransmittance(float2 mu_r)
{
	// See [Bruneton08] 2.2 (5)
	// Extinction Coefficients at sea level is extracted out of integration

	float3 rayleigh		= mPerFrameConstants.mAtmosphere.mRayleighExtinction.xyz * IntegrateDensity(mPerFrameConstants.mAtmosphere.mRayleighDensity, mu_r);
	float3 mie			= mPerFrameConstants.mAtmosphere.mMieExtinction.xyz *		IntegrateDensity(mPerFrameConstants.mAtmosphere.mMieDensity, mu_r);
	float3 ozone		= mPerFrameConstants.mAtmosphere.mOzoneExtinction.xyz *	IntegrateDensity(mPerFrameConstants.mAtmosphere.mOzoneDensity, mu_r);

	float3 transmittance = exp(-(rayleigh + mie + ozone));
	return transmittance;
}

float3 GetTransmittanceToTopAtmosphereBoundary(float r, float mu)
{
	// [Bruneton17] GetTransmittanceToTopAtmosphereBoundary

	float2 mu_r = float2(mu, r);
	float2 transmittance_uv = XY_to_UV(Encode2D_Transmittance(mu_r), TransmittanceSRV);

	// Sample 2D LUT
	float3 transmittance = TransmittanceSRV.SampleLevel(BilinearClampSampler, transmittance_uv, 0).xyz;

	// Debug - Compute transmittance directly instead of sampling LUT
	if (false)
		transmittance = ComputeTransmittance(mu_r);

	return transmittance;
}

float3 GetTransmittance(float r, float mu, float d, bool intersects_ground)
{
	// [Bruneton17] GetTransmittance

	// P -> Top along Eye -> P
	float r_d = ClampRadius(sqrt(d * d + 2.0 * r * mu * d + r * r));
	float mu_d = ClampCosine((r * mu + d) / r_d); // [NOTE] direction is same, but up vector changed

	// Eye -> P
	if (intersects_ground)
		return min(GetTransmittanceToTopAtmosphereBoundary(r_d, -mu_d) / GetTransmittanceToTopAtmosphereBoundary(r, -mu), 1.0);
	else
		return min(GetTransmittanceToTopAtmosphereBoundary(r, mu) / GetTransmittanceToTopAtmosphereBoundary(r_d, mu_d), 1.0);
}

float3 GetTransmittanceToSun(float r, float mu_s)
{
	// [Bruneton17] GetTransmittanceToSun

	float sin_theta_h = mPerFrameConstants.mAtmosphere.mBottomRadius / r;
	float cos_theta_h = -sqrt(max(1.0 - sin_theta_h * sin_theta_h, 0.0));

	// [Bruneton08 Doc] Approximate visible sun disk fraction
	// [TODO] Add notes
	return GetTransmittanceToTopAtmosphereBoundary(r, mu_s) *
		smoothstep(-sin_theta_h * mPerFrameConstants.mAtmosphere.mSunAngularRadius, sin_theta_h * mPerFrameConstants.mAtmosphere.mSunAngularRadius, mu_s - cos_theta_h);
}

void ComputeSingleScatteringIntegrand(float4 r_mu_mu_s_nu, float d, bool intersects_ground, out float3 rayleigh, out float3 mie)
{
	float r = r_mu_mu_s_nu.x;
	float mu = r_mu_mu_s_nu.y;
	float mu_s = r_mu_mu_s_nu.z;
	float nu = r_mu_mu_s_nu.w;

	// d = distance(Eye, P)

	// P -> Sun
	float r_d = ClampRadius(sqrt(d * d + 2.0 * r * mu * d + r * r)); // = ((mu * d) + r)^2 + (1-mu^2)d^2
	float mu_s_d = ClampCosine((r * mu_s + d * nu) / r_d);

	// Eye -> P then P -> Sun
	float3 transmittance = GetTransmittance(r, mu, d, intersects_ground) * GetTransmittanceToSun(r_d, mu_s_d);

	rayleigh = transmittance * GetProfileDensity(mPerFrameConstants.mAtmosphere.mRayleighDensity, r_d - mPerFrameConstants.mAtmosphere.mBottomRadius);
	mie = transmittance * GetProfileDensity(mPerFrameConstants.mAtmosphere.mMieDensity, r_d - mPerFrameConstants.mAtmosphere.mBottomRadius);
}

[RootSignature(AtmosphereRootSignature)]
[numthreads(8, 8, 1)]
void ComputeTransmittanceCS(
	uint3 inGroupThreadID : SV_GroupThreadID,
	uint3 inGroupID : SV_GroupID,
	uint3 inDispatchThreadID : SV_DispatchThreadID,
	uint inGroupIndex : SV_GroupIndex)
{
	// XY
	float2 xy = DispatchThreadID_to_XY(inDispatchThreadID.xy, TransmittanceUAV);

	// Decode
	float2 mu_r = Decode2D_Transmittance(xy);

	// Transmittance
	float3 transmittance = ComputeTransmittance(mu_r);

	// Output
	TransmittanceUAV[inDispatchThreadID.xy] = float4(transmittance, 1.0);

	// Debug
	// float3 debug = float3(uv, 0);
	// debug = (r - 6360) / 60.0;
	// debug = mu.xxx;
	// debug = IntegrateDensity(mPerFrameConstants.mAtmosphere.mRayleighDensity, r, mu).xxx;
	// TransmittanceUAV[inDispatchThreadID.xy] = float4(debug, 1.0);
	// TransmittanceUAV[inDispatchThreadID.xy] = float4(mu_r.xy, 0, 1);
	// TransmittanceUAV[inDispatchThreadID.xy] = float4(xy, 0, 1);
	// TransmittanceUAV[inDispatchThreadID.xy] = float4(1,0,0, 1.0);
}

[RootSignature(AtmosphereRootSignature)]
[numthreads(8, 8, 1)]
void ComputeDirectIrradianceCS(
	uint3 inGroupThreadID : SV_GroupThreadID,
	uint3 inGroupID : SV_GroupID,
	uint3 inDispatchThreadID : SV_DispatchThreadID,
	uint inGroupIndex : SV_GroupIndex)
{
	// xy
	float2 xy = DispatchThreadID_to_XY(inDispatchThreadID.xy, IrradianceUAV);

	// Decode
	float2 mu_r = Decode2D_Irradiance(xy);
	float mu_s = mu_r.x;
	float r = mu_r.y;

	// Approximate average of the cosine factor mu_s over the visible fraction of the Sun disc.
	float alpha_s = mPerFrameConstants.mAtmosphere.mSunAngularRadius;
	float average_cosine_factor = 0.0;
	if (mu_s < -alpha_s) // Sun below horizon
		average_cosine_factor = 0.0;
	else if (mu_s > alpha_s) // Sun above horizon
		average_cosine_factor = mu_s;
	else // [-alpha_s, alpha_s] Sun behind horizon
		average_cosine_factor = (mu_s + alpha_s) * (mu_s + alpha_s) / (4.0 * alpha_s);

	// Direct Irradiance
	float3 transmittance = GetTransmittanceToTopAtmosphereBoundary(r, mu_s);
	float3 direct_irradiance = transmittance * average_cosine_factor; // [Bruneton08] 2.2 (9) L_0

	// Output
	DeltaIrradianceUAV[inDispatchThreadID.xy] = float4(direct_irradiance, 1.0);
	IrradianceUAV[inDispatchThreadID.xy] = float4(0, 0, 0, 1.0);

	// Debug
	// DeltaIrradianceUAV[inDispatchThreadID.xy] = float4(xy, 0, 1);
	// DeltaIrradianceUAV[inDispatchThreadID.xy] = float4(mu_r, 0, 1);
	// DeltaIrradianceUAV[inDispatchThreadID.xy] = float4(transmittance, 1);
	// IrradianceUAV[inDispatchThreadID.xy] = float4(uv, 0, 1);
}

void IntegrateSingleScattering(float4 r_mu_mu_s_nu, bool intersects_ground, out float3 rayleigh, out float3 mie)
{
	// [Bruneton17] ComputeSingleScattering

	float r = r_mu_mu_s_nu.x;
	float mu = r_mu_mu_s_nu.y;
	float mu_s = r_mu_mu_s_nu.z;
	float nu = r_mu_mu_s_nu.w;

	// Number of intervals for the numerical integration.
	const int SAMPLE_COUNT = 50;

	// The integration step, i.e. the length of each integration interval.
	float dx = DistanceToNearestAtmosphereBoundary(r, mu, intersects_ground) / float(SAMPLE_COUNT);

	// Integration loop.
	float3 rayleigh_sum = 0.0;
	float3 mie_sum = 0.0;
	for (int i = 0; i <= SAMPLE_COUNT; ++i)
	{
		float d_i = i * dx;

		// The Rayleigh and Mie single scattering at the current sample point.
		float3 rayleigh_i;
		float3 mie_i;

		ComputeSingleScatteringIntegrand(r_mu_mu_s_nu, d_i, intersects_ground, rayleigh_i, mie_i);

		// Sample weight (from the trapezoidal rule).
		float weight_i = (i == 0 || i == SAMPLE_COUNT) ? 0.5 : 1.0;

		rayleigh_sum += rayleigh_i * weight_i;
		mie_sum += mie_i * weight_i;
	}

	rayleigh = rayleigh_sum * dx * mPerFrameConstants.mAtmosphere.mRayleighScattering;
	mie = mie_sum * dx * mPerFrameConstants.mAtmosphere.mMieScattering;
}

[RootSignature(AtmosphereRootSignature)]
[numthreads(8, 8, 1)]
void ComputeSingleScatteringCS(
	uint3 inGroupThreadID : SV_GroupThreadID,
	uint3 inGroupID : SV_GroupID,
	uint3 inDispatchThreadID : SV_DispatchThreadID,
	uint inGroupIndex : SV_GroupIndex)
{
	float3 xyz = DispatchThreadID_to_XYZ(inDispatchThreadID.xyz, DeltaRayleighScatteringUAV);

	// Decode
	bool intersects_ground = false;
	float4 r_mu_mu_s_nu = Decode4D(inDispatchThreadID.xyz, DeltaRayleighScatteringUAV, intersects_ground);

	// Compute
	float3 delta_rayleigh = 0;
	float3 delta_mie = 0;
	IntegrateSingleScattering(r_mu_mu_s_nu, intersects_ground, delta_rayleigh, delta_mie);

	// Output
	DeltaRayleighScatteringUAV[inDispatchThreadID.xyz] = float4(delta_rayleigh, 1);
	DeltaMieScatteringUAV[inDispatchThreadID.xyz] = float4(delta_mie, 1);
	ScatteringUAV[inDispatchThreadID.xyz] = float4(delta_rayleigh.xyz, delta_mie.x);

	// Debug
	// DeltaRayleighScatteringUAV[inDispatchThreadID.xyz] = TransmittanceSRV.SampleLevel(BilinearClampSampler, float2(1,1), 0) * 100;
	// DeltaRayleighScatteringUAV[inDispatchThreadID.xyz] = float4(r_mu_mu_s_nu.xyz, 1);
	// DeltaRayleighScatteringUAV[inDispatchThreadID.xyz] = float4(xyz, xyz.x);
	// DeltaMieScatteringUAV[inDispatchThreadID.xyz] = float4(xyz, xyz.y);
	// ScatteringUAV[inDispatchThreadID.xyz] = float4(xyz, xyz.z);
}

float RayleighPhaseFunction(float nu)
{
	float k = 3.0 / (16.0 * MATH_PI);
	return k * (1.0 + nu * nu);
}

float MiePhaseFunction(float g, float nu)
{
	return PhaseFunction_CornetteShanks(g, nu);
}

float3 GetIrradiance(float r, float mu_s)
{
	float2 uv = XY_to_UV(Encode2D_Irradiance(float2(mu_s, r)), DeltaIrradianceSRV);

	// Sample 2D LUT
	return DeltaIrradianceSRV.SampleLevel(BilinearClampSampler, uv, 0).xyz;
}

float4 GetScattering(Texture3D<float4> scattering_texture, float r, float mu, float mu_s, float nu, bool intersects_ground)
{
	float3 uvw0, uvw1;
	float s;
	Encode4D(float4(r, mu, mu_s, nu), intersects_ground, scattering_texture, uvw0, uvw1, s);

	// Sample 4D LUT
	return lerp(scattering_texture.SampleLevel(BilinearClampSampler, uvw0, 0), scattering_texture.SampleLevel(BilinearClampSampler, uvw1, 0), s);
}

float3 GetScattering(float r, float mu, float mu_s, float nu, bool intersects_ground, int scattering_order)
{
	// [TODO] Add note

	if (scattering_order == 1)
	{
		float3 rayleigh = GetScattering(DeltaRayleighScatteringSRV, r, mu, mu_s, nu, intersects_ground).xyz;
		float3 mie = GetScattering(DeltaMieScatteringSRV, r, mu, mu_s, nu, intersects_ground).xyz;
		return rayleigh * RayleighPhaseFunction(nu) + mie * MiePhaseFunction(mPerFrameConstants.mAtmosphere.mMiePhaseFunctionG, nu);
	}

	return GetScattering(DeltaRayleighScatteringSRV, r, mu, mu_s, nu, intersects_ground).xyz;
}

float3 ComputeScatteringDensity(float4 r_mu_mu_s_nu, bool intersects_ground)
{
	// [Bruneton17] ComputeScatteringDensity

	float r = r_mu_mu_s_nu.x;
	float mu = r_mu_mu_s_nu.y;
	float mu_s = r_mu_mu_s_nu.z;
	float nu = r_mu_mu_s_nu.w;

	// Cosine of Zenith -> Direction
	float3 zenith_direction = float3(0.0, 0.0, 1.0);
	float3 omega = float3(sqrt(1.0 - mu * mu), 0.0, mu);							// View direction (assume on XZ plane)
	float sun_dir_x = omega.x == 0.0 ? 0.0 : (nu - mu * mu_s) / omega.x;
	float sun_dir_y = sqrt(max(1.0 - sun_dir_x * sun_dir_x - mu_s * mu_s, 0.0));
	float3 omega_s = float3(sun_dir_x, sun_dir_y, mu_s);							// Sun direction

	const int SAMPLE_COUNT = 16;

	const float dphi = MATH_PI / float(SAMPLE_COUNT);
	const float dtheta = MATH_PI / float(SAMPLE_COUNT);

	float3 rayleigh_mie = 0;

	// Nested loops for the integral over all the incident directions omega_i.
	for (int l = 0; l < SAMPLE_COUNT; ++l)
	{
		float theta = (float(l) + 0.5) * dtheta;
		float cos_theta = cos(theta);
		float sin_theta = sin(theta);
		bool ray_r_theta_intersects_ground = RayIntersectsGround(r, cos_theta);

		// The distance and transmittance to the ground only depend on theta, so we
		// can compute them in the outer loop for efficiency.
		float distance_to_ground = 0.0;
		float3 transmittance_to_ground = 0.0;
		float3 ground_albedo = 0.0;
		if (ray_r_theta_intersects_ground)
		{
			distance_to_ground = DistanceToBottomAtmosphereBoundary(r, cos_theta);
			transmittance_to_ground = GetTransmittance(r, cos_theta, distance_to_ground, true /* ray_intersects_ground */);

			ground_albedo = mPerFrameConstants.mAtmosphere.mGroundAlbedo;
		}

		for (int m = 0; m < 2 * SAMPLE_COUNT; ++m)
		{
			float phi = (float(m) + 0.5) * dphi;
			float3 omega_i =
				float3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);
			float domega_i = (dtheta) * (dphi)*sin(theta);

			// The radiance L_i arriving from direction omega_i after n-1 bounces is
			// the sum of a term given by the precomputed scattering texture for the
			// (n-1)-th order:
			float nu1 = dot(omega_s, omega_i);
			float3 incident_radiance = GetScattering(r, omega_i.z, mu_s, nu1, ray_r_theta_intersects_ground, mAtmospherePerDraw.mScatteringOrder - 1);

			// and of the contribution from the light paths with n-1 bounces and whose
			// last bounce is on the ground. This contribution is the product of the
			// transmittance to the ground, the ground albedo, the ground BRDF, and
			// the irradiance received on the ground after n-2 bounces.
			float3 ground_normal = normalize(zenith_direction * r + omega_i * distance_to_ground);
			float3 ground_irradiance = GetIrradiance(mPerFrameConstants.mAtmosphere.mBottomRadius, dot(ground_normal, omega_s));
			incident_radiance += transmittance_to_ground * ground_albedo * (1.0 / (MATH_PI)) * ground_irradiance;

			// The radiance finally scattered from direction omega_i towards direction
			// -omega is the product of the incident radiance, the scattering
			// coefficient, and the phase function for directions omega and omega_i
			// (all this summed over all particle types, i.e. Rayleigh and Mie).
			float nu2 = dot(omega, omega_i);
			float rayleigh_density = GetProfileDensity(mPerFrameConstants.mAtmosphere.mRayleighDensity, r - mPerFrameConstants.mAtmosphere.mBottomRadius);
			float mie_density = GetProfileDensity(mPerFrameConstants.mAtmosphere.mMieDensity, r - mPerFrameConstants.mAtmosphere.mBottomRadius);

			rayleigh_mie += incident_radiance *
				(
					mPerFrameConstants.mAtmosphere.mRayleighScattering * rayleigh_density * RayleighPhaseFunction(nu2)
					+
					mPerFrameConstants.mAtmosphere.mMieScattering * mie_density * MiePhaseFunction(mPerFrameConstants.mAtmosphere.mMiePhaseFunctionG, nu2)
					) * domega_i;
		}
	}

	// Debug
	// rayleigh_mie *= 1000.0;
	// float3 uvw0, uvw1;
	// float s;
	// Encode4D(float4(r, mu, mu_s, nu), false, DeltaRayleighScatteringSRV, uvw0, uvw1, s);
	// return uvw0;

	return rayleigh_mie;
}

[RootSignature(AtmosphereRootSignature)]
[numthreads(8, 8, 1)]
void ComputeScatteringDensityCS(
	uint3 inGroupThreadID : SV_GroupThreadID,
	uint3 inGroupID : SV_GroupID,
	uint3 inDispatchThreadID : SV_DispatchThreadID,
	uint inGroupIndex : SV_GroupIndex)
{
	float3 xyz = DispatchThreadID_to_XYZ(inDispatchThreadID.xyz, DeltaScatteringDensityUAV);

	uint3 dispatch_thread_id = inDispatchThreadID.xyz;

	// Decode
	bool intersects_ground = false;
	float4 r_mu_mu_s_nu = Decode4D(dispatch_thread_id, DeltaScatteringDensityUAV, intersects_ground);

	// Compute
	float3 scattering_density = ComputeScatteringDensity(r_mu_mu_s_nu, intersects_ground);

	// Output
	DeltaScatteringDensityUAV[inDispatchThreadID.xyz] = float4(scattering_density, 1.0);

	// Debug
	bool ray_r_theta_intersects_ground = false;
	// DeltaScatteringDensityUAV[inDispatchThreadID.xyz] = float4(xyz, 1.0);
	// DeltaScatteringDensityUAV[inDispatchThreadID.xyz] = float4(GetScattering(r_mu_mu_s_nu.x, r_mu_mu_s_nu.y, r_mu_mu_s_nu.z, r_mu_mu_s_nu.w, ray_r_theta_intersects_ground, mAtmospherePerDraw.mScatteringOrder - 1), 1.0);
	// DeltaScatteringDensityUAV[inDispatchThreadID.xyz] = r_mu_mu_s_nu;
	// DeltaScatteringDensityUAV[inDispatchThreadID.xyz] = float4(tan((2.0 * xyz - 1.0 + 0.26) * 1.1) / tan(1.26 * 1.1), 1.0);
}

float3 ComputeIndirectIrradiance(float2 mu_s_r)
{
	float mu_s = mu_s_r.x;
	float r = mu_s_r.y;

	const int SAMPLE_COUNT = 32;

	const float dphi = MATH_PI / float(SAMPLE_COUNT);
	const float dtheta = MATH_PI / float(SAMPLE_COUNT);

	// Sun Zenith -> Sun Direction
	float3 omega_s = float3(sqrt(1.0 - mu_s * mu_s), 0.0, mu_s);

	// Integrate Scattering over hemisphere
	float3 result = 0;
	for (int j = 0; j < SAMPLE_COUNT / 2; ++j)
	{
		// Polar Angle
		float theta = (float(j) + 0.5) * dtheta;

		for (int i = 0; i < 2 * SAMPLE_COUNT; ++i)
		{
			// Azimuthal Angle
			float phi = (float(i) + 0.5) * dphi;

			// View Direction
			float3 omega = float3(cos(phi) * sin(theta), sin(phi) * sin(theta), cos(theta));

			// Cosine of View Sun Angle
			float nu = dot(omega, omega_s);

			// Surface Element
			float domega = (dtheta) * (dphi)*sin(theta);

			// Integration
			result += GetScattering(r, omega.z, mu_s, nu, false /* ray_r_theta_intersects_ground */, mAtmospherePerDraw.mScatteringOrder) * omega.z * domega;
		}
	}

	// Debug
	if (false)
	{
		float3 uvw0, uvw1;
		float s;
		Encode4D(float4(r, 0, mu_s, 0), false, DeltaRayleighScatteringSRV, uvw0, uvw1, s);
		return uvw0;
		return lerp(DeltaRayleighScatteringSRV.SampleLevel(BilinearClampSampler, uvw0, 0), DeltaRayleighScatteringSRV.SampleLevel(BilinearClampSampler, uvw1, 0), s).xyz;
		return GetScattering(DeltaRayleighScatteringSRV, r, 0, mu_s, 0, false).xyz;
	}

	return result;
}

[RootSignature(AtmosphereRootSignature)]
[numthreads(8, 8, 1)]
void ComputeIndirectIrradianceCS(
	uint3 inGroupThreadID : SV_GroupThreadID,
	uint3 inGroupID : SV_GroupID,
	uint3 inDispatchThreadID : SV_DispatchThreadID,
	uint inGroupIndex : SV_GroupIndex)
{
	// XY
	float2 xy = DispatchThreadID_to_XY(inDispatchThreadID.xy, IrradianceUAV);

	// Decode
	float2 mu_s_r = Decode2D_Irradiance(xy);

	// Irradiance
	float3 delta_irradiance = ComputeIndirectIrradiance(mu_s_r);
	float3 irradiance = delta_irradiance;

	// Output
	DeltaIrradianceUAV[inDispatchThreadID.xy] = float4(delta_irradiance, 1.0);
	IrradianceUAV[inDispatchThreadID.xy] = IrradianceUAV[inDispatchThreadID.xy] + float4(irradiance, 0.0); // Accumulation

	// Debug
	// DeltaIrradianceUAV[inDispatchThreadID.xy] = float4(xy, 0.0, 1.0);
	// IrradianceUAV[inDispatchThreadID.xy] = float4(xy, 0.0, 1.0);
	// DeltaIrradianceUAV[inDispatchThreadID.xy] = float4(mu_s_r.x, (mu_s_r.y - mPerFrameConstants.mAtmosphere.mBottomRadius) / (mPerFrameConstants.mAtmosphere.mTopRadius - mPerFrameConstants.mAtmosphere.mBottomRadius), 0.0, 1.0);
	// DeltaIrradianceUAV[inDispatchThreadID.xy] = DeltaRayleighScatteringSRV.SampleLevel(BilinearClampSampler, float3(xy, 0), 0);
	// DeltaIrradianceUAV[inDispatchThreadID.xy] = DeltaMieScatteringSRV.SampleLevel(BilinearClampSampler, float3(xy, 0), 0);
}

void ComputeMultipleScattering(float4 r_mu_mu_s_nu, bool intersects_ground, out float3 delta_multiple_scattering)
{
	float r = r_mu_mu_s_nu.x;
	float mu = r_mu_mu_s_nu.y;
	float mu_s = r_mu_mu_s_nu.z;
	float nu = r_mu_mu_s_nu.w;

	// Number of intervals for the numerical integration.
	const int SAMPLE_COUNT = 50;

	// The integration step, i.e. the length of each integration interval.
	float dx = DistanceToNearestAtmosphereBoundary(r, mu, intersects_ground) / float(SAMPLE_COUNT);

	// Integration loop.
	float3 rayleigh_mie_sum = 0;
	for (int i = 0; i <= SAMPLE_COUNT; ++i)
	{
		float d_i = float(i) * dx;

		// The r, mu and mu_s parameters at the current integration point (see the
		// single scattering section for a detailed explanation).
		float r_i = ClampRadius(sqrt(d_i * d_i + 2.0 * r * mu * d_i + r * r));
		float mu_i = ClampCosine((r * mu + d_i) / r_i);
		float mu_s_i = ClampCosine((r * mu_s + d_i * nu) / r_i);

		// The Rayleigh and Mie multiple scattering at the current sample point.
		float3 rayleigh_mie_i =
			GetScattering(DeltaScatteringDensitySRV, r_i, mu_i, mu_s_i, nu, intersects_ground).xyz
			*
			GetTransmittance(r, mu, d_i, intersects_ground)
			*
			dx;

		// Sample weight (from the trapezoidal rule).
		float weight_i = (i == 0 || i == SAMPLE_COUNT) ? 0.5 : 1.0;
		rayleigh_mie_sum += rayleigh_mie_i * weight_i;
	}

	delta_multiple_scattering = rayleigh_mie_sum;
}

[RootSignature(AtmosphereRootSignature)]
[numthreads(8, 8, 1)]
void ComputeMultipleScatteringCS(
	uint3 inGroupThreadID : SV_GroupThreadID,
	uint3 inGroupID : SV_GroupID,
	uint3 inDispatchThreadID : SV_DispatchThreadID,
	uint inGroupIndex : SV_GroupIndex)
{
	float3 xyz = DispatchThreadID_to_XYZ(inDispatchThreadID.xyz, ScatteringUAV);

	// Decode
	bool intersects_ground = false;
	float4 r_mu_mu_s_nu = Decode4D(inDispatchThreadID, DeltaRayleighScatteringUAV, intersects_ground);

	// Scatter
	float3 delta_multiple_scattering = 0;
	ComputeMultipleScattering(r_mu_mu_s_nu, intersects_ground, delta_multiple_scattering);
	float3 delta_scattering = delta_multiple_scattering.rgb / RayleighPhaseFunction(r_mu_mu_s_nu.w);

	// Output
	DeltaRayleighScatteringUAV[inDispatchThreadID.xyz] = float4(delta_multiple_scattering, 1.0);
	ScatteringUAV[inDispatchThreadID.xyz] = ScatteringUAV[inDispatchThreadID.xyz] + float4(delta_scattering, 0.0); // Accumulation

	// Debug
	// DeltaRayleighScatteringUAV[inDispatchThreadID.xyz] = r_mu_mu_s_nu;
	// DeltaRayleighScatteringUAV[inDispatchThreadID.xyz] = DeltaScatteringDensitySRV[inDispatchThreadID.xyz];
	// DeltaRayleighScatteringUAV[inDispatchThreadID.xyz] = float4(r_mu_mu_s_nu.xyz, 1);
	// DeltaRayleighScatteringUAV[inDispatchThreadID.xyz] = float4(xyz, 1.0);
	// ScatteringUAV[inDispatchThreadID.xyz] = float4(xyz, 1.0);

	// [NOTE] Accumulation here inside shader may produce different result compared with RenderDoc capture from [Bruneton17] using alpha blend

	// [SANITY CHECK] Try to improve?
	// (1.32227, 2.15625, 2.65234, 0.35205) at [95, 127] 4-order scattering
	// (1.32227, 2.16016, 2.65625, 0.35205) at [95, 127] 4-order scattering from [Bruneton17] RenderDoc capture
}

//////////////////////////////////////////////////////////////////////////////////

namespace AtmosphereIntegration { namespace Bruneton17 {

float3 GetExtrapolatedSingleMieScattering(float4 scattering)
{
	// Algebraically this can never be negative, but rounding errors can produce
	// that effect for sufficiently short view rays.
	if (scattering.r <= 0.0)
		return 0.0;

	return scattering.rgb * scattering.a / scattering.r * (mPerFrameConstants.mAtmosphere.mRayleighScattering.r / mPerFrameConstants.mAtmosphere.mMieScattering.r) * (mPerFrameConstants.mAtmosphere.mMieScattering / mPerFrameConstants.mAtmosphere.mRayleighScattering);
}

void GetCombinedScattering(float r, float mu, float mu_s, float nu, bool ray_r_mu_intersects_ground, out float3 rayleigh_scattering, out float3 single_mie_scattering)
{
	float4 scattering = GetScattering(ScatteringSRV, r, mu, mu_s, nu, ray_r_mu_intersects_ground);

	single_mie_scattering = GetExtrapolatedSingleMieScattering(scattering);
	rayleigh_scattering = scattering.xyz;
}

void GetSkyRadiance(out float3 outSkyRadiance, out float3 outTransmittanceToTop)
{
	outSkyRadiance = 0;
	outTransmittanceToTop = 1;

	float3 camera = PlanetRayOrigin() - PlanetCenter();
	float3 view_ray = PlanetRayDirection();
	float3 sun_direction = GetSunDirection();

	float r = length(camera);
	float rmu = dot(camera, view_ray);
	float distance_to_top_atmosphere_boundary = -rmu - sqrt(rmu * rmu - r * r + mPerFrameConstants.mAtmosphere.mTopRadius * mPerFrameConstants.mAtmosphere.mTopRadius);

	if (distance_to_top_atmosphere_boundary > 0.0) 
	{
		// Outer space

		// Move camera to top of atmosphere along view direction
		camera = camera + view_ray * distance_to_top_atmosphere_boundary;
		r = mPerFrameConstants.mAtmosphere.mTopRadius;
		rmu += distance_to_top_atmosphere_boundary;
	}
	else if (r > mPerFrameConstants.mAtmosphere.mTopRadius) 
	{
		// No hit
		return;
	}

	float mu = rmu / r;
	float mu_s = dot(camera, sun_direction) / r;
	float nu = dot(view_ray, sun_direction);
	bool ray_r_mu_intersects_ground = RayIntersectsGround(r, mu);

	outTransmittanceToTop = GetTransmittanceToTopAtmosphereBoundary(r, mu);

	// [TODO] shadow
	float3 rayleigh_scattering;
	float3 single_mie_scattering;
	GetCombinedScattering(r, mu, mu_s, nu, ray_r_mu_intersects_ground, rayleigh_scattering, single_mie_scattering);

	// [TODO] light shafts

	outSkyRadiance = rayleigh_scattering * RayleighPhaseFunction(nu) + single_mie_scattering * MiePhaseFunction(mPerFrameConstants.mAtmosphere.mMiePhaseFunctionG, nu);
	outSkyRadiance *= mPerFrameConstants.mAtmosphere.mSolarIrradiance;

	// override
	{
		//outSkyRadiance = float3(1.1, 1.2, 1.3);
		//float3 uvw0, uvw1;
		//float s;
		//Encode4D(float4(r, mu, mu_s, nu), ray_r_mu_intersects_ground, ScatteringSRV, uvw0, uvw1, s);
		//rayleigh_scattering = uvw0;
		//rayleigh_scattering = ScatteringSRV.SampleLevel(BilinearClampSampler, uvw0, 0);
	}
}

void GetSkyRadianceToPoint(out float3 outSkyRadiance, out float3 outTransmittance)
{
	outSkyRadiance = 0;
	outTransmittance = 1;

	float3 hit_position = PlanetRayHitPosition() - PlanetCenter();
	float3 camera = PlanetRayOrigin() - PlanetCenter();
	float3 sun_direction = GetSunDirection();

	float3 view_ray = PlanetRayDirection();
	float r = length(camera);
	float rmu = dot(camera, view_ray);
	float distance_to_top_atmosphere_boundary = -rmu - sqrt(rmu * rmu - r * r + mPerFrameConstants.mAtmosphere.mTopRadius * mPerFrameConstants.mAtmosphere.mTopRadius);

	// If the viewer is in space and the view ray intersects the atmosphere, move
	// the viewer to the top atmosphere boundary (along the view ray):
	if (distance_to_top_atmosphere_boundary > 0.0)
	{
		// Outer space

		// Move camera to top of atmosphere along view direction
		camera = camera + view_ray * distance_to_top_atmosphere_boundary;
		r = mPerFrameConstants.mAtmosphere.mTopRadius;
		rmu += distance_to_top_atmosphere_boundary;
	}
	else if (r > mPerFrameConstants.mAtmosphere.mTopRadius)
	{
		// No hit
		return;
	}

	// Compute the r, mu, mu_s and nu parameters for the first texture lookup.
	float mu = rmu / r;
	float mu_s = dot(camera, sun_direction) / r;
	float nu = dot(view_ray, sun_direction);
	float d = length(hit_position - camera);
	bool ray_r_mu_intersects_ground = RayIntersectsGround(r, mu);

	outTransmittance = GetTransmittance(r, mu, d, ray_r_mu_intersects_ground);

	float3 rayleigh_scattering;
	float3 single_mie_scattering;
	GetCombinedScattering(r, mu, mu_s, nu, ray_r_mu_intersects_ground, rayleigh_scattering, single_mie_scattering);

	// [TODO] shadow
	float shadow_length = 0;

	// Compute the r, mu, mu_s and nu parameters for the second texture lookup.
	// If shadow_length is not 0 (case of light shafts), we want to ignore the
	// rayleigh_scattering along the last shadow_length meters of the view ray, which we
	// do by subtracting shadow_length from d (this way rayleigh_scattering_p is equal to
	// the S|x_s=x_0-lv term in Eq. (17) of our paper).
	d = max(d - shadow_length, 0.0);
	float r_p = ClampRadius(sqrt(d * d + 2.0 * r * mu * d + r * r));
	float mu_p = (r * mu + d) / r_p;
	float mu_s_p = (r * mu_s + d * nu) / r_p;
	float3 rayleigh_scattering_p = 0;
	float3 single_mie_scattering_p = 0;

	// [TODO] artifact near horizon
	GetCombinedScattering(r_p, mu_p, mu_s_p, nu, ray_r_mu_intersects_ground, rayleigh_scattering_p, single_mie_scattering_p);

	// Combine the lookup results to get the scattering between camera and point.
	float3 shadow_transmittance = outTransmittance;
	if (shadow_length > 0.0)
	{
		// This is the T(x,x_s) term in Eq. (17) of our paper, for light shafts.
		shadow_transmittance = GetTransmittance(r, mu, d, ray_r_mu_intersects_ground);
	}

	rayleigh_scattering = rayleigh_scattering - shadow_transmittance * rayleigh_scattering_p;
	single_mie_scattering = single_mie_scattering - shadow_transmittance * single_mie_scattering_p;

	single_mie_scattering = GetExtrapolatedSingleMieScattering(float4(rayleigh_scattering.rgb, single_mie_scattering.r));

	// Hack to avoid rendering artifacts when the sun is below the horizon.
	single_mie_scattering = single_mie_scattering * smoothstep(float(0.0), float(0.01), mu_s);

	outSkyRadiance = rayleigh_scattering* RayleighPhaseFunction(nu) + single_mie_scattering * MiePhaseFunction(mPerFrameConstants.mAtmosphere.mMiePhaseFunctionG, nu);
	outSkyRadiance *= mPerFrameConstants.mAtmosphere.mSolarIrradiance;
}

void GetSunAndSkyIrradiance(float3 inHitPosition, float3 inNormal, out float3 outSunIrradiance, out float3 outSkyIrradiance)
{
	float3 local_position = inHitPosition - PlanetCenter();
	float3 sun_direction = GetSunDirection();

	float r = length(local_position);
	float mu_s = dot(local_position, sun_direction) / r;

	// Indirect irradiance from sky (approximated if the surface is not horizontal).
	outSkyIrradiance = GetIrradiance(r, mu_s) * (1.0 + dot(inNormal, local_position) / r) * 0.5;
	outSkyIrradiance *= mPerFrameConstants.mAtmosphere.mSolarIrradiance;

	// Direct irradiance from sun
	outSunIrradiance = GetTransmittanceToSun(r, mu_s) * max(dot(inNormal, sun_direction), 0.0);
	outSunIrradiance *= mPerFrameConstants.mAtmosphere.mSolarIrradiance;
}

}} // namespace AtmosphereIntegration { namespace Bruneton17 {
