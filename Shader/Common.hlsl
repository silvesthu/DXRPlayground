#define MATH_PI 3.14159265359f

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

//// From https://github.com/k-ishiyama/toymodels/blob/master/shader/atmosphere/AtmosphericScattering.hlsl.h
//#define EARTH_RADIUS        6360.f // [km]
//#define ATM_TOP_HEIGHT      260.f // [km]
//#define ATM_TOP_RADIUS      (EARTH_RADIUS + ATM_TOP_HEIGHT)
//#define EARTH_CENTER        float3(0,-EARTH_RADIUS,0)
//float4 RayDoubleSphereIntersect(
//    in float3 inRayOrigin,
//    in float3 inRayDir,
//    in float2 inSphereRadii //  x = 1st sphere, y = 2nd sphere
//    )
//{
//    float a = dot(inRayDir, inRayDir);
//    float b = 2.0 * dot(inRayOrigin, inRayDir);
//    float2 c = dot(inRayOrigin, inRayOrigin) - inSphereRadii * inSphereRadii;
//    float2 d = b*b - 4.0 * a*c;
//    // d < 0 .. Ray misses the sphere
//    // d = 0 .. Ray intersects the sphere in one point
//    // d > 0 .. Ray intersects the sphere in two points
//    float2 real_root_mask = (d.xy >= 0.0);
//    d = sqrt(max(d, 0.0));
//    float4 distance = float4(-b - d.x, -b + d.x, -b - d.y, -b + d.y) / (2.0 * a);
//    distance = lerp(float4(-1.0, -1.0, -1.0, -1.0), distance, real_root_mask.xxyy);
//    // distance.x = distance to the intersection point of the 1st sphere (near side)
//    // distance.y = distance to the intersection point of the 1st sphere (far side)
//    // distance.z = distance to the intersection point of the 2nd sphere (near side)
//    // distance.w = distance to the intersection point of the 2nd sphere (far side)
//    return distance;
//}
