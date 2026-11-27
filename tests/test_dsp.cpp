#include "TestFramework.h"

#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

#include "dsp/LimiterDsp.h"

using namespace limiter;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kFs = 44100.0;

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

std::vector<float> makeSine(int n, double fs, double freq, double amp) {
    std::vector<float> v(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        v[size_t(i)] = float(amp * std::sin(2.0 * kPi * freq * i / fs));
    return v;
}

Parameters baseParams() {
    Parameters p;
    p.ceilingDb = -6.0f;
    p.lookaheadMs = 5.0f;
    p.attackMs = 1.0f;
    p.releaseMs = 100.0f;
    p.truePeak = false;
    p.link = LinkMode::Max;
    p.outputGainDb = 0.0f;
    return p;
}

void configure(LimiterDsp& dsp, const Parameters& p, double fs = kFs) {
    dsp.setSampleRate(fs);
    dsp.setParameters(p);
    dsp.reset();
}

void runDsp(LimiterDsp& dsp, std::vector<float>& l, std::vector<float>& r, int blockSize) {
    const int n = int(l.size());
    for (int off = 0; off < n; off += blockSize) {
        const int count = std::min(blockSize, n - off);
        float* bufs[2] = { l.data() + off, r.data() + off };
        dsp.process(bufs, 2, count);
    }
}

void testPassthroughAndLatency() {
    LimiterDsp dsp;
    Parameters p = baseParams();
    p.ceilingDb = 0.0f;
    configure(dsp, p);
    const int n = 2048;
    std::vector<float> l(n, 0.0f), r(n, 0.0f);
    l[3] = 0.5f;
    r[3] = 0.5f;
    runDsp(dsp, l, r, n);
    const int L = dsp.lookaheadSamples();
    CHECK(L > 0);
    CHECK_NEAR(l[3 + L], 0.5, 1e-6);
    CHECK_NEAR(peakOf(l), 0.5, 1e-6);
    CHECK(peakOfRange(l, 0, 3 + L) < 1e-6);
    CHECK_NEAR(dsp.gainReductionDb(), 0.0, 1e-6);
}

void testSineLimiting() {
    LimiterDsp dsp;
    configure(dsp, baseParams());
    const int n = 44100;
    std::vector<float> l = makeSine(n, kFs, 1000.0, 1.0);
    std::vector<float> r = l;
    runDsp(dsp, l, r, 512);
    const double target = std::pow(10.0, -6.0 / 20.0);
    const double peak = peakOf(l);
    CHECK(peak <= target + 0.02);
    CHECK(peak > target - 0.02);
    CHECK(dsp.gainReductionDb() > 5.9f);
    CHECK(dsp.gainReductionDb() < 6.1f);
}

void testImpulseBrickwall() {
    LimiterDsp dsp;
    configure(dsp, baseParams());
    const int n = 8192;
    std::vector<float> l(n, 0.0f), r(n, 0.0f);
    l[100] = 1.0f;
    r[100] = 1.0f;
    runDsp(dsp, l, r, 256);
    const double target = std::pow(10.0, -6.0 / 20.0);
    const double peak = peakOf(l);
    CHECK(peak <= target + 0.01);
    CHECK(peak > 0.49);
}

void testTruePeakCatchesInterSamplePeaks() {
    const int n = 8192;
    auto run = [&](bool tpOn) {
        LimiterDsp dsp;
        Parameters p = baseParams();
        p.ceilingDb = 0.0f;
        p.truePeak = tpOn;
        configure(dsp, p);
        std::vector<float> l(n, 0.0f), r(n, 0.0f);
        l[10] = 1.0f;
        l[11] = 1.0f;
        r[10] = 1.0f;
        r[11] = 1.0f;
        runDsp(dsp, l, r, 512);
        return peakOf(l);
    };
    const double withTp = run(true);
    const double withoutTp = run(false);
    CHECK(withoutTp > 0.99);
    CHECK(withTp < 0.95);
    CHECK(withTp > 0.5);
}

void testTruePeakNoFalseTriggerOnSine() {
    LimiterDsp dsp;
    Parameters p = baseParams();
    p.ceilingDb = 0.0f;
    p.truePeak = true;
    configure(dsp, p);
    const int n = 44100;
    std::vector<float> l = makeSine(n, kFs, 1000.0, 1.0);
    std::vector<float> r = l;
    runDsp(dsp, l, r, 512);
    CHECK(peakOf(l) > 0.98);
}

void testReleaseRecovery() {
    LimiterDsp dsp;
    Parameters p = baseParams();
    p.releaseMs = 100.0f;
    configure(dsp, p);
    const int n = 22050;
    std::vector<float> l = makeSine(n, kFs, 1000.0, 1.0);
    std::vector<float> r = l;
    runDsp(dsp, l, r, 512);
    CHECK(dsp.gainReductionDb() > 5.0f);
    const int sil = 44100;
    std::vector<float> zl(sil, 0.0f), zr(sil, 0.0f);
    runDsp(dsp, zl, zr, 512);
    CHECK(dsp.gainReductionDb() < 0.1f);
}

void testAutoReleaseSlowsDeepGr() {
    LimiterDsp dsp;
    Parameters p = baseParams();
    p.ceilingDb = -6.0f;
    p.releaseMs = 150.0f;
    configure(dsp, p);
    const int burst = int(0.2 * kFs);
    std::vector<float> l = makeSine(burst, kFs, 1000.0, 2.0);
    std::vector<float> r = l;
    runDsp(dsp, l, r, 512);
    CHECK(dsp.gainReductionDb() > 11.0f);
    std::vector<float> z1(int(0.25 * kFs), 0.0f), z2(int(0.25 * kFs), 0.0f);
    runDsp(dsp, z1, z2, 512);
    CHECK(dsp.gainReductionDb() > 3.0f);
    std::vector<float> z3(int(2.5 * kFs), 0.0f), z4(int(2.5 * kFs), 0.0f);
    runDsp(dsp, z3, z4, 512);
    CHECK(dsp.gainReductionDb() < 0.1f);
}

void testStereoLink() {
    auto run = [&](LinkMode link) {
        LimiterDsp dsp;
        Parameters p = baseParams();
        p.link = link;
        configure(dsp, p);
        const int n = 22050;
        std::vector<float> l = makeSine(n, kFs, 1000.0, 1.0);
        std::vector<float> r = makeSine(n, kFs, 1000.0, 0.01);
        runDsp(dsp, l, r, 512);
        return std::make_pair(peakOf(l), peakOf(r));
    };
    const auto linked = run(LinkMode::Max);
    const auto unlinked = run(LinkMode::Off);
    const double target = std::pow(10.0, -6.0 / 20.0);
    CHECK_NEAR(linked.first, target, 0.02);
    CHECK_NEAR(linked.second, 0.01 * target, 0.001);
    CHECK_NEAR(unlinked.first, target, 0.02);
    CHECK_NEAR(unlinked.second, 0.01, 0.001);
}

void testBlockSizeIndependence() {
    const int n = 3000;
    std::vector<float> a = makeSine(n, kFs, 700.0, 1.0);
    std::vector<float> b = a;
    LimiterDsp dsp1, dsp2;
    configure(dsp1, baseParams());
    configure(dsp2, baseParams());
    std::vector<float> r1 = a, r2 = a;
    runDsp(dsp1, a, r1, n);
    runDsp(dsp2, b, r2, 7);
    CHECK(std::memcmp(a.data(), b.data(), sizeof(float) * size_t(n)) == 0);
    CHECK(std::memcmp(r1.data(), r2.data(), sizeof(float) * size_t(n)) == 0);
}

void testOutputGain() {
    LimiterDsp dsp;
    Parameters p = baseParams();
    p.ceilingDb = 0.0f;
    p.outputGainDb = 6.0f;
    configure(dsp, p);
    const int n = 4096;
    std::vector<float> l = makeSine(n, kFs, 500.0, 0.1);
    std::vector<float> r = l;
    runDsp(dsp, l, r, 512);
    CHECK_NEAR(peakOf(l), 0.1 * std::pow(10.0, 6.0 / 20.0), 1e-3);
    CHECK_NEAR(dsp.gainReductionDb(), 0.0, 1e-6);
}

void testParamSmoothing() {
    LimiterDsp dsp;
    Parameters p = baseParams();
    p.ceilingDb = 0.0f;
    p.outputGainDb = 0.0f;
    configure(dsp, p);
    const int n = 256;
    std::vector<float> l(n, 0.001f), r(n, 0.001f);
    runDsp(dsp, l, r, 256);
    CHECK_NEAR(l[size_t(n - 1)], 0.001, 1e-6);
    Parameters p2 = p;
    p2.outputGainDb = 24.0f;
    dsp.setParameters(p2);
    std::vector<float> one(1, 0.001f), oneR(1, 0.001f);
    float* bufs[2] = { one.data(), oneR.data() };
    dsp.process(bufs, 2, 1);
    CHECK_NEAR(one[0], 0.001 * std::pow(10.0, 24.0 * (1.0 - std::exp(-1.0 / 441.0)) / 20.0), 2e-5);
    const int settle = 8192;
    std::vector<float> zl(settle, 0.001f), zr(settle, 0.001f);
    runDsp(dsp, zl, zr, 512);
    CHECK_NEAR(zl[size_t(settle - 1)], 0.001 * std::pow(10.0, 24.0 / 20.0), 5e-4);
}

void testSilenceAndTinyInput() {
    LimiterDsp dsp;
    configure(dsp, baseParams());
    const int n = 4096;
    std::vector<float> l(n, 0.0f), r(n, 0.0f);
    runDsp(dsp, l, r, 128);
    CHECK(peakOf(l) == 0.0);
    std::vector<float> t1(n, 1e-30f), t2(n, 1e-30f);
    runDsp(dsp, t1, t2, 128);
    for (int i = 0; i < n; ++i) {
        CHECK(std::isfinite(t1[size_t(i)]));
        CHECK(std::isfinite(t2[size_t(i)]));
    }
    CHECK(peakOf(t1) < 1e-20);
}

void testFilterDesign() {
    float h[LimiterDsp::kFirTaps];
    LimiterDsp::designTruePeakFilter(h, LimiterDsp::kFirTaps, LimiterDsp::kOversampling);
    double sum = 0.0;
    for (int i = 0; i < LimiterDsp::kFirTaps; ++i) {
        CHECK(std::isfinite(h[i]));
        sum += h[i];
    }
    CHECK_NEAR(sum, double(LimiterDsp::kOversampling), 1e-3);
}

double goertzelMag(const std::vector<float>& v, int from, int n, double fs, double freq) {
    const double w = 2.0 * kPi * freq / fs;
    const double cw = std::cos(w);
    double coeff = 2.0 * cw;
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    for (int i = 0; i < n; ++i) {
        s0 = double(v[size_t(from + i)]) + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return std::sqrt(s1 * s1 + s2 * s2 - coeff * s1 * s2) * 2.0 / n;
}

void testThd() {
    LimiterDsp dsp;
    configure(dsp, baseParams());
    const int n = 44100;
    std::vector<float> l = makeSine(n, kFs, 1000.0, 1.0);
    std::vector<float> r = l;
    runDsp(dsp, l, r, 512);
    const int from = n - 8192;
    const double fund = goertzelMag(l, from, 8192, kFs, 1000.0);
    double harmSq = 0.0;
    for (int h = 2; h <= 8; ++h)
        harmSq += std::pow(goertzelMag(l, from, 8192, kFs, 1000.0 * h), 2);
    const double thd = std::sqrt(harmSq) / fund;
    CHECK(fund > 0.4);
    CHECK(thd < 0.02);
}

void testSampleRateSweep() {
    for (double fs : {44100.0, 48000.0, 96000.0, 192000.0}) {
        LimiterDsp dsp;
        configure(dsp, baseParams(), fs);
        const int n = int(fs);
        std::vector<float> l = makeSine(n, fs, 1000.0, 1.0);
        std::vector<float> r = l;
        runDsp(dsp, l, r, 512);
        const double target = std::pow(10.0, -6.0 / 20.0);
        const double peak = peakOf(l);
        CHECK(peak <= target + 0.02);
        CHECK(peak > target - 0.03);
    }
}

}

void testDsp() {
    testPassthroughAndLatency();
    testSineLimiting();
    testImpulseBrickwall();
    testTruePeakCatchesInterSamplePeaks();
    testTruePeakNoFalseTriggerOnSine();
    testReleaseRecovery();
    testAutoReleaseSlowsDeepGr();
    testStereoLink();
    testBlockSizeIndependence();
    testOutputGain();
    testParamSmoothing();
    testSilenceAndTinyInput();
    testFilterDesign();
    testThd();
    testSampleRateSweep();
}
