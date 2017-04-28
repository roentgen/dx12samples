/**
 * (C) roentgen 
 * this code is licensed under the MIT License.
 */
#include <string>
#include "stdafx.h"
#include "shadow.hpp"
#include <D3DCompiler.h>
#include <vector>
#include <memory>
#include <deque>
#include <cmath>
#include <algorithm>

template < typename T >
inline T align256(T t)
{
	return static_cast< T >((255ULL + t) & ~255ULL);
}

static const float pi = std::acos(-1.f);
void set_perspective_lefthand(DirectX::XMMATRIX& mat, float fov, float aspectrate, float znear, float zfar)
{
	float s = 1.f / (std::tan(fov / 2.f));// radian from degree
	float d = zfar - znear;
	DirectX::XMMATRIX m = {{s / aspectrate, 0.f, 0.f, 0.f},
						   {0.f, s, 0.f, 0.f},
						   {0.f, 0.f, zfar / d, 1.f},
						   {0.f, 0.f, -(znear * zfar) / d, 0.f}};
	mat = m;
}

using namespace Microsoft::WRL;

void playground_t::init(uniq_device_t& u, ID3D12GraphicsCommandList* cmdlist, const std::wstring& dir)
{
	finished_ = false;

	D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
	featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
	if (FAILED(u.dev()->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData)))) {
		INF("root signature version 1.0\n");
		featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0; /* win10 anniv (2016/5) まではこっち */
	}

	D3D12_STATIC_SAMPLER_DESC sampler = {};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.MipLODBias = 0;
	sampler.MaxAnisotropy = 0;
	sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS;
	sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	sampler.MinLOD = 0.0f;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;
	sampler.ShaderRegister = 0;
	sampler.RegisterSpace = 0;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	{
		D3D12_DESCRIPTOR_RANGE1 ranges[3];
		ranges[0] = create_range(D3D12_DESCRIPTOR_RANGE_TYPE_CBV,
								 DYNAMIC_MODEL_MATRICIES /* descriptors */, 0 /* base-register index */, 0 /* space */,
								 D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
		/* DescriptrHeap をひとつずらして渡すのでレジスタが被らないよう 9 からマップする */
		ranges[1] = create_range(D3D12_DESCRIPTOR_RANGE_TYPE_CBV,
								 VIEWPROJECTION_MATRICIES + LIGHT_VIEWPROJECTION_MATRICIES /* descriptors */, 9 /* base-register index */, 0 /* space */,
								 D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
		ranges[2] = create_range(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
								 SRV_DESC_NUM /* descriptors */, 0 /* base-register index */, 0 /* space */,
								 D3D12_DESCRIPTOR_RANGE_FLAG_NONE);
		
		D3D12_ROOT_DESCRIPTOR_TABLE1 rdt[] = {
			{1, ranges},
			{1, ranges + 1},
			{1, ranges + 2}};
		
		D3D12_ROOT_PARAMETER1 params[] = {
			create_root_param(rdt    , D3D12_SHADER_VISIBILITY_VERTEX),
			create_root_param(rdt + 1, D3D12_SHADER_VISIBILITY_ALL),
			create_root_param(rdt + 2, D3D12_SHADER_VISIBILITY_PIXEL)};
		
		ComPtr<ID3DBlob> signature;
		ComPtr<ID3DBlob> error;
		D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = {};
		desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
		desc.Desc_1_1.NumParameters = std::extent< decltype(params) >::value;
		desc.Desc_1_1.pParameters = params;
		desc.Desc_1_1.NumStaticSamplers = 1;
		desc.Desc_1_1.pStaticSamplers = &sampler;
		desc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

		auto hr = serialize_root_signature(&desc, featureData.HighestVersion, &signature, &error);
		if (FAILED(hr)) {
			ABT("failed to serialize root signature: err:0x%x blob:\n", hr);
			OutputDebugStringA(reinterpret_cast< char* >(error->GetBufferPointer()));
		}
		hr = u.dev()->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootsig_));
		if (FAILED(hr)) {
			ABT("failed to create root signature: err:0x%x\n", hr);
			return;
		}
		NAME_OBJ(rootsig_);
	}

#if 1
	/* Ground: need tesselation.
	   vertex buffer size */
	{
		/**
		   Z
		   ^
		   |
		   +---> X
		 **/

//#define CHECK_DEPTH_BUFFER
#if !defined(CHECK_DEPTH_BUFFER)
		vertex_t v[6*8*8] = {};
		using namespace DirectX;
		XMFLOAT3 norm = {0.f, 1.f, 0.f};
		XMFLOAT3 fpos = {-4.f, 0.f, 4.f};
		for (int i = 0; i < 8; i ++) {
			for (int j = 0; j < 8; j ++) {
				float du = 1.f / 8.f;
				float dv = 1.f / 8.f;
				XMFLOAT2 uv = {du * float(j), dv * float(i)};
				XMFLOAT3 pos = {fpos.x + 1.f * float(j), 0.f, fpos.z + -1.f * float(i)};
				auto uv0 = uv;
				XMFLOAT2 uv1 = {uv.x + du, uv.y};
				XMFLOAT2 uv2 = {uv.x, uv.y + dv};
				XMFLOAT2 uv3 = {uv.x + du, uv.y + dv};
				auto v0 = pos;
				XMFLOAT3 v1 = {pos.x + 1.f, pos.y, pos.z};
				XMFLOAT3 v2 = {pos.x, pos.y, pos.z - 1.f};
				XMFLOAT3 v3 = {pos.x + 1.f, pos.y, pos.z - 1.f};
				const int cols = 6 * 8;
				int p = i * cols + j * 6;
				v[p + 0] = {v0, uv0, norm};
				v[p + 1] = {v1, uv1, norm};
				v[p + 2] = {v2, uv2, norm};
				v[p + 3] = {v2, uv2, norm};
				v[p + 4] = {v1, uv1, norm};
				v[p + 5] = {v3, uv3, norm};
			}
		}
#else
		/* debug 用の sprite plane */
		vertex_t v[] = {
			{{-1.f, -1.f, 0.f}, {1.f, 0.f}, {0.f, 1.f, 0.f}}, // 0: 
			{{-1.f,  1.f, 0.f}, {0.f, 0.f}, {0.f, 1.f, 0.f}}, // 1: 
			{{ 1.f, -1.f, 0.f}, {1.f, 1.f}, {0.f, 1.f, 0.f}}, // 2: 
			{{ 1.f,  1.f, 0.f}, {0.f, 1.f}, {0.f, 1.f, 0.f}}, // 3: 
		};
#endif
		size_t size = sizeof(v);
		auto prop = setup_heapprop(D3D12_HEAP_TYPE_UPLOAD);
		auto bufdesc = setup_buffer(size);

		auto hr = u.dev()->CreateCommittedResource(&prop, D3D12_HEAP_FLAG_NONE, &bufdesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&ground_vertupload_));

		if (FAILED(hr)) {
			ABT("failed to create vert buffer: err:0x%x\n", hr);
		}

		uint8_t* va = nullptr;
		D3D12_RANGE readrange = {0, 0};
		ground_vertupload_->Map(0, &readrange, reinterpret_cast< void** >(&va));
		memcpy(va, v, size);
		ground_vertupload_->Unmap(0, nullptr);

		u.dev()->CreateCommittedResource(&setup_heapprop(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &setup_buffer(size), D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&ground_vert_));

		cmdlist->CopyBufferRegion(ground_vert_.Get(), 0, ground_vertupload_.Get(), 0, size);

		D3D12_RESOURCE_BARRIER barrier1 = {
			D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, D3D12_RESOURCE_BARRIER_FLAG_NONE,
			{ground_vert_.Get(),
			 D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
			 D3D12_RESOURCE_STATE_COPY_DEST,/* before state */
			 D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE /* after state */ }};
		
		cmdlist->ResourceBarrier(1, &barrier1);

		ground_vbv_.BufferLocation = ground_vertupload_->GetGPUVirtualAddress(); // GPU
		ground_vbv_.StrideInBytes = sizeof(vertex_t);
		ground_vbv_.SizeInBytes = static_cast< UINT >(size);
	}
#endif

#if 1
	/* vertex buffer size */
	{
		/* cube 正面の左上から時計回りに A, B, C D とすると */
		vertex_t v[] = {
			// front
			{{-.5f, -.5f, -.5f}, {0.f, 1.f}, {0.f, 0.f, -1.f}}, // 0: D
			{{-.5f,  .5f, -.5f}, {0.f, 0.f}, {0.f, 0.f, -1.f}}, // 1: A
			{{ .5f, -.5f, -.5f}, {1.f, 1.f}, {0.f, 0.f, -1.f}}, // 2: C
			{{ .5f,  .5f, -.5f}, {1.f, 0.f}, {0.f, 0.f, -1.f}}, // 3: B
			/* left Y
			        |
				  Z-+     */
			{{-.5f, -.5f,  .5f}, {0.f, 1.f}, {-1.f, 0.f, 0.f}}, // 4: D
			{{-.5f,  .5f,  .5f}, {0.f, 0.f}, {-1.f, 0.f, 0.f}}, // 5: A
			{{-.5f, -.5f, -.5f}, {1.f, 1.f}, {-1.f, 0.f, 0.f}}, // 6: C
			{{-.5f,  .5f, -.5f}, {1.f, 0.f}, {-1.f, 0.f, 0.f}}, // 7: B
			/* right Y
			         |
					 +--Z  */
			{{ .5f, -.5f, -.5f}, {0.f, 1.f}, {1.f, 0.f, 0.f}}, // 8: D
			{{ .5f,  .5f, -.5f}, {0.f, 0.f}, {1.f, 0.f, 0.f}}, // 9: A
			{{ .5f, -.5f,  .5f}, {1.f, 1.f}, {1.f, 0.f, 0.f}}, // 10: C
			{{ .5f,  .5f,  .5f}, {1.f, 0.f}, {1.f, 0.f, 0.f}}, // 11: B
			/* back  Y
			         |
				  X--+	   */
			{{ .5f, -.5f, .5f}, {0.f, 1.f}, {0.f, 0.f, 1.f}}, // 12: D'
			{{ .5f,  .5f, .5f}, {0.f, 0.f}, {0.f, 0.f, 1.f}}, // 13: A'
			{{-.5f, -.5f, .5f}, {1.f, 1.f}, {0.f, 0.f, 1.f}}, // 14: C'
			{{-.5f,  .5f, .5f}, {1.f, 0.f}, {0.f, 0.f, 1.f}}, // 15: B'
			/* top   Z
			         |
				     +--X  */
			{{-.5f, .5f,-.5f}, {0.f, 1.f}, {0.f, 1.f, 0.f}}, // 16: D'
			{{-.5f, .5f, .5f}, {0.f, 0.f}, {0.f, 1.f, 0.f}}, // 17: A'
			{{ .5f, .5f,-.5f}, {1.f, 1.f}, {0.f, 1.f, 0.f}}, // 18: C'
			{{ .5f, .5f, .5f}, {1.f, 0.f}, {0.f, 1.f, 0.f}}, // 19: B'
			/* bottom
				     +--X
					 |
					 Z   */
			{{-.5f,-.5f, .5f}, {0.f, 1.f}, {0.f,-1.f, 0.f}}, // 20: D'
			{{-.5f,-.5f,-.5f}, {0.f, 0.f}, {0.f,-1.f, 0.f}}, // 21: A'
			{{ .5f,-.5f, .5f}, {1.f, 1.f}, {0.f,-1.f, 0.f}}, // 22: C'
			{{ .5f,-.5f,-.5f}, {1.f, 0.f}, {0.f,-1.f, 0.f}}  // 23: B'
		};
		size_t size = sizeof(v);
		auto prop = setup_heapprop(D3D12_HEAP_TYPE_UPLOAD);
		auto bufdesc = setup_buffer(size);
		
		auto hr = u.dev()->CreateCommittedResource(&prop, D3D12_HEAP_FLAG_NONE, &bufdesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&cube_vertupload_));
				
		if (FAILED(hr)) {
			ABT("failed to create vert buffer: err:0x%x\n", hr);
		}

		uint8_t* va = nullptr;
		D3D12_RANGE readrange = {0, 0};
		cube_vertupload_->Map(0, &readrange, reinterpret_cast< void** >(&va));
		memcpy(va, v, size);
		cube_vertupload_->Unmap(0, nullptr);

		u.dev()->CreateCommittedResource(&setup_heapprop(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &setup_buffer(size), D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&cube_vert_));

		cmdlist->CopyBufferRegion(cube_vert_.Get(), 0, cube_vertupload_.Get(), 0, size);

		D3D12_RESOURCE_BARRIER barrier1 = {
			D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, D3D12_RESOURCE_BARRIER_FLAG_NONE,
			{cube_vert_.Get(),
			 D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
			 D3D12_RESOURCE_STATE_COPY_DEST,/* before state */
			 D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE /* after state */ }};
		
		cmdlist->ResourceBarrier(1, &barrier1);

		vbv_.BufferLocation = cube_vertupload_->GetGPUVirtualAddress(); // GPU
		vbv_.StrideInBytes = sizeof(vertex_t);
		vbv_.SizeInBytes = static_cast< UINT >(size);
	}
#endif
	
	{
		/**
		    A'     B'
		    +-----+
		   A    B/|
		   +----+ |C'
		   |    | +
		   |    |/
		   +----+
		   D    C
		 **/
		int16_t idx[] = {0, 1, 2, 2, 1, 3,
						 4, 5, 6, 6, 5, 7,
						 8, 9, 10, 10, 9, 11,
						 12, 13, 14, 14, 13, 15,
						 16, 17, 18, 18, 17, 19,
						 20, 21, 22, 22, 21, 23};
		size_t size = sizeof(idx);
		u.dev()->CreateCommittedResource(&setup_heapprop(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE, &setup_buffer(size), D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&cube_idxupload_));
		int32_t* va = nullptr;
		D3D12_RANGE readrange = {0, 0};
		cube_idxupload_->Map(0, &readrange, reinterpret_cast< void** >(&va));
		memcpy(va, idx, size);
		cube_idxupload_->Unmap(0, nullptr);

		u.dev()->CreateCommittedResource(&setup_heapprop(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &setup_buffer(size), D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&cube_idx_));

		cmdlist->CopyBufferRegion(cube_idx_.Get(), 0, cube_idxupload_.Get(), 0, size);

		D3D12_RESOURCE_BARRIER barrier2 = {
			D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, D3D12_RESOURCE_BARRIER_FLAG_NONE,
			{cube_idx_.Get(),
			 D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
			 D3D12_RESOURCE_STATE_COPY_DEST,/* before state */
			 D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE /* after state */ }};
		
		cmdlist->ResourceBarrier(1, &barrier2);

		ibv_.BufferLocation = cube_idx_->GetGPUVirtualAddress(); // GPU
		ibv_.Format = DXGI_FORMAT_R16_UINT;
		ibv_.SizeInBytes = static_cast< UINT >(size);
	}

	D3D12_DESCRIPTOR_HEAP_DESC nullheap = {
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2 /* num descriptors */, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0
	};
	u.dev()->CreateDescriptorHeap(&nullheap, IID_PPV_ARGS(&nullsrv_descheap_));
	NAME_OBJ(nullsrv_descheap_);
	D3D12_CPU_DESCRIPTOR_HANDLE nullhdl = nullsrv_descheap_->GetCPUDescriptorHandleForHeapStart();
	for (int i = 0; i < 2; i ++) {
		D3D12_SHADER_RESOURCE_VIEW_DESC unbound = {};
		unbound.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		unbound.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		unbound.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		unbound.Texture2D.MipLevels = 1;
		u.dev()->CreateShaderResourceView(nullptr, &unbound, nullhdl);
		nullhdl.ptr += u.sizeset().view;
	}

	/* ConstantBufferView * 3 と ShaderResouceView の Descriptor Heap:
	   matrix を 2mesh ぶん送るため、それぞれの CBV を格納する.
	   view[2], projection, model[4], texture
	   
	   bundle がなるべく多くの描画コマンドを記録できるよう多めに model を確保しておく.
	   (ひとつの描画コマンドは複数のデスクリプタヒープを参照できないので)
	   SetDescriptorHeaps() でデスクリプタヒープを切り替えながら描画コマンドを発行するが
	   Bundle で切り替えるデスクリプタヒープは上位コマンドバッファのものと互換である必要がある.
	   (それでもインスタンス index を用いて一つの描画コマンドで頑張ろうとしなければ、ここまでする必要はない)
	*/
	D3D12_DESCRIPTOR_HEAP_DESC heap = {
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, CBV_DESC_NUM /* num descriptors */, /*D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE*/ D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0
	};
	/* CopyDescriptors() の src は、 CPU から read するために D3D12_DESCRIPTOR_HEAP_FLAG_NONE を使う必要があるが、
	   それだと GPU で読み取れないのでそのまま DescriptorHeap としては使用できない. */
	u.dev()->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&descheap_));

	D3D12_DESCRIPTOR_HEAP_DESC rheap = {
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, CBV_DESC_NUM + SRV_DESC_NUM /* num descriptors */, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0
	};
	u.dev()->CreateDescriptorHeap(&rheap, IID_PPV_ARGS(&scene_descheap_));

	D3D12_DESCRIPTOR_HEAP_DESC sheap = {
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, CBV_DESC_NUM + SRV_DESC_NUM /* num descriptors */, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0
	};
	u.dev()->CreateDescriptorHeap(&sheap, IID_PPV_ARGS(&shadow_descheap_));

	/* set all 'unbound' for shadow-pass */
	D3D12_CPU_DESCRIPTOR_HANDLE sshdl = shadow_descheap_->GetCPUDescriptorHandleForHeapStart();
	sshdl.ptr += (u.sizeset().view * CBV_DESC_NUM);
	for (int i = 0; i < SRV_DESC_NUM; i ++) {
		D3D12_SHADER_RESOURCE_VIEW_DESC unbound = {};
		unbound.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		unbound.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		unbound.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		unbound.Texture2D.MipLevels = 1;
		u.dev()->CreateShaderResourceView(nullptr, &unbound, sshdl);
		sshdl.ptr += u.sizeset().view;
	}
	
	static const int MATRICIES = CBV_DESC_NUM;
	DirectX::XMMATRIX matrix[MATRICIES] = {};


	// load identity matrix
	DirectX::XMMATRIX ident = {{1.f, 0.f, 0.f, 0.f},
							   {0.f, 1.f, 0.f, 0.f},
							   {0.f, 0.f, 1.f, 0.f},
							   {0.f, 0.f, 0.f, 1.f}};
	matrix[0] = ident;
	matrix[1] = ident;
	/* model and lightspace view and projection matricies */
	// camera-space
	matrix[DYNAMIC_MODEL_MATRICIES] = DirectX::XMMatrixLookAtLH({0.f, 5.f, -1.f, 0.f}, {0.f, 0.f, 0.f}, {0.f, 1.f, 0.f});
	//set_perspective_lefthand(matrix[DYNAMIC_MODEL_MATRICIES + 1], 120.f * (pi / 180.f), 16.f/9.f, 1.0f, 125.f);
	matrix[DYNAMIC_MODEL_MATRICIES + 1] = DirectX::XMMatrixPerspectiveFovLH(100.f * (pi / 180.f), 16.0f/9.0f, 1.f, 125.f);

	// light-space
	matrix[DYNAMIC_MODEL_MATRICIES + 2] = DirectX::XMMatrixLookAtLH({-2.f, 5.f, 0.f}, {0.f, 0.f, 0.f}, {0.f, 1.f, 0.f});
	matrix[DYNAMIC_MODEL_MATRICIES + 3] = DirectX::XMMatrixPerspectiveFovLH(60.f * (pi / 180.f), 1.0f, 1.f, 125.f);

	D3D12_CPU_DESCRIPTOR_HANDLE hdl = descheap_->GetCPUDescriptorHandleForHeapStart();
	{
		D3D12_RESOURCE_DESC desc = setup_buffer(align256(sizeof(float) * 16) * CBV_DESC_NUM);
		INF("cbv_: size:%lld\n", desc.Width);
		D3D12_HEAP_PROPERTIES prop = setup_heapprop(D3D12_HEAP_TYPE_UPLOAD);
		auto hr = u.dev()->CreateCommittedResource(&prop, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&cbv_));
		if (FAILED(hr)) {
			ABT("failed to create cbv resource: error:0x%x\n", hr);
		}

		float* ptr = nullptr;
		D3D12_RANGE readrange = {0, 0};
		cbv_->Map(0, &readrange, reinterpret_cast< void** >(&ptr));
		int n = 0;
		for (int i = 0; i < std::min< size_t >(std::extent< decltype(matrix) >::value, MATRICIES); i ++) {
			INF("matrix[%d]: gpu addr:0x%llx\n", i, cbv_->GetGPUVirtualAddress() + i * 256);
			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvdesc = { cbv_->GetGPUVirtualAddress() + i * 256, align256(sizeof(float) * 16)};
			u.dev()->CreateConstantBufferView(&cbvdesc, hdl);
			hdl.ptr += u.sizeset().view;
		
			memcpy(reinterpret_cast< uint8_t* >(ptr) + i * align256(sizeof(float) * 16), &matrix[i], sizeof(float) * 16);
			n ++;
		}
		cbv_->Unmap(0, nullptr);
#if 1
		D3D12_CONSTANT_BUFFER_VIEW_DESC unbound	= {};
		for (int i = n; i < CBV_DESC_NUM; i ++) {
			INF("matrix[%d]: gpu addr:0x%llx\n", i, cbv_->GetGPUVirtualAddress() + i * 256);
			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvdesc = { cbv_->GetGPUVirtualAddress() + i * 256, align256(sizeof(float) * 16)};
			u.dev()->CreateConstantBufferView(&cbvdesc, hdl);
			hdl.ptr += u.sizeset().view;
		}
#endif

		D3D12_CPU_DESCRIPTOR_HANDLE src = descheap_->GetCPUDescriptorHandleForHeapStart();
		D3D12_CPU_DESCRIPTOR_HANDLE dst1 = scene_descheap_->GetCPUDescriptorHandleForHeapStart();
		D3D12_CPU_DESCRIPTOR_HANDLE dst2 = shadow_descheap_->GetCPUDescriptorHandleForHeapStart();
		//u.dev()->CopyDescriptors(CBV_DESC_NUM, &dst1, nullptr, 0, &src, nullptr, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		//u.dev()->CopyDescriptors(CBV_DESC_NUM, &dst2, nullptr, 0, &src, nullptr, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		u.dev()->CopyDescriptorsSimple(CBV_DESC_NUM, dst1, src, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		u.dev()->CopyDescriptorsSimple(CBV_DESC_NUM, dst2, src, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}
	
	{
		auto shaderfile = dir + L"shadow.hlsl";
		ComPtr< ID3DBlob > vertex_shader;
		ComPtr< ID3DBlob > pixel_shader;
		ComPtr< ID3DBlob > err;
		if (FAILED(D3DCompileFromFile(shaderfile.c_str(), nullptr, nullptr, "VSMain", "vs_5_0", 0/* optimized */, 0, &vertex_shader, &err))) {
			ABTMSG("failed to compile vertex shader\n");
			OutputDebugStringA(reinterpret_cast< char* >(err->GetBufferPointer()));
		}
		if (FAILED(D3DCompileFromFile(shaderfile.c_str(), nullptr, nullptr, "PSMain", "ps_5_0", 0/* optimized */, 0, &pixel_shader, &err))) {
			ABTMSG("failed to compile pixel shader\n");
			OutputDebugStringA(reinterpret_cast< char* >(err->GetBufferPointer()));
		}

		D3D12_INPUT_ELEMENT_DESC input_descs[] = {
			{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,  0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,     0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
		};

		/* create Shadow PSO */
		D3D12_SHADER_BYTECODE vsbytes = {vertex_shader->GetBufferPointer(), vertex_shader->GetBufferSize()};
		D3D12_SHADER_BYTECODE psbytes = {pixel_shader->GetBufferPointer(), pixel_shader->GetBufferSize()};
		D3D12_RASTERIZER_DESC raster = {
			D3D12_FILL_MODE_SOLID,
			D3D12_CULL_MODE_NONE, // Back-culling:OFF
			FALSE, /* Front=CW */
			D3D12_DEFAULT_DEPTH_BIAS,
			D3D12_DEFAULT_DEPTH_BIAS_CLAMP,
			D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS,
			TRUE, /* depth clipping */
			FALSE, /* multi sampling */
			FALSE, /* line AA */
			0, /* sampling count */
			D3D12_CONSERVATIVE_RASTERIZATION_MODE_ON /* ラスタ境界での blooming を許可 */
		};
		
		D3D12_BLEND_DESC blend = {
			FALSE, /* alpha to coverage */
			FALSE, /* 同時に独立の blend を行う: false なら RenderTarge[0] だけが使われる  */
			{{ TRUE /* enable blend */, FALSE /* alpha-op */,
			   D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_OP_ADD, /* col: src, dst, op */
			   D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD, /* alpha: src, dst, op */
			   D3D12_LOGIC_OP_NOOP,
			   D3D12_COLOR_WRITE_ENABLE_ALL }, {}, {}, {}, {}, {}, {}, {}}
		};

		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
		desc.InputLayout = {input_descs, std::extent< decltype(input_descs) >::value};
		desc.pRootSignature = rootsig_.Get();
		desc.VS = vsbytes;
		desc.RasterizerState = raster;
		desc.BlendState = blend;
		desc.DepthStencilState.DepthEnable = TRUE;
		desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		desc.DepthStencilState.StencilEnable = FALSE;
		desc.SampleMask = UINT_MAX;
		desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
#if 0
		desc.PS = psbytes;
		desc.NumRenderTargets = 1;
		desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
#else
		desc.PS = {};
		desc.NumRenderTargets = 0;
		desc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
#endif
		desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		desc.SampleDesc.Count = 1;
		if (FAILED(u.dev()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&shadow_pso_)))) {
			/* PSO の RootSignature と shader に互換性がなければエラーになるはず */
		}
	}
	
	{
		auto shaderfile = dir + L"scene.hlsl";
		ComPtr< ID3DBlob > vertex_shader, pixel_shader, err;
		if (FAILED(D3DCompileFromFile(shaderfile.c_str(), nullptr, nullptr, "VSMain", "vs_5_0", 0/* optimized */, 0, &vertex_shader, &err))) {
			ABTMSG("failed to compile vertex shader\n");
			OutputDebugStringA(reinterpret_cast< char* >(err->GetBufferPointer()));
		}
		if (FAILED(D3DCompileFromFile(shaderfile.c_str(), nullptr, nullptr, "PSMain", "ps_5_0", 0/* optimized */, 0, &pixel_shader, &err))) {
			ABTMSG("failed to compile pixel shader\n");
			OutputDebugStringA(reinterpret_cast< char* >(err->GetBufferPointer()));
		}

		D3D12_INPUT_ELEMENT_DESC input_descs[] = {
			{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,  0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,     0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
		};

		/* create PSO */
		D3D12_SHADER_BYTECODE vsbytes = {vertex_shader->GetBufferPointer(), vertex_shader->GetBufferSize()};
		D3D12_SHADER_BYTECODE psbytes = {pixel_shader->GetBufferPointer(), pixel_shader->GetBufferSize()};
		D3D12_RASTERIZER_DESC raster = {
			D3D12_FILL_MODE_SOLID,
			D3D12_CULL_MODE_BACK, // Back-culling:ON
			FALSE, /* FrontCCW=Flase: Front=CW */
			D3D12_DEFAULT_DEPTH_BIAS,
			D3D12_DEFAULT_DEPTH_BIAS_CLAMP,
			D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS,
			TRUE, /* depth clipping */
			FALSE, /* multi sampling */
			FALSE, /* line AA */
			0, /* sampling count */
			D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF
		};

		D3D12_BLEND_DESC blend = {
			FALSE, /* alpha to coverage */
			FALSE, /* 同時に独立の blend を行う: false なら RenderTarge[0] だけが使われる  */
			{{ TRUE /* enable blend */, FALSE /* alpha-op */,
			   D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_OP_ADD, /* col: src, dst, op */
			   D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD, /* alpha: src, dst, op */
			   D3D12_LOGIC_OP_NOOP,
			   D3D12_COLOR_WRITE_ENABLE_ALL }, {}, {}, {}, {}, {}, {}, {}}
		};

		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
		desc.InputLayout = {input_descs, std::extent< decltype(input_descs) >::value};
		desc.pRootSignature = rootsig_.Get();
		desc.VS = vsbytes;
		desc.PS = psbytes;
		desc.RasterizerState = raster;
		desc.BlendState = blend;
		desc.DepthStencilState.DepthEnable = TRUE;
		/* greater: cur.z > depth(pos) -> pass */
		/* less:    cur.z < depth(pos) -> pass */
		desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;

		desc.DepthStencilState.StencilEnable = FALSE;
		desc.SampleMask = UINT_MAX;
		desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		desc.NumRenderTargets = 1;
		desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		desc.SampleDesc.Count = 1;
		if (FAILED(u.dev()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso_)))) {
			/* PSO の RootSignature と shader に互換性がなければエラーになるはず */
		}
	}

}

void playground_t::update(uniq_device_t& u, uint64_t freq)
{
#if 1
	DirectX::XMMATRIX mat = {
		{1.f, 0.f, 0.f, 0.f},
		{0.f, 1.f, 0.f, 0.f},
		{0.f, 0.f, 1.f, 0.f},
		{0.f, 0.f, 0.f, 1.f},
	};

	static uint64_t time = 0ULL;
	/* +angle の回転方向は左ネジの法則と同じ */
	auto q = DirectX::XMQuaternionRotationAxis({0.f, 1.0f, 0.f}, (time / 40.f) * (pi/180.f));
	/* 原点で rotate してから translate */
	mat = DirectX::XMMatrixMultiplyTranspose(DirectX::XMMatrixRotationQuaternion(q), DirectX::XMMatrixTranslationFromVector({0.f, 0.500f, 0.f}));
	time += freq;
	
	float* ptr = nullptr;
	D3D12_RANGE readrange = {0, 0};
	auto hr = cbv_->Map(0, &readrange, reinterpret_cast< void** >(&ptr));
	memcpy(reinterpret_cast< uint8_t* >(ptr) + 0 * align256(sizeof(float) * 16), &mat, sizeof(float) * 16);
	cbv_->Unmap(0, nullptr);
#endif
}

void playground_t::draw_shadowpass(uniq_device_t& u, ID3D12GraphicsCommandList* cmdlist)
{
	cmdlist->SetPipelineState(shadow_pso_.Get());
	cmdlist->SetGraphicsRootSignature(rootsig_.Get());

	ID3D12DescriptorHeap* heaps[] = {shadow_descheap_.Get()};
	cmdlist->SetDescriptorHeaps(1, heaps); /* all of DescriptorHeap(s) */

	/* CBV */
	D3D12_GPU_DESCRIPTOR_HANDLE gpuheap = shadow_descheap_->GetGPUDescriptorHandleForHeapStart();
	cmdlist->SetGraphicsRootDescriptorTable(0 /* slot */, gpuheap);
	gpuheap.ptr += (u.sizeset().view * DYNAMIC_MODEL_MATRICIES);
	cmdlist->SetGraphicsRootDescriptorTable(1 /* slot */, gpuheap);

	/* SRV */
	gpuheap.ptr += (u.sizeset().view * (VIEWPROJECTION_MATRICIES + LIGHT_VIEWPROJECTION_MATRICIES));
	cmdlist->SetGraphicsRootDescriptorTable(2 /* slot */, gpuheap);

	/* recording draw commands */
	cmdlist->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdlist->IASetVertexBuffers(0, 1, &vbv_);
	cmdlist->IASetIndexBuffer(&ibv_);
	//cmdlist->DrawInstanced(4, 1, 0, 0);
	cmdlist->DrawIndexedInstanced(36, 1, 0, 0, 0);
}

void playground_t::draw(uniq_device_t& u, ID3D12GraphicsCommandList* cmdlist)
{
	cmdlist->SetPipelineState(pso_.Get());
	cmdlist->SetGraphicsRootSignature(rootsig_.Get());

	ID3D12DescriptorHeap* heaps[] = {scene_descheap_.Get()};
	cmdlist->SetDescriptorHeaps(1, heaps); /* all of DescriptorHeap(s) */

#if 1
	/* CBV */
	D3D12_GPU_DESCRIPTOR_HANDLE gpuhandle2 = scene_descheap_->GetGPUDescriptorHandleForHeapStart();
	gpuhandle2.ptr += u.sizeset().view;
	cmdlist->SetGraphicsRootDescriptorTable(0 /* slot */, gpuhandle2); /* 間接参照用の Descriptor Table として GPU 空間のアドレスを指定する */
	gpuhandle2.ptr += (u.sizeset().view * (DYNAMIC_MODEL_MATRICIES - 1));
	cmdlist->SetGraphicsRootDescriptorTable(1 /* slot */, gpuhandle2);

	/* SRV */
	gpuhandle2.ptr += (u.sizeset().view * (VIEWPROJECTION_MATRICIES + LIGHT_VIEWPROJECTION_MATRICIES));
	cmdlist->SetGraphicsRootDescriptorTable(2 /* slot */, gpuhandle2);

	/* DescriptorHeap 上、 2 つ目の CBV を示すよう offset して drawcall を発行していたがそれをやると
	   全デスクリプタがずれるのでそれだけではまずい. rootsignature も切り替えるか、
	   DescriptorHeap 全体のコピーを切り替えるか、そのどちらか */
	cmdlist->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdlist->IASetVertexBuffers(0, 1, &ground_vbv_);

	cmdlist->DrawInstanced(6*8*8, 1, 0, 0);
#endif	

#if !defined(CHECK_DEPTH_BUFFER)
	/* CBV */
	D3D12_GPU_DESCRIPTOR_HANDLE gpuheap = scene_descheap_->GetGPUDescriptorHandleForHeapStart();
	cmdlist->SetGraphicsRootDescriptorTable(0 /* slot */, gpuheap);
	gpuheap.ptr += (u.sizeset().view * DYNAMIC_MODEL_MATRICIES);
	cmdlist->SetGraphicsRootDescriptorTable(1 /* slot */, gpuheap);

	/* SRV */
	gpuheap.ptr += (u.sizeset().view * (VIEWPROJECTION_MATRICIES + LIGHT_VIEWPROJECTION_MATRICIES));
	cmdlist->SetGraphicsRootDescriptorTable(2 /* slot */, gpuheap);

	/* recording draw commands */
	cmdlist->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdlist->IASetVertexBuffers(0, 1, &vbv_);
	cmdlist->IASetIndexBuffer(&ibv_);
	cmdlist->DrawIndexedInstanced(36, 1, 0, 0, 0);
#endif
	
}


bool playground_t::is_ready()
{
	return true;
}

