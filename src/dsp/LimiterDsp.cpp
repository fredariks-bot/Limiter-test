#include "LimiterDsp.h"

#include <algorithm>
#include <cmath>

namespace limiter {

namespace {

constexpr double kPi = 3.14159265358979323846;

double sinc(double x) {
    if (x == 0.0) return 1.0;
    const double px = kPi * x;
    return std::sin(px) / px;
}

}

void LimiterDsp::designTruePeakFilter(float* h, int taps, int oversampling) {
    const int m = (taps - 1) / 2;
    const double fc = 0.5 / oversampling;
    const double a0 = 0.35875, a1 = 0.48829, a2 = 0.14128, a3 = 0.01168;
    double sum = 0.0;
    for (int n = 0; n < taps; ++n) {
        const double x = double(n) - m;
        const double w = a0
            - a1 * std::cos(2.0 * kPi * n / (taps - 1))
            + a2 * std::cos(4.0 * kPi * n / (taps - 1))
            - a3 * std::cos(6.0 * kPi * n / (taps - 1));
        const double v = 2.0 * fc * sinc(2.0 * fc * x) * w;
        h[n] = float(v);
        sum += v;
    }
    const float scale = float(double(oversampling) / sum);
    for (int n = 0; n < taps; ++n) h[n] *= scale;
}

LimiterDsp::LimiterDsp() {
    designTruePeakFilter(fir_, kFirTaps, kOversampling);
    setSampleRate(sampleRate_);
    setParameters(params_);
}

void LimiterDsp::setSampleRate(double sampleRate) {
    if (sampleRate < 1000.0) sampleRate = 1000.0;
    sampleRate_ = sampleRate;
    reallocate();
    recomputeCoeffs();
}

void LimiterDsp::setParameters(const Parameters& params) {
    const int newLookahead = std::max(1, int(std::lround(params.lookaheadMs * 0.001 * sampleRate_)));
    const bool changed = (newLookahead != lookahead_);
    params_ = params;
    if (changed) reallocate();
    recomputeCoeffs();
}

void LimiterDsp::reset() {
    for (int c = 0; c < 2; ++c) {
        std::fill(delay_[c].begin(), delay_[c].end(), 0.0f);
        std::fill(tpHistory_[c].begin(), tpHistory_[c].end(), 0.0f);
        samplesSeen_[c] = 0;
    }
    window_[0].clear();
    window_[1].clear();
    windowCounter_[0] = windowCounter_[1] = 0;
    delayIndex_ = 0;
    gr_[0] = gr_[1] = 0.0;
    grDb_ = 0.0f;
    outPeakLin_ = 0.0f;
}

void LimiterDsp::reallocate() {
    lookahead_ = std::max(1, int(std::lround(params_.lookaheadMs * 0.001 * sampleRate_)));
    for (int c = 0; c < 2; ++c) {
        delay_[c].assign(lookahead_, 0.0f);
        tpHistory_[c].assign(kHistory, 0.0f);
        samplesSeen_[c] = 0;
    }
    window_[0].clear();
    window_[1].clear();
    windowCounter_[0] = windowCounter_[1] = 0;
    delayIndex_ = 0;
    gr_[0] = gr_[1] = 0.0;
}

void LimiterDsp::recomputeCoeffs() {
    const double attackSec  = std::max(params_.attackMs * 0.001, 0.25 / sampleRate_);
    const double releaseSec = std::max(params_.releaseMs * 0.001, 0.25 / sampleRate_);
    attackCoef_  = std::exp(-1.0 / (attackSec * sampleRate_));
    releaseCoef_ = std::exp(-1.0 / (releaseSec * sampleRate_));
    outLin_      = std::pow(10.0, params_.outputGainDb / 20.0);
}

float LimiterDsp::symmetricTruePeak(int channel) const {
    const std::vector<float>& hist = tpHistory_[channel];
    constexpr int kCenter = kHistory / 2;
    float tp = std::fabs(hist[size_t(kCenter)]);
    for (int j = 0; j < kOversampling; ++j) {
        float acc = 0.0f;
        for (int m = -kCenter; m <= kCenter; ++m) {
            const int t = (kFirTaps - 1) / 2 + j + kOversampling * m;
            if (t >= 0 && t < kFirTaps) acc += fir_[t] * hist[size_t(kCenter + m)];
        }
        const float a = std::fabs(acc);
        if (a > tp) tp = a;
    }
    return tp;
}

void LimiterDsp::process(float* const* buffers, int numChannels, int numSamples) {
    if (!buffers || numSamples <= 0 || numChannels < 1) return;
    if (numChannels > 2) numChannels = 2;
    outPeakLin_ = 0.0f;
    const double ceiling = params_.ceilingDb;
    const bool tpOn = params_.truePeak;
    for (int n = 0; n < numSamples; ++n) {
        float xin[2] = {0.0f, 0.0f};
        for (int c = 0; c < numChannels; ++c) {
            const float x = buffers[c][n];
            xin[c] = x;
            std::vector<float>& hist = tpHistory_[c];
            for (int m = kHistory - 1; m > 0; --m) hist[size_t(m)] = hist[size_t(m - 1)];
            hist[0] = x;
            if (samplesSeen_[c] < kHistory) ++samplesSeen_[c];
            double peak;
            if (tpOn) {
                if (samplesSeen_[c] >= kHistory) {
                    peak = symmetricTruePeak(c);
                } else {
                    peak = 0.0;
                    for (int m = 0; m < samplesSeen_[c]; ++m)
                        peak = std::max(peak, double(std::fabs(hist[size_t(m)])));
                }
            } else {
                peak = double(std::fabs(x));
            }
            if (peak < 1e-9) peak = 1e-9;
            double grDes = 20.0 * std::log10(peak) - ceiling;
            if (grDes < 0.0) grDes = 0.0;
            ++windowCounter_[c];
            std::deque<std::pair<int64_t, double>>& win = window_[c];
            while (!win.empty() && win.back().second <= grDes) win.pop_back();
            win.push_back({windowCounter_[c], grDes});
            while (!win.empty() && win.front().first <= windowCounter_[c] - int64_t(lookahead_))
                win.pop_front();
            const double env = win.front().second;
            const double coef = (env >= gr_[c]) ? attackCoef_ : releaseCoef_;
            gr_[c] = env + (gr_[c] - env) * coef;
            if (gr_[c] < 0.0) gr_[c] = 0.0;
        }
        const bool linked = (numChannels == 2 && params_.link == LinkMode::Max);
        const double grLink = std::max(gr_[0], gr_[1]);
        double grApplied = 0.0;
        for (int c = 0; c < numChannels; ++c) {
            const double grA = linked ? grLink : gr_[c];
            const double gain = std::pow(10.0, -grA / 20.0) * outLin_;
            const float d = delay_[c][delayIndex_];
            delay_[c][delayIndex_] = xin[c];
            const float y = d * float(gain);
            buffers[c][n] = y;
            const float ay = std::fabs(y);
            if (ay > outPeakLin_) outPeakLin_ = ay;
            if (grA > grApplied) grApplied = grA;
        }
        delayIndex_ = (delayIndex_ + 1) % lookahead_;
        grDb_ = float(grApplied);
    }
}

}
