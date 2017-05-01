/**
 * (C) roentgen 
 * this code is licensed under the MIT License.
 */
#if !defined(SCENE_HPP__)
#define SCENE_HPP__

#include "uniq_device.hpp"

class scene_t {
protected:
    /*
    Microsoft::WRL::ComPtr< ID3D12RootSignature > rootsig_;
    Microsoft::WRL::ComPtr< ID3D12PipelineState > pso_;
    Microsoft::WRL::ComPtr< ID3D12DescriptorHeap > descheap_;

    D3D12_VERTEX_BUFFER_VIEW vbv_;
    Microsoft::WRL::ComPtr< ID3D12Resource > vertbuf_;
    Microsoft::WRL::ComPtr< ID3D12Resource > cbv_;
    */
public:
    virtual ~scene_t() {}
    virtual void init(uniq_device_t& u, ID3D12GraphicsCommandList* cmdlist, const std::wstring&) = 0;
    virtual void update(uniq_device_t& u, uint64_t freq) = 0;
    virtual void draw(uniq_device_t& u, ID3D12GraphicsCommandList* cmdlist) = 0;
    virtual bool is_ready() = 0;
    virtual Microsoft::WRL::ComPtr< ID3D12PipelineState > get_pso() = 0;
};


#endif
