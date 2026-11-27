#include "EditorView.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

#include "LimiterPlugin.h"
#include "Parameters.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__linux__)
#define LIMITER_USE_X11 1
#include "x11/X11Shim.h"
#include <chrono>
#include <thread>
#include <unistd.h>
#endif

namespace limiter { namespace plugin {

using namespace vst3mini;

namespace {

constexpr int kWinW = 480;
constexpr int kWinH = 320;

constexpr uint32_t kColBg     = 0x18181A;
constexpr uint32_t kColPanel  = 0x222226;
constexpr uint32_t kColPanel2 = 0x1E1E22;
constexpr uint32_t kColOrange = 0xFF8C1A;
constexpr uint32_t kColGreen  = 0x7AC74F;
constexpr uint32_t kColText   = 0xEBEBEB;
constexpr uint32_t kColDim    = 0x969696;
constexpr uint32_t kColGrid   = 0x464650;
constexpr uint32_t kColBtn    = 0x303034;
constexpr uint32_t kColBtnOn  = 0x406030;

constexpr int kGraphX = 8, kGraphY = 40, kGraphW = 464, kGraphH = 64;
constexpr int kWaveX = 8, kWaveY = 112, kWaveW = 464, kWaveH = 116;
constexpr int kKnobY = 262, kKnobR = 24, kKnobSpacing = 92, kKnobX0 = 52;
constexpr double kPi = 3.14159265358979323846;

struct Btn { int x, y, w, h; };
constexpr Btn kBtnBypass{8, 8, 68, 20};
constexpr Btn kBtnReset{80, 8, 60, 20};
constexpr Btn kBtnLink{336, 8, 68, 20};
constexpr Btn kBtnTP{408, 8, 64, 20};

struct KnobDef {
    uint32_t id;
    const char* label;
};

const KnobDef kKnobs[] = {
    { kParamCeiling,    "CEILING"   },
    { kParamLookahead,  "LOOKAHEAD" },
    { kParamAttack,     "ATTACK"    },
    { kParamRelease,    "RELEASE"   },
    { kParamOutputGain, "OUT GAIN"  },
};
constexpr int kNumKnobs = int(sizeof(kKnobs) / sizeof(kKnobs[0]));

struct UiPainter {
    virtual ~UiPainter() {}
    virtual void fillRect(int x, int y, int w, int h, uint32_t rgb) = 0;
    virtual void frameRect(int x, int y, int w, int h, uint32_t rgb) = 0;
    virtual void line(int x1, int y1, int x2, int y2, uint32_t rgb) = 0;
    virtual void text(int x, int y, const char* s, uint32_t rgb) = 0;
    virtual void textCenter(int cx, int y, int w, const char* s, uint32_t rgb) = 0;
};

int knobAt(int x, int y) {
    for (int i = 0; i < kNumKnobs; ++i) {
        const int cx = kKnobX0 + i * kKnobSpacing;
        const int dx = x - cx, dy = y - kKnobY;
        if (dx * dx + dy * dy <= (kKnobR + 6) * (kKnobR + 6)) return i;
    }
    return -1;
}

int buttonAt(int x, int y) {
    auto hit = [&](const Btn& b) { return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h; };
    if (hit(kBtnBypass)) return 1;
    if (hit(kBtnReset)) return 2;
    if (hit(kBtnLink)) return 3;
    if (hit(kBtnTP)) return 4;
    return 0;
}

void circle(UiPainter& p, int cx, int cy, int r, uint32_t rgb) {
    constexpr int kSeg = 48;
    int px = cx + r, py = cy;
    for (int i = 1; i <= kSeg; ++i) {
        const double a = 2.0 * kPi * i / kSeg;
        const int nx = cx + int(r * std::cos(a));
        const int ny = cy + int(r * std::sin(a));
        p.line(px, py, nx, ny, rgb);
        px = nx;
        py = ny;
    }
}

void arc(UiPainter& p, int cx, int cy, int r, double a0deg, double a1deg, uint32_t rgb) {
    constexpr int kSeg = 40;
    int px = cx + int(r * std::cos(a0deg * kPi / 180.0));
    int py = cy + int(r * std::sin(a0deg * kPi / 180.0));
    for (int i = 1; i <= kSeg; ++i) {
        const double a = (a0deg + (a1deg - a0deg) * i / kSeg) * kPi / 180.0;
        const int nx = cx + int(r * std::cos(a));
        const int ny = cy + int(r * std::sin(a));
        p.line(px, py, nx, ny, rgb);
        px = nx;
        py = ny;
    }
}

void button(UiPainter& p, const Btn& b, const char* label, bool active) {
    p.fillRect(b.x, b.y, b.w, b.h, active ? kColBtnOn : kColBtn);
    p.frameRect(b.x, b.y, b.w, b.h, kColGrid);
    p.textCenter(b.x + b.w / 2, b.y + 4, b.w, label, active ? kColGreen : kColText);
}

void drawUi(UiPainter& p, LimiterPlugin* owner, double& grHold) {
    p.fillRect(0, 0, kWinW, kWinH, kColBg);

    const bool bypass = owner->getParamNormalized(kParamBypass) >= 0.5;
    const bool link   = owner->getParamNormalized(kParamLink) >= 0.5;
    const bool tp     = owner->getParamNormalized(kParamTruePeak) >= 0.5;
    button(p, kBtnBypass, bypass ? "bypass*" : "bypass", bypass);
    button(p, kBtnReset, "reset", false);
    char linkLabel[32];
    std::snprintf(linkLabel, sizeof linkLabel, "link %s", link ? "100" : "off");
    button(p, kBtnLink, linkLabel, link);
    button(p, kBtnTP, tp ? "true peak*" : "true peak", tp);

    const double gr = owner->currentGainReductionDb();
    char grText[32];
    std::snprintf(grText, sizeof grText, "-%.1f dB", gr);
    p.text(168, 12, grText, kColOrange);
    p.text(238, 12, "GR", kColDim);

    p.fillRect(kGraphX, kGraphY, kGraphW, kGraphH, kColPanel);
    for (int db = 6; db <= 18; db += 6) {
        const int y = kGraphY + kGraphH - 2 - int(double(db) / 24.0 * (kGraphH - 4));
        p.line(kGraphX, y, kGraphX + kGraphW, y, kColGrid);
        char lbl[8];
        std::snprintf(lbl, sizeof lbl, "%d", db);
        p.text(kGraphX + kGraphW - 16, y - 6, lbl, kColDim);
    }
    const int count = owner->grHistoryCount();
    const int n = std::min(count, kGraphW);
    int prevX = kGraphX + kGraphW - 1, prevY = kGraphY + kGraphH - 2;
    for (int i = 0; i < n; ++i) {
        const float g = owner->grHistoryAt(n - 1 - i);
        const int x = kGraphX + kGraphW - 1 - i;
        const int y = kGraphY + kGraphH - 2 - int(std::min(24.0, double(g)) / 24.0 * (kGraphH - 4));
        p.line(prevX, prevY, x, y, kColOrange);
        prevX = x;
        prevY = y;
    }
    if (gr > grHold) grHold = gr;
    grHold *= 0.995;
    if (grHold > 0.05) {
        const int hy = kGraphY + kGraphH - 2 - int(std::min(24.0, grHold) / 24.0 * (kGraphH - 4));
        p.line(kGraphX, hy, kGraphX + kGraphW, hy, 0x8A5A20);
    }
    p.text(kGraphX + 4, kGraphY + 3, "gain reduction", kColDim);

    p.fillRect(kWaveX, kWaveY, kWaveW, kWaveH, kColPanel2);
    const int midY = kWaveY + kWaveH / 2;
    p.line(kWaveX, midY, kWaveX + kWaveW, midY, kColGrid);
    const double ceiling = owner->normalizedParamToPlain(kParamCeiling, owner->getParamNormalized(kParamCeiling));
    if (ceiling < 0.0) {
        const int cy = midY - int(std::pow(10.0, ceiling / 20.0) * (kWaveH / 2 - 2));
        p.line(kWaveX, cy, kWaveX + kWaveW, cy, kColOrange);
    }
    std::vector<float> wave(static_cast<size_t>(kWaveW));
    owner->copyWaveHistory(wave.data(), kWaveW);
    const int got = std::min(kWaveW, owner->waveHistoryCount());
    for (int i = 0; i < got; ++i) {
        const int x = kWaveX + i;
        const int y = midY - int(std::min(1.0, double(wave[size_t(i)])) * (kWaveH / 2 - 2));
        p.line(x, midY, x, y, 0xC8C8CD);
    }
    char lim[40];
    std::snprintf(lim, sizeof lim, "%.1f dB limit", ceiling);
    p.text(kWaveX + 4, kWaveY + 3, lim, kColOrange);

    for (int i = 0; i < kNumKnobs; ++i) {
        const int cx = kKnobX0 + i * kKnobSpacing;
        const double v = owner->getParamNormalized(kKnobs[i].id);
        circle(p, cx, kKnobY, kKnobR, kColGrid);
        arc(p, cx, kKnobY, kKnobR, 135.0, 135.0 + 270.0 * v, kColGreen);
        const double a = (135.0 + 270.0 * v) * kPi / 180.0;
        p.line(cx, kKnobY, cx + int((kKnobR - 5) * std::cos(a)), kKnobY + int((kKnobR - 5) * std::sin(a)), kColText);
        p.textCenter(cx, kKnobY + kKnobR + 2, 92, kKnobs[i].label, kColDim);
        const double plain = owner->normalizedParamToPlain(kKnobs[i].id, v);
        char val[32];
        std::snprintf(val, sizeof val, "%.2f", plain);
        p.textCenter(cx, kKnobY + kKnobR + 14, 92, val, kColText);
    }
}

}

#if defined(_WIN32)

namespace {

class GdiPainter : public UiPainter {
public:
    GdiPainter(HDC dc) : dc_(dc) {
        font_ = CreateFontA(11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH, "Segoe UI");
        oldFont_ = (HFONT)SelectObject(dc_, font_);
        SetBkMode(dc_, TRANSPARENT);
    }
    ~GdiPainter() override {
        SelectObject(dc_, oldFont_);
        DeleteObject(font_);
    }
    void fillRect(int x, int y, int w, int h, uint32_t rgb) override {
        RECT r{x, y, x + w, y + h};
        HBRUSH b = CreateSolidBrush(RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF));
        FillRect(dc_, &r, b);
        DeleteObject(b);
    }
    void frameRect(int x, int y, int w, int h, uint32_t rgb) override {
        HPEN pen = CreatePen(PS_SOLID, 1, RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF));
        HPEN old = (HPEN)SelectObject(dc_, pen);
        HBRUSH oldB = (HBRUSH)SelectObject(dc_, GetStockObject(NULL_BRUSH));
        Rectangle(dc_, x, y, x + w, y + h);
        SelectObject(dc_, old);
        SelectObject(dc_, oldB);
        DeleteObject(pen);
    }
    void line(int x1, int y1, int x2, int y2, uint32_t rgb) override {
        HPEN pen = CreatePen(PS_SOLID, 1, RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF));
        HPEN old = (HPEN)SelectObject(dc_, pen);
        MoveToEx(dc_, x1, y1, nullptr);
        LineTo(dc_, x2, y2);
        SelectObject(dc_, old);
        DeleteObject(pen);
    }
    void text(int x, int y, const char* s, uint32_t rgb) override {
        SetTextColor(dc_, RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF));
        RECT r{x, y, x + 300, y + 13};
        DrawTextA(dc_, s, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    void textCenter(int cx, int y, int w, const char* s, uint32_t rgb) override {
        SetTextColor(dc_, RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF));
        RECT r{cx - w / 2, y, cx + w / 2, y + 13};
        DrawTextA(dc_, s, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
private:
    HDC dc_;
    HFONT font_;
    HFONT oldFont_;
};

}

struct EditorView::Impl {
    HWND hwnd = nullptr;
    WNDPROC origProc = nullptr;
    EditorView* self = nullptr;
    void* parent = nullptr;
    UINT_PTR timer = 0;

    static LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        Impl* impl = reinterpret_cast<Impl*>(GetWindowLongPtrA(h, GWLP_USERDATA));
        if (impl && impl->self) return impl->handle(h, msg, w, l);
        return DefWindowProcA(h, msg, w, l);
    }

    void redraw(HWND h) {
        HDC hdc = GetDC(h);
        RECT rc;
        GetClientRect(h, &rc);
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HBITMAP oldBmp = (HBITMAP)SelectObject(mem, bmp);
        GdiPainter p(mem);
        drawUi(p, self->owner_, self->grHold_);
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldBmp);
        DeleteObject(bmp);
        DeleteDC(mem);
        ReleaseDC(h, hdc);
    }

    LRESULT handle(HWND h, UINT msg, WPARAM w, LPARAM l) {
        switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_TIMER:
            redraw(h);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(h, &ps);
            redraw(h);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN:
            self->mousePress(int(short(LOWORD(l))), int(short(HIWORD(l))));
            SetCapture(h);
            return 0;
        case WM_MOUSEMOVE:
            if (w & MK_LBUTTON) self->mouseMove(int(short(HIWORD(l))));
            return 0;
        case WM_LBUTTONUP:
            self->mouseRelease();
            ReleaseCapture();
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        default:
            break;
        }
        if (origProc) return CallWindowProcA(origProc, h, msg, w, l);
        return DefWindowProcA(h, msg, w, l);
    }
};

#elif defined(LIMITER_USE_X11)

namespace {

class X11Painter : public UiPainter {
public:
    X11Painter(const x11::Api& x, x11::Display* dpy, x11::Window win, x11::GC gc)
        : x_(x), dpy_(dpy), win_(win), gc_(gc) {}
    void setColor(uint32_t rgb) {
        const unsigned long px = (static_cast<unsigned long>(rgb & 0xFF0000))
                               | static_cast<unsigned long>(rgb & 0xFF00)
                               | static_cast<unsigned long>(rgb & 0xFF);
        x_.XSetForeground(dpy_, gc_, px);
    }
    void fillRect(int x, int y, int w, int h, uint32_t rgb) override {
        setColor(rgb);
        x_.XFillRectangle(dpy_, win_, gc_, x, y, static_cast<unsigned>(w), static_cast<unsigned>(h));
    }
    void frameRect(int x, int y, int w, int h, uint32_t rgb) override {
        setColor(rgb);
        x_.XDrawRectangle(dpy_, win_, gc_, x, y, static_cast<unsigned>(w - 1), static_cast<unsigned>(h - 1));
    }
    void line(int x1, int y1, int x2, int y2, uint32_t rgb) override {
        setColor(rgb);
        x_.XDrawLine(dpy_, win_, gc_, x1, y1, x2, y2);
    }
    void text(int x, int y, const char* s, uint32_t rgb) override {
        setColor(rgb);
        x_.XDrawString(dpy_, win_, gc_, x, y + 9, s, int(std::strlen(s)));
    }
    void textCenter(int cx, int y, int w, const char* s, uint32_t rgb) override {
        (void)w;
        const int len = int(std::strlen(s));
        text(cx - 3 * len, y, s, rgb);
    }
private:
    const x11::Api& x_;
    x11::Display* dpy_;
    x11::Window win_;
    x11::GC gc_;
};

}

struct EditorView::Impl {
    x11::Api x;
    x11::Display* dpy = nullptr;
    x11::Window win = 0;
    x11::GC gc = nullptr;
    void* parent = nullptr;
    std::thread th;
    std::atomic<bool> running{false};
    std::chrono::steady_clock::time_point lastDraw{};

    void draw() {
        X11Painter p(x, dpy, win, gc);
        drawUi(p, self->owner_, self->grHold_);
        x.XFlush(dpy);
        lastDraw = std::chrono::steady_clock::now();
    }

    void loop(EditorView* selfView) {
        self = selfView;
        using namespace std::chrono;
        while (running.load()) {
            if (x.XPending(dpy) > 0) {
                x11::Event ev;
                x.XNextEvent(dpy, &ev);
                if (ev.type == x11::Expose) {
                    draw();
                } else if (ev.type == x11::ButtonPress) {
                    selfView->mousePress(ev.button.x, ev.button.y);
                    draw();
                } else if (ev.type == x11::MotionNotify) {
                    if (ev.motion.state & x11::Button1Mask) {
                        selfView->mouseMove(ev.motion.y);
                        draw();
                    }
                } else if (ev.type == x11::ButtonRelease) {
                    selfView->mouseRelease();
                    draw();
                }
            } else if (steady_clock::now() - lastDraw > milliseconds(33)) {
                draw();
            } else {
                usleep(3000);
            }
        }
    }
    EditorView* self = nullptr;
};

#else

struct EditorView::Impl {
    void* parent = nullptr;
};

#endif

EditorView::EditorView(LimiterPlugin* owner) : impl_(new Impl), owner_(owner) {
    impl_->self = this;
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
#elif defined(LIMITER_USE_X11)
    if (std::strcmp(type, "X11EmbedWindowID") == 0 && parent) {
        if (!x11::loadApi(impl_->x)) return kNotImplemented;
        static std::once_flag threadsOnce;
        std::call_once(threadsOnce, [&] { if (impl_->x.XInitThreads) impl_->x.XInitThreads(); });
        impl_->dpy = impl_->x.XOpenDisplay(nullptr);
        if (!impl_->dpy) return kInternalError;
        impl_->win = (x11::Window)(uintptr_t)parent;
        impl_->gc = impl_->x.XCreateGC(impl_->dpy, impl_->win, 0, nullptr);
        impl_->x.XSelectInput(impl_->dpy, impl_->win,
                              x11::ExposureMask | x11::ButtonPressMask |
                              x11::ButtonReleaseMask | x11::PointerMotionMask);
        impl_->running = true;
        impl_->th = std::thread([this] { impl_->loop(this); });
        impl_->draw();
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
#elif defined(LIMITER_USE_X11)
    if (impl_->dpy) {
        impl_->running = false;
        if (impl_->th.joinable()) impl_->th.join();
        impl_->x.XSelectInput(impl_->dpy, impl_->win, 0);
        if (impl_->gc) impl_->x.XFreeGC(impl_->dpy, impl_->gc);
        impl_->x.XCloseDisplay(impl_->dpy);
        impl_->dpy = nullptr;
        impl_->gc = nullptr;
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

void EditorView::mousePress(int x, int y) {
    const int k = knobAt(x, y);
    if (k >= 0) {
        dragKnob_ = k;
        dragStartY_ = y;
        dragStartVal_ = owner_->getParamNormalized(kKnobs[k].id);
        return;
    }
    switch (buttonAt(x, y)) {
    case 1: toggleParam(kParamBypass); break;
    case 2: resetAll(); break;
    case 3: toggleParam(kParamLink); break;
    case 4: toggleParam(kParamTruePeak); break;
    default: return;
    }
}

void EditorView::mouseMove(int y) {
    if (dragKnob_ < 0) return;
    double v = dragStartVal_ - double(y - dragStartY_) * 0.01;
    if (v < 0.0) v = 0.0;
    if (v > 1.0) v = 1.0;
    owner_->setParamNormalized(kKnobs[dragKnob_].id, v);
}

void EditorView::mouseRelease() { dragKnob_ = -1; }

double EditorView::ownerGet(uint32_t id) const { return owner_->getParamNormalized(id); }

void EditorView::setParam(uint32_t id, double value) { owner_->setParamNormalized(id, value); }

void EditorView::toggleParam(uint32_t id) {
    owner_->setParamNormalized(id, owner_->getParamNormalized(id) >= 0.5 ? 0.0 : 1.0);
}

void EditorView::resetAll() { owner_->resetDsp(); }

double EditorView::ownerPlain(uint32_t id) const {
    return owner_->normalizedParamToPlain(id, owner_->getParamNormalized(id));
}

float EditorView::ownerGr() const { return owner_->currentGainReductionDb(); }

int EditorView::ownerGrCount() const { return owner_->grHistoryCount(); }

float EditorView::ownerGrAt(int indexBack) const { return owner_->grHistoryAt(indexBack); }

int EditorView::ownerWave(float* dst, int maxCount) const {
    owner_->copyWaveHistory(dst, maxCount);
    return std::min(maxCount, owner_->waveHistoryCount());
}

}}
