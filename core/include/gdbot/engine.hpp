#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "gdbot/config.hpp"
#include "gdbot/macro.hpp"
#include "gdbot/pilot.hpp"
#include "gdbot/vision.hpp"

namespace gdbot {

// Platform independent bot state machine. Platform layers (Windows / Android):
//  - feed frames with onFrame()
//  - call onTick() often (~1 ms on PC) for precise macro timing
//  - report real user input with onUserInput()
//  - execute the returned InputCmds
// Not thread safe: the platform must serialize calls.
class BotEngine {
public:
    Config cfg;

    // ---- controls ----
    void setAutoPlay(bool on, int64_t nowUs, std::vector<InputCmd>& out);
    bool autoPlay() const { return autoPlay_; }
    void setBotMode(BotMode m, int64_t nowUs, std::vector<InputCmd>& out);
    BotMode botMode() const { return botMode_; }
    void togglePause(int64_t nowUs, std::vector<InputCmd>& out);
    void emergencyStop(std::vector<InputCmd>& out);
    void startRecording(int64_t nowUs, std::vector<InputCmd>& out);
    Macro stopRecording();
    bool recording() const { return recorder_.active(); }
    void loadMacro(const Macro& m) { macro_ = m; hasMacro_ = !m.events.empty(); player_.load(m); }
    bool hasMacro() const { return hasMacro_; }
    const Macro& macro() const { return macro_; }
    void manualSync(int64_t nowUs, std::vector<InputCmd>& out);  // "attempt started now"
    void setGameMode(GameMode m) { cfg.gameMode = m; pilot_.reset(); }

    // ---- feeding ----
    void onFrame(const Frame& f, std::vector<InputCmd>& out);
    void onTick(int64_t nowUs, std::vector<InputCmd>& out);
    // Real (non-injected) user input. Returns true if the bot handed control back.
    bool onUserInput(int64_t tUs, bool press, int player, std::vector<InputCmd>& out);

    // ---- status ----
    Status status() const;
    float analysisFps() const { return fps_; }
    float progress() const { return tracker_.fraction(); }
    int attempts() const { return tracker_.attempts(); }
    const Scene& scene() const { return analyzer_.scene(); }
    const PilotDecision& lastDecision() const { return lastDecision_; }
    std::string statusLine() const;
    // Next time onTick must run (macro/tap deadlines), -1 if none.
    int64_t nextDeadlineUs() const;

private:
    bool attemptStarted(int64_t syncUs, std::vector<InputCmd>& out);
    void setHeld(int player, bool held, int64_t nowUs, std::vector<InputCmd>& out);
    void tap(int player, int64_t nowUs, std::vector<InputCmd>& out);
    void releaseAll(std::vector<InputCmd>& out);
    bool botActive() const { return autoPlay_ && !paused_ && !stopped_; }

    bool autoPlay_ = false;
    bool paused_ = false;
    bool stopped_ = false;
    BotMode botMode_ = BotMode::Smart;
    bool hasMacro_ = false;
    Macro macro_;
    MacroRecorder recorder_;
    MacroPlayer player_;
    Analyzer analyzer_;
    ProgressBarReader barReader_;
    StartFrameMatcher matcher_;
    AttemptTracker tracker_;
    int64_t lastAttemptUs_ = -10000000;
    bool captureStartNext_ = false;
    bool waitingSync_ = false;
    SmartPilot pilot_;
    PilotDecision lastDecision_;

    bool held_[2] = {false, false};
    int64_t releaseAtUs_[2] = {-1, -1};
    int64_t lastCheckpointUs_ = 0;
    int64_t lastRetryUs_ = 0;

    // fps
    int64_t fpsWindowStart_ = 0;
    int fpsFrames_ = 0;
    float fps_ = 0;
};

}  // namespace gdbot
