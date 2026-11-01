#include "gdbot/engine.hpp"
#include <algorithm>
#include <cstdio>

namespace gdbot {

Status BotEngine::status() const {
    if (stopped_) return Status::Stopped;
    if (recorder_.active()) return Status::Recording;
    if (paused_) return Status::Paused;
    if (autoPlay_) return Status::Playing;
    return Status::Ready;
}

std::string BotEngine::statusLine() const {
    char buf[256];
    const char* extra = "";
    if (autoPlay_ && botMode_ == BotMode::Macro) {
        if (!hasMacro_) extra = " | no macro loaded";
        else if (waitingSync_) extra = " | waiting for attempt start";
        else if (player_.running()) extra = " | replaying";
        else extra = " | macro finished";
    } else if (autoPlay_ && botMode_ == BotMode::Smart) {
        extra = lastDecision_.reason;
    }
    snprintf(buf, sizeof buf, "%s %s | %s | %.0f%% | att %d | %.0f fps%s%s",
             statusName(status()), botModeName(botMode_), gameModeName(cfg.gameMode),
             progress() * 100.f, attempts(), fps_, (*extra ? " | " : ""), extra);
    return buf;
}

void BotEngine::setHeld(int player, bool held, int64_t nowUs, std::vector<InputCmd>& out) {
    (void)nowUs;
    if (held_[player] == held) return;
    held_[player] = held;
    releaseAtUs_[player] = -1;
    out.push_back({held ? CmdType::Press : CmdType::Release, player});
}

void BotEngine::tap(int player, int64_t nowUs, std::vector<InputCmd>& out) {
    if (held_[player]) return;  // still inside the previous tap
    held_[player] = true;
    releaseAtUs_[player] = nowUs + (int64_t)std::max(1, cfg.tapDurationMs) * 1000;
    out.push_back({CmdType::Press, player});
}

void BotEngine::releaseAll(std::vector<InputCmd>& out) {
    for (int p = 0; p < 2; ++p) {
        if (held_[p]) out.push_back({CmdType::Release, p});
        held_[p] = false;
        releaseAtUs_[p] = -1;
    }
}

void BotEngine::setAutoPlay(bool on, int64_t nowUs, std::vector<InputCmd>& out) {
    (void)nowUs;
    if (on) {
        stopped_ = false;
        paused_ = false;
        autoPlay_ = true;
        pilot_.reset();
        // a macro can only start on a known attempt start
        waitingSync_ = (botMode_ == BotMode::Macro);
    } else {
        autoPlay_ = false;
        player_.stop();
        releaseAll(out);
    }
}

void BotEngine::setBotMode(BotMode m, int64_t nowUs, std::vector<InputCmd>& out) {
    if (m == botMode_) return;
    bool was = autoPlay_;
    if (was) setAutoPlay(false, nowUs, out);
    botMode_ = m;
    if (was) setAutoPlay(true, nowUs, out);
}

void BotEngine::togglePause(int64_t nowUs, std::vector<InputCmd>& out) {
    (void)nowUs;
    paused_ = !paused_;
    if (paused_) releaseAll(out);
}

void BotEngine::emergencyStop(std::vector<InputCmd>& out) {
    releaseAll(out);
    autoPlay_ = false;
    paused_ = false;
    stopped_ = true;
    player_.stop();
    if (recorder_.active()) {
        Macro m = recorder_.stop();
        if (!m.events.empty()) { m.levelId = cfg.levelId; loadMacro(m); }
    }
}

void BotEngine::startRecording(int64_t nowUs, std::vector<InputCmd>& out) {
    setAutoPlay(false, nowUs, out);   // recording means the human plays
    stopped_ = false;
    recorder_.start(nowUs);
}

Macro BotEngine::stopRecording() {
    Macro m = recorder_.stop();
    m.levelId = cfg.levelId;
    return m;
}

void BotEngine::manualSync(int64_t nowUs, std::vector<InputCmd>& out) {
    captureStartNext_ = true;
    attemptStarted(nowUs, out);
}

bool BotEngine::attemptStarted(int64_t syncUs, std::vector<InputCmd>& out) {
    if (syncUs - lastAttemptUs_ < 300000) return false;   // bar + matcher may both fire
    lastAttemptUs_ = syncUs;
    pilot_.reset();
    recorder_.onAttemptStart(syncUs);
    if (autoPlay_ && botMode_ == BotMode::Macro && hasMacro_) {
        releaseAll(out);
        player_.start(syncUs, (int64_t)cfg.timingOffsetMs * 1000);
        waitingSync_ = false;
    }
    return true;
}

void BotEngine::onFrame(const Frame& f, std::vector<InputCmd>& out) {
    // fps of the analysis loop
    if (fpsWindowStart_ == 0) fpsWindowStart_ = f.timeUs;
    ++fpsFrames_;
    if (f.timeUs - fpsWindowStart_ >= 500000) {
        fps_ = fpsFrames_ * 1e6f / (float)(f.timeUs - fpsWindowStart_);
        fpsFrames_ = 0;
        fpsWindowStart_ = f.timeUs;
    }

    const Scene& s = analyzer_.analyze(f, cfg);

    // ---- attempt sync ----
    bool barStart = false;
    if (cfg.progressSync) {
        ProgressReading r = barReader_.read(f, cfg);
        auto ev = tracker_.update(r, f.timeUs, cfg);
        if (ev == AttemptTracker::Event::AttemptStart) barStart = true;
        // LevelComplete is only informational: the bar can read 100% a few frames
        // early, so the macro is never cut short here (it ends with its last event).
        if (ev == AttemptTracker::Event::Stalled && cfg.autoRetry && cfg.retryX >= 0 && cfg.retryY >= 0 &&
            !stopped_ && (autoPlay_ || recorder_.active()) && f.timeUs - lastRetryUs_ > 2000000) {
            releaseAll(out);
            out.push_back({CmdType::RetryClick, 0});
            lastRetryUs_ = f.timeUs;
        }
        if (recorder_.active()) recorder_.onProgress(tracker_.fraction());
    }
    // The start-frame matcher catches early deaths the bar cannot resolve, but an
    // empty stretch of a level can look like the start screen: when the bar is in
    // use and clearly shows progress, a "match" is ignored.
    bool matchStart = matcher_.update(f);
    if (matchStart && cfg.progressSync && tracker_.fraction() > 0.03f) matchStart = false;
    bool accepted = (barStart || matchStart) && attemptStarted(f.timeUs, out);
    // bar saw a start the matcher did not recognise => new level (or first run): re-learn start frame
    if (accepted && barStart && !matchStart) captureStartNext_ = true;
    if (captureStartNext_) {
        matcher_.capture(f);
        captureStartNext_ = false;
    }

    // ---- smart auto play ----
    if (botActive() && botMode_ == BotMode::Smart) {
        PilotDecision d = pilot_.update(s, cfg, f.timeUs);
        lastDecision_ = d;
        if (d.tapOnly) {
            if (d.hold) tap(0, f.timeUs, out);
        } else {
            setHeld(0, d.hold, f.timeUs, out);
        }
    }
}

void BotEngine::onTick(int64_t nowUs, std::vector<InputCmd>& out) {
    for (int p = 0; p < 2; ++p)
        if (releaseAtUs_[p] >= 0 && nowUs >= releaseAtUs_[p]) {
            held_[p] = false;
            releaseAtUs_[p] = -1;
            out.push_back({CmdType::Release, p});
        }

    if (botActive() && botMode_ == BotMode::Macro && player_.running()) {
        std::vector<MacroEvent> due;
        player_.poll(nowUs, due);
        for (const auto& e : due) setHeld(e.player, e.press, nowUs, out);
    }

    if (cfg.practiceAssist && !stopped_ && !paused_ && (autoPlay_ || recorder_.active() || tracker_.inAttempt())) {
        if (lastCheckpointUs_ == 0) lastCheckpointUs_ = nowUs;
        if (nowUs - lastCheckpointUs_ >= (int64_t)cfg.checkpointIntervalMs * 1000) {
            out.push_back({CmdType::PlaceCheckpoint, 0});
            lastCheckpointUs_ = nowUs;
        }
    }
}

bool BotEngine::onUserInput(int64_t tUs, bool press, int player, std::vector<InputCmd>& out) {
    recorder_.onInput(tUs, press, player);
    if (press && botActive() && cfg.userInputTakesOver) {
        setAutoPlay(false, tUs, out);
        return true;
    }
    return false;
}

int64_t BotEngine::nextDeadlineUs() const {
    int64_t t = -1;
    auto consider = [&](int64_t v) { if (v >= 0 && (t < 0 || v < t)) t = v; };
    consider(releaseAtUs_[0]);
    consider(releaseAtUs_[1]);
    if (botActive() && botMode_ == BotMode::Macro) consider(player_.nextEventUs());
    return t;
}

}  // namespace gdbot
