#pragma once
#include <cstdint>
#include <windows.h>
#include <exception>

class Window
{
public:
	Window() = delete;
	~Window() = default;
	Window(HINSTANCE h_instance, uint16_t width = 960, uint16_t height = 540);

	HWND get_handle();

	static LRESULT CALLBACK window_proc(HWND h_window, UINT message, WPARAM wparam, LPARAM lparam);
private:
	HINSTANCE	_h_instance;
	HWND		_h_window;
	uint16_t	_width;
	uint16_t	_height;

};
