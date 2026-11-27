#pragma once

#include <cmath>
#include <cstdint>
#include <deque>
#include <vector>

namespace limiter {

enum class LinkMode : int { Off = 0, Max = 1 };

struct Parameters {
    float ceilingDb    = -1.0f;
    float lookaheadMs  = 5.0f;
    float attackMs     = 1.0f;
    float releaseMs    = 150.0f;
    bool  truePeak     = true;
    LinkMode link      = LinkMode::Max;
    float outputGainDb = 0.0f;
};

class LimiterDsp {
public:
    static constexpr int kOversampling = 8;
    static constexpr int kFirTaps      = kOversampling * 8 + 1;
    static constexpr int kHistory      = (kFirTaps + kOversampling - 1) / kOversampling;

    LimiterDsp();

    void setSampleRate(double sampleRate);
    void setParameters(const Parameters& params);
    const Parameters& parameters() const { return params_; }

    void reset();
    void process(float* const* buffers, int numChannels, int numSamples);

    int   lookaheadSamples() const { return lookahead_; }
    float gainReductionDb()  const { return grDb_; }
    float outputPeakDb() const {
        return outPeakLin_ > 1e-12f ? 20.0f * std::log10f(outPeakLin_) : -240.0f;
    }

    static void designTruePeakFilter(float* h, int taps, int oversampling);

private:
    float symmetricTruePeak(int channel) const;
    void  recomputeCoeffs();
    void  reallocate();
    void  snapSmoothed();

    Parameters params_;
    double sampleRate_ = 44100.0;
    int    lookahead_  = 1;
    double paramCoef_    = 0.0;
    double attackSlope_  = 0.0;
    double releaseCoef_  = 0.0;
    double ceilSmoothDb_ = -1.0;
    double outSmoothDb_  = 0.0;
    std::vector<float> delay_[2];
    int delayIndex_ = 0;
    std::vector<float> tpHistory_[2];
    int samplesSeen_[2] = {0, 0};
    float fir_[kFirTaps] = {};
    std::deque<std::pair<int64_t, double>> window_[2];
    int64_t windowCounter_[2] = {0, 0};
    double gr_[2] = {0.0, 0.0};
    float grDb_ = 0.0f;
    float outPeakLin_ = 0.0f;
};

}
