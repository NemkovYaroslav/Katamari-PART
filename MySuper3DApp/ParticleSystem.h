#pragma once
#include "Component.h"

#include "magic_enum.hpp"
#include <map>

using namespace DirectX::SimpleMath;

using namespace magic_enum::bitwise_operators;

class ParticleSystem : public Component
{
public:

	ID3D11Buffer *bufFirst, *bufSecond, *countBuf, *injectionBuf, *constBuf;
	// countBuf - ������������ ����� �������� ���������� ����� ��� ������� ������
	// injectionBuf - � ������� ��� ����� �������� ����� �������

	ID3D11ShaderResourceView  *srvFirst, *srvSecond, *srvSrc, *srvDst;
	ID3D11UnorderedAccessView *uavFirst, *uavSecond, *uavSrc, *uavDst, *injUav;
	// srvFirst � srvSecond - ������������ ��� �����������

	// ��� - ������� ��������� ������
	Vector3 Position;
	float Width, Height, Length;

	const unsigned int MaxParticlesCount = 1024;          // ������������ ���������� ������
	const unsigned int MaxParticlesInjectionCount = 256; // ������������ ���������� ������, ������� ����� �������� �� 1 ����, �� ������ 100
	UINT injectionCount = 0;                             // ���������� ������, ������� �� ��������� �� ������� �����
	int particlesCount = MaxParticlesCount;              // ������� ���������� ������

	struct Particle         // ���������, ����������� �������
	{
		Vector4 Position;   // ������� �������
		Vector4 Velocity;   // �������� �������
		Vector4 Color0;     // ���� �������
		Vector2 Size0Size1; // ��� ������� ������� (������ ����� �������� �� ��������)
		float LifeTime;     // ����� ����� �������
	};

	struct ConstData
	{
		Matrix World;                          // ������� ������� �������
		Matrix View;                           // ������� ���� ������
		Matrix Projection;                     // ������� �������� ������
		Vector4 DeltaTimeMaxParticlesGroupdim; // tick, ������������ ���������� ������, ������
	};

	enum class ComputeFlags
	{
		INJECTION   = 1 << 0, // ��������� ����� �������
		SIMULATION  = 1 << 1, // ������� ����������� ������
		ADD_GRAVITY = 1 << 2, // ��������� ����������
	};

	std::map<ComputeFlags, ID3D11ComputeShader*> ComputeShaders;

	ConstData constData;

	Particle* injectionParticles = new Particle[MaxParticlesInjectionCount]; // ���� ����������� ������

	ID3D11VertexShader*   vertShader; // ��� ��������� ������
	ID3D11GeometryShader* geomShader; // ��� ��������� ������
	ID3D11PixelShader*    pixShader;  // ��� ��������� ������

	ID3D11RasterizerState*   rastState;
	ID3D11BlendState*        blendState;
	ID3D11DepthStencilState* depthState;

	ParticleSystem();

	static void GetGroupSize(int partCount, int& groupSizeX, int& groupSizeY);
	void SwapBuffers();

	void Initialize() override;
	void Update(float deltaTime) override;
	void Draw(float deltaTime);

	void LoadShaders(std::string shaderFileName);
	void CreateBuffers();
};

