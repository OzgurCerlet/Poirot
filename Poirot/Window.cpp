#include "Window.h"

Window::Window(HINSTANCE h_instance, uint16_t width, uint16_t height) : _h_instance(h_instance), _width(width), _height(height) {

	WNDCLASSEX window_class = { 0 };
	window_class.cbSize = sizeof(WNDCLASSEX);
	window_class.style = CS_HREDRAW | CS_VREDRAW;
	window_class.hIcon = (HICON)(LoadImage(NULL, "poirot.ico", IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE));
	window_class.lpfnWndProc = window_proc;
	window_class.hInstance = _h_instance;
	window_class.lpszClassName = "Poirot Window Class";
	RegisterClassEx(&window_class);

	RECT window_rect = { 0,0,static_cast<LONG>(_width),static_cast<LONG>(_height) };
	AdjustWindowRect(&window_rect, WS_OVERLAPPEDWINDOW, FALSE);

	_h_window = CreateWindow(
		window_class.lpszClassName, "Poirot", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
		window_rect.right - window_rect.left, window_rect.bottom - window_rect.top, NULL, NULL, _h_instance, NULL
	);
	if(_h_window == NULL) { throw std::exception(); };

	ShowWindow(_h_window, TRUE);
	UpdateWindow(_h_window);
}

HWND Window::get_handle() {
	return _h_window;
}

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK Window::window_proc(HWND h_window, UINT msg, WPARAM wparam, LPARAM lparam)
{
	if(ImGui_ImplWin32_WndProcHandler(h_window, msg, wparam, lparam)) {
		return true;
	}

	PAINTSTRUCT paint_struct;
	HDC h_device_context;
	switch(msg) {
		case WM_PAINT:{
			h_device_context = BeginPaint(h_window, &paint_struct);
			EndPaint(h_window, &paint_struct);
		} break;
		case WM_KEYDOWN: if(wparam == VK_ESCAPE) PostQuitMessage(0); break;
		case WM_QUIT:
		case WM_DESTROY:
			PostQuitMessage(0);
			break;
		default:
			return DefWindowProc(h_window, msg, wparam, lparam);
	}

	return 0;
}
