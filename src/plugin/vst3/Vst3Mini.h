#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace vst3mini {

using int8   = int8_t;
using uint8  = uint8_t;
using int16  = int16_t;
using uint16 = uint16_t;
using int32  = int32_t;
using uint32 = uint32_t;
using int64  = int64_t;
using uint64 = uint64_t;
using tresult = int32;
using TChar   = char16_t;
using ParamID = uint32;
using UnitID  = int32;

enum : tresult {
    kResultOk         = 0,
    kResultFalse      = 1,
    kInvalidArgument  = 2,
    kNotImplemented   = 3,
    kInternalError    = 4,
    kNoInterface      = 5
};

constexpr tresult kResultTrue = tresult(0x40000000);

constexpr int32 fourcc(char a, char b, char c, char d) {
    return int32(uint32(uint8(a)) | (uint32(uint8(b)) << 8) | (uint32(uint8(c)) << 16) | (uint32(uint8(d)) << 24));
}

inline constexpr int32 kAudio = fourcc('A', 'u', 'd', 'o');
inline constexpr int32 kEvent = fourcc('E', 'v', 'n', 't');

enum : int32 { kInput = 0, kOutput = 1 };
enum : int32 { kMain = 0, kAux = 1 };
enum : int32 { kSample32 = 0, kSample64 = 1 };
enum : int32 { kRealtime = 0, kPrefetch = 1, kOffline = 2 };

struct FUID {
    uint8 data[16];
    bool operator==(const FUID& o) const { return std::memcmp(data, o.data, 16) == 0; }
    bool operator!=(const FUID& o) const { return !(*this == o); }
};

FUID fuidFromString(const char* str);

extern const FUID kIUnknownIID;
extern const FUID kIPluginBaseIID;
extern const FUID kIComponentIID;
extern const FUID kIAudioProcessorIID;
extern const FUID kIEditControllerIID;
extern const FUID kIPlugViewIID;
extern const FUID kIPluginFactoryIID;

class IUnknown {
public:
    virtual tresult queryInterface(const FUID& iid, void** obj) = 0;
    virtual uint32 addRef() = 0;
    virtual uint32 release() = 0;
    virtual ~IUnknown() {}
};

enum : int32 { kIBSeekSet = 0, kIBSeekCur = 1, kIBSeekEnd = 2 };

class IStream : public IUnknown {
public:
    virtual tresult read(void* buffer, int32 numBytes, int32* numBytesRead) = 0;
    virtual tresult write(const void* buffer, int32 numBytes, int32* numBytesWritten) = 0;
    virtual tresult seek(int64 pos, int32 mode, int64* result) = 0;
    virtual tresult tell(int64* pos) = 0;
};

class IParamValueQueue : public IUnknown {
public:
    virtual ParamID getParameterId() = 0;
    virtual int32 getPointCount() = 0;
    virtual tresult getPoint(int32 index, int32& sampleOffset, double& value) = 0;
    virtual tresult addPoint(int32 sampleOffset, double value, int32& index) = 0;
};

class IParameterChanges : public IUnknown {
public:
    virtual int32 getParameterCount() = 0;
    virtual IParamValueQueue* getParameterData(int32 index) = 0;
    virtual IParamValueQueue* addParameterData(const ParamID& id, int32& index) = 0;
};

class IEventList : public IUnknown {};
class IProcessContext : public IUnknown {};
class IPlugFrame : public IUnknown {};

struct ViewRect {
    int32 left = 0, top = 0, right = 0, bottom = 0;
    int32 getWidth() const { return right - left; }
    int32 getHeight() const { return bottom - top; }
};

enum : uint32 { kDefaultActive = 1 };

struct BusInfo {
    int32 mediaType = 0;
    int32 direction = 0;
    int32 channelCount = 0;
    int32 busType = 0;
    uint32 flags = 0;
    TChar name[128] = {};
};

struct RoutingInfo {
    int32 mediaType = 0;
    int32 busIndex = 0;
    int32 channel = 0;
};

struct BusArrangement {
    int32 mainChannel = 0;
    int32 numChannels = 0;
    int32 type = 0;
};

struct AudioBusBuffers {
    int32 numChannels = 0;
    uint64 silenceFlags = 0;
    union {
        float** channelBuffers32;
        double** channelBuffers64;
    };
};

struct ProcessSetup {
    int32 processMode = 0;
    int32 symbolicSampleSize = 0;
    int32 maxSamplesPerBlock = 0;
    double sampleRate = 0.0;
};

struct ProcessData {
    int32 processMode = 0;
    int32 symbolicSampleSize = 0;
    int32 numSamples = 0;
    int32 numInputs = 0;
    int32 numOutputs = 0;
    AudioBusBuffers* inputs = nullptr;
    AudioBusBuffers* outputs = nullptr;
    IEventList* inputEvents = nullptr;
    IEventList* outputEvents = nullptr;
    IParameterChanges* inputParameterChanges = nullptr;
    IParameterChanges* outputParameterChanges = nullptr;
    IProcessContext* processContext = nullptr;
};

enum : UnitID { kRootUnitId = 0 };

enum : int32 {
    kCanAutomate = 1 << 1,
    kIsReadOnly  = 1 << 2,
    kIsList      = 1 << 3
};

struct ParameterInfo {
    ParamID id = 0;
    TChar title[128] = {};
    TChar shortTitle[128] = {};
    TChar units[128] = {};
    int32 stepCount = 0;
    double defaultNormalizedValue = 0.0;
    UnitID unitID = 0;
    int32 flags = 0;
};

class IPluginBase : public IUnknown {
public:
    virtual tresult initialize(IUnknown* context) = 0;
    virtual tresult terminate() = 0;
};

class IComponent : public IPluginBase {
public:
    virtual tresult getControllerClassId(FUID& classId) = 0;
    virtual tresult setIoMode(int32 mode) = 0;
    virtual int32 getBusCount(int32 mediaType, int32 direction) = 0;
    virtual tresult getBusInfo(int32 mediaType, int32 direction, int32 index, BusInfo& bus) = 0;
    virtual tresult getRoutingInfo(RoutingInfo& inInfo, RoutingInfo& outInfo) = 0;
    virtual tresult activateBus(int32 mediaType, int32 direction, int32 index, bool state) = 0;
    virtual tresult setActive(bool state) = 0;
    virtual tresult setState(IStream* state) = 0;
    virtual tresult getState(IStream* state) = 0;
};

class IAudioProcessor : public IUnknown {
public:
    virtual tresult setBusArrangements(int32 numIns, int32 numOuts, BusArrangement* inputs, BusArrangement* outputs) = 0;
    virtual tresult getBusArrangement(int32 direction, int32 index, BusArrangement& arr) = 0;
    virtual tresult canProcessSampleSize(int32 symbolicSampleSize) = 0;
    virtual tresult setupProcessing(ProcessSetup& setup) = 0;
    virtual tresult setProcessing(bool state) = 0;
    virtual tresult process(ProcessData& data) = 0;
    virtual uint32 getLatencySamples() = 0;
};

class IPlugView : public IUnknown {
public:
    virtual tresult isPlatformTypeSupported(const char* type) = 0;
    virtual tresult attached(void* parent, const char* type) = 0;
    virtual tresult removed() = 0;
    virtual tresult onWheel(float distance) = 0;
    virtual tresult onKeyDown(TChar key, int16 keyMsg, int16 modifiers) = 0;
    virtual tresult onKeyUp(TChar key, int16 keyMsg, int16 modifiers) = 0;
    virtual tresult getSize(ViewRect* size) = 0;
    virtual tresult onSize(ViewRect* newSize) = 0;
    virtual tresult onFocus(bool state) = 0;
    virtual tresult setFrame(IPlugFrame* frame) = 0;
    virtual tresult canResize() = 0;
    virtual tresult checkSizeConstraint(ViewRect* rect) = 0;
};

class IEditController : public IPluginBase {
public:
    virtual tresult setComponentState(IStream* state) = 0;
    virtual tresult setState(IStream* state) = 0;
    virtual tresult getState(IStream* state) = 0;
    virtual int32 getParameterCount() = 0;
    virtual tresult getParameterInfo(int32 paramIndex, ParameterInfo& info) = 0;
    virtual tresult getParamStringByValue(ParamID id, double valueNormalized, TChar* string) = 0;
    virtual tresult getParamValueByString(ParamID id, const TChar* string, double& valueNormalized) = 0;
    virtual double normalizedParamToPlain(ParamID id, double valueNormalized) = 0;
    virtual double plainParamToNormalized(ParamID id, double plainValue) = 0;
    virtual tresult setParamNormalized(ParamID id, double value) = 0;
    virtual double getParamNormalized(ParamID id) = 0;
    virtual IPlugView* createView(const char* name) = 0;
};

enum : int32 { kManyInstances = 0x7FFFFFFF };

struct PFactoryInfo {
    char vendor[64] = {};
    char url[256] = {};
    char email[128] = {};
    int32 flags = 0;
};

struct PClassInfo {
    FUID cid = {};
    int32 cardinality = 0;
    char category[32] = {};
    char name[64] = {};
};

class IPluginFactory : public IUnknown {
public:
    virtual tresult getFactoryInfo(PFactoryInfo* info) = 0;
    virtual int32 countClasses() = 0;
    virtual tresult getClassInfo(int32 index, PClassInfo* info) = 0;
    virtual tresult createInstance(const FUID& cid, const FUID& iid, void** obj) = 0;
};

void setTCharString(TChar* dst, size_t dstCount, const char* src);

}
