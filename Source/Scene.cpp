#include "Scene.h"

#include "Common.h"
#include "AccelerationStructure.h"
#include "PipelineState.h"
#include "ShaderResource.h"
#include "ShaderTable.h"

#include "Thirdparty/tinyobjloader/tiny_obj_loader.h"

void gCreateScene()
{
	std::string inputfile = "Asset/raytracing-references/cornellbox/cornellbox.obj";
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;

	std::string warn;
	std::string err;
	bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, inputfile.c_str());

	if (!warn.empty())
		gDebugPrint(warn.c_str());

	if (!err.empty())
		gDebugPrint(err.c_str());

	gDebugPrint(ret);

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