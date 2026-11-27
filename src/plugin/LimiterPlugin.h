#pragma once

#include <atomic>
#include <vector>

#include "../dsp/LimiterDsp.h"
#include "Parameters.h"
#include "vst3/Vst3Mini.h"

namespace limiter { namespace plugin {

extern const vst3mini::FUID kLimiterClassID;

class LimiterPlugin : public vst3mini::IComponent,
                      public vst3mini::IAudioProcessor,
                      public vst3mini::IEditController {
public:
    LimiterPlugin();
    virtual ~LimiterPlugin();

    vst3mini::tresult queryInterface(const vst3mini::FUID& iid, void** obj) override;
    vst3mini::uint32 addRef() override;
    vst3mini::uint32 release() override;

    vst3mini::tresult initialize(vst3mini::IUnknown* context) override;
    vst3mini::tresult terminate() override;

    vst3mini::tresult getControllerClassId(vst3mini::FUID& classId) override;
    vst3mini::tresult setIoMode(vst3mini::int32 mode) override;
    vst3mini::int32 getBusCount(vst3mini::int32 mediaType, vst3mini::int32 direction) override;
    vst3mini::tresult getBusInfo(vst3mini::int32 mediaType, vst3mini::int32 direction,
                                 vst3mini::int32 index, vst3mini::BusInfo& bus) override;
    vst3mini::tresult getRoutingInfo(vst3mini::RoutingInfo& inInfo, vst3mini::RoutingInfo& outInfo) override;
    vst3mini::tresult activateBus(vst3mini::int32 mediaType, vst3mini::int32 direction,
                                  vst3mini::int32 index, bool state) override;
    vst3mini::tresult setActive(bool state) override;
    vst3mini::tresult setState(vst3mini::IStream* state) override;
    vst3mini::tresult getState(vst3mini::IStream* state) override;

    vst3mini::tresult setBusArrangements(vst3mini::int32 numIns, vst3mini::int32 numOuts,
                                         vst3mini::BusArrangement* inputs,
                                         vst3mini::BusArrangement* outputs) override;
    vst3mini::tresult getBusArrangement(vst3mini::int32 direction, vst3mini::int32 index,
                                        vst3mini::BusArrangement& arr) override;
    vst3mini::tresult canProcessSampleSize(vst3mini::int32 symbolicSampleSize) override;
    vst3mini::tresult setupProcessing(vst3mini::ProcessSetup& setup) override;
    vst3mini::tresult setProcessing(bool state) override;
    vst3mini::tresult process(vst3mini::ProcessData& data) override;
    vst3mini::uint32 getLatencySamples() override;

    vst3mini::tresult setComponentState(vst3mini::IStream* state) override;
    vst3mini::int32 getParameterCount() override;
    vst3mini::tresult getParameterInfo(vst3mini::int32 paramIndex, vst3mini::ParameterInfo& info) override;
    vst3mini::tresult getParamStringByValue(vst3mini::ParamID id, double valueNormalized,
                                            vst3mini::TChar* string) override;
    vst3mini::tresult getParamValueByString(vst3mini::ParamID id, const vst3mini::TChar* string,
                                            double& valueNormalized) override;
    double normalizedParamToPlain(vst3mini::ParamID id, double valueNormalized) override;
    double plainParamToNormalized(vst3mini::ParamID id, double plainValue) override;
    vst3mini::tresult setParamNormalized(vst3mini::ParamID id, double value) override;
    double getParamNormalized(vst3mini::ParamID id) override;
    vst3mini::IPlugView* createView(const char* name) override;

    void  resetDsp();
    float currentGainReductionDb() const;
    int   waveHistoryCount() const;
    void  copyWaveHistory(float* dst, int maxCount) const;
    float grHistoryAt(int indexBack) const;
    int   grHistoryCount() const;

private:
    void applyParameters();
    void noteWaveform(const float* const* buffers, int numChannels, int numSamples);

    static constexpr int kWaveCap = 4096;
    static constexpr int kGrCap = 256;

    std::atomic<vst3mini::uint32> refCount_{1};
    bool active_ = false;
    bool processing_ = false;
    double sampleRate_ = 44100.0;
    vst3mini::int32 maxBlock_ = 512;
    vst3mini::int32 numChannels_ = 2;
    double normalized_[kNumParams] = {};
    Parameters dspParams_;
    LimiterDsp dsp_;
    std::vector<float> block_[2];
    float wave_[kWaveCap] = {};
    int waveWrite_ = 0;
    int waveCount_ = 0;
    float grHist_[kGrCap] = {};
    int grWrite_ = 0;
    int grCount_ = 0;
};

class LimiterFactory : public vst3mini::IPluginFactory {
public:
    vst3mini::tresult queryInterface(const vst3mini::FUID& iid, void** obj) override;
    vst3mini::uint32 addRef() override;
    vst3mini::uint32 release() override;
    vst3mini::tresult getFactoryInfo(vst3mini::PFactoryInfo* info) override;
    vst3mini::int32 countClasses() override;
    vst3mini::tresult getClassInfo(vst3mini::int32 index, vst3mini::PClassInfo* info) override;
    vst3mini::tresult createInstance(const vst3mini::FUID& cid, const vst3mini::FUID& iid, void** obj) override;

private:
    vst3mini::uint32 refCount_ = 1;
};

vst3mini::IPluginFactory* getLimiterPluginFactory();

}}
