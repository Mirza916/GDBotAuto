#include "gdbot/pilot.hpp"
#include <algorithm>
#include <cmath>

namespace gdbot {

float speedBlocksPerSecond(float speed) {
    // GD horizontal speeds (units/s / 30 units per block)
    if (speed <= 0.6f) return 251.16f / 30.f;   // 0.5x
    if (speed <= 1.5f) return 311.58f / 30.f;   // 1x
    if (speed <= 2.5f) return 387.42f / 30.f;   // 2x
    if (speed <= 3.5f) return 468.00f / 30.f;   // 3x
    return 576.00f / 30.f;                      // 4x
}

void SmartPilot::reset() {
    hasPrev_ = false;
    vy_ = 0;
    lastTapUs_ = 0;
    waveHold_ = false;
}

float SmartPilot::hazardDensity(const Scene& s, float x0, float x1, float y0, float y1) const {
    int xa = std::max(0, (int)x0), xb = std::min(s.w, (int)std::ceil(x1));
    int ya = std::max(0, (int)y0), yb = std::min(s.h, (int)std::ceil(y1));
    if (xb <= xa || yb <= ya) return 0.f;
    int n = 0;
    for (int y = ya; y < yb; ++y)
        for (int x = xa; x < xb; ++x) n += s.solid[(size_t)y * s.w + x];
    return n / (float)((xb - xa) * (yb - ya));
}

PilotDecision SmartPilot::update(const Scene& s, const Config& cfg, int64_t tUs) {
    nowUs_ = tUs;
    PilotDecision d;
    if (!s.player.found) {
        d.reason = "player not found (calibrate color)";
        hasPrev_ = false;
        return d;
    }
    if (hasPrev_ && tUs > prevT_) {
        float dt = (tUs - prevT_) / 1e6f;
        float v = (s.player.cy - prevY_) / dt;
        vy_ = 0.5f * vy_ + 0.5f * v;   // light smoothing
    }
    prevY_ = s.player.cy;
    prevT_ = tUs;
    hasPrev_ = true;

    switch (cfg.gameMode) {
        case GameMode::Cube: return cube(s, cfg, false);
        case GameMode::Robot: return cube(s, cfg, true);
        case GameMode::Ball:
        case GameMode::Spider: return flipper(s, cfg);
        default: return flyer(s, cfg);
    }
}

// Look-ahead window start/end in pixels (compensates latency by speed)
static void lookWindow(const Scene& s, const Config& cfg, float& x0, float& x1) {
    const float b = s.blockPx;
    const float pxPerSec = speedBlocksPerSecond(cfg.speed) * b;
    const float lat = cfg.inputDelayMs / 1000.f;
    x0 = s.player.cx + s.player.w * 0.5f + pxPerSec * lat;
    x1 = x0 + cfg.lookaheadBlocks * b * std::max(1.f, cfg.speed);
}

// Finds the first group of hazards in front of the player at body height.
// Returns false if nothing within `maxAhead` pixels. Columns closer than half a block merge.
static bool firstHazardGroup(const Scene& s, float xStart, float maxAhead, float y0, float y1,
                             float& gx0, float& gx1) {
    const int xa = std::max(0, (int)xStart), xb = std::min(s.w, (int)(xStart + maxAhead));
    const int ya = std::max(0, (int)y0), yb = std::min(s.h, (int)std::ceil(y1));
    if (xb <= xa || yb <= ya) return false;
    const int minPix = std::max(1, (yb - ya) / 12);
    const int mergeGap = std::max(2, (int)(s.blockPx * 0.5f));
    int first = -1, last = -1, gap = 0;
    for (int x = xa; x < xb; ++x) {
        int n = 0;
        for (int y = ya; y < yb; ++y) n += s.solid[(size_t)y * s.w + x];
        if (n >= minPix) {
            if (first < 0) first = x;
            last = x;
            gap = 0;
        } else if (first >= 0 && ++gap > mergeGap) {
            break;
        }
    }
    if (first < 0) return false;
    gx0 = (float)first;
    gx1 = (float)last + 1;
    return true;
}

PilotDecision SmartPilot::cube(const Scene& s, const Config& cfg, bool robot) {
    PilotDecision d;
    d.tapOnly = !robot;
    const PlayerInfo& p = s.player;
    const float b = s.blockPx;
    const float pxPerSec = speedBlocksPerSecond(cfg.speed) * b;
    const float lat = cfg.inputDelayMs / 1000.f;
    // where the player will really be when the press reaches the game
    const float pxAtInput = p.cx + pxPerSec * lat;
    const float jumpLen = pxPerSec * cfg.jumpAirtimeMs / 1000.f * (cfg.mini ? 0.8f : 1.f);
    const float y0 = p.cy - p.h * 0.5f + 1, y1 = p.cy + p.h * 0.5f - 1;
    float gx0, gx1;
    if (!firstHazardGroup(s, p.cx + p.w * 0.5f, jumpLen * 1.5f + pxPerSec * lat, y0, y1, gx0, gx1)) {
        d.reason = "clear";
        return d;
    }
    // Centre the jump arc over the hazard group: take off when the group's centre
    // is half a jump ahead. Wide groups: take off as late as still clears the start.
    const float centre = (gx0 + gx1) * 0.5f;
    const float width = gx1 - gx0;
    float takeoff = centre - jumpLen * 0.5f;
    if (width > jumpLen * 0.8f) takeoff = gx0 - p.w * 0.9f;   // too wide to centre: earliest safe
    if (pxAtInput >= takeoff) {
        d.hold = true;
        d.reason = width > jumpLen * 0.8f ? "wide hazard -> jump early" : "hazard ahead -> jump";
    } else {
        d.reason = "hazard ahead, waiting for jump point";
    }
    if (robot) {
        // robot: hold longer for taller obstacles
        float gy0, gy1;
        if (firstHazardGroup(s, p.cx + p.w * 0.5f, jumpLen, y0 - b, y0, gy0, gy1) && d.hold)
            d.reason = "robot: tall obstacle, long hold";
        else if (!d.hold && vy_ < 0 && firstHazardGroup(s, p.cx, jumpLen * 0.5f, y0 - b, y1, gy0, gy1)) {
            d.hold = true;
            d.reason = "robot: extend jump";
        }
    }
    return d;
}

float SmartPilot::corridorTarget(const Scene& s, const Config& cfg, float xa, float xb) {
    const int cols = 6;
    const float minGap = std::max(4.f, s.player.h * 1.1f);
    float ref = s.player.cy;
    float acc = 0, wsum = 0;
    const int top = s.uiY, bot = s.h;
    for (int c = 0; c < cols; ++c) {
        int x = (int)(xa + (xb - xa) * c / (cols - 1));
        if (x < 0 || x >= s.w) continue;
        // free runs in this column
        float bestCenter = -1, bestScore = 1e9f;
        int y = top;
        while (y < bot) {
            while (y < bot && s.solid[(size_t)y * s.w + x]) ++y;
            int ys = y;
            while (y < bot && !s.solid[(size_t)y * s.w + x]) ++y;
            int ye = y;
            if (ye - ys >= minGap) {
                float center = (ys + ye) * 0.5f;
                // distance from reference, 0 if reference lies inside the run
                float dd = (ref < ys) ? ys - ref : (ref > ye ? ref - ye : 0.f);
                float score = dd * 3.f + std::fabs(center - ref) * 0.25f;
                if (score < bestScore) { bestScore = score; bestCenter = center; }
            }
        }
        if (bestCenter >= 0) {
            float wgt = 1.f + c;   // farther columns matter more
            acc += bestCenter * wgt;
            wsum += wgt;
            ref = bestCenter;      // follow the corridor
        }
    }
    (void)cfg;
    return wsum > 0 ? acc / wsum : s.player.cy;
}

// Free vertical span (screen rows) around `y` that stays free for every column in [xa, xb].
static bool nearFreeSpan(const Scene& s, float xa, float xb, float y, float& lo, float& hi) {
    int ia = std::max(0, (int)xa), ib = std::min(s.w - 1, (int)xb);
    int yc = std::clamp((int)y, s.uiY, s.h - 1);
    if (ib < ia) return false;
    int top = s.uiY, bot = s.h;
    for (int x = ia; x <= ib; ++x) {
        int yy = yc;
        while (yy > s.uiY && !s.solid[(size_t)(yy - 1) * s.w + x]) --yy;
        top = std::max(top, yy);
        yy = yc;
        while (yy < s.h - 1 && !s.solid[(size_t)(yy + 1) * s.w + x]) ++yy;
        bot = std::min(bot, yy + 1);
    }
    if (bot <= top) return false;
    lo = (float)top; hi = (float)bot;
    return true;
}

PilotDecision SmartPilot::flyer(const Scene& s, const Config& cfg) {
    PilotDecision d;
    float x0, x1;
    lookWindow(s, cfg, x0, x1);
    const float b = s.blockPx;
    float target = corridorTarget(s, cfg, x0, x1 + b);
    // Never steer out of the corridor we are in right now: clamp the far target
    // into the free space directly in front of the player.
    {
        float lo = 0, hi = (float)s.h;
        if (nearFreeSpan(s, s.player.cx + s.player.w * 0.5f + 1, x0 + 0.5f * b, s.player.cy, lo, hi)) {
            float margin = s.player.h * 0.5f + 0.2f * b;
            if (hi - lo > 2 * margin) target = std::clamp(target, lo + margin, hi - margin);
            else target = (lo + hi) * 0.5f;
        }
    }
    d.targetY = target;
    // err > 0 => player is below the target (needs to go "up" in its gravity)
    // predict where the player is when our input lands (latency compensation)
    const float lat = cfg.inputDelayMs / 1000.f;
    float err = (s.player.cy + vy_ * lat) - target;
    float vdown = vy_;
    if (cfg.upsideDown) { err = -err; vdown = -vdown; }
    const int64_t sinceTap = nowUs_ - lastTapUs_;

    switch (cfg.gameMode) {
        case GameMode::Ship: {
            float u = err + 0.16f * vdown;   // PD: position + velocity damping
            d.hold = u > 0;
            d.reason = d.hold ? "ship: climb to corridor" : "ship: descend";
            break;
        }
        case GameMode::Wave: {
            float db = (cfg.mini ? 0.15f : 0.25f) * b;
            if (err > db) waveHold_ = true;
            else if (err < -db) waveHold_ = false;
            d.hold = waveHold_;
            d.reason = d.hold ? "wave: up" : "wave: down";
            break;
        }
        case GameMode::Ufo: {
            d.tapOnly = true;
            if (err + 0.08f * vdown > 0.2f * b && vdown > -b && sinceTap > 120000) {
                d.hold = true; lastTapUs_ = nowUs_; d.reason = "ufo: flap";
            } else d.reason = "ufo: glide";
            break;
        }
        case GameMode::Swing: {
            d.tapOnly = true;
            bool wrongWay = (err > 0.25f * b && vdown > 0) || (err < -0.25f * b && vdown < 0);
            if (wrongWay && sinceTap > 150000) {
                d.hold = true; lastTapUs_ = nowUs_; d.reason = "swing: flip toward corridor";
            } else d.reason = "swing: keep";
            break;
        }
        default: break;
    }
    return d;
}

PilotDecision SmartPilot::flipper(const Scene& s, const Config& cfg) {
    PilotDecision d;
    d.tapOnly = true;
    float x0, x1;
    lookWindow(s, cfg, x0, x1);
    const PlayerInfo& p = s.player;
    const float b = s.blockPx;
    float dens = hazardDensity(s, x0, x1, p.cy - p.h * 0.5f + 1, p.cy + p.h * 0.5f - 1);
    const bool resting = std::fabs(vy_) < b * 1.5f;
    const int64_t sinceTap = nowUs_ - lastTapUs_;
    if (dens > 0.04f && resting && sinceTap > 200000) {
        // flip only if the other side has room in front of us
        float target = corridorTarget(s, cfg, x0, x1);
        d.targetY = target;
        if (std::fabs(target - p.cy) > p.h * 0.5f || dens > 0.25f) {
            d.hold = true;
            lastTapUs_ = nowUs_;
            d.reason = "flip gravity: hazard ahead";
            return d;
        }
    }
    d.reason = dens > 0.04f ? "hazard ahead, no better side" : "clear";
    return d;
}

}  // namespace gdbot
