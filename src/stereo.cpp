/**
 * (C) roentgen 
 * this code is licensed under the MIT License.
 */
#include <string>
#include "stdafx.h"
#include "stereo.hpp"
#include <D3DCompiler.h>
#include <vector>
#include <memory>
#include <deque>
#include <cmath>
#include <algorithm>
#include "serializer.hpp"

using Microsoft::WRL::ComPtr;

template < typename T >
inline T align256(T t)
{
    return static_cast< T >((255ULL + t) & ~255ULL);
}

void stereo_t::create_pipeline_state(uniq_device_t& u, const std::wstring& dir)
{
    auto shaderfile = dir + L"sidebyside.hlsl";
    ComPtr< ID3DBlob > vertex_shader;
    ComPtr< ID3DBlob > pixel_shader;
    ComPtr< ID3DBlob > err;
    if (FAILED(D3DCompileFromFile(shaderfile.c_str(), nullptr, nullptr, "VSMain", "vs_5_1", 0/* optimized */, 0, &vertex_shader, &err))) {
        ABTMSG("failed to compile vertex shader\n");
        OutputDebugStringA(reinterpret_cast< char* >(err->GetBufferPointer()));
    }
    if (FAILED(D3DCompileFromFile(shaderfile.c_str(), nullptr, nullptr, "PSMain", "ps_5_1", 0/* optimized */, 0, &pixel_shader, &err))) {
        ABTMSG("failed to compile pixel shader\n");
        OutputDebugStringA(reinterpret_cast< char* >(err->GetBufferPointer()));
    }
    
    D3D12_INPUT_ELEMENT_DESC input_descs[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,  0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,     0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    
    /* create PSO: no depth-test/write, no alpha-blend/op */
    D3D12_SHADER_BYTECODE vsbytes = {vertex_shader->GetBufferPointer(), vertex_shader->GetBufferSize()};
    D3D12_SHADER_BYTECODE psbytes = {pixel_shader->GetBufferPointer(), pixel_shader->GetBufferSize()};
    D3D12_RASTERIZER_DESC raster = {
        D3D12_FILL_MODE_SOLID,
        D3D12_CULL_MODE_BACK, FALSE, /* CCW */
        D3D12_DEFAULT_DEPTH_BIAS, D3D12_DEFAULT_DEPTH_BIAS_CLAMP, D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS, FALSE, /* depth clipping */
        FALSE, /* multi sampling */ FALSE, /* line AA */ 0, /* sampling count */
        D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF
    };
        
    D3D12_BLEND_DESC blend = {
        FALSE, /* alpha to coverage */
        FALSE, /* 同時に独立の blend を行う: false なら RenderTarge[0] だけが使われる  */
        {{ FALSE /* enable blend */, FALSE /* alpha-op */,
           D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD, /* col: src, dst, op */
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
    desc.DepthStencilState.DepthEnable = FALSE;
    desc.DepthStencilState.StencilEnable = FALSE;
    desc.SampleMask = UINT_MAX;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    if (FAILED(uniq_.dev()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso_)))) {
        /* PSO の RootSignature と shader に互換性がなければエラーになるはず */
    }
}


void stereo_t::create_root_signature(uniq_device_t& u)
{
    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    if (FAILED(u.dev()->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData)))) {
        INF("root signature version 1.0\n");
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0; /* win10 anniv (2016/5) まではこっち */
    }

    /**
     * ROOT CONSTANT    b0 - lr  (left or right)
     *                     - idx (offscreen index)
     * DESCRIPTOR TABLE b1 - model matricies
     * DESCRIPTOR TABLE t0 - 0-left texture
     *                  t1 - 1-left texture
     *                  t2 - 0-right texture
     *                  t3 - 1-right texture
     */
    D3D12_DESCRIPTOR_RANGE1 ranges[] = {
        create_range(D3D12_DESCRIPTOR_RANGE_TYPE_CBV,
                     1 /* descriptors */, 1 /* base-register index */, 0 /* space */,
                     D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC),
        create_range(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
                     4 /* descriptors */, 0 /* base-register index */, 0 /* space */,
                     D3D12_DESCRIPTOR_RANGE_FLAG_NONE),
    };
        
    D3D12_ROOT_DESCRIPTOR_TABLE1 rdt[] = {
        {1, ranges},
        {1, ranges + 1},
    };
    
    D3D12_ROOT_CONSTANTS rc[] = {{0 /* register */, 0 /* space */, 2 /* num constants in a single constant buffer */}};
    
    D3D12_ROOT_PARAMETER1 params[] = {
        create_root_param(rc     , D3D12_SHADER_VISIBILITY_ALL),
        create_root_param(rdt    , D3D12_SHADER_VISIBILITY_VERTEX),
        create_root_param(rdt + 1, D3D12_SHADER_VISIBILITY_PIXEL),
    };

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.MipLODBias = 0;
    sampler.MaxAnisotropy = 0;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    
    using namespace Microsoft::WRL;
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
    }
    NAME_OBJ(rootsig_);
}

void stereo_t::load_asset()
{
    wchar_t pathbuf[1024];
    uint32_t sz = GetModuleFileName(nullptr, pathbuf, 1024);
    if (!sz || sz == 1024)
        return;
    INF("Program base path:%s\n", pathbuf);
    std::wstring path(pathbuf);
    auto dir = path.substr(0, path.rfind(L"\\") + 1);
    
    const uint32_t idx = flipper_.idx();
    cmd_alloc_[idx]->Reset();
    auto hr = cmd_list_->Reset(cmd_alloc_[idx].Get(), nullptr);
    if (FAILED(hr)) {
        ABT("failed to main cmdlist:0x%x\n", hr);
    }

    {
        /* Final Stage */
        create_root_signature(uniq_);
        create_pipeline_state(uniq_, dir);
        plane_vertex_t v[] = {
            {{-.5f,-1.f, 0.f}, {0.f, 1.0f}},
            {{-.5f, 1.f, 0.f}, {0.f, 0.0f}},
            {{ .5f,-1.f, 0.f}, {1.f, 1.0f}},
            {{ .5f, 1.f, 0.f}, {1.f, 0.0f}}
        };
        
        size_t size = sizeof(v);
        
        D3D12_HEAP_PROPERTIES prop = setup_heapprop(D3D12_HEAP_TYPE_UPLOAD);
        D3D12_RESOURCE_DESC res = {
            D3D12_RESOURCE_DIMENSION_BUFFER, 0, /* alignment */ size /* width*/,
            1 /* height */, 1 /* depth/array size */, 1/* miplevels */, DXGI_FORMAT_UNKNOWN,
            {1 /* sample count */, 0 /* quality */}, /* SampleDesc */
            D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE,
        };
        auto hr = uniq_.dev()->CreateCommittedResource(&prop, D3D12_HEAP_FLAG_NONE, &res, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertbuf_));
        if (FAILED(hr)) {
            ABT("failed to create vert buffer: err:0x%x\n", hr);
        }
        uint8_t* va = nullptr;
        D3D12_RANGE readrange = {0, 0};
        vertbuf_->Map(0, &readrange, reinterpret_cast< void** >(&va));
        memcpy(va, v, size);
        vertbuf_->Unmap(0, nullptr);
        
        vbv_.BufferLocation = vertbuf_->GetGPUVirtualAddress(); // GPU
        vbv_.StrideInBytes = sizeof(plane_vertex_t);
        vbv_.SizeInBytes = static_cast< UINT >(size);
        
        /* CBV+L/R textures */
        D3D12_DESCRIPTOR_HEAP_DESC heap = {
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1+4/* num descriptors */, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0
        };
        uniq_.dev()->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&descheap_));

        {
            /* model 行列の定義と RT 用 unbound テクスチャ */
            D3D12_CPU_DESCRIPTOR_HANDLE hdl = descheap_->GetCPUDescriptorHandleForHeapStart();
            const size_t aligned = align256(sizeof(float) * 16); /* 計算するまでもなく 256 だけど */
            D3D12_RESOURCE_DESC desc = setup_buffer(aligned * 2);
            D3D12_HEAP_PROPERTIES prop = setup_heapprop(D3D12_HEAP_TYPE_UPLOAD);
            auto hr = uniq_.dev()->CreateCommittedResource(&prop, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&cbv_));
            D3D12_CONSTANT_BUFFER_VIEW_DESC cbvdesc = { cbv_->GetGPUVirtualAddress(), aligned * 2};
            uniq_.dev()->CreateConstantBufferView(&cbvdesc, hdl);
            hdl.ptr += uniq_.sizeset().view;
            
            DirectX::XMMATRIX matrix[] = {
                {{1.f, 0.f, 0.f, -0.5f}, {0.f, 1.f, 0.f, 0.0f}, {0.f, 0.f, 1.f, 0.0f}, {0.f, 0.f, 0.f, 1.0f}},
                {{1.f, 0.f, 0.f, 0.5f}, {0.f, 1.f, 0.f, 0.0f}, {0.f, 0.f, 1.f, 0.0f}, {0.f, 0.f, 0.f, 1.0f}},
                //{{1.f, 0.f, 0.f, -0.5f}, {0.f, 1.f, 0.f, 0.0f}, {0.f, 0.f, 1.f, 0.0f}, {0.f, 0.f, 0.f, 1.0f}},
                //{{1.f, 0.f, 0.f, 0.5f}, {0.f, 1.f, 0.f, 0.0f}, {0.f, 0.f, 1.f, 0.0f}, {0.f, 0.f, 0.f, 1.0f}},
            };
            volatile uint8_t* ptr = nullptr;
            D3D12_RANGE readrange = {0, 0};
            cbv_->Map(0, &readrange, reinterpret_cast< void** >(const_cast< uint8_t** >(&ptr)));
            memcpy(const_cast< uint8_t* >(ptr), matrix, sizeof(float) * 16);
            memcpy(const_cast< uint8_t* >(ptr) + sizeof(float) * 16, &matrix[1], sizeof(float) * 16);
            //memcpy(const_cast< uint8_t* >(ptr) + aligned * 2, &matrix[2], sizeof(float) * 16);
            //memcpy(const_cast< uint8_t* >(ptr) + aligned * 3, &matrix[3], sizeof(float) * 16);
            cbv_->Unmap(0, nullptr);

            D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
            srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srv.Texture2D.MipLevels = 1;
            /* 指定した SRV Heap に ID3D12Resource/D3D12_SHADER_RESOURCE_VIEW_DESC で指定した SRV を生成する */
            for (int i = 0; i < 2 * offscreen_buffers_; i ++) {
                uniq_.dev()->CreateShaderResourceView(eye_.rt(i), &srv, hdl);
                hdl.ptr += uniq_.sizeset().view;
            }
        }
    }

    ComPtr< ID3D12CommandAllocator > thr_cmd_alloc;
    ComPtr< ID3D12GraphicsCommandList > thr_cmd_lst;
    uniq_.dev()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&thr_cmd_alloc));
    uniq_.dev()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, thr_cmd_alloc.Get(), nullptr/*pso_.Get()*/,IID_PPV_ARGS(&thr_cmd_lst));
    NAME_OBJ(thr_cmd_lst);
    NAME_OBJ(thr_cmd_alloc);

    auto workq = std::make_shared< std::deque< std::wstring > >();
    workq->push_back(dir + L"mc512x512_blue.rawdata");
    
    loading_ = std::make_shared< loading_t< playground_t > >(workq);
    playing_ = std::make_shared< playground_t >();

    /* [&, thr_cmd_lst, thr_cmd_alloc] の capture を指定するとなぜか future::get() で例外が発生する */
    loading_->set_consumer(std::async(std::launch::deferred, [=]{
                playing_->init(uniq_, thr_cmd_lst.Get(), dir);
                /* depth に使う rendertarget, shadow_ をテクスチャとして参照できるよう、
                   先行して descriptor heap に書き込んでおく */
                playing_->set_shadowtexture(uniq_, shadow_.rt(0));

                thr_cmd_lst->Close();
                
                ID3D12CommandList* l[] = {thr_cmd_lst.Get()};
                uniq_.queue()->ExecuteCommandLists(std::extent< decltype(l) >::value, l);
                flipper_.wait(uniq_.queue().Get());
                
                return std::weak_ptr< playground_t >(playing_);
            }));
    
    loading_->init(uniq_, cmd_list_.Get(), dir);

    cmd_list_->Close();
    ID3D12CommandList* l[] = {cmd_list_.Get()};
    uniq_.queue()->ExecuteCommandLists(std::extent< decltype(l) >::value, l);
    flipper_.wait(uniq_.queue().Get());
}

ComPtr< ID3D12Resource > stereo_t::create_texture(int width, int height)
{
    D3D12_RESOURCE_DESC desc = setup_tex2d(width, height);
    D3D12_HEAP_PROPERTIES resident = setup_heapprop(D3D12_HEAP_TYPE_DEFAULT);

    ComPtr< ID3D12Resource > tex;
    auto hr = uniq_.dev()->CreateCommittedResource(&resident, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex));
    if (FAILED(hr)) {
        ABT("failed to create resident texture: err:0x%x\n", hr);
    }
    return tex;
}

extern "C" {
    int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int cmd);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int cmd)
{
    return runapp< stereo_t >(1280, 720, instance, cmd);
}

