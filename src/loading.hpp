/**
 * (C) roentgen 
 * this code is licensed under the MIT License.
 */
#if !defined(LOADING_HPP__)
#define LOADING_HPP__

#include "scene.hpp"
#include "dbgutils.hpp"
#include "uniq_device.hpp"
#include <comdef.h>
#include <vector>
#include <thread>
#include <future>
#include <atomic>
#include <memory>
#include <string>
#include <deque>

struct graphics_impl_t {
	Microsoft::WRL::ComPtr< ID3D12RootSignature > rootsig_;
	Microsoft::WRL::ComPtr< ID3D12PipelineState > pso_;
	Microsoft::WRL::ComPtr< ID3D12DescriptorHeap > descheap_;

	D3D12_VERTEX_BUFFER_VIEW vbv_;
	Microsoft::WRL::ComPtr< ID3D12Resource > vertbuf_;
	Microsoft::WRL::ComPtr< ID3D12Resource > cbv_;
	Microsoft::WRL::ComPtr< ID3D12Resource > tex_;
	uint64_t time_;
	int32_t cover_alpha_;
public:
	graphics_impl_t(): time_(0ULL), cover_alpha_(0) {}
	void init(uniq_device_t& u,  ID3D12GraphicsCommandList* cmdlist, const std::wstring&, ID3D12Resource*);
	void update(uniq_device_t& u, uint64_t freq);
	void draw(uniq_device_t& u, ID3D12GraphicsCommandList* cmdlist);

	Microsoft::WRL::ComPtr< ID3D12Resource > create_texture(uniq_device_t& u, int width, int height);
	Microsoft::WRL::ComPtr< ID3D12PipelineState > get_pso() { return pso_;};
};

static const int TRAMPOLINE_MAX_WIDTH = 512;
static const int TRAMPOLINE_MAX_HEIGHT = 512;

std::vector< uint32_t > load_graphics_asset(const std::wstring& fname, int& width, int& height);

D3D12_PLACED_SUBRESOURCE_FOOTPRINT write_to_trampoline(uniq_device_t& u, std::vector< uint32_t > data, const D3D12_RESOURCE_DESC& texdesc, ID3D12Resource* trampoline);

int issue_texture_upload(ID3D12GraphicsCommandList* cmdlist, const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& foorprint, ID3D12Resource* tex, ID3D12Resource* trampoline, D3D12_RESOURCE_STATES after=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

template < typename Next >
class loading_t : public scene_t {
	typedef Next next_scene_t;
	graphics_impl_t impl_;
	Microsoft::WRL::ComPtr< ID3D12Resource > trampoline_;

	asset_uploader_t uploader_;
	queue_sync_object_t< 1 > qsync_;

	std::thread loadthr_;
	std::future< std::weak_ptr< Next > > consumer_;
	std::atomic< bool > finished_;
	std::atomic< bool > shutdown_;
	std::shared_ptr< std::deque< std::wstring > > workq_;
public:
	loading_t(std::shared_ptr< std::deque< std::wstring > > workq) : finished_(false), shutdown_(false), workq_(std::move(workq)) {}
	void set_consumer(std::future< std::weak_ptr< Next > > weakref) { consumer_ = std::move(weakref); }
	
	void init(uniq_device_t& u, ID3D12GraphicsCommandList* cmdlist, const std::wstring& basepath)
	{
		/* Load Loading Screen */

		uploader_.create_uploader(u);
		
		ComPtr< ID3D12GraphicsCommandList > copycmdlist;
		/* upload のための cmdlist には PSO は必要ではない */
		u.dev()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, uploader_.allocator().Get(), nullptr/*pso_.Get()*/, IID_PPV_ARGS(&copycmdlist));
		NAME_OBJ2(copycmdlist, L"cmdlist(upload)");

		{
			/* trampoline buffer の作成: 
			   トランポリンバッファは D3D12_HEAP_TYPE_UPLOAD (CPU 側 TLB に Map 可能). CPU から書き込み, COPY コマンドで VRAM の常駐ヒープにコピーする.
			   bufsize は適当な大きさ. 必ずしもテクスチャと同じ大きさである必要はなく適当に 1MB とかでもよいが
			   Mipmap や 3D texture の stride alignment などを考慮するのが面倒なので footprint をクエリする. */
			D3D12_RESOURCE_DESC tmp = setup_tex2d(TRAMPOLINE_MAX_WIDTH, TRAMPOLINE_MAX_HEIGHT);
			
			size_t bufsize;
			u.dev()->GetCopyableFootprints(&tmp, 0, 1, 0, nullptr, nullptr, nullptr, &bufsize);
			INF("create trampoline buffer for texture: required:%lld\n", bufsize);
			
			auto upload = setup_heapprop(D3D12_HEAP_TYPE_UPLOAD);
			D3D12_RESOURCE_DESC buf = setup_buffer(bufsize);
			auto hr = u.dev()->CreateCommittedResource(&upload, D3D12_HEAP_FLAG_NONE, &buf, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&trampoline_));
			if (FAILED(hr)) {
				ABT("failed to create trampoline buffer: err:0x%x\n", hr);
			}
		}

		/* FIXME: このコンテキストでは copy 用のコマンドバッファを使わなくしいほうが正しいが、
		   その場合このコンテキストで trampoline バッファを使いまわしてはいけないようになるので copy engine のコマンドバッファを使うことにした */
		//impl_.init(u, cmdlist, basepath, trampoline_.Get());
		impl_.init(u, copycmdlist.Get(), basepath, trampoline_.Get());
		
		Microsoft::WRL::ComPtr< ID3D12Fence > fence;
		u.dev()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
		qsync_.init(std::move(fence), 0);
		
		/* 楽に move capture が使いたいなぁ */
		auto payload = std::make_shared< std::vector< Microsoft::WRL::ComPtr< ID3D12Resource > > >();
		
		copycmdlist->Close();
		
		qsync_.incr(); /* wait ready */
		qsync_.wait(uploader_.queue().Get());
		
		ID3D12CommandList* l[] = {copycmdlist.Get()};
		uploader_.queue()->ExecuteCommandLists(std::extent< decltype(l) >::value, l);
		qsync_.wait(uploader_.queue().Get());
		
		finished_ = true;
		
		loadthr_ = std::thread([&, payload, copycmdlist]{
				while (!shutdown_ && !workq_->empty()) {
					auto wi = workq_->front();
					workq_->pop_front();
					int w, h;
					auto data = load_graphics_asset(wi, w, h);
					if (!data.size())
						continue;
					INF("Load texture: %s w:%d h:%d\n", wi.c_str(), w, h);
					
					Sleep(1000); /* Loading Screen っぽくもったいつける */
					uploader_.allocator()->Reset();
					auto hr = copycmdlist->Reset(uploader_.allocator().Get(), nullptr);
					if (FAILED(hr)) {
						ABT("failed to reset copycmdlist:0x%x\n", hr);
					}
					
					auto tex = impl_.create_texture(u, w, h);
					auto copied = write_to_trampoline(u, std::move(data), tex->GetDesc(), trampoline_.Get()); /* copy to trampoline */
					issue_texture_upload(copycmdlist.Get(), copied, tex.Get(), trampoline_.Get());
					copycmdlist->Close();
					
					ID3D12CommandList* l[] = {copycmdlist.Get()};
					uploader_.queue()->ExecuteCommandLists(std::extent< decltype(l) >::value, l);
					qsync_.wait(uploader_.queue().Get());
					
					payload->push_back(std::move(tex));
				}
				
				auto p = consumer_.get().lock();
				if (p) {
					p->set_payload(u, std::move(payload));
				}
				finished_ = false;
			});

	}
	void update(uniq_device_t& u, uint64_t freq)
	{
		impl_.update(u, freq);
	}
	void draw(uniq_device_t& u, ID3D12GraphicsCommandList* cmdlist)
	{
		impl_.draw(u, cmdlist);
	}
	bool is_ready()
	{
		return finished_;
	}

	Microsoft::WRL::ComPtr< ID3D12PipelineState > get_pso() { return impl_.pso_;};

	void shutdown()
	{
		if (loadthr_.joinable()) {
			shutdown_ = true;
			loadthr_.join();
		}
	}
};


#endif
