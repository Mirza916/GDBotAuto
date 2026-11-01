#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace gdbot {

struct MacroEvent {
    int64_t tUs = 0;      // time since attempt start (sync point)
    bool press = false;
    uint8_t player = 0;
};

// Time-based macro. GD 2.2 runs physics on fixed ticks (240 TPS), so inputs
// that land within ~4 ms of the recorded time reproduce the same run.
struct Macro {
    int levelId = 0;
    std::string gameVersion = "2.209";
    std::string platform = "unknown";
    float bestProgress = 0.f;
    std::vector<MacroEvent> events;

    int64_t durationUs() const { return events.empty() ? 0 : events.back().tUs; }
    std::string serialize() const;
    static bool parse(const std::string& text, Macro& out, std::string* err = nullptr);
};

// Records inputs per attempt. Every attempt start (progress bar reset or manual
// sync) begins a new take; the take that reached the highest progress is kept.
class MacroRecorder {
public:
    void start(int64_t nowUs);
    void onAttemptStart(int64_t syncUs);
    void onInput(int64_t tUs, bool press, int player);
    void onProgress(float fraction);
    Macro stop();
    bool active() const { return active_; }
    size_t currentEvents() const { return take_.size(); }
    float bestProgress() const { return bestProgress_; }

private:
    void commitTake();
    bool active_ = false;
    int64_t syncUs_ = 0;
    std::vector<MacroEvent> take_;
    float takeProgress_ = 0.f;
    std::vector<MacroEvent> best_;
    float bestProgress_ = -1.f;
    bool held_[2] = {false, false};
};

class MacroPlayer {
public:
    void load(const Macro& m) { macro_ = m; idx_ = 0; running_ = false; }
    void start(int64_t syncUs, int64_t offsetUs);
    void stop() { running_ = false; }
    // Appends every event due at nowUs. Returns number appended.
    int poll(int64_t nowUs, std::vector<MacroEvent>& out);
    // Absolute time of next event, or -1.
    int64_t nextEventUs() const;
    bool running() const { return running_; }
    bool finished() const { return idx_ >= macro_.events.size(); }
    const Macro& macro() const { return macro_; }

private:
    Macro macro_;
    size_t idx_ = 0;
    int64_t baseUs_ = 0;
    bool running_ = false;
};

}  // namespace gdbot
