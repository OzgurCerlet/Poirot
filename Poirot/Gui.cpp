#include "Gui.h"
#include <d3d12.h>

using namespace std;

static ImVec2 last_mouse_pos = { 0,0 };
static float mouse_pos_scale = 0.0025;

Gui::Gui(void* window_handle, uint8_t max_inflight_frame_count,ID3D12Device *p_device, DXGI_FORMAT rtv_format, D3D12_CPU_DESCRIPTOR_HANDLE gui_font_srv_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE gui_font_srv_gpu_desc_handle) {
	unq_data = make_unique<GuiData>();
	ImGui::CreateContext();
	ImGui_ImplDX12_Init(window_handle, max_inflight_frame_count, p_device, rtv_format /*DXGI_FORMAT_R8G8B8A8_UNORM*/, gui_font_srv_cpu_desc_handle, gui_font_srv_gpu_desc_handle);
}

Gui::~Gui() {
	ImGui_ImplDX12_Shutdown();
	unq_data.reset();
}

void Gui::update(ID3D12GraphicsCommandList *p_command_list) {
	ImGui_ImplDX12_NewFrame(p_command_list);
	{
		ImVec2 mouse_pos = ImGui::GetMousePos();
		ImGuiIO& io = ImGui::GetIO();
		bool is_mouse_captured = io.WantCaptureMouse;
		bool is_right_mouse_button_pressed = ImGui::IsMouseDown(1);
		
		if(is_right_mouse_button_pressed) {
			unq_data->delta_pitch_rad = (mouse_pos.y - last_mouse_pos.y) * mouse_pos_scale;
			unq_data->delta_yaw_rad = (mouse_pos.x - last_mouse_pos.x) * mouse_pos_scale;
			
			unq_data->is_a_pressed = io.KeysDown['A'];
			unq_data->is_d_pressed = io.KeysDown['D'];
			unq_data->is_e_pressed = io.KeysDown['E'];
			unq_data->is_q_pressed = io.KeysDown['Q'];
			unq_data->is_s_pressed = io.KeysDown['S'];
			unq_data->is_w_pressed = io.KeysDown['W'];
		}
		else {
			unq_data->delta_pitch_rad = 0.0;
			unq_data->delta_yaw_rad = 0.0;
			
			unq_data->is_a_pressed = false;
			unq_data->is_d_pressed = false;
			unq_data->is_e_pressed = false;
			unq_data->is_q_pressed = false;
			unq_data->is_s_pressed = false;
			unq_data->is_w_pressed = false;
		}
		last_mouse_pos = mouse_pos;
		//if(io.MouseWheel < 0.0) { gui_data.view_distance *= 1.05f; }
		//else if(io.MouseWheel > 0.0) { gui_data.view_distance /= 1.05f; };
	}
	// Prepare gui
	{
		ImGui::SliderFloat("View azimuth angle", &unq_data->view_azimuth_angle_in_degrees, 0.0f, 360.0);
		ImGui::SliderFloat("View zenith angle", &unq_data->view_zenith_angle_in_degrees, 0.0f, 90.0);
		ImGui::SliderFloat("View distance", &unq_data->view_distance_in_meters, 1.0f, 1000.0);
		ImGui::Text("Mouse Pos:  %.3f, %.3f", last_mouse_pos.x, last_mouse_pos.y);
		ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);	
	}

	ImGui::Separator();

	{
		ImGui::PushItemWidth(200);
		ImGui::Text("Model: ");
		const char* a_scenes[] = { "CVC Helmet", "Damaged Sci-fi Helmet", "Cartoon Pony", "Vintage Suitcase" };
		ImGui::Combo("Model", &unq_data->model_scene_index, a_scenes, IM_ARRAYSIZE(a_scenes));
	}
	
	ImGui::Separator();
	
	{
		ImGui::PushItemWidth(200);
		ImGui::Text("Image Based Lighting: ");
		const char* a_environments[] = { "Courtyard Night", "Ninomaru Teien" };
		ImGui::Combo("Environment", &unq_data->ibl_environment_index, a_environments, IM_ARRAYSIZE(a_environments));
	}
	
	ImGui::Separator();

	{
		ImGui::PushItemWidth(200);
		ImGui::Text("Background: ");
		const char* a_env_map_types[] = { "Radiance", "Diffuse Irradiance", "Specular Irradiance" };
		ImGui::Combo("Environment Map Types", &unq_data->background_env_map_type, a_env_map_types, IM_ARRAYSIZE(a_env_map_types));
		ImGui::SliderInt("Specular Irradiance Map Mip Level", &unq_data->background_specular_irradiance_mip_level, 0, 10);
	}
}

const GuiData& Gui::get_data() {
	return *unq_data;
}

void Gui::render() {
	ImGui::Render();
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData());
}