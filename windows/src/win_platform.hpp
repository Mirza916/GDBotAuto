// Windows platform layer: find the GD window, capture it, send input.
// No game files are touched and nothing is injected into the game process.
#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include "gdbot/types.hpp"

namespace winplat {

int64_t nowUs();                       // QueryPerformanceCounter based

// ---- game window ----
HWND findGameWindow();                 // top level window titled "Geometry Dash"
bool clientRectOnScreen(HWND hwnd, RECT& out);
bool isGameFocused(HWND hwnd);

// ---- capture (GDI, from the screen => works for windowed / borderless) ----
class Capturer {
public:
    ~Capturer();
    // Captures the client area of `hwnd`, downscaled to maxWidth (keeps aspect).
    bool capture(HWND hwnd, int maxWidth, gdbot::Frame& out);
    int width() const { return w_; }
    int height() const { return h_; }
    RECT lastRect() const { return rect_; }

private:
    void ensure(int w, int h);
    HDC memDc_ = nullptr;
    HBITMAP dib_ = nullptr;
    HGDIOBJ old_ = nullptr;
    uint8_t* bits_ = nullptr;
    int w_ = 0, h_ = 0;
    RECT rect_{};
};

// ---- input (SendInput, only while GD has focus) ----
struct KeyMap {
    WORD p1 = VK_SPACE;     // jump player 1
    WORD p2 = VK_UP;        // jump player 2 (2-player levels)
    WORD placeCheckpoint = 'Z';
    WORD removeCheckpoint = 'X';
};

class InputSender {
public:
    KeyMap keys;
    // Returns false if not sent (game not focused).
    bool press(HWND game, int player);
    bool release(HWND game, int player);
    bool tapKey(HWND game, WORD vk);
    bool clickRelative(HWND game, float rx, float ry);   // retry button etc.
    void releaseAll();
    bool isDown(int player) const { return down_[player & 1]; }

private:
    static void sendKey(WORD vk, bool up);
    bool down_[2] = {false, false};
};

std::wstring exeDir();
std::string readFile(const std::wstring& path);
bool writeFile(const std::wstring& path, const std::string& data);

}  // namespace winplat
