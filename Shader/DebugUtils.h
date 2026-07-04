#pragma once

#include "Shared.h"
#include "Binding.h"
#include "Common.h"
#include "Context.h"
#include "Reservoir.h"
#include "SpatialCache.h"
#include "ShaderPrint.h"

namespace Visualize
{
    void Hit(inout PathContext inPathContext, HitContext inHitContext, inout bool ioContinueBounce)
    {
#if SHADER_DEBUG
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
#endif // SHADER_DEBUG
    }
}

namespace Inspect
{
    static bool sActive = false;
    static bool sUpdatePath = false;
    static USING_RESOURCE(RWStructuredBuffer<InspectData>, InspectDataUAV);

    InspectMode Mode()
    {
#if SHADER_DEBUG
        return mConstants.mInspectMode;
#else
        return InspectMode::Manual;
#endif // SHADER_DEBUG
    }

    bool UpdatePath() { return (GetDebugFlag() & DebugFlag::UpdateInspectRay) != 0; }

    void Initialize(PixelContext inPixelContext)
    {
#if SHADER_DEBUG
        sActive = IsDebugCoord(inPixelContext.mPixelIndex);
        sUpdatePath = sActive && UpdatePath();
#endif // SHADER_DEBUG
    }

    void RayPrimary(RayDesc inRay)
    {
        if (sUpdatePath)
            InspectDataUAV[0].mPositionWS[0] = float4(inRay.Origin + inRay.Direction * inRay.TMin, 1.0);
    }

    void Update(InspectMode inDebugMode, PathContext inPathContext, float3 inValue, bool inPrev = false)
    {
        if (Mode() == inDebugMode)
        {
            uint recursion_depth = inPrev ? (inPathContext.mRecursionDepth - 1) : inPathContext.mRecursionDepth;
            if (sActive && recursion_depth < InspectData::kPathLength)
                InspectDataUAV[0].mValue[recursion_depth] = float4(inValue, 1.0); // 1.0 indicate value is written

            if (mConstants.mVisualizeMode == VisualizeMode::Inspect)
                sVisualizeModeValue = inValue;
        }
    }

    void Hit(PathContext inPathContext, HitContext inHitContext)
    {
        if (sActive && inPathContext.mRecursionDepth == 0)
            InspectDataUAV[0].mPixelInstanceID = inHitContext.mInstanceID;

        Update(InspectMode::PositionWS,         inPathContext, float3(inHitContext.PositionWS()));
        Update(InspectMode::DirectionWS,        inPathContext, float3(inHitContext.DirectionWS()));
        Update(InspectMode::InstanceID_BSDF,    inPathContext, float3(inHitContext.mInstanceID, (uint)inHitContext.BSDF(), inHitContext.BSDF() == BSDF::Light ? Inf() : 0));

        if (sUpdatePath)
            InspectDataUAV[0].mPositionWS[inPathContext.mRecursionDepth + 1] = float4(inHitContext.PositionWS(), 1.0);
    }

    void HitLight(PathContext inPathContext, float3 inPositionWS)
    {
        if (sUpdatePath)
            InspectDataUAV[0].mLightPositionWS[inPathContext.mRecursionDepth + 1] = float4(inPositionWS, 1.0);
    }

    void DGF(PathContext inPathContext, BSDFContext inBSDFContext, float D, float G, float3 F)
    {
        if (inBSDFContext.mMode == BSDFContext::Mode::BSDF)
        {
            Update(InspectMode::BSDF__D,        inPathContext, float3(D, 0, 0));
            Update(InspectMode::BSDF__G,        inPathContext, float3(G, 0, 0));
            Update(InspectMode::BSDF__F,        inPathContext, float3(F));
        }
        else
        {
            Update(InspectMode::Light_D,        inPathContext, float3(D, 0, 0));
            Update(InspectMode::Light_G,        inPathContext, float3(G, 0, 0));
            Update(InspectMode::Light_F,        inPathContext, float3(F));
        }
    }

    void BSDF(PathContext inPathContext, BSDFContext inBSDFContext)
    {
        if (inBSDFContext.mMode == BSDFContext::Mode::BSDF)
		{
			Update(InspectMode::BSDF__L,		inPathContext, float3(inBSDFContext.mL));
			Update(InspectMode::BSDF__V,		inPathContext, float3(inBSDFContext.mV));
			Update(InspectMode::BSDF__N,		inPathContext, float3(inBSDFContext.mN));
			Update(InspectMode::BSDF__H,		inPathContext, float3(inBSDFContext.mH));
			Update(InspectMode::BSDF__Lobe,	    inPathContext, float3(inBSDFContext.mLobeIndex, 0, 0));
		}
		else
		{
			Update(InspectMode::Light_L,		inPathContext, float3(inBSDFContext.mL));
			Update(InspectMode::Light_V,		inPathContext, float3(inBSDFContext.mV));
			Update(InspectMode::Light_N,		inPathContext, float3(inBSDFContext.mN));
			Update(InspectMode::Light_H,		inPathContext, float3(inBSDFContext.mH));
			Update(InspectMode::Light_Lobe,	    inPathContext, float3(inBSDFContext.mLobeIndex, 0, 0));
		}
    }

    void ReSTIRInitialize(PathContext inPathContext, Reservoir inReservoir)
    {
        Update(InspectMode::ReSTIR_Initial,     inPathContext, float3(inReservoir.LightIndex(), inReservoir.mContributionWeight, inReservoir.mUV));
    }

    void ReSTIRTemporal(PathContext inPathContext, Reservoir inReservoir)
    {
        Update(InspectMode::ReSTIR_Temporal,    inPathContext, float3(inReservoir.LightIndex(), inReservoir.mContributionWeight, inReservoir.mUV));
    }

    void ReSTIRSpatial(PathContext inPathContext, Reservoir inReservoir)
    {
        Update(InspectMode::ReSTIR_Spatial,       inPathContext, float3(inReservoir.LightIndex(), inReservoir.mContributionWeight, inReservoir.mUV));
    }

    void SampleLight(PathContext inPathContext, LightContext inLightContext)
    {
        Update(InspectMode::Light_Index_UV,     inPathContext, float3(inLightContext.mLightIndex, inLightContext.mUV));
    }

    void SampleLightResult(PathContext inPathContext, BSDFContext inBSDFContext, BSDFResult inBSDFResult, float inLightPDF)
    {
        Update(InspectMode::Light_BSDF,         inPathContext, float3(inBSDFResult.mBSDF));
        Update(InspectMode::Light_PDF,          inPathContext, float3(inLightPDF, 0, 0));
    }

    void SampleBSDFResult(PathContext inPathContext, HitContext inHitContext, BSDFContext inBSDFContext, BSDFResult inBSDFResult)
    {
        Update(InspectMode::BSDF__BSDF,         inPathContext, float3(inBSDFResult.mBSDF));
        Update(InspectMode::BSDF__PDF,          inPathContext, float3(inBSDFResult.mBSDFSamplePDF, 0, 0));

        Update(InspectMode::Eta,                inPathContext, float3(inBSDFResult.mEta, 0, 0));
        Update(InspectMode::DiracDelta,         inPathContext, float3(inHitContext.DiracDeltaDistribution(), 0, 0));
    }

    void Miss(PathContext inPathContext, RayDesc inRay)
    {
        if (sActive && inPathContext.mRecursionDepth == 0)
            InspectDataUAV[0].mPixelInstanceID = InvalidInstanceID;

        Update(InspectMode::PositionWS,         inPathContext, float3(inRay.Origin));
        Update(InspectMode::DirectionWS,        inPathContext, float3(inRay.Direction));
        Update(InspectMode::InstanceID_BSDF,    inPathContext, QNaN());

        const float kMissRayVisualizationLength = 64.0f;
        if (sUpdatePath)
            InspectDataUAV[0].mPositionWS[inPathContext.mRecursionDepth + 1] = float4(inRay.Origin + inRay.Direction * kMissRayVisualizationLength, 0.0);
    }

    void Manual(PathContext inPathContext, float3 inValue)
    {
        Update(InspectMode::Manual,             inPathContext, inValue);
    }

    void Clear(uint inPathVertexIndex)
    {
        if (inPathVertexIndex == 0)
        {
            InspectDataUAV[0].mScreenColor                              = 0;
            InspectDataUAV[0].mScreenDebug                              = 0;
            InspectDataUAV[0].mPixelInstanceID                          = -1;
        }

        if (inPathVertexIndex < InspectData::kPathLength)
        {
            InspectDataUAV[0].mValue[inPathVertexIndex]                 = 0;
            
            if (UpdatePath())
            {
                // Initialize position as NaN to kill vertices those are not updated
                InspectDataUAV[0].mPositionWS[inPathVertexIndex]        = sqrt(-1.0);
                InspectDataUAV[0].mNormalWS[inPathVertexIndex]          = sqrt(-1.0);
                InspectDataUAV[0].mLightPositionWS[inPathVertexIndex]   = sqrt(-1.0);
            }
        }
    }
}
