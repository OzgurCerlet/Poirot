#define _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "external/tiny_gltf/tiny_gltf.h"
#include "external/stb/stb_image_resize.h"
#include "external/dear_imgui/imgui.h"
#include "external/dear_imgui/imgui_impl_dx12.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <wrl.h>

#include <dxgi1_6.h>
#include <dxgidebug.h>
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

#include "D:/Octarine/OctarineImage/octarine_image.h"
#include "D:/Octarine/OctarineMesh/octarine_mesh.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace std;

#include "common.cpp"
#include "window.cpp"
#include "gui.cpp"
#include "renderer.cpp"
#include "scene_manager.cpp"

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "d3dcompiler.lib")

unique_ptr<Window> unq_window = nullptr;

void init(HINSTANCE h_instance) {

	unq_window = make_unique<Window>(h_instance, back_buffer_width, back_buffer_height);
	renderer::init(unq_window->get_handle());
	scene_manager::init();
	renderer::execute_initial_commands();

	auto [gui_font_srv_cpu_desc_handle, gui_font_srv_gpu_desc_handle] = renderer::get_handles_for_a_srv_desc();
	gui::init(unq_window->get_handle(), max_inflight_frame_count, renderer::get_device(), back_buffer_format, gui_font_srv_cpu_desc_handle, gui_font_srv_gpu_desc_handle);
}

void update() {
	gui::update(renderer::get_command_list());
	auto gui_data = gui::get_data();

	scene_manager::update(gui_data);
	auto [start_index_into_textures, num_used_textures] = scene_manager::get_scene_texture_usage();
	auto camera = scene_manager::get_camera();
	
	renderer::update(gui_data, start_index_into_textures, num_used_textures, camera);
}

void render_frame() {
	renderer::begin_render();
	renderer::render();
	gui::render();
	renderer::end_render();
}

void clean_up() {
	renderer::wait_for_gpu();
	gui::clean_up();

	//ComPtr<ID3D12DebugDevice> com_debug_interface;
	//com_device->QueryInterface(com_debug_interface.GetAddressOf());
	//if(com_debug_interface) {
	//	com_debug_interface->ReportLiveDeviceObjects(D3D12_RLDO_DETAIL | D3D12_RLDO_IGNORE_INTERNAL);
	//}
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
				renderer::present();
				renderer::prepare_next_frame();
			}
		}
		clean_up();

	} catch(std::exception& ex) {
		OutputDebugString(ex.what());
		MessageBox(unq_window->get_handle(), ex.what(), "", 0);
	}

	return 0;
}