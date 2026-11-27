#include "LimiterPlugin.h"

namespace limiter { namespace plugin {

vst3mini::IPluginFactory* getLimiterPluginFactory() {
    static LimiterFactory factory;
    return &factory;
}

}}

#if defined(_WIN32)
#define VST3_EXPORT __declspec(dllexport)
#else
#define VST3_EXPORT __attribute__((visibility("default")))
#endif

extern "C" {

VST3_EXPORT vst3mini::IPluginFactory* GetPluginFactory() {
    return limiter::plugin::getLimiterPluginFactory();
}

VST3_EXPORT bool InitModule() { return true; }

VST3_EXPORT bool DeinitModule() { return true; }

}

#if defined(_WIN32)
#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    (void)instance;
    if (reason == DLL_PROCESS_ATTACH)
        return InitModule() ? TRUE : FALSE;
    if (reason == DLL_PROCESS_DETACH)
        DeinitModule();
    return TRUE;
}
#endif
