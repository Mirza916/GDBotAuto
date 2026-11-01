// A tiny GD-like simulator used ONLY to test the bot core on the host.
// It is a simplification (box hitboxes, approximate physics), not Geometry Dash.
#pragma once
#include <cstdint>
#include <random>
#include <vector>
#include "gdbot/engine.hpp"

namespace sim {

struct Box { float x0, y0, x1, y1; bool deadly; };  // world blocks, y up, ground top = 0

struct Course {
    gdbot::GameMode mode = gdbot::GameMode::Cube;
    float length = 150.f;
    float ceiling = 1e9f;          // world y of ceiling (flying sections)
    std::vector<Box> boxes;
};

Course makeCubeCourse(uint32_t seed, float length);
Course makeShipCourse(uint32_t seed, float length);
Course makeWaveCourse(uint32_t seed, float length);

struct RunResult {
    bool completed = false;
    float distance = 0.f;      // blocks travelled before death / end
    int framesAnalyzed = 0;
    int presses = 0;
};

struct SimOptions {
    int width = 320, height = 180;
    float captureHz = 60.f;
    int latencyMs = 20;           // real latency from capture to input applied
    float speed = 1.0f;
    bool noise = true;
    uint32_t seed = 1;
};

// Renders the course and lets BotEngine play it in Smart mode.
RunResult runSmart(const Course& c, const SimOptions& o);

// Plays the course with a fixed input timeline (macro) through BotEngine in Macro mode.
// syncJitterUs: random error added to the detected attempt start.
RunResult runMacro(const Course& c, const SimOptions& o, const gdbot::Macro& m, bool viaProgressBar);

// Records the inputs of a Smart run (to produce a known-good macro).
RunResult recordSmart(const Course& c, const SimOptions& o, gdbot::Macro& out);

// Renders one frame (RGBA) for given player state, exposed for unit tests.
void render(const Course& c, const SimOptions& o, float px, float py, float progress,
            std::vector<uint8_t>& rgba, std::mt19937& rng);

}  // namespace sim
