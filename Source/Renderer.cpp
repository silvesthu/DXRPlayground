#include "Renderer.h"
#include "Atmosphere.h"
#include "Cloud.h"

#include <fstream>
#include <sstream>

static IDxcBlob* sCompileShader(const char* inFilename, const char* inEntryPoint, const char* inProfile)
{
	// Load shader
	std::ifstream shader_file(inFilename);
	std::stringstream shader_stream;
	shader_stream << shader_file.rdbuf();
	std::string shader_string = shader_stream.str();

	// LoadLibraryW + GetProcAddress to eliminate dependency on .lib. Make updating .dll easier.
	HMODULE dll = LoadLibraryW(L"dxcompiler.dll");
	if (dll == NULL)
		return nullptr;
	DxcCreateInstanceProc DxcCreateInstance = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(dll, "DxcCreateInstance"));

	// See https://simoncoenen.com/blog/programming/graphics/DxcRevised.html
	ComPtr<IDxcUtils> utils;
	DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(utils.GetAddressOf()));

	IDxcBlobEncoding* blob_encoding;
	gValidate(utils->CreateBlobFromPinned(shader_string.c_str(), static_cast<glm::uint32>(shader_string.length()), CP_UTF8, &blob_encoding));

	ComPtr<IDxcCompiler> compiler;
	DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(compiler.GetAddressOf()));

	ComPtr<IDxcIncludeHandler> include_handler;
	utils->CreateDefaultIncludeHandler(include_handler.GetAddressOf());

	std::string filename(inFilename);
	std::wstring wfilename(filename.begin(), filename.end());

	std::vector<DxcDefine> defines;
	std::wstring profile = gToWString(inProfile);
	DxcDefine dxc_define_profile {};
	dxc_define_profile.Name = L"SHADER_PROFILE_UNKNOWN";
	dxc_define_profile.Value = L"1";
	if (profile._Starts_with(L"lib"))
		dxc_define_profile.Name = L"SHADER_PROFILE_LIB";
	if (profile._Starts_with(L"cs"))
		dxc_define_profile.Name = L"SHADER_PROFILE_CS";
	if (profile._Starts_with(L"ps"))
		dxc_define_profile.Name = L"SHADER_PROFILE_PS";
	if (profile._Starts_with(L"vs"))
		dxc_define_profile.Name = L"SHADER_PROFILE_VS";
	defines.push_back(dxc_define_profile);

	std::wstring entry_point = gToWString(inEntryPoint);
	std::wstring entry_point_macro = L"ENTRY_POINT_";
	entry_point_macro += entry_point;
	DxcDefine dxc_define_entry_point {};
	dxc_define_entry_point.Name = entry_point_macro.c_str();
	defines.push_back(dxc_define_entry_point);

	std::vector<LPCWSTR> arguments;
	arguments.push_back(DXC_ARG_WARNINGS_ARE_ERRORS);				// -WX
	arguments.push_back(DXC_ARG_ALL_RESOURCES_BOUND);				// -all_resources_bound
	arguments.push_back(DXC_ARG_DEBUG);								// -Zi
	arguments.push_back(L"-Qembed_debug");							// -Qembed_debug

	IDxcOperationResult* operation_result;
	if (FAILED(compiler->Compile(
		blob_encoding,												// program text
		wfilename.c_str(),											// file name, mostly for error messages
		entry_point.c_str(),										// entry point function
		profile.c_str(),											// target profile
		arguments.data(), static_cast<UINT32>(arguments.size()),	// compilation arguments and their count
		defines.data(), static_cast<UINT32>(defines.size()),		// name/value defines and their count
		include_handler.Get(),										// handler for #include directives
		&operation_result)))
		assert(false);

	HRESULT compile_result;
	gValidate(operation_result->GetStatus(&compile_result));

	if (FAILED(compile_result))
	{
		IDxcBlobEncoding* blob = nullptr;
		IDxcBlobUtf8* blob_8 = nullptr;
		gValidate(operation_result->GetErrorBuffer(&blob));
		// We can use the library to get our preferred encoding.
		gValidate(utils->GetBlobAsUtf8(blob, &blob_8));
		std::string str((char*)blob_8->GetBufferPointer(), blob_8->GetBufferSize() - 1);
		gTrace(str.c_str());
		blob->Release();
		blob_8->Release();
		return nullptr;
	}

	IDxcBlob* blob = nullptr;
	gValidate(operation_result->GetResult(&blob));

	if (gRenderer.mDumpDisassemblyRayQuery && std::string_view("RayQueryCS") == inEntryPoint)
	{
		IDxcBlob* blob_to_dissemble = blob;
		IDxcBlobEncoding* disassemble = nullptr;
		IDxcBlobUtf8* blob_8 = nullptr;
		compiler->Disassemble(blob_to_dissemble, &disassemble);
		gValidate(utils->GetBlobAsUtf8(disassemble, &blob_8));
		std::string str((char*)blob_8->GetBufferPointer(), blob_8->GetBufferSize() - 1);

		static int counter = 0;
		std::filesystem::path path = gCreateDumpFolder();
		path += "RayQueryCS_";
		path += std::to_string(counter++);
		path += ".txt";
		std::ofstream stream(path);
		stream << str;
		stream.close();

		gRenderer.mDumpDisassemblyRayQuery = false;
	}

	return blob;
}

static bool sCreateVSPSPipelineState(const char* inShaderFileName, const char* inVSName, const char* inPSName, Shader& ioSystemShader)
{
	IDxcBlob* vs_blob = sCompileShader(inShaderFileName, inVSName, "vs_6_6");
	IDxcBlob* ps_blob = sCompileShader(inShaderFileName, inPSName, "ps_6_6");
	if (vs_blob == nullptr || ps_blob == nullptr)
		return false;

	if (FAILED(gDevice->CreateRootSignature(0, ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), IID_PPV_ARGS(&ioSystemShader.mData.mRootSignature))))
		return false;

	D3D12_RASTERIZER_DESC rasterizer_desc = {};
	rasterizer_desc.FillMode = D3D12_FILL_MODE_SOLID;
	rasterizer_desc.CullMode = D3D12_CULL_MODE_NONE;

	D3D12_BLEND_DESC blend_desc = {};
	blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_state_desc = {};
	pipeline_state_desc.VS.pShaderBytecode = vs_blob->GetBufferPointer();
	pipeline_state_desc.VS.BytecodeLength = vs_blob->GetBufferSize();
	pipeline_state_desc.PS.pShaderBytecode = ps_blob->GetBufferPointer();
	pipeline_state_desc.PS.BytecodeLength = ps_blob->GetBufferSize();
	pipeline_state_desc.pRootSignature = ioSystemShader.mData.mRootSignature.Get();
	pipeline_state_desc.RasterizerState = rasterizer_desc;
	pipeline_state_desc.BlendState = blend_desc;
	pipeline_state_desc.DepthStencilState.DepthEnable = FALSE;
	pipeline_state_desc.DepthStencilState.StencilEnable = FALSE;
	pipeline_state_desc.SampleMask = UINT_MAX;
	pipeline_state_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pipeline_state_desc.NumRenderTargets = 1;
	pipeline_state_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	pipeline_state_desc.SampleDesc.Count = 1;
	
	if (FAILED(gDevice->CreateGraphicsPipelineState(&pipeline_state_desc, IID_PPV_ARGS(&ioSystemShader.mData.mPipelineState))))
		return false;

	std::wstring name = gToWString(inVSName) + L"_" + gToWString(inPSName);
	ioSystemShader.mData.mPipelineState->SetName(name.c_str());

	return true;
}

static bool sCreateCSPipelineState(const char* inShaderFileName, const char* inCSName, Shader& ioSystemShader)
{
	IDxcBlob* cs_blob = sCompileShader(inShaderFileName, inCSName, "cs_6_6");
	if (cs_blob == nullptr)
		return false;

	LPVOID root_signature_pointer = cs_blob->GetBufferPointer();
	SIZE_T root_signature_size = cs_blob->GetBufferSize();

	if (FAILED(gDevice->CreateRootSignature(0, root_signature_pointer, root_signature_size, IID_PPV_ARGS(&ioSystemShader.mData.mRootSignature))))
		return false;

	D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_state_desc = {};
	pipeline_state_desc.CS.pShaderBytecode = cs_blob->GetBufferPointer();
	pipeline_state_desc.CS.BytecodeLength = cs_blob->GetBufferSize();
	pipeline_state_desc.pRootSignature = ioSystemShader.mData.mRootSignature.Get();
	if (FAILED(gDevice->CreateComputePipelineState(&pipeline_state_desc, IID_PPV_ARGS(&ioSystemShader.mData.mPipelineState))))
		return false;

	std::wstring name = gToWString(inCSName);
	ioSystemShader.mData.mPipelineState->SetName(name.c_str());

	return true;
}

static bool sCreatePipelineState(Shader& ioSystemShader)
{
	if (ioSystemShader.mCSName != nullptr)
		return sCreateCSPipelineState(ioSystemShader.mFileName, ioSystemShader.mCSName, ioSystemShader);
	else
		return sCreateVSPSPipelineState(ioSystemShader.mFileName, ioSystemShader.mVSName, ioSystemShader.mPSName, ioSystemShader);
}

void Renderer::Initialize()
{
	InitializeScreenSizeTextures();
	InitializeShaders();
}

void Renderer::Finalize()
{
	FinalizeScreenSizeTextures();

	mRuntime.Reset();
}

void Renderer::ImGuiShowTextures()
{
	ImGui::Textures(mRuntime.mTextures, "Renderer", ImGuiTreeNodeFlags_None);
}

void Renderer::InitializeScreenSizeTextures()
{
	for (int i = 0; i < NUM_BACK_BUFFERS; i++)
	{
		gSwapChain->GetBuffer(i, IID_PPV_ARGS(mRuntime.mBackBuffers[i].GetAddressOf()));
		std::wstring name = L"BackBuffer_" + std::to_wstring(i);
		mRuntime.mBackBuffers[i]->SetName(name.c_str());
		gDevice->CreateRenderTargetView(mRuntime.mBackBuffers[i].Get(), nullptr, mRuntime.mBufferBufferRTVs[i]);
	}

	DXGI_SWAP_CHAIN_DESC1 swap_chain_desc;
	gSwapChain->GetDesc1(&swap_chain_desc);

	gRenderer.mRuntime.mScreenColorTexture.Width(swap_chain_desc.Width).Height(swap_chain_desc.Height).Initialize();
	gRenderer.mRuntime.mScreenDebugTexture.Width(swap_chain_desc.Width).Height(swap_chain_desc.Height).Initialize();
}

void Renderer::FinalizeScreenSizeTextures()
{
	for (int i = 0; i < NUM_BACK_BUFFERS; i++)
		mRuntime.mBackBuffers[i] = nullptr;
}

void Renderer::InitializeShaders()
{
	sCreatePipelineState(gRenderer.mRuntime.mCompositeShader);
	sCreatePipelineState(gRenderer.mRuntime.mRayQueryShader);
	sCreatePipelineState(gRenderer.mRuntime.mDiffTexture2DShader);
	sCreatePipelineState(gRenderer.mRuntime.mDiffTexture3DShader);

	for (auto&& shaders : gAtmosphere.mRuntime.mShadersSet)
		for (auto&& shader : shaders)
			sCreatePipelineState(shader);

	for (auto&& shader : gCloud.mRuntime.mShaders)
		sCreatePipelineState(shader);
}

void Renderer::FinalizeShaders()
{
	// No actual cleanup in case rebuild fails
}

Renderer gRenderer;