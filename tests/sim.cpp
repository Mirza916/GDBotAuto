#include "sim.hpp"
#include <algorithm>
#include <cmath>
#include <deque>
#include <functional>
#include <cstdio>
#include <cstdlib>

using namespace gdbot;

namespace sim {

// ---------------- courses ----------------

Course makeCubeCourse(uint32_t seed, float length) {
    std::mt19937 rng(seed);
    Course c;
    c.mode = GameMode::Cube;
    c.length = length;
    float x = 10.f;
    std::uniform_int_distribution<int> kind(0, 9), count(1, 3);
    std::uniform_real_distribution<float> gap(6.f, 10.f);
    while (x < length - 10.f) {
        int k = kind(rng);
        if (k < 7) {  // spike group (GD spike hitbox is much smaller than its sprite)
            int n = count(rng);
            for (int i = 0; i < n; ++i) c.boxes.push_back({x + i + 0.35f, 0.f, x + i + 0.65f, 0.5f, true});
            x += n;
        } else {      // single block (landable)
            c.boxes.push_back({x, 0.f, x + 1.f, 1.f, false});
            x += 1;
        }
        x += gap(rng);
    }
    return c;
}

Course makeShipCourse(uint32_t seed, float length) {
    std::mt19937 rng(seed);
    Course c;
    c.mode = GameMode::Ship;
    c.length = length;
    c.ceiling = 9.f;
    float center = 4.5f, gapH = 4.0f;
    std::uniform_real_distribution<float> step(-1.5f, 1.5f);
    for (float x = 12.f; x < length - 6.f; x += 3.f) {
        center = std::clamp(center + step(rng), 2.2f, 6.8f);
        float lo = center - gapH / 2, hi = center + gapH / 2;
        if (lo > 0.f) c.boxes.push_back({x, 0.f, x + 3.f, lo, true});
        if (hi < c.ceiling) c.boxes.push_back({x, hi, x + 3.f, c.ceiling, true});
    }
    return c;
}

Course makeWaveCourse(uint32_t seed, float length) {
    std::mt19937 rng(seed);
    Course c;
    c.mode = GameMode::Wave;
    c.length = length;
    c.ceiling = 9.f;
    float center = 4.5f, gapH = 3.5f;
    std::uniform_int_distribution<int> step(-1, 1);
    for (float x = 12.f; x < length - 6.f; x += 2.f) {
        center = std::clamp(center + step(rng) * 0.75f, 2.5f, 6.5f);
        float lo = center - gapH / 2, hi = center + gapH / 2;
        c.boxes.push_back({x, 0.f, x + 2.f, lo, true});
        c.boxes.push_back({x, hi, x + 2.f, c.ceiling, true});
    }
    return c;
}

// ---------------- rendering ----------------

struct Cam {
    float bpx, groundSy, playerSx;
};

static Cam camera(const Course& c, const SimOptions& o) {
    Cam k;
    k.bpx = o.height * 30.f / 320.f;
    k.groundSy = (c.ceiling < 1e8f) ? o.height - 0.5f * k.bpx : o.height * 0.75f;
    k.playerSx = 0.30f * o.width;
    return k;
}

static void fillRect(std::vector<uint8_t>& img, int W, int H, float x0, float y0, float x1, float y1,
                     uint8_t r, uint8_t g, uint8_t b) {
    int xa = std::max(0, (int)std::floor(x0)), xb = std::min(W, (int)std::ceil(x1));
    int ya = std::max(0, (int)std::floor(y0)), yb = std::min(H, (int)std::ceil(y1));
    for (int y = ya; y < yb; ++y)
        for (int x = xa; x < xb; ++x) {
            uint8_t* p = &img[((size_t)y * W + x) * 4];
            p[0] = r; p[1] = g; p[2] = b; p[3] = 255;
        }
}

void render(const Course& c, const SimOptions& o, float px, float py, float progress,
            std::vector<uint8_t>& img, std::mt19937& rng) {
    const int W = o.width, H = o.height;
    img.assign((size_t)W * H * 4, 255);
    Cam k = camera(c, o);
    // background gradient (GD-like blue)
    for (int y = 0; y < H; ++y) fillRect(img, W, H, 0, (float)y, (float)W, (float)y + 1, 30, 50 + y * 20 / H, 150);
    auto sx = [&](float wx) { return k.playerSx + (wx - px) * k.bpx; };
    auto sy = [&](float wy) { return k.groundSy - wy * k.bpx; };
    // floor + ceiling
    fillRect(img, W, H, 0, sy(0), (float)W, (float)H, 10, 20, 70);
    fillRect(img, W, H, 0, sy(0) - 1, (float)W, sy(0) + 1, 230, 230, 255);   // floor line
    if (c.ceiling < 1e8f) fillRect(img, W, H, 0, 0, (float)W, sy(c.ceiling), 10, 20, 70);
    // objects
    for (const Box& b : c.boxes) {
        float x0 = sx(b.x0), x1 = sx(b.x1);
        if (x1 < 0 || x0 > W) continue;
        if (b.deadly && b.y1 - b.y0 <= 0.6f) {
            // spike: draw full 1-block triangle sprite around the small hitbox
            float cx = (x0 + x1) * 0.5f, base = sy(0), top = sy(1);
            for (int y = (int)top; y < (int)base; ++y) {
                float t = (y - top) / (base - top);
                float half = t * 0.5f * k.bpx;
                fillRect(img, W, H, cx - half, (float)y, cx + half, (float)y + 1, 0, 0, 0);
            }
        } else {
            fillRect(img, W, H, x0, sy(b.y1), x1, sy(b.y0), 255, 255, 255);
            fillRect(img, W, H, x0 + 1, sy(b.y1) + 1, x1 - 1, sy(b.y0) - 1, 0, 0, 0);
        }
    }
    // player (green icon, cube = 1 block, ship = 1 x 0.7)
    float ph = (c.mode == GameMode::Cube) ? 1.f : (c.mode == GameMode::Wave ? 0.6f : 0.7f);
    float pw = (c.mode == GameMode::Wave) ? 0.6f : 1.f;
    fillRect(img, W, H, k.playerSx - pw * k.bpx / 2, sy(py + ph), k.playerSx + pw * k.bpx / 2, sy(py), 0, 255, 0);
    // progress bar
    int bx0 = (int)(0.32f * W), bx1 = (int)(0.68f * W), by = (int)(0.022f * H);
    fillRect(img, W, H, (float)bx0 - 1, (float)by - 2, (float)bx1 + 1, (float)by + 3, 200, 200, 200);
    fillRect(img, W, H, (float)bx0, (float)by - 1, (float)bx1, (float)by + 2, 45, 45, 45);
    fillRect(img, W, H, (float)bx0, (float)by - 1, bx0 + (bx1 - bx0) * std::clamp(progress, 0.f, 1.f), (float)by + 2,
             90, 230, 90);
    if (o.noise) {
        std::uniform_int_distribution<int> n(-5, 5);
        for (size_t i = 0; i < img.size(); i += 4)
            for (int ch = 0; ch < 3; ++ch) img[i + ch] = (uint8_t)std::clamp((int)img[i + ch] + n(rng), 0, 255);
    }
}

// ---------------- physics ----------------

struct Body {
    float x = 0, y = 0, vy = 0;
    bool grounded = true, dead = false;
};

static constexpr float kCubeGravity = 90.7f, kCubeJump = 19.0f;
static constexpr float kShipAccel = 45.f, kShipMaxV = 7.f;

static bool overlap(const Box& b, float x0, float y0, float x1, float y1) {
    return x0 < b.x1 && x1 > b.x0 && y0 < b.y1 && y1 > b.y0;
}

static void step(const Course& c, Body& p, bool held, float vx, float dt) {
    const float prevY = p.y;
    p.x += vx * dt;
    float hw, hh;
    switch (c.mode) {
        case GameMode::Cube: {
            hw = 0.8f; hh = 0.8f;
            if (held && p.grounded) { p.vy = kCubeJump; p.grounded = false; if (getenv("SIMTRACE")) fprintf(stderr, "jump at x=%.2f\n", p.x); }
            p.vy -= kCubeGravity * dt;
            p.y += p.vy * dt;
            p.grounded = false;
            if (p.y <= 0.f) { p.y = 0.f; p.vy = 0.f; p.grounded = true; }
            for (const Box& b : c.boxes) {
                if (b.x0 > p.x + 2 || b.x1 < p.x - 2) continue;
                float x0 = p.x - hw / 2, x1 = p.x + hw / 2;
                if (!overlap(b, x0, p.y, x1, p.y + hh)) continue;
                if (b.deadly) { p.dead = true; if (getenv("SIMTRACE")) fprintf(stderr, "dead x=%.2f y=%.2f vy=%.2f box %.2f-%.2f\n", p.x, p.y, p.vy, b.x0, b.x1); return; }
                if (prevY >= b.y1 - 0.25f && p.vy <= 0) { p.y = b.y1; p.vy = 0; p.grounded = true; }
                else { p.dead = true; return; }
            }
            return;
        }
        case GameMode::Ship: {
            hw = 0.9f; hh = 0.6f;
            p.vy += (held ? kShipAccel : -kShipAccel) * dt;
            p.vy = std::clamp(p.vy, -kShipMaxV, kShipMaxV);
            p.y += p.vy * dt;
            break;
        }
        default: {  // wave: 45 degrees
            hw = 0.35f; hh = 0.35f;
            p.vy = held ? vx : -vx;
            p.y += p.vy * dt;
            break;
        }
    }
    float h = (c.mode == GameMode::Wave) ? 0.6f : 0.7f;
    if (p.y < 0.f) { p.y = 0.f; p.vy = 0.f; }
    if (p.y + h > c.ceiling) { p.y = c.ceiling - h; p.vy = 0.f; }
    float yo = (h - hh) / 2;
    for (const Box& b : c.boxes) {
        if (b.x0 > p.x + 2 || b.x1 < p.x - 2) continue;
        if (overlap(b, p.x - hw / 2, p.y + yo, p.x + hw / 2, p.y + yo + hh)) { p.dead = true; if (getenv("SIMTRACE")) fprintf(stderr, "dead x=%.2f y=%.2f vy=%.2f\n", p.x, p.y, p.vy); return; }
    }
}

// ---------------- session ----------------

// Drives one or more attempts. `frameFn` receives each captured frame,
// `tickFn` every millisecond; both return commands for the bot's virtual button.
struct Session {
    const Course& c;
    SimOptions o;
    std::mt19937 rng;
    Body body;
    bool held = false;
    std::deque<std::pair<int64_t, bool>> pending;   // (applyAt, pressed)
    int presses = 0;

    Session(const Course& course, const SimOptions& opt) : c(course), o(opt), rng(opt.seed) {}

    float startY() const { return c.mode == GameMode::Cube ? 0.f : 4.f; }
    void resetBody() { body = Body{}; body.y = startY(); held = false; pending.clear(); }

    // Runs one attempt starting at t0 (us). Returns time it ended.
    int64_t attempt(int64_t t0, BotEngine& eng, int64_t inputLatencyUs, float maxDistance,
                    const std::function<void(int64_t, bool)>& onApplied, RunResult& res) {
        resetBody();
        const float vx = speedBlocksPerSecond(o.speed);
        const int64_t physDt = 1000000 / 240;
        const int64_t capDt = (int64_t)(1e6f / o.captureHz);
        std::uniform_int_distribution<int64_t> phase(0, capDt - 1);
        int64_t nextPhys = t0, nextCap = t0 + phase(rng);
        std::vector<uint8_t> img;
        std::vector<InputCmd> cmds;
        auto apply = [&](int64_t t) {
            for (auto& c2 : cmds) {
                if (c2.type == CmdType::Press) pending.push_back({t + inputLatencyUs, true});
                else if (c2.type == CmdType::Release) pending.push_back({t + inputLatencyUs, false});
            }
            cmds.clear();
        };
        for (int64_t t = t0;; t += 1000) {
            eng.onTick(t, cmds);
            apply(t);
            while (!pending.empty() && pending.front().first <= t) {
                bool nh = pending.front().second;
                pending.pop_front();
                if (nh != held) { held = nh; if (held) ++presses; if (onApplied) onApplied(t, held); }
            }
            while (nextPhys <= t) {
                step(c, body, held, vx, physDt / 1e6f);
                nextPhys += physDt;
                if (body.dead || body.x >= std::min(c.length, maxDistance)) break;
            }
            if (body.dead || body.x >= std::min(c.length, maxDistance)) {
                res.distance = body.x;
                res.completed = !body.dead && body.x >= c.length;
                res.presses = presses;
                return t;
            }
            if (t >= nextCap) {
                render(c, o, body.x, body.y, body.x / c.length, img, rng);
                Frame f{img.data(), o.width, o.height, o.width * 4, PixelFormat::RGBA, t};
                eng.onFrame(f, cmds);
                apply(t);
                ++res.framesAnalyzed;
                nextCap += capDt;
            }
        }
    }
};

static Config baseConfig(const Course& c, const SimOptions& o) {
    Config cfg;
    cfg.gameMode = c.mode;
    cfg.speed = o.speed;
    cfg.playerColor = {0, 255, 0};
    cfg.playerX = 0.30f;
    cfg.inputDelayMs = o.latencyMs + (int)(500.f / o.captureHz);  // + half a frame on average
    cfg.autoRetry = false;
    cfg.userInputTakesOver = false;
    return cfg;
}

RunResult runSmart(const Course& c, const SimOptions& o) {
    BotEngine eng;
    eng.cfg = baseConfig(c, o);
    std::vector<InputCmd> cmds;
    eng.setBotMode(BotMode::Smart, 0, cmds);
    eng.setAutoPlay(true, 0, cmds);
    Session s(c, o);
    RunResult r;
    s.attempt(1000, eng, (int64_t)o.latencyMs * 1000, 1e9f, nullptr, r);
    return r;
}

RunResult recordSmart(const Course& c, const SimOptions& o, Macro& out) {
    // "human" = Smart engine; recorder = second engine that only watches the screen
    // and receives the human's applied presses as user input (like PC hooks would).
    BotEngine human, rec;
    human.cfg = baseConfig(c, o);
    rec.cfg = human.cfg;
    std::vector<InputCmd> cmds;
    human.setAutoPlay(true, 0, cmds);
    Session s(c, o);
    RunResult r;
    rec.startRecording(0, cmds);
    // Attempt 1: human gives up after 25 blocks (dies), attempt 2: full run.
    int64_t t = 1000;
    for (int a = 0; a < 2; ++a) {
        BotEngine* drive = &human;
        Session& S = s;   // both engines see the same frames
        S.resetBody();
        const float vx = speedBlocksPerSecond(o.speed);
        const int64_t physDt = 1000000 / 240, capDt = (int64_t)(1e6f / o.captureHz);
        std::uniform_int_distribution<int64_t> phase(0, capDt - 1);
        int64_t nextPhys = t, nextCap = t + phase(S.rng);
        std::vector<uint8_t> img;
        std::vector<InputCmd> hc, rc;
        const float giveUpAt = (a == 0) ? 25.f : 1e9f;
        for (;; t += 1000) {
            drive->onTick(t, hc);
            rec.onTick(t, rc);
            for (auto& cc : hc)
                if (cc.type == CmdType::Press || cc.type == CmdType::Release)
                    S.pending.push_back({t + (int64_t)o.latencyMs * 1000, cc.type == CmdType::Press});
            hc.clear(); rc.clear();
            while (!S.pending.empty() && S.pending.front().first <= t) {
                bool nh = S.pending.front().second;
                S.pending.pop_front();
                if (S.body.x > giveUpAt) nh = false;   // the human stops pressing
                if (nh != S.held) { S.held = nh; rec.onUserInput(t, nh, 0, rc); }
            }
            while (nextPhys <= t) {
                step(c, S.body, S.held, vx, physDt / 1e6f);
                nextPhys += physDt;
                if (S.body.dead || S.body.x >= c.length) break;
            }
            if (S.body.dead || S.body.x >= c.length) break;
            if (t >= nextCap) {
                render(c, o, S.body.x, S.body.y, S.body.x / c.length, img, S.rng);
                Frame f{img.data(), o.width, o.height, o.width * 4, PixelFormat::RGBA, t};
                drive->onFrame(f, hc);
                rec.onFrame(f, rc);
                ++r.framesAnalyzed;
                nextCap += capDt;
            }
        }
        r.completed = !S.body.dead;
        r.distance = S.body.x;
        if (S.body.dead) {
            // death pause frames go to the recorder (bar holds, then resets on respawn)
            int64_t e0 = t;
            for (int64_t e = e0 + capDt; e < e0 + 1000000; e += capDt) {
                render(c, o, S.body.x, S.body.y, S.body.x / c.length, img, S.rng);
                Frame f{img.data(), o.width, o.height, o.width * 4, PixelFormat::RGBA, e};
                rec.onFrame(f, rc);
                rc.clear();
            }
            t = e0 + 1000000;
            human.setAutoPlay(true, t, hc);  // reset pilot for next attempt
            hc.clear();
        }
    }
    out = rec.stopRecording();
    return r;
}

RunResult runMacro(const Course& c, const SimOptions& o, const Macro& m, bool viaProgressBar) {
    BotEngine eng;
    eng.cfg = baseConfig(c, o);
    eng.cfg.progressSync = viaProgressBar;
    std::vector<InputCmd> cmds;
    eng.loadMacro(m);
    eng.setBotMode(BotMode::Macro, 0, cmds);
    eng.setAutoPlay(true, 0, cmds);
    if (!viaProgressBar) eng.manualSync(1000, cmds);
    Session s(c, o);
    RunResult r;
    // macro inputs go out through SendInput / dispatchGesture: modelled with 1 ms latency
    s.attempt(1000, eng, 1000, 1e9f, nullptr, r);
    return r;
}

}  // namespace sim
