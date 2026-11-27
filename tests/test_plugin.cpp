#include "TestFramework.h"

#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

#include "dsp/LimiterDsp.h"
#include "plugin/LimiterPlugin.h"

using namespace vst3mini;

namespace {

constexpr double kPi = 3.14159265358979323846;

class MemStream : public IStream {
public:
    tresult queryInterface(const FUID& iid, void** obj) override {
        if (!obj) return kInvalidArgument;
        if (iid == kIUnknownIID) {
            *obj = this;
            addRef();
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 addRef() override { return ++ref_; }
    uint32 release() override { return --ref_; }
    tresult read(void* buffer, int32 numBytes, int32* numBytesRead) override {
        const int64 avail = int64(data_.size()) - int64(pos_);
        const int32 n = int32(std::min<int64>(numBytes, std::max<int64>(avail, 0)));
        if (n > 0) std::memcpy(buffer, data_.data() + pos_, size_t(n));
        pos_ += size_t(n);
        if (numBytesRead) *numBytesRead = n;
        return kResultOk;
    }
    tresult write(const void* buffer, int32 numBytes, int32* numBytesWritten) override {
        const size_t end = pos_ + size_t(numBytes);
        if (end > data_.size()) data_.resize(end);
        std::memcpy(data_.data() + pos_, buffer, size_t(numBytes));
        pos_ += size_t(numBytes);
        if (numBytesWritten) *numBytesWritten = numBytes;
        return kResultOk;
    }
    tresult seek(int64 pos, int32 mode, int64* result) override {
        int64 base = 0;
        if (mode == kIBSeekCur) base = int64(pos_);
        else if (mode == kIBSeekEnd) base = int64(data_.size());
        pos_ = size_t(std::max<int64>(0, base + pos));
        if (result) *result = int64(pos_);
        return kResultOk;
    }
    tresult tell(int64* pos) override {
        if (pos) *pos = int64(pos_);
        return kResultOk;
    }
    std::vector<uint8> data_;
    size_t pos_ = 0;
    uint32 ref_ = 1;
};

class TestValueQueue : public IParamValueQueue {
public:
    TestValueQueue(ParamID id, int32 offset, double value) : id_(id) {
        points_.push_back({offset, value});
    }
    ParamID getParameterId() override { return id_; }
    int32 getPointCount() override { return int32(points_.size()); }
    tresult getPoint(int32 index, int32& sampleOffset, double& value) override {
        if (index < 0 || index >= int32(points_.size())) return kInvalidArgument;
        sampleOffset = points_[size_t(index)].first;
        value = points_[size_t(index)].second;
        return kResultOk;
    }
    tresult addPoint(int32 sampleOffset, double value, int32& index) override {
        points_.push_back({sampleOffset, value});
        index = int32(points_.size()) - 1;
        return kResultOk;
    }
    tresult queryInterface(const FUID& iid, void** obj) override {
        if (!obj) return kInvalidArgument;
        if (iid == kIUnknownIID) {
            *obj = this;
            addRef();
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 addRef() override { return ++ref_; }
    uint32 release() override { return --ref_; }
private:
    ParamID id_;
    std::vector<std::pair<int32, double>> points_;
    uint32 ref_ = 1;
};

class TestParamChanges : public IParameterChanges {
public:
    ~TestParamChanges() {
        for (auto* q : queues_) q->release();
    }
    void add(ParamID id, int32 offset, double value) {
        queues_.push_back(new TestValueQueue(id, offset, value));
    }
    int32 getParameterCount() override { return int32(queues_.size()); }
    IParamValueQueue* getParameterData(int32 index) override {
        return (index >= 0 && index < int32(queues_.size())) ? queues_[size_t(index)] : nullptr;
    }
    IParamValueQueue* addParameterData(const ParamID& id, int32& index) override {
        queues_.push_back(new TestValueQueue(id, 0, 0.0));
        index = int32(queues_.size()) - 1;
        return queues_.back();
    }
    tresult queryInterface(const FUID& iid, void** obj) override {
        if (!obj) return kInvalidArgument;
        if (iid == kIUnknownIID) {
            *obj = this;
            addRef();
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 addRef() override { return ++ref_; }
    uint32 release() override { return --ref_; }
private:
    std::vector<TestValueQueue*> queues_;
    uint32 ref_ = 1;
};

double peakOf(const std::vector<float>& v) {
    double p = 0.0;
    for (float x : v) p = std::max(p, double(std::fabs(x)));
    return p;
}

double peakOfRange(const std::vector<float>& v, int from, int to) {
    double p = 0.0;
    for (int i = from; i < to && i < int(v.size()); ++i)
        p = std::max(p, double(std::fabs(v[size_t(i)])));
    return p;
}

}

void testPlugin() {
    using namespace limiter::plugin;

    IPluginFactory* factory = getLimiterPluginFactory();
    CHECK(factory != nullptr);
    PFactoryInfo finfo;
    CHECK(factory->getFactoryInfo(&finfo) == kResultOk);
    CHECK(factory->countClasses() == 1);
    PClassInfo cinfo;
    CHECK(factory->getClassInfo(0, &cinfo) == kResultOk);
    CHECK(std::strcmp(cinfo.category, "Audio Module Class") == 0);
    void* bad = nullptr;
    CHECK(factory->createInstance(fuidFromString("11111111-2222-3333-4444-555555555555"),
                                  kIComponentIID, &bad) == kResultFalse);

    void* obj = nullptr;
    CHECK(factory->createInstance(cinfo.cid, kIComponentIID, &obj) == kResultOk);
    CHECK(obj != nullptr);
    IComponent* comp = static_cast<IComponent*>(obj);
    IAudioProcessor* proc = nullptr;
    CHECK(comp->queryInterface(kIAudioProcessorIID, reinterpret_cast<void**>(&proc)) == kResultOk);
    IEditController* ctrl = nullptr;
    CHECK(comp->queryInterface(kIEditControllerIID, reinterpret_cast<void**>(&ctrl)) == kResultOk);
    void* unknown = nullptr;
    CHECK(comp->queryInterface(fuidFromString("11111111-2222-3333-4444-555555555555"),
                               &unknown) == kNoInterface);
    CHECK(ctrl != nullptr && proc != nullptr);

    CHECK(comp->initialize(nullptr) == kResultOk);
    CHECK(comp->getBusCount(kAudio, kInput) == 1);
    CHECK(comp->getBusCount(kAudio, kOutput) == 1);
    CHECK(comp->getBusCount(kEvent, kInput) == 0);
    BusInfo bi;
    CHECK(comp->getBusInfo(kAudio, kInput, 0, bi) == kResultOk);
    CHECK(bi.channelCount == 2);
    CHECK(bi.busType == kMain);
    FUID ctrlId;
    CHECK(comp->getControllerClassId(ctrlId) == kResultOk);
    CHECK(ctrlId == kLimiterClassID);

    BusArrangement inArr{0, 2, kMain};
    BusArrangement outArr{0, 2, kMain};
    CHECK(proc->setBusArrangements(1, 1, &inArr, &outArr) == kResultTrue);
    BusArrangement got;
    CHECK(proc->getBusArrangement(kOutput, 0, got) == kResultOk);
    CHECK(got.numChannels == 2);
    CHECK(proc->canProcessSampleSize(kSample32) == kResultTrue);
    CHECK(proc->canProcessSampleSize(kSample64) == kResultTrue);
    CHECK(proc->canProcessSampleSize(99) == kResultFalse);

    ProcessSetup setup;
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = 512;
    setup.sampleRate = 44100.0;
    CHECK(proc->setupProcessing(setup) == kResultOk);
    CHECK(ctrl->setParamNormalized(kParamCeiling, 0.75) == kResultOk);
    CHECK(comp->setActive(true) == kResultOk);

    limiter::LimiterDsp ref;
    ref.setSampleRate(44100.0);
    CHECK(proc->getLatencySamples() == uint32(ref.lookaheadSamples()));

    CHECK(ctrl->getParameterCount() == kNumParams);
    ParameterInfo pi;
    CHECK(ctrl->getParameterInfo(0, pi) == kResultOk);
    CHECK(pi.id == kParamCeiling);
    CHECK_NEAR(ctrl->normalizedParamToPlain(kParamCeiling, 1.0), 0.0, 1e-9);
    CHECK_NEAR(ctrl->normalizedParamToPlain(kParamCeiling, 0.0), -24.0, 1e-9);
    CHECK_NEAR(ctrl->plainParamToNormalized(kParamCeiling, -1.0), 23.0 / 24.0, 1e-9);
    CHECK_NEAR(ctrl->normalizedParamToPlain(kParamLookahead, 0.0), 0.1, 1e-9);
    CHECK_NEAR(ctrl->normalizedParamToPlain(kParamLookahead, 1.0), 10.0, 1e-9);
    TChar pstr[128];
    CHECK(ctrl->getParamStringByValue(kParamCeiling, 1.0, pstr) == kResultOk);
    CHECK(pstr[0] == '0');
    double parsed = 0.0;
    TChar inStr[128];
    setTCharString(inStr, 128, "-6.00 dB");
    CHECK(ctrl->getParamValueByString(kParamCeiling, inStr, parsed) == kResultOk);
    CHECK_NEAR(parsed, 18.0 / 24.0, 1e-9);

    CHECK_NEAR(ctrl->getParamNormalized(kParamCeiling), 0.75, 1e-12);
    CHECK(ctrl->setParamNormalized(999, 0.5) == kInvalidArgument);

    const int N = 512;
    std::vector<float> inL(N), inR(N), outL(N), outR(N);
    for (int i = 0; i < N; ++i) {
        const float s = float(std::sin(2.0 * kPi * 1000.0 * i / 44100.0));
        inL[size_t(i)] = s;
        inR[size_t(i)] = s;
    }
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
    data.numSamples = N;
    data.numInputs = 1;
    data.numOutputs = 1;
    data.inputs = &inBus;
    data.outputs = &outBus;

    CHECK(proc->setProcessing(true) == kResultOk);
    CHECK(proc->process(data) == kResultOk);
    const double target = std::pow(10.0, -6.0 / 20.0);
    const double outPeak = peakOf(outL);
    CHECK(outPeak <= target + 0.05);
    CHECK(outPeak > 0.45);
    CHECK(peakOf(outR) <= target + 0.05);

    TestParamChanges changes;
    changes.add(kParamCeiling, 0, 1.0);
    data.inputParameterChanges = &changes;
    for (int block = 0; block < 100; ++block) {
        for (int i = 0; i < N; ++i) {
            const float s = float(std::sin(2.0 * kPi * 1000.0 * (block * N + i) / 44100.0));
            inL[size_t(i)] = s;
            inR[size_t(i)] = s;
            outL[size_t(i)] = 0.0f;
            outR[size_t(i)] = 0.0f;
        }
        CHECK(proc->process(data) == kResultOk);
        if (block == 0) data.inputParameterChanges = nullptr;
    }
    CHECK(peakOf(outL) > 0.95);
    data.inputParameterChanges = nullptr;
    CHECK_NEAR(ctrl->getParamNormalized(kParamCeiling), 1.0, 1e-12);

    MemStream st;
    CHECK(ctrl->getState(&st) == kResultOk);
    CHECK(st.data_.size() > 0);
    CHECK(ctrl->setParamNormalized(kParamCeiling, 0.75) == kResultOk);
    int64 pos = 0;
    CHECK(st.seek(0, kIBSeekSet, &pos) == kResultOk);
    CHECK(ctrl->setState(&st) == kResultOk);
    CHECK_NEAR(ctrl->getParamNormalized(kParamCeiling), 1.0, 1e-12);

    CHECK(ctrl->setParamNormalized(kParamCeiling, 0.75) == kResultOk);
    CHECK(ctrl->setParamNormalized(kParamBypass, 1.0) == kResultOk);
    for (int i = 0; i < N; ++i) {
        const float s = float(std::sin(2.0 * kPi * 1000.0 * i / 44100.0));
        inL[size_t(i)] = s;
        inR[size_t(i)] = s;
        outL[size_t(i)] = 0.0f;
        outR[size_t(i)] = 0.0f;
    }
    CHECK(proc->process(data) == kResultOk);
    CHECK(peakOf(outL) > 0.95);
    CHECK(std::memcmp(inL.data(), outL.data(), sizeof(float) * size_t(N)) == 0);
    CHECK(ctrl->setParamNormalized(kParamBypass, 0.0) == kResultOk);

    comp->setActive(false);
    comp->setActive(true);
    TestParamChanges midChanges;
    midChanges.add(kParamBypass, 256, 1.0);
    data.inputParameterChanges = &midChanges;
    for (int i = 0; i < N; ++i) {
        const float s = float(std::sin(2.0 * kPi * 1000.0 * i / 44100.0));
        inL[size_t(i)] = s;
        inR[size_t(i)] = s;
        outL[size_t(i)] = 0.0f;
        outR[size_t(i)] = 0.0f;
    }
    CHECK(proc->process(data) == kResultOk);
    data.inputParameterChanges = nullptr;
    CHECK(peakOfRange(outL, 0, 256) <= std::pow(10.0, -6.0 / 20.0) + 0.05);
    CHECK(peakOfRange(outL, 256, N) > 0.9);
    CHECK_NEAR(ctrl->getParamNormalized(kParamBypass), 1.0, 1e-12);
    CHECK(ctrl->setParamNormalized(kParamBypass, 0.0) == kResultOk);

    IPlugView* view = ctrl->createView("editor");
    CHECK(view != nullptr);
    if (view) {
        ViewRect rect;
        CHECK(view->getSize(&rect) == kResultOk);
        CHECK(rect.getWidth() == 480);
        CHECK(rect.getHeight() == 320);
        CHECK(view->isPlatformTypeSupported("X11EmbedWindowID") == kResultTrue);
        CHECK(view->isPlatformTypeSupported("HWND") == kResultTrue);
        CHECK(view->isPlatformTypeSupported("nope") == kResultFalse);
        CHECK(view->attached(nullptr, "HWND") == kResultOk);
        CHECK(view->removed() == kResultOk);
        ViewRect constraint{0, 0, 10, 10};
        CHECK(view->checkSizeConstraint(&constraint) == kResultOk);
        CHECK(constraint.getWidth() >= 480);
        CHECK(ctrl->createView("other") == nullptr);
        view->release();
    }

    CHECK(proc->setProcessing(false) == kResultOk);
    CHECK(comp->setActive(false) == kResultOk);
    CHECK(comp->terminate() == kResultOk);
    ctrl->release();
    proc->release();
    comp->release();
}
