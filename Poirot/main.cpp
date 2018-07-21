#define _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "external/tiny_gltf/tiny_gltf.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <wrl.h>

#include <dxgi1_6.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <d3d12sdklayers.h>
#include <DirectXMath.h>

#include <iostream>
#include <exception>
#include <vector>
#include <array>
#include <fstream>
#include <algorithm>
#include <iterator>

#include "Window.h"
#include "Gui.h"
#include "D:/Octarine/OctarineImage/octarine_image.h"
#include "D:/Octarine/OctarineMesh/octarine_mesh.h"

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace std;

#define CHECK_D3D12_CALL(x)	{if(FAILED(x)) { throw std::exception(_STRINGIZE(x));}};
#define CHECK_DXGI_CALL(x)	{if(FAILED(x)) { throw std::exception(_STRINGIZE(x));}};
#define CHECK_WIN32_CALL(x)	check_win32_call(x);

template <typename T, size_t N>
constexpr size_t count_of(T(&)[N]) { return N; }

constexpr uint8_t max_inflight_frame_count = 3;
constexpr uint16_t back_buffer_width = 1920;
constexpr uint16_t back_buffer_height = 1080;
constexpr DXGI_FORMAT back_buffer_format = DXGI_FORMAT_R8G8B8A8_UNORM;

unique_ptr<Window> unq_window = nullptr;
unique_ptr<Gui> unq_gui = nullptr;

uint8_t frame_index = 0;
ComPtr<ID3D12Device> com_device = nullptr;
ComPtr<ID3D12CommandQueue> com_command_queue = nullptr;
ComPtr<IDXGISwapChain3> com_swap_chain = nullptr;

ComPtr<ID3D12DescriptorHeap> com_rtv_heap = nullptr;
ComPtr<ID3D12DescriptorHeap> com_dsv_heap = nullptr;
//ComPtr<ID3D12DescriptorHeap> com_cbv_srv_uav_heap = nullptr;

array<ComPtr<ID3D12Resource>, max_inflight_frame_count> a_com_render_targets = {};
ComPtr<ID3D12Resource> com_dsv = nullptr;
uint16_t rtv_descriptor_size = 0;

array<ComPtr<ID3D12CommandAllocator>, max_inflight_frame_count> a_com_command_allocators = {};
ComPtr<ID3D12GraphicsCommandList> com_command_list = nullptr;

array<uint64_t, max_inflight_frame_count> a_fence_values = {};
ComPtr<ID3D12Fence> com_fence = nullptr;
HANDLE h_fence_event = 0;

ComPtr<ID3D12PipelineState> com_pipeline_state_object = nullptr;
ComPtr<ID3D12RootSignature> com_root_signature = nullptr;

struct PoirotPerFrameConstantBuffer {
	__declspec(align(256)) struct {
		XMFLOAT4X4 clip_from_view;
		XMFLOAT4X4 view_from_world;
		XMFLOAT4X4 world_from_view;
		XMFLOAT4   color;
	} constants;
	ComPtr<ID3D12Resource> com_buffer;
	void	*cpu_virtual_address;
	uint64_t gpu_virtual_address;
};

struct PoirotMeshHeader {
	int header_size;
	int vertex_count;
	int index_count;
};

struct PoirotMesh {
	PoirotMeshHeader header;
	ComPtr<ID3D12Resource> com_vertex_buffer;
	ComPtr<ID3D12Resource> com_index_buffer;
	ComPtr<ID3D12Resource> com_vertex_upload_buffer;
	ComPtr<ID3D12Resource> com_index_upload_buffer;
	D3D12_VERTEX_BUFFER_VIEW vbv;
	D3D12_INDEX_BUFFER_VIEW ibv;
};

struct Texture {
	ComPtr<ID3D12Resource> com_resource;
	ComPtr<ID3D12Resource> com_upload;
	D3D12_GPU_DESCRIPTOR_HANDLE gpu_descriptor_handle;
};

struct DescriptorHeap{
	ComPtr<ID3D12DescriptorHeap> com_heap;
	uint32_t num_max_descriptors;
	uint32_t num_used_descriptors;
	uint32_t descriptor_increment_size;
	D3D12_GPU_DESCRIPTOR_HANDLE base_descriptor;
	D3D12_DESCRIPTOR_HEAP_TYPE type;
	bool is_shader_visible;

	DescriptorHeap(uint32_t num_max_descriptors, D3D12_DESCRIPTOR_HEAP_TYPE type, bool is_shader_visible) 
		: num_max_descriptors(num_max_descriptors), type(type), is_shader_visible(is_shader_visible) {
	};

	void init() {
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.NumDescriptors = num_max_descriptors;
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.Flags = is_shader_visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		CHECK_D3D12_CALL(com_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&com_heap)));

		descriptor_increment_size = com_device->GetDescriptorHandleIncrementSize(type);
		base_descriptor = com_heap->GetGPUDescriptorHandleForHeapStart();
	}

	D3D12_CPU_DESCRIPTOR_HANDLE get_cpu_handle(uint32_t descriptor_index) {
		if(descriptor_index > num_used_descriptors || descriptor_index == num_max_descriptors) { throw exception("Not enough descriptors left in this heap"); }
		D3D12_CPU_DESCRIPTOR_HANDLE cpu_descriptor_handle = com_heap->GetCPUDescriptorHandleForHeapStart();
		cpu_descriptor_handle.ptr += descriptor_increment_size * descriptor_index;
		return cpu_descriptor_handle;
	};

	D3D12_GPU_DESCRIPTOR_HANDLE get_gpu_handle(uint32_t descriptor_index) {
		if(descriptor_index > num_used_descriptors || descriptor_index == num_max_descriptors) { throw exception("Not enough descriptors left in this heap"); }
		D3D12_GPU_DESCRIPTOR_HANDLE gpu_descriptor_handle = base_descriptor;
		gpu_descriptor_handle.ptr += descriptor_increment_size * descriptor_index;
		return gpu_descriptor_handle;
	};
};

DescriptorHeap cbv_srv_uav_desc_heap = {32u, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV , true};

Texture env_map = {};
Texture mesh_albedo = {};

PoirotMesh mesh = {};

PoirotPerFrameConstantBuffer per_frame_cb = {};

struct Camera {	
	XMFLOAT4X4 clip_from_view;
	XMFLOAT4X4 view_from_world;
	XMFLOAT4X4 world_from_view;
	
	XMFLOAT3 pos_ws;
	XMFLOAT3 dir_ws;
	XMFLOAT3 up_ws;

	float yaw_rad;
	float pitch_rad;
	
	float aspect_ratio;
	float vertical_fov_in_degrees;
	float near_plane_in_meters;
	float far_plane_in_meters;
};

Camera camera;

float view_zenith_angle_rad = XMConvertToRadians(45.0);
float view_azimuth_angle_rad = XMConvertToRadians(0.0);
float view_distance_in_meters = 10.0;

inline void check_win32_call(HANDLE h) {
	if(h == NULL) { throw std::exception(); };
}

void wait_for_gpu() {
	CHECK_D3D12_CALL(com_command_queue->Signal(com_fence.Get(), a_fence_values[frame_index]));
	CHECK_D3D12_CALL(com_fence->SetEventOnCompletion(a_fence_values[frame_index], h_fence_event));
	WaitForSingleObjectEx(h_fence_event, INFINITE, FALSE);
	a_fence_values[frame_index]++;
}

void prepare_next_frame() {
	uint64_t current_fence_value = a_fence_values[frame_index];
	CHECK_D3D12_CALL(com_command_queue->Signal(com_fence.Get(), current_fence_value));
	frame_index = com_swap_chain->GetCurrentBackBufferIndex();
	if(com_fence->GetCompletedValue() < a_fence_values[frame_index]) {
		CHECK_D3D12_CALL(com_fence->SetEventOnCompletion(a_fence_values[frame_index], h_fence_event));
		WaitForSingleObjectEx(h_fence_event, INFINITE, FALSE);
	}

	a_fence_values[frame_index] = current_fence_value + 1;
}

void load_mesh(string asset_filename, PoirotMesh &mesh) {

	{
		string asset_folder = "D:/Octarine/OctarineAssets/";
		std::ifstream file(asset_folder + asset_filename, std::ios::binary);
		if(!file.is_open()) {
			throw exception("Couldn't open a necessary file\n");
		}

		OctarineMeshHeader header = {};
		void *p_data = nullptr;

		OCTARINE_MESH_RESULT result = octarine_mesh_read_from_file(asset_filename.c_str(), &header, &p_data);
		if(result != OCTARINE_MESH_OK) { string msg = "File error: " + asset_filename; throw exception(msg.c_str()); };

		mesh.header.vertex_count = header.num_vertices;
		mesh.header.index_count = header.num_indices;

		uint32_t vertex_size = sizeof(float) * 8;
		uint32_t vertex_buffer_size = mesh.header.vertex_count * vertex_size;
		uint32_t index_buffer_size = mesh.header.index_count * sizeof(uint32_t);

		void *p_vertex_data = p_data;
		void *p_index_data = (void*)((uint8_t*)p_data + vertex_buffer_size);

		{
			D3D12_HEAP_PROPERTIES heap_properties = {};
			heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
			D3D12_RESOURCE_DESC resource_desc = {};
			resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			resource_desc.Width = vertex_buffer_size;
			resource_desc.Height = 1;
			resource_desc.DepthOrArraySize = 1;
			resource_desc.MipLevels = 1;
			resource_desc.Format = DXGI_FORMAT_UNKNOWN;
			resource_desc.SampleDesc.Count = 1;
			resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

			CHECK_D3D12_CALL(com_device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&mesh.com_vertex_buffer)));
			heap_properties.Type = D3D12_HEAP_TYPE_UPLOAD;
			CHECK_D3D12_CALL(com_device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mesh.com_vertex_upload_buffer)));

			D3D12_RESOURCE_BARRIER resource_barrier = {};
			resource_barrier.Transition.pResource = mesh.com_vertex_buffer.Get();
			resource_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
			resource_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
			com_command_list->ResourceBarrier(1, &resource_barrier);

			void *p_data = nullptr;
			CHECK_D3D12_CALL(mesh.com_vertex_upload_buffer->Map(0, nullptr, &p_data));
			memcpy(p_data, p_vertex_data, vertex_buffer_size);
			mesh.com_vertex_upload_buffer->Unmap(0, nullptr);
			com_command_list->CopyBufferRegion(mesh.com_vertex_buffer.Get(), 0, mesh.com_vertex_upload_buffer.Get(), 0, vertex_buffer_size);

			resource_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
			resource_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
			com_command_list->ResourceBarrier(1, &resource_barrier);
		}

		mesh.vbv.BufferLocation = mesh.com_vertex_buffer->GetGPUVirtualAddress();
		mesh.vbv.SizeInBytes = vertex_buffer_size;
		mesh.vbv.StrideInBytes = vertex_size;

		{
			D3D12_HEAP_PROPERTIES heap_properties = {};
			heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
			D3D12_RESOURCE_DESC resource_desc = {};
			resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			resource_desc.Width = index_buffer_size;
			resource_desc.Height = 1;
			resource_desc.DepthOrArraySize = 1;
			resource_desc.MipLevels = 1;
			resource_desc.Format = DXGI_FORMAT_UNKNOWN;
			resource_desc.SampleDesc.Count = 1;
			resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

			CHECK_D3D12_CALL(com_device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&mesh.com_index_buffer)));
			heap_properties.Type = D3D12_HEAP_TYPE_UPLOAD;
			CHECK_D3D12_CALL(com_device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mesh.com_index_upload_buffer)));

			D3D12_RESOURCE_BARRIER resource_barrier = {};
			resource_barrier.Transition.pResource = mesh.com_index_buffer.Get();
			resource_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
			resource_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
			com_command_list->ResourceBarrier(1, &resource_barrier);

			void *p_data = nullptr;
			CHECK_D3D12_CALL(mesh.com_index_upload_buffer->Map(0, nullptr, &p_data));
			memcpy(p_data, p_index_data, index_buffer_size);
			mesh.com_index_upload_buffer->Unmap(0, nullptr);
			com_command_list->CopyBufferRegion(mesh.com_index_buffer.Get(), 0, mesh.com_index_upload_buffer.Get(), 0, index_buffer_size);

			resource_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
			resource_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
			com_command_list->ResourceBarrier(1, &resource_barrier);
		}

		mesh.ibv.BufferLocation = mesh.com_index_buffer->GetGPUVirtualAddress();
		if(index_buffer_size / mesh.header.index_count == 4) {
			mesh.ibv.Format = DXGI_FORMAT_R32_UINT;
		}
		else if(index_buffer_size / mesh.header.index_count == 2) {
			mesh.ibv.Format = DXGI_FORMAT_R16_UINT;
		}
		mesh.ibv.SizeInBytes = index_buffer_size;
	}
}

void load_texture(string asset_filename, Texture &tex) {
	OctarineImageHeader header = {};
	void *p_src_data = nullptr;

	OCTARINE_IMAGE result = octarine_image_read_from_file(asset_filename.c_str(), &header, &p_src_data);
	if(result != OCTARINE_IMAGE::OCTARINE_IMAGE_OK) { string msg = "File error: " + asset_filename; throw exception(msg.c_str()); };

	UINT num_subresources = header.array_size * header.mip_levels;
	UINT64 *p_src_subresource_offsets = reinterpret_cast<UINT64*>(alloca(sizeof(UINT64)*num_subresources));
	UINT64 *p_src_subresource_sizes = reinterpret_cast<UINT64*>(alloca(sizeof(UINT64)*num_subresources));
	UINT64 *p_src_subresource_row_sizes = reinterpret_cast<UINT64*>(alloca(sizeof(UINT64)*num_subresources));
	octarine_image_get_subresource_infos(&header, p_src_subresource_offsets, p_src_subresource_sizes, p_src_subresource_row_sizes);

	D3D12_HEAP_PROPERTIES heap_properties = {};
	heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC resource_desc = {};
	resource_desc.Dimension = (header.depth == 1) ? D3D12_RESOURCE_DIMENSION_TEXTURE2D : D3D12_RESOURCE_DIMENSION_TEXTURE3D;
	resource_desc.Alignment = 0;
	resource_desc.Width = header.width;
	resource_desc.Height = header.height;
	resource_desc.DepthOrArraySize = (header.depth == 1) ? header.array_size : header.depth;
	resource_desc.MipLevels = header.mip_levels;
	resource_desc.Format = octarine_image_get_dxgi_format(header.format.as_enum);
	resource_desc.SampleDesc.Count = 1;
	resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

	CHECK_D3D12_CALL(com_device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&tex.com_resource)));
	wstring asset_filename_w(asset_filename.begin(), asset_filename.end());
	tex.com_resource->SetName(asset_filename_w.c_str());

	heap_properties.Type = D3D12_HEAP_TYPE_UPLOAD;

	D3D12_PLACED_SUBRESOURCE_FOOTPRINT *p_dst_footprints = reinterpret_cast<D3D12_PLACED_SUBRESOURCE_FOOTPRINT*>(alloca(sizeof(D3D12_PLACED_SUBRESOURCE_FOOTPRINT)*num_subresources));
	UINT *p_dst_num_rows = reinterpret_cast<UINT*>(alloca(sizeof(UINT)*num_subresources));
	UINT64 *p_dst_row_sizes = reinterpret_cast<UINT64*>(alloca(sizeof(UINT64)*num_subresources));
	UINT64 dst_required_size = 0;
	com_device->GetCopyableFootprints(&resource_desc, 0, num_subresources, 0, p_dst_footprints, p_dst_num_rows, p_dst_row_sizes, &dst_required_size);

	D3D12_RESOURCE_DESC upload_resource_desc = {};
	upload_resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	upload_resource_desc.Alignment = 0;
	upload_resource_desc.Width = dst_required_size;
	upload_resource_desc.Height = 1;
	upload_resource_desc.DepthOrArraySize = 1;
	upload_resource_desc.MipLevels = 1;
	upload_resource_desc.Format = DXGI_FORMAT_UNKNOWN;
	upload_resource_desc.SampleDesc.Count = 1;
	upload_resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	upload_resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

	CHECK_D3D12_CALL(com_device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &upload_resource_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&tex.com_upload)));

	D3D12_RESOURCE_BARRIER resource_barrier = {};
	resource_barrier.Transition.pResource = tex.com_resource.Get();
	resource_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	resource_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
	resource_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
	com_command_list->ResourceBarrier(1, &resource_barrier);

	void *p_dst_data = nullptr;
	CHECK_D3D12_CALL(tex.com_upload->Map(0, nullptr, &p_dst_data));
	for(UINT subresource_index = 0; subresource_index < num_subresources; ++subresource_index) {
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT subresource_footprint = p_dst_footprints[subresource_index];
		UINT num_subresource_slices = subresource_footprint.Footprint.Depth;
		UINT num_subresource_rows = p_dst_num_rows[subresource_index];
		UINT64 dst_subresource_row_size = subresource_footprint.Footprint.RowPitch;
		UINT64 dst_subresource_slice_size = dst_subresource_row_size * num_subresource_rows;
		for(UINT subresource_slice_index = 0; subresource_slice_index < num_subresource_slices; ++subresource_slice_index) {
			UINT64 dst_subresource_offset = subresource_footprint.Offset + dst_subresource_slice_size * subresource_slice_index;
			UINT64 src_subresource_offset = p_src_subresource_offsets[subresource_index];
			BYTE* p_dest = reinterpret_cast<BYTE*>(p_dst_data) + dst_subresource_offset;
			const BYTE* p_src = reinterpret_cast<const BYTE*>(p_src_data) + src_subresource_offset;
			for(UINT row_index = 0; row_index < num_subresource_rows; ++row_index) {
				memcpy(p_dest + dst_subresource_row_size * row_index, p_src + p_src_subresource_row_sizes[subresource_index] * row_index, p_dst_row_sizes[subresource_index]);
			}
		}
	}
	tex.com_upload->Unmap(0, nullptr);

	for(UINT subresource_index = 0; subresource_index < num_subresources; ++subresource_index) {
		D3D12_TEXTURE_COPY_LOCATION dest = {};
		dest.pResource = tex.com_resource.Get();
		dest.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dest.SubresourceIndex = subresource_index;
		D3D12_TEXTURE_COPY_LOCATION src = {};
		src.pResource = tex.com_upload.Get();
		src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		src.PlacedFootprint = p_dst_footprints[subresource_index];
		com_command_list->CopyTextureRegion(&dest, 0, 0, 0, &src, NULL);
	}

	resource_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	resource_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	com_command_list->ResourceBarrier(1, &resource_barrier);

	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
		srv_desc.Format = octarine_image_get_dxgi_format(header.format.as_enum);
		srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		if(header.flags == OCTARINE_IMAGE_FLAGS_CUBE) {
			srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
			srv_desc.TextureCube.MipLevels = header.mip_levels;
			srv_desc.TextureCube.MostDetailedMip = 0;
			srv_desc.TextureCube.ResourceMinLODClamp = 0;
		}
		else if(header.depth == 1 && header.array_size == 1 && header.height > 1) {
			srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srv_desc.Texture2D.MipLevels = header.mip_levels;
			srv_desc.Texture2D.MostDetailedMip = 0;
			srv_desc.Texture2D.PlaneSlice = 0;
			srv_desc.Texture2D.ResourceMinLODClamp = 0;
		}
		else { throw exception("INCOMPLETE!"); return; }

		D3D12_CPU_DESCRIPTOR_HANDLE cpu_descriptor_handle = cbv_srv_uav_desc_heap.get_cpu_handle(cbv_srv_uav_desc_heap.num_used_descriptors);
		com_device->CreateShaderResourceView(tex.com_resource.Get(), &srv_desc, cpu_descriptor_handle);

		D3D12_GPU_DESCRIPTOR_HANDLE gpu_descriptor_handle = cbv_srv_uav_desc_heap.get_gpu_handle(cbv_srv_uav_desc_heap.num_used_descriptors);
		tex.gpu_descriptor_handle = gpu_descriptor_handle;

		cbv_srv_uav_desc_heap.num_used_descriptors++;
	}

	free(p_src_data);
}

void init(HINSTANCE h_instance) {

	unq_window = make_unique<Window>(h_instance, 1920, 1080);
	{
		ComPtr<IDXGIFactory5> com_dxgi_factory = nullptr;
		vector<ComPtr<IDXGIAdapter1>> v_com_dxgi_adapters = {};
		v_com_dxgi_adapters.reserve(4);

		UINT factory_flags = 0;
#if(_DEBUG)
		ComPtr<ID3D12Debug> com_debug_controller = nullptr;
		if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&com_debug_controller)))) {
			com_debug_controller->EnableDebugLayer();
			factory_flags = DXGI_CREATE_FACTORY_DEBUG;
		}
#endif

		HRESULT h_result = CreateDXGIFactory2(factory_flags, IID_PPV_ARGS(&com_dxgi_factory));
		if(FAILED(h_result)) { throw exception(); }

		UINT adapter_index = 0;
		for(;;) {
			ComPtr<IDXGIAdapter1> com_curr_adapter = nullptr;
			h_result = com_dxgi_factory->EnumAdapters1(adapter_index, com_curr_adapter.GetAddressOf());
			if(FAILED(h_result)) {
				if((h_result == DXGI_ERROR_NOT_FOUND && adapter_index == 0) || h_result != DXGI_ERROR_NOT_FOUND) {
					throw exception();
				}
				else {
					break;
				}
			}
			else {
				v_com_dxgi_adapters.push_back(std::move(com_curr_adapter));
			}
			adapter_index++;
		}

		for(auto const &com_dxgi_adapter : v_com_dxgi_adapters) {
			DXGI_ADAPTER_DESC1 desc = {};
			com_dxgi_adapter->GetDesc1(&desc);
			if(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
				continue;
			}
			if(SUCCEEDED(D3D12CreateDevice(com_dxgi_adapter.Get(), D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), reinterpret_cast<void**>(com_device.GetAddressOf())))) {
				DXGI_ADAPTER_DESC adapter_desc = {};
				com_dxgi_adapter->GetDesc(&adapter_desc);
				break;
			}
			
		}
		if(com_device == nullptr) {
			throw exception("No suitable DX12 device found!\n");
		}

		{
			D3D12_COMMAND_QUEUE_DESC command_queue_desc = {};
			command_queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
			CHECK_D3D12_CALL(com_device->CreateCommandQueue(&command_queue_desc, IID_PPV_ARGS(&com_command_queue)));

			ComPtr<IDXGISwapChain1> com_temp_swap_chain = nullptr;
			DXGI_SWAP_CHAIN_DESC1 swap_chain_desc = {};
			swap_chain_desc.Width = back_buffer_width;
			swap_chain_desc.Height = back_buffer_height;
			swap_chain_desc.Format = back_buffer_format;
			swap_chain_desc.SampleDesc.Count = 1;
			swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			swap_chain_desc.BufferCount = max_inflight_frame_count;
			swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

			CHECK_DXGI_CALL(com_dxgi_factory->CreateSwapChainForHwnd(com_command_queue.Get(), unq_window->get_handle(), &swap_chain_desc, NULL, NULL, com_temp_swap_chain.GetAddressOf()));
			CHECK_DXGI_CALL(com_temp_swap_chain.As(&com_swap_chain));
			frame_index = com_swap_chain->GetCurrentBackBufferIndex();
		}

		{
			rtv_descriptor_size = com_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

			D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
			rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			rtv_heap_desc.NumDescriptors = max_inflight_frame_count;
			CHECK_D3D12_CALL(com_device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&com_rtv_heap)));

			D3D12_CPU_DESCRIPTOR_HANDLE rtv_cpu_handle = { com_rtv_heap->GetCPUDescriptorHandleForHeapStart() };
			for(int buffer_index = 0; buffer_index < max_inflight_frame_count; ++buffer_index) {
				CHECK_DXGI_CALL(com_swap_chain->GetBuffer(buffer_index, IID_PPV_ARGS(&(a_com_render_targets[buffer_index]))));
				com_device->CreateRenderTargetView(a_com_render_targets[buffer_index].Get(), nullptr, rtv_cpu_handle);
				rtv_cpu_handle.ptr += rtv_descriptor_size;
			}
		}

		{
			for(uint8_t allocator_index = 0; allocator_index < a_com_command_allocators.size(); ++allocator_index) {
				CHECK_D3D12_CALL(com_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&(a_com_command_allocators[allocator_index]))));
			}

			CHECK_D3D12_CALL(com_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE::D3D12_COMMAND_LIST_TYPE_DIRECT, a_com_command_allocators[0].Get(), nullptr, IID_PPV_ARGS(&com_command_list)));
		}

		{
			CHECK_D3D12_CALL(com_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&com_fence)));
			a_fence_values[frame_index]++;
			CHECK_WIN32_CALL(CreateEvent(nullptr, FALSE, FALSE, nullptr));
		}

		{
			D3D12_FEATURE_DATA_ROOT_SIGNATURE feature_data = {};
			feature_data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
			if(FAILED(com_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &feature_data, sizeof(feature_data)))) {
				feature_data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
			}

			D3D12_ROOT_PARAMETER1 a_root_params[2] = {};
			// root cbv descriptor parameter
			a_root_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
			a_root_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			a_root_params[0].Descriptor.ShaderRegister = 0;
			a_root_params[0].Descriptor.RegisterSpace = 0;
			a_root_params[0].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE;

			// srv desc table parameter
			D3D12_DESCRIPTOR_RANGE1 a_srv_descriptor_ranges[1] = {};
			a_srv_descriptor_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
			a_srv_descriptor_ranges[0].NumDescriptors = 2;
			a_srv_descriptor_ranges[0].BaseShaderRegister = 0;
			a_srv_descriptor_ranges[0].RegisterSpace = 0;
			a_srv_descriptor_ranges[0].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
			a_srv_descriptor_ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

			a_root_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			a_root_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
			a_root_params[1].DescriptorTable.NumDescriptorRanges = count_of(a_srv_descriptor_ranges);
			a_root_params[1].DescriptorTable.pDescriptorRanges = a_srv_descriptor_ranges;

			D3D12_STATIC_SAMPLER_DESC static_sampler = {};
			//static_sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
			static_sampler.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
			static_sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			static_sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			static_sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			static_sampler.MipLODBias = 0;
			static_sampler.MaxAnisotropy = 1;
			static_sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
			static_sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
			static_sampler.MinLOD = 0;
			static_sampler.MaxLOD = D3D12_FLOAT32_MAX;
			static_sampler.ShaderRegister = 0;
			static_sampler.RegisterSpace = 0;
			static_sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

			D3D12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc = {};
			root_signature_desc.Version = feature_data.HighestVersion;
			root_signature_desc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
			root_signature_desc.Desc_1_1.NumParameters = count_of(a_root_params);
			root_signature_desc.Desc_1_1.pParameters = a_root_params;
			root_signature_desc.Desc_1_1.NumStaticSamplers = 1;
			root_signature_desc.Desc_1_1.pStaticSamplers = &static_sampler;

			ComPtr<ID3DBlob> signature;
			ComPtr<ID3DBlob> error;
			CHECK_D3D12_CALL(D3D12SerializeVersionedRootSignature(&root_signature_desc, &signature, &error));
			CHECK_D3D12_CALL(com_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&com_root_signature)));
		}

		// Create the pipeline state, which includes compiling and loading shaders.
		{
			ComPtr<ID3DBlob> com_vertex_shader;
			ComPtr<ID3DBlob> com_pixel_shader;

#if defined(_DEBUG)
			// Enable better shader debugging with the graphics debugging tools.
			uint32_t compile_flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
			uint32_t compile_flags = 0;
#endif

			CHECK_D3D12_CALL(D3DCompileFromFile(L"shaders/basic_vs.hlsl", nullptr, nullptr, "vs_main", "vs_5_0", compile_flags, 0, &com_vertex_shader, nullptr));
			CHECK_D3D12_CALL(D3DCompileFromFile(L"shaders/basic_ps.hlsl", nullptr, nullptr, "ps_main", "ps_5_0", compile_flags, 0, &com_pixel_shader, nullptr));

			D3D12_INPUT_ELEMENT_DESC a_input_element_descs[] = {
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
				{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
				{ "UV", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			};

			D3D12_RASTERIZER_DESC default_rasterizer_desc = {};
			default_rasterizer_desc.FillMode = D3D12_FILL_MODE_SOLID;
			default_rasterizer_desc.CullMode = D3D12_CULL_MODE_NONE;
			default_rasterizer_desc.DepthClipEnable = TRUE;
			D3D12_BLEND_DESC default_blend_desc = {};
			for(uint32_t rt_index = 0; rt_index < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++rt_index) {
				default_blend_desc.RenderTarget[rt_index].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
			}
			D3D12_DEPTH_STENCIL_DESC default_depth_stencil_desc = {};
			default_depth_stencil_desc.DepthEnable = TRUE;
			default_depth_stencil_desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
			default_depth_stencil_desc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

			D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
			pso_desc.InputLayout = { a_input_element_descs, static_cast<UINT>(count_of(a_input_element_descs)) };
			pso_desc.pRootSignature = com_root_signature.Get();
			pso_desc.VS = { com_vertex_shader->GetBufferPointer(), com_vertex_shader->GetBufferSize() };
			pso_desc.PS = { com_pixel_shader->GetBufferPointer(), com_pixel_shader->GetBufferSize() };
			pso_desc.RasterizerState = default_rasterizer_desc;
			pso_desc.BlendState = default_blend_desc;
			pso_desc.DepthStencilState = default_depth_stencil_desc;
			pso_desc.SampleMask = UINT_MAX;
			pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			pso_desc.NumRenderTargets = 1;
			pso_desc.RTVFormats[0] = back_buffer_format;
			pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
			pso_desc.SampleDesc.Count = 1;
			CHECK_D3D12_CALL(com_device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&com_pipeline_state_object)));
		}

		// Create the depth-stencil view
		{
			// Describe and create a depth stencil view (DSV) descriptor heap.
			D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc = {};
			dsv_heap_desc.NumDescriptors = 1;
			dsv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
			CHECK_D3D12_CALL(com_device->CreateDescriptorHeap(&dsv_heap_desc, IID_PPV_ARGS(&com_dsv_heap)));

			D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
			dsv_desc.Format = DXGI_FORMAT_D32_FLOAT;
			dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
			dsv_desc.Flags = D3D12_DSV_FLAG_NONE;

			D3D12_CLEAR_VALUE dsc_clear_value = {};
			dsc_clear_value.Format = DXGI_FORMAT_D32_FLOAT;
			dsc_clear_value.DepthStencil.Depth = 1.0f;

			D3D12_HEAP_PROPERTIES heap_properties = {};
			heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
			D3D12_RESOURCE_DESC resource_desc = {};
			resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			resource_desc.Width = back_buffer_width;
			resource_desc.Height = back_buffer_height;
			resource_desc.DepthOrArraySize = 1;
			resource_desc.MipLevels = 1;
			resource_desc.Format = DXGI_FORMAT_D32_FLOAT;
			resource_desc.SampleDesc.Count = 1;
			resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
			resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

			CHECK_D3D12_CALL(com_device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &dsc_clear_value, IID_PPV_ARGS(&com_dsv)));
			com_device->CreateDepthStencilView(com_dsv.Get(), &dsv_desc, com_dsv_heap->GetCPUDescriptorHandleForHeapStart());
		}

		{
			cbv_srv_uav_desc_heap.init();
		}

		{
			load_mesh("Pony_cartoon.octrn", mesh);
		}

		{
			load_texture("ninomaru_teien_8k_cube_radiance.octrn", env_map);
			load_texture("Body_SG1_baseColor.octrn", mesh_albedo);
		}

		CHECK_D3D12_CALL(com_command_list->Close());
		ID3D12CommandList* pp_command_lists[] = { com_command_list.Get() };
		com_command_queue->ExecuteCommandLists(count_of(pp_command_lists), pp_command_lists);
		wait_for_gpu();

		mesh.com_vertex_upload_buffer.Reset();
		mesh.com_index_upload_buffer.Reset();
		env_map.com_upload.Reset();
		mesh_albedo.com_upload.Reset();
	}

	{
		D3D12_HEAP_PROPERTIES heap_properties = {};
		heap_properties.Type = D3D12_HEAP_TYPE_UPLOAD;
		D3D12_RESOURCE_DESC resource_desc = {};
		resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		resource_desc.Width = sizeof(per_frame_cb)*max_inflight_frame_count;
		resource_desc.Height = 1;
		resource_desc.DepthOrArraySize = 1;
		resource_desc.MipLevels = 1;
		resource_desc.Format = DXGI_FORMAT_UNKNOWN;
		resource_desc.SampleDesc.Count = 1;
		resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		CHECK_D3D12_CALL(com_device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&per_frame_cb.com_buffer)));
		D3D12_RANGE cpu_visible_range = {0, 0};
		per_frame_cb.com_buffer->Map(0, &cpu_visible_range, &per_frame_cb.cpu_virtual_address);
		per_frame_cb.gpu_virtual_address = per_frame_cb.com_buffer->GetGPUVirtualAddress();
	}


	D3D12_CPU_DESCRIPTOR_HANDLE gui_font_srv_cpu_desc_handle = cbv_srv_uav_desc_heap.get_cpu_handle(cbv_srv_uav_desc_heap.num_used_descriptors); 
	D3D12_GPU_DESCRIPTOR_HANDLE gui_font_srv_gpu_desc_handle = cbv_srv_uav_desc_heap.get_gpu_handle(cbv_srv_uav_desc_heap.num_used_descriptors);
	cbv_srv_uav_desc_heap.num_used_descriptors++;
	unq_gui = make_unique<Gui>(unq_window->get_handle(), max_inflight_frame_count, com_device.Get(), back_buffer_format, gui_font_srv_cpu_desc_handle, gui_font_srv_gpu_desc_handle);

	// init camera
	{
		camera.aspect_ratio = (float)back_buffer_width / back_buffer_height;
		camera.vertical_fov_in_degrees = 59.0;
		camera.near_plane_in_meters = 1.0;
		camera.far_plane_in_meters = 10000.0;

		camera.pos_ws = { 70.0, 0.0, 20.0 };
		camera.dir_ws = { -1.0, 0.0, 0.0 };
		camera.up_ws = { 0.0, 0.0, 1.0 };

		XMMATRIX xm_clip_from_view = XMMatrixTranspose(XMMatrixPerspectiveFovLH(XMConvertToRadians(camera.vertical_fov_in_degrees), camera.aspect_ratio, camera.near_plane_in_meters, camera.far_plane_in_meters));
		XMStoreFloat4x4(&camera.clip_from_view, xm_clip_from_view);

		XMMATRIX xm_clip_from_world;
		{
			float view_zenith_angle_rad = XMConvertToRadians(45.0);
			float view_azimuth_angle_rad = XMConvertToRadians(0.0);
			float view_distance_in_meters = 10.0;
			float cos_theta = cos(view_zenith_angle_rad);
			float sin_theta = sin(view_zenith_angle_rad);
			float cos_phi = cos(view_azimuth_angle_rad);
			float sin_phi = sin(view_azimuth_angle_rad);

			XMFLOAT3 view_x_ws = { -sin_phi, cos_phi, 0.f };
			XMFLOAT3 view_y_ws = { -cos_theta * cos_phi, -cos_theta * sin_phi, sin_theta };
			XMFLOAT3 view_z_ws = { -sin_theta * cos_phi, -sin_theta * sin_phi, -cos_theta };
			XMFLOAT4X4 world_from_view = {
				view_x_ws.x, view_y_ws.x, view_z_ws.x, -view_z_ws.x * view_distance_in_meters,
				view_x_ws.y, view_y_ws.y, view_z_ws.y, -view_z_ws.y * view_distance_in_meters,
				view_x_ws.z, view_y_ws.z, view_z_ws.z, -view_z_ws.z * view_distance_in_meters,
				0.0, 0.0, 0.0, 1.0
			};

			XMMATRIX xm_world_from_view = XMLoadFloat4x4(&world_from_view);
			XMMATRIX xm_view_from_world = XMMatrixInverse(nullptr, xm_world_from_view);
			xm_clip_from_world = XMMatrixMultiply(xm_clip_from_view, xm_view_from_world);
			//XMStoreFloat4x4(&per_frame_cb.constants.clip_from_object, xm_clip_from_world);
		}

		auto xm_world_from_view = XMLoadFloat4x4(&camera.world_from_view);
		auto xm_view_from_world = XMMatrixInverse(nullptr, xm_world_from_view);
		XMStoreFloat4x4(&camera.view_from_world, xm_view_from_world);
		XMStoreFloat4x4(&camera.world_from_view, xm_world_from_view);
	}
}

void update() {

	// update gui
	unq_gui->update(com_command_list.Get());
	auto gui_data = unq_gui->get_data();

	float yaw_rad = camera.yaw_rad;
	yaw_rad += gui_data.delta_yaw_rad;
	if(yaw_rad > XM_PI) yaw_rad -= XM_2PI;
	else if(yaw_rad <= -XM_PI) yaw_rad += XM_2PI;
	camera.yaw_rad = yaw_rad;

	float pitch_rad = camera.pitch_rad;
	pitch_rad += gui_data.delta_pitch_rad;
	pitch_rad = XMMin(XM_PIDIV2, pitch_rad);
	pitch_rad = XMMax(-XM_PIDIV2, pitch_rad);
	camera.pitch_rad = pitch_rad;

	static float move_speed_mps = 50.0f;
	static float speed_scale = 1.0f;
	static float delta_time_s = 0.016666f;

	float forward = move_speed_mps * speed_scale * ((gui_data.is_w_pressed ? delta_time_s : 0.0f) + (gui_data.is_s_pressed ? -delta_time_s : 0.0f));
	float strafe  = move_speed_mps * speed_scale * ((gui_data.is_d_pressed ? delta_time_s : 0.0f) + (gui_data.is_a_pressed ? -delta_time_s : 0.0f));
	float ascent  = move_speed_mps * speed_scale * ((gui_data.is_e_pressed ? delta_time_s : 0.0f) + (gui_data.is_q_pressed ? -delta_time_s : 0.0f));
	
	static float prev_forward = 0.0f;
	static float prev_strafe = 0.0f;
	static float prev_ascent = 0.0f;
	
	auto dampen = [&](float &prev_val, float &new_val) {
		float blended_val;
		XMVECTOR xm_blended_val; 
		XMVECTOR xm_prev_val = XMLoadFloat(&prev_val);
		XMVECTOR xm_new_val = XMLoadFloat(&new_val);
		if(abs(new_val) > abs(prev_val))
			xm_blended_val = XMVectorLerp(xm_new_val, xm_prev_val, pow(0.6f, delta_time_s * 60.0f));
		else
			xm_blended_val = XMVectorLerp(xm_new_val, xm_prev_val, pow(0.8f, delta_time_s * 60.0f));
		XMStoreFloat(&blended_val, xm_blended_val);
		prev_val = blended_val;
		new_val = blended_val;
	};

	dampen(prev_forward, forward);
	dampen(prev_strafe, strafe);
	dampen(prev_ascent, ascent);

	//{
	//	auto cos_theta = cos(XMConvertToRadians(gui_data.view_zenith_angle_in_degrees));
	//	auto sin_theta = sin(XMConvertToRadians(gui_data.view_zenith_angle_in_degrees));
	//	auto cos_phi = cos(XMConvertToRadians(gui_data.view_azimuth_angle_in_degrees));
	//	auto sin_phi = sin(XMConvertToRadians(gui_data.view_azimuth_angle_in_degrees));

	//	XMFLOAT3 view_x_ws = { -sin_phi, cos_phi, 0.f };
	//	XMFLOAT3 view_y_ws = { -cos_theta * cos_phi, -cos_theta * sin_phi, sin_theta };
	//	XMFLOAT3 view_z_ws = { -sin_theta * cos_phi, -sin_theta * sin_phi, -cos_theta };
	//	XMFLOAT4X4 world_from_view = {
	//		view_x_ws.x, view_y_ws.x, view_z_ws.x, -view_z_ws.x * gui_data.view_distance_in_meters,
	//		view_x_ws.y, view_y_ws.y, view_z_ws.y, -view_z_ws.y * gui_data.view_distance_in_meters,
	//		view_x_ws.z, view_y_ws.z, view_z_ws.z, -view_z_ws.z * gui_data.view_distance_in_meters,
	//		0.0, 0.0, 0.0, 1.0
	//	};
	//}

	///////////////////////////////////////////////////////////////

	static const XMMATRIX xm_change_of_basis = { // Left-handed +y : up, +x: right View Space -> Right-handed +z : up, -y: right World Space
		0.0, 0.0,-1.0, 0,
		1.0, 0.0, 0.0, 0,
		0.0, 1.0, 0.0, 0,
		0.0, 0.0, 0.0, 1.0
	};

	{
		auto xm_rotate_y = XMMatrixRotationY(-camera.yaw_rad);
		auto xm_rotate_x = XMMatrixRotationX(-camera.pitch_rad);
		auto xm_world_from_view = xm_change_of_basis * xm_rotate_y * xm_rotate_x;

		// Test
		//XMVECTOR xm_test = {0,0,1};
		//xm_test = XMVector3Transform(xm_test, XMMatrixTranspose(xm_world_from_view));
		
		XMVECTOR xm_movement_vs = { strafe, ascent, forward };
		auto xm_movement_ws = XMVector3Transform(xm_movement_vs, XMMatrixTranspose(xm_world_from_view));

		XMVECTOR xm_offset = XMLoadFloat3(&camera.pos_ws);
		xm_offset += xm_movement_ws;
		XMStoreFloat3(&camera.pos_ws, xm_offset);

		auto xm_translation = XMMatrixTranspose(XMMatrixTranslationFromVector(xm_offset));
		xm_world_from_view = xm_translation * xm_world_from_view;

		auto xm_view_from_world = XMMatrixInverse(nullptr, xm_world_from_view);

		XMStoreFloat4x4(&camera.view_from_world, xm_view_from_world);
		XMStoreFloat4x4(&camera.world_from_view, xm_world_from_view);
	}
	///////////////////////////////////////////////////////////////
	
	per_frame_cb.constants.clip_from_view = camera.clip_from_view;
	per_frame_cb.constants.view_from_world = camera.view_from_world;
	per_frame_cb.constants.world_from_view = camera.world_from_view;
	
	auto per_frame_cb_size = sizeof(per_frame_cb.constants);
	auto *p_dest = reinterpret_cast<uint8_t*>(per_frame_cb.cpu_virtual_address) + per_frame_cb_size * frame_index;
	memcpy(p_dest, &per_frame_cb, per_frame_cb_size);

}

void render_frame() {
	float clear_color[] = { 0.0f, 0.0f, 0.0f, 1.0f };

	CHECK_D3D12_CALL(a_com_command_allocators[frame_index]->Reset());
	CHECK_D3D12_CALL(com_command_list->Reset(a_com_command_allocators[frame_index].Get(), com_pipeline_state_object.Get()));

	com_command_list->SetGraphicsRootSignature(com_root_signature.Get());
	D3D12_VIEWPORT viewport = {};
	viewport.Width = back_buffer_width;
	viewport.Height = back_buffer_height;
	viewport.MaxDepth = 1.0;
	com_command_list->RSSetViewports(1, &viewport);
	D3D12_RECT rect = {};
	rect.right = back_buffer_width;
	rect.bottom = back_buffer_height;
	com_command_list->RSSetScissorRects(1, &rect);
	
	D3D12_RESOURCE_BARRIER resource_barrier = {};
	resource_barrier.Transition.pResource = a_com_render_targets[frame_index].Get();
	resource_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	resource_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	com_command_list->ResourceBarrier(1, &resource_barrier);

	// Draw Scene
	ID3D12DescriptorHeap *a_heaps[] = { cbv_srv_uav_desc_heap.com_heap.Get() };
	com_command_list->SetDescriptorHeaps(count_of(a_heaps), a_heaps);
	com_command_list->SetGraphicsRootConstantBufferView(0, per_frame_cb.gpu_virtual_address + sizeof(per_frame_cb.constants) * frame_index);
	com_command_list->SetGraphicsRootDescriptorTable(1, cbv_srv_uav_desc_heap.base_descriptor);
	// Clear the background to a random color
	D3D12_CPU_DESCRIPTOR_HANDLE rtv_cpu_handle(com_rtv_heap->GetCPUDescriptorHandleForHeapStart());
	rtv_cpu_handle.ptr += frame_index * com_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	D3D12_CPU_DESCRIPTOR_HANDLE dsv_cpu_handle(com_dsv_heap->GetCPUDescriptorHandleForHeapStart());
	com_command_list->OMSetRenderTargets(1, &rtv_cpu_handle, FALSE, &dsv_cpu_handle);
	com_command_list->ClearRenderTargetView(rtv_cpu_handle, clear_color, 0, nullptr);
	com_command_list->ClearDepthStencilView(dsv_cpu_handle, D3D12_CLEAR_FLAG_DEPTH, 1.0, 0, 0, NULL);
	com_command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	
	// Draw mesh
	com_command_list->IASetIndexBuffer(&mesh.ibv);
	com_command_list->IASetVertexBuffers(0,1,&mesh.vbv);
	com_command_list->DrawIndexedInstanced(mesh.header.index_count, 1, 0, 0, 0);

	// Draw GUI
	com_command_list->ClearState(nullptr);
	com_command_list->OMSetRenderTargets(1, &rtv_cpu_handle, FALSE, nullptr);
	com_command_list->SetDescriptorHeaps(count_of(a_heaps), a_heaps);
	unq_gui->render();

	resource_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	resource_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	com_command_list->ResourceBarrier(1, &resource_barrier);
	CHECK_D3D12_CALL(com_command_list->Close());
	
	ID3D12CommandList *a_command_lists[] = { com_command_list.Get() };
	com_command_queue->ExecuteCommandLists(count_of(a_command_lists), a_command_lists);
}

void present() {
	CHECK_DXGI_CALL(com_swap_chain->Present(1, 0));
}

void clean_up() {
	wait_for_gpu();
	unq_gui->~Gui();
}

int WINAPI WinMain(HINSTANCE h_instance, HINSTANCE, LPSTR, int nCmdShow) {
	try {
		init(h_instance);
		MSG msg = {};
		while(msg.message != WM_QUIT) {
			if(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
			else {
				update();
				render_frame();
				present();
				prepare_next_frame();
			}
		}
		clean_up();

	} catch(std::exception& ex) {
		OutputDebugString(ex.what());
		MessageBox(unq_window->get_handle(), ex.what(), "", 0);
	}

	return 0;
}