#include "Vst3Mini.h"

namespace vst3mini {

namespace {

int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

}

FUID fuidFromString(const char* str) {
    uint8 logical[16] = {};
    int n = 0;
    for (const char* p = str; *p && n < 16; ++p) {
        if (*p == '-') continue;
        const int hi = hexVal(*p);
        ++p;
        if (!*p) break;
        logical[n++] = uint8((hi << 4) | hexVal(*p));
    }
    const uint32 d1 = (uint32(logical[0]) << 24) | (uint32(logical[1]) << 16)
                    | (uint32(logical[2]) << 8) | uint32(logical[3]);
    const uint16 d2 = uint16((logical[4] << 8) | logical[5]);
    const uint16 d3 = uint16((logical[6] << 8) | logical[7]);
    FUID f{};
    std::memcpy(&f.data[0], &d1, 4);
    std::memcpy(&f.data[4], &d2, 2);
    std::memcpy(&f.data[6], &d3, 2);
    std::memcpy(&f.data[8], &logical[8], 8);
    return f;
}

const FUID kIUnknownIID        = fuidFromString("00000000-0000-0000-C000-000000000046");
const FUID kIPluginBaseIID     = fuidFromString("22888DDB-156E-45AE-8358-B34808190625");
const FUID kIComponentIID      = fuidFromString("E831FF31-F2D5-44301-928E-BBEE25697802");
const FUID kIAudioProcessorIID = fuidFromString("42043F99-B7DA-453C-A569-E79D9AAEC33D");
const FUID kIEditControllerIID = fuidFromString("DCD7BBE3-7742-448D-A874-AACC979C759E");
const FUID kIPlugViewIID       = fuidFromString("5BC32507-D060-49EA-A615-1F522AE86C87");
const FUID kIPluginFactoryIID  = fuidFromString("7A4D811C-5211-4A1F-AED9-D2EE0B43BF9F");

void setTCharString(TChar* dst, size_t dstCount, const char* src) {
    if (!dst || dstCount == 0) return;
    size_t i = 0;
    for (; src[i] && i + 1 < dstCount; ++i) dst[i] = TChar(uint8(src[i]));
    for (; i < dstCount; ++i) dst[i] = 0;
}

}
