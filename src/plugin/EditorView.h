#pragma once

#include <atomic>

#include "vst3/Vst3Mini.h"

namespace limiter { namespace plugin {

class LimiterPlugin;

class EditorView : public vst3mini::IPlugView {
public:
    explicit EditorView(LimiterPlugin* owner);
    ~EditorView() override;

    vst3mini::tresult queryInterface(const vst3mini::FUID& iid, void** obj) override;
    vst3mini::uint32 addRef() override;
    vst3mini::uint32 release() override;

    vst3mini::tresult isPlatformTypeSupported(const char* type) override;
    vst3mini::tresult attached(void* parent, const char* type) override;
    vst3mini::tresult removed() override;
    vst3mini::tresult onWheel(float distance) override;
    vst3mini::tresult onKeyDown(vst3mini::TChar key, vst3mini::int16 keyMsg, vst3mini::int16 modifiers) override;
    vst3mini::tresult onKeyUp(vst3mini::TChar key, vst3mini::int16 keyMsg, vst3mini::int16 modifiers) override;
    vst3mini::tresult getSize(vst3mini::ViewRect* size) override;
    vst3mini::tresult onSize(vst3mini::ViewRect* newSize) override;
    vst3mini::tresult onFocus(bool state) override;
    vst3mini::tresult setFrame(vst3mini::IPlugFrame* frame) override;
    vst3mini::tresult canResize() override;
    vst3mini::tresult checkSizeConstraint(vst3mini::ViewRect* rect) override;

private:
    struct Impl;
    Impl* impl_;
    LimiterPlugin* owner_;
    std::atomic<vst3mini::uint32> refCount_{1};

    double ownerGet(uint32_t id) const;
    void   setParam(uint32_t id, double value);
    void   toggleParam(uint32_t id);
    void   resetAll();
    double ownerPlain(uint32_t id) const;
    float  ownerGr() const;
    int    ownerGrCount() const;
    float  ownerGrAt(int indexBack) const;
    int    ownerWave(float* dst, int maxCount) const;
};

}}
