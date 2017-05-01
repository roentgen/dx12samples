/**
 * (C) roentgen 
 * this code is licensed under the MIT License.
 */
#if !defined(SIMPLE_HPP__)
#define SIMPLE_HPP__

#include "win32appbase.hpp"
#include "serializer.hpp"
#include "dbgutils.hpp"
#include "uniq_device.hpp"
#include <comdef.h>
#include <vector>

struct vertex_t {
    DirectX::XMFLOAT3 pos;
    DirectX::XMFLOAT2 uv;
};

class simple_t : public appbase_t {
    static const int offscreen_buffers_ = 2;

    uniq_device_t uniq_;
    D3D12_VIEWPORT viewport_;
    D3D12_RECT scissor_;

    queue_sync_object_t< 2 > flipper_;
    buffered_render_target_t< 2 > rt_;
    asset_uploader_t uploader_;

    Microsoft::WRL::ComPtr< ID3D12CommandAllocator >  cmd_alloc_[offscreen_buffers_];
    Microsoft::WRL::ComPtr< ID3D12GraphicsCommandList > cmd_list_;
    
    Microsoft::WRL::ComPtr< ID3D12RootSignature > rootsig_;
    Microsoft::WRL::ComPtr< ID3D12PipelineState > pso_;
    Microsoft::WRL::ComPtr< ID3D12DescriptorHeap > descheap_;

    Microsoft::WRL::ComPtr< ID3D12Resource > trampoline_;
    D3D12_VERTEX_BUFFER_VIEW vbv_;
    Microsoft::WRL::ComPtr< ID3D12Resource > vertbuf_;
    Microsoft::WRL::ComPtr< ID3D12Resource > tex_;
    Microsoft::WRL::ComPtr< ID3D12Resource > cbv_;
public:
    simple_t(uint32_t w, uint32_t h) : appbase_t (w, h),
                                       viewport_({0.f, 0.f, static_cast< float >(w), static_cast< float >(h), D3D12_MIN_DEPTH, D3D12_MAX_DEPTH}),
                                       scissor_({0, 0, static_cast< LONG >(w), static_cast< LONG >(h)})
    {
    }

    
    int init(HWND hwnd, int width, int height)
    {
        if (uniq_.init(hwnd, width, height, offscreen_buffers_) < 0) {
            ABT("failed to init uniq_device_t\n");
            return -1;
        }
        rt_.init(uniq_);

        Microsoft::WRL::ComPtr< ID3D12Fence > fence;
        uniq_.dev()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
        flipper_.init(std::move(fence), uniq_.swapchain()->GetCurrentBackBufferIndex());

        for (int i = 0; i < offscreen_buffers_; i ++) {
            uniq_.dev()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmd_alloc_[i]));
            NAME_OBJ_WITH_INDEXED(cmd_alloc_, i);
        }
        uniq_.dev()->CreateCommandList(0/* node mask for multi-graphics-cards */, D3D12_COMMAND_LIST_TYPE_DIRECT, cmd_alloc_[0].Get(), nullptr, IID_PPV_ARGS(&cmd_list_));
        NAME_OBJ(cmd_list_);
        cmd_list_->Close();
        return 0;
    }
    
    void load_asset();

    void produce_commands();

protected:

    Microsoft::WRL::ComPtr< ID3D12Resource > create_texture(int width, int height);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT write_to_trampoline(std::vector< uint32_t > data, const D3D12_RESOURCE_DESC& texdesc, ID3D12Resource* trampoline);
    int issue_texture_upload(ID3D12GraphicsCommandList* cmdlist, const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& foorprint, ID3D12Resource* tex, ID3D12Resource* trampoline);

    /* 便利は便利だが実装の詳細を隠しすぎているので、プリミティブなサンプルで使うべきではない */
    Microsoft::WRL::ComPtr< ID3D12Resource > create_and_upload_texture(ID3D12GraphicsCommandList* cmdlist, std::vector< uint32_t > data, int width, int height, ID3D12Resource* trampoline)
    {
        auto t = create_texture(width, height);
        if (t) {
            auto desc = t->GetDesc();
            auto copied_footprint = write_to_trampoline(std::move(data), desc, trampoline);
            issue_texture_upload(cmdlist, copied_footprint, t.Get(), trampoline);
        }
        return t;
    }

    void on_init()
    {
        DBG("===> %s\n", TEXT(__FUNCTION__));
        init(win32_window_proc_t::get_handle(), get_width(), get_height());
        load_asset();
        DBG("     %s --->\n", TEXT(__FUNCTION__));
    }
    
    void on_update()
    {
    }
    
    void on_draw()
    {
        DBG("===> %s\n", TEXT(__FUNCTION__));
        try {
            produce_commands();
            ID3D12CommandList* lists[] = {cmd_list_.Get()};
            uniq_.queue()->ExecuteCommandLists(1, lists);
            if (!flipper_.flip(uniq_.queue().Get(), uniq_.swapchain().Get())) {
                auto hr = uniq_.dev()->GetDeviceRemovedReason();
                ABT("flip failed due to error: 0x%x\n", hr);
            }
        }
        catch (_com_error& ex) {
            ABT("_com_error was occured\n", TEXT(__FUNCTION__));
            DebugBreak();
        }
        catch (...) {
            ABT("exception was occured\n", TEXT(__FUNCTION__));
        }
        DBG("     %s --->\n", TEXT(__FUNCTION__));
    }
    
    void on_final()
    {
        flipper_.wait(uniq_.queue().Get());
    }

    /* root signature/pipeline state object はシーンに応じて DescriptorHeap や roootparameter のレイアウトを決定する.
       従ってアセットをメモリに読んでから決定する.
       (当然、アップロードに使う commandlist/allocator よりも生存期間は長い.
       アップロードするだけでも CommandList の PSO のために rootsig が要る)
     */
    int create_root_signature(uniq_device_t& u)
    {
        D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
        if (FAILED(u.dev()->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData)))) {
            INF("root signature version 1.0\n");
            featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0; /* win10 anniv (2016/5) まではこっち */
        }

        D3D12_DESCRIPTOR_RANGE1 ranges[4];
        ranges[0] = create_range(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
                                 1 /* descriptors */, 0 /* base-register index */, 0 /* space */,
                                 D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

        ranges[1] = create_range(D3D12_DESCRIPTOR_RANGE_TYPE_CBV,
                                 1 /* descriptors */, 0 /* base-register index */, 0 /* space */,
                                 D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

        ranges[2] = create_range(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);
        ranges[3] = create_range(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 2, 0);

        D3D12_ROOT_DESCRIPTOR_TABLE1 rdt[] = {
            {1, ranges},
            {1, ranges + 1},
            {1, ranges + 2},
            {1, ranges + 3},
        };
        
        D3D12_ROOT_PARAMETER1 params[] = {
            create_root_param(rdt    , D3D12_SHADER_VISIBILITY_PIXEL),
            create_root_param(rdt + 1, D3D12_SHADER_VISIBILITY_VERTEX),
            //create_root_param(rdt + 2, D3D12_SHADER_VISIBILITY_PIXEL),
            //create_root_param(rdt + 3, D3D12_SHADER_VISIBILITY_PIXEL),
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
            return -1;
        }
        NAME_OBJ(rootsig_);
        return 0;
    }

};

#endif
