#include "LimiterPlugin.h"

#include "EditorView.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace limiter { namespace plugin {

using namespace vst3mini;

const FUID kLimiterClassID = fuidFromString("5B3E9A71-2C4D-4E8F-A6B1-0D9C3E7F5A28");

namespace {

void copyAscii(char* dst, size_t dstSize, const char* src) {
    size_t i = 0;
    for (; src[i] && i + 1 < dstSize; ++i) dst[i] = src[i];
    dst[i] = 0;
}

}

LimiterPlugin::LimiterPlugin() {
    for (int i = 0; i < kNumParams; ++i)
        normalized_[i] = plainToNormalized(i, kParamDefs[i].defaultValue);
    block_[0].assign(512, 0.0f);
    block_[1].assign(512, 0.0f);
    applyParameters();
}

LimiterPlugin::~LimiterPlugin() {}

tresult LimiterPlugin::queryInterface(const FUID& iid, void** obj) {
    if (!obj) return kInvalidArgument;
    if (iid == kIUnknownIID || iid == kIPluginBaseIID || iid == kIComponentIID)
        *obj = static_cast<IComponent*>(this);
    else if (iid == kIAudioProcessorIID)
        *obj = static_cast<IAudioProcessor*>(this);
    else if (iid == kIEditControllerIID)
        *obj = static_cast<IEditController*>(this);
    else {
        *obj = nullptr;
        return kNoInterface;
    }
    addRef();
    return kResultOk;
}

uint32 LimiterPlugin::addRef() { return ++refCount_; }

uint32 LimiterPlugin::release() {
    const uint32 r = --refCount_;
    if (r == 0) delete this;
    return r;
}

tresult LimiterPlugin::initialize(IUnknown*) { return kResultOk; }

tresult LimiterPlugin::terminate() { return kResultOk; }

tresult LimiterPlugin::getControllerClassId(FUID& classId) {
    classId = kLimiterClassID;
    return kResultOk;
}

tresult LimiterPlugin::setIoMode(int32) { return kResultOk; }

int32 LimiterPlugin::getBusCount(int32 mediaType, int32 direction) {
    if (mediaType != kAudio) return 0;
    if (direction == kInput || direction == kOutput) return 1;
    return 0;
}

tresult LimiterPlugin::getBusInfo(int32 mediaType, int32 direction, int32 index, BusInfo& bus) {
    if (mediaType != kAudio || index != 0 || (direction != kInput && direction != kOutput))
        return kInvalidArgument;
    bus.mediaType = kAudio;
    bus.direction = direction;
    bus.channelCount = 2;
    bus.busType = kMain;
    bus.flags = kDefaultActive;
    setTCharString(bus.name, 128, direction == kInput ? "Audio Input" : "Audio Output");
    return kResultOk;
}

tresult LimiterPlugin::getRoutingInfo(RoutingInfo& inInfo, RoutingInfo& outInfo) {
    inInfo.mediaType = kAudio;
    inInfo.busIndex = 0;
    inInfo.channel = -1;
    outInfo.mediaType = kAudio;
    outInfo.busIndex = 0;
    outInfo.channel = -1;
    return kResultOk;
}

tresult LimiterPlugin::activateBus(int32, int32, int32, bool) { return kResultOk; }

tresult LimiterPlugin::setActive(bool state) {
    active_ = state;
    if (state) {
        dsp_.setSampleRate(sampleRate_);
        dsp_.setParameters(dspParams_);
        dsp_.reset();
    }
    return kResultOk;
}

tresult LimiterPlugin::getState(IStream* state) {
    if (!state) return kInvalidArgument;
    const int32 magic = 0x564C5431;
    const int32 version = 1;
    const int32 count = kNumParams;
    int32 written = 0;
    state->write(&magic, sizeof(magic), &written);
    state->write(&version, sizeof(version), &written);
    state->write(&count, sizeof(count), &written);
    for (int i = 0; i < kNumParams; ++i) {
        const uint32 id = kParamDefs[i].id;
        const double v = normalized_[i];
        state->write(&id, sizeof(id), &written);
        state->write(&v, sizeof(v), &written);
    }
    return kResultOk;
}

tresult LimiterPlugin::setState(IStream* state) {
    if (!state) return kInvalidArgument;
    int32 magic = 0, version = 0, count = 0, read = 0;
    state->read(&magic, sizeof(magic), &read);
    state->read(&version, sizeof(version), &read);
    state->read(&count, sizeof(count), &read);
    if (magic != 0x564C5431 || version != 1) return kResultFalse;
    for (int i = 0; i < count && i < kNumParams; ++i) {
        uint32 id = 0;
        double v = 0.0;
        state->read(&id, sizeof(id), &read);
        state->read(&v, sizeof(v), &read);
        if (findParamIndex(id) >= 0) setParamNormalized(id, v);
    }
    return kResultOk;
}

tresult LimiterPlugin::setBusArrangements(int32 numIns, int32 numOuts, BusArrangement* inputs, BusArrangement* outputs) {
    if (numIns != 1 || numOuts != 1 || !inputs || !outputs) return kResultFalse;
    if (inputs[0].numChannels < 1 || inputs[0].numChannels > 2) return kResultFalse;
    if (outputs[0].numChannels < 1 || outputs[0].numChannels > 2) return kResultFalse;
    numChannels_ = inputs[0].numChannels;
    return kResultTrue;
}

tresult LimiterPlugin::getBusArrangement(int32 direction, int32 index, BusArrangement& arr) {
    if ((direction != kInput && direction != kOutput) || index != 0) return kInvalidArgument;
    arr.mainChannel = 0;
    arr.numChannels = numChannels_;
    arr.type = kMain;
    return kResultOk;
}

tresult LimiterPlugin::canProcessSampleSize(int32 symbolicSampleSize) {
    return (symbolicSampleSize == kSample32 || symbolicSampleSize == kSample64) ? kResultTrue
                                                                                 : kResultFalse;
}

tresult LimiterPlugin::setupProcessing(ProcessSetup& setup) {
    sampleRate_ = setup.sampleRate;
    maxBlock_ = setup.maxSamplesPerBlock > 0 ? setup.maxSamplesPerBlock : 512;
    block_[0].assign(size_t(maxBlock_), 0.0f);
    block_[1].assign(size_t(maxBlock_), 0.0f);
    dsp_.setSampleRate(sampleRate_);
    dsp_.reset();
    return kResultOk;
}

tresult LimiterPlugin::setProcessing(bool state) {
    processing_ = state;
    return kResultOk;
}

tresult LimiterPlugin::process(ProcessData& data) {
    if (data.numSamples <= 0) return kResultOk;
    if (data.symbolicSampleSize != kSample32 && data.symbolicSampleSize != kSample64)
        return kResultFalse;
    if (data.numInputs < 1 || data.numOutputs < 1) return kInvalidArgument;
    if (data.numSamples > maxBlock_) return kInvalidArgument;
    int ch = std::min(data.inputs[0].numChannels, data.outputs[0].numChannels);
    if (ch > 2) ch = 2;
    if (ch < 1) return kResultOk;
    const bool is64 = data.symbolicSampleSize == kSample64;

    struct Event { int32 offset; uint32 id; double value; };
    std::vector<Event> events;
    if (data.inputParameterChanges) {
        const int32 count = data.inputParameterChanges->getParameterCount();
        for (int32 i = 0; i < count; ++i) {
            IParamValueQueue* q = data.inputParameterChanges->getParameterData(i);
            if (!q) continue;
            const int32 pc = q->getPointCount();
            for (int32 p = 0; p < pc; ++p) {
                int32 offset = 0;
                double value = 0.0;
                if (q->getPoint(p, offset, value) == kResultOk) {
                    if (offset < 0) offset = 0;
                    if (offset > data.numSamples) offset = data.numSamples;
                    events.push_back({offset, q->getParameterId(), value});
                }
            }
        }
    }
    std::sort(events.begin(), events.end(),
              [](const Event& a, const Event& b) { return a.offset < b.offset; });

    auto processSegment = [&](int from, int to) {
        if (to <= from) return;
        const int len = to - from;
        for (int c = 0; c < ch; ++c) {
            if (is64) {
                const double* inD = data.inputs[0].channelBuffers64[c] + from;
                for (int i = 0; i < len; ++i) block_[size_t(c)][size_t(i)] = float(inD[i]);
            } else {
                std::memcpy(block_[size_t(c)].data(), data.inputs[0].channelBuffers32[c] + from,
                            sizeof(float) * size_t(len));
            }
        }
        const bool bypass = normalized_[kParamBypass] >= 0.5;
        if (!bypass) {
            float* dspBuffers[2] = { block_[0].data(), block_[1].data() };
            dsp_.process(dspBuffers, ch, len);
        }
        for (int c = 0; c < ch; ++c) {
            if (is64) {
                double* outD = data.outputs[0].channelBuffers64[c] + from;
                for (int i = 0; i < len; ++i) outD[i] = double(block_[size_t(c)][size_t(i)]);
            } else {
                std::memcpy(data.outputs[0].channelBuffers32[c] + from, block_[size_t(c)].data(),
                            sizeof(float) * size_t(len));
            }
        }
        float* seg[2] = { block_[0].data(), block_[1].data() };
        noteWaveform(seg, ch, len);
    };

    int pos = 0;
    for (const Event& ev : events) {
        if (ev.offset > pos) processSegment(pos, ev.offset);
        setParamNormalized(ev.id, ev.value);
        pos = ev.offset;
    }
    processSegment(pos, data.numSamples);

    grHist_[grWrite_] = dsp_.gainReductionDb();
    grWrite_ = (grWrite_ + 1) % kGrCap;
    if (grCount_ < kGrCap) ++grCount_;
    data.outputs[0].silenceFlags = 0;
    return kResultOk;
}

void LimiterPlugin::noteWaveform(const float* const* buffers, int numChannels, int numSamples) {
    for (int n = 0; n < numSamples; ++n) {
        float m = 0.0f;
        for (int c = 0; c < numChannels; ++c) {
            const float a = std::fabs(buffers[c][n]);
            if (a > m) m = a;
        }
        wave_[waveWrite_] = m;
        waveWrite_ = (waveWrite_ + 1) % kWaveCap;
        if (waveCount_ < kWaveCap) ++waveCount_;
    }
}

void LimiterPlugin::resetDsp() {
    dsp_.reset();
    waveWrite_ = 0;
    waveCount_ = 0;
    grWrite_ = 0;
    grCount_ = 0;
    std::fill(std::begin(wave_), std::end(wave_), 0.0f);
    std::fill(std::begin(grHist_), std::end(grHist_), 0.0f);
}

float LimiterPlugin::currentGainReductionDb() const { return dsp_.gainReductionDb(); }

int LimiterPlugin::waveHistoryCount() const { return waveCount_; }

void LimiterPlugin::copyWaveHistory(float* dst, int maxCount) const {
    const int n = std::min(maxCount, waveCount_);
    const int start = (waveWrite_ - n + kWaveCap) % kWaveCap;
    for (int i = 0; i < n; ++i) dst[i] = wave_[(start + i) % kWaveCap];
}

float LimiterPlugin::grHistoryAt(int indexBack) const {
    if (grCount_ == 0) return 0.0f;
    if (indexBack < 0) indexBack = 0;
    if (indexBack >= grCount_) indexBack = grCount_ - 1;
    return grHist_[(grWrite_ - 1 - indexBack + kGrCap) % kGrCap];
}

int LimiterPlugin::grHistoryCount() const { return grCount_; }

uint32 LimiterPlugin::getLatencySamples() {
    return uint32(std::max(0, dsp_.lookaheadSamples()));
}

tresult LimiterPlugin::setComponentState(IStream*) { return kResultOk; }

int32 LimiterPlugin::getParameterCount() { return kNumParams; }

tresult LimiterPlugin::getParameterInfo(int32 paramIndex, ParameterInfo& info) {
    if (paramIndex < 0 || paramIndex >= kNumParams) return kInvalidArgument;
    const ParamDef& d = kParamDefs[paramIndex];
    info.id = d.id;
    setTCharString(info.title, 128, d.title);
    setTCharString(info.shortTitle, 128, d.shortTitle);
    setTCharString(info.units, 128, d.units);
    info.stepCount = d.discrete ? (d.stepCount - 1) : 0;
    info.defaultNormalizedValue = plainToNormalized(paramIndex, d.defaultValue);
    info.unitID = kRootUnitId;
    info.flags = kCanAutomate | (d.discrete ? kIsList : 0);
    return kResultOk;
}

tresult LimiterPlugin::getParamStringByValue(ParamID id, double valueNormalized, TChar* string) {
    const int idx = findParamIndex(id);
    if (idx < 0 || !string) return kInvalidArgument;
    const ParamDef& d = kParamDefs[idx];
    char buf[64];
    if (d.discrete) {
        const double plain = normalizedToPlain(idx, valueNormalized);
        if (id == kParamTruePeak)
            std::snprintf(buf, sizeof buf, "%s", plain >= 0.5 ? "On" : "Off");
        else if (id == kParamLink)
            std::snprintf(buf, sizeof buf, "%s", plain >= 0.5 ? "Linked" : "Off");
        else
            std::snprintf(buf, sizeof buf, "%.0f", plain);
    } else {
        std::snprintf(buf, sizeof buf, "%.2f %s", normalizedToPlain(idx, valueNormalized), d.units);
    }
    setTCharString(string, 128, buf);
    return kResultOk;
}

tresult LimiterPlugin::getParamValueByString(ParamID id, const TChar* string, double& valueNormalized) {
    const int idx = findParamIndex(id);
    if (idx < 0 || !string) return kInvalidArgument;
    char buf[64];
    size_t i = 0;
    for (; string[i] && i + 1 < sizeof buf; ++i) buf[i] = char(string[i]);
    buf[i] = 0;
    char* end = nullptr;
    const double plain = std::strtod(buf, &end);
    if (end == buf) return kResultFalse;
    valueNormalized = plainToNormalized(idx, plain);
    return kResultOk;
}

double LimiterPlugin::normalizedParamToPlain(ParamID id, double valueNormalized) {
    const int idx = findParamIndex(id);
    return idx < 0 ? 0.0 : normalizedToPlain(idx, valueNormalized);
}

double LimiterPlugin::plainParamToNormalized(ParamID id, double plainValue) {
    const int idx = findParamIndex(id);
    return idx < 0 ? 0.0 : plainToNormalized(idx, plainValue);
}

tresult LimiterPlugin::setParamNormalized(ParamID id, double value) {
    const int idx = findParamIndex(id);
    if (idx < 0) return kInvalidArgument;
    if (value < 0.0) value = 0.0;
    if (value > 1.0) value = 1.0;
    normalized_[idx] = value;
    applyParameters();
    return kResultOk;
}

double LimiterPlugin::getParamNormalized(ParamID id) {
    const int idx = findParamIndex(id);
    return idx < 0 ? 0.0 : normalized_[idx];
}

IPlugView* LimiterPlugin::createView(const char* name) {
    if (!name || std::strcmp(name, "editor") != 0) return nullptr;
    return new EditorView(this);
}

void LimiterPlugin::applyParameters() {
    dspParams_.ceilingDb    = float(normalizedToPlain(kParamCeiling, normalized_[kParamCeiling]));
    dspParams_.lookaheadMs  = float(normalizedToPlain(kParamLookahead, normalized_[kParamLookahead]));
    dspParams_.attackMs     = float(normalizedToPlain(kParamAttack, normalized_[kParamAttack]));
    dspParams_.releaseMs    = float(normalizedToPlain(kParamRelease, normalized_[kParamRelease]));
    dspParams_.truePeak     = normalizedToPlain(kParamTruePeak, normalized_[kParamTruePeak]) >= 0.5;
    dspParams_.link         = normalizedToPlain(kParamLink, normalized_[kParamLink]) >= 0.5
                                  ? LinkMode::Max : LinkMode::Off;
    dspParams_.outputGainDb = float(normalizedToPlain(kParamOutputGain, normalized_[kParamOutputGain]));
    dsp_.setParameters(dspParams_);
}

tresult LimiterFactory::queryInterface(const FUID& iid, void** obj) {
    if (!obj) return kInvalidArgument;
    if (iid == kIUnknownIID || iid == kIPluginFactoryIID) {
        *obj = this;
        addRef();
        return kResultOk;
    }
    *obj = nullptr;
    return kNoInterface;
}

uint32 LimiterFactory::addRef() { return ++refCount_; }

uint32 LimiterFactory::release() { return --refCount_; }

tresult LimiterFactory::getFactoryInfo(PFactoryInfo* info) {
    if (!info) return kInvalidArgument;
    copyAscii(info->vendor, sizeof info->vendor, "Vibe");
    copyAscii(info->url, sizeof info->url, "");
    copyAscii(info->email, sizeof info->email, "");
    info->flags = 0;
    return kResultOk;
}

int32 LimiterFactory::countClasses() { return 1; }

tresult LimiterFactory::getClassInfo(int32 index, PClassInfo* info) {
    if (index != 0 || !info) return kInvalidArgument;
    info->cid = kLimiterClassID;
    info->cardinality = kManyInstances;
    copyAscii(info->category, sizeof info->category, "Audio Module Class");
    copyAscii(info->name, sizeof info->name, "Limiter");
    return kResultOk;
}

tresult LimiterFactory::createInstance(const FUID& cid, const FUID& iid, void** obj) {
    if (!obj) return kInvalidArgument;
    *obj = nullptr;
    if (cid != kLimiterClassID) return kResultFalse;
    LimiterPlugin* p = new LimiterPlugin();
    const tresult r = p->queryInterface(iid, obj);
    p->release();
    return r;
}

}}
