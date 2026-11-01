package com.mirza.gdautobot;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.content.res.Configuration;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.PixelFormat;
import android.graphics.Rect;
import android.hardware.display.DisplayManager;
import android.hardware.display.VirtualDisplay;
import android.media.Image;
import android.media.ImageReader;
import android.media.projection.MediaProjection;
import android.media.projection.MediaProjectionManager;
import android.os.Build;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.Looper;
import android.util.DisplayMetrics;
import android.view.Gravity;
import android.view.LayoutInflater;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.nio.ByteBuffer;
import java.util.Map;
import java.util.concurrent.locks.LockSupport;

/**
 * Foreground service: screen capture (MediaProjection, user approved), the native
 * bot engine, precise timing thread and the floating control panel.
 */
public class BotService extends Service {
    public static volatile BotService instance;

    public static final String EXTRA_CODE = "code";
    public static final String EXTRA_DATA = "data";
    private static final String CHANNEL = "gdautobot";
    private static final int NOTIF_ID = 2209;
    private static final int CAPTURE_WIDTH = 400;

    private final Object lock = new Object();
    private long h;                                // native engine handle
    private MediaProjection projection;
    private VirtualDisplay vdisplay;
    private ImageReader reader;
    private HandlerThread capThread;
    private Handler capHandler;
    private final Handler main = new Handler(Looper.getMainLooper());
    private Thread tickThread;
    private volatile boolean running;
    private volatile boolean calibrating;

    private WindowManager wm;
    private View panel, bubble, pad, crosshair, calibBar;
    private WindowManager.LayoutParams panelLp, bubbleLp, crossLp;
    private int screenW, screenH;

    // ---------------------------------------------------------------- lifecycle
    @Override public IBinder onBind(Intent intent) { return null; }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent == null || projection != null) return START_NOT_STICKY;
        startForegroundCompat();
        int code = intent.getIntExtra(EXTRA_CODE, 0);
        Intent data = Build.VERSION.SDK_INT >= 33 ? intent.getParcelableExtra(EXTRA_DATA, Intent.class)
                                                   : intent.getParcelableExtra(EXTRA_DATA);
        MediaProjectionManager mpm = (MediaProjectionManager) getSystemService(MEDIA_PROJECTION_SERVICE);
        try {
            projection = mpm.getMediaProjection(code, data);
        } catch (Exception e) {
            projection = null;
        }
        if (projection == null) {
            Toast.makeText(this, "Ekran yakalama izni alınamadı", Toast.LENGTH_LONG).show();
            stopSelf();
            return START_NOT_STICKY;
        }
        projection.registerCallback(new MediaProjection.Callback() {
            @Override public void onStop() { main.post(BotService.this::stopSelf); }
        }, main);

        h = NativeBridge.create();
        NativeBridge.setConfig(h, ConfigStore.rawPrefsConfig(this));
        int lvl = ConfigStore.levelId(this);
        File mf = ConfigStore.macroFile(this, lvl);
        if (mf.exists()) {
            String t = ConfigStore.readFile(mf);
            if (t != null) NativeBridge.loadMacro(h, t);
        }
        wm = (WindowManager) getSystemService(WINDOW_SERVICE);
        updateScreenSize();
        capThread = new HandlerThread("capture", android.os.Process.THREAD_PRIORITY_DISPLAY);
        capThread.start();
        capHandler = new Handler(capThread.getLooper());
        setupCapture();
        running = true;
        tickThread = new Thread(this::tickLoop, "bot-tick");
        tickThread.setPriority(Thread.MAX_PRIORITY);
        tickThread.start();
        instance = this;
        showPanel();
        main.postDelayed(statusUpdater, 200);
        return START_NOT_STICKY;
    }

    @Override
    public void onDestroy() {
        instance = null;
        running = false;
        main.removeCallbacksAndMessages(null);
        BotAccessibilityService acc = BotAccessibilityService.instance;
        if (acc != null) acc.releaseAll();
        removeOverlays();
        if (tickThread != null) try { tickThread.join(500); } catch (InterruptedException ignored) {}
        if (vdisplay != null) vdisplay.release();
        if (reader != null) reader.close();
        if (projection != null) projection.stop();
        if (capThread != null) capThread.quitSafely();
        synchronized (lock) {
            if (h != 0) {
                ConfigStore.storeRaw(this, NativeBridge.getConfig(h));
                NativeBridge.destroy(h);
                h = 0;
            }
        }
        super.onDestroy();
    }

    private void startForegroundCompat() {
        NotificationManager nm = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
        nm.createNotificationChannel(new NotificationChannel(CHANNEL, "GD AutoBot", NotificationManager.IMPORTANCE_LOW));
        PendingIntent pi = PendingIntent.getActivity(this, 0, new Intent(this, MainActivity.class),
                PendingIntent.FLAG_IMMUTABLE);
        Notification n = new Notification.Builder(this, CHANNEL)
                .setSmallIcon(R.drawable.ic_stat_bot)
                .setContentTitle("GD AutoBot aktif")
                .setContentText("Ekran analiz ediliyor · bot kullanımı açık")
                .setContentIntent(pi)
                .setOngoing(true)
                .build();
        if (Build.VERSION.SDK_INT >= 29) startForeground(NOTIF_ID, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION);
        else startForeground(NOTIF_ID, n);
    }

    // ---------------------------------------------------------------- capture
    private void updateScreenSize() {
        if (Build.VERSION.SDK_INT >= 30) {
            Rect b = wm.getCurrentWindowMetrics().getBounds();
            screenW = b.width();
            screenH = b.height();
        } else {
            DisplayMetrics dm = new DisplayMetrics();
            wm.getDefaultDisplay().getRealMetrics(dm);
            screenW = dm.widthPixels;
            screenH = dm.heightPixels;
        }
    }

    private int capW() { return CAPTURE_WIDTH; }
    private int capH() { return Math.max(16, (int) ((long) CAPTURE_WIDTH * screenH / screenW)); }

    private void setupCapture() {
        int dpi = getResources().getDisplayMetrics().densityDpi;
        reader = ImageReader.newInstance(capW(), capH(), PixelFormat.RGBA_8888, 3);
        reader.setOnImageAvailableListener(this::onImage, capHandler);
        vdisplay = projection.createVirtualDisplay("gdautobot", capW(), capH(), dpi,
                DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR, reader.getSurface(), null, capHandler);
    }

    @Override
    public void onConfigurationChanged(Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
        if (vdisplay == null) return;
        updateScreenSize();
        // rotation: resize the virtual display instead of creating a new one
        // (Android 14+ allows only one createVirtualDisplay per projection)
        int dpi = getResources().getDisplayMetrics().densityDpi;
        ImageReader old = reader;
        reader = ImageReader.newInstance(capW(), capH(), PixelFormat.RGBA_8888, 3);
        reader.setOnImageAvailableListener(this::onImage, capHandler);
        vdisplay.resize(capW(), capH(), dpi);
        vdisplay.setSurface(reader.getSurface());
        capHandler.post(old::close);
    }

    private void onImage(ImageReader r) {
        Image img;
        try {
            img = r.acquireLatestImage();
        } catch (IllegalStateException e) {
            return;
        }
        if (img == null) return;
        int[] cmds;
        try {
            Image.Plane p = img.getPlanes()[0];
            ByteBuffer buf = p.getBuffer();
            synchronized (lock) {
                if (h == 0) return;
                cmds = NativeBridge.onFrame(h, buf, img.getWidth(), img.getHeight(), p.getRowStride(),
                        System.nanoTime() / 1000, calibrating);
            }
        } finally {
            img.close();
        }
        exec(cmds);
    }

    // ---------------------------------------------------------------- timing + input
    private void tickLoop() {
        android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_URGENT_DISPLAY);
        while (running) {
            long next;
            synchronized (lock) {
                if (h == 0) break;
                next = NativeBridge.nextDeadlineUs(h);
            }
            long now = System.nanoTime() / 1000;
            if (next < 0 || next - now > 3000) {
                LockSupport.parkNanos(1_000_000);
            } else {
                while (System.nanoTime() / 1000 < next) LockSupport.parkNanos(200_000);
            }
            int[] cmds;
            synchronized (lock) {
                if (h == 0) break;
                cmds = NativeBridge.onTick(h, System.nanoTime() / 1000);
            }
            exec(cmds);
        }
    }

    private void exec(int[] cmds) {
        if (cmds == null || cmds.length == 0) return;
        BotAccessibilityService acc = BotAccessibilityService.instance;
        if (acc == null) return;   // gestures impossible without the accessibility service
        Map<String, String> cfg = null;
        for (int i = 0; i + 1 < cmds.length; i += 2) {
            int type = cmds[i], player = cmds[i + 1];
            switch (type) {
                case NativeBridge.CMD_PRESS:
                    acc.press(player, ConfigStore.tapX(this, player) * screenW, ConfigStore.tapY(this, player) * screenH);
                    break;
                case NativeBridge.CMD_RELEASE:
                    acc.release(player);
                    break;
                case NativeBridge.CMD_PLACE_CP:
                case NativeBridge.CMD_RETRY: {
                    if (cfg == null) cfg = ConfigStore.parse(getConfigText());
                    String kx = type == NativeBridge.CMD_RETRY ? "retryX" : "cpPlaceX";
                    String ky = type == NativeBridge.CMD_RETRY ? "retryY" : "cpPlaceY";
                    float x = parseF(cfg.get(kx), -1), y = parseF(cfg.get(ky), -1);
                    if (x >= 0 && y >= 0) acc.tap(x * screenW, y * screenH, 40);
                    break;
                }
                default: break;
            }
        }
    }

    private static float parseF(String s, float def) {
        try { return s == null ? def : Float.parseFloat(s); } catch (NumberFormatException e) { return def; }
    }

    // ---------------------------------------------------------------- API for MainActivity
    public String getConfigText() {
        synchronized (lock) { return h == 0 ? "" : NativeBridge.getConfig(h); }
    }

    public void setConfigText(String t) {
        synchronized (lock) { if (h != 0) NativeBridge.setConfig(h, t); }
    }

    public String loadMacroText(String t) {
        synchronized (lock) { return h == 0 ? "servis kapalı" : NativeBridge.loadMacro(h, t); }
    }

    public String currentMacroText() {
        synchronized (lock) { return h == 0 ? "" : NativeBridge.currentMacro(h); }
    }

    public String statusText() {
        synchronized (lock) { return h == 0 ? "" : NativeBridge.status(h); }
    }

    public void control(int action) {
        int[] cmds;
        synchronized (lock) {
            if (h == 0) return;
            cmds = NativeBridge.control(h, action, System.nanoTime() / 1000);
        }
        exec(cmds);
        if (action == NativeBridge.C_STOP) {
            BotAccessibilityService acc = BotAccessibilityService.instance;
            if (acc != null) acc.releaseAll();
        }
        refreshPanel();
    }

    private void toggleRecording() {
        String macro = null;
        boolean wasRec;
        synchronized (lock) {
            wasRec = ConfigStore.parse(NativeBridge.status(h)).get("recording").equals("1");
            if (wasRec) macro = NativeBridge.stopRecording(h);
        }
        if (!wasRec) {
            control(NativeBridge.C_REC_START);
            if (pad == null) togglePad();
            toast("Kayıt: kayıt pedine dokunarak oyna. Her ölüm yeni deneme; en iyi deneme saklanır.");
        } else if (macro == null || macro.isEmpty()) {
            toast("Kayıt boş (kayıt pedi kullanıldı mı?)");
        } else {
            int lvl = ConfigStore.levelId(this);
            File f = ConfigStore.macroFile(this, lvl);
            toast(ConfigStore.writeFile(f, macro) ? "Macro kaydedildi: Level " + lvl : "Macro yazılamadı");
        }
        refreshPanel();
    }

    private void toast(String s) { main.post(() -> Toast.makeText(this, s, Toast.LENGTH_SHORT).show()); }

    // ---------------------------------------------------------------- overlays
    private int overlayType() { return WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY; }

    private WindowManager.LayoutParams lp(int w, int hgt, boolean touchable) {
        int flags = WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN;
        if (!touchable) flags |= WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE;
        WindowManager.LayoutParams l = new WindowManager.LayoutParams(w, hgt, overlayType(), flags, PixelFormat.TRANSLUCENT);
        l.gravity = Gravity.TOP | Gravity.START;
        if (Build.VERSION.SDK_INT >= 28)
            l.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        return l;
    }

    private int dp(float v) { return (int) (v * getResources().getDisplayMetrics().density + 0.5f); }

    private void showPanel() {
        panel = LayoutInflater.from(this).inflate(R.layout.overlay_panel, null);
        panelLp = lp(WindowManager.LayoutParams.WRAP_CONTENT, WindowManager.LayoutParams.WRAP_CONTENT, true);
        panelLp.x = dp(8);
        panelLp.y = dp(60);
        wm.addView(panel, panelLp);
        makeDraggable(panel.findViewById(R.id.panelHandle), panel, panelLp);

        click(R.id.btnAuto, () -> {
            control(NativeBridge.C_AUTOPLAY_TOGGLE);
            if (isAutoPlay()) collapse();   // keep the panel out of the analysed screen
        });
        click(R.id.btnSmart, () -> control(NativeBridge.C_SET_SMART));
        click(R.id.btnMacro, () -> control(NativeBridge.C_SET_MACRO));
        click(R.id.btnRec, this::toggleRecording);
        click(R.id.btnSync, () -> control(NativeBridge.C_SYNC));
        click(R.id.btnPause, () -> control(NativeBridge.C_PAUSE_TOGGLE));
        click(R.id.btnStop, () -> control(NativeBridge.C_STOP));
        click(R.id.btnMode, () -> control(NativeBridge.C_GAMEMODE_NEXT));
        click(R.id.btnSpeed, () -> control(NativeBridge.C_SPEED_NEXT));
        click(R.id.btnGravity, () -> control(NativeBridge.C_GRAVITY_TOGGLE));
        click(R.id.btnPad, this::togglePad);
        click(R.id.btnCalib, this::toggleCalibration);
        click(R.id.btnCollapse, this::collapse);
        click(R.id.btnClose, this::stopSelf);
        refreshPanel();
    }

    private void click(int id, Runnable r) {
        View v = panel.findViewById(id);
        if (v != null) v.setOnClickListener(x -> r.run());
    }

    private boolean isAutoPlay() {
        return "1".equals(ConfigStore.parse(statusText()).get("autoPlay"));
    }

    private void collapse() {
        if (bubble != null) return;
        panel.setVisibility(View.GONE);
        TextView b = new TextView(this);
        b.setText("AB");
        b.setTextColor(0xFF1C1430);
        b.setTextSize(11);
        b.setGravity(Gravity.CENTER);
        b.setBackgroundResource(R.drawable.bubble_bg);
        bubble = b;
        bubbleLp = lp(dp(34), dp(34), true);
        bubbleLp.x = dp(4);
        bubbleLp.y = dp(2);
        wm.addView(bubble, bubbleLp);
        makeDraggable(bubble, bubble, bubbleLp);
        bubble.setOnClickListener(v -> expand());
    }

    private void expand() {
        if (bubble != null) { wm.removeView(bubble); bubble = null; }
        panel.setVisibility(View.VISIBLE);
    }

    private void makeDraggable(View handle, View target, WindowManager.LayoutParams l) {
        handle.setOnTouchListener(new View.OnTouchListener() {
            float sx, sy; int ox, oy; boolean moved;
            @Override public boolean onTouch(View v, MotionEvent e) {
                switch (e.getActionMasked()) {
                    case MotionEvent.ACTION_DOWN:
                        sx = e.getRawX(); sy = e.getRawY(); ox = l.x; oy = l.y; moved = false;
                        return true;
                    case MotionEvent.ACTION_MOVE:
                        if (Math.abs(e.getRawX() - sx) + Math.abs(e.getRawY() - sy) > dp(6)) moved = true;
                        l.x = ox + (int) (e.getRawX() - sx);
                        l.y = oy + (int) (e.getRawY() - sy);
                        wm.updateViewLayout(target, l);
                        return true;
                    case MotionEvent.ACTION_UP:
                        if (!moved) v.performClick();
                        return true;
                }
                return false;
            }
        });
    }

    /** Record pad: touches here are recorded AND forwarded to the game at the tap point. */
    private void togglePad() {
        if (pad != null) { wm.removeView(pad); pad = null; refreshPanel(); return; }
        View v = new View(this) {
            final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
            @Override protected void onDraw(Canvas c) {
                p.setColor(isPressed() ? 0x99FACC15 : 0x558B5CF6);
                c.drawRoundRect(0, 0, getWidth(), getHeight(), dp(18), dp(18), p);
                p.setColor(Color.WHITE);
                p.setTextSize(dp(13));
                p.setTextAlign(Paint.Align.CENTER);
                c.drawText("KAYIT PEDİ · dokun = zıpla", getWidth() / 2f, getHeight() / 2f, p);
            }
        };
        v.setOnTouchListener((view, e) -> {
            int a = e.getActionMasked();
            if (a == MotionEvent.ACTION_DOWN || a == MotionEvent.ACTION_UP || a == MotionEvent.ACTION_CANCEL) {
                boolean down = a == MotionEvent.ACTION_DOWN;
                view.setPressed(down);
                view.invalidate();
                userTouch(down);
            }
            return true;
        });
        pad = v;
        WindowManager.LayoutParams l = lp((int) (screenW * 0.30f), (int) (screenH * 0.40f), true);
        l.gravity = Gravity.BOTTOM | Gravity.START;
        wm.addView(pad, l);
        refreshPanel();
    }

    private void userTouch(boolean down) {
        long t = System.nanoTime() / 1000;
        int[] cmds;
        synchronized (lock) {
            if (h == 0) return;
            cmds = NativeBridge.onUserInput(h, t, down, 0);
        }
        exec(cmds);
        BotAccessibilityService acc = BotAccessibilityService.instance;
        if (acc != null) {
            if (down) acc.press(0, ConfigStore.tapX(this, 0) * screenW, ConfigStore.tapY(this, 0) * screenH);
            else acc.release(0);
        }
    }

    // ---------------- calibration (crosshair + target buttons) ----------------
    private void toggleCalibration() {
        if (crosshair != null) {
            wm.removeView(crosshair); crosshair = null;
            wm.removeView(calibBar); calibBar = null;
            calibrating = false;
            return;
        }
        calibrating = true;
        View c = new View(this) {
            final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
            @Override protected void onDraw(Canvas cv) {
                float cx = getWidth() / 2f, cy = getHeight() / 2f, r = dp(14);
                p.setStyle(Paint.Style.STROKE);
                p.setStrokeWidth(dp(2));
                p.setColor(0xFFFACC15);
                cv.drawCircle(cx, cy, r + dp(6), p);
                cv.drawLine(cx - r - dp(14), cy, cx - r, cy, p);
                cv.drawLine(cx + r, cy, cx + r + dp(14), cy, p);
                cv.drawLine(cx, cy - r - dp(14), cx, cy - r, p);
                cv.drawLine(cx, cy + r, cx, cy + r + dp(14), p);
            }
        };
        crosshair = c;
        int size = dp(84);
        crossLp = lp(size, size, true);
        crossLp.x = screenW / 2 - size / 2;
        crossLp.y = screenH / 2 - size / 2;
        wm.addView(crosshair, crossLp);
        makeDraggable(crosshair, crosshair, crossLp);

        LinearLayout bar = new LinearLayout(this);
        bar.setBackgroundResource(R.drawable.panel_bg);
        bar.setPadding(dp(6), dp(4), dp(6), dp(4));
        String[] labels = {"Oyuncu", "Çubuk sol", "Çubuk sağ", "Retry", "Checkpoint", "Dokunma", "Bitti"};
        for (int i = 0; i < labels.length; i++) {
            final int what = i;
            Button b = new Button(this);
            b.setText(labels[i]);
            b.setTextSize(11);
            b.setAllCaps(false);
            b.setOnClickListener(v -> calibrateAt(what));
            bar.addView(b);
        }
        calibBar = bar;
        WindowManager.LayoutParams bl = lp(WindowManager.LayoutParams.WRAP_CONTENT, WindowManager.LayoutParams.WRAP_CONTENT, true);
        bl.gravity = Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL;
        wm.addView(calibBar, bl);
        toast("Artı işaretini sürükle, sonra hedefi seç (Oyuncu = ikonun içi)");
    }

    private void calibrateAt(int what) {
        if (what == 6) { toggleCalibration(); return; }
        int size = crosshair.getWidth();
        float rx = (crossLp.x + size / 2f) / screenW, ry = (crossLp.y + size / 2f) / screenH;
        if (what == 5) {
            ConfigStore.setTap(this, 0, rx, ry);
            toast(String.format("Dokunma noktası: %.2f, %.2f", rx, ry));
            return;
        }
        int target = new int[]{NativeBridge.CAL_PLAYER, NativeBridge.CAL_BAR_LEFT, NativeBridge.CAL_BAR_RIGHT,
                NativeBridge.CAL_RETRY, NativeBridge.CAL_CHECKPOINT}[what];
        String msg;
        synchronized (lock) {
            msg = h == 0 ? "" : NativeBridge.calibrate(h, rx, ry, target);
            if (h != 0) ConfigStore.storeRaw(this, NativeBridge.getConfig(h));
        }
        toast(msg);
    }

    private void removeOverlays() {
        for (View v : new View[]{panel, bubble, pad, crosshair, calibBar}) {
            if (v != null) try { wm.removeView(v); } catch (Exception ignored) {}
        }
        panel = bubble = pad = crosshair = calibBar = null;
    }

    // ---------------- status ----------------
    private final Runnable statusUpdater = new Runnable() {
        @Override public void run() {
            refreshPanel();
            main.postDelayed(this, 250);
        }
    };

    private void refreshPanel() {
        if (panel == null) return;
        main.post(() -> {
            if (panel == null) return;
            Map<String, String> s = ConfigStore.parse(statusText());
            String st = s.containsKey("status") ? s.get("status") : "STOPPED";
            boolean auto = "1".equals(s.get("autoPlay"));
            boolean macro = "MACRO".equals(s.get("botMode"));
            boolean rec = "1".equals(s.get("recording"));
            boolean acc = BotAccessibilityService.isReady();
            ((TextView) panel.findViewById(R.id.txtBadge)).setText(
                    rec ? "● BOT · KAYIT" : auto ? "▶ BOT · AUTO PLAY" : "BOT · " + st);
            Button ba = panel.findViewById(R.id.btnAuto);
            ba.setText(auto ? "AUTO PLAY: ON" : "AUTO PLAY: OFF");
            ba.setBackgroundResource(auto ? R.drawable.btn_yellow : R.drawable.btn_dark);
            ba.setTextColor(auto ? 0xFF1C1430 : 0xFFECE8F8);
            panel.findViewById(R.id.btnSmart).setBackgroundResource(!macro ? R.drawable.btn_purple : R.drawable.btn_dark);
            panel.findViewById(R.id.btnMacro).setBackgroundResource(macro ? R.drawable.btn_purple : R.drawable.btn_dark);
            ((Button) panel.findViewById(R.id.btnRec)).setText(rec ? "■ KAYIT" : "● KAYIT");
            ((Button) panel.findViewById(R.id.btnPause)).setText("PAUSED".equals(st) ? "▶" : "❚❚");
            ((Button) panel.findViewById(R.id.btnMode)).setText(s.containsKey("gameMode") ? s.get("gameMode") : "Cube");
            float sp = parseF(s.get("speed"), 1f);
            ((Button) panel.findViewById(R.id.btnSpeed)).setText((sp == (int) sp ? String.valueOf((int) sp) : String.valueOf(sp)) + "x");
            ((Button) panel.findViewById(R.id.btnPad)).setText(pad != null ? "PED ✓" : "PED");
            String line = String.format("STATUS: %s · %s fps · %%%.0f · den. %s%s%s", st, s.get("fps"),
                    parseF(s.get("progress"), 0) * 100f, s.get("attempts"),
                    "1".equals(s.get("playerFound")) ? "" : " · oyuncu yok",
                    acc ? "" : " · ERİŞİLEBİLİRLİK KAPALI");
            ((TextView) panel.findViewById(R.id.txtStatus)).setText(line);
            if (bubble != null) {
                bubble.setBackgroundResource(rec ? R.drawable.bubble_rec : auto ? R.drawable.bubble_bg : R.drawable.bubble_idle);
            }
        });
    }

    public static boolean running() { return instance != null; }

    public static Intent startIntent(Context c, int code, Intent data) {
        Intent i = new Intent(c, BotService.class);
        i.putExtra(EXTRA_CODE, code);
        i.putExtra(EXTRA_DATA, data);
        return i;
    }
}
