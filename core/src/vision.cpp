#include "gdbot/vision.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace gdbot {

static inline void px(const Frame& f, int x, int y, int& r, int& g, int& b) {
    const uint8_t* p = f.data + (size_t)y * f.stride + (size_t)x * 4;
    if (f.format == PixelFormat::RGBA) { r = p[0]; g = p[1]; b = p[2]; }
    else { r = p[2]; g = p[1]; b = p[0]; }
}

static inline int dist3(int r1, int g1, int b1, int r2, int g2, int b2) {
    return std::abs(r1 - r2) + std::abs(g1 - g2) + std::abs(b1 - b2);
}

Rgb samplePixel(const Frame& f, int x, int y) {
    x = std::clamp(x, 0, f.width - 1);
    y = std::clamp(y, 0, f.height - 1);
    // 3x3 average to reduce noise / antialiasing
    int sr = 0, sg = 0, sb = 0, n = 0;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            int xx = x + dx, yy = y + dy;
            if (xx < 0 || yy < 0 || xx >= f.width || yy >= f.height) continue;
            int r, g, b; px(f, xx, yy, r, g, b);
            sr += r; sg += g; sb += b; ++n;
        }
    return Rgb{(uint8_t)(sr / n), (uint8_t)(sg / n), (uint8_t)(sb / n)};
}

// ===================== progress bar =====================

ProgressReading ProgressBarReader::read(const Frame& f, const Config& cfg) {
    ProgressReading r;
    if (!f.data || f.width < 16 || f.height < 16) return r;
    int x0 = (int)std::lround(cfg.barX0 * f.width);
    int x1 = (int)std::lround(cfg.barX1 * f.width);
    int y = (int)std::lround(cfg.barY * f.height);
    x0 = std::clamp(x0, 0, f.width - 1);
    x1 = std::clamp(x1, 0, f.width);
    y = std::clamp(y, 1, f.height - 2);
    int n = x1 - x0;
    if (n < 8) return r;

    std::vector<int> cr(n), cg(n), cb(n);
    for (int i = 0; i < n; ++i) {
        int sr = 0, sg = 0, sb = 0;
        for (int dy = -1; dy <= 1; ++dy) {
            int a, b2, c; px(f, x0 + i, y + dy, a, b2, c);
            sr += a; sg += b2; sb += c;
        }
        cr[i] = sr / 3; cg[i] = sg / 3; cb[i] = sb / 3;
    }
    // empty-track reference: median of the right part of the bar (skipping the border)
    int ta = n - std::max(3, n / 8), tb = n - std::max(1, n / 40);
    std::vector<int> vr, vg, vb;
    for (int i = ta; i < tb; ++i) { vr.push_back(cr[i]); vg.push_back(cg[i]); vb.push_back(cb[i]); }
    std::nth_element(vr.begin(), vr.begin() + vr.size() / 2, vr.end());
    std::nth_element(vg.begin(), vg.begin() + vg.size() / 2, vg.end());
    std::nth_element(vb.begin(), vb.begin() + vb.size() / 2, vb.end());
    int tr = vr[vr.size() / 2], tg = vg[vg.size() / 2], tb2 = vb[vb.size() / 2];

    const int thr = cfg.barThreshold;
    // Once the empty track color is known, keep using it: near 100% the right end is filled too.
    const bool tailIsTrack = !known_ || dist3(tr, tg, tb2, track_.r, track_.g, track_.b) <= thr;
    if (known_) { tr = track_.r; tg = track_.g; tb2 = track_.b; }

    // The fill ends at the first run of empty-track columns (borders / the
    // percentage label after the bar never look like a run of track).
    const int run = std::max(3, n / 40);
    int fillEnd = n, emptyRun = 0;
    for (int i = 0; i < n; ++i) {
        const bool filled = dist3(cr[i], cg[i], cb[i], tr, tg, tb2) > thr;
        if (filled) emptyRun = 0;
        else if (++emptyRun >= run) { fillEnd = i - run + 1; break; }
    }
    r.valid = true;
    r.fraction = fillEnd / (float)n;
    if (r.fraction < 0.5f && tailIsTrack) {   // right end is the empty track => remember / refresh it
        int ar, ag, ab;
        ar = vr[vr.size() / 2]; ag = vg[vg.size() / 2]; ab = vb[vb.size() / 2];
        track_ = Rgb{(uint8_t)ar, (uint8_t)ag, (uint8_t)ab};
        known_ = true;
    }
    return r;
}

// ===================== attempt tracker =====================

AttemptTracker::Event AttemptTracker::update(const ProgressReading& r, int64_t tUs, const Config& cfg) {
    if (!r.valid) {
        if (++invalidFrames_ > 60) inAttempt_ = false;
        return Event::None;
    }
    invalidFrames_ = 0;
    const float f = r.fraction;
    Event ev = Event::None;

    const bool dropped = last_ >= 0.f && f + 0.008f < last_;
    if (f < 0.012f && (last_ < 0.f || !inAttempt_ || dropped || completed_)) {
        if (!inAttempt_ || dropped || last_ < 0.f || completed_) {
            inAttempt_ = true;
            completed_ = false;
            stalledFired_ = false;
            ++attempts_;
            startUs_ = tUs;
            lastChangeUs_ = tUs;
            ev = Event::AttemptStart;
        }
    }
    if (f > max_) max_ = f;
    if (std::fabs(f - last_) > 0.0005f) { lastChangeUs_ = tUs; stalledFired_ = false; }
    if (inAttempt_ && !completed_ && f >= 0.995f) { completed_ = true; ev = Event::LevelComplete; }
    if (ev == Event::None && inAttempt_ && !completed_ && !stalledFired_ && f > 0.012f &&
        tUs - lastChangeUs_ > (int64_t)cfg.retryAfterMs * 1000) {
        stalledFired_ = true;
        ev = Event::Stalled;
    }
    last_ = f;
    return ev;
}

// ===================== start frame matcher =====================

void StartFrameMatcher::thumb(const Frame& f, uint8_t* out) const {
    for (int ty = 0; ty < TH; ++ty)
        for (int tx = 0; tx < TW; ++tx) {
            int xa = tx * f.width / TW, xb = (tx + 1) * f.width / TW;
            int ya = ty * f.height / TH, yb = (ty + 1) * f.height / TH;
            int sum = 0, n = 0;
            for (int y = ya; y < yb; y += 2)
                for (int x = xa; x < xb; x += 2) {
                    int r, g, b; px(f, x, y, r, g, b);
                    sum += (r * 30 + g * 59 + b * 11) / 100; ++n;
                }
            out[ty * TW + tx] = (uint8_t)(n ? sum / n : 0);
        }
}

void StartFrameMatcher::capture(const Frame& f) {
    if (!f.data) return;
    thumb(f, start_);
    has_ = true;
    diverged_ = false;
    startUs_ = f.timeUs;
}

bool StartFrameMatcher::update(const Frame& f) {
    if (!has_ || !f.data) return false;
    uint8_t cur[TW * TH];
    thumb(f, cur);
    long sum = 0;
    // ignore the top rows (progress bar / percentage text changes)
    for (int i = 2 * TW; i < TW * TH; ++i) sum += std::abs((int)cur[i] - (int)start_[i]);
    lastDiff_ = sum / (float)(TW * (TH - 2));
    if (!diverged_ && lastDiff_ > 22.f && f.timeUs - startUs_ > 400000) diverged_ = true;
    if (diverged_ && lastDiff_ < 7.f) {
        diverged_ = false;
        startUs_ = f.timeUs;
        return true;
    }
    return false;
}

// ===================== scene analyzer =====================

const Scene& Analyzer::analyze(const Frame& f, const Config& cfg) {
    Scene& s = scene_;
    const int w = f.width, h = f.height;
    s.w = w; s.h = h;
    s.player = PlayerInfo{};
    s.blockPx = cfg.blockRel * h;
    if (!f.data || w < 32 || h < 32) { s.solid.assign((size_t)std::max(0, w * h), 0); return s; }

    rgb_.resize((size_t)w * h * 3);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int r, g, b; px(f, x, y, r, g, b);
            uint8_t* o = &rgb_[((size_t)y * w + x) * 3];
            o[0] = (uint8_t)r; o[1] = (uint8_t)g; o[2] = (uint8_t)b;
        }

    // ---- per-row median background ----
    rowMed_.resize((size_t)h * 3);
    std::vector<uint8_t> cr, cg, cb;
    cr.reserve(w); cg.reserve(w); cb.reserve(w);
    for (int y = 0; y < h; ++y) {
        cr.clear(); cg.clear(); cb.clear();
        for (int x = 0; x < w; x += 2) {
            const uint8_t* p = &rgb_[((size_t)y * w + x) * 3];
            cr.push_back(p[0]); cg.push_back(p[1]); cb.push_back(p[2]);
        }
        size_t m = cr.size() / 2;
        std::nth_element(cr.begin(), cr.begin() + m, cr.end());
        std::nth_element(cg.begin(), cg.begin() + m, cg.end());
        std::nth_element(cb.begin(), cb.begin() + m, cb.end());
        rowMed_[y * 3] = cr[m]; rowMed_[y * 3 + 1] = cg[m]; rowMed_[y * 3 + 2] = cb[m];
    }
    const int ui = std::clamp((int)(cfg.uiBandRel * h), 0, h / 3);
    s.uiY = ui;

    // ---- player (needs no background model: matched by its calibrated color) ----
    {
        const float b = s.blockPx;
        int cx = (int)(cfg.playerX * w);
        int xa = std::max(0, (int)(cx - 2.5f * b)), xb = std::min(w, (int)(cx + 2.5f * b));
        const Rgb pc = cfg.playerColor;
        long sx = 0, sy = 0; int n = 0;
        for (int y = ui; y < h; ++y)
            for (int x = xa; x < xb; ++x) {
                const uint8_t* p = &rgb_[((size_t)y * w + x) * 3];
                if (dist3(p[0], p[1], p[2], pc.r, pc.g, pc.b) <= cfg.colorTolerance) { sx += x; sy += y; ++n; }
            }
        if (n >= 4) {
            float mx = sx / (float)n, my = sy / (float)n;
            // bbox of matches near the centroid (drops stray same-colored decoration)
            int minx = w, maxx = -1, miny = h, maxy = -1, m = 0;
            float lim = 1.5f * b;
            for (int y = std::max(ui, (int)(my - lim)); y < std::min(h, (int)(my + lim)); ++y)
                for (int x = std::max(xa, (int)(mx - lim)); x < std::min(xb, (int)(mx + lim)); ++x) {
                    const uint8_t* p = &rgb_[((size_t)y * w + x) * 3];
                    if (dist3(p[0], p[1], p[2], pc.r, pc.g, pc.b) <= cfg.colorTolerance) {
                        minx = std::min(minx, x); maxx = std::max(maxx, x);
                        miny = std::min(miny, y); maxy = std::max(maxy, y);
                        ++m;
                    }
                }
            if (m >= 4) {
                s.player.found = true;
                s.player.pixels = m;
                s.player.w = (float)(maxx - minx + 1);
                s.player.h = (float)(maxy - miny + 1);
                s.player.cx = (minx + maxx) * 0.5f;
                s.player.cy = (miny + maxy) * 0.5f;
            }
        }
    }

    // ---- background model ----
    // The player always flies/stands in free space, so the pixels around it are
    // background. Starting from that color we walk up and down the rows; each
    // row's background is the median of the pixels close to the current estimate
    // (follows smooth GD gradients, ignores walls even if they cover most of a row).
    rowBg_.resize((size_t)h * 3);
    rowBgFrac_.assign(h, 0.f);
    int seedR, seedG, seedB, seedRow;
    {
        cr.clear(); cg.clear(); cb.clear();
        if (s.player.found) {
            const float b = s.blockPx;
            seedRow = std::clamp((int)s.player.cy, ui, h - 1);
            for (int y = std::max(ui, (int)(s.player.cy - 1.6f * b)); y < std::min(h, (int)(s.player.cy + 0.3f * b)); ++y)
                for (int x = std::max(0, (int)(s.player.cx - 1.6f * b)); x < std::min(w, (int)(s.player.cx + 1.6f * b)); ++x) {
                    if (std::fabs(x - s.player.cx) < s.player.w * 0.7f && std::fabs(y - s.player.cy) < s.player.h * 0.7f) continue;
                    const uint8_t* p = &rgb_[((size_t)y * w + x) * 3];
                    cr.push_back(p[0]); cg.push_back(p[1]); cb.push_back(p[2]);
                }
        }
        if (cr.size() < 16) {   // no player: most common row median
            seedRow = h / 2;
            cr.clear(); cg.clear(); cb.clear();
            for (int y = ui; y < h; ++y) { cr.push_back(rowMed_[y * 3]); cg.push_back(rowMed_[y * 3 + 1]); cb.push_back(rowMed_[y * 3 + 2]); }
        }
        size_t m = cr.size() / 2;
        std::nth_element(cr.begin(), cr.begin() + m, cr.end());
        std::nth_element(cg.begin(), cg.begin() + m, cg.end());
        std::nth_element(cb.begin(), cb.begin() + m, cb.end());
        seedR = cr[m]; seedG = cg[m]; seedB = cb[m];
    }
    auto fitRow = [&](int y, int& curR, int& curG, int& curB) {
        cr.clear(); cg.clear(); cb.clear();
        for (int x = 0; x < w; x += 2) {
            const uint8_t* p = &rgb_[((size_t)y * w + x) * 3];
            if (dist3(p[0], p[1], p[2], curR, curG, curB) < 60) { cr.push_back(p[0]); cg.push_back(p[1]); cb.push_back(p[2]); }
        }
        const float frac = cr.size() / (float)((w + 1) / 2);
        rowBgFrac_[y] = frac;
        if (frac >= 0.08f) {
            size_t m = cr.size() / 2;
            std::nth_element(cr.begin(), cr.begin() + m, cr.end());
            std::nth_element(cg.begin(), cg.begin() + m, cg.end());
            std::nth_element(cb.begin(), cb.begin() + m, cb.end());
            curR = cr[m]; curG = cg[m]; curB = cb[m];
        }
        rowBg_[y * 3] = (uint8_t)curR; rowBg_[y * 3 + 1] = (uint8_t)curG; rowBg_[y * 3 + 2] = (uint8_t)curB;
    };
    {
        int r = seedR, g = seedG, b = seedB;
        for (int y = seedRow; y >= 0; --y) fitRow(y, r, g, b);
        r = seedR; g = seedG; b = seedB;
        for (int y = seedRow + 1; y < h; ++y) fitRow(y, r, g, b);
    }
    {
        // floor / ceiling = rows (almost) without background at the bottom / top
        int y = h - 1;
        while (y > h / 3 && rowBgFrac_[y] < 0.08f) --y;
        s.groundY = (h - 1 - y >= 2) ? y + 1 : h;
        y = ui;
        while (y < 2 * h / 3 && rowBgFrac_[y] < 0.08f) ++y;
        s.ceilY = (y - ui >= 2) ? y : ui;
    }

    // ---- solid mask: anything that is not background (floor, ceiling, blocks, spikes) ----
    s.solid.assign((size_t)w * h, 0);
    const int thr = cfg.obstacleThreshold;
    for (int y = ui; y < h; ++y) {
        const int mr = rowBg_[y * 3], mg = rowBg_[y * 3 + 1], mb = rowBg_[y * 3 + 2];
        for (int x = 0; x < w; ++x) {
            const uint8_t* p = &rgb_[((size_t)y * w + x) * 3];
            if (dist3(p[0], p[1], p[2], mr, mg, mb) > thr) s.solid[(size_t)y * w + x] = 1;
        }
    }
    // never treat the player (or its trail behind it) as an obstacle
    if (s.player.found) {
        int pad = 3;
        int px0 = std::max(0, (int)(s.player.cx - s.player.w / 2) - pad - (int)(2 * s.blockPx));
        int px1 = std::min(w, (int)(s.player.cx + s.player.w / 2) + pad);
        int py0 = std::max(0, (int)(s.player.cy - s.player.h / 2) - pad);
        int py1 = std::min(h, (int)(s.player.cy + s.player.h / 2) + pad);
        for (int y = std::max(py0, ui); y < py1; ++y)
            for (int x = px0; x < px1; ++x) s.solid[(size_t)y * w + x] = 0;
    }
    return s;
}

}  // namespace gdbot
