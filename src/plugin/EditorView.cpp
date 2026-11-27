#include "EditorView.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "LimiterPlugin.h"
#include "Parameters.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace limiter { namespace plugin {

using namespace vst3mini;

namespace {

constexpr int kWinW = 480;
constexpr int kWinH = 320;

struct KnobDef {
    uint32_t id;
    const char* label;
};

const KnobDef kKnobs[] = {
    { kParamCeiling,    "Ceiling"   },
    { kParamLookahead,  "Lookahead" },
    { kParamAttack,     "Attack"    },
    { kParamRelease,    "Release"   },
    { kParamOutputGain, "Out Gain"  },
};
constexpr int kNumKnobs = int(sizeof(kKnobs) / sizeof(kKnobs[0]));

constexpr int kGraphX = 8, kGraphY = 40, kGraphW = 464, kGraphH = 64;
constexpr int kWaveX = 8, kWaveY = 112, kWaveW = 464, kWaveH = 116;
constexpr int kKnobY = 268, kKnobR = 24, kKnobSpacing = 92, kKnobX0 = 52;

[[maybe_unused]] bool inRect(int x, int y, int l, int t, int r, int b) {
    return x >= l && x < r && y >= t && y < b;
}

}

#if defined(_WIN32)

struct EditorView::Impl {
    HWND hwnd = nullptr;
    WNDPROC origProc = nullptr;
    EditorView* self = nullptr;
    void* parent = nullptr;
    int dragKnob = -1;
    int startY = 0;
    double startVal = 0.0;
    UINT_PTR timer = 0;

    static LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        Impl* impl = reinterpret_cast<Impl*>(GetWindowLongPtrA(h, GWLP_USERDATA));
        if (impl && impl->self) return impl->handle(h, msg, w, l);
        return DefWindowProcA(h, msg, w, l);
    }

    int knobAt(int x, int y) const {
        for (int i = 0; i < kNumKnobs; ++i) {
            const int cx = kKnobX0 + i * kKnobSpacing;
            const int dx = x - cx, dy = y - kKnobY;
            if (dx * dx + dy * dy <= (kKnobR + 6) * (kKnobR + 6)) return i;
        }
        return -1;
    }

    bool buttonAt(int x, int y, int& which) const {
        if (inRect(x, y, 8, 8, 76, 28)) { which = 0; return true; }
        if (inRect(x, y, 80, 8, 140, 28)) { which = 1; return true; }
        if (inRect(x, y, 336, 8, 404, 28)) { which = 2; return true; }
        if (inRect(x, y, 408, 8, 472, 28)) { which = 3; return true; }
        return false;
    }

    LRESULT handle(HWND h, UINT msg, WPARAM w, LPARAM l) {
        switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_TIMER:
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(h, &ps);
            RECT rc;
            GetClientRect(h, &rc);
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
            HBITMAP oldBmp = (HBITMAP)SelectObject(mem, bmp);
            draw(mem, rc.right, rc.bottom);
            BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, oldBmp);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            const int x = int(short(LOWORD(l)));
            const int y = int(short(HIWORD(l)));
            const int k = knobAt(x, y);
            if (k >= 0) {
                dragKnob = k;
                startY = y;
                startVal = self->ownerGet(kKnobs[k].id);
                SetCapture(h);
                return 0;
            }
            int which = -1;
            if (buttonAt(x, y, which)) {
                if (which == 0) self->toggleParam(kParamBypass);
                else if (which == 1) self->resetAll();
                else if (which == 2) self->toggleParam(kParamLink);
                else if (which == 3) self->toggleParam(kParamTruePeak);
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (dragKnob >= 0) {
                const int y = int(short(HIWORD(l)));
                double v = startVal - double(y - startY) * 0.01;
                if (v < 0.0) v = 0.0;
                if (v > 1.0) v = 1.0;
                self->setParam(kKnobs[dragKnob].id, v);
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            }
            return 0;
        }
        case WM_LBUTTONUP:
            if (dragKnob >= 0) {
                dragKnob = -1;
                ReleaseCapture();
                return 0;
            }
            return 0;
        default:
            break;
        }
        if (origProc) return CallWindowProcA(origProc, h, msg, w, l);
        return DefWindowProcA(h, msg, w, l);
    }

    void draw(HDC dc, int w, int h) {
        RECT rc{0, 0, w, h};
        HBRUSH bg = CreateSolidBrush(RGB(24, 24, 26));
        FillRect(dc, &rc, bg);
        DeleteObject(bg);

        HFONT font = CreateFontA(11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 DEFAULT_PITCH, "Segoe UI");
        HFONT oldFont = (HFONT)SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(235, 235, 235));

        drawButtons(dc);
        drawGrReadout(dc);
        drawGraph(dc);
        drawWave(dc);
        drawKnobs(dc);

        SelectObject(dc, oldFont);
        DeleteObject(font);
    }

    void drawText(HDC dc, int x, int y, const char* s, COLORREF c) {
        SetTextColor(dc, c);
        RECT r{x, y, x + 300, y + 16};
        DrawTextA(dc, s, -1, &r, DT_LEFT | DT_NOPREFIX);
    }

    void drawButtonBox(HDC dc, int l, int t, int r, int b, const char* label, bool active) {
        RECT rc{l, t, r, b};
        HBRUSH br = CreateSolidBrush(active ? RGB(64, 96, 48) : RGB(48, 48, 52));
        FillRect(dc, &rc, br);
        DeleteObject(br);
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(90, 90, 96));
        HPEN oldPen = (HPEN)SelectObject(dc, pen);
        HBRUSH oldBr = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, l, t, r, b);
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBr);
        DeleteObject(pen);
        SetTextColor(dc, active ? RGB(190, 255, 160) : RGB(220, 220, 220));
        RECT tr{l, t + 4, r, b};
        DrawTextA(dc, label, -1, &tr, DT_CENTER | DT_NOPREFIX);
    }

    void drawButtons(HDC dc) {
        const bool bypass = self->ownerGet(kParamBypass) >= 0.5;
        const bool link = self->ownerGet(kParamLink) >= 0.5;
        const bool tp = self->ownerGet(kParamTruePeak) >= 0.5;
        drawButtonBox(dc, 8, 8, 76, 28, bypass ? "bypass*" : "bypass", bypass);
        drawButtonBox(dc, 80, 8, 140, 28, "reset", false);
        char linkLabel[32];
        std::snprintf(linkLabel, sizeof linkLabel, "link %s", link ? "100" : "off");
        drawButtonBox(dc, 336, 8, 404, 28, linkLabel, link);
        drawButtonBox(dc, 408, 8, 472, 28, tp ? "true peak*" : "true peak", tp);
    }

    void drawGrReadout(HDC dc) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "-%.1f dB", double(self->ownerGr()));
        drawText(dc, 170, 12, buf, RGB(255, 150, 40));
        drawText(dc, 240, 12, "GR", RGB(160, 160, 160));
    }

    void drawGraph(HDC dc) {
        RECT rc{kGraphX, kGraphY, kGraphX + kGraphW, kGraphY + kGraphH};
        HBRUSH br = CreateSolidBrush(RGB(34, 34, 38));
        FillRect(dc, &rc, br);
        DeleteObject(br);
        const int count = self->ownerGrCount();
        const int n = std::min(count, kGraphW);
        HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 140, 26));
        HPEN oldPen = (HPEN)SelectObject(dc, pen);
        MoveToEx(dc, kGraphX, kGraphY + kGraphH, nullptr);
        for (int i = 0; i < n; ++i) {
            const float gr = self->ownerGrAt(n - 1 - i);
            const double frac = std::min(1.0, double(gr) / 24.0);
            const int y = kGraphY + kGraphH - int(frac * (kGraphH - 2));
            LineTo(dc, kGraphX + kGraphW - 1 - i, y);
        }
        SelectObject(dc, oldPen);
        DeleteObject(pen);
        drawText(dc, kGraphX + 4, kGraphY + 2, "gain reduction", RGB(150, 150, 150));
    }

    void drawWave(HDC dc) {
        RECT rc{kWaveX, kWaveY, kWaveX + kWaveW, kWaveY + kWaveH};
        HBRUSH br = CreateSolidBrush(RGB(30, 30, 34));
        FillRect(dc, &rc, br);
        DeleteObject(br);
        const int midY = kWaveY + kWaveH / 2;
        HPEN zeroPen = CreatePen(PS_SOLID, 1, RGB(70, 70, 76));
        HPEN oldPen = (HPEN)SelectObject(dc, zeroPen);
        MoveToEx(dc, kWaveX, midY, nullptr);
        LineTo(dc, kWaveX + kWaveW, midY);
        const double ceiling = self->ownerPlain(kParamCeiling);
        if (ceiling < 0.0) {
            const double frac = std::pow(10.0, ceiling / 20.0);
            const int cy = midY - int(frac * (kWaveH / 2 - 2));
            HPEN ceilPen = CreatePen(PS_SOLID, 1, RGB(255, 140, 26));
            SelectObject(dc, ceilPen);
            MoveToEx(dc, kWaveX, cy, nullptr);
            LineTo(dc, kWaveX + kWaveW, cy);
            SelectObject(dc, zeroPen);
            DeleteObject(ceilPen);
        }
        std::vector<float> wave(kWaveW);
        const int got = self->ownerWave(wave.data(), kWaveW);
        HPEN wavePen = CreatePen(PS_SOLID, 1, RGB(200, 200, 205));
        SelectObject(dc, wavePen);
        for (int i = 0; i < got; ++i) {
            const int x = kWaveX + i;
            const double v = std::min(1.0, double(wave[size_t(i)]));
            const int y = midY - int(v * (kWaveH / 2 - 2));
            MoveToEx(dc, x, midY, nullptr);
            LineTo(dc, x, y);
        }
        SelectObject(dc, oldPen);
        DeleteObject(zeroPen);
        DeleteObject(wavePen);
        char lim[32];
        std::snprintf(lim, sizeof lim, "%.1f dB limit", ceiling);
        drawText(dc, kWaveX + 4, kWaveY + 2, lim, RGB(255, 140, 26));
    }

    void drawKnobs(HDC dc) {
        for (int i = 0; i < kNumKnobs; ++i) {
            const int cx = kKnobX0 + i * kKnobSpacing;
            const double v = self->ownerGet(kKnobs[i].id);
            const double angle = (-225.0 + v * 270.0) * 3.14159265358979323846 / 180.0;
            HPEN ring = CreatePen(PS_SOLID, 3, RGB(60, 60, 66));
            HPEN oldPen = (HPEN)SelectObject(dc, ring);
            HBRUSH oldBr = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
            Ellipse(dc, cx - kKnobR, kKnobY - kKnobR, cx + kKnobR, kKnobY + kKnobR);
            SelectObject(dc, oldPen);
            SelectObject(dc, oldBr);
            DeleteObject(ring);
            HPEN arc = CreatePen(PS_SOLID, 3, RGB(122, 199, 79));
            SelectObject(dc, arc);
            Arc(dc, cx - kKnobR, kKnobY - kKnobR, cx + kKnobR, kKnobY + kKnobR,
                cx + int(kKnobR * std::cos(angle)), kKnobY - int(kKnobR * std::sin(angle)),
                cx - kKnobR, kKnobY);
            SelectObject(dc, oldPen);
            DeleteObject(arc);
            HPEN ptr = CreatePen(PS_SOLID, 2, RGB(240, 240, 240));
            SelectObject(dc, ptr);
            MoveToEx(dc, cx, kKnobY, nullptr);
            LineTo(dc, cx + int((kKnobR - 5) * std::cos(angle)), kKnobY - int((kKnobR - 5) * std::sin(angle)));
            SelectObject(dc, oldPen);
            DeleteObject(ptr);
            const double plain = self->ownerPlain(kKnobs[i].id);
            char val[48];
            std::snprintf(val, sizeof val, "%.2f", plain);
            RECT vr{cx - 40, kKnobY + kKnobR + 14, cx + 40, kKnobY + kKnobR + 30};
            SetTextColor(dc, RGB(220, 220, 220));
            DrawTextA(dc, val, -1, &vr, DT_CENTER | DT_NOPREFIX);
            RECT lr{cx - 46, kKnobY + kKnobR + 2, cx + 46, kKnobY + kKnobR + 16};
            SetTextColor(dc, RGB(150, 150, 150));
            DrawTextA(dc, kKnobs[i].label, -1, &lr, DT_CENTER | DT_NOPREFIX);
        }
    }
};

#else

struct EditorView::Impl {
    void* parent = nullptr;
};

#endif

EditorView::EditorView(LimiterPlugin* owner) : impl_(new Impl), owner_(owner) {
#if defined(_WIN32)
    impl_->self = this;
#endif
}

EditorView::~EditorView() { delete impl_; }

tresult EditorView::queryInterface(const FUID& iid, void** obj) {
    if (!obj) return kInvalidArgument;
    if (iid == kIUnknownIID || iid == kIPlugViewIID) {
        *obj = this;
        addRef();
        return kResultOk;
    }
    *obj = nullptr;
    return kNoInterface;
}

uint32 EditorView::addRef() { return ++refCount_; }

uint32 EditorView::release() {
    const uint32 r = --refCount_;
    if (r == 0) delete this;
    return r;
}

tresult EditorView::isPlatformTypeSupported(const char* type) {
    if (!type) return kResultFalse;
    static const char* kTypes[] = { "HWND", "X11EmbedWindowID", "HIView", "NSView", "UIView" };
    for (const char* t : kTypes)
        if (std::strcmp(type, t) == 0) return kResultTrue;
    return kResultFalse;
}

tresult EditorView::attached(void* parent, const char* type) {
    if (isPlatformTypeSupported(type) != kResultTrue) return kInvalidArgument;
#if defined(_WIN32)
    if (std::strcmp(type, "HWND") == 0 && parent) {
        HWND h = (HWND)parent;
        impl_->hwnd = h;
        SetWindowLongPtrA(h, GWLP_USERDATA, (LONG_PTR)impl_);
        impl_->origProc = (WNDPROC)SetWindowLongPtrA(h, GWLP_WNDPROC, (LONG_PTR)&Impl::wndProc);
        impl_->timer = SetTimer(h, 1, 33, nullptr);
        InvalidateRect(h, nullptr, FALSE);
        return kResultOk;
    }
#endif
    impl_->parent = parent;
    return kResultOk;
}

tresult EditorView::removed() {
#if defined(_WIN32)
    if (impl_->hwnd) {
        KillTimer(impl_->hwnd, impl_->timer);
        SetWindowLongPtrA(impl_->hwnd, GWLP_WNDPROC, (LONG_PTR)impl_->origProc);
        SetWindowLongPtrA(impl_->hwnd, GWLP_USERDATA, 0);
        impl_->hwnd = nullptr;
    }
#endif
    impl_->parent = nullptr;
    return kResultOk;
}

tresult EditorView::onWheel(float) { return kNotImplemented; }
tresult EditorView::onKeyDown(TChar, int16, int16) { return kNotImplemented; }
tresult EditorView::onKeyUp(TChar, int16, int16) { return kNotImplemented; }

tresult EditorView::getSize(ViewRect* size) {
    if (!size) return kInvalidArgument;
    size->left = 0;
    size->top = 0;
    size->right = kWinW;
    size->bottom = kWinH;
    return kResultOk;
}

tresult EditorView::onSize(ViewRect*) { return kResultOk; }
tresult EditorView::onFocus(bool) { return kResultOk; }
tresult EditorView::setFrame(IPlugFrame*) { return kResultOk; }
tresult EditorView::canResize() { return kResultFalse; }

tresult EditorView::checkSizeConstraint(ViewRect* rect) {
    if (!rect) return kInvalidArgument;
    if (rect->getWidth() < kWinW) rect->right = rect->left + kWinW;
    if (rect->getHeight() < kWinH) rect->bottom = rect->top + kWinH;
    return kResultOk;
}

double EditorView::ownerGet(uint32_t id) const { return owner_->getParamNormalized(id); }

void EditorView::setParam(uint32_t id, double value) { owner_->setParamNormalized(id, value); }

void EditorView::toggleParam(uint32_t id) {
    owner_->setParamNormalized(id, owner_->getParamNormalized(id) >= 0.5 ? 0.0 : 1.0);
}

void EditorView::resetAll() { owner_->resetDsp(); }

double EditorView::ownerPlain(uint32_t id) const { return owner_->normalizedParamToPlain(id, owner_->getParamNormalized(id)); }

float EditorView::ownerGr() const { return owner_->currentGainReductionDb(); }

int EditorView::ownerGrCount() const { return owner_->grHistoryCount(); }

float EditorView::ownerGrAt(int indexBack) const { return owner_->grHistoryAt(indexBack); }

int EditorView::ownerWave(float* dst, int maxCount) const {
    owner_->copyWaveHistory(dst, maxCount);
    return std::min(maxCount, owner_->waveHistoryCount());
}

}}
