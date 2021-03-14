#define MATH_PI 3.14159265359f

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

// From https://www.shadertoy.com/view/lsdGzN
float3 hsv2rgb( in float3 c )
{
    float3 rgb = clamp( abs(fmod(c.x*6.0+float3(0.0,4.0,2.0),6.0)-3.0)-1.0, 0.0, 1.0 );

    rgb = rgb*rgb*(3.0-2.0*rgb); // cubic smoothing 

    return c.z * lerp(1.0, rgb, c.y);
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

// http://www.pbr-book.org/3ed-2018/Monte_Carlo_Integration/Importance_Sampling.html#PowerHeuristic
float MIS_PowerHeuristic(int nf, float fPdf, int ng, float gPdf, float power)
{
    float f = nf * fPdf;
    float g = ng * gPdf;
    return pow(f, power) / (pow(f, power) + pow(g, power));
}

// http://www.pbr-book.org/3ed-2018/Monte_Carlo_Integration/Importance_Sampling.html#BalanceHeuristic
float MIS_BalanceHeuristic(int nf, float fPdf, int ng, float gPdf)
{
    return MIS_PowerHeuristic(nf, fPdf, ng, gPdf, 1.0);
}