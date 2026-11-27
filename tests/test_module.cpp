#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <dlfcn.h>

#include "plugin/LimiterPlugin.h"

int g_checks = 0;
int g_failures = 0;

#define CHECK(cond)                                                                              \
    do {                                                                                         \
        ++g_checks;                                                                              \
        if (!(cond)) {                                                                           \
            ++g_failures;                                                                        \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                          \
        }                                                                                        \
    } while (0)

using namespace vst3mini;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("SKIP: no module path given\n");
        return 0;
    }
    void* handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    CHECK(handle != nullptr);
    if (!handle) {
        std::printf("dlopen failed: %s\n", dlerror());
        return 1;
    }
    auto getFactory = reinterpret_cast<IPluginFactory* (*)()>(dlsym(handle, "GetPluginFactory"));
    CHECK(getFactory != nullptr);
    if (!getFactory) return 1;
    IPluginFactory* factory = getFactory();
    CHECK(factory != nullptr);

    PFactoryInfo finfo;
    CHECK(factory->getFactoryInfo(&finfo) == kResultOk);
    CHECK(factory->countClasses() == 1);
    PClassInfo cinfo;
    CHECK(factory->getClassInfo(0, &cinfo) == kResultOk);
    CHECK(std::strcmp(cinfo.category, "Audio Module Class") == 0);

    void* obj = nullptr;
    CHECK(factory->createInstance(cinfo.cid, kIComponentIID, &obj) == kResultOk);
    IComponent* comp = static_cast<IComponent*>(obj);
    IAudioProcessor* proc = nullptr;
    IEditController* ctrl = nullptr;
    CHECK(comp->queryInterface(kIAudioProcessorIID, reinterpret_cast<void**>(&proc)) == kResultOk);
    CHECK(comp->queryInterface(kIEditControllerIID, reinterpret_cast<void**>(&ctrl)) == kResultOk);
    CHECK(proc && ctrl);
    CHECK(comp->initialize(nullptr) == kResultOk);

    SpeakerArrangement inArr = kSpeakerStereo;
    SpeakerArrangement outArr = kSpeakerStereo;
    CHECK(proc->setBusArrangements(&inArr, 1, &outArr, 1) == kResultTrue);
    CHECK(proc->canProcessSampleSize(kSample32) == kResultTrue);
    CHECK(proc->canProcessSampleSize(kSample64) == kResultTrue);
    ProcessSetup setup;
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = 512;
    setup.sampleRate = 44100.0;
    CHECK(proc->setupProcessing(setup) == kResultOk);
    CHECK(ctrl->setParamNormalized(limiter::plugin::kParamCeiling, 0.75) == kResultOk);
    CHECK(comp->setActive(true) == kResultOk);
    CHECK(proc->setProcessing(true) == kResultOk);

    constexpr double kPi = 3.14159265358979323846;
    const int N = 512;
    const double target = std::pow(10.0, -6.0 / 20.0);

    for (int mode = 0; mode < 2; ++mode) {
        const bool is64 = mode == 1;
        std::vector<float> inF(N), outF(N);
        std::vector<double> inD(N), outD(N);
        for (int i = 0; i < N; ++i) {
            const double s = std::sin(2.0 * kPi * 1000.0 * i / 44100.0);
            inF[size_t(i)] = float(s);
            inD[size_t(i)] = s;
            outF[size_t(i)] = 0.0f;
            outD[size_t(i)] = 0.0;
        }
        float* inFP[2] = {inF.data(), inF.data()};
        float* outFP[2] = {outF.data(), outF.data()};
        double* inDP[2] = {inD.data(), inD.data()};
        double* outDP[2] = {outD.data(), outD.data()};
        AudioBusBuffers inBus, outBus;
        inBus.numChannels = 2;
        inBus.silenceFlags = 0;
        outBus.numChannels = 2;
        outBus.silenceFlags = 0;
        if (is64) {
            inBus.channelBuffers64 = inDP;
            outBus.channelBuffers64 = outDP;
        } else {
            inBus.channelBuffers32 = inFP;
            outBus.channelBuffers32 = outFP;
        }
        ProcessData data;
        data.processMode = kRealtime;
        data.symbolicSampleSize = is64 ? kSample64 : kSample32;
        data.numSamples = N;
        data.numInputs = 1;
        data.numOutputs = 1;
        data.inputs = &inBus;
        data.outputs = &outBus;
        CHECK(proc->process(data) == kResultOk);
        double peak = 0.0;
        for (int i = 0; i < N; ++i) {
            const double v = is64 ? outD[size_t(i)] : double(outF[size_t(i)]);
            peak = std::max(peak, std::fabs(v));
        }
        CHECK(peak <= target + 0.05);
        CHECK(peak > 0.45);
    }

    CHECK(proc->setProcessing(false) == kResultOk);
    CHECK(comp->setActive(false) == kResultOk);
    CHECK(comp->terminate() == kResultOk);
    ctrl->release();
    proc->release();
    comp->release();
    dlclose(handle);
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
