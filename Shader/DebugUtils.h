#pragma once

#include "Shared.h"
#include "Binding.h"
#include "Common.h"
#include "Context.h"
#include "SpatialCache.h"
#include "ShaderPrint.h"

namespace Visualize
{
    void Hit(inout PathContext inPathContext, HitContext inHitContext, inout bool ioContinueBounce)
    {
        switch (GetVisualizeMode())
        {
        case VisualizeMode::None:							break;
        case VisualizeMode::PrimitiveIndex:					inPathContext.mEmission = IntToColor((inHitContext.mInstanceID << 16) + inHitContext.mPrimitiveIndex + 1 /* skip 0 = black */); ioContinueBounce = false; break;
        case VisualizeMode::ClusterID:						inPathContext.mEmission = IntToColor((inHitContext.mInstanceID << 16) + inHitContext.mClusterID + 1 /* skip 0 = black */); ioContinueBounce = false; break;
        case VisualizeMode::Barycentrics: 					inPathContext.mEmission = inHitContext.Barycentrics(); ioContinueBounce = false; break;
        case VisualizeMode::Position: 						inPathContext.mEmission = inHitContext.PositionWS(); ioContinueBounce = false; break;
        case VisualizeMode::Normal: 						inPathContext.mEmission = inHitContext.NormalWS(); ioContinueBounce = false; break;
        case VisualizeMode::UV:								inPathContext.mEmission = float3(inHitContext.UV(), 0.0); ioContinueBounce = false; break;
        case VisualizeMode::Albedo: 						inPathContext.mEmission = inHitContext.Albedo(); ioContinueBounce = false; break;
        case VisualizeMode::Reflectance: 					inPathContext.mEmission = inHitContext.SpecularReflectance(); ioContinueBounce = false; break;
        case VisualizeMode::Emission: 						inPathContext.mEmission = inHitContext.Emission(); ioContinueBounce = false; break;
        case VisualizeMode::RoughnessAlpha:					inPathContext.mEmission = inHitContext.RoughnessAlpha(); ioContinueBounce = false; break;
        case VisualizeMode::RecursionDepth:					ioContinueBounce = true; break;
        case VisualizeMode::RandomState:					ioContinueBounce = inPathContext.mRecursionDepth <= GetDebugRecursion(); break;
        case VisualizeMode::SpatialHash:					inPathContext.mEmission = SpatialCache::HashGridGetColorFromHash32(SpatialCache::FindOrInsert(inHitContext.PositionWS(), 0, SpatialCache::kCellSize)); ioContinueBounce = false; break;
        case VisualizeMode::SpatialData:					inPathContext.mEmission = SpatialCache::LoadData(SpatialCache::FindOrInsert(inHitContext.PositionWS(), 0, SpatialCache::kCellSize)) / 1024.0; ioContinueBounce = false; break;
        default:											inPathContext.mEmission = sVisualizeModeValue; ioContinueBounce = false; break;
        }
    }
}

namespace InspectPixel
{
    static bool sWritePixel = false;

    InspectPixelMode Mode()
    {
#if SHADER_DEBUG
        return mConstants.mInspectPixelMode;
#else
        return InspectPixelMode::Manual;
#endif // SHADER_DEBUG
    }

    void Initialize(PixelContext inPixelContext)
    {
#if SHADER_DEBUG
        USING_RESOURCE(RWStructuredBuffer<PixelInspection>, PixelInspectionUAV);

        sWritePixel = IsDebugCoord(inPixelContext.mPixelIndex);
        if (sWritePixel)
            for (int i = 0; i < PixelInspection::kArraySize; i++)
                PixelInspectionUAV[0].mPixelValueArray[i] = 0;
#endif // SHADER_DEBUG
    }

    void Update(InspectPixelMode inDebugMode, PathContext inPathContext, float3 inValue, bool inPrev = false)
    {
#if SHADER_DEBUG
        USING_RESOURCE(RWStructuredBuffer<PixelInspection>, PixelInspectionUAV);

        uint recursion_depth = inPrev ? (inPathContext.mRecursionDepth - 1) : inPathContext.mRecursionDepth;
        if (Mode() == inDebugMode)
        {
            if (sWritePixel && recursion_depth < PixelInspection::kArraySize)
                PixelInspectionUAV[0].mPixelValueArray[recursion_depth] = float4(inValue, 1.0); // 1.0 indicate value is written

            if (mConstants.mVisualizeMode == VisualizeMode::DebugValue)
                sVisualizeModeValue = inValue;
        }
#endif // SHADER_DEBUG
    }

    void Hit(PathContext inPathContext, HitContext inHitContext)
    {
#if SHADER_DEBUG
        USING_RESOURCE(RWStructuredBuffer<PixelInspection>, PixelInspectionUAV);

        if (sWritePixel && inPathContext.mRecursionDepth == 0)
            PixelInspectionUAV[0].mPixelInstanceID = inHitContext.mInstanceID;

        Update(InspectPixelMode::PositionWS, inPathContext, float3(inHitContext.PositionWS()));
        Update(InspectPixelMode::DirectionWS, inPathContext, float3(inHitContext.DirectionWS()));
        Update(InspectPixelMode::InstanceID, inPathContext, float3(inHitContext.mInstanceID, 0.0, 0.0));
#endif // SHADER_DEBUG
    }

    void DGF(PathContext inPathContext, BSDFContext inBSDFContext, float D, float G, float3 F)
    {
        if (inBSDFContext.mMode == BSDFContext::Mode::BSDF)
        {
            Update(InspectPixelMode::BSDF__D, inPathContext, float3(D, 0, 0));
            Update(InspectPixelMode::BSDF__G, inPathContext, float3(G, 0, 0));
            Update(InspectPixelMode::BSDF__F, inPathContext, float3(F));
        }
        else
        {
            Update(InspectPixelMode::Light_D, inPathContext, float3(D, 0, 0));
            Update(InspectPixelMode::Light_G, inPathContext, float3(G, 0, 0));
            Update(InspectPixelMode::Light_F, inPathContext, float3(F));
        }
    }

    void SampleLight(PathContext inPathContext, LightContext inLightContext)
    {
#if SHADER_DEBUG
        Update(InspectPixelMode::LightIndex, inPathContext, float3(inLightContext.LightIndex(), 0.0, 0.0));
        Update(InspectPixelMode::RIS_SAMPLE, inPathContext, float3(inLightContext.mReservoir.mTargetPDF, 0.0, 0.0));
        Update(InspectPixelMode::RIS_SUM, inPathContext, float3(inLightContext.mReservoir.mWeightSum, inLightContext.mReservoir.mCountSum, 0.0));
#endif // SHADER_DEBUG
    }

    void SampleLightDone(PathContext inPathContext, BSDFContext inBSDFContext, BSDFResult inBSDFResult, float inLightPDF)
    {
#if SHADER_DEBUG
        Update(InspectPixelMode::Light_L,       inPathContext, float3(inBSDFContext.mL));
        Update(InspectPixelMode::Light_V,       inPathContext, float3(inBSDFContext.mV));
        Update(InspectPixelMode::Light_N,       inPathContext, float3(inBSDFContext.mN));
        Update(InspectPixelMode::Light_H,       inPathContext, float3(inBSDFContext.mH));
        Update(InspectPixelMode::Light_Lobe,    inPathContext, float3(inBSDFContext.mLobeIndex, 0, 0));

        Update(InspectPixelMode::Light_BSDF, inPathContext, float3(inBSDFResult.mBSDF));
        Update(InspectPixelMode::Light_PDF, inPathContext, float3(inLightPDF, 0, 0));
#endif // SHADER_DEBUG
    }

    void SampleBSDF(PathContext inPathContext, HitContext inHitContext, BSDFContext inBSDFContext, BSDFResult inBSDFResult)
    {
#if SHADER_DEBUG
        Update(InspectPixelMode::BSDF__BSDF, inPathContext, float3(inBSDFResult.mBSDF));
        Update(InspectPixelMode::BSDF__PDF, inPathContext, float3(inBSDFResult.mBSDFSamplePDF, 0, 0));

        Update(InspectPixelMode::DiracDelta, inPathContext, float3(inHitContext.DiracDeltaDistribution(), 0, 0));
#endif // SHADER_DEBUG
    }

    void Miss(PathContext inPathContext, RayDesc inRay)
    {
#if SHADER_DEBUG
        USING_RESOURCE(RWStructuredBuffer<PixelInspection>, PixelInspectionUAV);

        if (sWritePixel && inPathContext.mRecursionDepth == 0)
            PixelInspectionUAV[0].mPixelInstanceID = InvalidInstanceID;

        Update(InspectPixelMode::PositionWS, inPathContext, float3(inRay.Origin));
        Update(InspectPixelMode::DirectionWS, inPathContext, float3(inRay.Direction));
        Update(InspectPixelMode::InstanceID, inPathContext, float3(-1.0, 0.0, 0.0));
#endif // SHADER_DEBUG
    }

    void Manual(PathContext inPathContext, float3 inValue)
    {
#if SHADER_DEBUG
        Update(InspectPixelMode::Manual, inPathContext, inValue);
#endif // SHADER_DEBUG
    }
}

namespace InspectRay
{
    static bool sUpdatePixel = false;
	static bool sUpdate = false;

    bool UpdateRequested() { return (GetDebugFlag() & DebugFlag::UpdateInspectRay) != 0; }

    void Initialize(PixelContext inPixelContext, RayDesc inRay)
    {
#if SHADER_DEBUG
        USING_RESOURCE(RWStructuredBuffer<RayInspection>, RayInspectionUAV);

        sUpdate = UpdateRequested() && IsDebugCoord(inPixelContext.mPixelIndex);
        if (sUpdate)
            RayInspectionUAV[0].mPositionWS[0] = float4(inRay.Origin + inRay.Direction * inRay.TMin, 1.0);
#endif // SHADER_DEBUG
    }

    void Hit(PathContext inPathContext, HitContext inHitContext)
    {
#if SHADER_DEBUG
        USING_RESOURCE(RWStructuredBuffer<RayInspection>, RayInspectionUAV);

        if (sUpdate)
            RayInspectionUAV[0].mPositionWS[inPathContext.mRecursionDepth + 1] = float4(inHitContext.PositionWS(), 1.0);
#endif // SHADER_DEBUG
    }

    void Miss(PathContext inPathContext, RayDesc inRay)
    {
#if SHADER_DEBUG
        USING_RESOURCE(RWStructuredBuffer<RayInspection>, RayInspectionUAV);

        const float kMissRayVisualizationLength = 64.0f;
        if (sUpdate)
            RayInspectionUAV[0].mPositionWS[inPathContext.mRecursionDepth + 1] = float4(inRay.Origin + inRay.Direction * kMissRayVisualizationLength, 0.0);
#endif // SHADER_DEBUG
    }

    void HitLight(PathContext inPathContext, float3 inPositionWS)
    {
#if SHADER_DEBUG
        USING_RESOURCE(RWStructuredBuffer<RayInspection>, RayInspectionUAV);

        if (sUpdate)
            RayInspectionUAV[0].mLightPositionWS[inPathContext.mRecursionDepth + 1] = float4(inPositionWS, 1.0);
#endif // SHADER_DEBUG
    }

    void Clear(uint inIndex)
    {
#if SHADER_DEBUG
        USING_RESOURCE(RWStructuredBuffer<RayInspection>, RayInspectionUAV);

        if (UpdateRequested() && inIndex < RayInspection::kArraySize)
        {
            // Initialize position as NaN to kill vertices those are not updated
            RayInspectionUAV[0].mPositionWS[inIndex] = sqrt(-1.0);
            RayInspectionUAV[0].mNormalWS[inIndex] = sqrt(-1.0);
            RayInspectionUAV[0].mLightPositionWS[inIndex] = sqrt(-1.0);
        }
#endif // SHADER_DEBUG
    }
}
