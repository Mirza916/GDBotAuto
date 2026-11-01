package com.mirza.gdautobot;

import android.content.Context;
import android.content.SharedPreferences;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.LinkedHashMap;
import java.util.Map;

/**
 * Config text (key=value, same format as the C++ Config) + Android-only settings.
 * When BotService runs, the native engine owns the live config.
 */
public final class ConfigStore {
    private ConfigStore() {}

    private static SharedPreferences prefs(Context c) {
        return c.getSharedPreferences("gdautobot", Context.MODE_PRIVATE);
    }

    public static String defaultConfig() {
        // Android defaults that differ from the C++ defaults: bigger UI band (floating
        // panel bubble lives up there), higher latency estimate (gesture dispatch).
        return "uiBandRel=0.10\ninputDelayMs=45\ntapDurationMs=40\n";
    }

    public static Map<String, String> parse(String text) {
        Map<String, String> m = new LinkedHashMap<>();
        if (text == null) return m;
        for (String line : text.split("\n")) {
            line = line.trim();
            if (line.isEmpty() || line.startsWith("#")) continue;
            int eq = line.indexOf('=');
            if (eq > 0) m.put(line.substring(0, eq), line.substring(eq + 1));
        }
        return m;
    }

    public static String format(Map<String, String> m) {
        StringBuilder sb = new StringBuilder();
        for (Map.Entry<String, String> e : m.entrySet()) sb.append(e.getKey()).append('=').append(e.getValue()).append('\n');
        return sb.toString();
    }

    /** Current config: from the running engine, else from preferences. */
    public static Map<String, String> load(Context c) {
        BotService s = BotService.instance;
        if (s != null) return parse(s.getConfigText());
        String t = prefs(c).getString("config", null);
        if (t == null) t = defaultConfig();
        return parse(t);
    }

    public static void save(Context c, Map<String, String> m) {
        String text = format(m);
        prefs(c).edit().putString("config", text).apply();
        BotService s = BotService.instance;
        if (s != null) s.setConfigText(text);
    }

    public static String rawPrefsConfig(Context c) {
        String t = prefs(c).getString("config", null);
        return t == null ? defaultConfig() : t;
    }

    public static void storeRaw(Context c, String text) {
        prefs(c).edit().putString("config", text).apply();
    }

    // ---- Android-only: where the bot touches (relative screen coords) ----
    public static float tapX(Context c, int player) { return prefs(c).getFloat("tapX" + player, player == 0 ? 0.55f : 0.85f); }
    public static float tapY(Context c, int player) { return prefs(c).getFloat("tapY" + player, 0.60f); }
    public static void setTap(Context c, int player, float x, float y) {
        prefs(c).edit().putFloat("tapX" + player, x).putFloat("tapY" + player, y).apply();
    }
    public static int levelId(Context c) { return prefs(c).getInt("levelId", 0); }
    public static void setLevelId(Context c, int id) { prefs(c).edit().putInt("levelId", id).apply(); }

    // ---- files ----
    public static File profileFile(Context c, int level) {
        File d = new File(c.getFilesDir(), "profiles");
        d.mkdirs();
        return new File(d, level + ".cfg");
    }

    public static File macroFile(Context c, int level) {
        File d = new File(c.getFilesDir(), "macros");
        d.mkdirs();
        return new File(d, level + ".gdbm");
    }

    public static String readAll(InputStream in) throws IOException {
        byte[] buf = new byte[8192];
        StringBuilder sb = new StringBuilder();
        java.io.ByteArrayOutputStream bo = new java.io.ByteArrayOutputStream();
        int n;
        while ((n = in.read(buf)) > 0) bo.write(buf, 0, n);
        sb.append(new String(bo.toByteArray(), StandardCharsets.UTF_8));
        return sb.toString();
    }

    public static String readFile(File f) {
        try (InputStream in = new FileInputStream(f)) {
            return readAll(in);
        } catch (IOException e) {
            return null;
        }
    }

    public static boolean writeFile(File f, String text) {
        try (OutputStream out = new FileOutputStream(f)) {
            out.write(text.getBytes(StandardCharsets.UTF_8));
            return true;
        } catch (IOException e) {
            return false;
        }
    }
}
