#pragma once
#include <cstdint>
#include "gdbot/config.hpp"
#include "gdbot/vision.hpp"

namespace gdbot {

// GD horizontal speed in blocks/second for each speed portal.
float speedBlocksPerSecond(float speed);

struct PilotDecision {
    bool hold = false;       // desired button state for player 1
    bool tapOnly = false;    // mode wants short taps (cube/ufo/ball/spider/swing)
    const char* reason = "";
    float targetY = -1;      // corridor target (debug)
};

// Real-time decision maker. Works purely from the analyzed Scene.
// Deliberately simple and explainable: hazard look-ahead for jump modes,
// corridor-following PD control for flying modes.
class SmartPilot {
public:
    void reset();
    PilotDecision update(const Scene& s, const Config& cfg, int64_t tUs);
    float velocityY() const { return vy_; }

private:
    PilotDecision cube(const Scene& s, const Config& cfg, bool robot);
    PilotDecision flyer(const Scene& s, const Config& cfg);
    PilotDecision flipper(const Scene& s, const Config& cfg);  // ball / spider
    float corridorTarget(const Scene& s, const Config& cfg, float x0, float x1);
    float hazardDensity(const Scene& s, float x0, float x1, float y0, float y1) const;

    bool hasPrev_ = false;
    float prevY_ = 0;
    int64_t prevT_ = 0;
    float vy_ = 0;            // px/s, + = down on screen
    int64_t lastTapUs_ = 0;
    int64_t nowUs_ = 0;
    bool waveHold_ = false;
};

}  // namespace gdbot
