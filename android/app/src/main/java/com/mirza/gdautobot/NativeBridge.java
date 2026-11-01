package com.mirza.gdautobot;

import java.nio.ByteBuffer;

/** Thin JNI wrapper around the shared C++ core (core/ at the repo root). */
public final class NativeBridge {
    static { System.loadLibrary("gdautobot"); }

    private NativeBridge() {}

    // command types returned as int pairs [type, player, ...] (must match gdbot::CmdType)
    public static final int CMD_PRESS = 0, CMD_RELEASE = 1, CMD_PLACE_CP = 2, CMD_REMOVE_CP = 3, CMD_RETRY = 4;

    // control actions (must match jni_bridge.cpp)
    public static final int C_AUTOPLAY_TOGGLE = 1, C_SET_SMART = 2, C_SET_MACRO = 3, C_REC_START = 4, C_SYNC = 5,
            C_PAUSE_TOGGLE = 6, C_STOP = 7, C_GAMEMODE_NEXT = 8, C_SPEED_NEXT = 9, C_GRAVITY_TOGGLE = 10,
            C_MINI_TOGGLE = 11, C_AUTOPLAY_OFF = 12;

    // calibration targets
    public static final int CAL_PLAYER = 0, CAL_BAR_LEFT = 1, CAL_BAR_RIGHT = 2, CAL_RETRY = 3, CAL_CHECKPOINT = 4;

    public static native long create();
    public static native void destroy(long h);
    public static native void setConfig(long h, String text);
    public static native String getConfig(long h);
    public static native int[] onFrame(long h, ByteBuffer rgba, int w, int hgt, int rowStride, long timeUs, boolean keepCopy);
    public static native int[] onTick(long h, long nowUs);
    public static native long nextDeadlineUs(long h);
    public static native int[] onUserInput(long h, long tUs, boolean press, int player);
    public static native int[] control(long h, int action, long nowUs);
    public static native String stopRecording(long h);
    public static native String loadMacro(long h, String text);
    public static native String currentMacro(long h);
    public static native String status(long h);
    public static native String calibrate(long h, float rx, float ry, int what);
}
