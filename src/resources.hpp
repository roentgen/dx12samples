/**
 * (C) roentgen 
 * this code is licensed under the MIT License.
 */
#if !defined(RESOURCES_HPP__)
#define RESOURCES_HPP__

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

using Microsoft::WRL::ComPtr;

struct vertex_t {
	DirectX::XMFLOAT3 pos;
	DirectX::XMFLOAT2 uv;
};


class scene_t {
protected:	
	Microsoft::WRL::ComPtr< ID3D12RootSignature > rootsig_;
	Microsoft::WRL::ComPtr< ID3D12PipelineState > pso_;
	Microsoft::WRL::ComPtr< ID3D12DescriptorHeap > descheap_;

	D3D12_VERTEX_BUFFER_VIEW vbv_;
	Microsoft::WRL::ComPtr< ID3D12Resource > vertbuf_;
	Microsoft::WRL::ComPtr< ID3D12Resource > cbv_;

public:
	virtual ~scene_t() {}
	virtual void init(uniq_device_t& u, ID3D12GraphicsCommandList* cmdlist, const std::wstring&) = 0;
	virtual void update(uniq_device_t& u, uint64_t freq) = 0;
	virtual void draw(uniq_device_t& u, ID3D12GraphicsCommandList* cmdlist) = 0;
	virtual bool is_ready() = 0;
};

class playground_t : public scene_t {
	std::shared_ptr< std::vector< Microsoft::WRL::ComPtr< ID3D12Resource > > > payload_;
	std::atomic< bool > finished_;
public:
	void set_payload(uniq_device_t& u, std::shared_ptr< std::vector< Microsoft::WRL::ComPtr< ID3D12Resource > > > payload)
	{
		payload_ = std::move(payload);
		D3D12_CPU_DESCRIPTOR_HANDLE hdl = descheap_->GetCPUDescriptorHandleForHeapStart();
		hdl.ptr += (u.sizeset().view * 2); 
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
		for (int i = 0; i < 8 - payload_->size(); i ++) {
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
	
	void init(uniq_device_t& u, ID3D12GraphicsCommandList* cmdlist, const std::wstring&);
	void update(uniq_device_t& u, uint64_t freq);
	void draw(uniq_device_t& u, ID3D12GraphicsCommandList* cmdlist);
	bool is_ready();
};

class loading_t : public scene_t {
	Microsoft::WRL::ComPtr< ID3D12Resource > trampoline_;

	asset_uploader_t uploader_;
	queue_sync_object_t< 1 > qsync_;
	Microsoft::WRL::ComPtr< ID3D12Resource > tex_;

	std::thread loadthr_;
	std::future< std::weak_ptr< playground_t > > consumer_;
	std::atomic< bool > finished_;
	std::atomic< bool > shutdown_;
	uint64_t time_;
	int32_t cover_alpha_;
public:
	loading_t() : finished_(false), shutdown_(false), time_(0ULL), cover_alpha_(0) {}
	void set_consumer(std::future< std::weak_ptr< playground_t > > weakref) { consumer_ = std::move(weakref); }
	
	void init(uniq_device_t& u,  ID3D12GraphicsCommandList* cmdlist, const std::wstring&);
	void update(uniq_device_t& u, uint64_t freq);
	void draw(uniq_device_t& u, ID3D12GraphicsCommandList* cmdlist);
	bool is_ready();

	std::vector< uint32_t > load_graphics_asset(const std::wstring& fname, int& width, int& height);
	Microsoft::WRL::ComPtr< ID3D12Resource > create_texture(uniq_device_t& u, int width, int height);

	void shutdown()
	{
		if (loadthr_.joinable()) {
			shutdown_ = true;
			loadthr_.join();
		}
	}
};

class resources_t : public appbase_t {
	static const int offscreen_buffers_ = 2;

	uniq_device_t uniq_;
	D3D12_VIEWPORT viewport_;
	D3D12_RECT scissor_;

	queue_sync_object_t< 2 > flipper_;
	buffered_render_target_t< 2 > rt_;
	asset_uploader_t uploader_;

	ComPtr< ID3D12CommandAllocator >  cmd_alloc_[offscreen_buffers_];
	ComPtr< ID3D12GraphicsCommandList > cmd_list_;
	
	std::shared_ptr< loading_t > loading_;
	std::shared_ptr< playground_t > playing_;
public:
	resources_t(uint32_t w, uint32_t h) : appbase_t (w, h),
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
		return 0;
	}
	
	void load_asset();

protected:

	ComPtr< ID3D12Resource > create_texture(int width, int height);

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
			/* bind or re-bind indexed command buffer  */
			auto hr = cmd_alloc_[idx]->Reset();
			if (FAILED(hr)) {
				/* faile to sync prev frame */
				ABT("failed to reset at idx:%d err:0x%x\n", idx, hr);
				DebugBreak();
			}
			
			hr = cmd_list_->Reset(cmd_alloc_[idx].Get(), nullptr);
			if (FAILED(hr)) {
				ABT("failed to reset cmdlist at idx:%d err:0x%x\n", idx, hr);
				DebugBreak();
			}

			cmd_list_->RSSetViewports(1, &viewport_);
			cmd_list_->RSSetScissorRects(1, &scissor_);
			
			D3D12_RESOURCE_BARRIER barrier_begin = {
				D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
				D3D12_RESOURCE_BARRIER_FLAG_NONE,
				{ 
					rt_.rt(idx),
					D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
					D3D12_RESOURCE_STATE_PRESENT,/* before state */
					D3D12_RESOURCE_STATE_RENDER_TARGET /* after state */}};
			
			cmd_list_->ResourceBarrier(1, &barrier_begin);
			
			D3D12_CPU_DESCRIPTOR_HANDLE rtv = {
				rt_.descriptor_heap()->GetCPUDescriptorHandleForHeapStart().ptr + uniq_.sizeset().rtv * idx
			};
			
			cmd_list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
			
			/* recording draw commands */
			const float col[] = {0.1f, 0.1f, 0.1f, 1.f};
			cmd_list_->ClearRenderTargetView(rtv, col, 0, nullptr);

			scene_t* scene = nullptr;
			if (loading_->is_ready()) {
				INF("ready loading: %d\n", 1);
				scene = loading_.get();
			}
			else if (playing_->is_ready()) {
				INF("ready playing: %d\n", 1);
				scene = playing_.get();
			}
			scene->draw(uniq_, cmd_list_.Get());
			D3D12_RESOURCE_BARRIER barrier_end = {
				D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
				D3D12_RESOURCE_BARRIER_FLAG_NONE,
				{ 
					rt_.rt(idx),
					D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
					D3D12_RESOURCE_STATE_RENDER_TARGET,/* before state */
					D3D12_RESOURCE_STATE_PRESENT /* after state */}};

			cmd_list_->ResourceBarrier(1, &barrier_end);
			cmd_list_->Close();
			ID3D12CommandList* lists[] = {cmd_list_.Get()};
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
