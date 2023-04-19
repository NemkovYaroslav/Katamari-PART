#pragma once
#include "Component.h"

using namespace DirectX::SimpleMath;

class ParticleSystem : Component
{
public:

	ID3D11Buffer *bufFirst, *bufSecond, *countBuf, *injectionBuf, *constBuf;

	ID3D11ShaderResourceView *srvFirst, *srvSecond, *srvSrc, *srvDst;
	ID3D11UnorderedAccessView *uavFirst, *uavSecond, *uavSrc, *uavDst, *injUav;

	Vector3 Position;
	float Width, Height, Length;

	const unsigned int MaxParticlesCount = 256 * 256 * 128;
	const unsigned int MaxParticlesInjectionCount = 100;
	UINT injectionCount = 0;

	int ParticlesCount = MaxParticlesCount; // ������� ���������� ������

	struct Particle // �������
	{
		Vector4 Position; // �������
		Vector4 Velocity; // ��������
		Vector4 Color0; // ����
		Vector2 Size0Size1; // ��� ������� (������ ������� ����� �������� �� ��������)
		float LifeTime; // ����� �����
	};

	struct ConstData
	{
		Matrix World;
		Matrix View;
		Matrix Projection;
		Vector4 DeltaTimeMaxParticlesGroupdim; // ����� ����, ������������ ���������� ������, ���-��...
	};

	// INJECTION   // ��������� ����� �������
	// SIMULATION  // ������ ����������� ������
	// ADD_GRAVITY // ��������� ����������

	ID3D11ComputeShader* ComputeShaders;

	ConstData constData;

	Particle* injectionParticles = new Particle[MaxParticlesInjectionCount];

	ID3D11VertexShader*   vertShader;
	ID3D11GeometryShader* geomShader;
	ID3D11PixelShader*    pixShader;

	ID3D11RasterizerState*   rastState;
	ID3D11BlendState*        blendState;
	ID3D11DepthStencilState* depthState;

	ParticleSystem();

	static void GetGroupSize(int partCount, int &groupSizeX, int &groupSizeY);

	void Initialize() override;
	void Update(float deltaTime) override;
	void Draw(float deltaTime);

	void DrawDebugBox();

	void LoadShaders(std::string shaderFileName);

	void CreateBuffers();
	void AddParticle(const Particle& p);
	void SwapBuffers();
};

