// JNI bridge: exposes the shared C++ bot core (../../../../core) to Java.
// All calls for one handle must be serialized by the caller (BotService does that).
#include <jni.h>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "gdbot/engine.hpp"

using namespace gdbot;

namespace {

struct Bot {
    BotEngine eng;
    std::vector<uint8_t> lastFrame;
    int lastW = 0, lastH = 0, lastStride = 0;
};

Bot* B(jlong h) { return reinterpret_cast<Bot*>(h); }

std::string str(JNIEnv* env, jstring s) {
    if (!s) return {};
    const char* c = env->GetStringUTFChars(s, nullptr);
    std::string r(c ? c : "");
    if (c) env->ReleaseStringUTFChars(s, c);
    return r;
}

// Commands are returned as int pairs: [type, player, type, player, ...]
jintArray cmdsToJava(JNIEnv* env, const std::vector<InputCmd>& cmds) {
    std::vector<jint> v;
    v.reserve(cmds.size() * 2);
    for (const auto& c : cmds) { v.push_back((jint)c.type); v.push_back(c.player); }
    jintArray a = env->NewIntArray((jsize)v.size());
    if (!v.empty()) env->SetIntArrayRegion(a, 0, (jsize)v.size(), v.data());
    return a;
}

enum Control {
    C_AUTOPLAY_TOGGLE = 1, C_SET_SMART, C_SET_MACRO, C_REC_START, C_SYNC, C_PAUSE_TOGGLE, C_STOP,
    C_GAMEMODE_NEXT, C_SPEED_NEXT, C_GRAVITY_TOGGLE, C_MINI_TOGGLE, C_AUTOPLAY_OFF
};

}  // namespace

#define FN(name) Java_com_mirza_gdautobot_NativeBridge_##name

extern "C" {

JNIEXPORT jlong JNICALL FN(create)(JNIEnv*, jclass) { return reinterpret_cast<jlong>(new Bot()); }

JNIEXPORT void JNICALL FN(destroy)(JNIEnv*, jclass, jlong h) { delete B(h); }

JNIEXPORT void JNICALL FN(setConfig)(JNIEnv* env, jclass, jlong h, jstring text) {
    Config c = Config::parse(str(env, text));
    B(h)->eng.cfg = c;
}

JNIEXPORT jstring JNICALL FN(getConfig)(JNIEnv* env, jclass, jlong h) {
    return env->NewStringUTF(B(h)->eng.cfg.serialize().c_str());
}

JNIEXPORT jintArray JNICALL FN(onFrame)(JNIEnv* env, jclass, jlong h, jobject buf, jint w, jint hgt, jint rowStride,
                                        jlong timeUs, jboolean keepCopy) {
    Bot* b = B(h);
    std::vector<InputCmd> out;
    auto* data = static_cast<const uint8_t*>(env->GetDirectBufferAddress(buf));
    if (!data) return cmdsToJava(env, out);
    Frame f{data, w, hgt, rowStride, PixelFormat::RGBA, (int64_t)timeUs};
    b->eng.onFrame(f, out);
    if (keepCopy) {
        b->lastFrame.assign(data, data + (size_t)rowStride * hgt);
        b->lastW = w; b->lastH = hgt; b->lastStride = rowStride;
    }
    return cmdsToJava(env, out);
}

JNIEXPORT jintArray JNICALL FN(onTick)(JNIEnv* env, jclass, jlong h, jlong nowUs) {
    std::vector<InputCmd> out;
    B(h)->eng.onTick((int64_t)nowUs, out);
    return cmdsToJava(env, out);
}

JNIEXPORT jlong JNICALL FN(nextDeadlineUs)(JNIEnv*, jclass, jlong h) { return (jlong)B(h)->eng.nextDeadlineUs(); }

// returns commands; element [0..] as usual. Java checks autoPlay afterwards for takeover.
JNIEXPORT jintArray JNICALL FN(onUserInput)(JNIEnv* env, jclass, jlong h, jlong tUs, jboolean press, jint player) {
    std::vector<InputCmd> out;
    B(h)->eng.onUserInput((int64_t)tUs, press, player, out);
    return cmdsToJava(env, out);
}

JNIEXPORT jintArray JNICALL FN(control)(JNIEnv* env, jclass, jlong h, jint action, jlong nowUs) {
    BotEngine& e = B(h)->eng;
    std::vector<InputCmd> out;
    Config& c = e.cfg;
    switch (action) {
        case C_AUTOPLAY_TOGGLE: e.setAutoPlay(!e.autoPlay(), nowUs, out); break;
        case C_AUTOPLAY_OFF: e.setAutoPlay(false, nowUs, out); break;
        case C_SET_SMART: e.setBotMode(BotMode::Smart, nowUs, out); break;
        case C_SET_MACRO: e.setBotMode(BotMode::Macro, nowUs, out); break;
        case C_REC_START: e.startRecording(nowUs, out); break;
        case C_SYNC: e.manualSync(nowUs, out); break;
        case C_PAUSE_TOGGLE: e.togglePause(nowUs, out); break;
        case C_STOP: e.emergencyStop(out); break;
        case C_GAMEMODE_NEXT: e.setGameMode((GameMode)(((int)c.gameMode + 1) % (int)GameMode::Count)); break;
        case C_SPEED_NEXT: {
            static const float sp[] = {0.5f, 1.f, 2.f, 3.f, 4.f};
            int i = 0;
            while (i < 5 && sp[i] < c.speed - 0.01f) ++i;
            c.speed = sp[(i + 1) % 5];
            break;
        }
        case C_GRAVITY_TOGGLE: c.upsideDown = !c.upsideDown; break;
        case C_MINI_TOGGLE: c.mini = !c.mini; break;
        default: break;
    }
    return cmdsToJava(env, out);
}

// Stops recording and returns the macro file text ("" if nothing recorded).
JNIEXPORT jstring JNICALL FN(stopRecording)(JNIEnv* env, jclass, jlong h) {
    BotEngine& e = B(h)->eng;
    if (!e.recording()) return env->NewStringUTF("");
    Macro m = e.stopRecording();
    m.platform = "android";
    if (m.events.empty()) return env->NewStringUTF("");
    e.loadMacro(m);
    return env->NewStringUTF(m.serialize().c_str());
}

// Loads macro text. Returns "" on success, otherwise the error.
JNIEXPORT jstring JNICALL FN(loadMacro)(JNIEnv* env, jclass, jlong h, jstring text) {
    Macro m;
    std::string err;
    if (!Macro::parse(str(env, text), m, &err)) return env->NewStringUTF(err.c_str());
    B(h)->eng.loadMacro(m);
    return env->NewStringUTF("");
}

JNIEXPORT jstring JNICALL FN(currentMacro)(JNIEnv* env, jclass, jlong h) {
    BotEngine& e = B(h)->eng;
    return env->NewStringUTF(e.hasMacro() ? e.macro().serialize().c_str() : "");
}

// Status as simple key=value lines (parsed in Java).
JNIEXPORT jstring JNICALL FN(status)(JNIEnv* env, jclass, jlong h) {
    BotEngine& e = B(h)->eng;
    std::string s;
    s += "status=" + std::string(statusName(e.status())) + "\n";
    s += "autoPlay=" + std::string(e.autoPlay() ? "1" : "0") + "\n";
    s += "botMode=" + std::string(botModeName(e.botMode())) + "\n";
    s += "recording=" + std::string(e.recording() ? "1" : "0") + "\n";
    s += "fps=" + std::to_string((int)(e.analysisFps() + 0.5f)) + "\n";
    s += "progress=" + std::to_string(e.progress()) + "\n";
    s += "attempts=" + std::to_string(e.attempts()) + "\n";
    s += "gameMode=" + std::string(gameModeName(e.cfg.gameMode)) + "\n";
    s += "speed=" + std::to_string(e.cfg.speed) + "\n";
    s += "playerFound=" + std::string(e.scene().player.found ? "1" : "0") + "\n";
    s += "macroEvents=" + std::to_string(e.hasMacro() ? e.macro().events.size() : 0) + "\n";
    s += "macroSec=" + std::to_string(e.hasMacro() ? e.macro().durationUs() / 1e6 : 0.0) + "\n";
    s += "reason=" + std::string(e.lastDecision().reason) + "\n";
    s += "line=" + e.statusLine() + "\n";
    return env->NewStringUTF(s.c_str());
}

// Calibration from the last kept frame. what: 0 player, 1 bar left, 2 bar right, 3 retry button,
// 4 checkpoint button. Returns a short description.
JNIEXPORT jstring JNICALL FN(calibrate)(JNIEnv* env, jclass, jlong h, jfloat rx, jfloat ry, jint what) {
    Bot* b = B(h);
    Config& c = b->eng.cfg;
    char buf[160];
    switch (what) {
        case 0: {
            if (b->lastW == 0) return env->NewStringUTF("Henüz kare yok: ekran yakalama çalışıyor mu?");
            Frame f{b->lastFrame.data(), b->lastW, b->lastH, b->lastStride, PixelFormat::RGBA, 0};
            Rgb col = samplePixel(f, (int)(rx * b->lastW), (int)(ry * b->lastH));
            c.playerColor = col;
            c.playerX = rx;
            snprintf(buf, sizeof buf, "Oyuncu: renk (%d,%d,%d), x=%.2f", col.r, col.g, col.b, rx);
            break;
        }
        case 1: c.barX0 = rx; c.barY = ry; snprintf(buf, sizeof buf, "Çubuk sol ucu: %.2f, %.3f", rx, ry); break;
        case 2: c.barX1 = rx; c.barY = (c.barY + ry) * 0.5f; snprintf(buf, sizeof buf, "Çubuk sağ ucu: %.2f", rx); break;
        case 3: c.retryX = rx; c.retryY = ry; snprintf(buf, sizeof buf, "Retry: %.2f, %.2f", rx, ry); break;
        case 4: c.cpPlaceX = rx; c.cpPlaceY = ry; snprintf(buf, sizeof buf, "Checkpoint: %.2f, %.2f", rx, ry); break;
        default: snprintf(buf, sizeof buf, "?");
    }
    return env->NewStringUTF(buf);
}

}  // extern "C"
