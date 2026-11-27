#include "Parameters.h"

#include <cmath>

namespace limiter { namespace plugin {

const ParamDef kParamDefs[kNumParams] = {
    { kParamCeiling,    "Ceiling",     "Ceil", "dB", -24.0,   0.0,  -1.0, false, 0 },
    { kParamLookahead,  "Lookahead",   "Look", "ms",   0.1,  10.0,   5.0, false, 0 },
    { kParamAttack,     "Attack",      "Att",  "ms",   0.05, 10.0,   1.0, false, 0 },
    { kParamRelease,    "Release",     "Rel",  "ms",  10.0, 1000.0, 150.0, false, 0 },
    { kParamTruePeak,   "True Peak",   "TP",   "",     0.0,   1.0,   1.0, true,  2 },
    { kParamLink,       "Stereo Link", "Link", "",     0.0,   1.0,   1.0, true,  2 },
    { kParamOutputGain, "Output Gain", "Out",  "dB", -24.0,  24.0,   0.0, false, 0 },
    { kParamBypass,     "Bypass",      "Byp",  "",     0.0,   1.0,   0.0, true,  2 },
};

int findParamIndex(uint32_t id) {
    for (int i = 0; i < kNumParams; ++i)
        if (kParamDefs[i].id == id) return i;
    return -1;
}

double normalizedToPlain(int index, double normalized) {
    if (index < 0 || index >= kNumParams) return 0.0;
    const ParamDef& d = kParamDefs[index];
    if (normalized < 0.0) normalized = 0.0;
    if (normalized > 1.0) normalized = 1.0;
    if (d.discrete) return std::lround(normalized * (d.stepCount - 1));
    return d.minValue + normalized * (d.maxValue - d.minValue);
}

double plainToNormalized(int index, double plain) {
    if (index < 0 || index >= kNumParams) return 0.0;
    const ParamDef& d = kParamDefs[index];
    if (d.discrete) {
        if (d.stepCount <= 1) return 0.0;
        double n = plain / (d.stepCount - 1);
        if (n < 0.0) n = 0.0;
        if (n > 1.0) n = 1.0;
        return n;
    }
    if (d.maxValue <= d.minValue) return 0.0;
    double n = (plain - d.minValue) / (d.maxValue - d.minValue);
    if (n < 0.0) n = 0.0;
    if (n > 1.0) n = 1.0;
    return n;
}

}}
