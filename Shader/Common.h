#pragma once

#include "RayQuery.h"

template <typename T> T fmadd(T inA, T inB, T inC) { return inA * inB + inC; }
template <typename T> T fmsub(T inA, T inB, T inC) { return inA * inB - inC; }
template <typename T> T fnmadd(T inA, T inB, T inC) { return -inA * inB + inC; }
template <typename T> T fnmsub(T inA, T inB, T inC) { return -inA * inB - inC; }

template <typename T> T sqr(T inValue) { return inValue * inValue; }
template <typename T> T safe_sqrt(T inValue) { return sqrt(max(0, inValue)); }

template <typename T> T remap(T x, T a, T b, T c, T d) { return (((x - a) / (b - a)) * (d - c)) + c; }

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
    float PowerHeuristic(int nf, float fPdf, int ng, float gPdf)
    {
        float f = nf * fPdf;
        float g = ng * gPdf;
        return (f * f) / (f * f + g * g);
    }

    // http://www.pbr-book.org/3ed-2018/Monte_Carlo_Integration/Importance_Sampling.html#BalanceHeuristic
    float BalanceHeuristic(int nf, float fPdf, int ng, float gPdf)
    {
        return (nf * fPdf) / (nf * fPdf + ng * gPdf);
    }
}

// [Filament] https://google.github.io/filament/Filament.md.html#materialsystem/specularbrdf/normaldistributionfunction(speculard)
float D_GGX(float inNdotH, float inA)
{
    float a = inNdotH * inA;
    float k = inA / (1.0 - inNdotH * inNdotH + a * a);
    return k * k * (1.0 / MATH_PI);

    // [Mitsuba3] MicrofacetDistribution::eval. Anisotropy. m = H. https://github.com/mitsuba-renderer/mitsuba3/blob/master/include/mitsuba/render/microfacet.h#L199
    // [PBRT3] TrowbridgeReitzDistribution::D. Anisotropy. wh = H. https://www.pbr-book.org/3ed-2018/Reflection_Models/Microfacet_Models

    // [Walter 2007] https://www.cs.cornell.edu/~srm/publications/EGSR07-btdf.pdf
    // [TODO] Check VNDF
}

// [TODO] G_SmithGGX_Lambda, G_SmithGGX_1

// [Schuttejoe 2018] https://schuttejoe.github.io/post/ggximportancesamplingpart1/
float G_SmithGGX(float inNdotL, float inNdotV, float inA)
{
    float a = inA;
    float a2 = a * a;

    float denomA = inNdotV * sqrt(a2 + (1.0 - a2) * inNdotL * inNdotL);
    float denomB = inNdotL * sqrt(a2 + (1.0 - a2) * inNdotV * inNdotV);

    return 2.0 * inNdotL * inNdotV / (denomA + denomB);

    // [Heitz 2014] original https://jcgt.org/published/0003/02/03/paper.pdf
    // [Filament] V_SmithGGXCorrelated. Note that V = G / (4 * NdotL * NdotV) https://google.github.io/filament/Filament.md.html#materialsystem/specularbrdf/geometricshadowing(specularg)
    // Also some implementation use tangent instead of cosine, and keep the lambda as it is.
}

float3 F_Schlick(float3 inR0, float inHoV)
{
    return inR0 + (1.0 - inR0) * pow(1.0 - inHoV, 5.0);
}

// [Mitsuba3] fresnel in fresnel.h for dielectric-dielectric interface, unpolarized
// Same as formulation on https://en.wikipedia.org/wiki/Fresnel_equations where r = R_eff = 1/2 * (R_s + R_p)
void F_Dielectric_Mitsuba(float inEta, float inCosThetaI, out float outR, out float outCosThetaT, out float outEtaIT, out float outEtaTI)
{
    float eta = inEta;
    float cos_theta_i = inCosThetaI;

    //

    float outside_mask = cos_theta_i >= 0.f;

    float rcp_eta = rcp(eta);
    float eta_it = select(outside_mask, eta, rcp_eta);
    float eta_ti = select(outside_mask, rcp_eta, eta);

    /* Using Snell's law, calculate the squared sine of the
       angle between the surface normal and the transmitted ray */
    float cos_theta_t_sqr =
        fnmadd(fnmadd(cos_theta_i, cos_theta_i, 1.f), eta_ti * eta_ti, 1.f);

    /* Find the absolute cosines of the incident/transmitted rays */
    float cos_theta_i_abs = abs(cos_theta_i);
    float cos_theta_t_abs = safe_sqrt(cos_theta_t_sqr);

    bool index_matched = (eta == 1.f);
    bool special_case = index_matched || (cos_theta_i_abs == 0.f);

    float r_sc = select(index_matched, float(0.f), float(1.f));

    /* Amplitudes of reflected waves */
    float a_s = fnmadd(eta_it, cos_theta_t_abs, cos_theta_i_abs) /
        fmadd(eta_it, cos_theta_t_abs, cos_theta_i_abs);

    float a_p = fnmadd(eta_it, cos_theta_i_abs, cos_theta_t_abs) /
        fmadd(eta_it, cos_theta_i_abs, cos_theta_t_abs);

    float r = 0.5f * (sqr(a_s) + sqr(a_p));

    if (special_case)
        r = r_sc;

    /* Adjust the sign of the transmitted direction */
    float cos_theta_t = select(cos_theta_i >= 0, -cos_theta_t_abs, cos_theta_t_abs);

    //

    outR = r;
    outCosThetaT = cos_theta_t;
    outEtaIT = eta_it;
    outEtaTI = eta_ti;
}

// [Mitsuba3] fresnel_conductor in fresnel.h for conductor-dielectric interface, unpolarized
// Index of refraction of conductor is a complex value (eta, k)
// See also https://seblagarde.wordpress.com/2013/04/29/memo-on-fresnel-equations/
float3 F_Conductor_Mitsuba(float3 inEta, float3 inK, float inCosThetaI)
{
    // Modified from "Optics" by K.D. Moeller, University Science Books, 1988
    float cos_theta_i_2 = inCosThetaI * inCosThetaI,
        sin_theta_i_2 = 1.f - cos_theta_i_2,
        sin_theta_i_4 = sin_theta_i_2 * sin_theta_i_2;

    float3 eta_r = inEta;
    float3 eta_i = inK;

    float3 temp_1 = eta_r * eta_r - eta_i * eta_i - sin_theta_i_2,
        a_2_pb_2 = sqrt(temp_1 * temp_1 + 4.f * eta_i * eta_i * eta_r * eta_r),
        a = sqrt(.5f * (a_2_pb_2 + temp_1));

    float3 term_1 = a_2_pb_2 + cos_theta_i_2;
    float3 term_2 = 2.f * inCosThetaI * a;

    float3 r_s = (term_1 - term_2) / (term_1 + term_2);

    float3 term_3 = a_2_pb_2 * cos_theta_i_2 + sin_theta_i_4;
    float3 term_4 = term_2 * sin_theta_i_2;

    float3 r_p = r_s * (term_3 - term_4) / (term_3 + term_4);

    return 0.5f * (r_s + r_p);
}

static float3 sDebugOutput = 0;
void DebugOutput(DebugMode inDebugMode, float3 inValue)
{
    if (mConstants.mDebugMode == inDebugMode)
        sDebugOutput = inValue;
}

void DebugValueInit()
{
    if (sGetDispatchRaysIndex().x == mConstants.mPixelDebugCoord.x && sGetDispatchRaysIndex().y == mConstants.mPixelDebugCoord.y)
        for (int i = 0; i < Debug::kValueArraySize; i++)
            BufferDebugUAV[0].mPixelValueArray[i] = 0;
}

void DebugValue(PixelDebugMode inPixelDebugMode, uint inRecursionCount, float4 inValue)
{
    if (mConstants.mPixelDebugMode == inPixelDebugMode)
        if (sGetDispatchRaysIndex().x == mConstants.mPixelDebugCoord.x && sGetDispatchRaysIndex().y == mConstants.mPixelDebugCoord.y && inRecursionCount < Debug::kValueArraySize)
            BufferDebugUAV[0].mPixelValueArray[inRecursionCount] = inValue;
}

void DebugTexture(bool inCondition, float4 inValue)
{
    if (inCondition)
    {
        ScreenDebugUAV[sGetDispatchRaysIndex().xy] = inValue;
    }
}