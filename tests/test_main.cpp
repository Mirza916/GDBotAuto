// Host tests for the shared core: unit tests + simulator runs.
// Build: cmake -B build && cmake --build build && ./build/gdbot_tests
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include "gdbot/engine.hpp"
#include "sim.hpp"

using namespace gdbot;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (cond) ++g_pass;                                                         \
        else { ++g_fail; printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

static void testConfig() {
    printf("[config] serialize/parse roundtrip\n");
    Config c;
    c.inputDelayMs = 33; c.timingOffsetMs = -7; c.gameMode = GameMode::Wave; c.speed = 3.f;
    c.playerColor = {12, 200, 34}; c.levelId = 128; c.macroFile = "macros/128.gdbm"; c.practiceAssist = true;
    Config d = Config::parse(c.serialize());
    CHECK(d.inputDelayMs == 33 && d.timingOffsetMs == -7 && d.gameMode == GameMode::Wave);
    CHECK(std::fabs(d.speed - 3.f) < 1e-4 && d.playerColor.g == 200 && d.levelId == 128);
    CHECK(d.macroFile == "macros/128.gdbm" && d.practiceAssist);
    CHECK(Config::parse("garbage\nspeed=\n").speed == 1.0f);
}

static void testMacro() {
    printf("[macro] format, recorder best-take, player timing\n");
    Macro m;
    m.levelId = 55; m.platform = "windows"; m.bestProgress = 1.f;
    m.events = {{100000, true, 0}, {130000, false, 0}, {500000, true, 1}, {900000, false, 1}};
    Macro p;
    std::string err;
    CHECK(Macro::parse(m.serialize(), p, &err));
    CHECK(p.events.size() == 4 && p.events[2].player == 1 && p.levelId == 55 && p.platform == "windows");
    CHECK(!Macro::parse("hello", p, &err));
    CHECK(!Macro::parse("GDBOTMACRO 1\nevents 3\n1 1 0\n", p, &err));   // count mismatch

    MacroRecorder r;
    r.start(0);
    r.onAttemptStart(1000000);
    r.onInput(1100000, true, 0); r.onInput(1100500, true, 0);   // duplicate ignored
    r.onInput(1200000, false, 0);
    r.onProgress(0.30f);
    r.onAttemptStart(5000000);        // attempt 2 reaches farther
    r.onInput(5050000, true, 0); r.onInput(5080000, false, 0);
    r.onInput(5400000, true, 0);      // left held at end -> recorder closes it
    r.onProgress(0.62f);
    r.onAttemptStart(9000000);        // attempt 3 dies early
    r.onInput(9010000, true, 0);
    r.onProgress(0.05f);
    Macro best = r.stop();
    CHECK(std::fabs(best.bestProgress - 0.62f) < 1e-4);
    CHECK(best.events.size() == 4 && best.events[0].tUs == 50000 && !best.events.back().press);

    MacroPlayer pl;
    pl.load(m);
    pl.start(2000000, -5000);
    std::vector<MacroEvent> due;
    CHECK(pl.poll(2094999, due) == 0);
    CHECK(pl.poll(2095000, due) == 1 && due[0].press);
    CHECK(pl.nextEventUs() == 2125000);
    CHECK(pl.poll(10000000, due) == 3 && !pl.running());
}

static void testProgressBar() {
    printf("[vision] progress bar reader + attempt tracker\n");
    sim::SimOptions o;
    sim::Course c = sim::makeCubeCourse(1, 100);
    std::mt19937 rng(3);
    std::vector<uint8_t> img;
    Config cfg;
    ProgressBarReader rd;
    float fr[] = {0.f, 0.10f, 0.37f, 0.80f, 0.999f, 1.0f};
    for (float f : fr) {
        sim::render(c, o, 20, 0, f, img, rng);
        Frame fm{img.data(), o.width, o.height, o.width * 4, PixelFormat::RGBA, 0};
        ProgressReading r = rd.read(fm, cfg);
        printf("    bar %.3f -> read %.3f (valid %d)\n", f, r.fraction, r.valid);
        CHECK(r.valid && std::fabs(r.fraction - f) < 0.03f);
    }
    // tracker: start, progress, death (reset), stall
    AttemptTracker t;
    cfg.retryAfterMs = 1000;
    CHECK(t.update({true, 0.f}, 0, cfg) == AttemptTracker::Event::AttemptStart);
    CHECK(t.update({true, 0.2f}, 100000, cfg) == AttemptTracker::Event::None);
    CHECK(t.update({true, 0.0f}, 200000, cfg) == AttemptTracker::Event::AttemptStart);
    CHECK(t.attempts() == 2);
    t.update({true, 0.3f}, 300000, cfg);
    CHECK(t.update({true, 0.3f}, 1400000, cfg) == AttemptTracker::Event::Stalled);
    CHECK(t.update({true, 1.0f}, 1500000, cfg) == AttemptTracker::Event::LevelComplete);
}

static void testAnalyzer() {
    printf("[vision] scene analyzer on rendered frames\n");
    sim::SimOptions o;
    o.noise = true;
    sim::Course c;
    c.mode = GameMode::Cube;
    c.boxes.push_back({22.35f, 0, 22.65f, 0.5f, true});   // spike ~2 blocks ahead
    std::mt19937 rng(5);
    std::vector<uint8_t> img;
    sim::render(c, o, 20, 0, 0.1f, img, rng);
    Frame fm{img.data(), o.width, o.height, o.width * 4, PixelFormat::RGBA, 0};
    Config cfg;
    Analyzer a;
    const Scene& s = a.analyze(fm, cfg);
    float bpx = o.height * 30.f / 320.f;
    printf("    player found=%d at (%.1f,%.1f) %0.fx%.0f, groundY=%d (expect %.0f), ceilY=%d\n", s.player.found,
           s.player.cx, s.player.cy, s.player.w, s.player.h, s.groundY, o.height * 0.75f, s.ceilY);
    CHECK(s.player.found);
    CHECK(std::fabs(s.player.cx - 0.3f * o.width) < 2.f);
    CHECK(std::fabs(s.player.h - bpx) < 3.f);
    CHECK(std::abs(s.groundY - (int)(o.height * 0.75f)) <= 2);
    // the spike must be marked solid about 2 blocks in front of the player
    int sx = (int)(0.3f * o.width + 2.5f * bpx), sy = (int)(o.height * 0.75f - 0.3f * bpx);
    CHECK(s.isSolid(sx, sy));
    // background just above the spike is not
    CHECK(!s.isSolid(sx, (int)(o.height * 0.75f - 2.5f * bpx)));
}

static void testEngineControls() {
    printf("[engine] emergency stop, takeover, pause, practice assist\n");
    BotEngine e;
    std::vector<InputCmd> out;
    e.cfg.tapDurationMs = 30;
    e.setAutoPlay(true, 0, out);
    CHECK(e.status() == Status::Playing);
    // fake a smart tap via macro path: load macro & manual sync
    Macro m; m.events = {{10000, true, 0}, {500000, false, 0}};
    e.loadMacro(m);
    e.setBotMode(BotMode::Macro, 0, out);
    e.manualSync(0, out);
    out.clear();
    e.onTick(10000, out);
    CHECK(out.size() == 1 && out[0].type == CmdType::Press);
    out.clear();
    e.emergencyStop(out);
    CHECK(out.size() == 1 && out[0].type == CmdType::Release);   // held input released immediately
    CHECK(e.status() == Status::Stopped && !e.autoPlay());
    out.clear();
    e.setAutoPlay(true, 600000, out);
    CHECK(e.onUserInput(700000, true, 0, out) && !e.autoPlay());   // user takes over
    e.togglePause(0, out);
    CHECK(e.status() == Status::Paused);
    e.togglePause(0, out);
    BotEngine p;
    p.cfg.practiceAssist = true; p.cfg.checkpointIntervalMs = 1000;
    p.setAutoPlay(true, 0, out);
    out.clear();
    int cps = 0;
    for (int64_t t = 1; t <= 3500000; t += 1000) { p.onTick(t, out); }
    for (auto& c : out) cps += c.type == CmdType::PlaceCheckpoint;
    CHECK(cps == 3);
}

static void simSuite(const char* name, sim::Course (*make)(uint32_t, float), int runs, sim::SimOptions o) {
    int ok = 0;
    float dist = 0;
    for (int i = 0; i < runs; ++i) {
        sim::Course c = make(1000 + i, 150.f);
        o.seed = 77 + i;
        sim::RunResult r = sim::runSmart(c, o);
        ok += r.completed;
        dist += r.distance;
    }
    printf("    %-5s speed %.1fx, capture %3.0f Hz, latency %2d ms: %2d/%d completed, avg distance %.1f / 150 blocks\n",
           name, o.speed, o.captureHz, o.latencyMs, ok, runs, dist / runs);
}

int main(int argc, char** argv) {
    bool quick = argc > 1 && !strcmp(argv[1], "--quick");
    testConfig();
    testMacro();
    testProgressBar();
    testAnalyzer();
    testEngineControls();

    printf("[sim] record a macro through the engine (attempt 1 dies, attempt 2 completes), then replay it\n");
    int recOk = 0, repOk = 0, repTotal = 0;
    for (int i = 0; i < (quick ? 2 : 8); ++i) {
        sim::Course c = sim::makeCubeCourse(500 + i, 120.f);
        sim::SimOptions o;
        o.seed = 900 + i;
        o.latencyMs = 5;
        Macro m;
        sim::RunResult rr = sim::recordSmart(c, o, m);
        if (!rr.completed) continue;
        ++recOk;
        for (int k = 0; k < 3; ++k) {
            o.seed = 1234 + i * 10 + k;  // different capture phase => different sync error
            sim::RunResult pr = sim::runMacro(c, o, m, true);
            ++repTotal;
            repOk += pr.completed;
        }
        CHECK(m.events.size() > 2 && m.bestProgress > 0.9f);
    }
    printf("    recorded takes that completed: %d, replays completed: %d/%d (progress-bar sync, 60 Hz capture)\n",
           recOk, repOk, repTotal);
    CHECK(recOk > 0 && repTotal > 0 && repOk * 10 >= repTotal * 8);

    printf("[sim] Smart Auto Play on generated courses (simplified physics, NOT real GD)\n");
    int runs = quick ? 4 : 20;
    sim::SimOptions o;
    simSuite("cube", sim::makeCubeCourse, runs, o);
    simSuite("ship", sim::makeShipCourse, runs, o);
    simSuite("wave", sim::makeWaveCourse, runs, o);
    o.latencyMs = 50;
    simSuite("cube", sim::makeCubeCourse, runs, o);
    simSuite("ship", sim::makeShipCourse, runs, o);
    o.latencyMs = 20; o.captureHz = 30;
    simSuite("cube", sim::makeCubeCourse, runs, o);
    simSuite("wave", sim::makeWaveCourse, runs, o);

    printf("\nRESULT: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
