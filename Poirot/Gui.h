#pragma once

#include <cstdint>
#include <memory>

#include "external\dear_imgui\imgui.h"
#include "external\dear_imgui\imgui_impl_dx12.h"

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

	// background
	int background_env_map_type;
	int background_specular_irradiance_mip_level;
};

class Gui
{
public:
	Gui(void* window_handle, uint8_t max_inflight_frame_count, ID3D12Device *p_device, DXGI_FORMAT rtv_format, D3D12_CPU_DESCRIPTOR_HANDLE gui_font_srv_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE gui_font_srv_gpu_desc_handle);
	~Gui();

	const GuiData& get_data();
	void render();
	void update(ID3D12GraphicsCommandList *p_command_list);
private:
	std::unique_ptr<GuiData> unq_data;
};