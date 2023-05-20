#include "ParticleSystem.h"

#include "RenderSystem.h"
#include "GameObject.h"
#include "TransformComponent.h"
#include "CameraComponent.h"
#include "GBuffer.h"

#include <iostream>
#include <random>
#include <ctime>

float RandomFloat(float min, float max)
{
	return ((float)rand() / RAND_MAX) * (max - min) + min;
}

float RandomInt(int min, int max)
{
	return min + (rand() % static_cast<int>(max - min + 1));
}

template<typename T>
std::vector<D3D_SHADER_MACRO> GetMacros(T flags)
{
	std::vector<D3D_SHADER_MACRO> macros;
	constexpr auto& entries = magic_enum::enum_entries<T>();
	for (const std::pair<T, std::string_view>& p : entries)
	{
		if (static_cast<uint32_t>(flags & p.first) > 0)
		{
			D3D_SHADER_MACRO macro;
			macro.Name = p.second.data();
			macro.Definition = "1";
			macros.push_back(macro);
		}
	}
	macros.push_back(D3D_SHADER_MACRO{ nullptr, nullptr });
	return macros;
}

ParticleSystem::ParticleSystem()
{
	Width    = 100;
	Height   = 100;
	Length   = 100;
	Position = Vector3(0.0f, 0.0f, 0.0f);
}

void ParticleSystem::Initialize()
{
	LoadShaders("../Shaders/ParticlesRender.hlsl"); // ������� �������
	CreateBuffers();                                // ������� ������

	D3D11_RASTERIZER_DESC rastDesc = {};
	rastDesc.CullMode = D3D11_CULL_BACK;
	rastDesc.FillMode = D3D11_FILL_SOLID;
	auto result = Game::GetInstance()->GetRenderSystem()->device->CreateRasterizerState(&rastDesc, &rastState);
	assert(SUCCEEDED(result));

	auto blendStateDesc = D3D11_BLEND_DESC{false, false};
	blendStateDesc.RenderTarget[0].BlendEnable           = TRUE;
	blendStateDesc.RenderTarget[0].RenderTargetWriteMask = 0x0f;
	blendStateDesc.RenderTarget[0].BlendOp               = D3D11_BLEND_OP_ADD;
	blendStateDesc.RenderTarget[0].BlendOpAlpha          = D3D11_BLEND_OP_ADD;
	blendStateDesc.RenderTarget[0].SrcBlend              = D3D11_BLEND_SRC_ALPHA;
	blendStateDesc.RenderTarget[0].DestBlend             = D3D11_BLEND_ONE;
	blendStateDesc.RenderTarget[0].SrcBlendAlpha         = D3D11_BLEND_ZERO;
	blendStateDesc.RenderTarget[0].DestBlendAlpha        = D3D11_BLEND_ONE;
	result = Game::GetInstance()->GetRenderSystem()->device->CreateBlendState(&blendStateDesc, &blendState);
	assert(SUCCEEDED(result));

	auto depthDesc = D3D11_DEPTH_STENCIL_DESC {};
	depthDesc.DepthEnable      = TRUE;
	depthDesc.StencilEnable    = FALSE;
	depthDesc.DepthFunc        = D3D11_COMPARISON_LESS;
	depthDesc.DepthWriteMask   = D3D11_DEPTH_WRITE_MASK_ZERO;
	depthDesc.StencilReadMask  = 0x00;
	depthDesc.StencilWriteMask = 0x00;
	result = Game::GetInstance()->GetRenderSystem()->device->CreateDepthStencilState(&depthDesc, &depthState);
	assert(SUCCEEDED(result));
}

void ParticleSystem::GetGroupSize(int partCount, int& groupSizeX, int& groupSizeY)
{
	int numGroups = (partCount % 256 != 0) ? ((partCount / 256) + 1) : (partCount / 256);
	double secondRoot = std::pow((double)numGroups, (double)(1.0f / 2.0f));
	secondRoot = std::ceil(secondRoot); // ��������� ���������� integer �������� �� ������ ��� secondRoot
	groupSizeX = (int)secondRoot;
	groupSizeY = groupSizeX;
}

void ParticleSystem::LoadShaders(std::string shaderFileName)
{
	std::wstring fileName(shaderFileName.begin(), shaderFileName.end());
	ID3DBlob* errorCode = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> vertexShaderByteCode;
	auto result = D3DCompileFromFile(
		fileName.c_str(),
		nullptr,
		nullptr,
		"VSMain",
		"vs_5_0",
		D3DCOMPILE_PACK_MATRIX_ROW_MAJOR | D3DCOMPILE_DEBUG,
		0,
		vertexShaderByteCode.GetAddressOf(),
		&errorCode
	);
	if (FAILED(result))
	{
		if (errorCode)
		{
			const char* compileErrors = (char*)(errorCode->GetBufferPointer());
			std::cout << compileErrors << std::endl;
		}
		else
		{
			std::cout << "Missing Shader File: " << std::endl;
		}
		return;
	}
	result = Game::GetInstance()->GetRenderSystem()->device->CreateVertexShader(
		vertexShaderByteCode->GetBufferPointer(),
		vertexShaderByteCode->GetBufferSize(),
		nullptr, &vertShader
	);
	assert(SUCCEEDED(result));

	Microsoft::WRL::ComPtr<ID3DBlob> geometryShaderByteCode;
	result = D3DCompileFromFile(
		fileName.c_str(),
		nullptr,
		nullptr,
		"GSMain",
		"gs_5_0",
		D3DCOMPILE_PACK_MATRIX_ROW_MAJOR | D3DCOMPILE_DEBUG,
		0,
		geometryShaderByteCode.GetAddressOf(),
		&errorCode
	);
	assert(SUCCEEDED(result));
	result = Game::GetInstance()->GetRenderSystem()->device->CreateGeometryShader(
		geometryShaderByteCode->GetBufferPointer(),
		geometryShaderByteCode->GetBufferSize(),
		nullptr, &geomShader
	);

	Microsoft::WRL::ComPtr<ID3DBlob> pixelShaderByteCode;
	result = D3DCompileFromFile(
		fileName.c_str(),
		nullptr,
		nullptr,
		"PSMain",
		"ps_5_0",
		D3DCOMPILE_PACK_MATRIX_ROW_MAJOR | D3DCOMPILE_DEBUG,
		0,
		pixelShaderByteCode.GetAddressOf(),
		&errorCode
	);
	assert(SUCCEEDED(result));
	result = Game::GetInstance()->GetRenderSystem()->device->CreatePixelShader(
		pixelShaderByteCode->GetBufferPointer(),
		pixelShaderByteCode->GetBufferSize(),
		nullptr, &pixShader
	);
	assert(SUCCEEDED(result));

	std::vector<ComputeFlags> flags = {
		ComputeFlags::INJECTION,
		ComputeFlags::SIMULATION,
		ComputeFlags::SIMULATION | ComputeFlags::ADD_GRAVITY,
	};

	for (auto &flag : flags)
	{
		auto macros = GetMacros(flag);
		Microsoft::WRL::ComPtr<ID3DBlob> computerShaderByteCode;
		result = D3DCompileFromFile(
			fileName.c_str(),
			&macros[0],
			nullptr,
			"CSMain",
			"cs_5_0",
			D3DCOMPILE_PACK_MATRIX_ROW_MAJOR | D3DCOMPILE_DEBUG,
			0,
			computerShaderByteCode.GetAddressOf(),
			&errorCode
		);
		assert(SUCCEEDED(result));
		ID3D11ComputeShader* compShader;
		result = Game::GetInstance()->GetRenderSystem()->device->CreateComputeShader(
			computerShaderByteCode->GetBufferPointer(),
			computerShaderByteCode->GetBufferSize(),
			nullptr, &compShader
		);
		assert(SUCCEEDED(result));
		ComputeShaders.emplace(flag, compShader);
	}
}

void ParticleSystem::CreateBuffers()
{
	D3D11_BUFFER_DESC constBufDesc;
	constBufDesc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
	constBufDesc.Usage          = D3D11_USAGE_DEFAULT;
	constBufDesc.MiscFlags      = 0;
	constBufDesc.CPUAccessFlags = 0;
	constBufDesc.ByteWidth      = sizeof(ConstData);
	auto result = Game::GetInstance()->GetRenderSystem()->device->CreateBuffer(&constBufDesc, nullptr, &constBuf);
	assert(SUCCEEDED(result));

	srand(time(NULL));
	auto partsTempBuf = new Particle[MaxParticlesCount]; // ������� ��������� ������ ������
	for (int i = 0; i < MaxParticlesCount; i++)
	{
		auto pos = Vector4(Length * RandomFloat(-1.0f, 1.0f), Height * 2.0f, Width * RandomFloat(0.0f, 1.0f), 1.0f);
		pos.Normalize();
		auto vel = pos * Vector4(10.0f, 10.0f, 10.0f, 1.0f);
		vel.w = 0.0f;

		partsTempBuf[i] = Particle // �������������� ������ �������
		{
			pos,
			vel,
			Vector4(RandomFloat(0.0f, 1.0f), RandomFloat(0.0f, 1.0f), RandomFloat(0.0f, 1.0f), 1.0f),
			Vector2(RandomFloat(0.1f, 0.3f), RandomFloat(0.3f, 0.5f)),
			RandomFloat(2.0f, 5.0f)
		};
	}

	// ������ ����������� ����� �����������
	D3D11_BUFFER_DESC bufDesc;
	bufDesc.BindFlags           = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
	bufDesc.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; // <-- ����������� �����          
	bufDesc.Usage               = D3D11_USAGE_DEFAULT;
	bufDesc.CPUAccessFlags      = 0;
	bufDesc.StructureByteStride = sizeof(Particle);
	bufDesc.ByteWidth           = MaxParticlesCount * sizeof(Particle);
	D3D11_SUBRESOURCE_DATA data;
	data.pSysMem          = &partsTempBuf[0];
	data.SysMemPitch      = 0;
	data.SysMemSlicePitch = 0;
	result = Game::GetInstance()->GetRenderSystem()->device->CreateBuffer(&bufDesc,  &data,  &bufFirst);
	assert(SUCCEEDED(result));
	result = Game::GetInstance()->GetRenderSystem()->device->CreateBuffer(&bufDesc, nullptr, &bufSecond);
	assert(SUCCEEDED(result));
	delete[] partsTempBuf;

	// SHADER RESOURCE VIEW
	result = Game::GetInstance()->GetRenderSystem()->device->CreateShaderResourceView(bufFirst,  nullptr, &srvFirst);
	assert(SUCCEEDED(result));
	result = Game::GetInstance()->GetRenderSystem()->device->CreateShaderResourceView(bufSecond, nullptr, &srvSecond);
	assert(SUCCEEDED(result));

	// UNORDERED ACCESS VIEW
	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc;
	uavDesc.Format        = DXGI_FORMAT_UNKNOWN;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	uavDesc.Buffer = D3D11_BUFFER_UAV
	{
		0,                           // ������ �������
		MaxParticlesCount,           // ���������� ���������
		D3D11_BUFFER_UAV_FLAG_APPEND // ����
	};
	result = Game::GetInstance()->GetRenderSystem()->device->CreateUnorderedAccessView(bufFirst,  &uavDesc, &uavFirst);
	assert(SUCCEEDED(result));
	result = Game::GetInstance()->GetRenderSystem()->device->CreateUnorderedAccessView(bufSecond, &uavDesc, &uavSecond);
	assert(SUCCEEDED(result));

	// �.�. ������ ����� ������ ������� �������, �� ������ �������������
	srvSrc = srvFirst;
	uavSrc = uavFirst;
	srvDst = srvSecond;
	uavDst = uavSecond;

	ID3D11UnorderedAccessView* nuPtr = nullptr;
	// ����� &MaxParticlesCount - �������� �������� (���� ����� ��������� ��������, ������� ���� �� �����, �� ������ ����� -1)
	Game::GetInstance()->GetRenderSystem()->context->CSSetUnorderedAccessViews(0, 1, &uavSrc, &MaxParticlesCount);
	Game::GetInstance()->GetRenderSystem()->context->CSSetUnorderedAccessViews(0, 1, &nuPtr, nullptr);

	// �����, � ������� �������� ������ ��������, ���������� � �������� uavSrc
	D3D11_BUFFER_DESC countBufDesc;
	countBufDesc.BindFlags           = 0;
	countBufDesc.Usage               = D3D11_USAGE_STAGING;   // �.�. ��� ������, �� �������� �� ������
	countBufDesc.CPUAccessFlags      = D3D11_CPU_ACCESS_READ;
	countBufDesc.MiscFlags           = 0;
	countBufDesc.StructureByteStride = 0;
	countBufDesc.ByteWidth           = 4; // � 1 int'� 4 �����
	result = Game::GetInstance()->GetRenderSystem()->device->CreateBuffer(&countBufDesc, nullptr, &countBuf);
	assert(SUCCEEDED(result));

	// ����� ��� ���������� ������ �� 1 ����
	D3D11_BUFFER_DESC injBufDesc;
	injBufDesc.BindFlags           = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
	injBufDesc.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	injBufDesc.Usage               = D3D11_USAGE_DEFAULT;
	injBufDesc.CPUAccessFlags      = 0;
	injBufDesc.StructureByteStride = sizeof(Particle);
	injBufDesc.ByteWidth           = MaxParticlesInjectionCount * sizeof(Particle);
	result = Game::GetInstance()->GetRenderSystem()->device->CreateBuffer(&injBufDesc, nullptr, &injectionBuf);
	assert(SUCCEEDED(result));

	// UNORDERED ACCESS VIEW ��� INJECTION
	D3D11_UNORDERED_ACCESS_VIEW_DESC injUavDesc;
	injUavDesc.Format        = DXGI_FORMAT_UNKNOWN;
	injUavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	injUavDesc.Buffer = D3D11_BUFFER_UAV
	{
		0,
		MaxParticlesInjectionCount,
		D3D11_BUFFER_UAV_FLAG_APPEND
	};
	result = Game::GetInstance()->GetRenderSystem()->device->CreateUnorderedAccessView(injectionBuf, &injUavDesc, &injUav);
	assert(SUCCEEDED(result));
}

void ParticleSystem::SwapBuffers()
{
	std::swap(srvSrc, srvDst);
	std::swap(uavSrc, uavDst);
}

void ParticleSystem::Update(float deltaTime) {}

void ParticleSystem::Draw(float deltaTime)
{
	if (Game::GetInstance()->inputDevice->IsKeyDown(Keys::V))
	{
		deltaTime = 0.0f;
	}

	// Const Buffer Standart Values
	constData.World      = Matrix::CreateTranslation(Position);
	constData.View       = Game::GetInstance()->currentCamera->gameObject->transformComponent->GetView();
	constData.Projection = Game::GetInstance()->currentCamera->GetProjection();

	// Group size ParticlesCount
	int groupSizeX, groupSizeY;
	GetGroupSize(particlesCount, groupSizeX, groupSizeY);
	constData.DeltaTimeMaxParticlesGroupdim = Vector4(deltaTime, particlesCount, groupSizeY, 0);
	Game::GetInstance()->GetRenderSystem()->context->UpdateSubresource(constBuf, 0, nullptr, &constData, 0, 0);
	Game::GetInstance()->GetRenderSystem()->context->CSSetConstantBuffers(0, 1, &constBuf);

	const UINT counterKeepValue = -1;
	const UINT counterZero = 0;
	Game::GetInstance()->GetRenderSystem()->context->CSSetUnorderedAccessViews(0, 1, &uavSrc, &counterKeepValue); // ������ �������, ��������� ������� ����� -1 // particlesBufSrc
	Game::GetInstance()->GetRenderSystem()->context->CSSetUnorderedAccessViews(1, 1, &uavDst, &counterZero);      // ���� ���������� ������� ������ 0           // particlesBufDst

	Game::GetInstance()->GetRenderSystem()->context->OMSetRenderTargets(0, nullptr, nullptr); ////////////////////////////////////

	// GBUFFER
	ID3D11ShaderResourceView* resources[] = {
		Game::GetInstance()->GetRenderSystem()->gBuffer->normalSRV,
		Game::GetInstance()->GetRenderSystem()->gBuffer->worldPositionSRV,
		// DEPTHMAP
		Game::GetInstance()->GetRenderSystem()->srvDepth.Get()
	};
	Game::GetInstance()->GetRenderSystem()->context->CSSetShaderResources(0, 3, resources);
	
	Game::GetInstance()->GetRenderSystem()->context->CSSetShader(ComputeShaders[ComputeFlags::SIMULATION | ComputeFlags::ADD_GRAVITY], nullptr, 0);

	if (groupSizeX > 0)
	{
		Game::GetInstance()->GetRenderSystem()->context->Dispatch(groupSizeX, groupSizeY, 1);
	}

	if (particlesCount < MaxParticlesCount)
	{
		int partToAdd;
		if (MaxParticlesCount - particlesCount > MaxParticlesInjectionCount)
		{
			partToAdd = MaxParticlesInjectionCount;
		}
		else
		{
			partToAdd = MaxParticlesCount - particlesCount;
		}
		for (int i = 0; i < partToAdd; i++)
		{
			// ������ ��������� �������, ��������
			auto pos = Vector4(Length * RandomFloat(-1.0f, 1.0f), Height * 2.0f, Width * RandomFloat(-1.0f, 1.0f), 1.0f);
			pos.Normalize();
			auto vel = pos * Vector4(10.0f, 10.0f, 10.0f, 1.0f);
			vel.w = 0.0f;

			injectionParticles[i] = Particle // �������������� ������ �������
			{
				pos,
				vel,
				Vector4(RandomFloat(0.0f, 1.0f), RandomFloat(0.0f, 1.0f), RandomFloat(0.0f, 1.0f), 1.0f),
				Vector2(RandomFloat(0.1f, 0.3f), RandomFloat(0.3f, 0.5f)),
				RandomFloat(2.0f, 5.0f)
			};
		}
		injectionCount = partToAdd;
	}

	if (injectionCount > 0)
	{
		int injSizeX, injSizeY;
		GetGroupSize(injectionCount, injSizeX, injSizeY);
		constData.DeltaTimeMaxParticlesGroupdim = Vector4(deltaTime, injectionCount, injSizeY, 0);
		Game::GetInstance()->GetRenderSystem()->context->UpdateSubresource(constBuf, 0, nullptr, &constData, 0, 0);
		Game::GetInstance()->GetRenderSystem()->context->CSSetConstantBuffers(0, 1, &constBuf);

		Game::GetInstance()->GetRenderSystem()->context->UpdateSubresource(injectionBuf, 0, nullptr, injectionParticles, 0, 0);
		Game::GetInstance()->GetRenderSystem()->context->CSSetUnorderedAccessViews(0, 1, &injUav, &injectionCount);

		ID3D11ShaderResourceView* nullResources[] = { nullptr, nullptr, nullptr };
		Game::GetInstance()->GetRenderSystem()->context->CSSetShaderResources(0, 3, nullResources);

		Game::GetInstance()->GetRenderSystem()->context->CSSetShader(ComputeShaders[ComputeFlags::INJECTION], nullptr, 0);

		Game::GetInstance()->GetRenderSystem()->context->Dispatch(injSizeX, injSizeY, 1);

		injectionCount = 0;
	}

	ID3D11ShaderResourceView* nullResources[] = { nullptr, nullptr, nullptr };
	Game::GetInstance()->GetRenderSystem()->context->CSSetShaderResources(0, 3, nullResources);

	ID3D11UnorderedAccessView* nuPtr = nullptr;
	Game::GetInstance()->GetRenderSystem()->context->CSSetUnorderedAccessViews(0, 1, &nuPtr, &counterZero);
	Game::GetInstance()->GetRenderSystem()->context->CSSetUnorderedAccessViews(1, 1, &nuPtr, &counterZero);

	// �������� �������� ��������
	Game::GetInstance()->GetRenderSystem()->context->CopyStructureCount(countBuf, 0, uavDst);
	D3D11_MAPPED_SUBRESOURCE subresource;
	Game::GetInstance()->GetRenderSystem()->context->Map(countBuf, 0, D3D11_MAP_READ, 0, &subresource);
	UINT* data = reinterpret_cast<UINT*>(subresource.pData);
	particlesCount = data[0];
	Game::GetInstance()->GetRenderSystem()->context->Unmap(countBuf, 0);

	SwapBuffers();

	// draw points
	Game::GetInstance()->GetRenderSystem()->context->OMSetRenderTargets(1, Game::GetInstance()->GetRenderSystem()->renderView.GetAddressOf(), Game::GetInstance()->GetRenderSystem()->depthView.Get()); //////////

	ID3D11RasterizerState* oldState = nullptr;
	Game::GetInstance()->GetRenderSystem()->context->RSGetState(&oldState);
	Game::GetInstance()->GetRenderSystem()->context->RSSetState(rastState);
	ID3D11BlendState* oldBlend = nullptr;
	float old_blend_factor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	UINT oldMask = 0;
	Game::GetInstance()->GetRenderSystem()->context->OMGetBlendState(&oldBlend, old_blend_factor, &oldMask);
	float blend_factor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	Game::GetInstance()->GetRenderSystem()->context->OMSetBlendState(blendState, blend_factor, 0xffffffff);

	ID3D11DepthStencilState* oldDepthState = nullptr;
	UINT oldStenRef = 0;
	Game::GetInstance()->GetRenderSystem()->context->OMGetDepthStencilState(&oldDepthState, &oldStenRef);
	Game::GetInstance()->GetRenderSystem()->context->OMSetDepthStencilState(depthState, 0);

	Game::GetInstance()->GetRenderSystem()->context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);

	Game::GetInstance()->GetRenderSystem()->context->VSSetShader(vertShader, nullptr, 0);
	Game::GetInstance()->GetRenderSystem()->context->GSSetShader(geomShader, nullptr, 0);
	Game::GetInstance()->GetRenderSystem()->context->PSSetShader(pixShader,  nullptr, 0);

	Game::GetInstance()->GetRenderSystem()->context->GSSetConstantBuffers(0, 1, &constBuf);
	Game::GetInstance()->GetRenderSystem()->context->GSSetShaderResources(3, 1, &srvSrc);

	Game::GetInstance()->GetRenderSystem()->context->Draw(particlesCount, 0);

	Game::GetInstance()->GetRenderSystem()->context->OMSetBlendState(oldBlend, old_blend_factor, oldMask);
	Game::GetInstance()->GetRenderSystem()->context->RSSetState(oldState);
	Game::GetInstance()->GetRenderSystem()->context->OMSetDepthStencilState(oldDepthState, oldStenRef);
}