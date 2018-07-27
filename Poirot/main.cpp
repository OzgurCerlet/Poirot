#define _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "external/tiny_gltf/tiny_gltf.h"
#include "external/stb/stb_image_resize.h"

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

#define CHECK_D3D12_CALL(x, y)	{string err_0 = y; string err_1 = _STRINGIZE(x); if(FAILED(x)) { err_0 += err_1; throw std::exception(err_0.c_str());}};
#define CHECK_DXGI_CALL(x)	{if(FAILED(x)) { throw std::exception(_STRINGIZE(x));}};
#define CHECK_WIN32_CALL(x)	check_win32_call(x);

template <typename T, size_t N>
constexpr size_t count_of(T(&)[N]) { return N; }

constexpr uint8_t max_inflight_frame_count = 3;
constexpr uint16_t back_buffer_width = 1280;
constexpr uint16_t back_buffer_height = 720;
constexpr DXGI_FORMAT back_buffer_format = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr uint8_t ms_count = 1;
constexpr uint8_t ms_quality = 0;
bool is_msaa_enabled = false;

unique_ptr<Window> unq_window = nullptr;
unique_ptr<Gui> unq_gui = nullptr;

uint8_t frame_index = 0;
ComPtr<ID3D12Device> com_device = nullptr;
ComPtr<ID3D12CommandQueue> com_command_queue = nullptr;
ComPtr<IDXGISwapChain3> com_swap_chain = nullptr;

ComPtr<ID3D12DescriptorHeap> com_rtv_heap = nullptr;
ComPtr<ID3D12DescriptorHeap> com_dsv_heap = nullptr;
//ComPtr<ID3D12DescriptorHeap> com_cbv_srv_uav_heap = nullptr;

array<ComPtr<ID3D12Resource>, max_inflight_frame_count + 1> a_com_render_targets = {};
ComPtr<ID3D12Resource> com_dsv = nullptr;
uint16_t rtv_descriptor_size = 0;

array<ComPtr<ID3D12CommandAllocator>, max_inflight_frame_count> a_com_command_allocators = {};
ComPtr<ID3D12GraphicsCommandList> com_command_list = nullptr;

array<uint64_t, max_inflight_frame_count> a_fence_values = {};
ComPtr<ID3D12Fence> com_fence = nullptr;
HANDLE h_fence_event = 0;

ComPtr<ID3D12PipelineState> com_scene_opaque_pso = nullptr;
ComPtr<ID3D12PipelineState> com_scene_alpha_blend_pso = nullptr;
ComPtr<ID3D12PipelineState> com_background_pso = nullptr;
ComPtr<ID3D12PipelineState> com_final_pso = nullptr;
ComPtr<ID3D12RootSignature> com_root_signature = nullptr;

struct PerFrameConstantBuffer {
	__declspec(align(256)) struct {
		XMFLOAT4X4 clip_from_view;
		XMFLOAT4X4 view_from_clip;
		XMFLOAT4X4 view_from_world;
		XMFLOAT4X4 world_from_view;
		XMFLOAT3   cam_pos_ws;
	} constants;
	ComPtr<ID3D12Resource> com_buffer;
	void	*cpu_virtual_address;
	uint64_t gpu_virtual_address;
};

constexpr uint16_t k_max_object_count = 32;
constexpr uint16_t k_max_material_count = 32;

struct ObjectTransformationsConstantBuffer {
	__declspec(align(256)) struct {
		XMFLOAT4X4 a_world_from_objects[k_max_object_count];
	} constants;
	ComPtr<ID3D12Resource> com_buffer;
	void	*cpu_virtual_address;
	uint64_t gpu_virtual_address;
};

struct MaterialData {
	XMFLOAT4 base_color_factor = {1,1,1,1};
	float metallic_factor = 1.0;
	float roughness_factor = 1.0 ;
	float alpha_mask_cutoff = 0.0;
	int base_color_texture_index = -1;	
	int normal_texture_index = -1;
	int metallic_roughness_texture_index =-1;
	int emissive_texture_index =-1;
	int occlusion_texture_index =-1;	
	int is_alpha_masked = 0;
	float __pad[3];
};

struct MaterialDataConstantBuffer {
	__declspec(align(256)) struct {
		MaterialData a_material_data[k_max_material_count];
	} constants;
	ComPtr<ID3D12Resource> com_buffer;
	void	*cpu_virtual_address;
	uint64_t gpu_virtual_address;
};

struct MeshHeader {
	int header_size;
	int vertex_count;
	int index_count;
};

struct Mesh {
	MeshHeader header;
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
	uint32_t srv_descriptor_table_index;
};

struct RenderBuffer {
	ComPtr<ID3D12Resource> com_resource;
	uint32_t srv_descriptor_table_index;
	uint32_t rtv_descriptor_table_index;
};

array<RenderBuffer, max_inflight_frame_count> a_back_buffers;
RenderBuffer hdr_buffer;

struct Material {
	enum AlphaMode { ALPHAMODE_OPAQUE, ALPHAMODE_MASK, ALPHAMODE_BLEND };
	AlphaMode alphaMode = ALPHAMODE_OPAQUE;
	float alpha_cutoff = 1.0f;
	float metallic_factor = 1.0f;
	float roughness_factor = 1.0f;
	XMFLOAT4 basecolor_factor = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	Texture *basecolor_texture;
	Texture *metallic_roughness_texture;
	Texture *normal_texture;
	Texture *occlusion_texture;
	Texture *emissive_texture;
	uint32_t index_into_desc_heap;
};

struct Primitive {
	uint32_t first_index;
	uint32_t index_count;
	uint32_t material_index;
};

struct Node {
	Node *p_parent;
	vector<Node*> children;
	string name;
	Mesh *p_mesh;
	vector<Primitive> primitives;
	
	XMMATRIX xm_transform = {};
	XMVECTOR xm_rotation = {};
	XMVECTOR xm_translation = {};
	XMVECTOR xm_scale = { 1.0f, 1.0f, 1.0f, 1.0f };
	
	uint32_t index;
	uint32_t transformation_index;

	void update(ObjectTransformationsConstantBuffer &transformations_cb);
};

void Node::update(ObjectTransformationsConstantBuffer &transformations_cb) {
	if(p_mesh) {
		static uint32_t index = 0;
		transformation_index = index;
		XMVECTOR origin = {0,0,0,0};
		XMVECTOR new_scale = {0.00157,0.00157,0.00157};
		XMMATRIX local_transformation = XMMatrixAffineTransformation(new_scale, origin, xm_rotation, xm_translation);
		XMMATRIX final_transformation = XMMatrixMultiply(local_transformation, xm_transform);
		XMStoreFloat4x4(transformations_cb.constants.a_world_from_objects + index++, final_transformation);
		for(auto& child : children) {
			child->update(transformations_cb);
		}
	}
}

struct Vertex {
	XMFLOAT3 pos;
	XMFLOAT3 normal;
	XMFLOAT2 uv;
};

struct Scene {
	vector<Texture*> textures;
	vector<Material*> materials;
	vector<Mesh*> meshes;
	vector<Node*> nodes;
	vector<Node*> linear_nodes;
};

struct Camera {
	XMFLOAT4X4 clip_from_view;
	XMFLOAT4X4 view_from_clip;
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
		CHECK_D3D12_CALL(com_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&com_heap)),"");

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

DescriptorHeap cbv_srv_uav_desc_heap = {64u, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV , true};

vector<Texture*> env_maps = {};
Texture mesh_albedo = {};
Mesh mesh = {};

PerFrameConstantBuffer per_frame_cb = {};
ObjectTransformationsConstantBuffer transformations_cb = {};
MaterialDataConstantBuffer material_data_cb = {};

Camera camera;
Scene scene = {};

float view_zenith_angle_rad = XMConvertToRadians(45.0);
float view_azimuth_angle_rad = XMConvertToRadians(0.0);
float view_distance_in_meters = 10.0;

inline void check_win32_call(HANDLE h) {
	if(h == NULL) { throw std::exception(); };
}

inline void set_name(ComPtr<ID3D12Resource> &com_resource, string name) {
	wstring name_w(name.begin(), name.end());
	com_resource->SetName(name_w.c_str());
}

void wait_for_gpu() {
	CHECK_D3D12_CALL(com_command_queue->Signal(com_fence.Get(), a_fence_values[frame_index]),"");
	CHECK_D3D12_CALL(com_fence->SetEventOnCompletion(a_fence_values[frame_index], h_fence_event),"");
	WaitForSingleObjectEx(h_fence_event, INFINITE, FALSE);
	a_fence_values[frame_index]++;
}

void prepare_next_frame() {
	uint64_t current_fence_value = a_fence_values[frame_index];
	CHECK_D3D12_CALL(com_command_queue->Signal(com_fence.Get(), current_fence_value),"");
	frame_index = com_swap_chain->GetCurrentBackBufferIndex();
	if(com_fence->GetCompletedValue() < a_fence_values[frame_index]) {
		CHECK_D3D12_CALL(com_fence->SetEventOnCompletion(a_fence_values[frame_index], h_fence_event),"");
		WaitForSingleObjectEx(h_fence_event, INFINITE, FALSE);
	}

	a_fence_values[frame_index] = current_fence_value + 1;
}

void load_mesh(uint32_t vertex_count, uint32_t index_count, const void *p_vertex_data, const void *p_index_data, Mesh &mesh) {
	
	uint32_t vertex_size = sizeof(Vertex);
	uint32_t vertex_buffer_size = vertex_count * vertex_size;
	uint32_t index_buffer_size = index_count * sizeof(uint32_t);

	mesh.header.vertex_count = vertex_count;
	mesh.header.index_count = index_count;

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

		CHECK_D3D12_CALL(com_device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&mesh.com_vertex_buffer)), "");
		set_name(mesh.com_vertex_buffer, _STRINGIZE(__LINE__));
		heap_properties.Type = D3D12_HEAP_TYPE_UPLOAD;
		CHECK_D3D12_CALL(com_device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mesh.com_vertex_upload_buffer)), "");

		D3D12_RESOURCE_BARRIER resource_barrier = {};
		resource_barrier.Transition.pResource = mesh.com_vertex_buffer.Get();
		resource_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
		resource_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
		com_command_list->ResourceBarrier(1, &resource_barrier);

		void *p_data = nullptr;
		CHECK_D3D12_CALL(mesh.com_vertex_upload_buffer->Map(0, nullptr, &p_data), "");
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

		CHECK_D3D12_CALL(com_device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&mesh.com_index_buffer)),"");
		set_name(mesh.com_vertex_buffer, _STRINGIZE(__LINE__));
		heap_properties.Type = D3D12_HEAP_TYPE_UPLOAD;
		CHECK_D3D12_CALL(com_device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mesh.com_index_upload_buffer)),"");

		D3D12_RESOURCE_BARRIER resource_barrier = {};
		resource_barrier.Transition.pResource = mesh.com_index_buffer.Get();
		resource_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
		resource_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
		com_command_list->ResourceBarrier(1, &resource_barrier);

		void *p_data = nullptr;
		CHECK_D3D12_CALL(mesh.com_index_upload_buffer->Map(0, nullptr, &p_data),"");
		memcpy(p_data, p_index_data, index_buffer_size);
		mesh.com_index_upload_buffer->Unmap(0, nullptr);
		com_command_list->CopyBufferRegion(mesh.com_index_buffer.Get(), 0, mesh.com_index_upload_buffer.Get(), 0, index_buffer_size);

		resource_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		resource_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
		com_command_list->ResourceBarrier(1, &resource_barrier);
	}

	mesh.ibv.BufferLocation = mesh.com_index_buffer->GetGPUVirtualAddress();
	mesh.ibv.Format = DXGI_FORMAT_R32_UINT;
	mesh.ibv.SizeInBytes = index_buffer_size;
}

void load_mesh(string asset_filename, Mesh &mesh) {
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

		uint32_t vertex_size = sizeof(Vertex);
		uint32_t vertex_buffer_size = mesh.header.vertex_count * vertex_size;
		uint32_t index_buffer_size = mesh.header.index_count * sizeof(uint32_t);

		void *p_vertex_data = p_data;
		void *p_index_data = (void*)((uint8_t*)p_data + vertex_buffer_size);

		load_mesh(header.num_vertices, header.num_indices, p_vertex_data, p_index_data, mesh);
	}
}

void load_texture(OctarineImageHeader header, string texture_name, void *p_src_data, Texture &tex) {

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

	CHECK_D3D12_CALL(com_device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&tex.com_resource)),"");
	set_name(tex.com_resource, _STRINGIZE(__LINE__));

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

	CHECK_D3D12_CALL(com_device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &upload_resource_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&tex.com_upload)),"");

	D3D12_RESOURCE_BARRIER resource_barrier = {};
	resource_barrier.Transition.pResource = tex.com_resource.Get();
	resource_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	resource_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
	resource_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
	com_command_list->ResourceBarrier(1, &resource_barrier);

	void *p_dst_data = nullptr;
	CHECK_D3D12_CALL(tex.com_upload->Map(0, nullptr, &p_dst_data),"");
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
		tex.srv_descriptor_table_index = cbv_srv_uav_desc_heap.num_used_descriptors++;	
	}
}

void load_texture(string asset_filename, Texture &tex) {
	OctarineImageHeader header = {};
	void *p_src_data = nullptr;

	OCTARINE_IMAGE result = octarine_image_read_from_file(asset_filename.c_str(), &header, &p_src_data);
	if(result != OCTARINE_IMAGE::OCTARINE_IMAGE_OK) { string msg = "File error: " + asset_filename; throw exception(msg.c_str()); };

	load_texture(header, asset_filename, p_src_data, tex);

	free(p_src_data);
}

// TODO(cerlet): use is_srgb info!
void load_texture(tinygltf::Image &image, bool is_srgb, Texture &tex) {
	size_t image_size = 0;
	uint8_t *p_image_data = nullptr;
	{
		if(image.component == 3) {
			image_size = image.width * image.height * 4;
			p_image_data = new uint8_t[image_size];
			uint8_t* p_rgba = p_image_data;
			uint8_t* p_rgb = &image.image[0];
			for(size_t i = 0; i < image.width * image.height; ++i) {
				for(int32_t j = 0; j < 3; ++j) {
					p_rgba[j] = p_rgb[j];
				}
				p_rgba += 4;
				p_rgb += 3;
			}
		}
		else {
			p_image_data = &image.image[0];
			image_size = image.image.size();
		}
	}

	// Mipmap generation!
	int mip_levels = 1;
	size_t image_with_mips_size = image_size;
	uint8_t *p_image_with_mips_data = nullptr;
	{
		auto is_power_of_2 = [](int value, int &power){
			if((value && !(value & (value - 1)))) {
				static const unsigned int b[] = { 0xAAAAAAAA, 0xCCCCCCCC, 0xF0F0F0F0,0xFF00FF00, 0xFFFF0000 };
				register unsigned int r = (value & b[0]) != 0;
				r |= ((value & b[4]) != 0) << 4;
				r |= ((value & b[3]) != 0) << 3;
				r |= ((value & b[2]) != 0) << 2;
				r |= ((value & b[1]) != 0) << 1;
				power = r;
				return true;
			}
			else {
				return false;
			}
		};

		// For now only deal with square images with power of 2 width!
		int power = 0;
		if((image.width == image.height) && is_power_of_2(image.width, power)) {
			int size = image.width;
			mip_levels = power + 1;
			const int pixel_size = 4;
			unsigned char *p_input = p_image_data;
			unsigned char *p_output = reinterpret_cast<unsigned char *>(malloc(size*(size>>1)*3* pixel_size));
			memcpy(p_output, p_input, image_size);
			p_image_with_mips_data = p_output;
			p_output += image_size;
			
			int success = 1;
			while((size > 1) && success) {
				if(is_srgb) {
					success = stbir_resize_uint8_srgb(p_input, size, size, size*pixel_size, p_output, size >> 1, size >> 1, (size >> 1)* pixel_size, 4, STBIR_ALPHA_CHANNEL_NONE, 0);
				}
				else {
					success = stbir_resize_uint8(p_input, size, size, size*pixel_size, p_output, size >> 1, size >> 1, (size >> 1)* pixel_size, 4);
				}
				size = size >> 1;
				
				size_t mip_size = size * size * pixel_size;
				p_input = p_output;
				p_output += mip_size;
				image_with_mips_size += mip_size;
			}

			if(!success) {
				mip_levels = 1;
				free(p_image_with_mips_data);
				p_image_with_mips_data = p_image_data;
				image_with_mips_size = image_size;
			}
		}
	}
	
	OctarineImageHeader header = {};
	header.width = image.width;
	header.height = image.height;
	header.depth = 1;
	header.array_size = 1;
	header.format = octarine_image_make_format_from_dxgi_format(is_srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM);
	header.mip_levels = mip_levels;
	header.size_of_data = image_with_mips_size;
	header.flags = 0;

	load_texture(header, image.name, p_image_with_mips_data, tex);
	
	delete(p_image_with_mips_data);
	if(image.component == 3) delete(p_image_data);
}

void load_textures(tinygltf::Model &gltf_model, Scene& scene) {
	
	vector<int> srgb_image_indices;
	srgb_image_indices.reserve(gltf_model.images.size());

	for(auto &material : gltf_model.materials) {
		auto it = material.values.find("baseColorTexture");
		if(it != material.values.end()) {
			srgb_image_indices.push_back(gltf_model.textures[it->second.TextureIndex()].source);
		}
		it = material.additionalValues.find("emissiveTexture");
		if(it != material.additionalValues.end()) {
			srgb_image_indices.push_back(gltf_model.textures[it->second.TextureIndex()].source);
		}
	}

	auto is_srgb = [&](int image_index) { 
		return find(srgb_image_indices.begin(), srgb_image_indices.end(), image_index) != srgb_image_indices.end(); 
	};

	int image_index = 0;
	for(auto &image : gltf_model.images) {
		Texture* p_tex = new Texture;
		load_texture(image, is_srgb(image_index++), *p_tex);
		scene.textures.push_back(p_tex);
	}
}

void load_materials(tinygltf::Model &gltf_model, Scene &scene) {
	uint32_t material_index = 0;
	for(auto &mat : gltf_model.materials) {
		Material *p_material = new Material();
		MaterialData *p_material_data = &material_data_cb.constants.a_material_data[material_index];
		p_material->index_into_desc_heap = cbv_srv_uav_desc_heap.num_used_descriptors;

		auto it = mat.values.find("baseColorTexture");
		if( it != mat.values.end()) {
			Texture *p_texture = scene.textures[gltf_model.textures[it->second.TextureIndex()].source];
			p_material->basecolor_texture = p_texture;
			p_material_data->base_color_texture_index = p_texture->srv_descriptor_table_index;
		}
		
		it = mat.additionalValues.find("normalTexture");
		if(it != mat.additionalValues.end()) {
			Texture *p_texture = scene.textures[gltf_model.textures[it->second.TextureIndex()].source];
			p_material->normal_texture = p_texture;
			p_material_data->normal_texture_index = p_texture->srv_descriptor_table_index;;
		}
		
		it = mat.additionalValues.find("occlusionTexture");
		if(it != mat.additionalValues.end()) {
			Texture *p_texture = scene.textures[gltf_model.textures[it->second.TextureIndex()].source];
			p_material->occlusion_texture = p_texture;
			p_material_data->occlusion_texture_index = p_texture->srv_descriptor_table_index;
		}
		
		it = mat.values.find("metallicRoughnessTexture");
		if(it != mat.values.end()) {
			Texture *p_texture = scene.textures[gltf_model.textures[it->second.TextureIndex()].source];
			p_material->metallic_roughness_texture = p_texture;
			p_material_data->metallic_roughness_texture_index = p_texture->srv_descriptor_table_index;
		}
		
		it = mat.additionalValues.find("emissiveTexture");
		if(it != mat.additionalValues.end()) {
			Texture *p_texture = scene.textures[gltf_model.textures[it->second.TextureIndex()].source];
			p_material->emissive_texture = p_texture;
			p_material_data->emissive_texture_index = p_texture->srv_descriptor_table_index;
		}
		
		it = mat.values.find("roughnessFactor");
		if(it != mat.values.end()) {
			p_material->roughness_factor = static_cast<float>(mat.values["roughnessFactor"].Factor());
			p_material_data->roughness_factor = p_material->roughness_factor;
		}

		it = mat.values.find("metallicFactor");
		if(it != mat.values.end()) {
			p_material->metallic_factor = static_cast<float>(mat.values["metallicFactor"].Factor());
			p_material_data->metallic_factor = p_material->metallic_factor;
		}

		it = mat.values.find("baseColorFactor");
		if(it != mat.values.end()) {
			tinygltf::ColorValue color = mat.values["baseColorFactor"].ColorFactor();
			p_material->basecolor_factor = XMFLOAT4(color[0], color[1], color[2], color[3]);
			p_material_data->base_color_factor = p_material->basecolor_factor;
		}

		it = mat.additionalValues.find("alphaMode");
		if(it != mat.additionalValues.end()) {
			tinygltf::Parameter param = mat.additionalValues["alphaMode"];
			if(param.string_value == "BLEND") {
				p_material->alphaMode = Material::ALPHAMODE_BLEND;
			}
			if(param.string_value == "MASK") {
				p_material->alphaMode = Material::ALPHAMODE_MASK;
				p_material_data->is_alpha_masked = 1;
			}
		}
		
		it = mat.additionalValues.find("alphaCutoff");
		if( it != mat.additionalValues.end()) {
			p_material->alpha_cutoff = static_cast<float>(mat.additionalValues["alphaCutoff"].Factor());
			p_material_data->alpha_mask_cutoff = p_material->alpha_cutoff;
		}

		scene.materials.push_back(p_material);
		++material_index;
	}
}

void load_node(Node *p_parent, const tinygltf::Node &node, uint32_t node_index, const tinygltf::Model &model, vector<uint32_t>& index_buffer, vector<Vertex>& vertex_buffer, Scene& scene) {
	Node *p_node = new Node{};
	p_node->index = node_index;
	p_node->p_parent = p_parent;
	p_node->name = node.name;
	p_node->xm_transform = XMMatrixIdentity();

	// Generate local node matrix
	if(node.translation.size() == 3) {
		XMFLOAT3 translation = { static_cast<float>(node.translation[0]), static_cast<float>(node.translation[1]), static_cast<float>(node.translation[2]) };
		p_node->xm_translation = XMLoadFloat3(&translation);
	}

	if(node.rotation.size() == 4) {
		XMFLOAT4 rotation = { static_cast<float>(node.rotation[0]), static_cast<float>(node.rotation[1]), static_cast<float>(node.rotation[2]), static_cast<float>(node.rotation[3]) };
		p_node->xm_rotation = XMLoadFloat4(&rotation);
	}

	if(node.scale.size() == 3) {
		XMFLOAT3 scale = { static_cast<float>(node.scale[0]), static_cast<float>(node.scale[1]), static_cast<float>(node.scale[2]) };
		p_node->xm_scale = XMLoadFloat3(&scale);
	}

	if(node.matrix.size() == 16) {
		p_node->xm_transform = XMMatrixSet(
			node.matrix[0],  node.matrix[1],  node.matrix[2],  node.matrix[3],
			node.matrix[4],  node.matrix[5],  node.matrix[6],  node.matrix[7],
			node.matrix[8],  node.matrix[9],  node.matrix[10], node.matrix[11], 
			node.matrix[12], node.matrix[13], node.matrix[14], node.matrix[15]
		);
	};

	// Node with children
	if(node.children.size() > 0) {
		for(auto i = 0; i < node.children.size(); i++) {
			load_node(p_node, model.nodes[node.children[i]], node.children[i], model, index_buffer, vertex_buffer, scene);
		}
	}

	// Node contains mesh data
	if(node.mesh > -1) {
		const tinygltf::Mesh mesh = model.meshes[node.mesh];
		for(size_t i = 0; i < mesh.primitives.size(); i++) {
			const tinygltf::Primitive &primitive = mesh.primitives[i];
			if(primitive.indices < 0) {
				continue;
			}
			uint32_t index_buffer_start = static_cast<uint32_t>(index_buffer.size());
			uint32_t vertex_buffer_start = static_cast<uint32_t>(vertex_buffer.size());
			uint32_t index_count = 0;
			// Vertices
			{
				const float *p_pos_buffer = nullptr;
				const float *p_nor_buffer = nullptr;
				const float *p_uv_buffer = nullptr;

				// Position attribute is required
				auto it = primitive.attributes.find("POSITION");
				assert(it != primitive.attributes.end());

				const tinygltf::Accessor &posAccessor = model.accessors[it->second];
				const tinygltf::BufferView &posView = model.bufferViews[posAccessor.bufferView];
				p_pos_buffer = reinterpret_cast<const float *>(&(model.buffers[posView.buffer].data[posAccessor.byteOffset + posView.byteOffset]));
				//posMin = glm::vec3(posAccessor.minValues[0], posAccessor.minValues[1], posAccessor.minValues[2]);
				//posMax = glm::vec3(posAccessor.maxValues[0], posAccessor.maxValues[1], posAccessor.maxValues[2]);

				it = primitive.attributes.find("NORMAL");
				if( it != primitive.attributes.end()) {
					const tinygltf::Accessor &normAccessor = model.accessors[it->second];
					const tinygltf::BufferView &normView = model.bufferViews[normAccessor.bufferView];
					p_nor_buffer = reinterpret_cast<const float *>(&(model.buffers[normView.buffer].data[normAccessor.byteOffset + normView.byteOffset]));
				}
				
				it = primitive.attributes.find("TEXCOORD_0");
				if(it != primitive.attributes.end()) {
					const tinygltf::Accessor &uvAccessor = model.accessors[it->second];
					const tinygltf::BufferView &uvView = model.bufferViews[uvAccessor.bufferView];
					p_uv_buffer = reinterpret_cast<const float *>(&(model.buffers[uvView.buffer].data[uvAccessor.byteOffset + uvView.byteOffset]));
				}

				for(size_t v = 0; v < posAccessor.count; v++) {
					Vertex vert{};
					vert.pos = XMFLOAT3(p_pos_buffer + (v * 3));
					if(p_nor_buffer) {
						XMFLOAT3 normal(p_nor_buffer + (v * 3));
						auto xm_normal = XMVector3Normalize(XMLoadFloat3(&normal));
						XMStoreFloat3(&vert.normal, xm_normal);
					}
					if(p_uv_buffer) {
						vert.uv = XMFLOAT2(p_uv_buffer + (v * 2));
					}
					vertex_buffer.push_back(vert);
				}
			}
			// Indices
			{
				const tinygltf::Accessor &accessor = model.accessors[primitive.indices];
				const tinygltf::BufferView &bufferView = model.bufferViews[accessor.bufferView];
				const tinygltf::Buffer &buffer = model.buffers[bufferView.buffer];

				index_count = static_cast<uint32_t>(accessor.count);

				switch(accessor.componentType) {
					case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: {
						uint32_t *buf = new uint32_t[accessor.count];
						memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint32_t));
						for(size_t index = 0; index < accessor.count; index++) {
							index_buffer.push_back(buf[index] + vertex_buffer_start);
						}
						delete[] buf;
						break;
					}
					case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: {
						uint16_t *buf = new uint16_t[accessor.count];
						memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint16_t));
						for(size_t index = 0; index < accessor.count; index++) {
							index_buffer.push_back(buf[index] + vertex_buffer_start);
						}
						delete[] buf;
						break;
					}
					case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: {
						uint8_t *buf = new uint8_t[accessor.count];
						memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint8_t));
						for(size_t index = 0; index < accessor.count; index++) {
							index_buffer.push_back(buf[index] + vertex_buffer_start);
						}
						delete[] buf;
						break;
					}
					default: throw exception("Index component type not supported!");
				}
			}			
			Primitive new_primitive;
			new_primitive.material_index = primitive.material;
			new_primitive.first_index = index_buffer_start;
			new_primitive.index_count = index_count;
			p_node->primitives.push_back(new_primitive);
		}
		Mesh *p_mesh = new Mesh();
		load_mesh(vertex_buffer.size(), index_buffer.size(), static_cast<const void*>(vertex_buffer.data()), static_cast<const void*>(index_buffer.data()), *p_mesh);
		scene.meshes.push_back(p_mesh);
		p_node->p_mesh = p_mesh;
	}
	if(p_node->p_parent) {
		p_node->p_parent->children.push_back(p_node);
	}
	else {
		scene.nodes.push_back(p_node);
	}
	scene.linear_nodes.push_back(p_node);
}

void load_scene(string asset_filename, Scene &scene) {
	tinygltf::Model gltf_model;
	tinygltf::TinyGLTF gltf_ctx;
	std::string err;

	bool is_loaded = gltf_ctx.LoadASCIIFromFile(&gltf_model, &err, asset_filename.c_str());
	if(!is_loaded) { throw exception(err.c_str()); }

	load_textures(gltf_model, scene);
	load_materials(gltf_model, scene);
	
	vector<uint32_t> index_buffer;
	vector<Vertex> vertex_buffer;
	
	const tinygltf::Scene &gltf_scene = gltf_model.scenes[gltf_model.defaultScene];
	for(size_t i = 0; i < gltf_scene.nodes.size(); i++) {
		const tinygltf::Node node = gltf_model.nodes[gltf_scene.nodes[i]];
		load_node(nullptr, node, gltf_scene.nodes[i], gltf_model, index_buffer, vertex_buffer, scene);
	}

	for(auto node : scene.linear_nodes) {
		if(node->p_mesh) {
			node->update(transformations_cb);
		}
	}
}

void create_root_signature() {
	D3D12_FEATURE_DATA_ROOT_SIGNATURE feature_data = {};
	feature_data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
	if(FAILED(com_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &feature_data, sizeof(feature_data)))) {
		feature_data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
	}

	D3D12_ROOT_PARAMETER1 a_root_params[5] = {};
	// per draw constant
	a_root_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	a_root_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	a_root_params[0].Constants.ShaderRegister = 2;
	a_root_params[0].Constants.RegisterSpace = 0;
	a_root_params[0].Constants.Num32BitValues = 2;

	// per frame cbv descriptor 
	a_root_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	a_root_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	a_root_params[1].Descriptor.ShaderRegister = 0;
	a_root_params[1].Descriptor.RegisterSpace = 0;
	a_root_params[1].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE;

	// transformations cbv descriptor 
	a_root_params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	a_root_params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	a_root_params[2].Descriptor.ShaderRegister = 1;
	a_root_params[2].Descriptor.RegisterSpace = 0;
	a_root_params[2].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE;

	// material data cbv descriptor 
	a_root_params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	a_root_params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	a_root_params[3].Descriptor.ShaderRegister = 1;
	a_root_params[3].Descriptor.RegisterSpace = 0;
	a_root_params[3].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE;

	// srv desc table parameter
	D3D12_DESCRIPTOR_RANGE1 a_srv_descriptor_ranges[2] = {};
	a_srv_descriptor_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	a_srv_descriptor_ranges[0].NumDescriptors = env_maps.size()+1;
	a_srv_descriptor_ranges[0].BaseShaderRegister = 0;
	a_srv_descriptor_ranges[0].RegisterSpace = 0;
	a_srv_descriptor_ranges[0].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
	a_srv_descriptor_ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	a_srv_descriptor_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	a_srv_descriptor_ranges[1].NumDescriptors = UINT_MAX;
	a_srv_descriptor_ranges[1].BaseShaderRegister = 0;
	a_srv_descriptor_ranges[1].RegisterSpace = 1;
	a_srv_descriptor_ranges[1].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
	a_srv_descriptor_ranges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	a_root_params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	a_root_params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	a_root_params[4].DescriptorTable.NumDescriptorRanges = count_of(a_srv_descriptor_ranges);
	a_root_params[4].DescriptorTable.pDescriptorRanges = a_srv_descriptor_ranges;

	D3D12_STATIC_SAMPLER_DESC static_sampler_0 = {};
	static_sampler_0.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	static_sampler_0.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	static_sampler_0.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	static_sampler_0.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	static_sampler_0.MipLODBias = 0;
	static_sampler_0.MaxAnisotropy = 1;
	static_sampler_0.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	static_sampler_0.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
	static_sampler_0.MinLOD = 0;
	static_sampler_0.MaxLOD = D3D12_FLOAT32_MAX;
	static_sampler_0.ShaderRegister = 0;
	static_sampler_0.RegisterSpace = 0;
	static_sampler_0.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_STATIC_SAMPLER_DESC static_sampler_1 = {};
	static_sampler_1.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	static_sampler_1.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	static_sampler_1.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	static_sampler_1.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	static_sampler_1.MipLODBias = 0;
	static_sampler_1.MaxAnisotropy = 16;
	static_sampler_1.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	static_sampler_1.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
	static_sampler_1.MinLOD = 0;
	static_sampler_1.MaxLOD = D3D12_FLOAT32_MAX;
	static_sampler_1.ShaderRegister = 1;
	static_sampler_1.RegisterSpace = 0;
	static_sampler_1.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_STATIC_SAMPLER_DESC a_static_samplers[] = { static_sampler_0, static_sampler_1 };

	D3D12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc = {};
	root_signature_desc.Version = feature_data.HighestVersion;
	root_signature_desc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	root_signature_desc.Desc_1_1.NumParameters = count_of(a_root_params);
	root_signature_desc.Desc_1_1.pParameters = a_root_params;
	root_signature_desc.Desc_1_1.NumStaticSamplers = count_of(a_static_samplers);
	root_signature_desc.Desc_1_1.pStaticSamplers = a_static_samplers;

	ComPtr<ID3DBlob> signature;
	ComPtr<ID3DBlob> error;
	CHECK_D3D12_CALL(D3D12SerializeVersionedRootSignature(&root_signature_desc, &signature, &error), error ? (char*)error->GetBufferPointer() : "");
	CHECK_D3D12_CALL(com_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&com_root_signature)),"");
}

void create_pipeline_state_objects() {
	ComPtr<ID3DBlob> com_vertex_shader;
	ComPtr<ID3DBlob> com_pixel_shader;
	ComPtr<ID3DBlob> com_full_screen_shader;
	ComPtr<ID3DBlob> com_background_shader;
	ComPtr<ID3DBlob> com_copy_shader;
	ComPtr<ID3DBlob> error;

	uint32_t compile_flags = D3DCOMPILE_PACK_MATRIX_ROW_MAJOR | D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES;
#if defined(_DEBUG)
	// Enable better shader debugging with the graphics debugging tools.
	compile_flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION ;
#endif

	CHECK_D3D12_CALL(D3DCompileFromFile(L"shaders/pbs_vs.hlsl", nullptr, nullptr, "vs_main", "vs_5_1", compile_flags, 0, &com_vertex_shader, &error), error ? (char*)error->GetBufferPointer() : "");
	CHECK_D3D12_CALL(D3DCompileFromFile(L"shaders/pbs_ps.hlsl", nullptr, nullptr, "ps_main", "ps_5_1", compile_flags, 0, &com_pixel_shader, &error), error ? (char*)error->GetBufferPointer() : "");
	CHECK_D3D12_CALL(D3DCompileFromFile(L"shaders/full_screen_vs.hlsl", nullptr, nullptr, "vs_main", "vs_5_1", compile_flags, 0, &com_full_screen_shader, &error), error ? (char*)error->GetBufferPointer() : "");
	CHECK_D3D12_CALL(D3DCompileFromFile(L"shaders/background_ps.hlsl", nullptr, nullptr, "ps_main", "ps_5_1", compile_flags, 0, &com_background_shader, &error), error ? (char*)error->GetBufferPointer() : "");
	CHECK_D3D12_CALL(D3DCompileFromFile(L"shaders/copy_ps.hlsl", nullptr, nullptr, "ps_main", "ps_5_1", compile_flags, 0, &com_copy_shader, &error), error ? (char*)error->GetBufferPointer() : "");

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

	D3D12_BLEND_DESC alpha_blend_desc = {};
	for(uint32_t rt_index = 0; rt_index < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++rt_index) {
		alpha_blend_desc.RenderTarget[rt_index].BlendEnable = true;
		alpha_blend_desc.RenderTarget[rt_index].LogicOpEnable = false;
		alpha_blend_desc.RenderTarget[rt_index].SrcBlend = D3D12_BLEND_SRC_ALPHA;
		alpha_blend_desc.RenderTarget[rt_index].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		alpha_blend_desc.RenderTarget[rt_index].BlendOp = D3D12_BLEND_OP_ADD;
		alpha_blend_desc.RenderTarget[rt_index].SrcBlendAlpha = D3D12_BLEND_ZERO;
		alpha_blend_desc.RenderTarget[rt_index].DestBlendAlpha = D3D12_BLEND_ONE;
		alpha_blend_desc.RenderTarget[rt_index].BlendOpAlpha = D3D12_BLEND_OP_ADD;
		alpha_blend_desc.RenderTarget[rt_index].LogicOp = D3D12_LOGIC_OP_NOOP;
		alpha_blend_desc.RenderTarget[rt_index].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	}
	
	D3D12_DEPTH_STENCIL_DESC default_depth_stencil_desc = {};
	default_depth_stencil_desc.DepthEnable = TRUE;
	default_depth_stencil_desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	default_depth_stencil_desc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	D3D12_DEPTH_STENCIL_DESC alpha_blend_depth_stencil_desc = {};
	alpha_blend_depth_stencil_desc.DepthEnable = TRUE;
	alpha_blend_depth_stencil_desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	alpha_blend_depth_stencil_desc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	D3D12_DEPTH_STENCIL_DESC background_depth_stencil_desc = {};
	background_depth_stencil_desc.DepthEnable = FALSE;
	background_depth_stencil_desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;

	{
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
		pso_desc.RTVFormats[0] = a_com_render_targets[max_inflight_frame_count]->GetDesc().Format;
		pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		pso_desc.SampleDesc.Count = ms_count;
		pso_desc.SampleDesc.Quality = ms_quality;
		CHECK_D3D12_CALL(com_device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&com_scene_opaque_pso)),"");
	}

	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
		pso_desc.InputLayout = { a_input_element_descs, static_cast<UINT>(count_of(a_input_element_descs)) };
		pso_desc.pRootSignature = com_root_signature.Get();
		pso_desc.VS = { com_vertex_shader->GetBufferPointer(), com_vertex_shader->GetBufferSize() };
		pso_desc.PS = { com_pixel_shader->GetBufferPointer(), com_pixel_shader->GetBufferSize() };
		pso_desc.RasterizerState = default_rasterizer_desc;
		pso_desc.BlendState = alpha_blend_desc;
		pso_desc.DepthStencilState = alpha_blend_depth_stencil_desc;
		pso_desc.SampleMask = UINT_MAX;
		pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		pso_desc.NumRenderTargets = 1;
		pso_desc.RTVFormats[0] = a_com_render_targets[max_inflight_frame_count]->GetDesc().Format;
		pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		pso_desc.SampleDesc.Count = ms_count;
		pso_desc.SampleDesc.Quality = ms_quality;
		CHECK_D3D12_CALL(com_device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&com_scene_alpha_blend_pso)), "");
	}

	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
		pso_desc.InputLayout = { a_input_element_descs, static_cast<UINT>(count_of(a_input_element_descs)) };
		pso_desc.pRootSignature = com_root_signature.Get();
		pso_desc.VS = { com_full_screen_shader->GetBufferPointer(), com_full_screen_shader->GetBufferSize() };
		pso_desc.PS = { com_background_shader->GetBufferPointer(), com_background_shader->GetBufferSize() };
		pso_desc.RasterizerState = default_rasterizer_desc;
		pso_desc.BlendState = default_blend_desc;
		pso_desc.DepthStencilState = background_depth_stencil_desc;
		pso_desc.SampleMask = UINT_MAX;
		pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		pso_desc.NumRenderTargets = 1;
		pso_desc.RTVFormats[0] = a_com_render_targets[max_inflight_frame_count]->GetDesc().Format;
		pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		pso_desc.SampleDesc.Count = ms_count;
		pso_desc.SampleDesc.Quality = ms_quality;
		CHECK_D3D12_CALL(com_device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&com_background_pso)), "");
	}

	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
		pso_desc.InputLayout = { a_input_element_descs, static_cast<UINT>(count_of(a_input_element_descs)) };
		pso_desc.pRootSignature = com_root_signature.Get();
		pso_desc.VS = { com_full_screen_shader->GetBufferPointer(), com_full_screen_shader->GetBufferSize() };
		pso_desc.PS = { com_copy_shader->GetBufferPointer(), com_copy_shader->GetBufferSize() };
		pso_desc.RasterizerState = default_rasterizer_desc;
		pso_desc.BlendState = default_blend_desc;
		pso_desc.DepthStencilState = background_depth_stencil_desc;
		pso_desc.SampleMask = UINT_MAX;
		pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		pso_desc.NumRenderTargets = 1;
		pso_desc.RTVFormats[0] = a_com_render_targets[0]->GetDesc().Format;
		pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		pso_desc.SampleDesc.Count = 1;
		pso_desc.SampleDesc.Quality = 0;
		CHECK_D3D12_CALL(com_device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&com_final_pso)), "");
	}

}

void init(HINSTANCE h_instance) {

	unq_window = make_unique<Window>(h_instance, back_buffer_width, back_buffer_height);
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
			CHECK_D3D12_CALL(com_device->CreateCommandQueue(&command_queue_desc, IID_PPV_ARGS(&com_command_queue)), "");

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
			//local_desc_heap.init();
			cbv_srv_uav_desc_heap.init();
		}

		{
			rtv_descriptor_size = com_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

			D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
			rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			rtv_heap_desc.NumDescriptors = max_inflight_frame_count+1;
			CHECK_D3D12_CALL(com_device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&com_rtv_heap)), "");

			D3D12_CPU_DESCRIPTOR_HANDLE rtv_cpu_handle = { com_rtv_heap->GetCPUDescriptorHandleForHeapStart() };
			for(int buffer_index = 0; buffer_index < max_inflight_frame_count; ++buffer_index) {
				CHECK_DXGI_CALL(com_swap_chain->GetBuffer(buffer_index, IID_PPV_ARGS(&(a_com_render_targets[buffer_index]))));
				com_device->CreateRenderTargetView(a_com_render_targets[buffer_index].Get(), nullptr, rtv_cpu_handle);
				rtv_cpu_handle.ptr += rtv_descriptor_size;
			}

			D3D12_HEAP_PROPERTIES heap_properties = {};
			heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
			D3D12_CLEAR_VALUE clear_value = {};
			clear_value.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

			D3D12_RESOURCE_DESC resource_desc = {};
			resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			resource_desc.Width = back_buffer_width;
			resource_desc.Height = back_buffer_height;
			resource_desc.DepthOrArraySize = 1;
			resource_desc.MipLevels = 1;
			resource_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			resource_desc.SampleDesc.Count = ms_count;
			resource_desc.SampleDesc.Quality = ms_quality;
			resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
			resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
			CHECK_D3D12_CALL(com_device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear_value, IID_PPV_ARGS(&(a_com_render_targets[max_inflight_frame_count]))), "");

			D3D12_RENDER_TARGET_VIEW_DESC desc = {};
			desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			desc.ViewDimension = is_msaa_enabled ? D3D12_RTV_DIMENSION_TEXTURE2DMS : D3D12_RTV_DIMENSION_TEXTURE2D;
			com_device->CreateRenderTargetView(a_com_render_targets[max_inflight_frame_count].Get(), &desc, rtv_cpu_handle);
	
			D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
			srv_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srv_desc.ViewDimension = is_msaa_enabled ? D3D12_SRV_DIMENSION_TEXTURE2DMS : D3D12_SRV_DIMENSION_TEXTURE2D;
			srv_desc.Texture2D.MostDetailedMip = 0;
			srv_desc.Texture2D.MipLevels = -1;
			srv_desc.Texture2D.PlaneSlice = 0;
			srv_desc.Texture2D.ResourceMinLODClamp = 0;
			D3D12_CPU_DESCRIPTOR_HANDLE cpu_descriptor_handle = cbv_srv_uav_desc_heap.get_cpu_handle(cbv_srv_uav_desc_heap.num_used_descriptors++);
			com_device->CreateShaderResourceView(a_com_render_targets[max_inflight_frame_count].Get(), &srv_desc, cpu_descriptor_handle);
		}

		{
			for(uint8_t allocator_index = 0; allocator_index < a_com_command_allocators.size(); ++allocator_index) {
				CHECK_D3D12_CALL(com_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&(a_com_command_allocators[allocator_index]))),"");
			}

			CHECK_D3D12_CALL(com_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE::D3D12_COMMAND_LIST_TYPE_DIRECT, a_com_command_allocators[0].Get(), nullptr, IID_PPV_ARGS(&com_command_list)),"");
		}

		{
			CHECK_D3D12_CALL(com_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&com_fence)),"");
			a_fence_values[frame_index]++;
			CHECK_WIN32_CALL(CreateEvent(nullptr, FALSE, FALSE, nullptr));
		}

		// Create the depth-stencil view
		{
			// Describe and create a depth stencil view (DSV) descriptor heap.
			D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc = {};
			dsv_heap_desc.NumDescriptors = 1;
			dsv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
			CHECK_D3D12_CALL(com_device->CreateDescriptorHeap(&dsv_heap_desc, IID_PPV_ARGS(&com_dsv_heap)),"");

			D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
			dsv_desc.Format = DXGI_FORMAT_D32_FLOAT;
			dsv_desc.ViewDimension = is_msaa_enabled ? D3D12_DSV_DIMENSION_TEXTURE2DMS : D3D12_DSV_DIMENSION_TEXTURE2D;
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
			resource_desc.SampleDesc.Count = ms_count;
			resource_desc.SampleDesc.Quality = ms_quality;
			resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
			resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

			CHECK_D3D12_CALL(com_device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &dsc_clear_value, IID_PPV_ARGS(&com_dsv)),"");
			com_device->CreateDepthStencilView(com_dsv.Get(), &dsv_desc, com_dsv_heap->GetCPUDescriptorHandleForHeapStart());
		}

		// Constant buffers
		{
			{
				D3D12_HEAP_PROPERTIES heap_properties = {};
				heap_properties.Type = D3D12_HEAP_TYPE_UPLOAD;
				D3D12_RESOURCE_DESC resource_desc = {};
				resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
				resource_desc.Width = sizeof(per_frame_cb.constants)*max_inflight_frame_count;
				resource_desc.Height = 1;
				resource_desc.DepthOrArraySize = 1;
				resource_desc.MipLevels = 1;
				resource_desc.Format = DXGI_FORMAT_UNKNOWN;
				resource_desc.SampleDesc.Count = 1;
				resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

				CHECK_D3D12_CALL(com_device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&per_frame_cb.com_buffer)),"");
				D3D12_RANGE cpu_visible_range = { 0, 0 };
				per_frame_cb.com_buffer->Map(0, &cpu_visible_range, &per_frame_cb.cpu_virtual_address);
				per_frame_cb.gpu_virtual_address = per_frame_cb.com_buffer->GetGPUVirtualAddress();
			}

			{
				D3D12_HEAP_PROPERTIES heap_properties = {};
				heap_properties.Type = D3D12_HEAP_TYPE_UPLOAD;
				D3D12_RESOURCE_DESC resource_desc = {};
				resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
				resource_desc.Width = sizeof(transformations_cb.constants)*max_inflight_frame_count;
				resource_desc.Height = 1;
				resource_desc.DepthOrArraySize = 1;
				resource_desc.MipLevels = 1;
				resource_desc.Format = DXGI_FORMAT_UNKNOWN;
				resource_desc.SampleDesc.Count = 1;
				resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

				CHECK_D3D12_CALL(com_device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&transformations_cb.com_buffer)),"");
				D3D12_RANGE cpu_visible_range = { 0, 0 };
				transformations_cb.com_buffer->Map(0, &cpu_visible_range, &transformations_cb.cpu_virtual_address);
				transformations_cb.gpu_virtual_address = transformations_cb.com_buffer->GetGPUVirtualAddress();
			}

			{
				D3D12_HEAP_PROPERTIES heap_properties = {};
				heap_properties.Type = D3D12_HEAP_TYPE_UPLOAD;
				D3D12_RESOURCE_DESC resource_desc = {};
				resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
				resource_desc.Width = sizeof(material_data_cb.constants)*max_inflight_frame_count;
				resource_desc.Height = 1;
				resource_desc.DepthOrArraySize = 1;
				resource_desc.MipLevels = 1;
				resource_desc.Format = DXGI_FORMAT_UNKNOWN;
				resource_desc.SampleDesc.Count = 1;
				resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

				CHECK_D3D12_CALL(com_device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&material_data_cb.com_buffer)),"");
				D3D12_RANGE cpu_visible_range = { 0, 0 };
				material_data_cb.com_buffer->Map(0, &cpu_visible_range, &material_data_cb.cpu_virtual_address);
				material_data_cb.gpu_virtual_address = material_data_cb.com_buffer->GetGPUVirtualAddress();
			}

		}

		{
			//load_mesh("Pony_cartoon.octrn", mesh);
		}

		{
			Texture *p_tex = new Texture();
			load_texture("ninomaru_teien_8k_cube_radiance.octrn", *p_tex);
			env_maps.push_back(p_tex);
					
			p_tex = new Texture();
			load_texture("ninomaru_teien_8k_cube_irradiance.octrn", *p_tex);
			env_maps.push_back(p_tex);

			p_tex = new Texture();
			load_texture("ninomaru_teien_8k_cube_specular.octrn", *p_tex);
			env_maps.push_back(p_tex);

			p_tex = new Texture();
			load_texture("brdf_lut.octrn", *p_tex);
			env_maps.push_back(p_tex);

			//p_tex = new Texture();
			//load_texture("irr.octrn", *p_tex);
			//env_maps.push_back(p_tex);

			//p_tex = new Texture();
			//load_texture("spec.octrn", *p_tex);
			//env_maps.push_back(p_tex);

			//p_tex = new Texture();
			//load_texture("lut.octrn", *p_tex);
			//env_maps.push_back(p_tex);
		}

		{
			load_scene("C:/Users/Cerlet/Desktop/3D_models/pony_cartoon_gltf/scene.gltf", scene);
		}

		{ 
			create_root_signature();
			create_pipeline_state_objects();
		}

		CHECK_D3D12_CALL(com_command_list->Close(),"");
		ID3D12CommandList* pp_command_lists[] = { com_command_list.Get() };
		com_command_queue->ExecuteCommandLists(count_of(pp_command_lists), pp_command_lists);
		wait_for_gpu();
	}


	D3D12_CPU_DESCRIPTOR_HANDLE gui_font_srv_cpu_desc_handle = cbv_srv_uav_desc_heap.get_cpu_handle(cbv_srv_uav_desc_heap.num_used_descriptors); 
	D3D12_GPU_DESCRIPTOR_HANDLE gui_font_srv_gpu_desc_handle = cbv_srv_uav_desc_heap.get_gpu_handle(cbv_srv_uav_desc_heap.num_used_descriptors);
	cbv_srv_uav_desc_heap.num_used_descriptors++;
	unq_gui = make_unique<Gui>(unq_window->get_handle(), max_inflight_frame_count, com_device.Get(), back_buffer_format, gui_font_srv_cpu_desc_handle, gui_font_srv_gpu_desc_handle);

	// init camera
	{
		camera.aspect_ratio = (float)back_buffer_width / back_buffer_height;
		camera.vertical_fov_in_degrees = 45.0;
		camera.near_plane_in_meters = 0.1;
		camera.far_plane_in_meters = 256.0;

		camera.pos_ws = { 0.7f, 0.0, 0.2f };
		camera.dir_ws = { -1.0, 0.0, 0.0 };
		camera.up_ws = { 0.0, 0.0, 1.0 };

		XMMATRIX xm_clip_from_view = XMMatrixTranspose(XMMatrixPerspectiveFovLH(XMConvertToRadians(camera.vertical_fov_in_degrees), camera.aspect_ratio, camera.near_plane_in_meters, camera.far_plane_in_meters));
		XMVECTOR determinant;
		XMMATRIX xm_view_from_clip = XMMatrixInverse(&determinant, xm_clip_from_view);
		XMStoreFloat4x4(&camera.clip_from_view, xm_clip_from_view);
		XMStoreFloat4x4(&camera.view_from_clip, xm_view_from_clip);

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

	// update per frame cb
	{
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
		static float speed_scale = 0.01f;
		static float delta_time_s = 0.016666f;

		float forward = move_speed_mps * speed_scale * ((gui_data.is_w_pressed ? delta_time_s : 0.0f) + (gui_data.is_s_pressed ? -delta_time_s : 0.0f));
		float strafe = move_speed_mps * speed_scale * ((gui_data.is_d_pressed ? delta_time_s : 0.0f) + (gui_data.is_a_pressed ? -delta_time_s : 0.0f));
		float ascent = move_speed_mps * speed_scale * ((gui_data.is_e_pressed ? delta_time_s : 0.0f) + (gui_data.is_q_pressed ? -delta_time_s : 0.0f));

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

		per_frame_cb.constants.clip_from_view = camera.clip_from_view;
		per_frame_cb.constants.view_from_clip = camera.view_from_clip;
		per_frame_cb.constants.view_from_world = camera.view_from_world;
		per_frame_cb.constants.world_from_view = camera.world_from_view;
		per_frame_cb.constants.cam_pos_ws = camera.pos_ws;
	}

	// update per frame cb
	{
		auto per_frame_cb_size = sizeof(per_frame_cb.constants);
		auto *p_dest = reinterpret_cast<uint8_t*>(per_frame_cb.cpu_virtual_address) + per_frame_cb_size * frame_index;
		memcpy(p_dest, &(per_frame_cb.constants), per_frame_cb_size);
	}

	// update transformations cb
	{
		auto transformations_cb_size = sizeof(transformations_cb.constants);
		auto *p_dest = reinterpret_cast<uint8_t*>(transformations_cb.cpu_virtual_address) + transformations_cb_size * frame_index;
		memcpy(p_dest, &(transformations_cb.constants), transformations_cb_size);
	}

	// update material data cb 
	{
		auto material_data_cb_size = sizeof(material_data_cb.constants);
		auto *p_dest = reinterpret_cast<uint8_t*>(material_data_cb.cpu_virtual_address) + material_data_cb_size * frame_index;
		memcpy(p_dest, &(material_data_cb.constants), material_data_cb_size);
	}
}

void render_node(const Node &node, Material::AlphaMode alpha_mode) {
	if(node.p_mesh) {
		com_command_list->IASetIndexBuffer(&node.p_mesh->ibv);
		com_command_list->IASetVertexBuffers(0, 1, &node.p_mesh->vbv);
		for(auto primitive : node.primitives) {
			Material *p_material = scene.materials[primitive.material_index];
			if(p_material->alphaMode != alpha_mode) continue;
			uint32_t a_root_constants[] = { node.transformation_index, primitive.material_index };
			com_command_list->SetGraphicsRoot32BitConstants(0, count_of(a_root_constants), a_root_constants, 0);
			com_command_list->DrawIndexedInstanced(primitive.index_count, 1, primitive.first_index, 0, 0);
		}
	}

	for(auto p_child : node.children) {
		render_node(*p_child, alpha_mode);
	}
}

void render_frame() {
	float clear_color[] = { 0.f, 0.f, 0.f, 0.f };

	CHECK_D3D12_CALL(a_com_command_allocators[frame_index]->Reset(),"");
	CHECK_D3D12_CALL(com_command_list->Reset(a_com_command_allocators[frame_index].Get(), com_background_pso.Get()),"");

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

	resource_barrier.Transition.pResource = a_com_render_targets[max_inflight_frame_count].Get();
	resource_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	resource_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	com_command_list->ResourceBarrier(1, &resource_barrier);

	// Draw Scene
	ID3D12DescriptorHeap *a_heaps[] = { cbv_srv_uav_desc_heap.com_heap.Get() };
	com_command_list->SetDescriptorHeaps(count_of(a_heaps), a_heaps);
	com_command_list->SetGraphicsRootConstantBufferView(1, per_frame_cb.gpu_virtual_address + sizeof(per_frame_cb.constants) * frame_index);
	com_command_list->SetGraphicsRootConstantBufferView(2, transformations_cb.gpu_virtual_address + sizeof(transformations_cb.constants) * frame_index);
	com_command_list->SetGraphicsRootConstantBufferView(3, material_data_cb.gpu_virtual_address + sizeof(material_data_cb.constants) * frame_index);
	com_command_list->SetGraphicsRootDescriptorTable(4, cbv_srv_uav_desc_heap.base_descriptor);

	// Clear the background to a random color
	D3D12_CPU_DESCRIPTOR_HANDLE rtv_cpu_handle(com_rtv_heap->GetCPUDescriptorHandleForHeapStart());
	rtv_cpu_handle.ptr += max_inflight_frame_count * com_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	D3D12_CPU_DESCRIPTOR_HANDLE dsv_cpu_handle(com_dsv_heap->GetCPUDescriptorHandleForHeapStart());
	com_command_list->OMSetRenderTargets(1, &rtv_cpu_handle, FALSE, &dsv_cpu_handle);
	com_command_list->ClearRenderTargetView(rtv_cpu_handle, clear_color, 0, nullptr);
	com_command_list->ClearDepthStencilView(dsv_cpu_handle, D3D12_CLEAR_FLAG_DEPTH, 1.0, 0, 0, NULL);

	// Draw Background
	com_command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	com_command_list->DrawInstanced(4, 1, 0, 0);
	
	// Draw Opaque objects
	com_command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	com_command_list->SetPipelineState(com_scene_opaque_pso.Get());
	for(auto node : scene.nodes) {
		render_node(*node, Material::ALPHAMODE_OPAQUE);
	}
	
	// Draw Alpha Blended objects
	com_command_list->SetPipelineState(com_scene_alpha_blend_pso.Get());
	for(auto node : scene.nodes) {
		render_node(*node, Material::ALPHAMODE_BLEND);
	}

	// Copy
	resource_barrier.Transition.pResource = a_com_render_targets[max_inflight_frame_count].Get();
	resource_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	resource_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	com_command_list->ResourceBarrier(1, &resource_barrier);

	com_command_list->SetPipelineState(com_final_pso.Get());
	rtv_cpu_handle = com_rtv_heap->GetCPUDescriptorHandleForHeapStart();
	rtv_cpu_handle.ptr += frame_index * com_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	com_command_list->OMSetRenderTargets(1, &rtv_cpu_handle, FALSE, &dsv_cpu_handle);
	com_command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	com_command_list->DrawInstanced(4, 1, 0, 0);

	// Draw GUI
	com_command_list->OMSetRenderTargets(1, &rtv_cpu_handle, FALSE, nullptr);
	com_command_list->SetDescriptorHeaps(count_of(a_heaps), a_heaps);
	unq_gui->render();

	//com_command_list->ResolveSubresource()
	resource_barrier.Transition.pResource = a_com_render_targets[frame_index].Get();
	resource_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	resource_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	com_command_list->ResourceBarrier(1, &resource_barrier);

	CHECK_D3D12_CALL(com_command_list->Close(),"");
	
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