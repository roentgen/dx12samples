/**
 * (C) roentgen 
 * this code is licensed under the MIT License.
 */
#include "stdafx.h"
#include <string>
#include "simple.hpp"
#include <D3DCompiler.h>
#include <vector>

using Microsoft::WRL::ComPtr;

void simple_t::load_asset()
{
    uploader_.create_uploader(uniq_);

    create_root_signature(uniq_);

    wchar_t pathbuf[1024];
    uint32_t sz = GetModuleFileName(nullptr, pathbuf, 1024);
    if (!sz || sz == 1024)
        return;
    INF("Program base path:%s\n", pathbuf);
    std::wstring path(pathbuf);
    auto dir = path.substr(0, path.rfind(L"\\") + 1);
    
    auto shaderfile = dir + L"texture.hlsl";
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
            D3D12_CULL_MODE_BACK,
            FALSE, /* CCW */
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

        /* 割と不安ではあるが、CreateGraphicsPipelineState() の call 後、
           Blob などの生存期間が終わってもよいらしい */
    }
    
    /* 頂点データの準備:
       UPLOAD Heap に書き込むが、このタイミングではアップロードされるわけではない */
    {
        vertex_t v[] = {
            {{0.f, 0.25f, 0.f}, {.5f, 0.5f}},
            {{0.25f, -0.25f, 0.f}, {0.5f, 0.5f}},
            {{-0.25f, -0.25f, 0.f}, {0.5f, 0.5f}}
        };

        size_t size = sizeof(v);
        
        D3D12_HEAP_PROPERTIES prop = {
            D3D12_HEAP_TYPE_UPLOAD,
            D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
            D3D12_MEMORY_POOL_UNKNOWN,
            1, /* creation node mask */
            1 /* visible node mask */
        };
        
        D3D12_RESOURCE_DESC res = {
            D3D12_RESOURCE_DIMENSION_BUFFER,
            0, /* alignment */
            size /* width*/, 1 /* height */, 1 /* depth/array size */, 1/* miplevels */,
            DXGI_FORMAT_UNKNOWN,
            {1 /* sample count */, 0 /* quality */}, /* SampleDesc */
            D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
            D3D12_RESOURCE_FLAG_NONE,
        };
        DXGI_QUERY_VIDEO_MEMORY_INFO meminfo1 = {};
        uniq_.adapter()->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &meminfo1);
        auto hr = uniq_.dev()->CreateCommittedResource(&prop, D3D12_HEAP_FLAG_NONE, &res, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertbuf_));
        DXGI_QUERY_VIDEO_MEMORY_INFO meminfo2 = {};
        uniq_.adapter()->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &meminfo2);
                
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
        INF("GPU VA:0x%llx stride:%d size:%d\n", vbv_.BufferLocation, vbv_.StrideInBytes, vbv_.SizeInBytes);
    }

    {
        /* ConstantBufferView * 2 と ShaderResouceView の Descriptor Heap:
           matrix を 2mesh ぶん送るため、それぞれの CBV を格納する */
        D3D12_DESCRIPTOR_HEAP_DESC heap = {
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2 + 1 /* num descriptors */, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0
        };
        uniq_.dev()->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&descheap_));

    }
    D3D12_CPU_DESCRIPTOR_HANDLE hdl = descheap_->GetCPUDescriptorHandleForHeapStart();

    {
        D3D12_RESOURCE_DESC desc = setup_buffer(((sizeof(float) * 16 + 255) & ~255) * 2);
        D3D12_HEAP_PROPERTIES prop = setup_heapprop(D3D12_HEAP_TYPE_UPLOAD);
        auto hr = uniq_.dev()->CreateCommittedResource(&prop, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&cbv_));
        if (FAILED(hr)) {
            ABT("failed to create cbv resource: error:0x%x\n", hr);
        }
        

        /* SRV の場合は VIEW_DESC にアドレスを含めず、 CreateShaderResourceView() で関連付けたが CBV は VIEW_DESC に含める */
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvdesc1 = { cbv_->GetGPUVirtualAddress(), (sizeof(float) * 16 + 255) & ~255};
        uniq_.dev()->CreateConstantBufferView(&cbvdesc1, hdl);
        hdl.ptr += uniq_.sizeset().view;
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvdesc2 = { cbv_->GetGPUVirtualAddress() + 256, (sizeof(float) * 16 + 255) & ~255};
        uniq_.dev()->CreateConstantBufferView(&cbvdesc2, hdl);
        hdl.ptr += uniq_.sizeset().view;
        
        DirectX::XMMATRIX matrix[2] = {
            {{1.f, 0.f, 0.f, 0.25f}, {0.f, 1.f, 0.f, 0.0f}, {0.f, 0.f, 1.f, 0.0f}, {0.f, 0.f, 0.f, 1.0f}},
            {{1.f, 0.f, 0.f, -0.5f}, {0.f, 1.f, 0.f, 0.0f}, {0.f, 0.f, 1.f, 0.0f}, {0.f, 0.f, 0.f, 1.0f}},
        };
        float* ptr = nullptr;
        D3D12_RANGE readrange = {0, 0};
        cbv_->Map(0, &readrange, reinterpret_cast< void** >(&ptr));
        memcpy(ptr, matrix, sizeof(float) * 16);
        memcpy(reinterpret_cast< uint8_t* >(ptr) + ((sizeof(float)*16+255) & ~255), &matrix[1], sizeof(float) * 16);
        cbv_->Unmap(0, nullptr);
    }
    
    ComPtr< ID3D12Resource > trampoline;
    {
        /* trampoline buffer の作成: 
           トランポリンバッファは D3D12_HEAP_TYPE_UPLOAD (CPU 側 TLB に Map 可能). CPU から書き込み, COPY コマンドで VRAM の常駐ヒープにコピーする.
           bufsize は適当な大きさ. 必ずしもテクスチャと同じ大きさである必要はなく適当に 1MB とかでもよいが
           Mipmap や 3D texture の stride alignment などを考慮するのが面倒なので footprint をクエリする. */
        D3D12_RESOURCE_DESC tmp = {};
        tmp.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        tmp.MipLevels = 1;
        tmp.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        tmp.Width = 128;
        tmp.Height = 128;
        tmp.DepthOrArraySize = 1;
        tmp.SampleDesc = {1 /* count */, 0 /* quality */};
        tmp.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN; //D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        tmp.Flags = D3D12_RESOURCE_FLAG_NONE;

        size_t bufsize;
        uniq_.dev()->GetCopyableFootprints(&tmp, 0, 1, 0, nullptr, nullptr, nullptr, &bufsize);
        INF("create trampoline buffer for texture: required:%lld\n", bufsize);
        D3D12_HEAP_PROPERTIES upload = {
            D3D12_HEAP_TYPE_UPLOAD,
            D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
            D3D12_MEMORY_POOL_UNKNOWN,
            1, /* creation node mask */
            1 /* visible node mask */
        };

        D3D12_RESOURCE_DESC buf = {};
        buf.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buf.MipLevels = 1;
        buf.Format = DXGI_FORMAT_UNKNOWN;
        buf.Width = bufsize;
        buf.Height = 1;
        buf.DepthOrArraySize = 1;
        buf.SampleDesc = {1, 0};
        buf.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        buf.Flags = D3D12_RESOURCE_FLAG_NONE;

        auto hr = uniq_.dev()->CreateCommittedResource(&upload, D3D12_HEAP_FLAG_NONE, &buf, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&trampoline));
        if (FAILED(hr)) {
            ABT("failed to create trampoline buffer: err:0x%x\n", hr);
        }

    }

    ComPtr< ID3D12GraphicsCommandList > cmdlist;
    /* upload のための cmdlist には PSO は必要ではない */
    uniq_.dev()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, uploader_.allocator().Get(), nullptr/*pso_.Get()*/, IID_PPV_ARGS(&cmdlist));
    NAME_OBJ2(cmdlist, L"cmdlist(upload)");
    
    /* テクスチャデータのアップロード */
    {
        const int width = 64;
        const int height = 64;
        std::vector< uint32_t > data;
        data.reserve(width * height);
        for (int i = 0; i < width * height; ++ i)
            data.push_back(0xffffffff); /* ABGR */

        //tex_ = create_and_upload_texture(cmdlist.Get(), std::move(data), width, height, trampoline.Get());
        D3D12_RESOURCE_DESC desc = setup_tex2d(width, height);
        D3D12_HEAP_PROPERTIES resident = setup_heapprop(D3D12_HEAP_TYPE_DEFAULT);

        auto hr = uniq_.dev()->CreateCommittedResource(&resident, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex_));
        if (FAILED(hr)) {
            ABT("failed to create resident texture: err:0x%x\n", hr);
        }
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT copied = write_to_trampoline(std::move(data), desc, trampoline.Get()); /* copy to trampoline */
        issue_texture_upload(cmdlist.Get(), copied, tex_.Get(), trampoline.Get());
        
        D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = 1;
        /* 指定した SRV Heap に ID3D12Resource/D3D12_SHADER_RESOURCE_VIEW_DESC で指定した SRV を生成する */
        uniq_.dev()->CreateShaderResourceView(tex_.Get(), &srv, hdl);
    }

    cmdlist->Close();

    /* init GPU */

    flipper_.incr();
    flipper_.wait(uniq_.queue().Get());

    ID3D12CommandList* lists[] = {cmdlist.Get()};
    uploader_.queue()->ExecuteCommandLists(1, lists);
    flipper_.incr();
    flipper_.wait(uploader_.queue().Get());
}

void simple_t::produce_commands()
{
    const uint32_t idx = flipper_.idx();
    DBG("===> produce_commands: idx:%d\n", idx);
    /* bind or re-bind an allocator to a command list of the index  */
    auto hr = cmd_alloc_[idx]->Reset();
    if (FAILED(hr)) {
        /* faile to sync prev frame */
        ABT("failed to reset at idx:%d err:0x%x\n", idx, hr);
        DebugBreak();
    }

    hr = cmd_list_->Reset(cmd_alloc_[idx].Get(), pso_.Get());
    if (FAILED(hr)) {
        ABT("failed to reset cmdlist at idx:%d err:0x%x\n", idx, hr);
        DebugBreak();
    }
    
    cmd_list_->SetGraphicsRootSignature(rootsig_.Get());

    ID3D12DescriptorHeap* heaps[] = {descheap_.Get()};
    cmd_list_->SetDescriptorHeaps(1, heaps); /* all of DescriptorHeap(s)  */
    /* CBV Heap */
    D3D12_GPU_DESCRIPTOR_HANDLE gpuheap = descheap_->GetGPUDescriptorHandleForHeapStart();
    cmd_list_->SetGraphicsRootDescriptorTable(1 /* slot */, gpuheap); /* 間接参照用の Descriptor Table として GPU 空間のアドレスを指定する */
    gpuheap.ptr += (uniq_.sizeset().view * 2);
    /* SRV Heap */
    cmd_list_->SetGraphicsRootDescriptorTable(0 /* slot */, gpuheap); /* 間接参照用の Descriptor Table として GPU 空間のアドレスを指定する */
    
    cmd_list_->RSSetViewports(1, &viewport_);
    cmd_list_->RSSetScissorRects(1, &scissor_);


    D3D12_RESOURCE_BARRIER barrier_begin = {
        D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        D3D12_RESOURCE_BARRIER_FLAG_NONE,
        /* D3D12_RESOURCE_TRANSITION_BARRIER */
        { 
            rt_.rt(idx),
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            D3D12_RESOURCE_STATE_PRESENT,/* before state */
            D3D12_RESOURCE_STATE_RENDER_TARGET /* after state */
        }
    };

    cmd_list_->ResourceBarrier(1, &barrier_begin);

    /*
        RTV DescriptorHeap layout:
        +---------+ <= HeapStart 
        |  idx:0  | 
        +---------+
        |  idx:1  |    Desc 一つの大きさは uniq_.sizeset().rtv
        +---------+    dev_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV) の結果と同じ. (...と create_rtv_heap() で決めている)
                       内部的にはデバイスの alignment 要件に合わせて (sizeof(desc) + (alignment-1)) & ~(alignment-1) などしてるのだろう.
     */
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = {
        rt_.descriptor_heap()->GetCPUDescriptorHandleForHeapStart().ptr + uniq_.sizeset().rtv * idx
    };

    cmd_list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    /* recording draw commands */
    const float col[] = {0.1f, 0.1f, 0.1f, 1.f};
    cmd_list_->ClearRenderTargetView(rtv, col, 0, nullptr);
    cmd_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd_list_->IASetVertexBuffers(0, 1, &vbv_);
    cmd_list_->DrawInstanced(3, 1, 0, 0);
#if 1
    /* DescriptorHeap 上、 2 つ目の CBV を示すよう offset して drawcall を発行する */
    D3D12_GPU_DESCRIPTOR_HANDLE gpuhandle2 = {descheap_->GetGPUDescriptorHandleForHeapStart()};
    gpuhandle2.ptr += uniq_.sizeset().view;
    cmd_list_->SetGraphicsRootDescriptorTable(1 /* slot */, gpuhandle2); /* 間接参照用の Descriptor Table として GPU 空間のアドレスを指定する */

    cmd_list_->DrawInstanced(3, 1, 0, 0);
#endif  
    D3D12_RESOURCE_BARRIER barrier_end = {
        D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        D3D12_RESOURCE_BARRIER_FLAG_NONE,
        /* D3D12_RESOURCE_TRANSITION_BARRIER */
        { 
            rt_.rt(idx),
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            D3D12_RESOURCE_STATE_RENDER_TARGET,/* before state */
            D3D12_RESOURCE_STATE_PRESENT /* after state */
        }
    };

    cmd_list_->ResourceBarrier(1, &barrier_end);

    cmd_list_->Close();
    DBGMSG("     produce_commands --->\n");
}

ComPtr< ID3D12Resource > simple_t::create_texture(int width, int height)
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

D3D12_PLACED_SUBRESOURCE_FOOTPRINT simple_t::write_to_trampoline(std::vector< uint32_t > data, const D3D12_RESOURCE_DESC& texdesc, ID3D12Resource* trampoline)
{
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    size_t rowpitch;
    size_t totalbytes;
    uint32_t rows;
    uniq_.dev()->GetCopyableFootprints(&texdesc,
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

int simple_t::issue_texture_upload(ID3D12GraphicsCommandList* cmdlist, const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint, ID3D12Resource* tex, ID3D12Resource* trampoline)
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

extern "C" {
    int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int cmd);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int cmd)
{
    return runapp< simple_t >(1280, 720, instance, cmd);
}

