namespace renderer {
	template<typename T>
	struct ConstantBuffer
	{
		T constants;
		ComPtr<ID3D12Resource> com_buffer;
		void	*cpu_virtual_address;
		uint64_t gpu_virtual_address;

		void init() {
			D3D12_HEAP_PROPERTIES heap_properties = {};
			heap_properties.Type = D3D12_HEAP_TYPE_UPLOAD;
			D3D12_RESOURCE_DESC resource_desc = {};
			resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			resource_desc.Width = sizeof(constants)*max_inflight_frame_count;
			resource_desc.Height = 1;
			resource_desc.DepthOrArraySize = 1;
			resource_desc.MipLevels = 1;
			resource_desc.Format = DXGI_FORMAT_UNKNOWN;
			resource_desc.SampleDesc.Count = 1;
			resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

			CHECK_D3D12_CALL(com_device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&com_buffer)), "");
			D3D12_RANGE cpu_visible_range = { 0, 0 };
			com_buffer->Map(0, &cpu_visible_range, &cpu_virtual_address);
			gpu_virtual_address = com_buffer->GetGPUVirtualAddress();
		}

		uint64_t get_gpu_address() {
			return gpu_virtual_address + sizeof(constants) * frame_index;
		}

		void update() {
			auto cb_size = sizeof(constants);
			auto *p_dest = reinterpret_cast<uint8_t*>(cpu_virtual_address) + cb_size * frame_index;
			memcpy(p_dest, &(constants), cb_size);
		}
	};

	__declspec(align(256)) struct PerFrameConstants
	{
		XMFLOAT4X4 clip_from_view;
		XMFLOAT4X4 view_from_clip;
		XMFLOAT4X4 view_from_world;
		XMFLOAT4X4 world_from_view;
		XMFLOAT3   cam_pos_ws;
	};

	__declspec(align(256)) struct Transformations
	{
		XMFLOAT4X4 a_world_from_objects[max_transformation_count_per_scene];
	};

	__declspec(align(256)) struct MaterialList
	{
		Material a_material_data[max_material_count_per_scene];
	};

	struct RenderBuffer
	{
		ComPtr<ID3D12Resource> com_resource;
		uint32_t srv_descriptor_table_index;
		uint32_t rtv_descriptor_table_index;
		uint16_t width;
		uint16_t height;
		DXGI_FORMAT format;
		bool is_shader_visible;
		bool is_multi_sampled;

		RenderBuffer(uint16_t _width = 0, uint16_t _height = 0, DXGI_FORMAT _format = DXGI_FORMAT_UNKNOWN, bool _is_shader_visible = false, bool _is_multi_sampled = false)
			: width(_width), height(_height), format(_format), is_shader_visible(_is_shader_visible), is_multi_sampled(_is_multi_sampled) {};
	};

	struct DescriptorHeap
	{
		ComPtr<ID3D12DescriptorHeap> com_heap;
		D3D12_GPU_DESCRIPTOR_HANDLE base_gpu_descriptor;
		uint32_t num_max_descriptors;
		uint32_t num_used_descriptors;
		uint32_t descriptor_increment_size;
		D3D12_DESCRIPTOR_HEAP_TYPE type;
		bool is_shader_visible;

		DescriptorHeap(uint32_t num_max_descriptors, D3D12_DESCRIPTOR_HEAP_TYPE type, bool is_shader_visible)
			: num_max_descriptors(num_max_descriptors), type(type), is_shader_visible(is_shader_visible) {
		};

		void init();
		D3D12_CPU_DESCRIPTOR_HANDLE get_cpu_handle(uint32_t descriptor_index);
		D3D12_GPU_DESCRIPTOR_HANDLE get_gpu_handle(uint32_t descriptor_index);
	};

	struct Texture
	{
		ComPtr<ID3D12Resource> com_resource;
		ComPtr<ID3D12Resource> com_upload;
		uint32_t srv_descriptor_table_index;
	};

	struct MeshHeader {
		size_t header_size;
		size_t vertex_count;
		size_t index_count;
	};

	struct Mesh
	{
		MeshHeader header;
		ComPtr<ID3D12Resource> com_vertex_buffer;
		ComPtr<ID3D12Resource> com_index_buffer;
		ComPtr<ID3D12Resource> com_vertex_upload_buffer;
		ComPtr<ID3D12Resource> com_index_upload_buffer;
		D3D12_VERTEX_BUFFER_VIEW vbv;
		D3D12_INDEX_BUFFER_VIEW ibv;
	};

	ComPtr<ID3D12Device> com_device{ nullptr };
	ComPtr<ID3D12CommandQueue> com_command_queue{ nullptr };
	ComPtr<IDXGISwapChain3> com_swap_chain{ nullptr };

	array<ComPtr<ID3D12CommandAllocator>, max_inflight_frame_count> a_com_command_allocators{};
	ComPtr<ID3D12GraphicsCommandList> com_command_list{ nullptr };

	ComPtr<ID3D12PipelineState> com_scene_opaque_pso{ nullptr };
	ComPtr<ID3D12PipelineState> com_scene_alpha_blend_pso{ nullptr };
	ComPtr<ID3D12PipelineState> com_background_pso{ nullptr };
	ComPtr<ID3D12PipelineState> com_final_pso{ nullptr };
	ComPtr<ID3D12RootSignature> com_root_signature{ nullptr };

	array<uint64_t, max_inflight_frame_count> a_fence_values{};
	ComPtr<ID3D12Fence> com_fence{ nullptr };
	HANDLE h_fence_event{ 0 };
	uint8_t frame_index{ 0 };

	array<RenderBuffer, max_inflight_frame_count> a_back_buffers{};
	RenderBuffer hdr_buffer{ back_buffer_width , back_buffer_height, DXGI_FORMAT_R16G16B16A16_FLOAT, true, is_msaa_enabled };
	RenderBuffer hdr_buffer_resolved{ back_buffer_width , back_buffer_height, DXGI_FORMAT_R16G16B16A16_FLOAT, true, false };
	RenderBuffer depth_buffer{ back_buffer_width , back_buffer_height, DXGI_FORMAT_D32_FLOAT, false, is_msaa_enabled };

	DescriptorHeap source_srv_desc_heap{ 256u, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV , false };
	DescriptorHeap target_srv_desc_heap{ max_descriptor_count_per_frame * max_inflight_frame_count, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV , true };
	DescriptorHeap rtv_desc_heap{ 64u, D3D12_DESCRIPTOR_HEAP_TYPE_RTV , false };
	DescriptorHeap dsv_desc_heap{ 1u, D3D12_DESCRIPTOR_HEAP_TYPE_DSV , false };

	ConstantBuffer<PerFrameConstants> per_frame_cb;
	ConstantBuffer<Transformations> transformations_cb;
	ConstantBuffer<MaterialList> material_list_cb;

	using TextureList = array<Texture, max_texture_count>;
	TextureList a_textures{};
	uint32_t num_used_texture{ 0 };

	using MeshList = array<Mesh, max_mesh_count>;
	MeshList a_meshes{};
	uint32_t num_used_mesh{ 0 };

	uint32_t current_env_index{0};
	uint32_t current_background_index{0};
	uint32_t current_specular_mip_level{0};
	uint32_t current_isolation_mode_index{0};
	float test{ 0.f };

	void DescriptorHeap::init() {
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.NumDescriptors = num_max_descriptors;
		desc.Type = type;
		desc.Flags = is_shader_visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		CHECK_D3D12_CALL(com_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&com_heap)), "");

		descriptor_increment_size = com_device->GetDescriptorHandleIncrementSize(type);
		base_gpu_descriptor = com_heap->GetGPUDescriptorHandleForHeapStart();
	}

	D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHeap::get_cpu_handle(uint32_t descriptor_index) {
		if(descriptor_index >= num_max_descriptors) { throw exception("Not enough descriptors left in this heap"); }
		D3D12_CPU_DESCRIPTOR_HANDLE cpu_descriptor_handle = com_heap->GetCPUDescriptorHandleForHeapStart();
		cpu_descriptor_handle.ptr += descriptor_increment_size * descriptor_index;
		return cpu_descriptor_handle;
	};

	D3D12_GPU_DESCRIPTOR_HANDLE DescriptorHeap::get_gpu_handle(uint32_t descriptor_index) {
		if(descriptor_index >= num_max_descriptors) { throw exception("Not enough descriptors left in this heap"); }
		D3D12_GPU_DESCRIPTOR_HANDLE gpu_descriptor_handle = base_gpu_descriptor;
		gpu_descriptor_handle.ptr += descriptor_increment_size * descriptor_index;
		return gpu_descriptor_handle;
	};

	inline void set_name(ComPtr<ID3D12Resource> &com_resource, const string &name) {
		wstring name_w(name.begin(), name.end());
		com_resource->SetName(name_w.c_str());
	}

	Texture& get_texture_to_fill(uint32_t &index) {
		return a_textures[index = num_used_texture++];
	}

	Mesh& get_mesh_to_fill(uint32_t &index) {
		return a_meshes[index = num_used_mesh++];
	};

	ID3D12Device *get_device() {
		return com_device.Get();
	}
	
	ID3D12GraphicsCommandList *get_command_list() {
		return com_command_list.Get();
	}

	pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> get_handles_for_a_srv_desc() {
		auto gui_font_srv_cpu_desc_handle = target_srv_desc_heap.get_cpu_handle(target_srv_desc_heap.num_used_descriptors);
		auto gui_font_srv_gpu_desc_handle = target_srv_desc_heap.get_gpu_handle(target_srv_desc_heap.num_used_descriptors);
		target_srv_desc_heap.num_used_descriptors++;
		return make_pair(gui_font_srv_cpu_desc_handle, gui_font_srv_gpu_desc_handle);
	}

	void load_texture(OctarineImageHeader header, string texture_name, const void *p_src_data, uint32_t &tex_index) {
		auto& srv_heap = source_srv_desc_heap;
		auto& tex = get_texture_to_fill(tex_index);

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

		CHECK_D3D12_CALL(com_device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&tex.com_resource)), "");
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

		CHECK_D3D12_CALL(com_device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &upload_resource_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&tex.com_upload)), "");

		D3D12_RESOURCE_BARRIER resource_barrier = {};
		resource_barrier.Transition.pResource = tex.com_resource.Get();
		resource_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		resource_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
		resource_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
		com_command_list->ResourceBarrier(1, &resource_barrier);

		void *p_dst_data = nullptr;
		CHECK_D3D12_CALL(tex.com_upload->Map(0, nullptr, &p_dst_data), "");
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

			D3D12_CPU_DESCRIPTOR_HANDLE cpu_descriptor_handle = srv_heap.get_cpu_handle(srv_heap.num_used_descriptors);
			com_device->CreateShaderResourceView(tex.com_resource.Get(), &srv_desc, cpu_descriptor_handle);
			tex.srv_descriptor_table_index = srv_heap.num_used_descriptors++;
		}
	}

	void load_texture(string asset_filename, uint32_t &tex_index) {
		OctarineImageHeader header = {};
		void *p_src_data = nullptr;

		OCTARINE_IMAGE result = octarine_image_read_from_file(asset_filename.c_str(), &header, &p_src_data);
		if(result != OCTARINE_IMAGE::OCTARINE_IMAGE_OK) { string msg = "File error: " + asset_filename; throw exception(msg.c_str()); };

		load_texture(header, asset_filename, p_src_data, tex_index);

		free(p_src_data);
	}

	void load_mesh(size_t vertex_count, size_t index_count, const void *p_vertex_data, const void *p_index_data, uint32_t &mesh_index) {
		auto& mesh = get_mesh_to_fill(mesh_index);

		auto vertex_size = sizeof(Vertex);
		auto vertex_buffer_size = vertex_count * vertex_size;
		auto index_buffer_size = index_count * sizeof(uint32_t);

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
		mesh.vbv.SizeInBytes = static_cast<UINT>(vertex_buffer_size);
		mesh.vbv.StrideInBytes = static_cast<UINT>(vertex_size);

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

			CHECK_D3D12_CALL(com_device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&mesh.com_index_buffer)), "");
			set_name(mesh.com_vertex_buffer, _STRINGIZE(__LINE__));
			heap_properties.Type = D3D12_HEAP_TYPE_UPLOAD;
			CHECK_D3D12_CALL(com_device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mesh.com_index_upload_buffer)), "");

			D3D12_RESOURCE_BARRIER resource_barrier = {};
			resource_barrier.Transition.pResource = mesh.com_index_buffer.Get();
			resource_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
			resource_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
			com_command_list->ResourceBarrier(1, &resource_barrier);

			void *p_data = nullptr;
			CHECK_D3D12_CALL(mesh.com_index_upload_buffer->Map(0, nullptr, &p_data), "");
			memcpy(p_data, p_index_data, index_buffer_size);
			mesh.com_index_upload_buffer->Unmap(0, nullptr);
			com_command_list->CopyBufferRegion(mesh.com_index_buffer.Get(), 0, mesh.com_index_upload_buffer.Get(), 0, index_buffer_size);

			resource_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
			resource_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
			com_command_list->ResourceBarrier(1, &resource_barrier);
		}

		mesh.ibv.BufferLocation = mesh.com_index_buffer->GetGPUVirtualAddress();
		mesh.ibv.Format = DXGI_FORMAT_R32_UINT;
		mesh.ibv.SizeInBytes = static_cast<UINT>(index_buffer_size);
	}

	void load_mesh(string asset_filename, uint32_t mesh_index) {
		{
			auto& mesh = get_mesh_to_fill(mesh_index);

			string asset_folder = "D:/Octarine/OctarineAssets/";
			std::ifstream file(asset_folder + asset_filename, std::ios::binary);
			if(!file.is_open()) {
				throw exception("Couldn't open a necessary file\n");
			}

			OctarineMeshHeader header{};
			void *p_data{ nullptr };

			auto result = octarine_mesh_read_from_file(asset_filename.c_str(), &header, &p_data);
			if(result != OCTARINE_MESH_OK) { string msg = "File error: " + asset_filename; throw exception(msg.c_str()); };

			auto vertex_size = sizeof(Vertex);
			auto vertex_buffer_size = mesh.header.vertex_count * vertex_size;
			auto index_buffer_size = mesh.header.index_count * sizeof(uint32_t);

			void *p_vertex_data = p_data;
			void *p_index_data = (void*)((uint8_t*)p_data + vertex_buffer_size);

			load_mesh(header.num_vertices, header.num_indices, p_vertex_data, p_index_data, mesh_index);
		}
	}

	void wait_for_gpu() {
		CHECK_D3D12_CALL(com_command_queue->Signal(com_fence.Get(), a_fence_values[frame_index]), "");
		CHECK_D3D12_CALL(com_fence->SetEventOnCompletion(a_fence_values[frame_index], h_fence_event), "");
		WaitForSingleObjectEx(h_fence_event, INFINITE, FALSE);
		a_fence_values[frame_index]++;
	}

	void prepare_next_frame() {
		uint64_t current_fence_value = a_fence_values[frame_index];
		CHECK_D3D12_CALL(com_command_queue->Signal(com_fence.Get(), current_fence_value), "");
		frame_index = com_swap_chain->GetCurrentBackBufferIndex();
		if(com_fence->GetCompletedValue() < a_fence_values[frame_index]) {
			CHECK_D3D12_CALL(com_fence->SetEventOnCompletion(a_fence_values[frame_index], h_fence_event), "");
			WaitForSingleObjectEx(h_fence_event, INFINITE, FALSE);
		}

		a_fence_values[frame_index] = current_fence_value + 1;
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
		a_root_params[0].Constants.Num32BitValues = 4;

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
		D3D12_DESCRIPTOR_RANGE1 a_srv_descriptor_ranges[3] = {};
		a_srv_descriptor_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		a_srv_descriptor_ranges[0].NumDescriptors = is_msaa_enabled ? 2 : 1;
		a_srv_descriptor_ranges[0].BaseShaderRegister = 0;
		a_srv_descriptor_ranges[0].RegisterSpace = 0;
		a_srv_descriptor_ranges[0].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
		a_srv_descriptor_ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		// brdf_lut, radiance, diffuse irradiance_ specular irradiance
		a_srv_descriptor_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		a_srv_descriptor_ranges[1].NumDescriptors = 1 + num_descriptor_per_environment;
		a_srv_descriptor_ranges[1].BaseShaderRegister = 0;
		a_srv_descriptor_ranges[1].RegisterSpace = 1;
		a_srv_descriptor_ranges[1].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
		a_srv_descriptor_ranges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		a_srv_descriptor_ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		a_srv_descriptor_ranges[2].NumDescriptors = UINT_MAX;
		a_srv_descriptor_ranges[2].BaseShaderRegister = 0;
		a_srv_descriptor_ranges[2].RegisterSpace = 2;
		a_srv_descriptor_ranges[2].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
		a_srv_descriptor_ranges[2].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

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
		static_sampler_1.Filter = D3D12_FILTER_ANISOTROPIC;
		static_sampler_1.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		static_sampler_1.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		static_sampler_1.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		static_sampler_1.MipLODBias = mip_lod_bias;
		static_sampler_1.MaxAnisotropy = max_anisotropy;
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
		CHECK_D3D12_CALL(com_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&com_root_signature)), "");
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
		compile_flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
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
			pso_desc.RTVFormats[0] = hdr_buffer.format;
			pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
			pso_desc.SampleDesc.Count = hdr_buffer.is_multi_sampled ? ms_count : 1;
			pso_desc.SampleDesc.Quality = hdr_buffer.is_multi_sampled ? ms_quality : 0;
			CHECK_D3D12_CALL(com_device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&com_scene_opaque_pso)), "");
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
			pso_desc.RTVFormats[0] = hdr_buffer.format;
			pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
			pso_desc.SampleDesc.Count = hdr_buffer.is_multi_sampled ? ms_count : 1;
			pso_desc.SampleDesc.Quality = hdr_buffer.is_multi_sampled ? ms_quality : 0;
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
			pso_desc.RTVFormats[0] = hdr_buffer.format;
			pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
			pso_desc.SampleDesc.Count = hdr_buffer.is_multi_sampled ? ms_count : 1;
			pso_desc.SampleDesc.Quality = hdr_buffer.is_multi_sampled ? ms_quality : 0;
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
			pso_desc.RTVFormats[0] = a_back_buffers[0].format;
			pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
			pso_desc.SampleDesc.Count = 1;
			pso_desc.SampleDesc.Quality = 0;
			CHECK_D3D12_CALL(com_device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&com_final_pso)), "");
		}
	}

	void create_render_buffer(RenderBuffer& render_buffer, DescriptorHeap *p_rtv_desc_heap, DescriptorHeap *p_srv_desc_heap, const string &debug_name, int32_t rtv_desc_heap_index =-1, int32_t stv_desc_heap_index = -1) {
		if(!render_buffer.com_resource) {
			D3D12_HEAP_PROPERTIES heap_properties = {};
			heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

			D3D12_CLEAR_VALUE clear_value = {};
			clear_value.Format = render_buffer.format;

			D3D12_RESOURCE_DESC resource_desc = {};
			resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			resource_desc.Width = render_buffer.width;
			resource_desc.Height = render_buffer.height;
			resource_desc.DepthOrArraySize = 1;
			resource_desc.MipLevels = 1;
			resource_desc.Format = render_buffer.format;
			resource_desc.SampleDesc.Count = render_buffer.is_multi_sampled ? ms_count : 1;
			resource_desc.SampleDesc.Quality = render_buffer.is_multi_sampled ? ms_quality : 0;
			resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
			resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
			CHECK_D3D12_CALL(com_device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_RENDER_TARGET, &clear_value, IID_PPV_ARGS(&(render_buffer.com_resource))), "");
			set_name(render_buffer.com_resource, debug_name);
		}

		D3D12_RENDER_TARGET_VIEW_DESC desc = {};
		desc.Format = render_buffer.format;
		desc.ViewDimension = is_msaa_enabled ? D3D12_RTV_DIMENSION_TEXTURE2DMS : D3D12_RTV_DIMENSION_TEXTURE2D;
		com_device->CreateRenderTargetView(render_buffer.com_resource.Get(), &desc, p_rtv_desc_heap->get_cpu_handle((rtv_desc_heap_index >= 0) ? rtv_desc_heap_index : p_rtv_desc_heap->num_used_descriptors));
		render_buffer.rtv_descriptor_table_index = (rtv_desc_heap_index >= 0) ? rtv_desc_heap_index : p_rtv_desc_heap->num_used_descriptors++;

		if(render_buffer.is_shader_visible) {
			D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
			srv_desc.Format = render_buffer.format;
			srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srv_desc.ViewDimension = render_buffer.is_multi_sampled ? D3D12_SRV_DIMENSION_TEXTURE2DMS : D3D12_SRV_DIMENSION_TEXTURE2D;
			srv_desc.Texture2D.MostDetailedMip = 0;
			srv_desc.Texture2D.MipLevels = -1;
			srv_desc.Texture2D.PlaneSlice = 0;
			srv_desc.Texture2D.ResourceMinLODClamp = 0;
			D3D12_CPU_DESCRIPTOR_HANDLE cpu_descriptor_handle = p_srv_desc_heap->get_cpu_handle((stv_desc_heap_index>= 0) ? stv_desc_heap_index : p_srv_desc_heap->num_used_descriptors);
			com_device->CreateShaderResourceView(render_buffer.com_resource.Get(), &srv_desc, cpu_descriptor_handle);
			render_buffer.srv_descriptor_table_index = (stv_desc_heap_index >= 0) ? stv_desc_heap_index : p_srv_desc_heap->num_used_descriptors++;
		}
	}

	void create_depth_buffer(RenderBuffer& depth_buffer, DescriptorHeap *p_dsv_desc_heap, const string &debug_name, int32_t dsv_desc_heap_index = -1) {

		D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
		dsv_desc.Format = depth_buffer.format;;
		dsv_desc.ViewDimension = depth_buffer.is_multi_sampled ? D3D12_DSV_DIMENSION_TEXTURE2DMS : D3D12_DSV_DIMENSION_TEXTURE2D;
		dsv_desc.Flags = D3D12_DSV_FLAG_NONE;

		D3D12_CLEAR_VALUE dsc_clear_value = {};
		dsc_clear_value.Format = depth_buffer.format;
		dsc_clear_value.DepthStencil.Depth = 1.0f;

		D3D12_HEAP_PROPERTIES heap_properties = {};
		heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
		D3D12_RESOURCE_DESC resource_desc = {};
		resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resource_desc.Width = depth_buffer.width;
		resource_desc.Height = depth_buffer.height;
		resource_desc.DepthOrArraySize = 1;
		resource_desc.MipLevels = 1;
		resource_desc.Format = depth_buffer.format;
		resource_desc.SampleDesc.Count = depth_buffer.is_multi_sampled ? ms_count : 1;
		resource_desc.SampleDesc.Quality = depth_buffer.is_multi_sampled ? ms_quality : 0;
		resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		CHECK_D3D12_CALL(com_device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &dsc_clear_value, IID_PPV_ARGS(&depth_buffer.com_resource)), "");
		D3D12_CPU_DESCRIPTOR_HANDLE cpu_descriptor_handle = p_dsv_desc_heap->get_cpu_handle((dsv_desc_heap_index >= 0) ? dsv_desc_heap_index : p_dsv_desc_heap->num_used_descriptors++);
		com_device->CreateDepthStencilView(depth_buffer.com_resource.Get(), &dsv_desc, cpu_descriptor_handle);
	}

	void init(HWND h_window) {
		ComPtr<IDXGIFactory5> com_dxgi_factory{ nullptr };
		vector<ComPtr<IDXGIAdapter1>> v_com_dxgi_adapters{};
		v_com_dxgi_adapters.reserve(8);

		UINT factory_flags = 0;
#if(_DEBUG)
		ComPtr<ID3D12Debug> com_debug_controller = nullptr;
		if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&com_debug_controller)))) {
			com_debug_controller->EnableDebugLayer();
			factory_flags = DXGI_CREATE_FACTORY_DEBUG;
		}
#endif

		HRESULT h_result = CreateDXGIFactory2(factory_flags, IID_PPV_ARGS(&com_dxgi_factory));
		if(FAILED(h_result)) { throw exception("DXGI Factory creation failed!"); }

		UINT adapter_index = 0;
		for(;;) {
			ComPtr<IDXGIAdapter1> com_curr_adapter = nullptr;
			h_result = com_dxgi_factory->EnumAdapters1(adapter_index, com_curr_adapter.GetAddressOf());
			if(FAILED(h_result)) {
				if((h_result == DXGI_ERROR_NOT_FOUND && adapter_index == 0) || h_result != DXGI_ERROR_NOT_FOUND) {
					throw exception("Could not find an adapter!");
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

			for(uint8_t allocator_index = 0; allocator_index < a_com_command_allocators.size(); ++allocator_index) {
				CHECK_D3D12_CALL(com_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&(a_com_command_allocators[allocator_index]))), "");
			}

			CHECK_D3D12_CALL(com_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE::D3D12_COMMAND_LIST_TYPE_DIRECT, a_com_command_allocators[0].Get(), nullptr, IID_PPV_ARGS(&com_command_list)), "");

			CHECK_D3D12_CALL(com_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&com_fence)), "");
			a_fence_values[frame_index]++;
			CHECK_WIN32_CALL(CreateEvent(nullptr, FALSE, FALSE, nullptr));
		}

		{ // Create the swap chain and get backbuffers
			ComPtr<IDXGISwapChain1> com_temp_swap_chain = nullptr;
			DXGI_SWAP_CHAIN_DESC1 swap_chain_desc = {};
			swap_chain_desc.Width = back_buffer_width;
			swap_chain_desc.Height = back_buffer_height;
			swap_chain_desc.Format = back_buffer_format;
			swap_chain_desc.SampleDesc.Count = 1;
			swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			swap_chain_desc.BufferCount = max_inflight_frame_count;
			swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

			CHECK_DXGI_CALL(com_dxgi_factory->CreateSwapChainForHwnd(com_command_queue.Get(), h_window, &swap_chain_desc, NULL, NULL, com_temp_swap_chain.GetAddressOf()));
			CHECK_DXGI_CALL(com_temp_swap_chain.As(&com_swap_chain));
			frame_index = com_swap_chain->GetCurrentBackBufferIndex();

			for(int buffer_index = 0; buffer_index < max_inflight_frame_count; ++buffer_index) {
				CHECK_DXGI_CALL(com_swap_chain->GetBuffer(buffer_index, IID_PPV_ARGS(&(a_back_buffers[buffer_index].com_resource))));
				D3D12_RESOURCE_DESC desc = a_back_buffers[buffer_index].com_resource->GetDesc();
				a_back_buffers[buffer_index].format = desc.Format;
				a_back_buffers[buffer_index].width = static_cast<uint16_t>(desc.Width);
				a_back_buffers[buffer_index].height = static_cast<uint16_t>(desc.Height);
				a_back_buffers[buffer_index].is_multi_sampled = (desc.SampleDesc.Count > 1);
			}
		}

		{ // Initialize descriptor heaps
			rtv_desc_heap.init();
			dsv_desc_heap.init();
			source_srv_desc_heap.init();
			target_srv_desc_heap.init();
		}

		{ // Initialize render buffers
			create_render_buffer(a_back_buffers[0], &rtv_desc_heap, nullptr, "back_buffer_0");
			create_render_buffer(a_back_buffers[1], &rtv_desc_heap, nullptr, "back_buffer_1");
			create_render_buffer(a_back_buffers[2], &rtv_desc_heap, nullptr, "back_buffer_2");
			if constexpr(is_msaa_enabled) {
				create_render_buffer(hdr_buffer_resolved, &rtv_desc_heap, &source_srv_desc_heap, "hdr_buffer_resolved");
			}
			create_render_buffer(hdr_buffer, &rtv_desc_heap, &source_srv_desc_heap, "hdr_buffer");
		}
	
		{ // Create the depth-stencil view
			create_depth_buffer(depth_buffer, &dsv_desc_heap, "depth_buffer");
		}
	
		{ // Create constant buffers
			per_frame_cb.init();
			transformations_cb.init();
			material_list_cb.init();
		}

		create_root_signature();
		create_pipeline_state_objects();
	}

	void update(const GuiData& gui_data, uint32_t start_index_into_textures, uint32_t num_used_textures, const Camera& camera) {
		current_env_index = gui_data.ibl_environment_index;
		current_background_index = gui_data.background_env_map_type;
		current_specular_mip_level = gui_data.background_specular_irradiance_mip_level;
		current_isolation_mode_index = gui_data.isolation_mode_index;
		test = gui_data.test;
		auto num_used_descs = num_used_textures;
		auto start_index_to_desc_heap = a_textures[start_index_into_textures].srv_descriptor_table_index;

		{ // Update constant buffers
			{
				per_frame_cb.constants.clip_from_view = camera.clip_from_view;
				per_frame_cb.constants.view_from_clip = camera.view_from_clip;
				per_frame_cb.constants.view_from_world = camera.view_from_world;
				per_frame_cb.constants.world_from_view = camera.world_from_view;
				per_frame_cb.constants.cam_pos_ws = camera.pos_ws;
				per_frame_cb.update();
			}

			{
				auto& transformation_list = scene_manager::get_transformation_list();
				auto num_transformations = transformation_list.size();
				memcpy(&transformations_cb.constants, transformation_list.data(), sizeof(XMFLOAT4X4) * num_transformations);
				transformations_cb.update();
			}

			{
				auto& material_list = scene_manager::get_material_list();
				auto num_materials = material_list.size();
				memcpy(&material_list_cb.constants, material_list.data(), sizeof(Material) * num_materials);
				material_list_cb.update();
			}
		}
		{ // Update srv descriptor table
			
			uint32_t num_source_static_descs = (is_msaa_enabled ? 2 : 1) + 1; // render_buffer srvs +  brdf_lut srv;
			uint32_t target_heap_start_index = frame_index * max_descriptor_count_per_frame + 1;

			// Copy Environment Map Descriptors
			com_device->CopyDescriptorsSimple(
				num_source_static_descs,
				target_srv_desc_heap.get_cpu_handle(target_heap_start_index),
				source_srv_desc_heap.get_cpu_handle(0),
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
			);

			// Copy Environment Map Descriptors
			com_device->CopyDescriptorsSimple(
				num_descriptor_per_environment,
				target_srv_desc_heap.get_cpu_handle(target_heap_start_index + num_source_static_descs),
				source_srv_desc_heap.get_cpu_handle(num_source_static_descs + current_env_index * num_descriptor_per_environment),
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
			);

			// Copy Scene Texture Descriptors
			com_device->CopyDescriptorsSimple(
				num_used_descs,
				target_srv_desc_heap.get_cpu_handle(target_heap_start_index + num_source_static_descs + num_descriptor_per_environment),
				source_srv_desc_heap.get_cpu_handle(start_index_to_desc_heap),
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
			);
		}
	}

	void begin_render() {
		CHECK_D3D12_CALL(a_com_command_allocators[frame_index]->Reset(), "");
		CHECK_D3D12_CALL(com_command_list->Reset(a_com_command_allocators[frame_index].Get(), com_background_pso.Get()), "");

		D3D12_RESOURCE_BARRIER resource_barrier = {};
		resource_barrier.Transition.pResource = a_back_buffers[frame_index].com_resource.Get();
		resource_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		resource_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		com_command_list->ResourceBarrier(1, &resource_barrier);

		if constexpr(!is_msaa_enabled) {
			D3D12_RESOURCE_BARRIER resource_barrier = {};
			resource_barrier.Transition.pResource = hdr_buffer.com_resource.Get();
			resource_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			resource_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
			com_command_list->ResourceBarrier(1, &resource_barrier);
		}
	}

	void draw(const vector<DrawInfo>& draw_list) {
		for(auto& draw_info : draw_list) {
			auto& mesh = a_meshes[draw_info.mesh_index];
			com_command_list->IASetIndexBuffer(&mesh.ibv);
			com_command_list->IASetVertexBuffers(0, 1, &mesh.vbv);
			uint32_t a_root_constants[] = { draw_info.transformation_index, draw_info.material_index, current_isolation_mode_index, *reinterpret_cast<uint32_t*>(&test) };
			com_command_list->SetGraphicsRoot32BitConstants(0, count_of(a_root_constants), a_root_constants, 0);
			com_command_list->DrawIndexedInstanced(draw_info.draw_index_count, 1, draw_info.draw_first_index, 0, 0);
		}
	}

	void render() {
		float clear_color[] = { 0.f, 0.f, 0.f, 0.f };
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

		// Draw Scene
		ID3D12DescriptorHeap *a_heaps[] = { target_srv_desc_heap.com_heap.Get() };
		com_command_list->SetDescriptorHeaps(count_of(a_heaps), a_heaps);
		com_command_list->SetGraphicsRootConstantBufferView(1, per_frame_cb.get_gpu_address());
		com_command_list->SetGraphicsRootConstantBufferView(2, transformations_cb.get_gpu_address());
		com_command_list->SetGraphicsRootConstantBufferView(3, material_list_cb.get_gpu_address());
		com_command_list->SetGraphicsRootDescriptorTable(4, target_srv_desc_heap.get_gpu_handle(1 + max_descriptor_count_per_frame * frame_index));

		D3D12_CPU_DESCRIPTOR_HANDLE rtv_cpu_handle(rtv_desc_heap.get_cpu_handle(hdr_buffer.rtv_descriptor_table_index));
		D3D12_CPU_DESCRIPTOR_HANDLE dsv_cpu_handle(dsv_desc_heap.get_cpu_handle(depth_buffer.rtv_descriptor_table_index));
		com_command_list->OMSetRenderTargets(1, &rtv_cpu_handle, FALSE, &dsv_cpu_handle);
		com_command_list->ClearRenderTargetView(rtv_cpu_handle, clear_color, 0, nullptr);
		com_command_list->ClearDepthStencilView(dsv_cpu_handle, D3D12_CLEAR_FLAG_DEPTH, 1.0, 0, 0, NULL);

		// Draw Background
		com_command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		uint32_t a_root_constants[] = { current_background_index, current_specular_mip_level };
		com_command_list->SetGraphicsRoot32BitConstants(0, count_of(a_root_constants), a_root_constants, 0);
		com_command_list->DrawInstanced(4, 1, 0, 0);

		// Draw Opaque objects
		com_command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		com_command_list->SetPipelineState(com_scene_opaque_pso.Get());
		draw(scene_manager::get_opaque_draw_list());

		// Draw Alpha Blended objects
		if(current_isolation_mode_index == 0) com_command_list->SetPipelineState(com_scene_alpha_blend_pso.Get());
		draw(scene_manager::get_alpha_blend_draw_list());

		if constexpr(is_msaa_enabled) {
			D3D12_RESOURCE_BARRIER a_resource_barriers[2] = {};
			a_resource_barriers[0].Transition.pResource = hdr_buffer.com_resource.Get();
			a_resource_barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
			a_resource_barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
			a_resource_barriers[1].Transition.pResource = hdr_buffer_resolved.com_resource.Get();
			a_resource_barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			a_resource_barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_RESOLVE_DEST;
			com_command_list->ResourceBarrier(count_of(a_resource_barriers), a_resource_barriers);
			com_command_list->ResolveSubresource(hdr_buffer_resolved.com_resource.Get(), 0, hdr_buffer.com_resource.Get(), 0, hdr_buffer.format);
			a_resource_barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
			a_resource_barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
			a_resource_barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RESOLVE_DEST;
			a_resource_barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			com_command_list->ResourceBarrier(count_of(a_resource_barriers), a_resource_barriers);
		}

		// Copy
		if constexpr(!is_msaa_enabled) {
			D3D12_RESOURCE_BARRIER resource_barrier = {};
			resource_barrier.Transition.pResource = hdr_buffer.com_resource.Get();
			resource_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
			resource_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			com_command_list->ResourceBarrier(1, &resource_barrier);
		}

		com_command_list->SetPipelineState(com_final_pso.Get());
		rtv_cpu_handle = rtv_desc_heap.get_cpu_handle(a_back_buffers[frame_index].rtv_descriptor_table_index);
		com_command_list->OMSetRenderTargets(1, &rtv_cpu_handle, FALSE, nullptr);
		com_command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		com_command_list->DrawInstanced(4, 1, 0, 0);

		// Prepare the command list for imgui commands
		com_command_list->OMSetRenderTargets(1, &rtv_cpu_handle, FALSE, nullptr);
		com_command_list->SetDescriptorHeaps(count_of(a_heaps), a_heaps);
	}

	void end_render() {
		D3D12_RESOURCE_BARRIER resource_barrier = {};
		resource_barrier.Transition.pResource = a_back_buffers[frame_index].com_resource.Get();
		resource_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		resource_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
		com_command_list->ResourceBarrier(1, &resource_barrier);

		CHECK_D3D12_CALL(com_command_list->Close(), "");

		ID3D12CommandList *a_command_lists[] = { com_command_list.Get() };
		com_command_queue->ExecuteCommandLists(count_of(a_command_lists), a_command_lists);
	}

	void execute_initial_commands() {
		CHECK_D3D12_CALL(com_command_list->Close(), "");
		ID3D12CommandList* pp_command_lists[] = { com_command_list.Get() };
		com_command_queue->ExecuteCommandLists(count_of(pp_command_lists), pp_command_lists);
		wait_for_gpu();

		// Release upload buffers
		for(auto& tex : a_textures) {
			tex.com_upload.Reset();
		}
		for(auto& mesh : a_meshes) {
			mesh.com_vertex_upload_buffer.Reset();
			mesh.com_index_upload_buffer.Reset();
		}
	}

	void present() {
		CHECK_DXGI_CALL(com_swap_chain->Present(1, 0));
	}

	bool resize(LPARAM lparam) {
		if(com_swap_chain) {
			back_buffer_width = (UINT)LOWORD(lparam);
			back_buffer_height = (UINT)HIWORD(lparam);
			HRESULT h_result;

			wait_for_gpu();

			for(int buffer_index = 0; buffer_index < max_inflight_frame_count; ++buffer_index) {
				a_back_buffers[buffer_index].com_resource.Reset();
				a_fence_values[buffer_index] = a_fence_values[frame_index];
			}

			DXGI_SWAP_CHAIN_DESC desc = {};
			com_swap_chain->GetDesc(&desc);
			CHECK_DXGI_CALL(com_swap_chain->ResizeBuffers(max_inflight_frame_count, back_buffer_width, back_buffer_height, desc.BufferDesc.Format, desc.Flags));
			frame_index = com_swap_chain->GetCurrentBackBufferIndex();

			for(int buffer_index = 0; buffer_index < max_inflight_frame_count; ++buffer_index) {
				CHECK_DXGI_CALL(com_swap_chain->GetBuffer(buffer_index, IID_PPV_ARGS(&(a_back_buffers[buffer_index].com_resource))));
				D3D12_RESOURCE_DESC desc = a_back_buffers[buffer_index].com_resource->GetDesc();
				a_back_buffers[buffer_index].format = desc.Format;
				a_back_buffers[buffer_index].width = static_cast<uint16_t>(desc.Width);
				a_back_buffers[buffer_index].height = static_cast<uint16_t>(desc.Height);
				a_back_buffers[buffer_index].is_multi_sampled = (desc.SampleDesc.Count > 1);
			}

			create_render_buffer(a_back_buffers[0], &rtv_desc_heap, nullptr, "back_buffer_0", 0);
			create_render_buffer(a_back_buffers[1], &rtv_desc_heap, nullptr, "back_buffer_1", 1);
			create_render_buffer(a_back_buffers[2], &rtv_desc_heap, nullptr, "back_buffer_2", 2);

			if constexpr(is_msaa_enabled) {
				hdr_buffer_resolved.com_resource.Reset();
				hdr_buffer_resolved.width = back_buffer_width;
				hdr_buffer_resolved.height = back_buffer_height;
				create_render_buffer(hdr_buffer_resolved, &rtv_desc_heap, &source_srv_desc_heap, "hdr_buffer_resolved", 3, 0);
			}
			hdr_buffer.com_resource.Reset();
			hdr_buffer.width = back_buffer_width;
			hdr_buffer.height = back_buffer_height;
			create_render_buffer(hdr_buffer, &rtv_desc_heap, &source_srv_desc_heap, "hdr_buffer", is_msaa_enabled ? 4 : 3, 1);
			
			depth_buffer.com_resource.Reset();
			depth_buffer.width = back_buffer_width;
			depth_buffer.height = back_buffer_height;
			create_depth_buffer(depth_buffer, &dsv_desc_heap, "depth_buffer", 0);

			return true;
		}
		return false;
	}

} // namespace renderer