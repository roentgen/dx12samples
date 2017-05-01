/**
 * (C) roentgen 
 * this code is licensed under the MIT License.
 */
#include "loading.hpp"

#include <string>
#include "stdafx.h"
#include <D3DCompiler.h>
#include <vector>
#include <memory>
#include <deque>
#include <cmath>
#include <algorithm>
#include "serializer.hpp"
    
using Microsoft::WRL::ComPtr;

struct vertex_t {
    DirectX::XMFLOAT3 pos;
    DirectX::XMFLOAT2 uv;
};

void graphics_impl_t::init(uniq_device_t& u,  ID3D12GraphicsCommandList* cmdlist, const std::wstring& dir, ID3D12Resource* trampoline)
{
    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    if (FAILED(u.dev()->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData)))) {
        INF("root signature version 1.0\n");
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0; /* win10 anniv (2016/5) まではこっち */
    }
    
    D3D12_DESCRIPTOR_RANGE1 ranges[2];
    ranges[0] = create_range(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
                             1 /* descriptors */, 0 /* base-register index */, 0 /* space */,
                             D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
    
    ranges[1] = create_range(D3D12_DESCRIPTOR_RANGE_TYPE_CBV,
                             1 /* descriptors */, 0 /* base-register index */, 0 /* space */,
                             D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

    D3D12_ROOT_DESCRIPTOR_TABLE1 rdt[] = {
        {1, ranges},
        {1, ranges + 1}
    };

    D3D12_ROOT_CONSTANTS rc[] = {{1 /* register */, 0 /* space */, 1 /* num constants in a single constant buffer */}};

    D3D12_ROOT_PARAMETER1 params[] = {
        create_root_param(rc, D3D12_SHADER_VISIBILITY_VERTEX),
        create_root_param(rdt    , D3D12_SHADER_VISIBILITY_PIXEL),
        create_root_param(rdt + 1, D3D12_SHADER_VISIBILITY_VERTEX)
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

    /* vertex buffer size */
    {
        vertex_t v[] = {
            {{-1.f, -1.f, 0.f}, {0.f, 1.f}},
            {{-1.f,  1.f, 0.f}, {0.f, 0.f}},
            {{ 1.f, -1.f, 0.f}, {1.f, 1.f}},
            {{ 1.f,  1.f, 0.f}, {1.f, 0.f}}
        };
        size_t size = sizeof(v);
        auto prop = setup_heapprop(D3D12_HEAP_TYPE_UPLOAD);
        auto bufdesc = setup_buffer(size);
        
        DXGI_QUERY_VIDEO_MEMORY_INFO meminfo1 = {};
        u.adapter()->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &meminfo1);
        auto hr = u.dev()->CreateCommittedResource(&prop, D3D12_HEAP_FLAG_NONE, &bufdesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertbuf_));
        DXGI_QUERY_VIDEO_MEMORY_INFO meminfo2 = {};
        u.adapter()->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &meminfo2);
                
        if (FAILED(hr)) {
            ABT("failed to create vert buffer: err:0x%x\n", hr);
        }
        uint8_t* va = nullptr;
        D3D12_RANGE readrange = {0, 0};
        vertbuf_->Map(0, &readrange, reinterpret_cast< void** >(&va));
        memcpy(va, v, size);
        vertbuf_->Unmap(0, nullptr);

        vbv_.BufferLocation = vertbuf_->GetGPUVirtualAddress(); // GPU
        vbv_.StrideInBytes = sizeof(vertex_t);
        vbv_.SizeInBytes = static_cast< UINT >(size);
    }


    /* ConstantBufferView * 2 と ShaderResouceView の Descriptor Heap:
       matrix を 2mesh ぶん送るため、それぞれの CBV を格納する */
    D3D12_DESCRIPTOR_HEAP_DESC heap = {
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2 + 1 /* num descriptors */, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0
    };
    u.dev()->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&descheap_));
    
    D3D12_CPU_DESCRIPTOR_HANDLE hdl = descheap_->GetCPUDescriptorHandleForHeapStart();
    {
        D3D12_RESOURCE_DESC desc = setup_buffer(((sizeof(float) * 16 + 255) & ~255) * 2);
        D3D12_HEAP_PROPERTIES prop = setup_heapprop(D3D12_HEAP_TYPE_UPLOAD);
        auto hr = u.dev()->CreateCommittedResource(&prop, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&cbv_));
        if (FAILED(hr)) {
            ABT("failed to create cbv resource: error:0x%x\n", hr);
        }

        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvdesc1 = { cbv_->GetGPUVirtualAddress(), (sizeof(float) * 16 + 255) & ~255};
        u.dev()->CreateConstantBufferView(&cbvdesc1, hdl);
        hdl.ptr += u.sizeset().view;
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvdesc2 = { cbv_->GetGPUVirtualAddress() + 256, (sizeof(float) * 16 + 255) & ~255};
        u.dev()->CreateConstantBufferView(&cbvdesc2, hdl);
        hdl.ptr += u.sizeset().view;
        
        DirectX::XMMATRIX matrix[2] = {
            {{0.2f, 0.f, 0.f, .75f},
             {0.f, 0.2f, 0.f, -.8f},
             {0.f, 0.f, 0.2f, 0.0f},
             {0.f, 0.f, 0.f, 1.0f}},
            {{1.f, 0.f, 0.f, -0.5f}, {0.f, 1.f, 0.f, 0.0f}, {0.f, 0.f, 1.f, 0.0f}, {0.f, 0.f, 0.f, 1.0f}},
        };
        float* ptr = nullptr;
        D3D12_RANGE readrange = {0, 0};
        cbv_->Map(0, &readrange, reinterpret_cast< void** >(&ptr));
        memcpy(ptr, matrix, sizeof(float) * 16);
        memcpy(reinterpret_cast< uint8_t* >(ptr) + ((sizeof(float)*16+255) & ~255), &matrix[1], sizeof(float) * 16);
        cbv_->Unmap(0, nullptr);
    }

    /* テクスチャデータのアップロード */
    {
        int width = 64;
        int height = 64;
        std::vector< uint32_t > data = load_graphics_asset(dir +L"loading.rawdata", width, height);
        INF("loading: first texture: width%d height:%d\n", width, height);
        D3D12_RESOURCE_DESC desc = setup_tex2d(width, height);
        D3D12_HEAP_PROPERTIES resident = setup_heapprop(D3D12_HEAP_TYPE_DEFAULT);

        auto hr = u.dev()->CreateCommittedResource(&resident, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex_));
        if (FAILED(hr)) {
            ABT("failed to create resident texture: err:0x%x\n", hr);
        }
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT copied = write_to_trampoline(u, std::move(data), desc, trampoline); /* copy to trampoline */
        
        //issue_texture_upload(copycmdlist.Get(), copied, tex_.Get(), trampoline);
        issue_texture_upload(cmdlist, copied, tex_.Get(), trampoline);
        
        D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = 1;
        /* 指定した SRV Heap に ID3D12Resource/D3D12_SHADER_RESOURCE_VIEW_DESC で指定した SRV を生成する */
        u.dev()->CreateShaderResourceView(tex_.Get(), &srv, hdl);
        hdl.ptr += u.sizeset().view;
    }

    auto shaderfile = dir + L"loading.hlsl";
    /* 静的リソースのロード. 最低限なにか読んで置かないと、 Loading 画面も出せない */
    {
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
        };

        /* create PSO */
        D3D12_SHADER_BYTECODE vsbytes = {vertex_shader->GetBufferPointer(), vertex_shader->GetBufferSize()};
        D3D12_SHADER_BYTECODE psbytes = {pixel_shader->GetBufferPointer(), pixel_shader->GetBufferSize()};
        D3D12_RASTERIZER_DESC raster = {
            D3D12_FILL_MODE_SOLID,
            D3D12_CULL_MODE_BACK, /* back culling */
            FALSE, /* CCW: Front=CW */
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
        desc.DepthStencilState.DepthEnable = FALSE;
        desc.DepthStencilState.StencilEnable = FALSE;
        desc.SampleMask = UINT_MAX;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        if (FAILED(u.dev()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso_)))) {
            /* PSO の RootSignature と shader に互換性がなければエラーになるはず */
        }
    }
}

std::vector< uint32_t > load_graphics_asset(const std::wstring& fname, int& width, int& height)
{
    std::vector< uint32_t > data;
    FILE* rawfp = nullptr;
    _wfopen_s(&rawfp, fname.c_str(), L"rb");
    std::unique_ptr< FILE, decltype(&fclose) > fp(rawfp, fclose);
    if (!fp) {
        WRN("could not locate file:%s\n", fname.c_str());
        return data;
    }
    __pragma(pack(push, 1)) struct header_t {
        uint8_t fourcc[4];
        uint16_t ver_hi;
        uint16_t ver_lo;
        uint32_t reserve;
        uint32_t width;
        uint32_t height;
        uint32_t format;
        uint32_t pixperbyte;
        uint32_t notelen;
    };// __attribute__((__packed__));
    __pragma(pack(pop));
    header_t head;
    if (fread(&head, 1, sizeof(header_t), fp.get()) < sizeof(header_t)) {
        WRN("file maybe broken:%s\n", fname.c_str());
        return data;
    }
    fseek(fp.get(), head.notelen, SEEK_CUR);
    
    width = head.width;
    height = head.height;
    
    if (head.width > TRAMPOLINE_MAX_WIDTH || head.height > TRAMPOLINE_MAX_HEIGHT) {
        WRN("file must small than trampoline buffer:%s (%d, %d) \n ", fname.c_str(), head.width, head.height);
        return data;
    }
    size_t len = head.width * head.height; /* たぶん dense */
    data.reserve(len);
    data.resize(len);
    fread(&data[0], 1, len * head.pixperbyte, fp.get());
    return data;
}

ComPtr< ID3D12Resource > graphics_impl_t::create_texture(uniq_device_t& u, int width, int height)
{
    D3D12_RESOURCE_DESC desc = setup_tex2d(width, height);
    D3D12_HEAP_PROPERTIES resident = setup_heapprop(D3D12_HEAP_TYPE_DEFAULT);
    ComPtr< ID3D12Resource > tex;
    auto hr = u.dev()->CreateCommittedResource(&resident, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex));
    if (FAILED(hr)) {
        ABT("failed to create resident texture: err:0x%x\n", hr);
    }
    return tex;
}


void graphics_impl_t::update(uniq_device_t& u, uint64_t freq)
{
    time_ += freq;
    cover_alpha_ = std::max< float >(sin(time_ / 500.f), 0.1f) * 255;
    INF("update: freq:%lld time:%lld cover_alpha_:%d\n", freq, time_, cover_alpha_);
    if (time_ >= 1000)
        time_ = 0;
}

void graphics_impl_t::draw(uniq_device_t& u, ID3D12GraphicsCommandList* cmdlist)
{
    cmdlist->SetPipelineState(pso_.Get());
    cmdlist->SetGraphicsRootSignature(rootsig_.Get());
    cmdlist->SetGraphicsRoot32BitConstant(0, cover_alpha_, 0);

    ID3D12DescriptorHeap* heaps[] = {descheap_.Get()};

    cmdlist->SetDescriptorHeaps(1, heaps); /* all of DescriptorHeap(s)  */
    /* CBV */
    D3D12_GPU_DESCRIPTOR_HANDLE gpuheap = descheap_->GetGPUDescriptorHandleForHeapStart();
    cmdlist->SetGraphicsRootDescriptorTable(2 /* slot */, gpuheap);
    gpuheap.ptr += (u.sizeset().view * 2);
    /* SRV */
    cmdlist->SetGraphicsRootDescriptorTable(1 /* slot */, gpuheap);
    
    /* recording draw commands */
    cmdlist->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    cmdlist->IASetVertexBuffers(0, 1, &vbv_);
    cmdlist->DrawInstanced(4, 1, 0, 0);
}


D3D12_PLACED_SUBRESOURCE_FOOTPRINT write_to_trampoline(uniq_device_t& u, std::vector< uint32_t > data, const D3D12_RESOURCE_DESC& texdesc, ID3D12Resource* trampoline)
{
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    size_t rowpitch;
    size_t totalbytes;
    uint32_t rows;
    u.dev()->GetCopyableFootprints(&texdesc,
                                   0 /* first idx of the resource */,
                                   1 /* num of subresorces */,
                                   0 /* base offset to the resource in bytes */,
                                       &footprint, &rows, &rowpitch, &totalbytes);
    INF("texture footprint: rows:%d rowpitch:%lld totalbyte:%lld\n", rows, rowpitch, totalbytes);
    uint8_t* ptr = nullptr;
    trampoline->Map(0, nullptr, reinterpret_cast< void** >(&ptr));
    for (int y = 0; y < texdesc.Height; ++ y) {
        memcpy(ptr + footprint.Offset + footprint.Footprint.RowPitch * y, data.data() + texdesc.Width * y, rowpitch);
    }
    trampoline->Unmap(0, nullptr);
    return footprint;
}

int issue_texture_upload(ID3D12GraphicsCommandList* cmdlist, const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint, ID3D12Resource* tex, ID3D12Resource* trampoline, D3D12_RESOURCE_STATES after)
{
    /* COPY コマンドを設定: trampoline(UPLOAD) -> tex(RESIDENT VRAM) */
    D3D12_TEXTURE_COPY_LOCATION dst = {tex, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {0}};
    D3D12_TEXTURE_COPY_LOCATION src = {trampoline, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {footprint}};
    /* dst の Dimension が Buffer なら CopyBufferRegion() を使う */
    cmdlist->CopyTextureRegion(&dst, 0 /* dst-x */, 0 /* dst-y */, 0/* dst-z */, &src, nullptr);

    /* Barrier(GPU 同期): 
       D3D12_RESOURCE_TRANSITION_BARRIER でリソースの状態を明示する.
       COPY 前に参照していない場合は D3D12_RESOURCE_STATE_COPY_DEST.
    */
    D3D12_RESOURCE_BARRIER barrier = {
        D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        D3D12_RESOURCE_BARRIER_FLAG_NONE,
        /* D3D12_RESOURCE_TRANSITION_BARRIER */
        { 
            tex,
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            D3D12_RESOURCE_STATE_COPY_DEST,/* before state */
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE /* after state */
        }
    };
    /* COPY cmdlist の場合は CommandQueue の非同期実行完了時に暗黙の状態遷移(COMMON への降格(decay))が起きる */
    if (cmdlist->GetType() == D3D12_COMMAND_LIST_TYPE_COPY) {
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
    }
    cmdlist->ResourceBarrier(1, &barrier);
    
    return 0;
}

