#include "gdbot/macro.hpp"
#include <algorithm>
#include <cstdio>
#include <sstream>

namespace gdbot {

// File format (.gdbm, plain text so it is easy to inspect / share):
//   GDBOTMACRO 1
//   game 2.209
//   level 128
//   platform windows
//   progress 1.000
//   events <n>
//   <t_us> <press 0/1> <player 0/1>
std::string Macro::serialize() const {
    std::ostringstream o;
    o << "GDBOTMACRO 1\n";
    o << "game " << gameVersion << "\n";
    o << "level " << levelId << "\n";
    o << "platform " << platform << "\n";
    char buf[32];
    snprintf(buf, sizeof buf, "%.4f", bestProgress);
    o << "progress " << buf << "\n";
    o << "events " << events.size() << "\n";
    for (const auto& e : events) o << e.tUs << ' ' << (e.press ? 1 : 0) << ' ' << (int)e.player << '\n';
    return o.str();
}

bool Macro::parse(const std::string& text, Macro& out, std::string* err) {
    std::istringstream in(text);
    std::string line;
    Macro m;
    auto fail = [&](const std::string& e) { if (err) *err = e; return false; };
    if (!std::getline(in, line)) return fail("empty file");
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line != "GDBOTMACRO 1") return fail("not a GDBOTMACRO 1 file");
    size_t expected = (size_t)-1;
    int lineNo = 1;
    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        if (isdigit((unsigned char)line[0]) || line[0] == '-') {
            long long t; int p, pl;
            if (!(ls >> t >> p >> pl) || (p != 0 && p != 1) || (pl != 0 && pl != 1))
                return fail("bad event at line " + std::to_string(lineNo));
            m.events.push_back({(int64_t)t, p == 1, (uint8_t)pl});
            continue;
        }
        std::string key;
        ls >> key;
        if (key == "game") ls >> m.gameVersion;
        else if (key == "level") ls >> m.levelId;
        else if (key == "platform") ls >> m.platform;
        else if (key == "progress") ls >> m.bestProgress;
        else if (key == "events") ls >> expected;
    }
    if (expected != (size_t)-1 && expected != m.events.size())
        return fail("event count mismatch (header " + std::to_string(expected) + ", found " +
                    std::to_string(m.events.size()) + ")");
    std::stable_sort(m.events.begin(), m.events.end(),
                     [](const MacroEvent& a, const MacroEvent& b) { return a.tUs < b.tUs; });
    out = std::move(m);
    return true;
}

// ---------------- recorder ----------------

void MacroRecorder::start(int64_t nowUs) {
    active_ = true;
    syncUs_ = nowUs;
    take_.clear();
    best_.clear();
    takeProgress_ = 0.f;
    bestProgress_ = -1.f;
    held_[0] = held_[1] = false;
}

void MacroRecorder::commitTake() {
    // close dangling presses so a take never ends "held"
    for (int p = 0; p < 2; ++p)
        if (held_[p] && !take_.empty()) take_.push_back({take_.back().tUs + 1000, false, (uint8_t)p});
    if (!take_.empty() && takeProgress_ > bestProgress_) {
        best_ = take_;
        bestProgress_ = takeProgress_;
    }
}

void MacroRecorder::onAttemptStart(int64_t syncUs) {
    if (!active_) return;
    commitTake();
    take_.clear();
    takeProgress_ = 0.f;
    syncUs_ = syncUs;
    held_[0] = held_[1] = false;
}

void MacroRecorder::onInput(int64_t tUs, bool press, int player) {
    if (!active_ || player < 0 || player > 1) return;
    if (held_[player] == press) return;  // ignore key auto-repeat / duplicates
    held_[player] = press;
    int64_t rel = tUs - syncUs_;
    if (rel < 0) rel = 0;
    take_.push_back({rel, press, (uint8_t)player});
}

void MacroRecorder::onProgress(float fraction) {
    if (fraction > takeProgress_) takeProgress_ = fraction;
}

Macro MacroRecorder::stop() {
    commitTake();
    active_ = false;
    Macro m;
    m.events = best_;
    m.bestProgress = bestProgress_ < 0 ? 0.f : bestProgress_;
    best_.clear();
    take_.clear();
    return m;
}

// ---------------- player ----------------

void MacroPlayer::start(int64_t syncUs, int64_t offsetUs) {
    baseUs_ = syncUs + offsetUs;
    idx_ = 0;
    running_ = !macro_.events.empty();
}

int MacroPlayer::poll(int64_t nowUs, std::vector<MacroEvent>& out) {
    if (!running_) return 0;
    int n = 0;
    while (idx_ < macro_.events.size() && baseUs_ + macro_.events[idx_].tUs <= nowUs) {
        out.push_back(macro_.events[idx_++]);
        ++n;
    }
    if (idx_ >= macro_.events.size()) running_ = false;
    return n;
}

int64_t MacroPlayer::nextEventUs() const {
    if (!running_ || idx_ >= macro_.events.size()) return -1;
    return baseUs_ + macro_.events[idx_].tUs;
}

}  // namespace gdbot
