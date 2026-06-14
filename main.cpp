#define DIRECTINPUT_VERSION     0x0800
#include <dinput.h>

#include "Input.h"

#include <Windows.h>
#include <cstdint>
#include <string>
#include <format>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cassert>
#include <dxgidebug.h>
#include <dxcapi.h>
#include <fstream>
#include <sstream>
#include <random>
#include <cmath>

#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
#include "externals/DirectXTex/DirectXTex.h"

#include "WinApp.h"
#include "DirectXCommon.h"
#include <wrl.h>
#include "SpriteCommon.h"
#include "Sprite.h"
#include "Math.h"
#include "TextureManager.h"
#include "Object3dCommon.h"
#include "Object3d.h"
#include "ModelManager.h"
#include "Skybox.h"

using namespace Microsoft::WRL;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);


#pragma comment(lib,"dxcompiler.lib")
#pragma comment(lib,"dxguid.lib")
#pragma comment(lib,"d3d12.lib")
#pragma comment(lib,"dxgi.lib")
#pragma comment(lib,"dinput8.lib")
#pragma comment(lib,"dxguid.lib")

//ウィンドウプロシージャ
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg,
	WPARAM wparam, LPARAM lparam) {
	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) {
		return true;
	}

	//メッセージに応じてゲーム固有の処理を行う
	switch (msg) {
		//ウィンドウが破棄された
	case WM_DESTROY:
		//OSに対して、アプリの終了を伝える
		PostQuitMessage(0);
		return 0;
	}

	//標準のメッセージ処理を行う
	return DefWindowProc(hwnd, msg, wparam, lparam);
}

void Log(const std::string& message) {
	OutputDebugStringA(message.c_str());
}

std::wstring ConvertString(const std::string& str) {
	if (str.empty()) {
		return std::wstring();
	}

	auto sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), NULL, 0
	);
	if (sizeNeeded == 0) {
		return std::wstring();
	}
	std::wstring result(sizeNeeded, 0);
	MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), &result[0], sizeNeeded);
	return result;
}

std::string ConverString(const std::wstring& str) {
	if (str.empty()) {
		return std::string();
	}


	auto sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), NULL, 0, NULL, NULL);
	if (sizeNeeded == 0) {
		return std::string();
	}
	std::string result(sizeNeeded, 0);
	WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), sizeNeeded, NULL, NULL);
	return result;
}

IDxcBlob* CompileShader(
	const std::wstring& filePath,
	const wchar_t* profile,
	IDxcUtils* dxcUtils,
	IDxcCompiler3* dxcCompiler,
	IDxcIncludeHandler* includeHandler
)

{
	Log(ConverString(std::format(L"Begin CompliteShader,path:{},profile:{}\n", filePath, profile)));
	IDxcBlobEncoding* shaderSource = nullptr;
	HRESULT hr = dxcUtils->LoadFile(filePath.c_str(), nullptr, &shaderSource);
	assert(SUCCEEDED(hr));
	DxcBuffer shaderSourceBuffer;
	shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
	shaderSourceBuffer.Size = shaderSource->GetBufferSize();
	shaderSourceBuffer.Encoding = DXC_CP_UTF8;

	LPCWSTR arguments[] = {
		filePath.c_str(),
		L"-E",L"main",
		L"-T",profile,
		L"-Zi",L"-Qembed_debug",
		L"-Od",
		L"-Zpr",
	};
	IDxcResult* shaderResult = nullptr;
	hr = dxcCompiler->Compile(
		&shaderSourceBuffer,
		arguments,
		_countof(arguments),
		includeHandler,
		IID_PPV_ARGS(&shaderResult)
	);
	assert(SUCCEEDED(hr));

	IDxcBlobUtf8* shaderError = nullptr;
	shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);
	if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
		Log(shaderError->GetStringPointer());
		assert(false);
	}

	IDxcBlob* shaderBlob = nullptr;
	hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
	assert(SUCCEEDED(hr));
	Log(ConverString(std::format(L"Compile Succeded, path:{}, profile:{}\n", filePath, profile)));
	shaderSource->Release();
	shaderResult->Release();
	return shaderBlob;

}

DirectX::ScratchImage LoadTexture(const std::string& filePath) {
	DirectX::ScratchImage image{};
	std::wstring filePathW = ConvertString(filePath);
	HRESULT hr = DirectX::LoadFromWICFile(filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);

	DirectX::ScratchImage mipImages{};
	hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::TEX_FILTER_SRGB, 0, mipImages);
	assert(SUCCEEDED(hr));

	return mipImages;
}

ID3D12Resource* CreateTextureResource(ID3D12Device* device, const DirectX::TexMetadata& metadata) {
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = UINT(metadata.width);
	resourceDesc.Height = UINT(metadata.height);
	resourceDesc.MipLevels = UINT16(metadata.mipLevels);
	resourceDesc.DepthOrArraySize = UINT16(metadata.arraySize);
	resourceDesc.Format = metadata.format;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION(metadata.dimension);

	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_CUSTOM;
	heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
	heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;

	ID3D12Resource* resource = nullptr;
	HRESULT hr = device->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&resource));
	assert(SUCCEEDED(hr));
	return resource;
}

void UploadTextureData(ID3D12Resource* texture, const DirectX::ScratchImage& mipImages) {

	const DirectX::TexMetadata& metadata = mipImages.GetMetadata();

	for (size_t mipLevel = 0; mipLevel < metadata.mipLevels; ++mipLevel) {
		const DirectX::Image* img = mipImages.GetImage(mipLevel, 0, 0);
		HRESULT hr = texture->WriteToSubresource(
			UINT(mipLevel),
			nullptr,
			img->pixels,
			UINT(img->rowPitch),
			UINT(img->slicePitch)
		);
		assert(SUCCEEDED(hr));
	}
}




// Windowsアプリでのエントリーポイント(main関数)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {

	WinApp* winApp = nullptr;

	winApp = new WinApp();
	winApp->Initialize();

	DirectXCommon* dxCommon = nullptr; 

	// DirectXの初期化
	dxCommon = new DirectXCommon(); 
	dxCommon->Initialize(winApp);        

	TextureManager::GetInstance()->SetDirectXCommon(dxCommon);

	TextureManager::GetInstance()->LoadTexture("resources/uvChecker.png");
	TextureManager::GetInstance()->LoadTexture("monsterBall.png");

	TextureManager::GetInstance()->Initialize();

	SpriteCommon* spriteCommon = nullptr;
	//スプライト共通部の初期化
	spriteCommon = new SpriteCommon;
	spriteCommon->Initialize(dxCommon);


	Object3dCommon* object3dCommon = nullptr;
	// 3Dオブジェクト共通部の初期化
	object3dCommon = new Object3dCommon();
	object3dCommon->Initialize(dxCommon);
	Skybox* skybox = new Skybox();

	const std::string kSkyboxTexturePath = "resources/rostock_laage_airport_4k.dds";

	skybox->Initialize(dxCommon, kSkyboxTexturePath); 

	uint32_t environmentTextureIndex =
		TextureManager::GetInstance()->GetTextureIndexByFilePath(kSkyboxTexturePath);



	////DXGIファクトリーの作成
	IDXGIFactory7* dxgiFactory = nullptr;
	////HRESULTはWindows刑のエラーコードであり、
	////関数が成功したかどうかSUCCEDEDマクロで判定できる
	HRESULT hr = CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory));

	ID3D12Device* device = nullptr;

	//ポインタ
	Input* input = nullptr;
	//入力の初期化
	input = new Input();
	input->Initialize(winApp);

	////dxCompilerを初期化
	IDxcUtils* dxcUtils = nullptr;
	IDxcCompiler3* dxcCompiler = nullptr;

	IDxcIncludeHandler* includeHandler = nullptr;

	hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
	assert(SUCCEEDED(hr));

	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
	assert(SUCCEEDED(hr));

	hr = dxcUtils->CreateDefaultIncludeHandler(&includeHandler);
	assert(SUCCEEDED(hr));

	// ==================================================
	// Object3d用 RootSignature
	// ==================================================
	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignatureForObject{};
	descriptionRootSignatureForObject.Flags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	// Texture用SRV
	D3D12_DESCRIPTOR_RANGE descriptorRangeForObjectTexture[1] = {};
	descriptorRangeForObjectTexture[0].BaseShaderRegister = 0;
	descriptorRangeForObjectTexture[0].NumDescriptors = 1;
	descriptorRangeForObjectTexture[0].RangeType =
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptorRangeForObjectTexture[0].OffsetInDescriptorsFromTableStart =
		D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// EnvironmentTexture用SRV
	D3D12_DESCRIPTOR_RANGE descriptorRangeForEnvironmentTexture[1] = {};
	descriptorRangeForEnvironmentTexture[0].BaseShaderRegister = 1; // t1
	descriptorRangeForEnvironmentTexture[0].NumDescriptors = 1;
	descriptorRangeForEnvironmentTexture[0].RangeType =
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptorRangeForEnvironmentTexture[0].OffsetInDescriptorsFromTableStart =
		D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// Object3d用RootParameter
	D3D12_ROOT_PARAMETER rootParametersForObject[5] = {};

	// Material用 b0
	rootParametersForObject[0].ParameterType =
		D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParametersForObject[0].ShaderVisibility =
		D3D12_SHADER_VISIBILITY_PIXEL;
	rootParametersForObject[0].Descriptor.ShaderRegister = 0;

	// Transform用 b0
	// Object3dでは今まで通りCBV
	rootParametersForObject[1].ParameterType =
		D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParametersForObject[1].ShaderVisibility =
		D3D12_SHADER_VISIBILITY_VERTEX;
	rootParametersForObject[1].Descriptor.ShaderRegister = 0;

	// Texture用 t0
	rootParametersForObject[2].ParameterType =
		D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParametersForObject[2].ShaderVisibility =
		D3D12_SHADER_VISIBILITY_PIXEL;
	rootParametersForObject[2].DescriptorTable.pDescriptorRanges =
		descriptorRangeForObjectTexture;
	rootParametersForObject[2].DescriptorTable.NumDescriptorRanges =
		_countof(descriptorRangeForObjectTexture);

	// Camera用 b1
	rootParametersForObject[3].ParameterType =
		D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParametersForObject[3].ShaderVisibility =
		D3D12_SHADER_VISIBILITY_PIXEL;
	rootParametersForObject[3].Descriptor.ShaderRegister = 1;

	// EnvironmentTexture用 t1
	rootParametersForObject[4].ParameterType =
		D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParametersForObject[4].ShaderVisibility =
		D3D12_SHADER_VISIBILITY_PIXEL;
	rootParametersForObject[4].DescriptorTable.pDescriptorRanges =
		descriptorRangeForEnvironmentTexture;
	rootParametersForObject[4].DescriptorTable.NumDescriptorRanges =
		_countof(descriptorRangeForEnvironmentTexture);


	descriptionRootSignatureForObject.pParameters =
		rootParametersForObject;
	descriptionRootSignatureForObject.NumParameters =
		_countof(rootParametersForObject);

	

	// ==================================================
    // Particle用 RootSignature
    // ==================================================
	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignatureForParticle{};
	descriptionRootSignatureForParticle.Flags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	// ParticleのTexture用SRV
	D3D12_DESCRIPTOR_RANGE descriptorRangeForParticleTexture[1] = {};
	descriptorRangeForParticleTexture[0].BaseShaderRegister = 0;
	descriptorRangeForParticleTexture[0].NumDescriptors = 1;
	descriptorRangeForParticleTexture[0].RangeType =
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptorRangeForParticleTexture[0].OffsetInDescriptorsFromTableStart =
		D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// Instancing用StructuredBuffer SRV
	D3D12_DESCRIPTOR_RANGE descriptorRangeForInstancing[1] = {};
	descriptorRangeForInstancing[0].BaseShaderRegister = 0;
	descriptorRangeForInstancing[0].NumDescriptors = 1;
	descriptorRangeForInstancing[0].RangeType =
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptorRangeForInstancing[0].OffsetInDescriptorsFromTableStart =
		D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// Particle用RootParameter
	D3D12_ROOT_PARAMETER rootParametersForParticle[3] = {};

	// Material用 b0
	rootParametersForParticle[0].ParameterType =
		D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParametersForParticle[0].ShaderVisibility =
		D3D12_SHADER_VISIBILITY_PIXEL;
	rootParametersForParticle[0].Descriptor.ShaderRegister = 0;

	// Instancing用 t0
	// ここがスライドで変更する場所
	rootParametersForParticle[1].ParameterType =
		D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParametersForParticle[1].ShaderVisibility =
		D3D12_SHADER_VISIBILITY_VERTEX;
	rootParametersForParticle[1].DescriptorTable.pDescriptorRanges =
		descriptorRangeForInstancing;
	rootParametersForParticle[1].DescriptorTable.NumDescriptorRanges =
		_countof(descriptorRangeForInstancing);

	// Texture用 t0
	rootParametersForParticle[2].ParameterType =
		D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParametersForParticle[2].ShaderVisibility =
		D3D12_SHADER_VISIBILITY_PIXEL;
	rootParametersForParticle[2].DescriptorTable.pDescriptorRanges =
		descriptorRangeForParticleTexture;
	rootParametersForParticle[2].DescriptorTable.NumDescriptorRanges =
		_countof(descriptorRangeForParticleTexture);

	descriptionRootSignatureForParticle.pParameters =
		rootParametersForParticle;
	descriptionRootSignatureForParticle.NumParameters =
		_countof(rootParametersForParticle);

	//Samplerの設定
	D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
	staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
	staticSamplers[0].ShaderRegister = 0;
	staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	descriptionRootSignatureForObject.pStaticSamplers = staticSamplers;
	descriptionRootSignatureForObject.NumStaticSamplers = _countof(staticSamplers);

	descriptionRootSignatureForParticle.pStaticSamplers = staticSamplers;
	descriptionRootSignatureForParticle.NumStaticSamplers = _countof(staticSamplers);


	// ==================================================
// Object3d用 RootSignature を作成
// ==================================================
	ID3DBlob* objectSignatureBlob = nullptr;
	ID3DBlob* objectErrorBlob = nullptr;

	hr = D3D12SerializeRootSignature(
		&descriptionRootSignatureForObject,
		D3D_ROOT_SIGNATURE_VERSION_1,
		&objectSignatureBlob,
		&objectErrorBlob
	);

	if (FAILED(hr)) {
		Log(reinterpret_cast<char*>(objectErrorBlob->GetBufferPointer()));
		assert(false);
	}

	ID3D12RootSignature* objectRootSignature = nullptr;
	hr = dxCommon->GetDevice()->CreateRootSignature(
		0,
		objectSignatureBlob->GetBufferPointer(),
		objectSignatureBlob->GetBufferSize(),
		IID_PPV_ARGS(&objectRootSignature)
	);
	assert(SUCCEEDED(hr));


	// ==================================================
	// Particle用 RootSignature を作成
	// ==================================================
	ID3DBlob* particleSignatureBlob = nullptr;
	ID3DBlob* particleErrorBlob = nullptr;

	hr = D3D12SerializeRootSignature(
		&descriptionRootSignatureForParticle,
		D3D_ROOT_SIGNATURE_VERSION_1,
		&particleSignatureBlob,
		&particleErrorBlob
	);

	if (FAILED(hr)) {
		Log(reinterpret_cast<char*>(particleErrorBlob->GetBufferPointer()));
		assert(false);
	}

	ID3D12RootSignature* particleRootSignature = nullptr;
	hr = dxCommon->GetDevice()->CreateRootSignature(
		0,
		particleSignatureBlob->GetBufferPointer(),
		particleSignatureBlob->GetBufferSize(),
		IID_PPV_ARGS(&particleRootSignature)
	);
	assert(SUCCEEDED(hr));

	// ==================================================
    // Camera用Resourceの作成
    // ==================================================
	struct CameraForGPU
	{
		Math::Vector3 worldPosition;
		float padding;
	};

	ComPtr<ID3D12Resource> cameraResource =
		dxCommon->CreateBufferResource(sizeof(CameraForGPU));

	CameraForGPU* cameraData = nullptr;

	cameraResource->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&cameraData)
	);

	// とりあえずカメラ位置
	cameraData->worldPosition = {
		0.0f,
		0.0f,
		-5.0f
	};

	cameraData->padding = 0.0f;

// ==================================================
// Instancing用Resourceの作成
// ==================================================

// インスタンス数
	const uint32_t kNumMaxInstance = 1;

	std::random_device seedGenerator;
	std::mt19937 randomEngine(seedGenerator());

	// Instancing用の変換行列データ
	struct ParticleForGPU
	{
		Math::Matrix4x4 WVP;
		Math::Matrix4x4 World;
		Math::Vector4 color;
	};

	struct Particle
	{
		Math::Transform transform;
		Math::Vector3 velocity;
		Math::Vector4 color;
		float lifeTime;
		float currentTime;
	};

	auto MakeNewParticle = [](std::mt19937& randomEngine) -> Particle
		{
			std::uniform_real_distribution<float> distAngle(0.0f, 6.28f);
			std::uniform_real_distribution<float> distSpeed(0.5f, 1.5f);
			std::uniform_real_distribution<float> distScaleY(0.4f, 1.5f);
			std::uniform_real_distribution<float> distTime(0.3f, 0.8f);

			Particle particle;

			float angle = distAngle(randomEngine);
			float speed = distSpeed(randomEngine);


			particle.transform.scale = {
	            0.25f,
	            0.25f,
	            0.25f
			};

			particle.transform.rotate = {
	            1.2f,
	            0.0f,
	            0.0f
			};

			particle.transform.translate = {
				0.0f,
				-0.4f,
				0.0f
			};

			particle.velocity = {
				0.0f,
				0.0f,
				0.0f
			};

			particle.color = {
				1.0f,
				1.0f,
				1.0f,
				1.0f
			};

			particle.lifeTime = distTime(randomEngine);
			particle.currentTime = 0.0f;

			return particle;
		};

	// Instancing用Resourceを作る
	ComPtr<ID3D12Resource> instancingResource =
		dxCommon->CreateBufferResource(sizeof(ParticleForGPU) * kNumMaxInstance);

	// 書き込むためのアドレスを取得
	ParticleForGPU* instancingData = nullptr;

	instancingResource->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&instancingData)
	);

	// 単位行列を書き込んでおく
	for (uint32_t index = 0; index < kNumMaxInstance; ++index) {
		instancingData[index].WVP = Math::MakeIdentity4x4();
		instancingData[index].World = Math::MakeIdentity4x4();
		instancingData[index].color = { 1.0f, 1.0f, 1.0f, 1.0f };
	}

	// ==================================================
    // Instancing用SRVの作成
    // ==================================================
	D3D12_SHADER_RESOURCE_VIEW_DESC instancingSrvDesc{};
	instancingSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
	instancingSrvDesc.Shader4ComponentMapping =
		D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	instancingSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	instancingSrvDesc.Buffer.FirstElement = 0;
	instancingSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	instancingSrvDesc.Buffer.NumElements = kNumMaxInstance;
	instancingSrvDesc.Buffer.StructureByteStride = sizeof(ParticleForGPU);

	const uint32_t kInstancingSrvIndex = 100;
	const uint32_t kParticleTextureSrvIndex = 101;

	// 今回はSRVヒープの3番をInstancing用に使う
	dxCommon->GetDevice()->CreateShaderResourceView(
		instancingResource.Get(),
		&instancingSrvDesc,
		dxCommon->GetSRVCPUDescriptorHandle(kInstancingSrvIndex)
	);


	// ==================================================
    // Particle配列の作成
    // ==================================================
	Particle particles[kNumMaxInstance];

	for (uint32_t index = 0; index < kNumMaxInstance; ++index) {
		particles[index] = MakeNewParticle(randomEngine);
	}
	D3D12_INPUT_ELEMENT_DESC inputElementDesc[2] = {};
	inputElementDesc[0].SemanticName = "POSITION";
	inputElementDesc[0].SemanticIndex = 0;
	inputElementDesc[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElementDesc[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElementDesc[1].SemanticName = "TEXCOORD";
	inputElementDesc[1].SemanticIndex = 0;
	inputElementDesc[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	inputElementDesc[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	D3D12_INPUT_LAYOUT_DESC inputLayOutDesc{};
	inputLayOutDesc.pInputElementDescs = inputElementDesc;
	inputLayOutDesc.NumElements = _countof(inputElementDesc);

	D3D12_BLEND_DESC blendDesc{};
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	blendDesc.RenderTarget[0].BlendEnable = TRUE;

	blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;

	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;


	D3D12_RASTERIZER_DESC rastrizeDesc{};
	rastrizeDesc.CullMode = D3D12_CULL_MODE_NONE;
	rastrizeDesc.FillMode = D3D12_FILL_MODE_SOLID;

	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	depthStencilDesc.DepthEnable = true;
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;


	// ==================================================
// Particle用 Shader をCompile
// ==================================================
	IDxcBlob* particleVertexShaderBlob = CompileShader(
		L"resources/shaders/Particle.VS.hlsl",
		L"vs_6_0",
		dxcUtils,
		dxcCompiler,
		includeHandler
	);
	assert(particleVertexShaderBlob != nullptr);

	IDxcBlob* particlePixelShaderBlob = CompileShader(
		L"resources/shaders/Particle.PS.hlsl",
		L"ps_6_0",
		dxcUtils,
		dxcCompiler,
		includeHandler
	);
	assert(particlePixelShaderBlob != nullptr);


	// ==================================================
	// Particle用 PipelineState を作成
	// ==================================================
	D3D12_GRAPHICS_PIPELINE_STATE_DESC particleGraphicsPipelineStateDesc{};
	particleGraphicsPipelineStateDesc.pRootSignature = particleRootSignature;
	particleGraphicsPipelineStateDesc.InputLayout = inputLayOutDesc;
	particleGraphicsPipelineStateDesc.VS = {
		particleVertexShaderBlob->GetBufferPointer(),
		particleVertexShaderBlob->GetBufferSize()
	};
	particleGraphicsPipelineStateDesc.PS = {
		particlePixelShaderBlob->GetBufferPointer(),
		particlePixelShaderBlob->GetBufferSize()
	};
	particleGraphicsPipelineStateDesc.BlendState = blendDesc;
	particleGraphicsPipelineStateDesc.RasterizerState = rastrizeDesc;
	particleGraphicsPipelineStateDesc.DepthStencilState = depthStencilDesc;
	particleGraphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	particleGraphicsPipelineStateDesc.NumRenderTargets = 1;
	particleGraphicsPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	particleGraphicsPipelineStateDesc.PrimitiveTopologyType =
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	particleGraphicsPipelineStateDesc.SampleDesc.Count = 1;
	particleGraphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	ID3D12PipelineState* particleGraphicsPipelineState = nullptr;
	hr = dxCommon->GetDevice()->CreateGraphicsPipelineState(
		&particleGraphicsPipelineStateDesc,
		IID_PPV_ARGS(&particleGraphicsPipelineState)
	);
	assert(SUCCEEDED(hr));



	// ==================================================
    // Particle用 頂点データ
    // ==================================================
	struct ParticleVertexData
	{
		Math::Vector4 position;
		Math::Vector2 texcoord;
	};

	const uint32_t kRingDivide = 32;
	const float kOuterRadius = 1.0f;
	const float kInnerRadius = 0.9f;
	const float radianPerDivide = 2.0f * 3.14159265f / float(kRingDivide);

	const uint32_t kCylinderDivide = 32;
	const float kTopRadius = 1.0f;
	const float kBottomRadius = 1.0f;
	const float kHeight = 1.5f;
	const float cylinderRadianPerDivide =
		2.0f * 3.14159265f / float(kCylinderDivide);

	ComPtr<ID3D12Resource> ringVertexResource =
		dxCommon->CreateBufferResource(sizeof(ParticleVertexData) * kRingDivide * 6);

	D3D12_VERTEX_BUFFER_VIEW ringVertexBufferView{};
	ringVertexBufferView.BufferLocation =
		ringVertexResource->GetGPUVirtualAddress();
	ringVertexBufferView.SizeInBytes =
		sizeof(ParticleVertexData) * kRingDivide * 6;
	ringVertexBufferView.StrideInBytes =
		sizeof(ParticleVertexData);

	ParticleVertexData* ringVertexData = nullptr;
	ringVertexResource->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&ringVertexData)
	);

	for (uint32_t index = 0; index < kRingDivide; ++index) {

		float sin = std::sin(index * radianPerDivide);
		float cos = std::cos(index * radianPerDivide);

		float sinNext = std::sin((index + 1) * radianPerDivide);
		float cosNext = std::cos((index + 1) * radianPerDivide);

		float u = float(index) / float(kRingDivide);
		float uNext = float(index + 1) / float(kRingDivide);

		uint32_t vertexIndex = index * 6;

		// 1枚目の三角形
		ringVertexData[vertexIndex + 0].position = {
			sin * kOuterRadius,
			cos * kOuterRadius,
			0.0f,
			1.0f
		};
		ringVertexData[vertexIndex + 0].texcoord = { u, 0.0f };

		ringVertexData[vertexIndex + 1].position = {
			sinNext * kOuterRadius,
			cosNext * kOuterRadius,
			0.0f,
			1.0f
		};
		ringVertexData[vertexIndex + 1].texcoord = { uNext, 0.0f };

		ringVertexData[vertexIndex + 2].position = {
			sin * kInnerRadius,
			cos * kInnerRadius,
			0.0f,
			1.0f
		};
		ringVertexData[vertexIndex + 2].texcoord = { u, 1.0f };

		// 2枚目の三角形
		ringVertexData[vertexIndex + 3].position = {
			sinNext * kOuterRadius,
			cosNext * kOuterRadius,
			0.0f,
			1.0f
		};
		ringVertexData[vertexIndex + 3].texcoord = { uNext, 0.0f };

		ringVertexData[vertexIndex + 4].position = {
			sinNext * kInnerRadius,
			cosNext * kInnerRadius,
			0.0f,
			1.0f
		};
		ringVertexData[vertexIndex + 4].texcoord = { uNext, 1.0f };

		ringVertexData[vertexIndex + 5].position = {
			sin * kInnerRadius,
			cos * kInnerRadius,
			0.0f,
			1.0f
		};
		ringVertexData[vertexIndex + 5].texcoord = { u, 1.0f };
	}

	// ==================================================
    // Plane用 頂点データ
    // ==================================================
	ComPtr<ID3D12Resource> planeVertexResource =
		dxCommon->CreateBufferResource(sizeof(ParticleVertexData) * 6);

	D3D12_VERTEX_BUFFER_VIEW planeVertexBufferView{};
	planeVertexBufferView.BufferLocation =
		planeVertexResource->GetGPUVirtualAddress();
	planeVertexBufferView.SizeInBytes =
		sizeof(ParticleVertexData) * 6;
	planeVertexBufferView.StrideInBytes =
		sizeof(ParticleVertexData);

	ParticleVertexData* planeVertexData = nullptr;
	planeVertexResource->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&planeVertexData)
	);

	// 細長いPlane
	planeVertexData[0].position = { -0.03f, -0.7f, 0.0f, 1.0f };
	planeVertexData[0].texcoord = { 0.0f, 1.0f };

	planeVertexData[1].position = { -0.03f,  0.7f, 0.0f, 1.0f };
	planeVertexData[1].texcoord = { 0.0f, 0.0f };

	planeVertexData[2].position = { 0.03f, -0.7f, 0.0f, 1.0f };
	planeVertexData[2].texcoord = { 1.0f, 1.0f };

	planeVertexData[3].position = { -0.03f,  0.7f, 0.0f, 1.0f };
	planeVertexData[3].texcoord = { 0.0f, 0.0f };

	planeVertexData[4].position = { 0.03f,  0.7f, 0.0f, 1.0f };
	planeVertexData[4].texcoord = { 1.0f, 0.0f };

	planeVertexData[5].position = { 0.03f, -0.7f, 0.0f, 1.0f };
	planeVertexData[5].texcoord = { 1.0f, 1.0f };


	// ==================================================
    // Cylinder用 頂点データ
    // ==================================================
	ComPtr<ID3D12Resource> cylinderVertexResource =
		dxCommon->CreateBufferResource(sizeof(ParticleVertexData) * kCylinderDivide * 6);

	D3D12_VERTEX_BUFFER_VIEW cylinderVertexBufferView{};
	cylinderVertexBufferView.BufferLocation =
		cylinderVertexResource->GetGPUVirtualAddress();
	cylinderVertexBufferView.SizeInBytes =
		sizeof(ParticleVertexData) * kCylinderDivide * 6;
	cylinderVertexBufferView.StrideInBytes =
		sizeof(ParticleVertexData);

	ParticleVertexData* cylinderVertexData = nullptr;
	cylinderVertexResource->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&cylinderVertexData)
	);

	for (uint32_t index = 0; index < kCylinderDivide; ++index) {

		float sin = std::sin(index * cylinderRadianPerDivide);
		float cos = std::cos(index * cylinderRadianPerDivide);

		float sinNext = std::sin((index + 1) * cylinderRadianPerDivide);
		float cosNext = std::cos((index + 1) * cylinderRadianPerDivide);

		float u = float(index) / float(kCylinderDivide);
		float uNext = float(index + 1) / float(kCylinderDivide);

		uint32_t vertexIndex = index * 6;

		// 1枚目の三角形
		cylinderVertexData[vertexIndex + 0].position = {
			-sin * kTopRadius,
			kHeight,
			cos * kTopRadius,
			1.0f
		};
		cylinderVertexData[vertexIndex + 0].texcoord = { u, 0.0f };

		cylinderVertexData[vertexIndex + 1].position = {
			-sinNext * kTopRadius,
			kHeight,
			cosNext * kTopRadius,
			1.0f
		};
		cylinderVertexData[vertexIndex + 1].texcoord = { uNext, 0.0f };

		cylinderVertexData[vertexIndex + 2].position = {
			-sin * kBottomRadius,
			0.0f,
			cos * kBottomRadius,
			1.0f
		};
		cylinderVertexData[vertexIndex + 2].texcoord = { u, 1.0f };

		// 2枚目の三角形
		cylinderVertexData[vertexIndex + 3].position = {
			-sinNext * kTopRadius,
			kHeight,
			cosNext * kTopRadius,
			1.0f
		};
		cylinderVertexData[vertexIndex + 3].texcoord = { uNext, 0.0f };

		cylinderVertexData[vertexIndex + 4].position = {
			-sinNext * kBottomRadius,
			0.0f,
			cosNext * kBottomRadius,
			1.0f
		};
		cylinderVertexData[vertexIndex + 4].texcoord = { uNext, 1.0f };

		cylinderVertexData[vertexIndex + 5].position = {
			-sin * kBottomRadius,
			0.0f,
			cos * kBottomRadius,
			1.0f
		};
		cylinderVertexData[vertexIndex + 5].texcoord = { u, 1.0f };
	}


	// ==================================================
    // Particle用 MaterialResource
    // ==================================================
	struct ParticleMaterial
	{
		Math::Vector4 color;
		Math::Matrix4x4 uvTransform;
	};

	ComPtr<ID3D12Resource> particleMaterialResource =
		dxCommon->CreateBufferResource(sizeof(ParticleMaterial));

	ParticleMaterial* particleMaterialData = nullptr;
	particleMaterialResource->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&particleMaterialData)
	);

	particleMaterialData->color = { 0.2f, 0.5f, 1.0f, 1.0f };
	particleMaterialData->uvTransform = Math::MakeIdentity4x4();


	D3D12_HEAP_PROPERTIES uploadHeapProperties{};
	uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

	DirectX::ScratchImage mipImages = LoadTexture("resources/gradationLine.png");

	ComPtr<ID3D12Resource> textureResource = dxCommon->CreateTextureResource(mipImages.GetMetadata());

	//UploadTextureData(textureResource, mipImages);
	dxCommon->UploadTextureData(textureResource, mipImages);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	//srvDesc.Format = metadata.format;
	srvDesc.Format = mipImages.GetMetadata().format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	//srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);
	srvDesc.Texture2D.MipLevels = UINT(mipImages.GetMetadata().mipLevels);

	dxCommon->GetDevice()->CreateShaderResourceView(
		textureResource.Get(), // ComPtrなので .Get()
		&srvDesc,
		dxCommon->GetSRVCPUDescriptorHandle(kParticleTextureSrvIndex) // 0番目のSRVヒープスロットを使用すると仮定
	);

	//ID3D12Resource* indexResourceSprite = CreateBufferResource(device, sizeof(uint32_t) * 6);
	ComPtr<ID3D12Resource> indexResourceSprite = dxCommon->CreateBufferResource(sizeof(uint32_t) * 6);

	uint32_t* indexDataSprite = nullptr;
	indexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&indexDataSprite));
	indexDataSprite[0] = 0; indexDataSprite[1] = 1; indexDataSprite[2] = 2;
	indexDataSprite[3] = 1; indexDataSprite[4] = 3; indexDataSprite[5] = 2;


	// 1. まず、使いたい画像のファイル名をリストにします
	std::vector<std::string> texFiles = {
		"resources/uvChecker.png",
		"monsterBall.png", // ← スライドで使っている別の画像のパスに変えてください
		"resources/uvChecker.png",
		"monsterBall.png",
		"resources/uvChecker.png"
	};

	std::vector<Sprite*> sprites;

	for (uint32_t i = 0; i < 5; ++i) {
		Sprite* sprite = new Sprite();

		// 2. ループの番号 (i) に応じて、違うファイル名を取り出して渡します
		// texFiles[i] を使うのがポイントです！
		sprite->Initialize(spriteCommon, texFiles[i]);

		float x_position = 100.0f + 150.0f * i;
		sprite->SetPosition({ x_position, 100.0f });
		sprite->SetSize({ 128.0f, 128.0f }); // 見やすいサイズに設定

		sprites.push_back(sprite);
	}	

	Sprite* sprite = new Sprite();
	// Initialize()に SpriteCommon などの必要な情報を渡す想定
	sprite->Initialize(spriteCommon, "resources/uvChecker.png");
	
	// 1. DirectXCommonの初期化のあと、TextureManagerなどの後が良いです
	ModelManager::GetInstance()->Initialize(dxCommon);

	// 2. ここでモデルをロードします（ファイルパスを指定）
	// これにより、ModelManager内の map にモデルデータが保管されます
	ModelManager::GetInstance()->LoadModel("plane.obj");
	// 他のモデルを使う場合はここに追加します
    ModelManager::GetInstance()->LoadModel("axis.obj");

	//Object3d* object3d = new Object3d();
	//object3d->Initialize(object3dCommon);

	// 1体目のオブジェクト（左側）
	Object3d* object3d_1 = new Object3d();
	object3d_1->Initialize(object3dCommon);
	object3d_1->SetModel("plane.obj"); // 新しく作った文字列版の SetModel を使用
	object3d_1->SetTranslate({ -2.0f, 0.0f, 0.0f }); // 少し左にずらす
	object3d_1->SetRotate({ 0.0f, 3.14f, 0.0f });

	// 2体目のオブジェクト（右側）
	Object3d* object3d_2 = new Object3d();
	object3d_2->Initialize(object3dCommon);
	object3d_2->SetModel("axis.obj"); // 同じモデルデータを使い回す！
	object3d_2->SetTranslate({ 2.0f, 0.0f, 0.0f }); // 少し右にずらす
	object3d_2->SetRotate({ 0.0f, 0.0f, 0.0f });

	//deltaTimeの設定
	const float kDeltaTime = 1.0f / 60.0f;

	//ウィンドウの×ボタンが押されるまでループ
	while (true) {

		// 変更: WinApp::ProcessMessage() にメッセージ処理を任せる
		// ProcessMessage() が true を返したら WM_QUIT が来たということ
		if (winApp->ProcessMessage()) {
			break; // ゲームループを抜ける
		} 
		{
			//入力の更新  <= ❌ 初期化時に一度だけ呼ばれている
			input->Update();


			static float uvOffsetX = 0.0f;
			uvOffsetX += 0.01f;

			particleMaterialData->uvTransform = Math::MakeIdentity4x4();
			particleMaterialData->uvTransform.m[3][0] = uvOffsetX;

			Math::Matrix4x4 viewProjectionMatrix = Math::MakeIdentity4x4();

			uint32_t numInstance = 0;

			for (uint32_t index = 0; index < kNumMaxInstance; ++index) {

				// Cylinder確認中は消えないようにする
				//// 経過時間を進める
				//particles[index].currentTime += kDeltaTime;

				//// 寿命を過ぎたParticleは描画しない
				//if (particles[index].currentTime >= particles[index].lifeTime) {
				//	continue;
				//}

				// 速度を位置に反映
				particles[index].transform.translate.x +=
					particles[index].velocity.x * kDeltaTime;

				particles[index].transform.translate.y +=
					particles[index].velocity.y * kDeltaTime;

				particles[index].transform.translate.z +=
					particles[index].velocity.z * kDeltaTime;

				Math::Matrix4x4 worldMatrix =
					Math::MakeAffineMatrix(
						particles[index].transform.scale,
						particles[index].transform.rotate,
						particles[index].transform.translate
					);

				Math::Matrix4x4 worldViewProjectionMatrix =
					Math::Multiply(worldMatrix, viewProjectionMatrix);

				//float alpha = 1.0f - (particles[index].currentTime / particles[index].lifeTime);

				instancingData[numInstance].WVP = worldViewProjectionMatrix;
				instancingData[numInstance].World = worldMatrix;
				instancingData[numInstance].color = particles[index].color;
				instancingData[numInstance].color.w = 1.0f;

				// 生きているParticle数を数える
				++numInstance;
			}



			//object3d->Update();

			// 更新
			object3d_1->Update();
			object3d_2->Update();

			if (input->Pushkey(DIK_0)) {
				OutputDebugStringA("Hit 0\n");
			}

			// [NEW] Sprite::Update() を呼び出す
			sprite->Update();

			// スプライトの更新
            // vector の要素を一つずつ取り出す
			for (Sprite* sprite : sprites) {

				// 例: 特定のスプライトのみを動かす、または共通のロジック
				// sprite->SetRotation(sprite->GetRotation() + 0.01f);

				// 座標変換行列の再計算
				sprite->Update();
			}

			ImGui_ImplDX12_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();

			//開発用UIの処理
			ImGui::ShowDemoWindow();

			ImGui::Begin("Settings");
		
			// 1. 現在の位置を取得
			Math::Vector2 currentPos = sprite->GetPosition();
			Math::Vector3 currentRot = sprite->GetRotation();
			
			Math::Vector2 currentSize = sprite->GetSize();

			// 2. DragFloat2 で編集（ここでは Vector3ではなく Vector2 が適切）
			// ImGui::DragFloat2 を使って位置を編集します。
			if (ImGui::DragFloat2("Position", &currentPos.x, 1.0f))
			{
				// 3. 編集された値を Setter で設定
				sprite->SetPosition(currentPos);
			}


			// --- Rotation (Vector3) ---
			if (ImGui::DragFloat3("Rotation", &currentRot.x, 0.01f)) {
				// [FIX] Vector3 の変数を Setter に渡す
				sprite->SetRotation(currentRot);
			}

			// [NEW] Color の Getter/Setter を使った ImGui ウィジェットを追加
			Math::Vector4 currentColor = sprite->GetColor();

			// DragFloat4 ではなく ColorEdit4 を使うことで、カラーピッカーが表示される
			if (ImGui::ColorEdit4("Color", &currentColor.x, ImGuiColorEditFlags_AlphaPreview)) {
				// 編集された値を Setter で設定
				sprite->SetColor(currentColor);
			}

			if (ImGui::DragFloat2("Size", &currentSize.x, 0.1f, 0.1f, 1000.0f)) // 1.0f 単位で、最小 1.0f までの制限
			{
				sprite->SetSize(currentSize);
			}

			ImGui::Text("--- 3D Object ---");

			// 現在の3Dオブジェクトの位置を取得
			Math::Vector3 planeobjPos = object3d_1->GetTranslate();

			Math::Vector3 axisobjPos = object3d_2->GetTranslate();

			// DragFloat3 を使って、X, Y, Z の3つの値を操作できるようにする
			if (ImGui::DragFloat3("Planeobj Position", &planeobjPos.x, 0.1f)) {
				// 操作されたら、新しい位置をセットし直す
				object3d_1->SetTranslate(planeobjPos);
			}
			// 回転の操作
			Math::Vector3 planeobjRot = object3d_1->GetRotate();
			if (ImGui::DragFloat3("Planeobj Rotation", &planeobjRot.x, 0.01f)) {
				object3d_1->SetRotate(planeobjRot);
			}

			if (ImGui::DragFloat3("axisobj Position", &axisobjPos.x, 0.1f)) {
				// 操作されたら、新しい位置をセットし直す
				object3d_2->SetTranslate(axisobjPos);
			}
			Math::Vector3 axisobjRot = object3d_1->GetRotate();
			if (ImGui::DragFloat3("axisobj Rotation", &axisobjRot.x, 0.01f)) {
				object3d_2->SetRotate(axisobjRot);
			}

			ImGui::End();

			//ImGuiの内部コマンド生成
			ImGui::Render();

			// --- 描画前処理 ---
			dxCommon->PreDraw();

			// 3Dオブジェクトの描画準備。3Dオブジェクトの描画に共通のグラフィックスコマンドを積む
			object3dCommon->SetCommonDrawSetting();

			//object3d->Draw();

		    // Camera用 b1 / rootParameter[3]
			dxCommon->GetCommandList()->SetGraphicsRootConstantBufferView(
				3,
				cameraResource->GetGPUVirtualAddress()
			);

			dxCommon->GetCommandList()->SetGraphicsRootDescriptorTable(
				4,
				TextureManager::GetInstance()->GetSrvHandleGPU(environmentTextureIndex)
			);

			//左
			object3d_1->Draw();
			//右
			object3d_2->Draw();

			//dxCommon->GetCommandList()->SetPipelineState(object3dCommon->GetGraphicsPipelineStateSkybox());

			// 2. 立方体の描画コマンドを呼び出す
			//skybox->Draw(dxCommon->GetCommandList());

			// Todo: 全てのObject3d個々の描画


			// --------------------------------------------------------------------------------------
			// 【スプライト描画の準備】
			// --------------------------------------------------------------------------------------
			// スプライト描画の共通設定を積む
			spriteCommon->SetCommonDrawSettings(dxCommon->GetCommandList());

			// [NEW] Sprite::Draw() を呼び出し、個別の描画コマンドを積む
			//sprite->Draw(dxCommon->GetCommandList());
			//for (Sprite* sprite : sprites) {
			//	sprite->Draw(dxCommon->GetCommandList());
			//}


			ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();

			// Particle用RootSignature
			commandList->SetGraphicsRootSignature(particleRootSignature);

			// Particle用PipelineState
			commandList->SetPipelineState(particleGraphicsPipelineState);

			// 頂点バッファ
			commandList->IASetVertexBuffers(0, 1, &ringVertexBufferView);

			// 三角形リスト
			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			// Material b0
			commandList->SetGraphicsRootConstantBufferView(
				0,
				particleMaterialResource->GetGPUVirtualAddress()
			);

			// Instancing用SRV t0 / rootParameter[1]
			commandList->SetGraphicsRootDescriptorTable(
				1,
				dxCommon->GetSRVGPUDescriptorHandle(kInstancingSrvIndex)
			);

			// Texture用SRV t0 / rootParameter[2]
			commandList->SetGraphicsRootDescriptorTable(
				2,
				dxCommon->GetSRVGPUDescriptorHandle(kParticleTextureSrvIndex)
			);

			// リング描画
			//commandList->DrawInstanced(
			//	kRingDivide * 6,
			//	numInstance,
			//	0,
			//	0
			//);

			// Plane描画
			//commandList->IASetVertexBuffers(0, 1, &planeVertexBufferView);

			//commandList->DrawInstanced(
			//	6,
			//	numInstance,
			//	0,
			//	0
			//);

			// Cylinder描画
			commandList->IASetVertexBuffers(0, 1, &cylinderVertexBufferView);

			commandList->DrawInstanced(
				kCylinderDivide * 6,
				numInstance,
				0,
				0
			);


			ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), dxCommon->GetCommandList());

			dxCommon->PostDraw();
		}

	}

	//変数から型を推測する

	Log(ConverString(std::format(L"WSTRING{}\n", L"abc")));



	//出力ウィンドウへの文字出力
	OutputDebugStringA("Hello,DirectX!\n");

	//delete object3d;

	delete object3d_1;
	delete object3d_2;

	delete sprite;

	delete input;


	

	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	if (objectSignatureBlob) {
		objectSignatureBlob->Release();
	}
	if (objectErrorBlob) {
		objectErrorBlob->Release();
	}

	if (particleSignatureBlob) {
		particleSignatureBlob->Release();
	}
	if (particleErrorBlob) {
		particleErrorBlob->Release();
	}

	if (objectRootSignature) {
		objectRootSignature->Release();
	}
	if (particleRootSignature) {
		particleRootSignature->Release();
	}

	if (particleVertexShaderBlob) {
		particleVertexShaderBlob->Release();
	}
	if (particlePixelShaderBlob) {
		particlePixelShaderBlob->Release();
	}
	if (particleGraphicsPipelineState) {
		particleGraphicsPipelineState->Release();
	}

	// スプライトの解放
	for (Sprite* sprite : sprites) {
		if (sprite) {
			delete sprite; // newしたオブジェクトを解放
		}
	}

	delete object3dCommon;

	ModelManager::GetInstance()->Finalize();

	//windowsAPIの終了処理
	winApp->Finalize();

	TextureManager::GetInstance()->Finalize();

	delete dxCommon;

	delete winApp;
	winApp = nullptr;

	
	return 0;
}
