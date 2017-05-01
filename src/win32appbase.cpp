/**
 * (C) roentgen 
 * this code is licensed under the MIT License.
 */
#include "stdafx.h"
#include "dbgutils.hpp"
#include "win32appbase.hpp"

HWND win32_window_proc_t::hwnd_ = nullptr;

int win32_window_proc_t::run(app_interface_t* app, HINSTANCE instance, int cmd)
{
    WNDCLASSEX wcls = {};
    wcls.cbSize = sizeof(WNDCLASSEX);
    wcls.style = CS_HREDRAW | CS_VREDRAW;
    wcls.lpfnWndProc = proc;
    wcls.hInstance = instance;
    wcls.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcls.lpszClassName = L"win32app_base_t";
    RegisterClassEx(&wcls);

    RECT rect = {0, 0, static_cast<LONG>(app->get_width()), static_cast<LONG>(app->get_height())};

    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    hwnd_ = CreateWindow(wcls.lpszClassName, app->get_title(),
                         WS_OVERLAPPEDWINDOW,
                         CW_USEDEFAULT, CW_USEDEFAULT,
                         rect.right - rect.left, rect.bottom - rect.top,
                         nullptr, nullptr, instance, app);

    app->on_init();

    ShowWindow(hwnd_, cmd);

    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    app->on_final();
    return static_cast<char>(msg.wParam);
}

LRESULT CALLBACK win32_window_proc_t::proc(HWND wnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto* app = reinterpret_cast<appbase_t*>(GetWindowLongPtr(wnd, GWLP_USERDATA));

    switch (msg) {
    case WM_CREATE:
        {
            auto p = reinterpret_cast<LPCREATESTRUCT>(lParam);
            SetWindowLongPtr(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(p->lpCreateParams));
        }
        return 0;
    case WM_SIZE:
        {
            if (!app)
                return 0;
            INF("WM_SIZE: wParam:0x%x lParam:0x%x\n", wParam, lParam);
            if (wParam == SIZE_MAXHIDE || wParam == SIZE_MINIMIZED) {
            }
            else if (wParam == SIZE_MAXIMIZED || wParam == SIZE_MAXSHOW) {
            }
            else {
                uint32_t h = HIWORD(lParam);
                uint32_t w = LOWORD(lParam);
                if (app->on_resize(w, h)) 
                    return 0;
            }
        }
        break;
    case WM_KEYDOWN:
        if (app)
            app->on_key(true, static_cast<uint8_t>(wParam));
        return 0;
    case WM_KEYUP:
        if (app)
            app->on_key(false, static_cast<uint8_t>(wParam));
        return 0;
    case WM_PAINT:
        if (app) {
            app->on_update();
            app->on_draw();
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProc(wnd, msg, wParam, lParam);
}
