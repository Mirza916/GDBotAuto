import com.mirza.gdautobot.NativeBridge;
import java.nio.ByteBuffer;

/** Host JVM smoke test of the Android JNI bridge (same .so code, built for Linux). */
public class JniSmoke {
    static int fails = 0;
    static void check(boolean c, String what) { System.out.println((c ? "  ok   " : "  FAIL ") + what); if (!c) fails++; }

    static ByteBuffer frame(int w, int h, int stride, float prog) {
        ByteBuffer b = ByteBuffer.allocateDirect(stride * h);
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
            int o = y * stride + x * 4;
            int r = 30, g = 60, bl = 150;                       // background
            if (y > h * 3 / 4) { r = 10; g = 20; bl = 70; }    // ground
            int px = (int) (0.3f * w), bs = (int) (h * 30f / 320f);
            if (x >= px - bs / 2 && x < px + bs / 2 && y > h * 3 / 4 - bs && y <= h * 3 / 4) { r = 0; g = 255; bl = 0; }
            int by = (int) (0.022f * h), bx0 = (int) (0.32f * w), bx1 = (int) (0.68f * w);
            if (y >= by - 1 && y <= by + 1 && x >= bx0 && x < bx1) {
                if (x < bx0 + (bx1 - bx0) * prog) { r = 90; g = 230; bl = 90; } else { r = 45; g = 45; bl = 45; }
            }
            b.put(o, (byte) r); b.put(o + 1, (byte) g); b.put(o + 2, (byte) bl); b.put(o + 3, (byte) 255);
        }
        return b;
    }

    public static void main(String[] a) {
        long h = NativeBridge.create();
        check(h != 0, "create()");
        NativeBridge.setConfig(h, "uiBandRel=0.10\ninputDelayMs=45\nlevelId=77\n");
        check(NativeBridge.getConfig(h).contains("inputDelayMs=45"), "setConfig/getConfig roundtrip");
        int w = 400, hh = 225, stride = 1664;   // rowStride with padding like real ImageReader planes
        long t = 1_000_000;
        int[] c = NativeBridge.onFrame(h, frame(w, hh, stride, 0f), w, hh, stride, t, true);
        String st = NativeBridge.status(h);
        check(st.contains("playerFound=1"), "player found in RGBA frame with padded stride");
        check(st.contains("attempts=1"), "progress bar 0% => attempt start");
        String cal = NativeBridge.calibrate(h, 0.3f, 0.70f, NativeBridge.CAL_PLAYER);
        check(cal.contains("(0,255,0)"), "calibrate player color: " + cal);
        c = NativeBridge.control(h, NativeBridge.C_AUTOPLAY_TOGGLE, t);
        check(NativeBridge.status(h).contains("status=PLAYING"), "AUTO PLAY ON => PLAYING");
        c = NativeBridge.control(h, NativeBridge.C_STOP, t);
        check(NativeBridge.status(h).contains("status=STOPPED"), "emergency stop => STOPPED");
        // record via user input (record pad path)
        NativeBridge.control(h, NativeBridge.C_REC_START, t);
        NativeBridge.control(h, NativeBridge.C_SYNC, t);
        NativeBridge.onUserInput(h, t + 100_000, true, 0);
        NativeBridge.onUserInput(h, t + 140_000, false, 0);
        NativeBridge.onFrame(h, frame(w, hh, stride, 0.2f), w, hh, stride, t + 200_000, false);
        String macro = NativeBridge.stopRecording(h);
        check(macro.startsWith("GDBOTMACRO 1") && macro.contains("100000 1 0") && macro.contains("platform android"),
              "record -> macro text");
        check(NativeBridge.loadMacro(h, macro).isEmpty(), "loadMacro(valid)");
        check(!NativeBridge.loadMacro(h, "junk").isEmpty(), "loadMacro(invalid) reports error");
        // macro replay through control + ticks
        NativeBridge.control(h, NativeBridge.C_SET_MACRO, t);
        NativeBridge.control(h, NativeBridge.C_AUTOPLAY_TOGGLE, t);
        NativeBridge.control(h, NativeBridge.C_SYNC, 5_000_000);
        long next = NativeBridge.nextDeadlineUs(h);
        check(next == 5_100_000, "next deadline = sync + first event (" + next + ")");
        int[] cmds = NativeBridge.onTick(h, next);
        check(cmds.length == 2 && cmds[0] == NativeBridge.CMD_PRESS, "tick emits PRESS at event time");
        cmds = NativeBridge.onTick(h, 5_140_000);
        check(cmds.length == 2 && cmds[0] == NativeBridge.CMD_RELEASE, "tick emits RELEASE");
        NativeBridge.destroy(h);
        System.out.println(fails == 0 ? "JNI SMOKE: ALL OK" : "JNI SMOKE: " + fails + " FAILED");
        System.exit(fails == 0 ? 0 : 1);
    }
}
