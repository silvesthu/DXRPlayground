#pragma once

#include "Shared.h"
#include "Binding.h"
#include "Context.h"
#include "Common.h"

ENUM_FLAG_TYPE(DebugFlag) GetDebugFlag()
{
#if SHADER_DEBUG
    return mConstants.mDebugFlag;
#else
    return DebugFlag::None;
#endif // SHADER_DEBUG
}

bool IsDebugCoord(PixelContext inPixelContext) 
{ 
    return inPixelContext.mPixelIndex.x == mConstants.mPixelDebugCoord.x && inPixelContext.mPixelIndex.y == mConstants.mPixelDebugCoord.y; 
}

namespace InspectPixel
{
    void Update(DebugMode inDebugMode, PathContext inPathContext, float3 inValue, bool inPrev = false)
    {
#if SHADER_DEBUG
        USING_RESOURCE(RWStructuredBuffer<PixelInspection>, PixelInspectionUAV);

        uint recursion_depth = inPrev ? inPathContext.mRecursionDepth : (inPathContext.mRecursionDepth - 1);
        if (GetDebugMode() == inDebugMode)
        {
            if (sDebugDispatchRaysIndex.x == mConstants.mPixelDebugCoord.x && sDebugDispatchRaysIndex.y == mConstants.mPixelDebugCoord.y && recursion_depth < PixelInspection::kArraySize)
                PixelInspectionUAV[0].mPixelValueArray[recursion_depth] = float4(inValue, 1.0); // 1.0 indicate value is written

            if (GetDebugRecursion() == recursion_depth)
            {
                sDebugValue = float4(inValue, 1.0); // fill alpha to show on ImGui
                sDebugValueUpdated = true;
            }

            if (mConstants.mVisualizeMode == VisualizeMode::DebugValue)
                sVisualizeModeValue = inValue;
        }
#endif // SHADER_DEBUG
    }

    void Update(float3 inValue)
    {
#if SHADER_DEBUG
        sDebugValue = float4(inValue, 1.0); // fill alpha to show on ImGui
        sDebugValueUpdated = true;
#endif // SHADER_DEBUG
    }
}

namespace InspectRay
{
    bool AllowUpdate() { return (GetDebugFlag() & DebugFlag::UpdateInspectRay) != 0; }

    void Primary(PixelContext inPixelContext, PathContext inPathContext, RayDesc inRay)
    {
#if SHADER_DEBUG
        USING_RESOURCE(RWStructuredBuffer<RayInspection>, RayInspectionUAV);

        if (AllowUpdate() && IsDebugCoord(inPixelContext))
            RayInspectionUAV[0].mPositionWS[0] = float4(inRay.Origin + inRay.Direction * inRay.TMin, 1.0);
#endif // SHADER_DEBUG
    }

    void Hit(PixelContext inPixelContext, PathContext inPathContext, HitContext inHitContext)
    {
#if SHADER_DEBUG
        USING_RESOURCE(RWStructuredBuffer<RayInspection>, RayInspectionUAV);
        USING_RESOURCE(RWStructuredBuffer<PixelInspection>, PixelInspectionUAV);

        if (AllowUpdate() && IsDebugCoord(inPixelContext))
            RayInspectionUAV[0].mPositionWS[inPathContext.mRecursionDepth + 1] = float4(inHitContext.PositionWS(), 1.0);

        if (inPathContext.mRecursionDepth == 0 && IsDebugCoord(inPixelContext))
            PixelInspectionUAV[0].mPixelInstanceID = inHitContext.mInstanceID;

        InspectPixel::Update(DebugMode::PositionWS, inPathContext, float3(inHitContext.PositionWS()));
        InspectPixel::Update(DebugMode::DirectionWS, inPathContext, float3(inHitContext.DirectionWS()));
        InspectPixel::Update(DebugMode::InstanceID, inPathContext, float3(inHitContext.mInstanceID, 0.0, 0.0));
#endif // SHADER_DEBUG
    }

    void Miss(PixelContext inPixelContext, PathContext inPathContext, RayDesc inRay)
    {
#if SHADER_DEBUG
        USING_RESOURCE(RWStructuredBuffer<RayInspection>, RayInspectionUAV);
        USING_RESOURCE(RWStructuredBuffer<PixelInspection>, PixelInspectionUAV);

        const float kMissRayVisualizationLength = 64.0f;

        if (AllowUpdate() && IsDebugCoord(inPixelContext))
            RayInspectionUAV[0].mPositionWS[inPathContext.mRecursionDepth + 1] = float4(inRay.Origin + inRay.Direction * kMissRayVisualizationLength, 0.0);

        if (inPathContext.mRecursionDepth == 0 && IsDebugCoord(inPixelContext))
            PixelInspectionUAV[0].mPixelInstanceID = InvalidInstanceID;

        InspectPixel::Update(DebugMode::PositionWS, inPathContext, float3(inRay.Origin));
        InspectPixel::Update(DebugMode::DirectionWS, inPathContext, float3(inRay.Direction));
        InspectPixel::Update(DebugMode::InstanceID, inPathContext, float3(-1.0, 0.0, 0.0));
#endif // SHADER_DEBUG
    }

    void HitLight(PixelContext inPixelContext, PathContext inPathContext, float3 inPositionWS)
    {
#if SHADER_DEBUG
        USING_RESOURCE(RWStructuredBuffer<RayInspection>, RayInspectionUAV);

        if (AllowUpdate() && IsDebugCoord(inPixelContext))
            RayInspectionUAV[0].mLightPositionWS[inPathContext.mRecursionDepth + 1] = float4(inPositionWS, 1.0);
#endif // SHADER_DEBUG
    }

    void Clear(uint inIndex)
    {
        USING_RESOURCE(RWStructuredBuffer<RayInspection>, RayInspectionUAV);

        if (AllowUpdate() && inIndex < RayInspection::kArraySize)
        {
            // Initialize position as NaN to kill vertices those are not updated
            RayInspectionUAV[0].mPositionWS[inIndex] = sqrt(-1.0);
            RayInspectionUAV[0].mNormalWS[inIndex] = sqrt(-1.0);
            RayInspectionUAV[0].mLightPositionWS[inIndex] = sqrt(-1.0);
        }
    }
}
