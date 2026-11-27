#pragma once

#include <cstdint>

namespace limiter { namespace plugin {

enum ParamId {
    kParamCeiling    = 0,
    kParamLookahead  = 1,
    kParamAttack     = 2,
    kParamRelease    = 3,
    kParamTruePeak   = 4,
    kParamLink       = 5,
    kParamOutputGain = 6,
    kParamBypass     = 7,
    kNumParams       = 8
};

struct ParamDef {
    uint32_t id;
    const char* title;
    const char* shortTitle;
    const char* units;
    double minValue;
    double maxValue;
    double defaultValue;
    bool discrete;
    int32_t stepCount;
};

extern const ParamDef kParamDefs[kNumParams];

int    findParamIndex(uint32_t id);
double normalizedToPlain(int index, double normalized);
double plainToNormalized(int index, double plain);

}}
