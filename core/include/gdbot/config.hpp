#pragma once
#include <string>
#include "gdbot/types.hpp"

namespace gdbot {

// All tunables. Serialized as simple `key=value` lines so a profile per level ID
// is just a small text file (profiles/<levelId>.cfg).
struct Config {
    // ---- timing ----
    int inputDelayMs = 20;        // estimated capture+input latency, Smart mode looks this far ahead
    int timingOffsetMs = 0;       // macro replay shift (+ = later, - = earlier)
    int tapDurationMs = 30;       // how long a "tap" keeps the button down

    // ---- smart auto play ----
    GameMode gameMode = GameMode::Cube;  // current gamemode (hotkey / panel switches it)
    float speed = 1.0f;           // 0.5, 1, 2, 3, 4  (GD speed portals)
    bool mini = false;
    bool upsideDown = false;      // gravity flipped (hotkey / panel)
    bool dual = false;            // dual: only player 1 is driven by Smart mode
    Rgb playerColor{0, 255, 0};   // calibrated main color of the player icon
    float playerX = 0.30f;        // relative x of the player on screen (calibrated)
    int colorTolerance = 70;      // sum-of-abs RGB distance for player color match
    int obstacleThreshold = 90;   // sum-of-abs distance from row background => solid
    float lookaheadBlocks = 1.6f; // how far ahead (in blocks) to scan for hazards (flying modes)
    int jumpAirtimeMs = 420;      // cube jump air time, used to centre jumps over hazards
    float blockRel = 30.0f / 320.0f; // block size relative to frame height (GD: 30pt of 320pt)
    float uiBandRel = 0.07f;      // top band ignored for obstacles (progress bar, %)

    // ---- progress bar (attempt sync) ----
    bool progressSync = true;
    float barX0 = 0.32f, barX1 = 0.68f, barY = 0.022f;  // relative, calibrated
    int barThreshold = 60;

    // ---- practice assist ----
    bool practiceAssist = false;
    int checkpointIntervalMs = 2000;
    float cpPlaceX = -1, cpPlaceY = -1;   // Android: on-screen checkpoint button
    float cpRemoveX = -1, cpRemoveY = -1;

    // ---- auto retry ----
    bool autoRetry = true;
    int retryAfterMs = 2500;      // progress frozen this long (not at 100%) => retry screen
    float retryX = -1, retryY = -1;  // relative position of the retry button (calibrated)

    // ---- manual takeover ----
    bool userInputTakesOver = true;  // a real user press pauses the bot instantly

    // ---- profile ----
    int levelId = 0;
    std::string macroFile;

    std::string serialize() const;
    // Unknown keys are ignored, bad values keep defaults.
    static Config parse(const std::string& text);
};

}  // namespace gdbot
