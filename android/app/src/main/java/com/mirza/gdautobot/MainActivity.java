package com.mirza.gdautobot;

import android.Manifest;
import android.app.Activity;
import android.content.ComponentName;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.media.projection.MediaProjectionManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.text.TextUtils;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Map;

/** Setup screen: permissions (explained), status, settings, profiles, macro import/export. */
public class MainActivity extends Activity {
    private static final int REQ_CAPTURE = 1, REQ_IMPORT = 2, REQ_EXPORT = 3, REQ_NOTIF = 4;
    private static final String GD_PACKAGE = "com.robtopx.geometryjump";

    private final Handler ui = new Handler(Looper.getMainLooper());
    private final Runnable refresher = new Runnable() {
        @Override public void run() { refresh(); ui.postDelayed(this, 500); }
    };

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        setContentView(R.layout.activity_main);

        if (Build.VERSION.SDK_INT >= 33 &&
                checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED)
            requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS}, REQ_NOTIF);

        on(R.id.btnPermOverlay, () -> startActivity(new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                Uri.parse("package:" + getPackageName()))));
        on(R.id.btnPermAccess, () -> startActivity(new Intent(Settings.ACTION_ACCESSIBILITY_SETTINGS)));
        on(R.id.btnRestricted, () -> startActivity(new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
                Uri.parse("package:" + getPackageName()))));
        on(R.id.btnStart, this::startOrStop);
        on(R.id.btnOpenGd, () -> {
            Intent i = getPackageManager().getLaunchIntentForPackage(GD_PACKAGE);
            if (i != null) startActivity(i);
            else toast("Geometry Dash bulunamadı (" + GD_PACKAGE + ")");
        });

        // main status controls (work when the service runs)
        on(R.id.btnAutoPlay, () -> svc(NativeBridge.C_AUTOPLAY_TOGGLE));
        on(R.id.btnModeSmart, () -> svc(NativeBridge.C_SET_SMART));
        on(R.id.btnModeMacro, () -> svc(NativeBridge.C_SET_MACRO));

        // settings steppers
        stepper(R.id.btnDelayM, R.id.btnDelayP, "inputDelayMs", 5, 0, 300);
        stepper(R.id.btnOffsetM, R.id.btnOffsetP, "timingOffsetMs", 1, -500, 500);
        stepper(R.id.btnTapM, R.id.btnTapP, "tapDurationMs", 5, 5, 500);
        stepper(R.id.btnCpM, R.id.btnCpP, "checkpointIntervalMs", 250, 500, 20000);
        toggle(R.id.swPractice, "practiceAssist", false);
        toggle(R.id.swRetry, "autoRetry", true);
        toggle(R.id.swBar, "progressSync", true);
        toggle(R.id.swTakeover, "userInputTakesOver", true);

        EditText lvl = findViewById(R.id.editLevel);
        lvl.setText(String.valueOf(ConfigStore.levelId(this)));
        on(R.id.btnLoadProfile, this::loadProfile);
        on(R.id.btnSaveProfile, this::saveProfile);
        on(R.id.btnImport, () -> {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);
            i.setType("*/*");
            startActivityForResult(i, REQ_IMPORT);
        });
        on(R.id.btnExport, () -> {
            Intent i = new Intent(Intent.ACTION_CREATE_DOCUMENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);
            i.setType("application/octet-stream");
            i.putExtra(Intent.EXTRA_TITLE, "level_" + levelId() + ".gdbm");
            startActivityForResult(i, REQ_EXPORT);
        });
    }

    @Override protected void onResume() { super.onResume(); ui.post(refresher); }
    @Override protected void onPause() { super.onPause(); ui.removeCallbacks(refresher); }

    private void on(int id, Runnable r) { findViewById(id).setOnClickListener(v -> r.run()); }
    private void toast(String s) { Toast.makeText(this, s, Toast.LENGTH_LONG).show(); }

    private void svc(int action) {
        BotService s = BotService.instance;
        if (s == null) { toast("Önce botu başlat (ekran yakalama)"); return; }
        s.control(action);
        refresh();
    }

    private int levelId() {
        try { return Integer.parseInt(((EditText) findViewById(R.id.editLevel)).getText().toString().trim()); }
        catch (NumberFormatException e) { return 0; }
    }

    // ---------------- permissions ----------------
    private boolean overlayOk() { return Settings.canDrawOverlays(this); }

    private boolean accessibilityOk() {
        String enabled = Settings.Secure.getString(getContentResolver(), Settings.Secure.ENABLED_ACCESSIBILITY_SERVICES);
        if (enabled == null) return false;
        ComponentName me = new ComponentName(this, BotAccessibilityService.class);
        TextUtils.SimpleStringSplitter sp = new TextUtils.SimpleStringSplitter(':');
        sp.setString(enabled);
        while (sp.hasNext()) {
            ComponentName c = ComponentName.unflattenFromString(sp.next());
            if (me.equals(c)) return true;
        }
        return false;
    }

    private void startOrStop() {
        if (BotService.running()) {
            stopService(new Intent(this, BotService.class));
            ui.postDelayed(this::refresh, 300);
            return;
        }
        if (!overlayOk()) { toast("Önce 1. adım: üstte gösterme izni"); return; }
        if (!accessibilityOk()) toast("Uyarı: Erişilebilirlik kapalı → bot ekranı görür ama dokunamaz");
        ConfigStore.setLevelId(this, levelId());
        MediaProjectionManager mpm = (MediaProjectionManager) getSystemService(MEDIA_PROJECTION_SERVICE);
        startActivityForResult(mpm.createScreenCaptureIntent(), REQ_CAPTURE);
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req == REQ_CAPTURE) {
            if (res != RESULT_OK || data == null) { toast("Ekran yakalama reddedildi"); return; }
            // Android 14+: the foreground service must be started before getMediaProjection()
            startForegroundService(BotService.startIntent(this, res, data));
            toast("Bot başlatıldı. Geometry Dash'i aç; panel oyunun üstünde.");
        } else if (req == REQ_IMPORT && res == RESULT_OK && data != null) {
            try (InputStream in = getContentResolver().openInputStream(data.getData())) {
                String text = ConfigStore.readAll(in);
                File f = ConfigStore.macroFile(this, levelId());
                ConfigStore.writeFile(f, text);
                BotService s = BotService.instance;
                String err = s != null ? s.loadMacroText(text) : "";
                toast(err.isEmpty() ? "Macro içe aktarıldı → Level " + levelId() : "Macro hatalı: " + err);
            } catch (Exception e) {
                toast("Okunamadı: " + e.getMessage());
            }
        } else if (req == REQ_EXPORT && res == RESULT_OK && data != null) {
            BotService s = BotService.instance;
            String text = s != null ? s.currentMacroText() : "";
            if (text.isEmpty()) text = ConfigStore.readFile(ConfigStore.macroFile(this, levelId()));
            if (text == null || text.isEmpty()) { toast("Dışa aktarılacak macro yok"); return; }
            try (OutputStream out = getContentResolver().openOutputStream(data.getData())) {
                out.write(text.getBytes(StandardCharsets.UTF_8));
                toast("Macro dışa aktarıldı");
            } catch (Exception e) {
                toast("Yazılamadı: " + e.getMessage());
            }
        }
    }

    // ---------------- settings ----------------
    private void stepper(int minus, int plus, String key, int step, int min, int max) {
        View.OnClickListener l = v -> {
            Map<String, String> c = ConfigStore.load(this);
            int cur;
            try { cur = Integer.parseInt(c.containsKey(key) ? c.get(key) : defaultOf(key)); }
            catch (NumberFormatException e) { cur = Integer.parseInt(defaultOf(key)); }
            cur = Math.max(min, Math.min(max, cur + (v.getId() == plus ? step : -step)));
            c.put(key, String.valueOf(cur));
            ConfigStore.save(this, c);
            refresh();
        };
        findViewById(minus).setOnClickListener(l);
        findViewById(plus).setOnClickListener(l);
    }

    private static String defaultOf(String key) {
        switch (key) {
            case "inputDelayMs": return "45";
            case "tapDurationMs": return "40";
            case "checkpointIntervalMs": return "2000";
            default: return "0";
        }
    }

    private void toggle(int id, String key, boolean def) {
        Switch s = findViewById(id);
        s.setOnClickListener(v -> {
            Map<String, String> c = ConfigStore.load(this);
            c.put(key, s.isChecked() ? "1" : "0");
            ConfigStore.save(this, c);
        });
    }

    private void loadProfile() {
        int lvl = levelId();
        ConfigStore.setLevelId(this, lvl);
        String text = ConfigStore.readFile(ConfigStore.profileFile(this, lvl));
        if (text == null) { toast("Level " + lvl + " için profil yok"); return; }
        Map<String, String> c = ConfigStore.parse(text);
        c.put("levelId", String.valueOf(lvl));
        ConfigStore.save(this, c);
        String macro = ConfigStore.readFile(ConfigStore.macroFile(this, lvl));
        BotService s = BotService.instance;
        if (macro != null && s != null) s.loadMacroText(macro);
        toast("Profil yüklendi: Level " + lvl + (macro != null ? " (+ macro)" : ""));
        refresh();
    }

    private void saveProfile() {
        int lvl = levelId();
        ConfigStore.setLevelId(this, lvl);
        Map<String, String> c = ConfigStore.load(this);
        c.put("levelId", String.valueOf(lvl));
        ConfigStore.save(this, c);
        boolean ok = ConfigStore.writeFile(ConfigStore.profileFile(this, lvl), ConfigStore.format(c));
        toast(ok ? "Profil kaydedildi: Level " + lvl : "Profil yazılamadı");
    }

    // ---------------- status ----------------
    private void setPerm(int id, boolean ok) {
        TextView t = findViewById(id);
        t.setText(ok ? "✓ Açık" : "✗ Kapalı");
        t.setTextColor(ok ? 0xFF22C55E : 0xFFEF4444);
    }

    private void refresh() {
        setPerm(R.id.txtOverlayState, overlayOk());
        setPerm(R.id.txtAccessState, accessibilityOk());
        boolean running = BotService.running();
        setPerm(R.id.txtCaptureState, running);
        ((Button) findViewById(R.id.btnStart)).setText(running ? "BOTU DURDUR" : "BOTU BAŞLAT (ekran yakalama)");
        findViewById(R.id.btnRestricted).setVisibility(Build.VERSION.SDK_INT >= 33 && !accessibilityOk() ? View.VISIBLE : View.GONE);

        Map<String, String> s = running ? ConfigStore.parse(BotService.instance.statusText()) : null;
        boolean auto = s != null && "1".equals(s.get("autoPlay"));
        boolean macro = s != null && "MACRO".equals(s.get("botMode"));
        Button ba = findViewById(R.id.btnAutoPlay);
        ba.setText(auto ? "ON" : "OFF");
        ba.setBackgroundResource(auto ? R.drawable.btn_yellow : R.drawable.btn_dark);
        ba.setTextColor(auto ? 0xFF1C1430 : 0xFFECE8F8);
        findViewById(R.id.btnModeSmart).setBackgroundResource(!macro ? R.drawable.btn_purple : R.drawable.btn_dark);
        findViewById(R.id.btnModeMacro).setBackgroundResource(macro ? R.drawable.btn_purple : R.drawable.btn_dark);
        ((TextView) findViewById(R.id.txtStatus)).setText(s == null ? "STOPPED" : s.get("status"));
        ((TextView) findViewById(R.id.txtStatusLine)).setText(s == null ? "Servis kapalı" : s.get("line"));
        ((TextView) findViewById(R.id.txtMacro)).setText(s == null ? "—" :
                ("0".equals(s.get("macroEvents")) ? "Macro yüklü değil" :
                        "Macro: " + s.get("macroEvents") + " olay · " + s.get("macroSec").replaceAll("0+$", "") + " sn"));

        Map<String, String> c = ConfigStore.load(this);
        ((TextView) findViewById(R.id.txtDelay)).setText(get(c, "inputDelayMs") + " ms");
        ((TextView) findViewById(R.id.txtOffset)).setText(get(c, "timingOffsetMs") + " ms");
        ((TextView) findViewById(R.id.txtTap)).setText(get(c, "tapDurationMs") + " ms");
        ((TextView) findViewById(R.id.txtCp)).setText(get(c, "checkpointIntervalMs") + " ms");
        ((Switch) findViewById(R.id.swPractice)).setChecked("1".equals(c.get("practiceAssist")));
        ((Switch) findViewById(R.id.swRetry)).setChecked(!"0".equals(c.get("autoRetry")));
        ((Switch) findViewById(R.id.swBar)).setChecked(!"0".equals(c.get("progressSync")));
        ((Switch) findViewById(R.id.swTakeover)).setChecked(!"0".equals(c.get("userInputTakesOver")));
    }

    private static String get(Map<String, String> c, String k) {
        return c.containsKey(k) ? c.get(k) : defaultOf(k);
    }
}
