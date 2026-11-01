#include "win_platform.hpp"
#include <cstdio>

namespace winplat {

int64_t nowUs() {
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (int64_t)(c.QuadPart / freq.QuadPart * 1000000 + (c.QuadPart % freq.QuadPart) * 1000000 / freq.QuadPart);
}

// ---------------- window ----------------

struct FindCtx { HWND found; };

static BOOL CALLBACK enumProc(HWND hwnd, LPARAM lp) {
    if (!IsWindowVisible(hwnd)) return TRUE;
    wchar_t title[256];
    int n = GetWindowTextW(hwnd, title, 256);
    if (n <= 0) return TRUE;
    if (wcscmp(title, L"Geometry Dash") == 0) {
        reinterpret_cast<FindCtx*>(lp)->found = hwnd;
        return FALSE;
    }
    return TRUE;
}

HWND findGameWindow() {
    HWND h = FindWindowW(nullptr, L"Geometry Dash");
    if (h && IsWindowVisible(h)) return h;
    FindCtx ctx{nullptr};
    EnumWindows(enumProc, (LPARAM)&ctx);
    return ctx.found;
}

bool clientRectOnScreen(HWND hwnd, RECT& out) {
    if (!hwnd || !IsWindow(hwnd) || IsIconic(hwnd)) return false;
    RECT rc;
    if (!GetClientRect(hwnd, &rc)) return false;
    POINT tl{rc.left, rc.top}, br{rc.right, rc.bottom};
    ClientToScreen(hwnd, &tl);
    ClientToScreen(hwnd, &br);
    out = RECT{tl.x, tl.y, br.x, br.y};
    return (out.right - out.left) > 32 && (out.bottom - out.top) > 32;
}

bool isGameFocused(HWND hwnd) {
    return hwnd && GetForegroundWindow() == hwnd;
}

// ---------------- capture ----------------

Capturer::~Capturer() {
    if (memDc_) {
        if (old_) SelectObject(memDc_, old_);
        DeleteDC(memDc_);
    }
    if (dib_) DeleteObject(dib_);
}

void Capturer::ensure(int w, int h) {
    if (w == w_ && h == h_ && dib_) return;
    if (memDc_) { if (old_) SelectObject(memDc_, old_); DeleteDC(memDc_); memDc_ = nullptr; }
    if (dib_) { DeleteObject(dib_); dib_ = nullptr; }
    HDC screen = GetDC(nullptr);
    memDc_ = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;          // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    dib_ = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    bits_ = (uint8_t*)bits;
    old_ = SelectObject(memDc_, dib_);
    SetStretchBltMode(memDc_, HALFTONE);
    SetBrushOrgEx(memDc_, 0, 0, nullptr);
    ReleaseDC(nullptr, screen);
    w_ = w; h_ = h;
}

bool Capturer::capture(HWND hwnd, int maxWidth, gdbot::Frame& out) {
    RECT r;
    if (!clientRectOnScreen(hwnd, r)) return false;
    rect_ = r;
    int sw = r.right - r.left, sh = r.bottom - r.top;
    int w = sw < maxWidth ? sw : maxWidth;
    int h = (int)((int64_t)sh * w / sw);
    ensure(w, h);
    if (!dib_ || !bits_) return false;
    HDC screen = GetDC(nullptr);
    BOOL ok = (w == sw) ? BitBlt(memDc_, 0, 0, w, h, screen, r.left, r.top, SRCCOPY)
                        : StretchBlt(memDc_, 0, 0, w, h, screen, r.left, r.top, sw, sh, SRCCOPY);
    ReleaseDC(nullptr, screen);
    if (!ok) return false;
    GdiFlush();
    out.data = bits_;
    out.width = w;
    out.height = h;
    out.stride = w * 4;
    out.format = gdbot::PixelFormat::BGRA;
    out.timeUs = nowUs();
    return true;
}

// ---------------- input ----------------

void InputSender::sendKey(WORD vk, bool up) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.wScan = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    in.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    if (vk == VK_UP || vk == VK_DOWN || vk == VK_LEFT || vk == VK_RIGHT) in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    SendInput(1, &in, sizeof(INPUT));
}

bool InputSender::press(HWND game, int player) {
    player &= 1;
    if (!isGameFocused(game)) return false;
    if (down_[player]) return true;
    sendKey(player ? keys.p2 : keys.p1, false);
    down_[player] = true;
    return true;
}

bool InputSender::release(HWND game, int player) {
    (void)game;
    player &= 1;
    if (!down_[player]) return true;
    // always release a key we pressed, even if focus moved, so nothing stays stuck
    sendKey(player ? keys.p2 : keys.p1, true);
    down_[player] = false;
    return true;
}

bool InputSender::tapKey(HWND game, WORD vk) {
    if (!isGameFocused(game)) return false;
    sendKey(vk, false);
    sendKey(vk, true);
    return true;
}

bool InputSender::clickRelative(HWND game, float rx, float ry) {
    if (!isGameFocused(game)) return false;
    RECT r;
    if (!clientRectOnScreen(game, r)) return false;
    int x = r.left + (int)(rx * (r.right - r.left));
    int y = r.top + (int)(ry * (r.bottom - r.top));
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN), vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    INPUT in[3]{};
    in[0].type = INPUT_MOUSE;
    in[0].mi.dx = (LONG)(((int64_t)(x - vx) * 65535) / (vw - 1));
    in[0].mi.dy = (LONG)(((int64_t)(y - vy) * 65535) / (vh - 1));
    in[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    in[1] = in[0];
    in[1].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    in[1].mi.dx = in[1].mi.dy = 0;
    in[2] = in[1];
    in[2].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(3, in, sizeof(INPUT));
    return true;
}

void InputSender::releaseAll() {
    for (int p = 0; p < 2; ++p)
        if (down_[p]) { sendKey(p ? keys.p2 : keys.p1, true); down_[p] = false; }
}

// ---------------- files ----------------

std::wstring exeDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf, n);
    size_t s = p.find_last_of(L"\\/");
    return s == std::wstring::npos ? L"." : p.substr(0, s);
}

std::string readFile(const std::wstring& path) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return {};
    std::string s;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    fclose(f);
    return s;
}

bool writeFile(const std::wstring& path, const std::string& data) {
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return false;
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    fclose(f);
    return ok;
}

}  // namespace winplat
