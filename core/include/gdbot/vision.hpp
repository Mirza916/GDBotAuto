#pragma once
#include <cstdint>
#include <vector>
#include "gdbot/config.hpp"
#include "gdbot/types.hpp"

namespace gdbot {

// ---------- progress bar / attempt detection ----------
struct ProgressReading {
    bool valid = false;
    float fraction = 0.f;
};

// Reads the GD progress bar (top center). Color agnostic: it compares every
// column with the empty "track" color sampled at the right end. It remembers the
// track color so a 100% (completely filled) bar is not mistaken for 0%.
class ProgressBarReader {
public:
    ProgressReading read(const Frame& f, const Config& cfg);
    void reset() { known_ = false; }

private:
    Rgb track_{};
    bool known_ = false;
};

// Detects respawns independent of the progress bar: remembers a tiny grayscale
// thumbnail of the attempt's first frame; when the screen first diverges and
// then matches it again, the level was reset (death in normal mode).
class StartFrameMatcher {
public:
    void capture(const Frame& f);
    // true when a reset back to the start frame is detected
    bool update(const Frame& f);
    bool hasStart() const { return has_; }
    float lastDiff() const { return lastDiff_; }

private:
    static constexpr int TW = 32, TH = 18;
    void thumb(const Frame& f, uint8_t* out) const;
    uint8_t start_[TW * TH]{};
    bool has_ = false;
    bool diverged_ = false;
    int64_t startUs_ = 0;
    float lastDiff_ = 0;
};

class AttemptTracker {
public:
    enum class Event { None, AttemptStart, LevelComplete, Stalled };
    Event update(const ProgressReading& r, int64_t tUs, const Config& cfg);
    float fraction() const { return last_; }
    float maxFraction() const { return max_; }
    int attempts() const { return attempts_; }
    bool inAttempt() const { return inAttempt_; }
    int64_t attemptStartUs() const { return startUs_; }
    void reset() { *this = AttemptTracker(); }

private:
    float last_ = -1.f;
    float max_ = 0.f;
    bool inAttempt_ = false;
    bool completed_ = false;
    bool stalledFired_ = false;
    int attempts_ = 0;
    int64_t startUs_ = 0;
    int64_t lastChangeUs_ = 0;
    int invalidFrames_ = 0;
};

// ---------- scene analysis ----------
struct PlayerInfo {
    bool found = false;
    float cx = 0, cy = 0, w = 0, h = 0;  // pixels in frame space
    int pixels = 0;
};

struct Scene {
    int w = 0, h = 0;
    PlayerInfo player;
    int groundY = 0;     // first floor row (h if no floor visible)
    int ceilY = 0;       // first row below ceiling (uiBand if none)
    int uiY = 0;         // rows above this are UI (ignored)
    float blockPx = 0;
    std::vector<uint8_t> solid;  // w*h, 1 = something that is not background
    bool isSolid(int x, int y) const {
        return x >= 0 && y >= 0 && x < w && y < h && solid[(size_t)y * w + x];
    }
};

class Analyzer {
public:
    const Scene& analyze(const Frame& f, const Config& cfg);
    const Scene& scene() const { return scene_; }

private:
    Scene scene_;
    std::vector<uint8_t> rgb_;       // packed RGB copy
    std::vector<uint8_t> rowMed_;    // per-row median (RGB)
    std::vector<uint8_t> rowBg_;     // per-row background estimate (RGB)
    std::vector<float> rowBgFrac_;   // share of each row that is background
};

// Reads a pixel (RGB) from a frame, for calibration.
Rgb samplePixel(const Frame& f, int x, int y);

}  // namespace gdbot
