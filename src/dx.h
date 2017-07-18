#if !defined(DX_H)
#define DX_H

#define NOMINMAX

#include <windows.h>

#undef NOMINMAX

typedef void (*OnErrorCallback)();

bool initd3d(HWND window, int width, int height, bool fullscreen, OnErrorCallback errorCallback);

void update();

void render();

void cleanupd3d();

#endif // defined(DX_H)
