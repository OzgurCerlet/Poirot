#define CHECK_D3D12_CALL(x, y)	{string err_0 = y; string err_1 = _STRINGIZE(x); if(FAILED(x)) { err_0 += err_1; throw std::exception(err_0.c_str());}};
#define CHECK_DXGI_CALL(x)	{if(FAILED(x)) { throw std::exception(_STRINGIZE(x));}};
#define CHECK_WIN32_CALL(x)	check_win32_call(x);

inline void check_win32_call(HANDLE h) {
	if(h == NULL) { throw std::exception(); };
}

template <typename T, uint32_t N>
constexpr uint32_t count_of(T(&)[N]) { return N; }

constexpr DXGI_FORMAT	back_buffer_format{ DXGI_FORMAT_R8G8B8A8_UNORM };
constexpr uint32_t		max_texture_count{ 128 };
constexpr uint32_t		max_mesh_count{ 64 };
constexpr float			mip_lod_bias{ -0.5f };
constexpr uint16_t		max_transformation_count_per_scene{ 128 };
constexpr uint16_t		max_material_count_per_scene{ 32 };
constexpr uint16_t		max_descriptor_count_per_frame{ 128 };
uint16_t				back_buffer_width{ 1280 };
uint16_t				back_buffer_height{ 720 };
constexpr uint8_t		ms_count{ 8 };
constexpr uint8_t		ms_quality{ 0 };
constexpr uint8_t		max_anisotropy{ 16 };
constexpr uint8_t		max_inflight_frame_count{ 3 };
constexpr uint8_t		num_descriptor_per_environment{ 3 };
constexpr bool			is_msaa_enabled{ true };
bool					is_mipchain_generation_enabled = true;

const string asset_folder{ "../assets/" };
const string shader_folder{ "../source/shaders/" };

struct GuiData {
	float view_azimuth_angle_in_degrees;
	float view_zenith_angle_in_degrees;
	float view_distance_in_meters;

	float delta_yaw_rad;
	float delta_pitch_rad;

	bool is_w_pressed;
	bool is_s_pressed;
	bool is_d_pressed;
	bool is_a_pressed;
	bool is_e_pressed;
	bool is_q_pressed;

	// view
	uint32_t isolation_mode_index;

	// model
	uint32_t model_scene_index;

	// image based lighting
	uint32_t ibl_environment_index;

	// background
	uint32_t background_env_map_type;
	uint32_t background_specular_irradiance_mip_level;

	// Test
	float test;
	float camera_yaw;
	float camera_pitch;
	XMFLOAT3 camera_pos;
};

struct Vertex {
	XMFLOAT3 pos;
	XMFLOAT3 normal;
	XMFLOAT2 uv;
};

__declspec(align(16)) struct Material {
	enum AlphaMode { ALPHAMODE_OPAQUE, ALPHAMODE_MASK, ALPHAMODE_BLEND };
	XMFLOAT4 basecolor_factor{ 1.0f, 1.0f, 1.0f, 1.0f };
	float metallic_factor{ 1.0f };
	float roughness_factor{ 1.0f };
	float alpha_cutoff{ 1.0f };

	int base_color_texture_index{ -1 };
	int normal_texture_index{ -1 };
	int metallic_roughness_texture_index{ -1 };
	int emissive_texture_index{ -1 };
	int occlusion_texture_index{ -1 };
	AlphaMode alphaMode{ ALPHAMODE_OPAQUE };
};

struct DrawInfo {
	uint32_t mesh_index;
	uint32_t transformation_index;
	uint32_t material_index;
	uint32_t draw_index_count;
	uint32_t draw_first_index;
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

namespace gui { 
	GuiData&				get_data(); 
}

namespace renderer {
	ID3D12Device*				get_device();
	ID3D12GraphicsCommandList*	get_command_list();
	pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> get_handles_for_a_srv_desc();
	bool resize(LPARAM lparam);
}

namespace scene_manager {
	const vector<DrawInfo>&		get_opaque_draw_list();
	const vector<DrawInfo>&		get_alpha_blend_draw_list();
	const vector<XMFLOAT4X4>&	get_transformation_list();
	const vector<Material>&		get_material_list();
	const Camera&				get_camera();
	pair<uint32_t, uint32_t>	get_scene_texture_usage();
}