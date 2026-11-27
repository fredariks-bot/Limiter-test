#include "X11Shim.h"

#if defined(__linux__) && !defined(_WIN32)

#include <dlfcn.h>

namespace x11 {

bool loadApi(Api& a) {
    static void* handle = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        handle = dlopen("libX11.so.6", RTLD_NOW | RTLD_LOCAL);
        if (!handle) handle = dlopen("libX11.so", RTLD_NOW | RTLD_LOCAL);
    }
    if (!handle) return false;
    auto sym = [&](const char* name) -> void* { return dlsym(handle, name); };
    a.XOpenDisplay   = (decltype(a.XOpenDisplay))sym("XOpenDisplay");
    a.XCloseDisplay  = (decltype(a.XCloseDisplay))sym("XCloseDisplay");
    a.XDefaultScreen = (decltype(a.XDefaultScreen))sym("XDefaultScreen");
    a.XCreateGC      = (decltype(a.XCreateGC))sym("XCreateGC");
    a.XFreeGC        = (decltype(a.XFreeGC))sym("XFreeGC");
    a.XSetForeground  = (decltype(a.XSetForeground))sym("XSetForeground");
    a.XFillRectangle = (decltype(a.XFillRectangle))sym("XFillRectangle");
    a.XDrawRectangle = (decltype(a.XDrawRectangle))sym("XDrawRectangle");
    a.XDrawLine      = (decltype(a.XDrawLine))sym("XDrawLine");
    a.XDrawString    = (decltype(a.XDrawString))sym("XDrawString");
    a.XSelectInput   = (decltype(a.XSelectInput))sym("XSelectInput");
    a.XPending       = (decltype(a.XPending))sym("XPending");
    a.XNextEvent     = (decltype(a.XNextEvent))sym("XNextEvent");
    a.XSync          = (decltype(a.XSync))sym("XSync");
    a.XFlush         = (decltype(a.XFlush))sym("XFlush");
    a.XInitThreads   = (decltype(a.XInitThreads))sym("XInitThreads");
    return a.XOpenDisplay && a.XNextEvent && a.XFillRectangle && a.XDrawLine && a.XDrawString;
}

}

#endif
