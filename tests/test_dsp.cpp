#include "TestFramework.h"

#include <cmath>
#include <cstring>
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

std::vector<float> makeSine(int n, double freq, double amp) {
    std::vector<float> v(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        v[size_t(i)] = float(amp * std::sin(2.0 * kPi * freq * i / kFs));
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
    dsp.setSampleRate(kFs);
    Parameters p = baseParams();
    p.ceilingDb = 0.0f;
    dsp.setParameters(p);
    const int n = 2048;
    std::vector<float> l(n, 0.0f), r(n, 0.0f);
    l[3] = 0.5f;
    r[3] = 0.5f;
    runDsp(dsp, l, r, n);
    const int L = dsp.lookaheadSamples();
    CHECK(L > 0);
    CHECK_NEAR(l[3 + L], 0.5, 1e-6);
    CHECK_NEAR(peakOf(l), 0.5, 1e-6);
    double prePeak = 0.0;
    for (int i = 0; i < 3 + L; ++i) prePeak = std::max(prePeak, double(std::fabs(l[i])));
    CHECK(prePeak < 1e-6);
    CHECK_NEAR(dsp.gainReductionDb(), 0.0, 1e-6);
}

void testSineLimiting() {
    LimiterDsp dsp;
    dsp.setSampleRate(kFs);
    dsp.setParameters(baseParams());
    const int n = 44100;
    std::vector<float> l = makeSine(n, 1000.0, 1.0);
    std::vector<float> r = l;
    runDsp(dsp, l, r, 512);
    const double target = std::pow(10.0, -6.0 / 20.0);
    const double peak = peakOf(l);
    CHECK(peak <= target + 0.03);
    CHECK(peak > target - 0.03);
    CHECK(dsp.gainReductionDb() > 5.5f);
    CHECK(dsp.gainReductionDb() < 6.5f);
}

void testImpulseBrickwall() {
    LimiterDsp dsp;
    dsp.setSampleRate(kFs);
    dsp.setParameters(baseParams());
    const int n = 8192;
    std::vector<float> l(n, 0.0f), r(n, 0.0f);
    l[100] = 1.0f;
    r[100] = 1.0f;
    runDsp(dsp, l, r, 256);
    const double target = std::pow(10.0, -6.0 / 20.0);
    const double peak = peakOf(l);
    CHECK(peak <= target + 0.03);
    CHECK(peak > 0.4);
}

void testTruePeakCatchesInterSamplePeaks() {
    const int n = 8192;
    auto run = [&](bool tpOn) {
        LimiterDsp dsp;
        dsp.setSampleRate(kFs);
        Parameters p = baseParams();
        p.ceilingDb = 0.0f;
        p.truePeak = tpOn;
        dsp.setParameters(p);
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
    dsp.setSampleRate(kFs);
    Parameters p = baseParams();
    p.ceilingDb = 0.0f;
    p.truePeak = true;
    dsp.setParameters(p);
    const int n = 44100;
    std::vector<float> l = makeSine(n, 1000.0, 1.0);
    std::vector<float> r = l;
    runDsp(dsp, l, r, 512);
    CHECK(peakOf(l) > 0.98);
}

void testReleaseRecovery() {
    LimiterDsp dsp;
    dsp.setSampleRate(kFs);
    Parameters p = baseParams();
    p.releaseMs = 100.0f;
    dsp.setParameters(p);
    const int n = 22050;
    std::vector<float> l = makeSine(n, 1000.0, 1.0);
    std::vector<float> r = l;
    runDsp(dsp, l, r, 512);
    CHECK(dsp.gainReductionDb() > 5.0f);
    std::vector<float> zl(n, 0.0f), zr(n, 0.0f);
    runDsp(dsp, zl, zr, 512);
    CHECK(dsp.gainReductionDb() < 0.1f);
}

void testStereoLink() {
    auto run = [&](LinkMode link) {
        LimiterDsp dsp;
        dsp.setSampleRate(kFs);
        Parameters p = baseParams();
        p.link = link;
        dsp.setParameters(p);
        const int n = 22050;
        std::vector<float> l = makeSine(n, 1000.0, 1.0);
        std::vector<float> r = makeSine(n, 1000.0, 0.01);
        runDsp(dsp, l, r, 512);
        return std::make_pair(peakOf(l), peakOf(r));
    };
    const auto linked = run(LinkMode::Max);
    const auto unlinked = run(LinkMode::Off);
    const double target = std::pow(10.0, -6.0 / 20.0);
    CHECK_NEAR(linked.first, target, 0.03);
    CHECK_NEAR(linked.second, 0.01 * target, 0.001);
    CHECK_NEAR(unlinked.first, target, 0.03);
    CHECK_NEAR(unlinked.second, 0.01, 0.001);
}

void testBlockSizeIndependence() {
    const int n = 3000;
    std::vector<float> a = makeSine(n, 700.0, 1.0);
    std::vector<float> b = a;
    LimiterDsp dsp1, dsp2;
    dsp1.setSampleRate(kFs);
    dsp2.setSampleRate(kFs);
    dsp1.setParameters(baseParams());
    dsp2.setParameters(baseParams());
    std::vector<float> r1 = a, r2 = a;
    runDsp(dsp1, a, r1, n);
    runDsp(dsp2, b, r2, 7);
    CHECK(std::memcmp(a.data(), b.data(), sizeof(float) * size_t(n)) == 0);
    CHECK(std::memcmp(r1.data(), r2.data(), sizeof(float) * size_t(n)) == 0);
}

void testOutputGain() {
    LimiterDsp dsp;
    dsp.setSampleRate(kFs);
    Parameters p = baseParams();
    p.ceilingDb = 0.0f;
    p.outputGainDb = 6.0f;
    dsp.setParameters(p);
    const int n = 4096;
    std::vector<float> l = makeSine(n, 500.0, 0.1);
    std::vector<float> r = l;
    runDsp(dsp, l, r, 512);
    CHECK_NEAR(peakOf(l), 0.1 * std::pow(10.0, 6.0 / 20.0), 1e-3);
    CHECK_NEAR(dsp.gainReductionDb(), 0.0, 1e-6);
}

void testSilenceAndTinyInput() {
    LimiterDsp dsp;
    dsp.setSampleRate(kFs);
    dsp.setParameters(baseParams());
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

}

void testDsp() {
    testPassthroughAndLatency();
    testSineLimiting();
    testImpulseBrickwall();
    testTruePeakCatchesInterSamplePeaks();
    testTruePeakNoFalseTriggerOnSine();
    testReleaseRecovery();
    testStereoLink();
    testBlockSizeIndependence();
    testOutputGain();
    testSilenceAndTinyInput();
    testFilterDesign();
}
