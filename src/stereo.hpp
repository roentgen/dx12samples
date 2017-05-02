/**
 * (C) roentgen 
 * this code is licensed under the MIT License.
 */
#if !defined(STEREO_HPP__)
#define STEREO_HPP__

#include "win32appbase.hpp"
#include "serializer.hpp"
#include "dbgutils.hpp"
#include "uniq_device.hpp"
#include <comdef.h>
#include <vector>
#include <thread>
#include <future>
#include <atomic>
#include <memory>
#include <string>
#include <pix.h>
#include "scene.hpp"
#include "loading.hpp"

/* override USE_OVR configuration */
#define DUMMY_OVR

#if defined(USE_OVR)
#include <openvr.h>
#endif

struct vertex_t {
    DirectX::XMFLOAT3 pos;
    DirectX::XMFLOAT2 uv;
    DirectX::XMFLOAT3 norm;
};

struct plane_vertex_t { // no lighting 
    DirectX::XMFLOAT3 pos;
    DirectX::XMFLOAT2 uv;
};

#define VIEWPROJECTION_MATRICIES (2+1)
#define DYNAMIC_MODEL_MATRICIES (1)
#define CBV_DESC_NUM (DYNAMIC_MODEL_MATRICIES + VIEWPROJECTION_MATRICIES)

#define SHADOW_TEXTURES (1)
#define DYNAMIC_TEXTURES (8)
#define SRV_DESC_NUM (SHADOW_TEXTURES + DYNAMIC_TEXTURES)

//typedef std::tuple< Microsoft::WRL::ComPtr< ID3D12CommandAllocator >,  Microsoft::WRL::ComPtr< ID3D12GraphicsCommandList > > cmdset_t;
struct cmdset_t {
    Microsoft::WRL::ComPtr< ID3D12CommandAllocator > alloc;
    Microsoft::WRL::ComPtr< ID3D12GraphicsCommandList >  lst;
};

class playground_t : public scene_t {
    Microsoft::WRL::ComPtr< ID3D12RootSignature > rootsig_;
    Microsoft::WRL::ComPtr< ID3D12PipelineState > pso_;
    Microsoft::WRL::ComPtr< ID3D12DescriptorHeap > descheap_;

    D3D12_VERTEX_BUFFER_VIEW vbv_;
    Microsoft::WRL::ComPtr< ID3D12Resource > vertbuf_;
    Microsoft::WRL::ComPtr< ID3D12Resource > cbv_;

    Microsoft::WRL::ComPtr< ID3D12Resource > cbv2_;
    Microsoft::WRL::ComPtr< ID3D12Resource > patch_cbv_;

    D3D12_INDEX_BUFFER_VIEW ibv_;
    Microsoft::WRL::ComPtr< ID3D12DescriptorHeap > shadow_descheap_;
    Microsoft::WRL::ComPtr< ID3D12DescriptorHeap > scene_descheap_;
    Microsoft::WRL::ComPtr< ID3D12DescriptorHeap > patch_descheap_;
    Microsoft::WRL::ComPtr< ID3D12PipelineState > shadow_pso_;
    Microsoft::WRL::ComPtr< ID3D12RootSignature > shadow_rootsig_;
    Microsoft::WRL::ComPtr< ID3D12Resource > cube_vert_;
    Microsoft::WRL::ComPtr< ID3D12Resource > cube_vertupload_;
    Microsoft::WRL::ComPtr< ID3D12Resource > cube_idx_;
    Microsoft::WRL::ComPtr< ID3D12Resource > cube_idxupload_;

    D3D12_VERTEX_BUFFER_VIEW ground_vbv_;
    Microsoft::WRL::ComPtr< ID3D12Resource > ground_vert_;
    Microsoft::WRL::ComPtr< ID3D12Resource > ground_vertupload_;
    Microsoft::WRL::ComPtr< ID3D12Resource > ground_idx_;
    Microsoft::WRL::ComPtr< ID3D12Resource > ground_idxupload_;

    Microsoft::WRL::ComPtr< ID3D12CommandAllocator > bundle_alloc_;
    Microsoft::WRL::ComPtr< ID3D12GraphicsCommandList > bundle_;

    Microsoft::WRL::ComPtr< ID3D12CommandAllocator > shadow_bundle_alloc_;
    Microsoft::WRL::ComPtr< ID3D12GraphicsCommandList > shadow_bundle_;

    cmdset_t deferred_cmdset_;
    bool shadowpass_;
    std::shared_ptr< std::vector< Microsoft::WRL::ComPtr< ID3D12Resource > > > payload_;
    std::atomic< bool > finished_;
public:
    playground_t() : shadowpass_(false), finished_(false) {}

    void set_deferred_cmdset(cmdset_t cmdset)
    {
        deferred_cmdset_ = cmdset;
    }
    
    cmdset_t get_deferred_cmdset() { return deferred_cmdset_; }
    
    void set_payload(uniq_device_t& u, std::shared_ptr< std::vector< Microsoft::WRL::ComPtr< ID3D12Resource > > > payload)
    {
        INF("SetPayload:%d\n", 0);
        payload_ = std::move(payload);
        D3D12_CPU_DESCRIPTOR_HANDLE hdl = scene_descheap_->GetCPUDescriptorHandleForHeapStart();
        hdl.ptr += (u.sizeset().view * (CBV_DESC_NUM + SHADOW_TEXTURES));
        for (auto& item : *payload_) {
            D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
            srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srv.Texture2D.MipLevels = 1;
            {
                /* 指定した SRV Heap に ID3D12Resource/D3D12_SHADER_RESOURCE_VIEW_DESC で指定した SRV を生成する */
                u.dev()->CreateShaderResourceView(item.Get(), &srv, hdl);
                hdl.ptr += u.sizeset().view;
            }
        }
        for (int i = 0; i < DYNAMIC_TEXTURES - payload_->size(); i ++) {
            D3D12_SHADER_RESOURCE_VIEW_DESC unbound = {};
            unbound.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            unbound.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            unbound.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            unbound.Texture2D.MipLevels = 1;
            {
                u.dev()->CreateShaderResourceView(nullptr, &unbound, hdl);
                hdl.ptr += u.sizeset().view;
            }
        }
        finished_ = true;
    }

    void set_shadowtexture(uniq_device_t& u, ID3D12Resource* resc)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE hdl = scene_descheap_->GetCPUDescriptorHandleForHeapStart();
        D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Format = DXGI_FORMAT_R32_FLOAT;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = 1;
        /* 指定した SRV Heap に ID3D12Resource/D3D12_SHADER_RESOURCE_VIEW_DESC で指定した SRV を生成する */
        hdl.ptr += (u.sizeset().view * CBV_DESC_NUM);
        u.dev()->CreateShaderResourceView(resc, &srv, hdl);
    }

    void init(uniq_device_t& u, ID3D12GraphicsCommandList* cmdlist, const std::wstring&);
    void update(uniq_device_t& u, uint64_t freq);
    void draw(uniq_device_t& u, ID3D12GraphicsCommandList* cmdlist);
    void draw_shadowpass(uniq_device_t& u, ID3D12GraphicsCommandList* cmdlist);
    bool is_ready();
    Microsoft::WRL::ComPtr< ID3D12PipelineState > get_pso()
    {
        if (shadowpass_)
            return shadow_pso_;
        return pso_;
    }

    void set_viewport(float w, float h, float znear, float zfar);
    void set_shadowpass(bool shadowpass) { shadowpass_ = shadowpass; }
};

class stereo_t : public appbase_t {
    static const int offscreen_buffers_ = 2;

    uniq_device_t uniq_;
    D3D12_VIEWPORT viewport_;
    D3D12_RECT scissor_;

#if defined(USE_OVR)
    vr::IVRSystem* vrsystem_;
    vr::IVRRenderModels* rendermodels_;
    buffered_render_target_t< 4 > eye_;
    Microsoft::WRL::ComPtr< ID3D12CommandQueue > leftq_;
    Microsoft::WRL::ComPtr< ID3D12CommandQueue > rightq_;
    vr::D3D12TextureData_t leftrt_;
    vr::D3D12TextureData_t rightrt_;
#endif
    /* final stage 用 */
    Microsoft::WRL::ComPtr< ID3D12RootSignature > rootsig_;
    Microsoft::WRL::ComPtr< ID3D12PipelineState > pso_;
    Microsoft::WRL::ComPtr< ID3D12DescriptorHeap > descheap_;
    D3D12_VERTEX_BUFFER_VIEW vbv_;
    Microsoft::WRL::ComPtr< ID3D12Resource > vertbuf_;
    Microsoft::WRL::ComPtr< ID3D12Resource > cbv_;
    
    queue_sync_object_t< offscreen_buffers_ > flipper_;
    /* swapchain への出力は ピクチャバッファのみなので
       (Z-buffer として使う場合は) depth/stencil buffer は一つでよい */
    buffered_render_target_t< offscreen_buffers_ > rt_;
    buffered_render_target_t< 1 > dsv_;
    buffered_render_target_t< offscreen_buffers_ > shadow_;
    asset_uploader_t uploader_;

    Microsoft::WRL::ComPtr< ID3D12CommandAllocator >  cmd_alloc_[offscreen_buffers_];
    Microsoft::WRL::ComPtr< ID3D12GraphicsCommandList > cmd_list_;

    Microsoft::WRL::ComPtr< ID3D12CommandAllocator >  stereo_cmd_alloc_[offscreen_buffers_][2];
    Microsoft::WRL::ComPtr< ID3D12GraphicsCommandList > stereo_cmd_list_[2];

    Microsoft::WRL::ComPtr< ID3D12CommandAllocator >  pre_cmd_alloc_[offscreen_buffers_];
    Microsoft::WRL::ComPtr< ID3D12GraphicsCommandList > pre_cmd_list_;
    Microsoft::WRL::ComPtr< ID3D12CommandAllocator >  shadowpass_cmd_alloc_[offscreen_buffers_];
    Microsoft::WRL::ComPtr< ID3D12GraphicsCommandList > shadowpass_cmd_list_;
    Microsoft::WRL::ComPtr< ID3D12CommandAllocator >  finish_cmd_alloc_[offscreen_buffers_];
    Microsoft::WRL::ComPtr< ID3D12GraphicsCommandList > finish_cmd_list_;

    std::shared_ptr< loading_t< playground_t > > loading_;
    std::shared_ptr< playground_t > playing_;
public:
    stereo_t(uint32_t w, uint32_t h) : appbase_t (w, h),
                                       viewport_({0.f, 0.f, static_cast< float >(w), static_cast< float >(h), D3D12_MIN_DEPTH, D3D12_MAX_DEPTH}),
                                       scissor_({0, 0, static_cast< LONG >(w), static_cast< LONG >(h)})
    {
    }

    
    int init(HWND hwnd, int width, int height)
    {
        using namespace Microsoft::WRL;
        if (uniq_.init(hwnd, width, height, offscreen_buffers_) < 0) {
            ABT("failed to init uniq_device_t\n");
            return -1;
        }

        rt_.init(uniq_);

        bool use_fullscreen = false;
        auto hr = uniq_.swapchain()->SetFullscreenState(use_fullscreen, nullptr);
        if (FAILED(hr)) {
            ABT("set to fullscreen failed:0x%x\n", hr);
            DebugBreak();
        }

        D3D12_CLEAR_VALUE clearval = {};
        clearval.Format = DXGI_FORMAT_D32_FLOAT;
        clearval.DepthStencil.Depth = 1.0f;
        clearval.DepthStencil.Stencil = 0;

        Microsoft::WRL::ComPtr< ID3D12Resource > dsv;
        auto heap = setup_heapprop(D3D12_HEAP_TYPE_DEFAULT);
        auto floattex = setup_tex2d(width, height, DXGI_FORMAT_D32_FLOAT, 1, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
        hr = uniq_.dev()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &floattex, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearval, IID_PPV_ARGS(&dsv));
        if (FAILED(hr)) {
            ABT("create committed resource for dsv failed:0x%x\n", hr);
        }
        NAME_OBJ(dsv);
        
        D3D12_DEPTH_STENCIL_VIEW_DESC desc = {};
        desc.Format = DXGI_FORMAT_D32_FLOAT;
        desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        desc.Flags = D3D12_DSV_FLAG_NONE;

        ComPtr< ID3D12Resource > dsvs[] = {dsv};
        dsv_.init_dsv(uniq_, &desc, 1, dsvs);

        ComPtr< ID3D12Resource > shadow_rtv[2];
        auto shadowtex = setup_tex2d(1024, 1024, DXGI_FORMAT_R32_TYPELESS, 1, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

        for (int i = 0; i < 2; i ++) {
            uniq_.dev()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &shadowtex, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearval, IID_PPV_ARGS(&shadow_rtv[i]));
            NAME_OBJ_WITH_INDEXED(shadow_rtv, i);
        }
        ComPtr< ID3D12Resource > rtvs[] = {shadow_rtv[0], shadow_rtv[1]};
        shadow_.init_dsv(uniq_, &desc, 2, rtvs);
        
        ComPtr< ID3D12Fence > fence;
        uniq_.dev()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
        flipper_.init(std::move(fence), uniq_.swapchain()->GetCurrentBackBufferIndex());

        for (int i = 0; i < offscreen_buffers_; i ++) {
            uniq_.dev()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmd_alloc_[i]));
            NAME_OBJ_WITH_INDEXED(cmd_alloc_, i);
        }
        uniq_.dev()->CreateCommandList(0/* node mask for multi-graphics-cards */, D3D12_COMMAND_LIST_TYPE_DIRECT, cmd_alloc_[0].Get(), nullptr, IID_PPV_ARGS(&cmd_list_));
        NAME_OBJ(cmd_list_);
        cmd_list_->Close();

        for (int j = 0; j < 2; j ++) {
            for (int i = 0; i < offscreen_buffers_; i ++) {
                uniq_.dev()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&stereo_cmd_alloc_[i][j]));
            }
            uniq_.dev()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, stereo_cmd_alloc_[0][j].Get(), nullptr, IID_PPV_ARGS(&stereo_cmd_list_[j]));
            NAME_OBJ_WITH_INDEXED(stereo_cmd_list_, j);
            stereo_cmd_list_[j]->Close();
        }
        
        for (int i = 0; i < offscreen_buffers_; i ++) {
            uniq_.dev()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&shadowpass_cmd_alloc_[i]));
            NAME_OBJ_WITH_INDEXED(shadowpass_cmd_alloc_, i);
        }
        uniq_.dev()->CreateCommandList(0/* node mask for multi-graphics-cards */, D3D12_COMMAND_LIST_TYPE_DIRECT, shadowpass_cmd_alloc_[0].Get(), nullptr, IID_PPV_ARGS(&shadowpass_cmd_list_));
        NAME_OBJ(shadowpass_cmd_list_);
        shadowpass_cmd_list_->Close();

        for (int i = 0; i < offscreen_buffers_; i ++) {
            uniq_.dev()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&finish_cmd_alloc_[i]));
            NAME_OBJ_WITH_INDEXED(finish_cmd_alloc_, i);
        }
        uniq_.dev()->CreateCommandList(0/* node mask for multi-graphics-cards */, D3D12_COMMAND_LIST_TYPE_DIRECT, finish_cmd_alloc_[0].Get(), nullptr, IID_PPV_ARGS(&finish_cmd_list_));
        NAME_OBJ(finish_cmd_list_);
        finish_cmd_list_->Close();
        
        for (int i = 0; i < offscreen_buffers_; i ++) {
            uniq_.dev()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&pre_cmd_alloc_[i]));
            NAME_OBJ_WITH_INDEXED(pre_cmd_alloc_, i);
        }
        uniq_.dev()->CreateCommandList(0/* node mask for multi-graphics-cards */, D3D12_COMMAND_LIST_TYPE_DIRECT, pre_cmd_alloc_[0].Get(), nullptr, IID_PPV_ARGS(&pre_cmd_list_));
        NAME_OBJ(pre_cmd_list_);
        pre_cmd_list_->Close();

#if defined(USE_OVR)
        {
#if !defined(DUMMY_OVR)
            vr::EVRInitError hmderr;
            vrsystem_ = vr::VR_Init(&hmderr, vr::VRApplication_Scene);
            INF("hmderr:%d vrsystem_:%p\n", hmderr, vrsystem_);
            if (hmderr == vr::VRInitError_Init_PathRegistryNotFound) {
                ABT("Unable to init VR runtime: %s\n", vr::VR_GetVRInitErrorAsEnglishDescription(hmderr));
                ABTMSG("SteamVR Registry file could not located.\n");
            }
            else if (hmderr == vr::VRInitError_Init_HmdNotFound) {
                ABTMSG("HMD not found .\n");
            }
            rendermodels_ = (vr::IVRRenderModels *)vr::VR_GetGenericInterface(vr::IVRRenderModels_Version, &hmderr);
            if (!rendermodels_) {
                vrsystem_ = nullptr;
                vr::VR_Shutdown();
                ABT("Failed to get generic interface: %d\n", hmderr);
            }
#endif
            
            ComPtr< ID3D12Resource > ovr_rtv[offscreen_buffers_ * 2];
            auto eyetex = setup_tex2d(1280, 720, DXGI_FORMAT_R8G8B8A8_UNORM, 1, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
            for (int i = 0; i < offscreen_buffers_ * 2; i ++) {
                D3D12_CLEAR_VALUE cv = {DXGI_FORMAT_R8G8B8A8_UNORM, {0.6f, 0.6f, 0.6f, 1.f}};
                uniq_.dev()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &eyetex, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &cv, IID_PPV_ARGS(&ovr_rtv[i]));
                NAME_OBJ_WITH_INDEXED(ovr_rtv, i);
            }

            D3D12_RENDER_TARGET_VIEW_DESC desc = {DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RTV_DIMENSION_TEXTURE2D, {0, 0}};
            eye_.init_rtv(uniq_, &desc, offscreen_buffers_ * 2, ovr_rtv);
            leftq_ = uniq_.create_command_queue(D3D12_COMMAND_LIST_TYPE_DIRECT);
            rightq_ = uniq_.create_command_queue(D3D12_COMMAND_LIST_TYPE_DIRECT);
            leftrt_ = {ovr_rtv[0].Get(), leftq_.Get(), 0};
            rightrt_ = {ovr_rtv[2].Get(), rightq_.Get(), 0};
        }

#endif
        return 0;
    }
    
    void load_asset();

protected:

    Microsoft::WRL::ComPtr< ID3D12Resource > create_texture(int width, int height);

    /* 便利は便利だが実装の詳細を隠しすぎているので、プリミティブなサンプルで使うべきではない */
#if 0
    ComPtr< ID3D12Resource > create_and_upload_texture(ID3D12GraphicsCommandList* cmdlist, std::vector< uint32_t > data, int width, int height, ID3D12Resource* trampoline)
    {
        auto t = create_texture(width, height);
        if (t) {
            auto desc = t->GetDesc();
            auto copied_footprint = write_to_trampoline(uniq_, std::move(data), desc, trampoline);
            issue_texture_upload(cmdlist, copied_footprint, t.Get(), trampoline);
        }
        return t;
    }
#endif
    
    void on_init()
    {
        DBG("===> %s\n", TEXT(__FUNCTION__));
        init(win32_window_proc_t::get_handle(), get_width(), get_height());
        load_asset();
        DBG("     %s --->\n", TEXT(__FUNCTION__));
    }
    
    void on_update()
    {
        static uint64_t prev = 0ULL;
        uint64_t freq;
        uniq_.queue()->GetTimestampFrequency(&freq);
        if (prev == 0ULL) {
            prev = GetTickCount64();
        }
        uint64_t cur = GetTickCount64();
        freq = cur - prev;
        prev = cur;

        auto begin = get_clock();
        if (loading_->is_ready()) {
            loading_->update(uniq_, freq);
        }
        else if (playing_->is_ready()) {
            playing_->update(uniq_, freq);
        }
        auto end = get_clock();
        INF("update: took: %f(ms)\n", measurement(std::move(begin), std::move(end)));
    }

    bool on_resize(uint32_t w, uint32_t h)
    {
        INF("on_resize(%d, %d)\n", w, h);
        //flipper_.wait(uniq_.queue().Get());

        rt_.reset_buffers();

        DXGI_SWAP_CHAIN_DESC desc = {};
        uniq_.swapchain()->GetDesc(&desc);

        INF("swapchain: desc:0x%x\n", desc.Flags);
        auto hr = uniq_.swapchain()->ResizeBuffers(2, w, h, desc.BufferDesc.Format, desc.Flags);
        if (FAILED(hr)) {
            ABT("faield to resize buffers: 0x%x\n", hr);
        }
        
        rt_.rebind_swapchain(uniq_);
        return false;
    }
    
    void on_draw()
    {
        DBG("===> %s\n", TEXT(__FUNCTION__));
        try {
            const uint32_t idx = flipper_.idx();
            INF("idx:%d\n", idx);
            /* bind or re-bind an allocator to a command list of the index  */
            auto hr = cmd_alloc_[idx]->Reset();
            if (FAILED(hr)) {
                /* faile to sync prev frame */
                ABT("failed to reset at idx:%d err:0x%x\n", idx, hr);
                DebugBreak();
            }
            bool loading = loading_->is_ready();
            
            pre_cmd_alloc_[idx]->Reset();
        
            playing_->set_shadowpass(true);
            
            ID3D12PipelineState* pso = nullptr;
            if (loading) 
                pso = loading_->get_pso().Get();
            else 
                pso = playing_->get_pso().Get();
            
            hr = pre_cmd_list_->Reset(pre_cmd_alloc_[idx].Get(), nullptr);
            cmd_list_->Reset(cmd_alloc_[idx].Get(), pso);
            if (FAILED(hr)) {
                ABT("failed to reset cmdlist at idx:%d err:0x%x\n", idx, hr);
                DebugBreak();
            }
            cmd_list_->Close();

            /* FIXME: 最終段の RT 以外は一本化できる(USE_OVR は別)  */
            stereo_cmd_alloc_[idx][0]->Reset();
            stereo_cmd_alloc_[idx][1]->Reset();
            stereo_cmd_list_[0]->Reset(stereo_cmd_alloc_[idx][0].Get(), pso);
            stereo_cmd_list_[1]->Reset(stereo_cmd_alloc_[idx][1].Get(), pso);

            auto begin = get_clock();
#if defined(USE_OVR) && !defined(DUMMY_OVR)
            vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount];
            //vr::VRCompositor()->WaitGetPoses(poses, vr::k_unMaxTrackedDeviceCount, nullptr, 0);
            vr::VRCompositor()->WaitGetPoses(nullptr, 0, nullptr, 0);
#endif
            draw_stereo(idx, true);

            auto end = get_clock();
            INF("Build Command Buffer: took: %f(ms)\n", measurement(std::move(begin), std::move(end)));

            auto qbegin = get_clock();
            PIXBeginEvent(uniq_.queue().Get(), 0, L"execute command");
            ID3D12CommandList* list[] = {pre_cmd_list_.Get(), shadowpass_cmd_list_.Get(), stereo_cmd_list_[0].Get(), stereo_cmd_list_[1].Get(), finish_cmd_list_.Get()};
            uniq_.queue()->ExecuteCommandLists(std::extent< decltype(list) >::value, list);

#if defined(USE_OVR) && !defined(DUMMY_OVR)
            /* left/right に command queue を渡してはいるが、明示的に何かのコマンドを詰む必要はなさそうだ. */
            vr::D3D12TextureData_t left = {eye_.rt(0), leftq_.Get(), 0};
            vr::D3D12TextureData_t right = {eye_.rt(2), rightq_.Get(), 0};
            vr::VRTextureBounds_t bounds = {/* uvmin */0.f, 0.f, /* uvmax */1.f, 1.f};
            vr::Texture_t lt = {&left, vr::TextureType_DirectX12, vr::ColorSpace_Gamma};
            vr::Texture_t rt = {&right, vr::TextureType_DirectX12, vr::ColorSpace_Gamma};
            vr::VRCompositor()->Submit(vr::Eye_Left, &lt, &bounds, vr::Submit_Default);
            vr::VRCompositor()->Submit(vr::Eye_Right, &rt, &bounds, vr::Submit_Default);
#endif
                        
            PIXEndEvent(uniq_.queue().Get());
            auto qmid = get_clock();
            /* flip をするかしないかは任意. ただし GPU を待つ必要はある.
               つまり OpenVR は自分では GPU 同期をしないようだ. 
               アプリケーションでの ExecuteCommandLists() の呼び出しや同期と IVRCompositor::Submit との前後関係は不明.
               SwapChain での Flip をするかどうかはともかく、 SwapChain が同期するクロックが支配的ということになるので
               eye RT だけを double buffering する方法もおそらくなさそう -> eye RT は unbuffered とする */
#if 1
            /* non mandatory: HMD がないときに画面が真っ白になるので SideBySide を表示しておく. HMD が使えるときは必須ではない */
            if (!flipper_.flip(uniq_.queue().Get(), uniq_.swapchain().Get(), true)) {
                auto hr = uniq_.dev()->GetDeviceRemovedReason();
                ABT("flip failed due to error: 0x%x\n", hr);
                DebugBreak();
            }
#else
            flipper_.wait(uniq_.queue().Get());
#endif
            auto qend = get_clock();
            INF("Flip and sync: took: Submit:%f Wait:%f Total:%f(ms)\n", measurement(qbegin, qmid), measurement(qmid, qend), measurement(qbegin, qend));
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

    void draw_stereo(int idx, bool use_sidebyside)
    {
        //ID3D12Resource* p[] = {eye_.rt(0 + idx), eye_.rt(2 + idx) };
        ID3D12Resource* p[] = {eye_.rt(0), eye_.rt(2) };
        auto begin_cmd = [&](ID3D12GraphicsCommandList* cmd, int i) {
            D3D12_RESOURCE_BARRIER barrier_begin = {
                D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, D3D12_RESOURCE_BARRIER_FLAG_NONE,
                {   p[i],
                    D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,/* before state */
                    D3D12_RESOURCE_STATE_RENDER_TARGET /* after state */}};
            cmd->ResourceBarrier(1, &barrier_begin); /* barrier の desc は長くてつらいがこれを隠蔽したらサンプルの意味がない */
        };
        begin_cmd(pre_cmd_list_.Get(), 0);
        begin_cmd(pre_cmd_list_.Get(), 1);
        
        pre_cmd_list_->Close();
        
        //D3D12_CPU_DESCRIPTOR_HANDLE rtv[] = {{eye_.descriptor_heap()->GetCPUDescriptorHandleForHeapStart().ptr + uniq_.sizeset().rtv * idx},
        //                                   {eye_.descriptor_heap()->GetCPUDescriptorHandleForHeapStart().ptr + uniq_.sizeset().rtv * (2 + idx)}};
        D3D12_CPU_DESCRIPTOR_HANDLE rtv[] = {{eye_.descriptor_heap()->GetCPUDescriptorHandleForHeapStart().ptr},
                                             {eye_.descriptor_heap()->GetCPUDescriptorHandleForHeapStart().ptr + uniq_.sizeset().rtv * 2}};

        D3D12_CPU_DESCRIPTOR_HANDLE dsv = {dsv_.descriptor_heap()->GetCPUDescriptorHandleForHeapStart()};
        
        /* シャドウの有無によらずサンプリング完了バリア用コマンドバッファを Reset しておく */
        finish_cmd_alloc_[idx]->Reset();
        finish_cmd_list_->Reset(finish_cmd_alloc_[idx].Get(), nullptr);
        auto finishcmd = finish_cmd_list_.Get();
        /* 同様に shadow も Reset しておかないと Debug Layer がエラーをレポートする */
        shadowpass_cmd_alloc_[idx]->Reset();
        shadowpass_cmd_list_->Reset(shadowpass_cmd_alloc_[idx].Get(), nullptr);
        auto shadowpasscmd = shadowpass_cmd_list_.Get();
        
        const float col[] = {0.1f, 0.1f, 0.1f, 1.f};
        /* recording draw commands */
        if (loading_->is_ready()) {
            auto func = [&](ID3D12GraphicsCommandList* cmd, int i) {
                cmd->RSSetViewports(1, &viewport_);
                cmd->RSSetScissorRects(1, &scissor_);
                cmd->OMSetRenderTargets(1, &rtv[i], FALSE, nullptr);
                cmd->ClearRenderTargetView(rtv[i], col, 0, nullptr);
                cmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);
                loading_->draw(uniq_, cmd);
            };
            func(stereo_cmd_list_[0].Get(), 0);
            func(stereo_cmd_list_[1].Get(), 1);
        }
        else if (playing_->is_ready()) {
            D3D12_RESOURCE_BARRIER end = {
                D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, D3D12_RESOURCE_BARRIER_FLAG_NONE,
                {   shadow_.rt(0),
                    D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,/* before state */
                    D3D12_RESOURCE_STATE_DEPTH_WRITE}};
            finishcmd->ResourceBarrier(1, &end);
            
            // shadow pass
            D3D12_CPU_DESCRIPTOR_HANDLE shadow = {shadow_.descriptor_heap()->GetCPUDescriptorHandleForHeapStart().ptr};
            shadowpasscmd->OMSetRenderTargets(0, nullptr, FALSE, &shadow);
            shadowpasscmd->ClearDepthStencilView(shadow, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
            D3D12_VIEWPORT vp = {0.f, 0.f, 1024.f, 1024.f, D3D12_MIN_DEPTH, D3D12_MAX_DEPTH};
            D3D12_RECT scissor = {0, 0, 1024, 1024};
            shadowpasscmd->RSSetViewports(1, &vp); /* viewport を大きくする GPU が死ぬ */
            shadowpasscmd->RSSetScissorRects(1, &scissor);

            playing_->draw_shadowpass(uniq_, shadowpasscmd);

            D3D12_RESOURCE_BARRIER mid = {
                D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, D3D12_RESOURCE_BARRIER_FLAG_NONE,
                {   shadow_.rt(0),
                    D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                    D3D12_RESOURCE_STATE_DEPTH_WRITE,/* before state */
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE /* after state */}};
            shadowpasscmd->ResourceBarrier(1, &mid);

            playing_->set_shadowpass(false);
            
            // Scene Pass
            auto func = [&](ID3D12GraphicsCommandList* cmd, int i) {
                const float col3[] = {0.6f, 0.6f, 0.6f, 1.f};
                cmd->OMSetRenderTargets(1, &rtv[i], FALSE, &dsv);
                cmd->ClearRenderTargetView(rtv[i], col3, 0, nullptr);
                cmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);
                cmd->RSSetViewports(1, &viewport_);
                cmd->RSSetScissorRects(1, &scissor_);
                playing_->draw(uniq_, cmd);
            };
            func(stereo_cmd_list_[0].Get(), 0);
            func(stereo_cmd_list_[1].Get(), 1);
        }

        auto end_cmd = [&](ID3D12GraphicsCommandList* cmd, ID3D12Resource* rt) {
            D3D12_RESOURCE_BARRIER barrier_end = {
                D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, D3D12_RESOURCE_BARRIER_FLAG_NONE,
                {   rt,
                    D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                    D3D12_RESOURCE_STATE_RENDER_TARGET,/* before state */
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE /* after */
                }};
            cmd->ResourceBarrier(1, &barrier_end);
            cmd->Close();
        };

        end_cmd(stereo_cmd_list_[0].Get(), p[0]);
        end_cmd(stereo_cmd_list_[1].Get(), p[1]);

        shadowpasscmd->Close();

        /* Final Stage: (non mandatory) */
        if (use_sidebyside) {
            D3D12_CPU_DESCRIPTOR_HANDLE rtv0 = {rt_.descriptor_heap()->GetCPUDescriptorHandleForHeapStart().ptr + uniq_.sizeset().rtv * idx};
            draw_sidebyside(rtv0, idx, finishcmd);
        }
        
        finishcmd->Close();
    }
    
    void draw_sidebyside(const D3D12_CPU_DESCRIPTOR_HANDLE& rtv0, int idx, ID3D12GraphicsCommandList* cmd)
    {
        D3D12_RESOURCE_BARRIER begin = {
            D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, D3D12_RESOURCE_BARRIER_FLAG_NONE,
            {   rt_.rt(idx),
                D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                D3D12_RESOURCE_STATE_PRESENT,/* before state */
                D3D12_RESOURCE_STATE_RENDER_TARGET /* after state */}};
        ID3D12DescriptorHeap* heaps[] = {descheap_.Get()};
        cmd->SetDescriptorHeaps(1, heaps);
        cmd->SetPipelineState(pso_.Get());
        cmd->SetGraphicsRootSignature(rootsig_.Get());
        // CBV
        D3D12_GPU_DESCRIPTOR_HANDLE gpuheap = descheap_->GetGPUDescriptorHandleForHeapStart();
        cmd->SetGraphicsRootDescriptorTable(1, gpuheap);
        // SRV
        gpuheap.ptr += (uniq_.sizeset().view);
        cmd->SetGraphicsRootDescriptorTable(2, gpuheap);

        cmd->ResourceBarrier(1, &begin);
        /* 画面を完全に隠すので DepthStencilBuffer, および Clear は不要 */
        cmd->OMSetRenderTargets(1, &rtv0, FALSE, nullptr);
        //float col[] = {0.f, 0.f, 0.f, 1.f};
        //cmd->ClearRenderTargetView(rtv0, col, 0, nullptr);
        //cmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);
        
        cmd->RSSetViewports(1, &viewport_);
        cmd->RSSetScissorRects(1, &scissor_);

        //cmd->SetGraphicsRoot32BitConstant(0, idx, 1);
        cmd->SetGraphicsRoot32BitConstant(0, 0, 1);
        
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        cmd->IASetVertexBuffers(0, 1, &vbv_);
        cmd->SetGraphicsRoot32BitConstant(0, 0, 0);
        cmd->DrawInstanced(4, 1, 0, 0);
        cmd->SetGraphicsRoot32BitConstant(0, 1, 0);
        cmd->DrawInstanced(4, 1, 0, 0);

        D3D12_RESOURCE_BARRIER end = {
            D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, D3D12_RESOURCE_BARRIER_FLAG_NONE,
            {   rt_.rt(idx),
                D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                D3D12_RESOURCE_STATE_RENDER_TARGET,/* before state */
                D3D12_RESOURCE_STATE_PRESENT /* after state */}};
        cmd->ResourceBarrier(1, &end);
    }

    void on_final()
    {
#if !defined(DUMMY_OVR)
        vr::VRCompositor()->WaitGetPoses(nullptr, 0, nullptr, 0);
        vr::VR_Shutdown();
#endif

        uniq_.swapchain()->SetFullscreenState(false, nullptr);
        loading_->shutdown();
        flipper_.wait(uniq_.queue().Get());
    }

    void create_root_signature(uniq_device_t& u);
    void create_pipeline_state(uniq_device_t& u, const std::wstring& dir);
};

#endif
