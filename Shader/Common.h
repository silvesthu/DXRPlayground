#pragma once

struct HitInfo
{
    float3 mBSDF;
    float mNdotL;

    float3 mEmission;

    float3 mReflectionDirection;

    float mSamplingPDF;

	// Participating Media along the ray
    float3 mTransmittance;
    float3 mInScattering;

    // [TODO] Refactor HitInfo
    float3 mHitPositionWS;
    float3 mBarycentrics;
    float2 mUV;
    float3 mVertexPositionOS;
    float3 mVertexNormalOS;
    float3 mVertexNormalWS;

    bool mDone;
};

float4 remap(float4 x, float4 a, float4 b, float4 c, float4 d)
{
    return (((x - a) / (b - a)) * (d - c)) + c;
}

// From https://www.shadertoy.com/view/tsBBWW
uint wang_hash(inout uint seed)
{
    seed = uint(seed ^ uint(61)) ^ uint(seed >> uint(16));
    seed *= uint(9);
    seed = seed ^ (seed >> 4);
    seed *= uint(0x27d4eb2d);
    seed = seed ^ (seed >> 15);
    return seed;
}
float RandomFloat01(inout uint state)
{
    return float(wang_hash(state)) / 4294967296.0;
}

float3 RandomUnitVector(inout uint state)
{
    float z = RandomFloat01(state) * 2.0f - 1.0f;
    float a = RandomFloat01(state) * 2.0f * MATH_PI;
    float r = sqrt(1.0f - z * z);
    float x = r * cos(a);
    float y = r * sin(a);
    return float3(x, y, z);
}

float3 RandomCosineDirection(inout uint state)
{
    float r1 = RandomFloat01(state);
    float r2 = RandomFloat01(state);
    float z = sqrt(1 - r2);

    float phi = 2 * MATH_PI * r1;
    float x = cos(phi) * sqrt(r2);
    float y = sin(phi) * sqrt(r2);

    return float3(x, y, z);
}

float3x3 GenerateTangentSpace(float3 inNormal)
{
    // onb - build_from_w - from Ray Tracing in One Weekend
    float3x3 matrix;
    matrix[2] = inNormal;
    float3 a = (abs(matrix[2].x) > 0.9) ? float3(0, 1, 0) : float3(1, 0, 0);
    matrix[1] = normalize(cross(matrix[2], a));
    matrix[0] = cross(matrix[2], matrix[1]);
    return matrix;
}

// From https://www.shadertoy.com/view/lsdGzN
float3 hsv2rgb( in float3 c )
{
    float3 rgb = clamp( abs(fmod(c.x*6.0+float3(0.0,4.0,2.0),6.0)-3.0)-1.0, 0.0, 1.0 );

    rgb = rgb*rgb*(3.0-2.0*rgb); // cubic smoothing 

    return c.z * lerp(1.0, rgb, c.y);
}

bool IntersectRaySphere(float3 origin, float3 direction, float3 center, float radius, out float2 distance)
{
    float3 local_origin = origin - center;

    // (local_origin + t * direction)^2 = radius^2
    float b = 2 * dot(local_origin, direction);
    float a = dot(direction, direction);
    float c = dot(local_origin, local_origin) - radius * radius;

    float discriminant = b * b - 4 * a * c;
    if (discriminant < 0)
    {
        distance = 0;
        return false;
    }

    distance.x = (-b - sqrt(discriminant)) / (2 * a);
    distance.y = (-b + sqrt(discriminant)) / (2 * a);
    return true;
}

float PhaseFunction_HenyeyGreenstein(float g, float cosine)
{
    return
        (1.0 * (1.0 - g * g))
        /
        (4.0 * MATH_PI * pow(1.0 + g * g - 2.0 * g * cosine, 3.0 / 2.0));
}

// Physically reasonable analytic expression for the single-scattering phase function
float PhaseFunction_CornetteShanks(float g, float cosine)
{
    // [NOTE] even when g = 0, it is not a isotropic distribution
    return 
        (3.0 * (1.0 - g * g) * (1.0 + cosine * cosine))
        /
        (4.0 * MATH_PI * 2.0 * (2.0 + g * g) * pow(1.0 + g * g - 2.0 * g * cosine, 3.0 / 2.0));
}

float PhaseFunction_Isotropic()
{
    return 1.0 / (4.0 * MATH_PI);
}

namespace MIS
{
    // http://www.pbr-book.org/3ed-2018/Monte_Carlo_Integration/Importance_Sampling.html#PowerHeuristic
    float PowerHeuristic(int nf, float fPdf, int ng, float gPdf, float power)
    {
        float f = nf * fPdf;
        float g = ng * gPdf;
        return pow(f, power) / (pow(f, power) + pow(g, power));
    }

    // http://www.pbr-book.org/3ed-2018/Monte_Carlo_Integration/Importance_Sampling.html#BalanceHeuristic
    float BalanceHeuristic(int nf, float fPdf, int ng, float gPdf)
    {
        return PowerHeuristic(nf, fPdf, ng, gPdf, 1.0);
    }
}

float G_SmithGGX(float inNoL, float inNoV, float inA2)
{
    float denomA = inNoV * sqrt(inA2 + (1.0 - inA2) * inNoL * inNoL);
    float denomB = inNoL * sqrt(inA2 + (1.0 - inA2) * inNoV * inNoV);

    return 2.0 * inNoL * inNoV / (denomA + denomB);
}

float3 F_Schlick(float3 inR0, float inHoV)
{
    return inR0 + (1.0 - inR0) * pow(1.0 - inHoV, 5.0);
}

float3 F_Conductor_Mitsuba(float3 inEta, float3 inK, float inCosTheta)
{
    // [Mitsuba3] From fresnel_conductor in fresnel.h
    // See also https://seblagarde.wordpress.com/2013/04/29/memo-on-fresnel-equations/

    // Modified from "Optics" by K.D. Moeller, University Science Books, 1988
    float cos_theta_i_2 = inCosTheta * inCosTheta,
        sin_theta_i_2 = 1.f - cos_theta_i_2,
        sin_theta_i_4 = sin_theta_i_2 * sin_theta_i_2;

    float3 eta_r = inEta,
        eta_i = inK;

    float3 temp_1 = eta_r * eta_r - eta_i * eta_i - sin_theta_i_2,
        a_2_pb_2 = sqrt(temp_1 * temp_1 + 4.f * eta_i * eta_i * eta_r * eta_r),
        a = sqrt(.5f * (a_2_pb_2 + temp_1));

    float3 term_1 = a_2_pb_2 + cos_theta_i_2,
        term_2 = 2.f * inCosTheta * a;

    float3 r_s = (term_1 - term_2) / (term_1 + term_2);

    float3 term_3 = a_2_pb_2 * cos_theta_i_2 + sin_theta_i_4,
        term_4 = term_2 * sin_theta_i_2;

    float3 r_p = r_s * (term_3 - term_4) / (term_3 + term_4);

    return 0.5f * (r_s + r_p);
}

// [2014][Heitz] Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs
// - https://jcgt.org/published/0003/02/03/
// - https://google.github.io/filament/Filament.md.html#materialsystem/specularbrdf/geometricshadowing(specularg)
float Vis_SmithGGXCorrelated(float inNoV, float inNoL, float inA2)
{
    float GGXV = inNoL * sqrt(inNoV * inNoV * (1.0 - inA2) + inA2);
    float GGXL = inNoV * sqrt(inNoL * inNoL * (1.0 - inA2) + inA2);
    return 0.5 / (GGXV + GGXL);
}