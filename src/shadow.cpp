/**
 * (C) roentgen 
 * this code is licensed under the MIT License.
 */
#include <string>
#include "stdafx.h"
#include <D3DCompiler.h>
#include <vector>
#include <memory>
#include <deque>
#include <cmath>
#include <algorithm>
#include "shadow.hpp"

using namespace Microsoft::WRL;

void shadow_t::load_asset()
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
    {
        auto hr = cmd_list_->Reset(cmd_alloc_[idx].Get(), nullptr);
        if (FAILED(hr)) {
            ABT("failed to main cmdlist:0x%x\n", hr);
        }
    }

    ComPtr< ID3D12CommandAllocator > thr_cmd_alloc;
    ComPtr< ID3D12GraphicsCommandList > thr_cmd_lst;
    uniq_.dev()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&thr_cmd_alloc));
    uniq_.dev()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, thr_cmd_alloc.Get(), nullptr,IID_PPV_ARGS(&thr_cmd_lst));
    NAME_OBJ(thr_cmd_lst);
    NAME_OBJ(thr_cmd_alloc);

    auto workq = std::make_shared< std::deque< std::wstring > >();
    workq->push_back(dir + L"mc512x512_blue.rawdata");
    
    loading_ = std::make_shared< loading_t< playground_t > >(std::move(workq));
    playing_ = std::make_shared< playground_t >();
    
    /* もし loader スレッドが一瞬で終わって set_consumer() よりも set_shadowtexture() が先に呼ばれたりしないよう
       先にリスナを登録しておく  */
    loading_->set_consumer(std::async(std::launch::deferred, [=] {
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

ComPtr< ID3D12Resource > shadow_t::create_texture(int width, int height)
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
    return runapp< shadow_t >(1280, 720, instance, cmd);
}

