#include "Scene.h"
#include "AccelerationStructure.h"
#include "PipelineState.h"
#include "ShaderResource.h"
#include "ShaderTable.h"

void gCreateScene()
{
	gCreateVertexBuffer();
	gCreateBottomLevelAccelerationStructure();
	gCreateTopLevelAccelerationStructure();

	gExecuteAccelerationStructureCreation();

	gCreatePipelineState();
	gCreateShaderResource();
	gCreateShaderTable();
}

void gCleanupScene()
{
	gCleanupShaderResource();
	gCleanupShaderTable();
	gCleanupPipelineState();

	gCleanupTopLevelAccelerationStructure();
	gCleanupBottomLevelAccelerationStructure();
	gCleanupVertexBuffer();
}

void gUpdateScene()
{
	gUpdateTopLevelAccelerationStructure();
}

void gRebuildBinding(std::function<void()> inCallback)
{
	gCleanupShaderTable();
	gCleanupShaderResource();

	if (inCallback)
		inCallback();

	gCreateShaderResource();
	gCreateShaderTable();
}