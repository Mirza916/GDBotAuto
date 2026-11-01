package com.mirza.gdautobot;

import android.accessibilityservice.AccessibilityService;
import android.accessibilityservice.GestureDescription;
import android.graphics.Path;
import android.os.Handler;
import android.os.HandlerThread;
import android.view.accessibility.AccessibilityEvent;

/**
 * Performs touches with the public, user-enabled Accessibility gesture API
 * (dispatchGesture). No root, no input injection outside of what Android allows.
 *
 * Holds of unknown length use continued strokes (API 26+): a stroke is dispatched
 * with willContinue=true and extended in short segments until release.
 * Android limitation: a new gesture cancels the one in progress, so two
 * simultaneous holds (2-player) are not possible; the newest wins.
 */
public class BotAccessibilityService extends AccessibilityService {
    public static volatile BotAccessibilityService instance;

    private static final long SEG_MS = 80;       // length of each hold segment
    private static final long RENEW_MS = 50;     // dispatch next segment before the previous ends

    private HandlerThread thread;
    private Handler handler;
    private final GestureDescription.StrokeDescription[] stroke = new GestureDescription.StrokeDescription[2];
    private final boolean[] down = new boolean[2];
    private final float[] px = new float[2], py = new float[2];
    private final Runnable[] renew = new Runnable[2];
    private volatile int dispatched, cancelled;

    @Override
    protected void onServiceConnected() {
        thread = new HandlerThread("gestures", android.os.Process.THREAD_PRIORITY_URGENT_DISPLAY);
        thread.start();
        handler = new Handler(thread.getLooper());
        for (int i = 0; i < 2; i++) {
            final int p = i;
            renew[i] = () -> continueHold(p);
        }
        instance = this;
    }

    @Override public void onAccessibilityEvent(AccessibilityEvent event) {}
    @Override public void onInterrupt() {}

    @Override
    public void onDestroy() {
        instance = null;
        if (thread != null) thread.quitSafely();
        super.onDestroy();
    }

    public static boolean isReady() { return instance != null; }
    public int dispatchedCount() { return dispatched; }
    public int cancelledCount() { return cancelled; }

    private static Path point(float x, float y) {
        Path p = new Path();
        p.moveTo(x, y);
        p.lineTo(x, y);   // zero-length segment: a stationary finger
        return p;
    }

    private final GestureResultCallback callback = new GestureResultCallback() {
        @Override public void onCancelled(GestureDescription g) { cancelled++; }
    };

    private void dispatch(GestureDescription.StrokeDescription s) {
        GestureDescription g = new GestureDescription.Builder().addStroke(s).build();
        if (dispatchGesture(g, callback, handler)) dispatched++;
    }

    /** Finger down at absolute screen pixel (x, y) and keep it down until release(). */
    public void press(int player, float x, float y) {
        final int p = player & 1;
        handler.post(() -> {
            if (down[p]) return;
            px[p] = x; py[p] = y;
            stroke[p] = new GestureDescription.StrokeDescription(point(x, y), 0, SEG_MS, true);
            dispatch(stroke[p]);
            down[p] = true;
            handler.postDelayed(renew[p], RENEW_MS);
        });
    }

    private void continueHold(int p) {
        if (!down[p] || stroke[p] == null) return;
        try {
            stroke[p] = stroke[p].continueStroke(point(px[p], py[p]), 0, SEG_MS, true);
            dispatch(stroke[p]);
        } catch (IllegalStateException e) {
            // previous stroke already ended (e.g. cancelled): start a fresh one
            stroke[p] = new GestureDescription.StrokeDescription(point(px[p], py[p]), 0, SEG_MS, true);
            dispatch(stroke[p]);
        }
        handler.postDelayed(renew[p], RENEW_MS);
    }

    public void release(int player) {
        final int p = player & 1;
        handler.post(() -> {
            if (!down[p]) return;
            handler.removeCallbacks(renew[p]);
            down[p] = false;
            try {
                GestureDescription.StrokeDescription end = stroke[p].continueStroke(point(px[p], py[p]), 0, 1, false);
                dispatch(end);
            } catch (IllegalStateException ignored) {
                // stroke already finished
            }
            stroke[p] = null;
        });
    }

    /** One complete tap (used for checkpoint / retry buttons). */
    public void tap(float x, float y, long durMs) {
        handler.post(() -> dispatch(new GestureDescription.StrokeDescription(point(x, y), 0, Math.max(1, durMs), false)));
    }

    public void releaseAll() {
        release(0);
        release(1);
    }
}
