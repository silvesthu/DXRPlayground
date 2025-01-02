
#include "Shared.h"
#include "Binding.h"
#include "Common.h"
#include "BSDF.h"
#include "Light.h"
#include "Reservoir.h"

bool FindTask(uint3 inDispatchThreadID, out PrepareLightsTask task)
{
	// [TODO] Is binary search the best way?
	
	// Use binary search to find the task that contains the current thread's output index:
	//   task.mLightBufferOffset <= inDispatchThreadID.x < (task.mLightBufferOffset + task.mTriangleCount)
	int left = 0;
	int right = int(mNumTasks) - 1;

	while (right >= left)
	{
		int middle = (left + right) / 2;
		task = TaskBufferSRV[middle];

		int tri = int(inDispatchThreadID.x) - int(task.mLightBufferOffset); // signed

		if (tri < 0)
		{
			// Go left
			right = middle - 1;
		}
		else if (tri < task.mTriangleCount)
		{
			// Found it!
			return true;
		}
		else
		{
			// Go right
			left = middle + 1;
		}
	}

	return false;
}

[RootSignature(ROOT_SIGNATURE_PREPARE_LIGHTS)]
[numthreads(256, 1, 1)]
void PrepareLightsCS(
	uint3 inGroupThreadID : SV_GroupThreadID,
	uint3 inGroupID : SV_GroupID,
	uint3 inDispatchThreadID : SV_DispatchThreadID,
	uint inGroupIndex : SV_GroupIndex)
{
	PrepareLightsTask task = (PrepareLightsTask)0;

	if (!FindTask(inDispatchThreadID, task))
        return;

    uint light_index						= inDispatchThreadID.x - task.mLightBufferOffset;    
    RAB_LightInfo lightInfo					= (RAB_LightInfo)0;
 
	{
		SurfaceContext surface_context		= (SurfaceContext)0;
		surface_context.mInstanceID			= task.mInstanceIndex;
		surface_context.mPrimitiveIndex		= light_index;
		surface_context.mBarycentrics		= 1.0 / 3.0; // use barycenter

		surface_context.LoadVertex();
		
		float3 positions[3];
		positions[0] 						= mul(surface_context.InstanceData().mTransform, float4(surface_context.mVertexPositions[0], 1)).xyz;
		positions[1] 						= mul(surface_context.InstanceData().mTransform, float4(surface_context.mVertexPositions[1], 1)).xyz;
		positions[2] 						= mul(surface_context.InstanceData().mTransform, float4(surface_context.mVertexPositions[2], 1)).xyz;
 
		TriangleLight triLight;
		triLight.mBase = positions[0];
		triLight.mEdge1 = positions[1] - positions[0];
		triLight.mEdge2 = positions[2] - positions[0];
		triLight.mRadiance = surface_context.Emission();
 
		lightInfo = triLight.Store();
	}
	// [NOTE] else for analytical light

	LightDataBufferUAV[task.mLightBufferOffset + light_index] = lightInfo;
}