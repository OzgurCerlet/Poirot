namespace scene_manager
{
	struct BoundingBox {
		XMFLOAT3 min;
		XMFLOAT3 max;
	};

	struct Primitive {
		BoundingBox bbox;
		uint32_t first_index;
		uint32_t index_count;
		uint32_t material_index;
	};

	struct Node {
		Node *p_parent;
		vector<Node*> children;
		vector<Primitive> primitives;

		XMMATRIX xm_transform{};
		XMVECTOR xm_rotation{};
		XMVECTOR xm_translation{};
		XMVECTOR xm_scale{ 1.0f, 1.0f, 1.0f, 1.0f };

		BoundingBox bbox;

		int32_t mesh_index{ -1 };
		uint32_t index;
		uint32_t transformation_index;

		inline XMMATRIX get_local_transform();
		inline XMMATRIX get_final_transform();
		inline void compute_bounding_box();
		void update(vector<XMFLOAT4X4> &node_transformation_list, const XMMATRIX& xm_global_transform);
	};

	inline void Node::compute_bounding_box() {
		if(primitives.size() > 0) {
			auto xm_transform = get_final_transform();
			for(auto& primitive : primitives) {
				XMVECTOR xm_min{ primitive.bbox.min.x, primitive.bbox.min.y, primitive.bbox.min.z, 1.f };
				xm_min = XMVector4Transform(xm_min, xm_transform);
				XMFLOAT3 min;
				XMStoreFloat3(&min, xm_min);

				XMVECTOR xm_max{ primitive.bbox.max.x, primitive.bbox.max.y, primitive.bbox.max.z, 1.f };
				xm_max = XMVector4Transform(xm_max, xm_transform);
				XMFLOAT3 max;
				XMStoreFloat3(&max, xm_max);

				if(min.x < bbox.min.x) { bbox.min.x = min.x; }
				if(min.y < bbox.min.y) { bbox.min.y = min.y; }
				if(min.z < bbox.min.z) { bbox.min.z = min.z; }
				if(max.x > bbox.max.x) { bbox.max.x = max.x; }
				if(max.y > bbox.max.y) { bbox.max.y = max.y; }
				if(max.z > bbox.max.z) { bbox.max.z = max.z; }
			}
		}
	}

	inline XMMATRIX Node::get_local_transform() {
		return XMMatrixTranspose(XMMatrixTranslationFromVector(xm_translation)) * XMMatrixTranspose(XMMatrixRotationQuaternion(xm_rotation)) * XMMatrixScalingFromVector(xm_scale) * xm_transform;
	}

	inline XMMATRIX Node::get_final_transform() {
		XMMATRIX transform = get_local_transform();
		Node *p = p_parent;
		while(p) {
			transform = p->get_local_transform() * transform;
			p = p->p_parent;
		}
		return transform;
	}

	void Node::update(vector<XMFLOAT4X4> &node_transformation_list, const XMMATRIX& xm_global_transform) {
		if(mesh_index >= 0) {
			transformation_index = static_cast<uint32_t>(node_transformation_list.size());
			XMMATRIX xm_final_transformation = xm_global_transform * get_final_transform();
			XMFLOAT4X4 final_transformation;
			XMStoreFloat4x4(&final_transformation, xm_final_transformation);
			node_transformation_list.push_back(final_transformation);
		}

		for(auto& child : children) {
			child->update(node_transformation_list, xm_global_transform);
		}
	}

	struct Scene
	{
		XMMATRIX global_transform;
		vector<DrawInfo> opaque_draw_info_list;
		vector<DrawInfo> alpha_blend_draw_info_list;
		vector<Material> materials;
		vector<XMFLOAT4X4> node_transformations;
		vector<Node*> nodes;
		vector<Node*> linear_nodes;
		BoundingBox bbox;
		uint32_t start_index_into_textures;
		uint32_t num_used_textures;

		Scene() {
			global_transform = XMMatrixIdentity();
			start_index_into_textures = 0;
			num_used_textures = 0;
			bbox.min.x = bbox.min.y = bbox.min.z = FLT_MAX;
			bbox.max.x = bbox.max.y = bbox.max.z = -FLT_MAX;
		};

		inline void compute_bounding_box();
	};

	inline void Scene::compute_bounding_box() {
		if(linear_nodes.size() > 0) {
			for(auto p_node : linear_nodes) {
				auto min = p_node->bbox.min;
				auto max = p_node->bbox.max;

				if(min.x < bbox.min.x) { bbox.min.x = min.x; }
				if(min.y < bbox.min.y) { bbox.min.y = min.y; }
				if(min.z < bbox.min.z) { bbox.min.z = min.z; }
				if(max.x > bbox.max.x) { bbox.max.x = max.x; }
				if(max.y > bbox.max.y) { bbox.max.y = max.y; }
				if(max.z > bbox.max.z) { bbox.max.z = max.z; }
			}
		}
	}
	
	vector<unique_ptr<Scene>> scenes{};
	Camera camera;
	uint32_t current_scene_index = 0;
	
	void load_texture(tinygltf::Image &image, bool is_srgb, uint32_t &tex_index) {
		size_t image_size = 0;
		uint8_t *p_image_data = nullptr;
		{
			if(image.component == 3) {
				image_size = image.width * image.height * 4;
				p_image_data = new uint8_t[image_size];
				uint8_t* p_rgba = p_image_data;
				uint8_t* p_rgb = &image.image[0];
				for(size_t i = 0; i < image.width * image.height; ++i) {
					p_rgba[0] = p_rgb[0];
					p_rgba[1] = p_rgb[1];
					p_rgba[2] = p_rgb[2];
					p_rgba[3] = 255;
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
		uint8_t *p_image_with_mips_data = p_image_data;
		if(is_mipchain_generation_enabled)
		{
			auto is_power_of_2 = [](int value, int &power) {
				if((value && !(value & (value - 1)))) {
					static const unsigned int b[] = { 0xAAAAAAAA, 0xCCCCCCCC, 0xF0F0F0F0,0xFF00FF00, 0xFFFF0000 };
					unsigned int r = (value & b[0]) != 0;
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
				unsigned char *p_output = reinterpret_cast<unsigned char *>(malloc(size*(size >> 1) * 3 * pixel_size));
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

		renderer::load_texture(header, image.name, p_image_with_mips_data, tex_index);
		if(is_mipchain_generation_enabled) {
			free(p_image_with_mips_data);
		}

		if(image.component == 3) delete[](p_image_data);
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

		uint32_t image_count{ 0 };
		uint32_t last_tex_index{ 0 };
		for(auto &image : gltf_model.images) {
			load_texture(image, is_srgb(image_count++), last_tex_index);
		}
		assert(last_tex_index >= image_count);
		scene.start_index_into_textures = last_tex_index - image_count + 1;
		scene.num_used_textures = image_count;
	}

	void load_materials(const tinygltf::Model &gltf_model, Scene &scene) {

		for(auto &mat : gltf_model.materials) {
			Material material;

			if(auto it = mat.values.find("baseColorTexture"); it != mat.values.end()) {
				material.base_color_texture_index = it->second.TextureIndex();
			}

			if(auto it = mat.additionalValues.find("normalTexture"); it != mat.additionalValues.end()) {
				material.normal_texture_index = it->second.TextureIndex();
			}

			if(auto it = mat.additionalValues.find("occlusionTexture"); it != mat.additionalValues.end()) {
				material.occlusion_texture_index = it->second.TextureIndex();
			}

			if(auto it = mat.values.find("metallicRoughnessTexture"); it != mat.values.end()) {
				material.metallic_roughness_texture_index = it->second.TextureIndex();
			}

			if(auto it = mat.additionalValues.find("emissiveTexture"); it != mat.additionalValues.end()) {
				material.emissive_texture_index = it->second.TextureIndex();
			}

			if(auto it = mat.values.find("roughnessFactor"); it != mat.values.end()) {
				material.roughness_factor = static_cast<float>(it->second.Factor());
			}

			if(auto it = mat.values.find("metallicFactor"); it != mat.values.end()) {
				material.metallic_factor = static_cast<float>(it->second.Factor());
			}

			if(auto it = mat.values.find("baseColorFactor"); it != mat.values.end()) {
				auto color = it->second.ColorFactor();
				material.basecolor_factor = XMFLOAT4(static_cast<float>(color[0]), static_cast<float>(color[1]), static_cast<float>(color[2]), static_cast<float>(color[3]));
			}

			if(auto it = mat.additionalValues.find("alphaMode"); it != mat.additionalValues.end()) {
				if(it->second.string_value == "BLEND") {
					material.alphaMode = Material::ALPHAMODE_BLEND;
				}
				if(it->second.string_value == "MASK") {
					material.alphaMode = Material::ALPHAMODE_MASK;
				}
			}

			if(auto it = mat.additionalValues.find("alphaCutoff"); it != mat.additionalValues.end()) {
				material.alpha_cutoff = static_cast<float>(it->second.Factor());
			}

			scene.materials.push_back(material);
		}
	}

	void load_node(Node *p_parent, const tinygltf::Node &node, uint32_t node_index, const tinygltf::Model &model, vector<uint32_t>& index_buffer, vector<Vertex>& vertex_buffer, Scene& scene) {
		Node *p_node = new Node{};
		p_node->index = node_index;
		p_node->p_parent = p_parent;

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

		if(node.matrix.size() == 16) { // gltf matrices are stored in column-major order
			p_node->xm_transform = XMMatrixTranspose(XMMatrixSet(
				static_cast<float>(node.matrix[0]),	static_cast<float>(node.matrix[1]),  static_cast<float>(node.matrix[2]),  static_cast<float>(node.matrix[3]),
				static_cast<float>(node.matrix[4]),	static_cast<float>(node.matrix[5]),  static_cast<float>(node.matrix[6]),  static_cast<float>(node.matrix[7]),
				static_cast<float>(node.matrix[8]),	static_cast<float>(node.matrix[9]),  static_cast<float>(node.matrix[10]), static_cast<float>(node.matrix[11]),
				static_cast<float>(node.matrix[12]),static_cast<float>(node.matrix[13]), static_cast<float>(node.matrix[14]), static_cast<float>(node.matrix[15])
			));
		};

		// Node with children
		if(node.children.size() > 0) {
			for(auto i = 0; i < node.children.size(); i++) {
				load_node(p_node, model.nodes[node.children[i]], node.children[i], model, index_buffer, vertex_buffer, scene);
			}
		}

		// Node contains mesh data
		if(node.mesh > -1) {
			const auto gltf_mesh = model.meshes[node.mesh];
			BoundingBox bbox;
			for(size_t i = 0; i < gltf_mesh.primitives.size(); i++) {
				const auto &gltf_primitive = gltf_mesh.primitives[i];
				if(gltf_primitive.indices < 0) {
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
					auto it = gltf_primitive.attributes.find("POSITION");
					assert(it != gltf_primitive.attributes.end());

					const tinygltf::Accessor &posAccessor = model.accessors[it->second];
					const tinygltf::BufferView &posView = model.bufferViews[posAccessor.bufferView];
					p_pos_buffer = reinterpret_cast<const float *>(&(model.buffers[posView.buffer].data[posAccessor.byteOffset + posView.byteOffset]));

					bbox.min = { static_cast<float>(posAccessor.minValues[0]), static_cast<float>(posAccessor.minValues[1]), static_cast<float>(posAccessor.minValues[2]) };
					bbox.max = { static_cast<float>(posAccessor.maxValues[0]), static_cast<float>(posAccessor.maxValues[1]), static_cast<float>(posAccessor.maxValues[2]) };

					it = gltf_primitive.attributes.find("NORMAL");
					if(it != gltf_primitive.attributes.end()) {
						const tinygltf::Accessor &normAccessor = model.accessors[it->second];
						const tinygltf::BufferView &normView = model.bufferViews[normAccessor.bufferView];
						p_nor_buffer = reinterpret_cast<const float *>(&(model.buffers[normView.buffer].data[normAccessor.byteOffset + normView.byteOffset]));
					}

					it = gltf_primitive.attributes.find("TEXCOORD_0");
					if(it != gltf_primitive.attributes.end()) {
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
					const tinygltf::Accessor &accessor = model.accessors[gltf_primitive.indices];
					const tinygltf::BufferView &bufferView = model.bufferViews[accessor.bufferView];
					const tinygltf::Buffer &buffer = model.buffers[bufferView.buffer];

					index_count = static_cast<uint32_t>(accessor.count);

					switch(accessor.componentType) {
						case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT:
						{
							uint32_t *buf = new uint32_t[accessor.count];
							memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint32_t));
							for(size_t index = 0; index < accessor.count; index++) {
								index_buffer.push_back(buf[index] + vertex_buffer_start);
							}
							delete[] buf;
							break;
						}
						case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT:
						{
							uint16_t *buf = new uint16_t[accessor.count];
							memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint16_t));
							for(size_t index = 0; index < accessor.count; index++) {
								index_buffer.push_back(buf[index] + vertex_buffer_start);
							}
							delete[] buf;
							break;
						}
						case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE:
						{
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
				Primitive primitive;
				primitive.material_index = gltf_primitive.material;
				primitive.first_index = index_buffer_start;
				primitive.index_count = index_count;
				primitive.bbox = bbox;
				p_node->primitives.push_back(primitive);
			}

			uint32_t mesh_index = 0;
			renderer::load_mesh(vertex_buffer.size(), index_buffer.size(), static_cast<const void*>(vertex_buffer.data()), static_cast<const void*>(index_buffer.data()), mesh_index);
			p_node->mesh_index = mesh_index;
		}

		p_node->compute_bounding_box();

		if(p_node->p_parent) {
			p_node->p_parent->children.push_back(p_node);
		}
		else {
			scene.nodes.push_back(p_node);
		}
		scene.linear_nodes.push_back(p_node);
	}

	void load_scene(const string& asset_filename, Scene &scene, bool flip_forward = false) {
		tinygltf::Model gltf_model;
		tinygltf::TinyGLTF gltf_ctx;
		string err;

		const string asset_file_address{ asset_folder + asset_filename };

		bool is_loaded = gltf_ctx.LoadASCIIFromFile(&gltf_model, &err, asset_file_address.c_str());
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

		scene.compute_bounding_box();
		XMVECTOR xm_center = (XMLoadFloat3(&scene.bbox.min) + XMLoadFloat3(&scene.bbox.max)) / 2.0;
		XMVECTOR xm_length = XMVector4Length(XMLoadFloat3(&scene.bbox.min) - XMLoadFloat3(&scene.bbox.max));
		float scale = 2.f / XMVectorGetX(xm_length);
		XMMATRIX xm_change_of_basis = XMMatrixSet(
		1.f, 0.f, 0.f, 0.f,
		0.f, 0.f, flip_forward ? 1.f : -1.f, 0.f,
		0.f, 1.f, 0.f, 0.f,
		0.f, 0.f, 0.f, 1.f
		);
		
		scene.global_transform = xm_change_of_basis * XMMatrixScaling(scale, scale, scale) * XMMatrixTranspose(XMMatrixTranslationFromVector(xm_center));

		for(auto node : scene.linear_nodes) {
			if(node->mesh_index >= 0) {
				node->update(scene.node_transformations, scene.global_transform);
			}
		}
	}

	void init_camera() {
		camera.aspect_ratio = (float)back_buffer_width / back_buffer_height;
		camera.vertical_fov_in_degrees = 45.0f;
		camera.near_plane_in_meters = 0.1f;
		camera.far_plane_in_meters = 1024.0f;

		camera.pos_ws = { 1.5f, -2.0f, 0.5f };
		camera.yaw_rad = XMConvertToRadians(50.0);
		camera.pitch_rad = XMConvertToRadians(20.0);
		//camera.dir_ws = { -1.0f, -1.0f, 0.0f };
		//camera.up_ws = { 0.0f, 0.0f, 1.0f };

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
		}

		auto xm_world_from_view = XMLoadFloat4x4(&camera.world_from_view);
		auto xm_view_from_world = XMMatrixInverse(nullptr, xm_world_from_view);
		XMStoreFloat4x4(&camera.view_from_world, xm_view_from_world);
		XMStoreFloat4x4(&camera.world_from_view, xm_world_from_view);
	}

	void prepare_draw_lists() {
		for(auto& p_scene : scenes) {
			for(auto p_node : p_scene->linear_nodes) {
				if(p_node->mesh_index >= 0) {
					for(auto& primitive : p_node->primitives) {

						DrawInfo draw_info{};
						draw_info.mesh_index = p_node->mesh_index;
						draw_info.transformation_index = p_node->transformation_index;
						draw_info.material_index = primitive.material_index;
						draw_info.draw_index_count = primitive.index_count;
						draw_info.draw_first_index = primitive.first_index;

						auto& material = p_scene->materials[primitive.material_index];
						switch(material.alphaMode) {
							case Material::ALPHAMODE_OPAQUE:
							case Material::ALPHAMODE_MASK:
							{
								p_scene->opaque_draw_info_list.push_back(draw_info);
							} break;
							case Material::ALPHAMODE_BLEND:
							{
								p_scene->alpha_blend_draw_info_list.push_back(draw_info);
							} break;
							default: break;
						}
					}
				}
			}
		}
	}

	void init() {
		{ // Load environment map sets
			uint32_t tex_index;
			renderer::load_texture("brdf_lut.octrn", tex_index);
			renderer::load_texture("courtyard_night_cube_radiance.octrn", tex_index);
			renderer::load_texture("courtyard_night_cube_irradiance.octrn", tex_index);
			renderer::load_texture("courtyard_night_cube_specular.octrn", tex_index);
			renderer::load_texture("ninomaru_teien_8k_cube_radiance.octrn", tex_index);
			renderer::load_texture("ninomaru_teien_8k_cube_irradiance.octrn", tex_index);
			renderer::load_texture("ninomaru_teien_8k_cube_specular.octrn", tex_index);
			renderer::load_texture("paul_lobe_haus_8k_cube_radiance.octrn", tex_index);
			renderer::load_texture("paul_lobe_haus_8k_cube_irradiance.octrn", tex_index);
			renderer::load_texture("paul_lobe_haus_8k_cube_specular.octrn", tex_index);
		}

		{ // Load sample scenes
			unique_ptr<Scene> p_scene = nullptr;

			p_scene = make_unique<Scene>();
			load_scene("cvc_helmet/scene.gltf", *p_scene, true);
			scenes.push_back(move(p_scene));

			p_scene = make_unique<Scene>();
			load_scene("damaged_helmet/damagedHelmet.gltf", *p_scene, true);
			scenes.push_back(move(p_scene));

			p_scene = make_unique<Scene>();
			load_scene("pony_cartoon/scene.gltf", *p_scene);
			scenes.push_back(move(p_scene));

			p_scene = make_unique<Scene>();
			load_scene("vintage_suitcase/scene.gltf", *p_scene);
			scenes.push_back(move(p_scene));
		}

		init_camera();
		prepare_draw_lists();
	}

	void update(GuiData& gui_data) {
		current_scene_index = (gui_data.model_scene_index < scenes.size()) ? gui_data.model_scene_index : 0;

		// update camera
		{
			camera.aspect_ratio = (float)back_buffer_width / back_buffer_height;
			// We are taking the transpose of the projection matrix because we use post multiplication where as DirextMath uses pre-multiplication
			XMMATRIX xm_clip_from_view = XMMatrixTranspose(XMMatrixPerspectiveFovLH(XMConvertToRadians(camera.vertical_fov_in_degrees), camera.aspect_ratio, camera.near_plane_in_meters, camera.far_plane_in_meters));
			XMVECTOR determinant;
			XMMATRIX xm_view_from_clip = XMMatrixInverse(&determinant, xm_clip_from_view);
			XMStoreFloat4x4(&camera.clip_from_view, xm_clip_from_view);
			XMStoreFloat4x4(&camera.view_from_clip, xm_view_from_clip);
		}

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

			static float move_speed_mps = 40.0f;
			static float speed_scale = .06f;
			static float delta_time_s = 0.016666f;

			float forward = move_speed_mps * speed_scale * ((gui_data.is_w_pressed ? delta_time_s : 0.0f) + (gui_data.is_s_pressed ? -delta_time_s : 0.0f));
			float strafe = move_speed_mps * speed_scale * ((gui_data.is_d_pressed ? delta_time_s : 0.0f) + (gui_data.is_a_pressed ? -delta_time_s : 0.0f));
			float ascent = move_speed_mps * speed_scale * ((gui_data.is_e_pressed ? delta_time_s : 0.0f) + (gui_data.is_q_pressed ? -delta_time_s : 0.0f));

			static float prev_forward = 0.0f;
			static float prev_strafe = 0.0f;
			static float prev_ascent = 0.0f;

			auto dampen_motion = [&](float &prev_val, float &new_val) {
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

			dampen_motion(prev_forward, forward);
			dampen_motion(prev_strafe, strafe);
			dampen_motion(prev_ascent, ascent);

			{
				// Left-handed +y : up, +x: right View Space -> Right-handed +z : up, -y: right World Space
				static const XMMATRIX xm_change_of_basis = { 
					0.0, 0.0,-1.0, 0,
					1.0, 0.0, 0.0, 0,
					0.0, 1.0, 0.0, 0,
					0.0, 0.0, 0.0, 1.0
				};

				auto xm_rotate_y = XMMatrixRotationY(-camera.yaw_rad);
				auto xm_rotate_x = XMMatrixRotationX(-camera.pitch_rad);
				auto xm_world_from_view = xm_change_of_basis * xm_rotate_y * xm_rotate_x;

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

			gui_data.camera_yaw = camera.yaw_rad;
			gui_data.camera_pitch = camera.pitch_rad;
			gui_data.camera_pos = camera.pos_ws;
		}
	}

	const vector<DrawInfo>& get_opaque_draw_list() {
		return scenes[current_scene_index]->opaque_draw_info_list;
	}

	const vector<DrawInfo>& get_alpha_blend_draw_list() {
		return scenes[current_scene_index]->alpha_blend_draw_info_list;
	}

	const vector<XMFLOAT4X4>& get_transformation_list() {
		return scenes[current_scene_index]->node_transformations;
	}

	const vector<Material>& get_material_list() {
		return scenes[current_scene_index]->materials;
	}

	const Camera& get_camera() {
		return camera;
	}

	pair<uint32_t, uint32_t> get_scene_texture_usage() {
		auto& p_scene = scenes[current_scene_index];
		return make_pair(p_scene->start_index_into_textures,p_scene->num_used_textures);
	}

} // namespace scene_amanger