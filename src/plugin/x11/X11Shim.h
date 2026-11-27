#pragma once

#if defined(__linux__) && !defined(_WIN32)

#include <cstdint>

namespace x11 {

typedef unsigned long Window;
typedef struct _XDisplay Display;
typedef void* GC;

union Event {
    int type;
    struct {
        int type; unsigned long serial; int send_event; Display* display; Window window;
        int x, y; unsigned int width, height; int count;
    } expose;
    struct {
        int type; unsigned long serial; int send_event; Display* display; Window window;
        Window root; Window subwindow; unsigned long time;
        int x, y, x_root, y_root; unsigned int state; unsigned int button; int same_screen;
    } button;
    struct {
        int type; unsigned long serial; int send_event; Display* display; Window window;
        Window root; Window subwindow; unsigned long time;
        int x, y, x_root, y_root; unsigned int state; char is_hint; int same_screen;
    } motion;
    long pad[32];
};

enum { ButtonPress = 4, ButtonRelease = 5, MotionNotify = 6, Expose = 12 };
enum : long {
    ButtonPressMask     = 1L << 2,
    ButtonReleaseMask   = 1L << 3,
    PointerMotionMask   = 1L << 6,
    ExposureMask        = 1L << 15
};
enum : unsigned int { Button1Mask = 1u << 8 };

struct Api {
    Display* (*XOpenDisplay)(const char*) = nullptr;
    int (*XCloseDisplay)(Display*) = nullptr;
    int (*XDefaultScreen)(Display*) = nullptr;
    GC (*XCreateGC)(Display*, Window, unsigned long, void*) = nullptr;
    int (*XFreeGC)(Display*, GC) = nullptr;
    int (*XSetForeground)(Display*, GC, unsigned long) = nullptr;
    int (*XFillRectangle)(Display*, Window, GC, int, int, unsigned, unsigned) = nullptr;
    int (*XDrawRectangle)(Display*, Window, GC, int, int, unsigned, unsigned) = nullptr;
    int (*XDrawLine)(Display*, Window, GC, int, int, int, int) = nullptr;
    int (*XDrawString)(Display*, Window, GC, int, int, const char*, int) = nullptr;
    int (*XSelectInput)(Display*, Window, long) = nullptr;
    int (*XPending)(Display*) = nullptr;
    int (*XNextEvent)(Display*, Event*) = nullptr;
    int (*XSync)(Display*, int) = nullptr;
    int (*XFlush)(Display*) = nullptr;
    int (*XInitThreads)() = nullptr;
};

bool loadApi(Api& api);

}

#endif
