/**
 * (C) roentgen 
 * this code is licensed under the MIT License.
 */
#if !defined(SHADOW_HPP__)
#define SHADOW_HPP__

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
#include "scene.hpp"
#include "loading.hpp"

struct vertex_t {
    DirectX::XMFLOAT3 pos;
    DirectX::XMFLOAT2 uv;
    DirectX::XMFLOAT3 norm;
};

#define DYNAMIC_MODEL_MATRICIES (8)
#define VIEWPROJECTION_MATRICIES (2)
#define LIGHT_VIEWPROJECTION_MATRICIES (2)
#define CBV_DESC_NUM (DYNAMIC_MODEL_MATRICIES + VIEWPROJECTION_MATRICIES + LIGHT_VIEWPROJECTION_MATRICIES)
#define SHADOW_TEXTURES (1)
#define DYNAMIC_TEXTURES (8)
#define SRV_DESC_NUM (SHADOW_TEXTURES + DYNAMIC_TEXTURES)

class playground_t : public scene_t {
    Microsoft::WRL::ComPtr< ID3D12Resource > cbv2_;
    
    Microsoft::WRL::ComPtr< ID3D12RootSignature > rootsig_;
    Microsoft::WRL::ComPtr< ID3D12PipelineState > pso_;
    Microsoft::WRL::ComPtr< ID3D12DescriptorHeap > descheap_;

    D3D12_VERTEX_BUFFER_VIEW vbv_;
    Microsoft::WRL::ComPtr< ID3D12Resource > vertbuf_;
    Microsoft::WRL::ComPtr< ID3D12Resource > cbv_;

    D3D12_INDEX_BUFFER_VIEW ibv_;
    Microsoft::WRL::ComPtr< ID3D12DescriptorHeap > shadow_descheap_;
    Microsoft::WRL::ComPtr< ID3D12DescriptorHeap > scene_descheap_;
    Microsoft::WRL::ComPtr< ID3D12DescriptorHeap > nullsrv_descheap_;
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

    std::shared_ptr< std::vector< Microsoft::WRL::ComPtr< ID3D12Resource > > > payload_;
    std::atomic< bool > finished_;
public:
    playground_t() : finished_(false) {}

    void set_payload(uniq_device_t& u, std::shared_ptr< std::vector< Microsoft::WRL::ComPtr< ID3D12Resource > > > payload)
    {
        payload_ = std::move(payload);
        D3D12_CPU_DESCRIPTOR_HANDLE hdl = scene_descheap_->GetCPUDescriptorHandleForHeapStart();
        hdl.ptr += (u.sizeset().view * (CBV_DESC_NUM + SHADOW_TEXTURES)); 
        for (auto& item : *payload_) {
            D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
            srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srv.Texture2D.MipLevels = 1;
            /* 指定した SRV Heap に ID3D12Resource/D3D12_SHADER_RESOURCE_VIEW_DESC で指定した SRV を生成する */
            u.dev()->CreateShaderResourceView(item.Get(), &srv, hdl);
            hdl.ptr += u.sizeset().view;
        }
        for (int i = 0; i < DYNAMIC_TEXTURES - payload_->size(); i ++) {
            D3D12_SHADER_RESOURCE_VIEW_DESC unbound = {};
            unbound.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            unbound.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            unbound.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            unbound.Texture2D.MipLevels = 1;
            u.dev()->CreateShaderResourceView(nullptr, &unbound, hdl);
            hdl.ptr += u.sizeset().view;
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

    Microsoft::WRL::ComPtr< ID3D12PipelineState > get_pso() { return pso_; }
    Microsoft::WRL::ComPtr< ID3D12PipelineState > get_shadow_pso() { return shadow_pso_; }

    void set_viewport(float w, float h, float znear, float zfar);
};


class shadow_t : public appbase_t {
    static const int offscreen_buffers_ = 2;

    uniq_device_t uniq_;
    D3D12_VIEWPORT viewport_;
    D3D12_RECT scissor_;

    queue_sync_object_t< 2 > flipper_;
    /* swapchain への出力は ピクチャバッファのみなので
       (Z-buffer として使う場合は) depth/stencil buffer は一つでよい */
    buffered_render_target_t< 2 > rt_;
    buffered_render_target_t< 1 > dsv_;
    buffered_render_target_t< 1 > shadow_;
    asset_uploader_t uploader_;

    Microsoft::WRL::ComPtr< ID3D12CommandAllocator >  cmd_alloc_[offscreen_buffers_];
    Microsoft::WRL::ComPtr< ID3D12GraphicsCommandList > cmd_list_;

    Microsoft::WRL::ComPtr< ID3D12CommandAllocator >  pre_cmd_alloc_[offscreen_buffers_];
    Microsoft::WRL::ComPtr< ID3D12GraphicsCommandList > pre_cmd_list_;
    Microsoft::WRL::ComPtr< ID3D12CommandAllocator >  shadowpass_cmd_alloc_[offscreen_buffers_];
    Microsoft::WRL::ComPtr< ID3D12GraphicsCommandList > shadowpass_cmd_list_;

    std::shared_ptr< loading_t< playground_t > > loading_;
    std::shared_ptr< playground_t > playing_;
public:
    shadow_t(uint32_t w, uint32_t h) : appbase_t (w, h),
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

        using namespace Microsoft::WRL;
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

        D3D12_CLEAR_VALUE clearval = {};
        clearval.Format = DXGI_FORMAT_D32_FLOAT;
        clearval.DepthStencil.Depth = 1.0f;
        clearval.DepthStencil.Stencil = 0;

        /* シーンパスの(通常の)デプスバッファ */
        ComPtr< ID3D12Resource > dsv;
        auto heap = setup_heapprop(D3D12_HEAP_TYPE_DEFAULT);
        auto floattex = setup_tex2d(width, height, DXGI_FORMAT_D32_FLOAT, 1, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
        auto hr = uniq_.dev()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &floattex, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearval, IID_PPV_ARGS(&dsv));
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

        /* シャドウバッファ(デプスバッファレンダーターゲット)*/
        ComPtr< ID3D12Resource > shadow_rtv;
        auto shadowtex = setup_tex2d(1024, 1024, DXGI_FORMAT_R32_TYPELESS, 1, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

        uniq_.dev()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &shadowtex, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearval, IID_PPV_ARGS(&shadow_rtv));
        NAME_OBJ(shadow_rtv);

        ComPtr< ID3D12Resource > rtvs[] = {shadow_rtv};
        shadow_.init_dsv(uniq_, &desc, 1, rtvs); /* de*/
        
        /* シャドウパス用のコマンドバッファ */
        for (int i = 0; i < offscreen_buffers_; i ++) {
            uniq_.dev()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&shadowpass_cmd_alloc_[i]));
            NAME_OBJ_WITH_INDEXED(shadowpass_cmd_alloc_, i);
        }
        uniq_.dev()->CreateCommandList(0/* node mask for multi-graphics-cards */, D3D12_COMMAND_LIST_TYPE_DIRECT, shadowpass_cmd_alloc_[0].Get(), nullptr, IID_PPV_ARGS(&shadowpass_cmd_list_));
        NAME_OBJ(shadowpass_cmd_list_);
        shadowpass_cmd_list_->Close();

        /* メインの RenderTarget barrier 用のコマンドバッファ */
        for (int i = 0; i < offscreen_buffers_; i ++) {
            uniq_.dev()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&pre_cmd_alloc_[i]));
            NAME_OBJ_WITH_INDEXED(pre_cmd_alloc_, i);
        }
        uniq_.dev()->CreateCommandList(0/* node mask for multi-graphics-cards */, D3D12_COMMAND_LIST_TYPE_DIRECT, pre_cmd_alloc_[0].Get(), nullptr, IID_PPV_ARGS(&pre_cmd_list_));
        NAME_OBJ(pre_cmd_list_);
        pre_cmd_list_->Close();

        return 0;
    }
    
    void load_asset();

protected:

    Microsoft::WRL::ComPtr< ID3D12Resource > create_texture(int width, int height);

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
        if (loading_->is_ready()) {
            loading_->update(uniq_, freq);
        }
        else if (playing_->is_ready()) {
            playing_->update(uniq_, freq);
        }
    }
    
    void on_draw()
    {
        DBG("===> %s\n", TEXT(__FUNCTION__));
        try {
            const uint32_t idx = flipper_.idx();
            auto hr = cmd_alloc_[idx]->Reset();
            if (FAILED(hr)) {
                /* faile to sync prev frame */
                ABT("failed to reset at idx:%d err:0x%x\n", idx, hr);
                DebugBreak();
            }

            pre_cmd_alloc_[idx]->Reset();
            pre_cmd_list_->Reset(pre_cmd_alloc_[idx].Get(), nullptr);
            
            if (playing_->is_ready()) 
                cmd_list_->Reset(cmd_alloc_[idx].Get(), playing_->get_pso().Get());
            else 
                cmd_list_->Reset(cmd_alloc_[idx].Get(), loading_->get_pso().Get());

            /* 使うかどうかによらず、前回のサブミットから一度も Reset しないでサブミットするとエラーになる */
            shadowpass_cmd_alloc_[idx]->Reset();
            shadowpass_cmd_list_->Reset(shadowpass_cmd_alloc_[idx].Get(), playing_->get_shadow_pso().Get());

            const float col[] = {0.1f, 0.1f, 0.1f, 1.f};
            D3D12_RESOURCE_BARRIER barrier_begin = {
                D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, D3D12_RESOURCE_BARRIER_FLAG_NONE,
                {   rt_.rt(idx),
                    D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                    D3D12_RESOURCE_STATE_PRESENT,/* before state */
                    D3D12_RESOURCE_STATE_RENDER_TARGET /* after state */}};
            pre_cmd_list_->ResourceBarrier(1, &barrier_begin);
            
            pre_cmd_list_->Close();
            
            D3D12_CPU_DESCRIPTOR_HANDLE rtv = {rt_.descriptor_heap()->GetCPUDescriptorHandleForHeapStart().ptr + uniq_.sizeset().rtv * idx};
            D3D12_CPU_DESCRIPTOR_HANDLE dsv = {dsv_.descriptor_heap()->GetCPUDescriptorHandleForHeapStart()};
            /* recording draw commands */
            if (loading_->is_ready()) {
                INF("ready loading: %d\n", 1);
                cmd_list_->RSSetViewports(1, &viewport_);
                cmd_list_->RSSetScissorRects(1, &scissor_);

                cmd_list_->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
                cmd_list_->ClearRenderTargetView(rtv, col, 0, nullptr);
                cmd_list_->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);
            
                loading_->draw(uniq_, cmd_list_.Get());
            }
            else if (playing_->is_ready()) {
                INF("ready playing: %d\n", 1);
                // shadow pass
                D3D12_CPU_DESCRIPTOR_HANDLE shadow = {shadow_.descriptor_heap()->GetCPUDescriptorHandleForHeapStart().ptr + uniq_.sizeset().dsv * 0/*idx*/};
//#define CHECK_COLOR_RTV
#if defined(CHECK_COLOR_RTV)
                /* とりあえずどんなシャドウバッファができているか確認のため */
                shadowpass_cmd_list_->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
                const float col2[] = {0.7f, 0.7f, .7f, 1.f};
                shadowpass_cmd_list_->ClearRenderTargetView(rtv, col2, 0, nullptr);
                shadowpass_cmd_list_->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
#else
                shadowpass_cmd_list_->OMSetRenderTargets(0, nullptr, FALSE, &shadow);
#endif
                shadowpass_cmd_list_->ClearDepthStencilView(shadow, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
                
                D3D12_VIEWPORT vp = {0.f, 0.f, 1024.f, 1024.f, D3D12_MIN_DEPTH, D3D12_MAX_DEPTH};
                D3D12_RECT scissor = {0, 0, 1024, 1024};
                shadowpass_cmd_list_->RSSetViewports(1, &vp); /* viewport を大きくすると GPU が死ぬ */
                shadowpass_cmd_list_->RSSetScissorRects(1, &scissor);

                playing_->draw_shadowpass(uniq_, shadowpass_cmd_list_.Get());

                D3D12_RESOURCE_BARRIER mid = {
                    D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, D3D12_RESOURCE_BARRIER_FLAG_NONE,
                    {   shadow_.rt(0),
                        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                        D3D12_RESOURCE_STATE_DEPTH_WRITE,/* before state */
                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE /* after state */}};
                shadowpass_cmd_list_->ResourceBarrier(1, &mid);

#if !defined(CHECK_COLOR_RTV)
                cmd_list_->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
                cmd_list_->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);
                const float col3[] = {0.2f, 0.2f, 0.2f, 1.f};

                cmd_list_->ClearRenderTargetView(rtv, col3, 0, nullptr);
                
                cmd_list_->RSSetViewports(1, &viewport_);
                cmd_list_->RSSetScissorRects(1, &scissor_);

                playing_->draw(uniq_, cmd_list_.Get());
#endif
                D3D12_RESOURCE_BARRIER end = {
                    D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, D3D12_RESOURCE_BARRIER_FLAG_NONE,
                    {   shadow_.rt(0),
                        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,/* before state */
                        D3D12_RESOURCE_STATE_DEPTH_WRITE /* after state */}};
                cmd_list_->ResourceBarrier(1, &end);
            }
            
            D3D12_RESOURCE_BARRIER barrier_end = {
                D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, D3D12_RESOURCE_BARRIER_FLAG_NONE,
                {   rt_.rt(idx),
                    D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                    D3D12_RESOURCE_STATE_RENDER_TARGET,/* before state */
                    D3D12_RESOURCE_STATE_PRESENT /* after state */}};
            cmd_list_->ResourceBarrier(1, &barrier_end);

            shadowpass_cmd_list_->Close();
            cmd_list_->Close();

            ID3D12CommandList* lists[] = {pre_cmd_list_.Get(), shadowpass_cmd_list_.Get(), cmd_list_.Get()};
            uniq_.queue()->ExecuteCommandLists(std::extent< decltype(lists) >::value, lists);
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
        loading_->shutdown();
        flipper_.wait(uniq_.queue().Get());
    }

};

#endif
