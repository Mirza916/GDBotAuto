// GD AutoBot - Windows app (standalone EXE, no mod loader, no injection).
// Captures the Geometry Dash 2.209 window from the screen, runs the shared bot
// core and sends keyboard input with SendInput only while GD is focused.
#include <windows.h>
#include <commdlg.h>
#include <mmsystem.h>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "gdbot/engine.hpp"
#include "win_platform.hpp"

using namespace gdbot;

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

// ======================= shared state =======================
static std::mutex g_mu;                 // guards g_eng, g_lastFrame, g_toast
static std::mutex g_inMu;               // guards g_input
static BotEngine g_eng;
static winplat::InputSender g_input;
static std::atomic<HWND> g_game{nullptr};
static std::atomic<bool> g_running{true};
static std::atomic<int> g_capW{0}, g_capH{0}, g_gameW{0}, g_gameH{0};
static std::atomic<bool> g_captureFailing{false};
static std::vector<uint8_t> g_lastFrame;
static int g_lastW = 0, g_lastH = 0;
static std::wstring g_toast;
static int64_t g_toastUntil = 0;
static int g_captureWidth = 400;        // analysis resolution
static bool g_twoPlayerKeys = false;    // P2 = Up arrow (2-player levels)

static HWND g_main = nullptr, g_overlay = nullptr, g_levelEdit = nullptr;
static HHOOK g_kbHook = nullptr, g_msHook = nullptr;

static const wchar_t* kTitle = L"GD AutoBot 2.209";

static void toast(const std::wstring& s) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_toast = s;
    g_toastUntil = winplat::nowUs() + 4000000;
}

static void execCmds(std::vector<InputCmd>& cmds) {
    if (cmds.empty()) return;
    HWND gw = g_game.load();
    float rx = 0, ry = 0;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        rx = g_eng.cfg.retryX; ry = g_eng.cfg.retryY;
    }
    std::lock_guard<std::mutex> lk(g_inMu);
    for (const auto& c : cmds) {
        switch (c.type) {
            case CmdType::Press: g_input.press(gw, c.player); break;
            case CmdType::Release: g_input.release(gw, c.player); break;
            case CmdType::PlaceCheckpoint: g_input.tapKey(gw, g_input.keys.placeCheckpoint); break;
            case CmdType::RemoveCheckpoint: g_input.tapKey(gw, g_input.keys.removeCheckpoint); break;
            case CmdType::RetryClick: if (rx >= 0 && ry >= 0) g_input.clickRelative(gw, rx, ry); break;
        }
    }
    cmds.clear();
}

// ======================= files / profiles =======================
static std::wstring dirPath(const wchar_t* sub) {
    std::wstring d = winplat::exeDir() + L"\\" + sub;
    CreateDirectoryW(d.c_str(), nullptr);
    return d;
}
static std::wstring profilePath(int level) { return dirPath(L"profiles") + L"\\" + std::to_wstring(level) + L".cfg"; }
static std::wstring macroPath(int level) { return dirPath(L"macros") + L"\\" + std::to_wstring(level) + L".gdbm"; }
static std::wstring defaultCfgPath() { return winplat::exeDir() + L"\\gdautobot.cfg"; }

static std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

static bool loadMacroFile(const std::wstring& path) {
    std::string text = winplat::readFile(path);
    if (text.empty()) { toast(L"Macro bulunamadı: " + path); return false; }
    Macro m;
    std::string err;
    if (!Macro::parse(text, m, &err)) { toast(L"Macro okunamadı: " + widen(err)); return false; }
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_eng.loadMacro(m);
    }
    wchar_t buf[256];
    swprintf(buf, 256, L"Macro yüklendi: %zu olay, %.1f sn, oyun %hs", m.events.size(), m.durationUs() / 1e6,
             m.gameVersion.c_str());
    toast(buf);
    return true;
}

static void saveMacro(const Macro& m, const std::wstring& path) {
    if (winplat::writeFile(path, m.serialize())) toast(L"Macro kaydedildi: " + path);
    else toast(L"Macro yazılamadı: " + path);
}

static int levelFromEdit() {
    wchar_t buf[32] = {};
    GetWindowTextW(g_levelEdit, buf, 32);
    return _wtoi(buf);
}

static void loadProfile(int level) {
    std::string text = winplat::readFile(profilePath(level));
    if (text.empty()) {
        std::lock_guard<std::mutex> lk(g_mu);
        g_eng.cfg.levelId = level;
        g_toast = L"Bu level için profil yok, mevcut ayarlar kullanılıyor (Level " + std::to_wstring(level) + L")";
        g_toastUntil = winplat::nowUs() + 4000000;
    } else {
        Config c = Config::parse(text);
        c.levelId = level;
        std::lock_guard<std::mutex> lk(g_mu);
        g_eng.cfg = c;
        g_toast = L"Profil yüklendi: Level " + std::to_wstring(level);
        g_toastUntil = winplat::nowUs() + 4000000;
    }
    std::wstring mp = macroPath(level);
    if (GetFileAttributesW(mp.c_str()) != INVALID_FILE_ATTRIBUTES) loadMacroFile(mp);
}

static void saveProfile(int level) {
    std::string text;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_eng.cfg.levelId = level;
        text = g_eng.cfg.serialize();
    }
    if (winplat::writeFile(profilePath(level), text)) toast(L"Profil kaydedildi: Level " + std::to_wstring(level));
}

// ======================= threads =======================
static void captureThread() {
    timeBeginPeriod(1);
    winplat::Capturer cap;
    Frame f;
    std::vector<InputCmd> cmds;
    int64_t lastFind = 0;
    int fails = 0;
    while (g_running) {
        int64_t now = winplat::nowUs();
        HWND gw = g_game.load();
        if ((!gw || !IsWindow(gw)) && now - lastFind > 1000000) {
            gw = winplat::findGameWindow();
            g_game = gw;
            lastFind = now;
        }
        if (!gw) { g_gameW = 0; Sleep(100); continue; }
        if (!cap.capture(gw, g_captureWidth, f)) {
            if (++fails > 20) g_captureFailing = true;
            if (!IsWindow(gw)) g_game = nullptr;
            Sleep(30);
            continue;
        }
        fails = 0;
        g_captureFailing = false;
        RECT r = cap.lastRect();
        g_gameW = r.right - r.left; g_gameH = r.bottom - r.top;
        g_capW = f.width; g_capH = f.height;
        {
            std::lock_guard<std::mutex> lk(g_mu);
            g_eng.onFrame(f, cmds);
            g_lastFrame.assign(f.data, f.data + (size_t)f.stride * f.height);
            g_lastW = f.width; g_lastH = f.height;
        }
        execCmds(cmds);
        Sleep(0);
    }
    timeEndPeriod(1);
}

// Precise timing for macro events and tap releases (~1 ms resolution).
static void tickThread() {
    timeBeginPeriod(1);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    std::vector<InputCmd> cmds;
    while (g_running) {
        int64_t next;
        {
            std::lock_guard<std::mutex> lk(g_mu);
            next = g_eng.nextDeadlineUs();
        }
        int64_t now = winplat::nowUs();
        if (next < 0 || next - now > 2500) {
            Sleep(1);
        } else {
            while (winplat::nowUs() < next) YieldProcessor();   // spin the last ~2 ms
        }
        {
            std::lock_guard<std::mutex> lk(g_mu);
            g_eng.onTick(winplat::nowUs(), cmds);
        }
        execCmds(cmds);
    }
    timeEndPeriod(1);
}

// ======================= hooks (recording + manual takeover) =======================
static int playerForKey(DWORD vk) {
    if (g_twoPlayerKeys && vk == g_input.keys.p2) return 1;
    if (vk == g_input.keys.p1 || vk == 'W' || vk == VK_UP) return 0;
    return -1;
}

static void userInput(bool down, int player) {
    std::vector<InputCmd> cmds;
    bool tookOver;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        tookOver = g_eng.onUserInput(winplat::nowUs(), down, player, cmds);
    }
    execCmds(cmds);
    if (tookOver) {
        toast(L"Manuel kontrol: bot kapandı (F8 ile tekrar aç)");
        PostMessageW(g_main, WM_APP + 1, 0, 0);
    }
}

static LRESULT CALLBACK kbProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) {
        auto* k = reinterpret_cast<KBDLLHOOKSTRUCT*>(lp);
        if (!(k->flags & LLKHF_INJECTED) && winplat::isGameFocused(g_game.load())) {
            int p = playerForKey(k->vkCode);
            bool down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN;
            bool up = wp == WM_KEYUP || wp == WM_SYSKEYUP;
            if (p >= 0 && (down || up)) userInput(down, p);
        }
    }
    return CallNextHookEx(g_kbHook, code, wp, lp);
}

static LRESULT CALLBACK msProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && (wp == WM_LBUTTONDOWN || wp == WM_LBUTTONUP)) {
        auto* m = reinterpret_cast<MSLLHOOKSTRUCT*>(lp);
        HWND gw = g_game.load();
        RECT r;
        if (!(m->flags & LLMHF_INJECTED) && winplat::isGameFocused(gw) && winplat::clientRectOnScreen(gw, r) &&
            PtInRect(&r, m->pt))
            userInput(wp == WM_LBUTTONDOWN, 0);
    }
    return CallNextHookEx(g_msHook, code, wp, lp);
}

// ======================= actions =======================
enum Action {
    A_AUTOPLAY = 1, A_BOTMODE, A_REC, A_SYNC, A_PAUSE, A_STOP, A_GAMEMODE, A_SPEED, A_GRAVITY, A_MINI,
    A_IMPORT, A_EXPORT, A_DELAY_M, A_DELAY_P, A_OFFSET_M, A_OFFSET_P, A_TAP_M, A_TAP_P, A_CP_M, A_CP_P,
    A_PRACTICE, A_RETRY, A_BARSYNC, A_TAKEOVER, A_LOADPROF, A_SAVEPROF, A_CALIB_PLAYER, A_CALIB_BARL,
    A_CALIB_BARR, A_CALIB_RETRY, A_LOOK_M, A_LOOK_P, A_SET_SMART, A_SET_MACRO
};

static bool cursorRel(float& rx, float& ry) {
    HWND gw = g_game.load();
    RECT r;
    POINT pt;
    if (!winplat::clientRectOnScreen(gw, r) || !GetCursorPos(&pt) || !PtInRect(&r, pt)) return false;
    rx = (pt.x - r.left) / (float)(r.right - r.left);
    ry = (pt.y - r.top) / (float)(r.bottom - r.top);
    return true;
}

static void doAction(int a) {
    std::vector<InputCmd> cmds;
    int64_t now = winplat::nowUs();
    Macro recorded;
    bool haveRecorded = false;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        Config& c = g_eng.cfg;
        switch (a) {
            case A_AUTOPLAY: g_eng.setAutoPlay(!g_eng.autoPlay(), now, cmds); break;
            case A_BOTMODE:
                g_eng.setBotMode(g_eng.botMode() == BotMode::Smart ? BotMode::Macro : BotMode::Smart, now, cmds);
                break;
            case A_SET_SMART: g_eng.setBotMode(BotMode::Smart, now, cmds); break;
            case A_SET_MACRO: g_eng.setBotMode(BotMode::Macro, now, cmds); break;
            case A_REC:
                if (g_eng.recording()) { recorded = g_eng.stopRecording(); recorded.platform = "windows"; haveRecorded = true; }
                else g_eng.startRecording(now, cmds);
                break;
            case A_SYNC: g_eng.manualSync(now, cmds); break;
            case A_PAUSE: g_eng.togglePause(now, cmds); break;
            case A_STOP: g_eng.emergencyStop(cmds); break;
            case A_GAMEMODE: g_eng.setGameMode((GameMode)(((int)c.gameMode + 1) % (int)GameMode::Count)); break;
            case A_SPEED: {
                static const float sp[] = {0.5f, 1.f, 2.f, 3.f, 4.f};
                int i = 0;
                while (i < 5 && sp[i] < c.speed - 0.01f) ++i;
                c.speed = sp[(i + 1) % 5];
                break;
            }
            case A_GRAVITY: c.upsideDown = !c.upsideDown; break;
            case A_MINI: c.mini = !c.mini; break;
            case A_DELAY_M: c.inputDelayMs = std::max(0, c.inputDelayMs - 5); break;
            case A_DELAY_P: c.inputDelayMs = std::min(300, c.inputDelayMs + 5); break;
            case A_OFFSET_M: c.timingOffsetMs -= 1; break;
            case A_OFFSET_P: c.timingOffsetMs += 1; break;
            case A_TAP_M: c.tapDurationMs = std::max(5, c.tapDurationMs - 5); break;
            case A_TAP_P: c.tapDurationMs = std::min(500, c.tapDurationMs + 5); break;
            case A_CP_M: c.checkpointIntervalMs = std::max(500, c.checkpointIntervalMs - 250); break;
            case A_CP_P: c.checkpointIntervalMs = std::min(20000, c.checkpointIntervalMs + 250); break;
            case A_LOOK_M: c.lookaheadBlocks = std::max(0.4f, c.lookaheadBlocks - 0.2f); break;
            case A_LOOK_P: c.lookaheadBlocks = std::min(6.f, c.lookaheadBlocks + 0.2f); break;
            case A_PRACTICE: c.practiceAssist = !c.practiceAssist; break;
            case A_RETRY: c.autoRetry = !c.autoRetry; break;
            case A_BARSYNC: c.progressSync = !c.progressSync; break;
            case A_TAKEOVER: c.userInputTakesOver = !c.userInputTakesOver; break;
            default: break;
        }
    }
    execCmds(cmds);

    switch (a) {
        case A_STOP: {
            std::lock_guard<std::mutex> lk(g_inMu);
            g_input.releaseAll();
            break;
        }
        case A_REC:
            if (haveRecorded) {
                int lvl = levelFromEdit();
                recorded.levelId = lvl;
                if (recorded.events.empty()) { toast(L"Kayıt boş: GD odaktayken Space/Up/W/tık kullan"); break; }
                {
                    std::lock_guard<std::mutex> lk(g_mu);
                    g_eng.loadMacro(recorded);
                }
                saveMacro(recorded, macroPath(lvl));
            } else {
                toast(L"Kayıt başladı: GD'de oyna. Her ölümde yeni deneme, en iyi deneme saklanır. F9 = bitir");
            }
            break;
        case A_IMPORT: {
            wchar_t file[MAX_PATH] = {};
            OPENFILENAMEW of{};
            of.lStructSize = sizeof of;
            of.hwndOwner = g_main;
            of.lpstrFilter = L"GD AutoBot macro (*.gdbm)\0*.gdbm\0All files\0*.*\0";
            of.lpstrFile = file;
            of.nMaxFile = MAX_PATH;
            of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameW(&of)) loadMacroFile(file);
            break;
        }
        case A_EXPORT: {
            Macro m;
            {
                std::lock_guard<std::mutex> lk(g_mu);
                if (!g_eng.hasMacro()) { g_toast = L"Dışa aktarılacak macro yok"; g_toastUntil = winplat::nowUs() + 3000000; break; }
                m = g_eng.macro();
            }
            wchar_t file[MAX_PATH] = L"macro.gdbm";
            OPENFILENAMEW of{};
            of.lStructSize = sizeof of;
            of.hwndOwner = g_main;
            of.lpstrFilter = L"GD AutoBot macro (*.gdbm)\0*.gdbm\0";
            of.lpstrFile = file;
            of.nMaxFile = MAX_PATH;
            of.lpstrDefExt = L"gdbm";
            of.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
            if (GetSaveFileNameW(&of)) saveMacro(m, file);
            break;
        }
        case A_LOADPROF: loadProfile(levelFromEdit()); break;
        case A_SAVEPROF: saveProfile(levelFromEdit()); break;
        case A_CALIB_PLAYER: {
            float rx, ry;
            if (!cursorRel(rx, ry)) { toast(L"Kalibrasyon: fareyi GD içinde oyuncunun üstüne getir, F7'ye bas"); break; }
            std::lock_guard<std::mutex> lk(g_mu);
            if (g_lastW == 0) break;
            Frame f{g_lastFrame.data(), g_lastW, g_lastH, g_lastW * 4, PixelFormat::BGRA, 0};
            Rgb c = samplePixel(f, (int)(rx * g_lastW), (int)(ry * g_lastH));
            g_eng.cfg.playerColor = c;
            g_eng.cfg.playerX = rx;
            wchar_t buf[160];
            swprintf(buf, 160, L"Oyuncu kalibre edildi: renk (%d,%d,%d), x=%.2f", c.r, c.g, c.b, rx);
            g_toast = buf;
            g_toastUntil = winplat::nowUs() + 4000000;
            break;
        }
        case A_CALIB_BARL: case A_CALIB_BARR: case A_CALIB_RETRY: {
            float rx, ry;
            if (!cursorRel(rx, ry)) { toast(L"Kalibrasyon: fare GD penceresinin içinde olmalı"); break; }
            std::lock_guard<std::mutex> lk(g_mu);
            Config& c = g_eng.cfg;
            if (a == A_CALIB_BARL) { c.barX0 = rx; c.barY = ry; g_toast = L"İlerleme çubuğu sol ucu ayarlandı"; }
            else if (a == A_CALIB_BARR) { c.barX1 = rx; c.barY = (c.barY + ry) * 0.5f; g_toast = L"İlerleme çubuğu sağ ucu ayarlandı"; }
            else { c.retryX = rx; c.retryY = ry; g_toast = L"Retry düğmesi konumu ayarlandı"; }
            g_toastUntil = winplat::nowUs() + 4000000;
            break;
        }
        default: break;
    }
    InvalidateRect(g_main, nullptr, FALSE);
}

// ======================= UI drawing =======================
namespace col {
const COLORREF bg = RGB(16, 14, 24), card = RGB(28, 24, 42), card2 = RGB(38, 32, 58);
const COLORREF purple = RGB(139, 92, 246), purpleDk = RGB(76, 52, 140), yellow = RGB(250, 204, 21);
const COLORREF text = RGB(236, 232, 248), muted = RGB(150, 144, 172), red = RGB(239, 68, 68),
               green = RGB(34, 197, 94), dark = RGB(20, 16, 30);
}

struct Button { RECT r; int action; };
static std::vector<Button> g_buttons;
static HFONT g_fTitle, g_fBig, g_fNorm, g_fSmall, g_fBold;

static void roundRect(HDC dc, RECT r, int rad, COLORREF fill, COLORREF border) {
    HBRUSH b = CreateSolidBrush(fill);
    HPEN p = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, rad, rad);
    SelectObject(dc, ob); SelectObject(dc, op);
    DeleteObject(b); DeleteObject(p);
}

static void textAt(HDC dc, int x, int y, const std::wstring& s, COLORREF c, HFONT f) {
    SelectObject(dc, f);
    SetTextColor(dc, c);
    TextOutW(dc, x, y, s.c_str(), (int)s.size());
}

static void textIn(HDC dc, RECT r, const std::wstring& s, COLORREF c, HFONT f, UINT fmt = DT_CENTER | DT_VCENTER | DT_SINGLELINE) {
    SelectObject(dc, f);
    SetTextColor(dc, c);
    DrawTextW(dc, s.c_str(), (int)s.size(), &r, fmt);
}

static void button(HDC dc, RECT r, const std::wstring& label, int action, COLORREF fill, COLORREF fg, HFONT f = nullptr) {
    roundRect(dc, r, 12, fill, fill == col::card2 ? col::purpleDk : fill);
    textIn(dc, r, label, fg, f ? f : g_fBold);
    g_buttons.push_back({r, action});
}

static void kv(HDC dc, int x, int y, const wchar_t* k, const std::wstring& v, COLORREF vc) {
    textAt(dc, x, y, k, col::muted, g_fSmall);
    textAt(dc, x, y + 16, v, vc, g_fBold);
}

static void stepper(HDC dc, int x, int y, const wchar_t* label, const std::wstring& val, int aMinus, int aPlus) {
    RECT box{x, y, x + 258, y + 40};
    roundRect(dc, box, 10, col::card2, col::card2);
    textAt(dc, x + 10, y + 4, label, col::muted, g_fSmall);
    textAt(dc, x + 10, y + 19, val, col::text, g_fBold);
    button(dc, RECT{x + 168, y + 6, x + 206, y + 34}, L"−", aMinus, col::purpleDk, col::text);
    button(dc, RECT{x + 212, y + 6, x + 250, y + 34}, L"+", aPlus, col::purpleDk, col::text);
}

static void toggle(HDC dc, int x, int y, int w, const wchar_t* label, bool on, int action) {
    RECT r{x, y, x + w, y + 32};
    roundRect(dc, r, 10, on ? col::purpleDk : col::card2, on ? col::purple : col::card2);
    textAt(dc, x + 10, y + 7, label, col::text, g_fNorm);
    RECT pill{x + w - 50, y + 7, x + w - 8, y + 25};
    roundRect(dc, pill, 18, on ? col::yellow : RGB(70, 64, 92), on ? col::yellow : RGB(70, 64, 92));
    textIn(dc, pill, on ? L"ON" : L"OFF", on ? col::dark : col::text, g_fSmall);
    g_buttons.push_back({r, action});
}

static void paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC wdc = BeginPaint(hwnd, &ps);
    RECT cr;
    GetClientRect(hwnd, &cr);
    HDC dc = CreateCompatibleDC(wdc);
    HBITMAP bmp = CreateCompatibleBitmap(wdc, cr.right, cr.bottom);
    HGDIOBJ ob = SelectObject(dc, bmp);
    SetBkMode(dc, TRANSPARENT);
    HBRUSH bgb = CreateSolidBrush(col::bg);
    FillRect(dc, &cr, bgb);
    DeleteObject(bgb);
    g_buttons.clear();

    // snapshot engine state
    Config c;
    Status st;
    bool autoPlay, rec, hasMacro;
    BotMode bm;
    float fps, prog;
    int attempts;
    std::string reason;
    Macro mi;
    std::wstring toastText;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        c = g_eng.cfg; st = g_eng.status(); autoPlay = g_eng.autoPlay(); rec = g_eng.recording();
        hasMacro = g_eng.hasMacro(); bm = g_eng.botMode(); fps = g_eng.analysisFps(); prog = g_eng.progress();
        attempts = g_eng.attempts(); reason = g_eng.lastDecision().reason;
        if (hasMacro) { mi.events.reserve(0); mi.levelId = g_eng.macro().levelId; mi.bestProgress = g_eng.macro().bestProgress;
                        mi.gameVersion = g_eng.macro().gameVersion; }
        if (winplat::nowUs() < g_toastUntil) toastText = g_toast;
    }
    size_t macroEvents = 0; double macroSec = 0;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (hasMacro) { macroEvents = g_eng.macro().events.size(); macroSec = g_eng.macro().durationUs() / 1e6; }
    }

    const int W = cr.right;
    int x = 18, y = 14;
    // ---- header ----
    textAt(dc, x, y, L"GD AutoBot", col::text, g_fTitle);
    textAt(dc, x + 168, y + 12, L"Geometry Dash 2.209 · Windows · v" + widen(kBotVersion), col::muted, g_fSmall);
    RECT badge{W - 150, y + 6, W - 18, y + 32};
    roundRect(dc, badge, 14, col::purpleDk, col::purple);
    textIn(dc, badge, L"BOT · açıkça işaretli", col::yellow, g_fSmall);
    y += 50;

    // ---- main status card ----
    RECT card{x, y, W - 18, y + 186};
    roundRect(dc, card, 18, col::card, col::purpleDk);
    int cx = x + 16, cy = y + 14;
    // AUTO PLAY
    textAt(dc, cx, cy, L"AUTO PLAY", col::muted, g_fSmall);
    button(dc, RECT{cx, cy + 18, cx + 230, cy + 66}, autoPlay ? L"ON" : L"OFF", A_AUTOPLAY,
           autoPlay ? col::yellow : col::card2, autoPlay ? col::dark : col::text, g_fBig);
    // BOT MODE
    textAt(dc, cx + 250, cy, L"BOT MODE", col::muted, g_fSmall);
    button(dc, RECT{cx + 250, cy + 18, cx + 362, cy + 66}, L"SMART", A_SET_SMART,
           bm == BotMode::Smart ? col::purple : col::card2, col::text, g_fBold);
    button(dc, RECT{cx + 368, cy + 18, cx + 480, cy + 66}, L"MACRO", A_SET_MACRO,
           bm == BotMode::Macro ? col::purple : col::card2, col::text, g_fBold);
    cy += 82;
    COLORREF stc = st == Status::Playing ? col::green : st == Status::Recording ? col::red
                 : st == Status::Stopped ? col::red : st == Status::Paused ? col::yellow : col::text;
    HWND gw = g_game.load();
    std::wstring gwText = gw ? (std::to_wstring(g_gameW.load()) + L"×" + std::to_wstring(g_gameH.load()))
                             : std::wstring(L"bulunamadı");
    if (gw && g_captureFailing) gwText = L"yakalanamıyor";
    kv(dc, cx, cy, L"GAME VERSION", L"2.209", col::text);
    kv(dc, cx + 120, cy, L"PLATFORM", L"WINDOWS", col::text);
    kv(dc, cx + 240, cy, L"STATUS", widen(statusName(st)), stc);
    kv(dc, cx + 370, cy, L"GD PENCERESİ", gwText, gw ? col::text : col::red);
    cy += 44;
    wchar_t buf[256];
    swprintf(buf, 256, L"%.0f fps (analiz)", fps);
    kv(dc, cx, cy, L"FPS", buf, col::text);
    swprintf(buf, 256, L"%.1f%%", prog * 100.f);
    kv(dc, cx + 120, cy, L"İLERLEME", buf, col::yellow);
    kv(dc, cx + 240, cy, L"DENEME", std::to_wstring(attempts), col::text);
    swprintf(buf, 256, L"%hs %.1fx%hs%hs", gameModeName(c.gameMode), c.speed, c.upsideDown ? " ters" : "", c.mini ? " mini" : "");
    kv(dc, cx + 370, cy, L"OYUN MODU", buf, col::text);
    y += 198;

    // ---- smart controls ----
    button(dc, RECT{x, y, x + 150, y + 34}, L"Mod değiştir (F2)", A_GAMEMODE, col::card2, col::text);
    button(dc, RECT{x + 158, y, x + 290, y + 34}, L"Hız (F3)", A_SPEED, col::card2, col::text);
    button(dc, RECT{x + 298, y, x + 420, y + 34}, L"Yerçekimi (F4)", A_GRAVITY, col::card2, col::text);
    button(dc, RECT{x + 428, y, W - 18, y + 34}, L"Mini", A_MINI, c.mini ? col::purple : col::card2, col::text);
    y += 40;
    std::wstring dec = autoPlay && bm == BotMode::Smart ? L"Karar: " + widen(reason) : L"Karar: —";
    textAt(dc, x + 4, y, dec, col::muted, g_fSmall);
    y += 22;

    // ---- action row ----
    int bw = (W - 36 - 3 * 8) / 4;
    button(dc, RECT{x, y, x + bw, y + 40}, rec ? L"■ KAYDI BİTİR (F9)" : L"● MACRO KAYIT (F9)", A_REC,
           rec ? col::red : col::card2, col::text);
    button(dc, RECT{x + (bw + 8), y, x + 2 * bw + 8, y + 40}, L"SYNC (F5)", A_SYNC, col::card2, col::text);
    button(dc, RECT{x + 2 * (bw + 8), y, x + 3 * bw + 16, y + 40}, st == Status::Paused ? L"DEVAM (F10)" : L"PAUSE (F10)",
           A_PAUSE, col::card2, col::text);
    button(dc, RECT{x + 3 * (bw + 8), y, W - 18, y + 40}, L"ACİL DUR (End)", A_STOP, col::red, col::text);
    y += 50;

    // ---- macro ----
    RECT mcard{x, y, W - 18, y + 48};
    roundRect(dc, mcard, 14, col::card, col::card);
    if (hasMacro) swprintf(buf, 256, L"Macro: %zu olay · %.1f sn · en iyi %%%.0f · GD %hs", macroEvents, macroSec,
                           mi.bestProgress * 100.f, mi.gameVersion.c_str());
    else swprintf(buf, 256, L"Macro: yüklü değil");
    textAt(dc, x + 12, y + 15, buf, hasMacro ? col::text : col::muted, g_fNorm);
    button(dc, RECT{W - 210, y + 8, W - 118, y + 40}, L"İçe aktar", A_IMPORT, col::purpleDk, col::text);
    button(dc, RECT{W - 112, y + 8, W - 26, y + 40}, L"Dışa aktar", A_EXPORT, col::purpleDk, col::text);
    y += 58;

    // ---- settings ----
    swprintf(buf, 256, L"%d ms", c.inputDelayMs);
    stepper(dc, x, y, L"Input Delay (Smart)", buf, A_DELAY_M, A_DELAY_P);
    swprintf(buf, 256, L"%+d ms", c.timingOffsetMs);
    stepper(dc, x + 266, y, L"Timing Offset (Macro)", buf, A_OFFSET_M, A_OFFSET_P);
    y += 46;
    swprintf(buf, 256, L"%d ms", c.tapDurationMs);
    stepper(dc, x, y, L"Dokunma süresi", buf, A_TAP_M, A_TAP_P);
    swprintf(buf, 256, L"%.1f blok", c.lookaheadBlocks);
    stepper(dc, x + 266, y, L"Bakış mesafesi (uçan modlar)", buf, A_LOOK_M, A_LOOK_P);
    y += 46;
    swprintf(buf, 256, L"%.2f sn", c.checkpointIntervalMs / 1000.f);
    stepper(dc, x, y, L"Practice checkpoint aralığı", buf, A_CP_M, A_CP_P);
    y += 48;
    int tw = (W - 36 - 8) / 2;
    toggle(dc, x, y, tw, L"Practice Assist (Z)", c.practiceAssist, A_PRACTICE);
    toggle(dc, x + tw + 8, y, tw, L"Auto Retry", c.autoRetry, A_RETRY);
    y += 38;
    toggle(dc, x, y, tw, L"İlerleme çubuğu senkronu", c.progressSync, A_BARSYNC);
    toggle(dc, x + tw + 8, y, tw, L"Basınca manuele geç", c.userInputTakesOver, A_TAKEOVER);
    y += 44;

    // ---- level profile ----
    textAt(dc, x, y + 9, L"Level ID", col::muted, g_fNorm);
    MoveWindow(g_levelEdit, x + 70, y + 4, 110, 26, TRUE);
    button(dc, RECT{x + 190, y + 2, x + 330, y + 32}, L"Profili yükle", A_LOADPROF, col::purpleDk, col::text);
    button(dc, RECT{x + 338, y + 2, x + 478, y + 32}, L"Profili kaydet", A_SAVEPROF, col::purpleDk, col::text);
    y += 42;

    // ---- calibration ----
    RECT sw{x, y, x + 26, y + 26};
    roundRect(dc, sw, 6, RGB(c.playerColor.r, c.playerColor.g, c.playerColor.b), col::text);
    swprintf(buf, 256, L"Oyuncu rengi (%d,%d,%d) x=%.2f · çubuk %.2f–%.2f · retry %hs", c.playerColor.r, c.playerColor.g,
             c.playerColor.b, c.playerX, c.barX0, c.barX1, c.retryX >= 0 ? "ayarlı" : "ayarsız");
    textAt(dc, x + 34, y + 5, buf, col::muted, g_fSmall);
    y += 32;
    textAt(dc, x, y, L"Kalibrasyon (fare GD içinde): F7 oyuncu · Ctrl+F7 / Ctrl+F8 çubuk sol/sağ · Ctrl+F9 retry düğmesi",
           col::muted, g_fSmall);
    y += 18;
    textAt(dc, x, y, L"Kısayollar: F8 Auto Play · F6 Smart/Macro · F9 kayıt · F5 sync · F10 pause · End acil durdurma",
           col::muted, g_fSmall);
    y += 26;

    if (!toastText.empty()) {
        RECT tr{x, y, W - 18, y + 34};
        roundRect(dc, tr, 12, col::purpleDk, col::yellow);
        textIn(dc, tr, toastText, col::text, g_fNorm);
    }

    BitBlt(wdc, 0, 0, cr.right, cr.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, ob);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(hwnd, &ps);
}

// ======================= overlay ("BOT ACTIVE" indicator on top of GD) =======================
static void updateOverlay() {
    Status st;
    BotMode bm;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        st = g_eng.status(); bm = g_eng.botMode();
    }
    HWND gw = g_game.load();
    RECT r;
    bool show = (st == Status::Playing || st == Status::Recording || st == Status::Paused) &&
                winplat::clientRectOnScreen(gw, r);
    if (!show) { ShowWindow(g_overlay, SW_HIDE); return; }
    SetWindowPos(g_overlay, HWND_TOPMOST, r.left + 8, r.top + 8, 230, 28, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    SetWindowLongPtrW(g_overlay, GWLP_USERDATA, (LONG_PTR)((int)st * 2 + (bm == BotMode::Macro)));
    InvalidateRect(g_overlay, nullptr, FALSE);
}

static LRESULT CALLBACK overlayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT r;
        GetClientRect(hwnd, &r);
        int v = (int)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        Status st = (Status)(v / 2);
        bool macro = v & 1;
        HBRUSH b = CreateSolidBrush(st == Status::Recording ? col::red : col::purpleDk);
        FillRect(dc, &r, b);
        DeleteObject(b);
        SetBkMode(dc, TRANSPARENT);
        std::wstring t = st == Status::Recording ? L"● BOT · MACRO KAYDEDİYOR"
                       : st == Status::Paused ? L"❚❚ BOT · DURAKLATILDI"
                       : (macro ? L"▶ BOT · AUTO PLAY (MACRO)" : L"▶ BOT · AUTO PLAY (SMART)");
        textIn(dc, r, t, col::yellow, g_fBold);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_NCHITTEST) return HTTRANSPARENT;
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ======================= main window =======================
enum Hot { H_AUTOPLAY = 1, H_BOTMODE, H_REC, H_SYNC, H_PAUSE, H_STOP, H_GAMEMODE, H_SPEED, H_GRAVITY, H_CAL_P,
           H_CAL_BL, H_CAL_BR, H_CAL_R, H_MINI };

static void registerHotkeys(HWND h) {
    struct { int id; UINT mod; UINT vk; } hk[] = {
        {H_AUTOPLAY, 0, VK_F8}, {H_BOTMODE, 0, VK_F6}, {H_REC, 0, VK_F9}, {H_SYNC, 0, VK_F5},
        {H_PAUSE, 0, VK_F10}, {H_STOP, 0, VK_END}, {H_GAMEMODE, 0, VK_F2}, {H_SPEED, 0, VK_F3},
        {H_GRAVITY, 0, VK_F4}, {H_MINI, MOD_SHIFT, VK_F4}, {H_CAL_P, 0, VK_F7}, {H_CAL_BL, MOD_CONTROL, VK_F7},
        {H_CAL_BR, MOD_CONTROL, VK_F8}, {H_CAL_R, MOD_CONTROL, VK_F9}};
    std::wstring failed;
    for (auto& k : hk)
        if (!RegisterHotKey(h, k.id, k.mod | MOD_NOREPEAT, k.vk)) failed += L" #" + std::to_wstring(k.id);
    if (!failed.empty()) toast(L"Bazı kısayollar başka program tarafından kullanılıyor:" + failed);
}

static LRESULT CALLBACK mainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            g_levelEdit = CreateWindowExW(0, L"EDIT", L"0", WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER, 0, 0, 100, 24,
                                          hwnd, (HMENU)100, GetModuleHandleW(nullptr), nullptr);
            SendMessageW(g_levelEdit, WM_SETFONT, (WPARAM)g_fBold, TRUE);
            SetTimer(hwnd, 1, 100, nullptr);
            registerHotkeys(hwnd);
            return 0;
        case WM_CTLCOLOREDIT: {
            static HBRUSH eb = CreateSolidBrush(col::card2);
            SetTextColor((HDC)wp, col::yellow);
            SetBkColor((HDC)wp, col::card2);
            return (LRESULT)eb;
        }
        case WM_TIMER:
            InvalidateRect(hwnd, nullptr, FALSE);
            updateOverlay();
            return 0;
        case WM_APP + 1:
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: paint(hwnd); return 0;
        case WM_LBUTTONDOWN: {
            POINT pt{(short)LOWORD(lp), (short)HIWORD(lp)};
            for (auto it = g_buttons.rbegin(); it != g_buttons.rend(); ++it)
                if (PtInRect(&it->r, pt)) {
                    int a = it->action;
                    doAction(a);
                    break;
                }
            return 0;
        }
        case WM_HOTKEY:
            switch ((int)wp) {
                case H_AUTOPLAY: doAction(A_AUTOPLAY); break;
                case H_BOTMODE: doAction(A_BOTMODE); break;
                case H_REC: doAction(A_REC); break;
                case H_SYNC: doAction(A_SYNC); break;
                case H_PAUSE: doAction(A_PAUSE); break;
                case H_STOP: doAction(A_STOP); break;
                case H_GAMEMODE: doAction(A_GAMEMODE); break;
                case H_SPEED: doAction(A_SPEED); break;
                case H_GRAVITY: doAction(A_GRAVITY); break;
                case H_MINI: doAction(A_MINI); break;
                case H_CAL_P: doAction(A_CALIB_PLAYER); break;
                case H_CAL_BL: doAction(A_CALIB_BARL); break;
                case H_CAL_BR: doAction(A_CALIB_BARR); break;
                case H_CAL_R: doAction(A_CALIB_RETRY); break;
            }
            return 0;
        case WM_GETMINMAXINFO: {
            auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
            mm->ptMinTrackSize.x = 600;
            mm->ptMinTrackSize.y = 820;
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    SetProcessDPIAware();
    HANDLE single = CreateMutexW(nullptr, TRUE, L"GDAutoBot_2209_single");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"GD AutoBot zaten çalışıyor.", kTitle, MB_ICONINFORMATION);
        return 0;
    }

    // config
    {
        std::string text = winplat::readFile(defaultCfgPath());
        if (!text.empty()) g_eng.cfg = Config::parse(text);
        std::string keys = winplat::readFile(winplat::exeDir() + L"\\keys.ini");
        // keys.ini: p1=32 / p2=38 / twoPlayer=1 / captureWidth=400  (virtual key codes)
        size_t pos;
        if ((pos = keys.find("p1=")) != std::string::npos) g_input.keys.p1 = (WORD)atoi(keys.c_str() + pos + 3);
        if ((pos = keys.find("p2=")) != std::string::npos) g_input.keys.p2 = (WORD)atoi(keys.c_str() + pos + 3);
        if ((pos = keys.find("twoPlayer=")) != std::string::npos) g_twoPlayerKeys = keys[pos + 10] == '1';
        if ((pos = keys.find("captureWidth=")) != std::string::npos)
            g_captureWidth = std::max(160, std::min(1280, atoi(keys.c_str() + pos + 13)));
    }

    auto mkFont = [](int h, int weight) {
        return CreateFontW(-h, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    };
    g_fTitle = mkFont(30, FW_BOLD);
    g_fBig = mkFont(26, FW_BOLD);
    g_fBold = mkFont(15, FW_SEMIBOLD);
    g_fNorm = mkFont(15, FW_NORMAL);
    g_fSmall = mkFont(12, FW_NORMAL);

    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = mainProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"GDAutoBotMain";
    RegisterClassExW(&wc);
    WNDCLASSEXW oc{sizeof oc};
    oc.lpfnWndProc = overlayProc;
    oc.hInstance = inst;
    oc.lpszClassName = L"GDAutoBotOverlay";
    RegisterClassExW(&oc);

    g_main = CreateWindowExW(0, wc.lpszClassName, kTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 620, 870,
                             nullptr, nullptr, inst, nullptr);
    g_overlay = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                oc.lpszClassName, L"", WS_POPUP, 0, 0, 230, 28, nullptr, nullptr, inst, nullptr);
    SetLayeredWindowAttributes(g_overlay, 0, 230, LWA_ALPHA);
    // keep our own windows out of the screen capture the bot analyses (Win10 2004+)
    SetWindowDisplayAffinity(g_overlay, WDA_EXCLUDEFROMCAPTURE);
    SetWindowDisplayAffinity(g_main, WDA_EXCLUDEFROMCAPTURE);
    ShowWindow(g_main, show);
    UpdateWindow(g_main);

    g_kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, kbProc, inst, 0);
    g_msHook = SetWindowsHookExW(WH_MOUSE_LL, msProc, inst, 0);
    if (!g_kbHook || !g_msHook) toast(L"Uyarı: klavye/fare kancası kurulamadı, macro kaydı çalışmaz");
    else toast(L"Hazır. GD 2.209'u pencereli/kenarlıksız aç, F7 ile oyuncuyu kalibre et.");

    std::thread tCap(captureThread), tTick(tickThread);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    g_running = false;
    tCap.join();
    tTick.join();
    {
        std::lock_guard<std::mutex> lk(g_inMu);
        g_input.releaseAll();
    }
    if (g_kbHook) UnhookWindowsHookEx(g_kbHook);
    if (g_msHook) UnhookWindowsHookEx(g_msHook);
    {
        std::lock_guard<std::mutex> lk(g_mu);
        winplat::writeFile(defaultCfgPath(), g_eng.cfg.serialize());
    }
    ReleaseMutex(single);
    return 0;
}
