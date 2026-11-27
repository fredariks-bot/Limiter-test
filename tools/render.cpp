#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "plugin/LimiterPlugin.h"

using namespace vst3mini;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kFs = 44100.0;

void writeWav(const std::string& path, const std::vector<float>& l, const std::vector<float>& r) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    const uint32_t numSamples = uint32_t(l.size());
    const uint32_t dataSize = numSamples * 2 * 2;
    const uint32_t byteRate = uint32_t(kFs) * 2 * 2;
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f);
    u32(36 + dataSize);
    std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f);
    u32(16);
    u16(1);
    u16(2);
    u32(uint32_t(kFs));
    u32(byteRate);
    u16(4);
    u16(16);
    std::fwrite("data", 1, 4, f);
    u32(dataSize);
    for (uint32_t i = 0; i < numSamples; ++i) {
        auto clip = [](float x) {
            if (x > 1.0f) x = 1.0f;
            if (x < -1.0f) x = -1.0f;
            return int16_t(x * 32767.0f);
        };
        const int16_t ls = clip(l[i]);
        const int16_t rs = clip(r[i]);
        std::fwrite(&ls, 2, 1, f);
        std::fwrite(&rs, 2, 1, f);
    }
    std::fclose(f);
}

double peakOf(const std::vector<float>& v) {
    double p = 0.0;
    for (float x : v) p = std::max(p, double(std::fabs(x)));
    return p;
}

double db(double lin) { return lin > 1e-12 ? 20.0 * std::log10(lin) : -240.0; }

}

int main(int argc, char** argv) {
    using namespace limiter::plugin;
    const std::string outDir = argc > 1 ? argv[1] : ".";

    IPluginFactory* factory = getLimiterPluginFactory();
    PClassInfo cinfo;
    if (!factory || factory->getClassInfo(0, &cinfo) != kResultOk) {
        std::fprintf(stderr, "no factory\n");
        return 1;
    }
    void* obj = nullptr;
    if (factory->createInstance(cinfo.cid, kIComponentIID, &obj) != kResultOk) {
        std::fprintf(stderr, "createInstance failed\n");
        return 1;
    }
    IComponent* comp = static_cast<IComponent*>(obj);
    IAudioProcessor* proc = nullptr;
    IEditController* ctrl = nullptr;
    comp->queryInterface(kIAudioProcessorIID, reinterpret_cast<void**>(&proc));
    comp->queryInterface(kIEditControllerIID, reinterpret_cast<void**>(&ctrl));
    comp->initialize(nullptr);
    SpeakerArrangement inArr = kSpeakerStereo;
    SpeakerArrangement outArr = kSpeakerStereo;
    proc->setBusArrangements(&inArr, 1, &outArr, 1);
    ProcessSetup setup;
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = 512;
    setup.sampleRate = kFs;
    proc->setupProcessing(setup);
    comp->setActive(true);
    proc->setProcessing(true);
    ctrl->setParamNormalized(kParamCeiling, ctrl->plainParamToNormalized(kParamCeiling, -1.0));
    ctrl->setParamNormalized(kParamTruePeak, 1.0);
    ctrl->setParamNormalized(kParamLink, 1.0);

    const int seconds = 3;
    const int n = int(kFs) * seconds;
    std::vector<float> inL(n), inR(n);
    unsigned int rng = 12345;
    for (int i = 0; i < n; ++i) {
        rng = rng * 1664525u + 1013904223u;
        const float noise = (float(rng >> 8) / 8388608.0f) - 1.0f;
        const double t = i / kFs;
        const float base = 0.95f * float(std::sin(2.0 * kPi * 220.0 * t))
                         + 0.25f * float(std::sin(2.0 * kPi * 2997.0 * t));
        float x = base + 0.05f * noise;
        if (i % int(kFs) < 64) x = 1.0f;
        inL[size_t(i)] = x;
        inR[size_t(i)] = 0.9f * x;
    }
    std::vector<float> outL = inL, outR = inR;

    const int block = 512;
    float* inPtrs[2] = {inL.data(), inR.data()};
    float* outPtrs[2] = {outL.data(), outR.data()};
    AudioBusBuffers inBus;
    inBus.numChannels = 2;
    inBus.silenceFlags = 0;
    inBus.channelBuffers32 = inPtrs;
    AudioBusBuffers outBus;
    outBus.numChannels = 2;
    outBus.silenceFlags = 0;
    outBus.channelBuffers32 = outPtrs;
    ProcessData data;
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numInputs = 1;
    data.numOutputs = 1;
    data.inputs = &inBus;
    data.outputs = &outBus;

    for (int off = 0; off < n; off += block) {
        const int count = std::min(block, n - off);
        inPtrs[0] = inL.data() + off;
        inPtrs[1] = inR.data() + off;
        outPtrs[0] = outL.data() + off;
        outPtrs[1] = outR.data() + off;
        data.numSamples = count;
        if (proc->process(data) != kResultOk) {
            std::fprintf(stderr, "process failed\n");
            return 1;
        }
    }

    const double inPeak = peakOf(inL);
    const double outPeak = std::max(peakOf(outL), peakOf(outR));
    std::printf("input  peak: %.2f dBFS\n", db(inPeak));
    std::printf("output peak: %.2f dBFS (ceiling -1.00 dBFS)\n", db(outPeak));
    std::printf("latency: %u samples\n", proc->getLatencySamples());

    writeWav(outDir + "/limiter_input.wav", inL, inR);
    writeWav(outDir + "/limiter_output.wav", outL, outR);
    std::printf("wrote %s/limiter_input.wav and %s/limiter_output.wav\n", outDir.c_str(),
                outDir.c_str());

    proc->setProcessing(false);
    comp->setActive(false);
    comp->terminate();
    ctrl->release();
    proc->release();
    comp->release();

    const bool ok = db(outPeak) <= -1.0 + 0.2;
    if (!ok) std::fprintf(stderr, "FAIL: output exceeds ceiling\n");
    return ok ? 0 : 1;
}
