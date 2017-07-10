#include "dx.h"

#include <Windows.h>

// Some utility/helpers

#define UNUSED(x) ((void)(x))

// General TODO: maybe support UNICODE? Add appropriate preprocessor symbol!

HWND window_ = nullptr;
LPCSTR windowName_ = "DX12 window";
LPCSTR windowTitle_ = "DX12 window";

int width_ = 800;
int height_ = 600;

bool isFullscreen_ = false;

bool isRunning_ = true;

// function declarations
static LRESULT CALLBACK windowProcess(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

// function definitions
bool initWindow(HINSTANCE instance, int showWindow, int width, int height, bool fullscreen)
{
    if (fullscreen) {
        HMONITOR monitor = MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST);

        // This could be expressed in one line:
        // MONITORINFO mi = { sizeof(mi) }
        // but I find it extremely ugly and unreadable
        MONITORINFO mi = { 0 };
        mi.cbSize = sizeof(mi);
        GetMonitorInfo(monitor, &mi);

        width = mi.rcMonitor.right - mi.rcMonitor.left;
        height = mi.rcMonitor.bottom - mi.rcMonitor.top;
    }

    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = windowProcess;
    wc.cbClsExtra = NULL;
    wc.cbWndExtra = NULL;
    wc.hInstance = instance;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 2);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = windowName_;
    wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassEx(&wc)) {
        MessageBox(NULL, "Error registering window class", "Error", MB_OK | MB_ICONERROR);

        return false;
    }

    window_ = CreateWindowEx(NULL, windowName_, windowTitle_, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, width, height, NULL, NULL, instance, NULL);

    if (!window_) {
        MessageBox(NULL, "Error creating window", "Error", MB_OK | MB_ICONERROR);

        return false;
    }

    if (fullscreen) {
        SetWindowLong(window_, GWL_STYLE, 0);
    }

    ShowWindow(window_, showWindow);
    UpdateWindow(window_);

    return true;
}

LRESULT windowProcess(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_KEYDOWN:
    {
        if (wparam == VK_ESCAPE) {
            isRunning_ = false;
            DestroyWindow(window);
        }

        return 0;
    }

    case WM_DESTROY:
    {
        isRunning_ = false;
        PostQuitMessage(0);
        return 0;
    }

    }

    return DefWindowProc(window, message, wparam, lparam);
}

void appMain()
{
    MSG msg = { 0 };

    while (isRunning_) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else {
            // Run game code
            update();
            render();
        }
    }
}

void errorCallback()
{
    isRunning_ = false;
}

int WinMain(HINSTANCE instance, HINSTANCE prevInstance, LPSTR cmdLineArgs, int showWindow)
{
    UNUSED(prevInstance);
    UNUSED(cmdLineArgs);

    if (!initWindow(instance, showWindow, width_, height_, isFullscreen_)) {
        MessageBox(0, "Window initialization failed", "Error", MB_OK);
        return 0;
    }

    if (!initd3d(window_, width_, height_, isFullscreen_, errorCallback)) {
        cleanupd3d();
        return 0;
    }

    isRunning_ = true;
    appMain();

    cleanupd3d();

    return 0;
}
