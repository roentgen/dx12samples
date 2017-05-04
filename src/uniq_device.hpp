/**
 * (C) roentgen 
 * this code is licensed under the MIT License.
 */
#if !defined(UNIQ_DEVICE_HPP__)
#define UNIQ_DEVICE_HPP__

#include "stdafx.h"
#include "dbgutils.hpp"
#include <comdef.h>
#include <vector>
#include <algorithm>
#include <dxgi1_5.h>
#if defined(USE_OVR)
#include <openvr.h>
#else
namespace vr {
	typedef void* IVRSystem;
}
#endif

struct desc_size_set_t {
    uint32_t view;
    uint32_t sampler;
    uint32_t rtv;
    uint32_t dsv;
};

inline LARGE_INTEGER get_clock()
{
    LARGE_INTEGER i = {};
    QueryPerformanceCounter(&i);
    return i;
}

inline double measurement(LARGE_INTEGER b, LARGE_INTEGER e)
{
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    double ms = double(e.QuadPart - b.QuadPart) / f.QuadPart * 1000;
    return ms;
}

class uniq_device_t {
    Microsoft::WRL::ComPtr< ID3D12Device > dev_;
    Microsoft::WRL::ComPtr< IDXGISwapChain3 > swap_chain_;
    Microsoft::WRL::ComPtr< ID3D12CommandQueue > queue_;
    Microsoft::WRL::ComPtr< IDXGIAdapter3 > adapter_;
    desc_size_set_t sizeset_;
	vr::IVRSystem* vrsystem_;
public:
    struct frame_resource_t {
    };

    int init(HWND hwnd, int width, int height, int fn, vr::IVRSystem* vrsys = nullptr, bool use_warp = false)
    {
		vrsystem_ = vrsys;
        using Microsoft::WRL::ComPtr;
        uint32_t flags = 0;
        INF("uniq_device_t::init() hwnd:0x%x width:%d height:%d fn:%d use_warp:%d\n", hwnd, width, height, fn, use_warp);
#if defined(_DEBUG)
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
            /* Graphics Tools のオプション機能を使うのにデバッグレイヤを有効化する.
               これは実際のデバイスを生成する前にやる必要がある。 */
            debug->EnableDebugLayer();
            flags |= DXGI_CREATE_FACTORY_DEBUG;
        }
#endif
        ComPtr< IDXGIFactory5 > factory;
        auto hr = CreateDXGIFactory2(flags, IID_PPV_ARGS(&factory));
        if (FAILED(hr)) {
            ABT("failed to create DXGI factory: err:0x%x\n", hr);
            return -1;
        }
        BOOL tearing = FALSE;
        factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(BOOL));
        INF("Feature: DXGI_FEATURE_PRESENT_ALLOW_TEARING: %d\n", tearing);

#if 1
        ComPtr< IDXGIAdapter3 > adapter;
        if (!use_warp) {
            /* default adapter */
            ComPtr< ID3D12Device > defaultdev;
            LUID luid = {};
            hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&defaultdev));
            if (SUCCEEDED(hr)) {
                luid = defaultdev->GetAdapterLuid();
                INF("default adapter luid: %x:%x\n", luid.LowPart, luid.HighPart);
            }
            else if (SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&defaultdev)))) {
                luid = defaultdev->GetAdapterLuid();
                INF("default adapter luid: %x:%x\n", luid.LowPart, luid.HighPart);
            }

            factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter));
            DXGI_QUERY_VIDEO_MEMORY_INFO meminfo = {};
            adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &meminfo);
            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, _uuidof(ID3D12Device), nullptr))) {
            }
            adapter_ = adapter;
        }
        else {
            factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter));
        }
#else
        ComPtr< IDXGIAdapter1 > adapter;
        if (!use_warp) {
            bool found = false;
            for (int i = 0; DXGI_ERROR_NOT_FOUND != factory->EnumAdapters1(i, &adapter); i ++) {
                DXGI_ADAPTER_DESC1 desc;
                adapter->GetDesc1(&desc);
                if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) 
                    continue;
                /* try one 試すだけ */
                if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr))) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                ABTMSG("no found device\n");
                return -1; /* no found suitable device */
            }
        }
        else {
            factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter));
        }
#endif
        /* actually be created: 実際に利用可能になる  */
        hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev_));
        if (FAILED(hr)) {
            ABT("failed to create device: err:0x%x\n", hr);
        }

        D3D12_FEATURE_DATA_ARCHITECTURE arch = {};
        D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT gpummu = {};
        D3D12_FEATURE_DATA_D3D12_OPTIONS opt = {};
        dev_->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE, &arch, sizeof(arch));
        dev_->CheckFeatureSupport(D3D12_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT, &gpummu, sizeof(gpummu));
        dev_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &opt, sizeof(opt));
        INF("Feature: D3D12_FEATURE_D3D12_OPTIONS: TiledResourcesTier:%d ResourceBindingTier:%d ConservativeRasterizationTier:%d ResourceHeapTier:%d\n", opt.TiledResourcesTier, opt.ResourceBindingTier, opt.ConservativeRasterizationTier, opt.ResourceHeapTier);
        INF("Feature: D3D12_FEATURE_ARCHITECTURE: UMA:%d CacheCoherency:%d TileRenderer:%d\n", arch.UMA, arch.CacheCoherentUMA, arch.TileBasedRenderer);
        INF("Feature: D3D12_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT: resource:0x%x process:0x%x\n", gpummu.MaxGPUVirtualAddressBitsPerResource, gpummu.MaxGPUVirtualAddressBitsPerProcess);
        
        /* create the command queue */
        queue_ = create_command_queue(D3D12_COMMAND_LIST_TYPE_DIRECT);
        
        /* create the swapchain : HWND が必要. ここから RTV の Resource を得る.
           init() 自体を分けてもよいが factory はまだ使うので GetInterface し直しになる.
           (メンバで保持するのは DXGISwapChain3 だが直接作るのは DXGISwapChain1 でよい
         */
        {
            DXGI_SWAP_CHAIN_DESC1 desc = {};
            desc.BufferCount = fn;
            desc.Width = width;
            desc.Height = height;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; /* RGBA32. 各チャンネル 255 までの整数 */
            desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; 
            desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            desc.SampleDesc.Count = 1; /* とりあえず Pixel by Pixel */
            desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
            ComPtr< IDXGISwapChain1 > sw;
            factory->CreateSwapChainForHwnd(queue_.Get(), hwnd, &desc, nullptr, nullptr, &sw);
            factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER); /* windowed only */
            sw.As(&swap_chain_); /* set to member as '3' */
        }

        sizeset_ = {
            dev_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV),
            dev_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER),
            dev_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV),
            dev_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV)
        };
        return 0;
    }

    inline Microsoft::WRL::ComPtr< ID3D12CommandQueue > create_command_queue(D3D12_COMMAND_LIST_TYPE type)
    {
        /* create the command queue */
        D3D12_COMMAND_QUEUE_DESC desc = {};
        desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        desc.Type = type;
        Microsoft::WRL::ComPtr< ID3D12CommandQueue > q;
        dev_->CreateCommandQueue(&desc, IID_PPV_ARGS(&q));
        return q;
    }
    
    inline Microsoft::WRL::ComPtr< ID3D12Device >& dev() { return dev_; }
    inline Microsoft::WRL::ComPtr< IDXGISwapChain3 >& swapchain() { return swap_chain_; }
    inline Microsoft::WRL::ComPtr< ID3D12CommandQueue >& queue() { return queue_; }
    inline Microsoft::WRL::ComPtr< IDXGIAdapter3 >& adapter() { return adapter_; }
    inline const desc_size_set_t& sizeset() const { return sizeset_; }
	inline vr::IVRSystem* vrsystem() { return vrsystem_; }
};

template < int Frames >
class queue_sync_object_t {
    HANDLE sync_;
    uint32_t idx_;
    Microsoft::WRL::ComPtr< ID3D12Fence > fence_;
    uint64_t values_[Frames];
public:
    queue_sync_object_t() : sync_(nullptr), idx_(0), fence_(), values_{} {}
    
    int init(Microsoft::WRL::ComPtr< ID3D12Fence > fence, uint32_t idx)
    {
        fence_ = fence;
        idx_ = idx;
        sync_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!sync_) {
            ABT("failed to create event for queue_sync_object_t: err:0x%x\n", GetLastError());
            return -1;
        }
        return 0;
    }

    ~queue_sync_object_t() { if (sync_) CloseHandle(sync_); }

    void incr() { values_[idx_] ++; }
    
    bool flip(ID3D12CommandQueue* q, IDXGISwapChain3* chain, bool nosync = false)
    {
        HRESULT hr;
        if (nosync) {
            BOOL fs = FALSE;
            chain->GetFullscreenState(&fs, nullptr);
            if (fs) {
                /* fullscreen のときは tearing モードは使えない. vsync=0 で充分 */
                hr = chain->Present(0, 0);
            }
            else {
                /* window のときは swapchain が許可している場合のみ tearing を使う */
                DXGI_SWAP_CHAIN_DESC desc = {};
                chain->GetDesc(&desc);
                hr = chain->Present(0, (desc.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) ? DXGI_PRESENT_ALLOW_TEARING : 0);
            }
        }
        else {
            hr = chain->Present(1, 0);
        }
        
        if (FAILED(hr)) {
            ABT("failed to Present: err:0x%x\n", hr);
            return false;
        }
        int previdx = idx_;
        const uint64_t cur = values_[idx_];
        //INF("flip():%p backbuffer:%d store value:%ld\n", this, idx_, cur);

        auto b = get_clock();
        q->Signal(fence_.Get(), cur); /* cur の値を fence に書き込んで queue に書き込んでおく */
        idx_ = chain->GetCurrentBackBufferIndex(); /* FIFO 内部の index を更新: idx_ -> 次に使う backbuffer */
        const uint64_t prev = values_[idx_];
        /* overlap していないことを確認:
           fence 値を読み出して評価する.
           次のフェンス完了の値が予定の値(前回の Signal() 後にインクリメントした値)に到達していなければ
           以前のフレーム(DoubleBuffering なら前の前のフレーム)が未完了として sync object で CPU を同期する(つまり CPU を待たせる).
         */
        uint64_t fence_value = fence_->GetCompletedValue();
        uint64_t after = 0ULL;
        if (prev - fence_value >= Frames - 1) {
            fence_->SetEventOnCompletion(prev, sync_);
            WaitForSingleObjectEx(sync_, INFINITE, FALSE);
            after = fence_->GetCompletedValue();
        }
        auto e = get_clock();
        //INF("flip():%p wait fence completion:%ld (waited after:%ld) idx:%d next:%ld time:%f(ms)\n", this, fence_value, after, idx_, cur + 1, measurement(b, e));
        
        values_[idx_] = cur + 1; /* 次に fence が到達するはずの値 */
        return true;
    }

    /* fence 値の確認とバックバッファの flip をせず、強制的に fence への到達まで CPU を待たせる */
    void wait(ID3D12CommandQueue* q)
    {
        //INF("wait:%p idx:%d value:{%ld, %ld}\n", this, idx_, values_[0], values_[1]);
        auto b = get_clock();
        q->Signal(fence_.Get(), values_[idx_]);
        fence_->SetEventOnCompletion(values_[idx_], sync_);
        WaitForSingleObjectEx(sync_, INFINITE, FALSE);
        auto e = get_clock();
        //INF("wait:%p took %f(ms) for:%ld after:%ld\n", this, measurement(b, e), values_[idx_], fence_->GetCompletedValue());
        values_[idx_] ++;
    }

    inline uint32_t idx() const { return idx_; }
};

template < int NUM >
struct render_buffer_t {
    Microsoft::WRL::ComPtr< ID3D12Resource > buf_[NUM];
    Microsoft::WRL::ComPtr< ID3D12DescriptorHeap > descheap_;
public:
    template< D3D12_DESCRIPTOR_HEAP_TYPE TYPE >
    int create_desc_heap(uniq_device_t& u)
    {
        Microsoft::WRL::ComPtr< ID3D12DescriptorHeap > heap;
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.NumDescriptors = NUM;
        desc.Type = TYPE;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        u.dev()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descheap_));
        return 0;
    }

    void create_rtv_from_swapchain(uniq_device_t& u)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE hdl = descheap_->GetCPUDescriptorHandleForHeapStart();
        for (int i = 0; i < NUM; i ++) {
            u.swapchain()->GetBuffer(i, IID_PPV_ARGS(&buf_[i]));
            u.dev()->CreateRenderTargetView(buf_[i].Get(), nullptr, hdl);
            hdl.ptr += u.sizeset().rtv;
        }
    }

    void create_rtv(uniq_device_t& u, const D3D12_RENDER_TARGET_VIEW_DESC* viewdesc = nullptr)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE hdl = descheap_->GetCPUDescriptorHandleForHeapStart();
        for (int i = 0; i < NUM; i ++) {
            u.dev()->CreateRenderTargetView(buf_[i].Get(), viewdesc, hdl);
            hdl.ptr += u.sizeset().rtv;
        }
    }

    void create_dsv(uniq_device_t& u, const D3D12_DEPTH_STENCIL_VIEW_DESC* viewdesc = nullptr)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE hdl = descheap_->GetCPUDescriptorHandleForHeapStart();
        for (int i = 0; i < NUM; i ++) {
            INF("create_dsv: [%d]: buf:%p\n", i, buf_[i].Get());
            u.dev()->CreateDepthStencilView(buf_[i].Get(), viewdesc, hdl);
            hdl.ptr += u.sizeset().dsv;
        }
    }

    Microsoft::WRL::ComPtr< ID3D12Resource > set_resource(int idx, Microsoft::WRL::ComPtr< ID3D12Resource > resc)
    {
        INF("render_buffer_t< %d >::set_resource(): idx:%d resc:0x%p\n", NUM, idx, resc.Get());
        if (idx < NUM) {
            auto prev = buf_[idx];
            buf_[idx] = resc;
            return prev;
        }
        return nullptr;
    }

    void reset_resource(int idx)
    {
        buf_[idx].Reset();
    }
    static int get_buffer_num() { return NUM; }
};


template < int Frames >
class buffered_render_target_t {
    render_buffer_t< Frames > buf_;
public:

    int init(uniq_device_t& u) { return init_from_swapchain(u); }
    
    int init_from_swapchain(uniq_device_t& u)
    {
        buf_.create_desc_heap< D3D12_DESCRIPTOR_HEAP_TYPE_RTV >(u);
        buf_.create_rtv_from_swapchain(u);
        return 0;
    }

    int reset_buffers()
    {
        for (int i = 0; i < Frames; i ++) 
            buf_.reset_resource(i);
        return 0;
    }

    int rebind_swapchain(uniq_device_t& u)
    {
        buf_.create_rtv_from_swapchain(u);
        return 0;
    }
        
    
    int init_rtv(uniq_device_t& u, const D3D12_RENDER_TARGET_VIEW_DESC* desc, int n, Microsoft::WRL::ComPtr< ID3D12Resource >* resources)
    {
        buf_.create_desc_heap< D3D12_DESCRIPTOR_HEAP_TYPE_RTV >(u);
        for (int i = 0; i < std::min< int >(n, buf_.get_buffer_num()); i ++) 
            buf_.set_resource(i, resources[i]);
        buf_.create_rtv(u, desc);
        return 0;
    }

    int init_dsv(uniq_device_t& u, const D3D12_DEPTH_STENCIL_VIEW_DESC* desc, int n, Microsoft::WRL::ComPtr< ID3D12Resource >* resources)
    {
        buf_.create_desc_heap< D3D12_DESCRIPTOR_HEAP_TYPE_DSV >(u);
        for (int i = 0; i < std::min< int >(n, buf_.get_buffer_num()); i ++) {
            INF("init_dsv:[%d]: resc:0x%p\n", i, resources[i].Get());
            buf_.set_resource(i, resources[i]);
        }
        buf_.create_dsv(u, desc);
        return 0;
    }

    inline ID3D12DescriptorHeap* descriptor_heap() { return buf_.descheap_.Get(); };
    inline ID3D12Resource* rt(uint32_t idx) { if (idx >= Frames) return nullptr; return buf_.buf_[idx].Get(); }
};

inline D3D12_DESCRIPTOR_RANGE1 create_range(D3D12_DESCRIPTOR_RANGE_TYPE t,
                                            uint32_t descs, uint32_t basereg, uint32_t regspace = 0,
                                            D3D12_DESCRIPTOR_RANGE_FLAGS flag = D3D12_DESCRIPTOR_RANGE_FLAG_NONE,
                                            uint32_t offset = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND)
{
    D3D12_DESCRIPTOR_RANGE1 r = {};
    r.RangeType = t;
    r.NumDescriptors = descs;
    r.BaseShaderRegister = basereg;
    r.RegisterSpace = regspace;
    r.Flags = flag;
    r.OffsetInDescriptorsFromTableStart = offset;
    return r;
}

/*
typedef enum D3D12_ROOT_PARAMETER_TYPE { 
    D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE  = 0,
    D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS   = 1,
    D3D12_ROOT_PARAMETER_TYPE_CBV               = 2,
    D3D12_ROOT_PARAMETER_TYPE_SRV               = 3,
    D3D12_ROOT_PARAMETER_TYPE_UAV               = 4
} D3D12_ROOT_PARAMETER_TYPE;
*/

template < typename T >
inline D3D12_ROOT_PARAMETER1 create_root_param(const T* t, D3D12_SHADER_VISIBILITY visibility)
{
    D3D12_ROOT_PARAMETER1 p = {};
    return p;
}

template <>
inline D3D12_ROOT_PARAMETER1 create_root_param< D3D12_ROOT_DESCRIPTOR_TABLE1 >(const D3D12_ROOT_DESCRIPTOR_TABLE1* t, D3D12_SHADER_VISIBILITY visibility)
{
    D3D12_ROOT_PARAMETER1 p = {};
    p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    p.DescriptorTable = *t;
    p.ShaderVisibility = visibility;
    return p;
}

template <>
inline D3D12_ROOT_PARAMETER1 create_root_param< D3D12_ROOT_CONSTANTS >(const D3D12_ROOT_CONSTANTS* t, D3D12_SHADER_VISIBILITY visibility)
{
    D3D12_ROOT_PARAMETER1 p = {};
    p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    p.Constants = *t;
    p.ShaderVisibility = visibility;
    return p;
}

inline D3D12_ROOT_PARAMETER1 create_root_param(const D3D12_ROOT_DESCRIPTOR1* t, D3D12_ROOT_PARAMETER_TYPE type, D3D12_SHADER_VISIBILITY visibility)
{
    D3D12_ROOT_PARAMETER1 p = {};
    p.ParameterType = type;
    p.Descriptor = *t;
    p.ShaderVisibility = visibility;
    return p;
}

inline D3D12_RESOURCE_DESC setup_tex2d(size_t width, uint32_t height, DXGI_FORMAT fmt = DXGI_FORMAT_R8G8B8A8_UNORM, uint16_t miplevel = 1, D3D12_RESOURCE_FLAGS flag = D3D12_RESOURCE_FLAG_NONE)
{
    D3D12_RESOURCE_DESC desc = {
        D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        0, /* alignment */
        width, height, 1, miplevel, fmt,
        {1 /* sample count */, 0 /* sample quality */},
        D3D12_TEXTURE_LAYOUT_UNKNOWN,
        flag
    };
    return desc;
}

inline D3D12_RESOURCE_DESC setup_buffer(size_t width)
{
    D3D12_RESOURCE_DESC desc = {
        D3D12_RESOURCE_DIMENSION_BUFFER,
        0, /* alignment */
        width, 1, 1, 1, DXGI_FORMAT_UNKNOWN,
        {1 /* sample count */, 0 /* sample quality */},
        D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
        D3D12_RESOURCE_FLAG_NONE
    };
    return desc;
}

inline D3D12_HEAP_PROPERTIES setup_heapprop(D3D12_HEAP_TYPE type)
{
    D3D12_HEAP_PROPERTIES prop = {
        type,
        D3D12_CPU_PAGE_PROPERTY_UNKNOWN /* no-access, WC or WB */,
        D3D12_MEMORY_POOL_UNKNOWN,
        1 /* creation node mask */, 1 /* visible node mask */
    };
    return prop;
}

class asset_uploader_t {
    Microsoft::WRL::ComPtr< ID3D12CommandAllocator >  upload_cmd_alloc_;
    Microsoft::WRL::ComPtr< ID3D12GraphicsCommandList > upload_cmd_list_;
    Microsoft::WRL::ComPtr< ID3D12CommandQueue > upload_cmdq_;
    Microsoft::WRL::ComPtr< ID3D12PipelineState > pso_;
public:
    Microsoft::WRL::ComPtr< ID3D12CommandAllocator > allocator() { return upload_cmd_alloc_; }
    Microsoft::WRL::ComPtr< ID3D12CommandQueue > queue() { return upload_cmdq_; }
    int create_uploader(uniq_device_t& u)
    {
        u.dev()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&upload_cmd_alloc_));
        NAME_OBJ(upload_cmd_alloc_);

        upload_cmdq_ = u.create_command_queue(D3D12_COMMAND_LIST_TYPE_COPY);
        NAME_OBJ(upload_cmdq_);
        return 0;
    }
    
};

#endif
