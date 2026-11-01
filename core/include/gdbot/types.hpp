// GD AutoBot - shared core types
// Target: Geometry Dash 2.209. The core never touches game memory or files:
// it only sees screen frames and produces input commands for the platform layer.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace gdbot {

constexpr const char* kTargetGameVersion = "2.209";
constexpr const char* kBotVersion = "0.1.0";

enum class PixelFormat { RGBA, BGRA };

// A captured, usually downscaled, screen frame of the game area.
struct Frame {
    const uint8_t* data = nullptr;
    int width = 0;
    int height = 0;
    int stride = 0;          // bytes per row
    PixelFormat format = PixelFormat::RGBA;
    int64_t timeUs = 0;      // monotonic capture time
};

enum class GameMode { Cube = 0, Ship, Ball, Ufo, Wave, Robot, Spider, Swing, Count };
const char* gameModeName(GameMode m);
GameMode gameModeFromName(const std::string& s);

enum class BotMode { Smart = 0, Macro };
const char* botModeName(BotMode m);

enum class Status { Ready = 0, Playing, Recording, Paused, Stopped };
const char* statusName(Status s);

struct Rgb {
    uint8_t r = 0, g = 0, b = 0;
};

// Commands the platform layer must execute.
enum class CmdType {
    Press,              // press jump/click for `player`
    Release,            // release jump/click for `player`
    PlaceCheckpoint,    // practice: place checkpoint (PC: Z key, Android: tap calibrated point)
    RemoveCheckpoint,   // practice: remove checkpoint (PC: X key)
    RetryClick          // click/tap the retry button at cfg.retryX/retryY
};

struct InputCmd {
    CmdType type = CmdType::Press;
    int player = 0;     // 0 = player 1, 1 = player 2 (dual / 2-player levels)
};

}  // namespace gdbot
