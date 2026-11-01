#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include "gdbot/config.hpp"
#include "gdbot/types.hpp"

namespace gdbot {

static const char* kModeNames[] = {"Cube", "Ship", "Ball", "UFO", "Wave", "Robot", "Spider", "Swing"};

const char* gameModeName(GameMode m) {
    int i = (int)m;
    return (i >= 0 && i < (int)GameMode::Count) ? kModeNames[i] : "?";
}

GameMode gameModeFromName(const std::string& s) {
    for (int i = 0; i < (int)GameMode::Count; ++i) {
        std::string a = kModeNames[i], b = s;
        for (auto& c : a) c = (char)toupper((unsigned char)c);
        for (auto& c : b) c = (char)toupper((unsigned char)c);
        if (a == b) return (GameMode)i;
    }
    return GameMode::Cube;
}

const char* botModeName(BotMode m) { return m == BotMode::Smart ? "SMART" : "MACRO"; }

const char* statusName(Status s) {
    switch (s) {
        case Status::Ready: return "READY";
        case Status::Playing: return "PLAYING";
        case Status::Recording: return "RECORDING";
        case Status::Paused: return "PAUSED";
        case Status::Stopped: return "STOPPED";
    }
    return "?";
}

// ---------------- Config ----------------

std::string Config::serialize() const {
    std::ostringstream o;
    o << "# GD AutoBot profile (game " << kTargetGameVersion << ")\n";
    o << "inputDelayMs=" << inputDelayMs << "\n";
    o << "timingOffsetMs=" << timingOffsetMs << "\n";
    o << "tapDurationMs=" << tapDurationMs << "\n";
    o << "gameMode=" << gameModeName(gameMode) << "\n";
    o << "speed=" << speed << "\n";
    o << "mini=" << mini << "\n";
    o << "upsideDown=" << upsideDown << "\n";
    o << "dual=" << dual << "\n";
    o << "playerColor=" << (int)playerColor.r << "," << (int)playerColor.g << "," << (int)playerColor.b << "\n";
    o << "playerX=" << playerX << "\n";
    o << "colorTolerance=" << colorTolerance << "\n";
    o << "obstacleThreshold=" << obstacleThreshold << "\n";
    o << "lookaheadBlocks=" << lookaheadBlocks << "\n";
    o << "jumpAirtimeMs=" << jumpAirtimeMs << "\n";
    o << "blockRel=" << blockRel << "\n";
    o << "uiBandRel=" << uiBandRel << "\n";
    o << "progressSync=" << progressSync << "\n";
    o << "barX0=" << barX0 << "\nbarX1=" << barX1 << "\nbarY=" << barY << "\n";
    o << "barThreshold=" << barThreshold << "\n";
    o << "practiceAssist=" << practiceAssist << "\n";
    o << "checkpointIntervalMs=" << checkpointIntervalMs << "\n";
    o << "cpPlaceX=" << cpPlaceX << "\ncpPlaceY=" << cpPlaceY << "\n";
    o << "cpRemoveX=" << cpRemoveX << "\ncpRemoveY=" << cpRemoveY << "\n";
    o << "autoRetry=" << autoRetry << "\n";
    o << "retryAfterMs=" << retryAfterMs << "\n";
    o << "retryX=" << retryX << "\nretryY=" << retryY << "\n";
    o << "userInputTakesOver=" << userInputTakesOver << "\n";
    o << "levelId=" << levelId << "\n";
    o << "macroFile=" << macroFile << "\n";
    return o.str();
}

Config Config::parse(const std::string& text) {
    Config c;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        const char* s = v.c_str();
        auto I = [&](int& dst) { if (!v.empty()) dst = atoi(s); };
        auto F = [&](float& dst) { if (!v.empty()) dst = (float)atof(s); };
        auto B = [&](bool& dst) { if (!v.empty()) dst = (v == "1" || v == "true"); };
        if (k == "inputDelayMs") I(c.inputDelayMs);
        else if (k == "timingOffsetMs") I(c.timingOffsetMs);
        else if (k == "tapDurationMs") I(c.tapDurationMs);
        else if (k == "gameMode") c.gameMode = gameModeFromName(v);
        else if (k == "speed") F(c.speed);
        else if (k == "mini") B(c.mini);
        else if (k == "upsideDown") B(c.upsideDown);
        else if (k == "dual") B(c.dual);
        else if (k == "playerColor") {
            int r = 0, g = 0, b = 0;
            if (sscanf(s, "%d,%d,%d", &r, &g, &b) == 3) c.playerColor = {(uint8_t)r, (uint8_t)g, (uint8_t)b};
        }
        else if (k == "playerX") F(c.playerX);
        else if (k == "colorTolerance") I(c.colorTolerance);
        else if (k == "obstacleThreshold") I(c.obstacleThreshold);
        else if (k == "lookaheadBlocks") F(c.lookaheadBlocks);
        else if (k == "jumpAirtimeMs") I(c.jumpAirtimeMs);
        else if (k == "blockRel") F(c.blockRel);
        else if (k == "uiBandRel") F(c.uiBandRel);
        else if (k == "progressSync") B(c.progressSync);
        else if (k == "barX0") F(c.barX0);
        else if (k == "barX1") F(c.barX1);
        else if (k == "barY") F(c.barY);
        else if (k == "barThreshold") I(c.barThreshold);
        else if (k == "practiceAssist") B(c.practiceAssist);
        else if (k == "checkpointIntervalMs") I(c.checkpointIntervalMs);
        else if (k == "cpPlaceX") F(c.cpPlaceX);
        else if (k == "cpPlaceY") F(c.cpPlaceY);
        else if (k == "cpRemoveX") F(c.cpRemoveX);
        else if (k == "cpRemoveY") F(c.cpRemoveY);
        else if (k == "autoRetry") B(c.autoRetry);
        else if (k == "retryAfterMs") I(c.retryAfterMs);
        else if (k == "retryX") F(c.retryX);
        else if (k == "retryY") F(c.retryY);
        else if (k == "userInputTakesOver") B(c.userInputTakesOver);
        else if (k == "levelId") I(c.levelId);
        else if (k == "macroFile") c.macroFile = v;
    }
    return c;
}

}  // namespace gdbot
