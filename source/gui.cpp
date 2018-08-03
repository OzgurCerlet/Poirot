namespace gui
{
	GuiData gui_data{};
	ImVec2 last_mouse_pos{ 0,0 };
	float mouse_pos_scale{ 0.0025f };

	void init(
		void* window_handle, uint8_t max_inflight_frame_count, 
		ID3D12Device *p_device, DXGI_FORMAT rtv_format, 
		D3D12_CPU_DESCRIPTOR_HANDLE gui_font_srv_cpu_desc_handle, 
		D3D12_GPU_DESCRIPTOR_HANDLE gui_font_srv_gpu_desc_handle) {

		ImGui::CreateContext();
		ImGui_ImplDX12_Init(window_handle, max_inflight_frame_count, p_device, rtv_format /*DXGI_FORMAT_R8G8B8A8_UNORM*/, gui_font_srv_cpu_desc_handle, gui_font_srv_gpu_desc_handle);
	}

	void clean_up() {
		ImGui_ImplDX12_Shutdown();
	}

	void update(ID3D12GraphicsCommandList *p_command_list) {
		ImGui_ImplDX12_NewFrame(p_command_list);
		{
			ImVec2 mouse_pos = ImGui::GetMousePos();
			ImGuiIO& io = ImGui::GetIO();
			bool is_mouse_captured = io.WantCaptureMouse;
			bool is_right_mouse_button_pressed = ImGui::IsMouseDown(1);

			if(is_right_mouse_button_pressed) {
				gui_data.delta_pitch_rad = (mouse_pos.y - last_mouse_pos.y) * mouse_pos_scale;
				gui_data.delta_yaw_rad = (mouse_pos.x - last_mouse_pos.x) * mouse_pos_scale;
				
				gui_data.is_a_pressed = io.KeysDown['A'];
				gui_data.is_d_pressed = io.KeysDown['D'];
				gui_data.is_e_pressed = io.KeysDown['E'];
				gui_data.is_q_pressed = io.KeysDown['Q'];
				gui_data.is_s_pressed = io.KeysDown['S'];
				gui_data.is_w_pressed = io.KeysDown['W'];
			}
			else {
				gui_data.delta_pitch_rad = 0.0;
				gui_data.delta_yaw_rad = 0.0;

				gui_data.is_a_pressed = false;
				gui_data.is_d_pressed = false;
				gui_data.is_e_pressed = false;
				gui_data.is_q_pressed = false;
				gui_data.is_s_pressed = false;
				gui_data.is_w_pressed = false;
			}
			last_mouse_pos = mouse_pos;
			//if(io.MouseWheel < 0.0) { gui_data.view_distance *= 1.05f; }
			//else if(io.MouseWheel > 0.0) { gui_data.view_distance /= 1.05f; };
		}
		// Prepare gui
		ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always, ImVec2(0, 0));
		ImGui::SetNextWindowBgAlpha(0.2f); // Transparent background
		ImGui::Begin("Controls", NULL, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
		ImGui::PushItemWidth(200);
		{
			ImGui::Text("View: ");
			const char* a_isolation_modes[] = { "None", "Base Color", "Metallic", "Roughness", "Normal", "Opacity", "Emission", "Diffuse Response", "Specular Response" };
			ImGui::Combo("Isolation Mode", reinterpret_cast<int*>(&gui_data.isolation_mode_index), a_isolation_modes, IM_ARRAYSIZE(a_isolation_modes));
		}
		ImGui::Separator();
		{
			ImGui::Text("Model: ");
			const char* a_scenes[] = { "CVC Helmet", "Damaged Sci-fi Helmet", "Cartoon Pony", "Vintage Suitcase" };
			ImGui::Combo("Model", reinterpret_cast<int*>(&gui_data.model_scene_index), a_scenes, IM_ARRAYSIZE(a_scenes));
		}
		ImGui::Separator();
		{
			ImGui::Text("Image Based Lighting: ");
			const char* a_environments[] = { "Courtyard Night", "Ninomaru Teien", "Paul Lobe Haus" };
			ImGui::Combo("Environment", reinterpret_cast<int*>(&gui_data.ibl_environment_index), a_environments, IM_ARRAYSIZE(a_environments));
		}
		ImGui::Separator();
		{
			ImGui::Text("Background: ");
			const char* a_env_map_types[] = { "Radiance", "Diffuse Irradiance", "Specular Irradiance" };
			ImGui::Combo("Environment Map Types", reinterpret_cast<int*>(&gui_data.background_env_map_type), a_env_map_types, IM_ARRAYSIZE(a_env_map_types));
			ImGui::SliderInt("Specular Irradiance Map Mip Level", reinterpret_cast<int*>(&gui_data.background_specular_irradiance_mip_level), 0, 10);
		}
		ImGui::Separator();
		{
			ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
		}
		ImGui::Separator();
		ImGui::End();
	}

	void render() {
		ImGui::Render();
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData());
	}

	const GuiData& get_data() {
		return gui_data;
	}
} // namespace gui