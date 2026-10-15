package org.openjkdf2.app;

import android.app.Activity;
import android.content.Context;
import android.os.Build;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.provider.Settings;
import android.view.HapticFeedbackConstants;
import android.view.View;

/**
    Added: the touch controls' haptic ticks (src/Platform/Touch/touchUi_android.c
    calls perform from the game's thread). A tick as the thumb crosses a
    wheel's slice or starts running; a tap for a hold that went off.

    It is the screen's own haptic feedback, so it follows the system's
    "Touch feedback" setting and needs no permission. Where that does nothing
    (some tablets) and the setting is on, a short predefined vibration instead.
    A device without either just stays still.
*/
public class TouchHaptics {
    private static final int KIND_TICK = 1;
    private static final int KIND_TAP = 2;

    public static void perform(final Activity activity, final int kinds) {
        if (activity == null) {
            return;
        }
        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                try {
                    performNow(activity, kinds);
                } catch (Exception e) {
                    // (haptics are optional)
                }
            }
        });
    }

    private static void performNow(Activity activity, int kinds) {
        final boolean bTap = (kinds & KIND_TAP) != 0;
        View view = activity.getWindow() != null ? activity.getWindow().getDecorView() : null;
        int constant = bTap ? HapticFeedbackConstants.CONTEXT_CLICK : HapticFeedbackConstants.CLOCK_TICK;
        if (view != null && view.performHapticFeedback(constant)) {
            return;
        }
        // Not done: only if the system's touch feedback is on, as a vibration
        int bEnabled = Settings.System.getInt(activity.getContentResolver(), Settings.System.HAPTIC_FEEDBACK_ENABLED, 1);
        if (bEnabled == 0) {
            return;
        }
        Vibrator vibrator = (Vibrator) activity.getSystemService(Context.VIBRATOR_SERVICE);
        if (vibrator == null || !vibrator.hasVibrator()) {
            return;
        }
        if (Build.VERSION.SDK_INT >= 29) {
            vibrator.vibrate(VibrationEffect.createPredefined(bTap ? VibrationEffect.EFFECT_CLICK : VibrationEffect.EFFECT_TICK));
        } else if (Build.VERSION.SDK_INT >= 26) {
            vibrator.vibrate(VibrationEffect.createOneShot(bTap ? 15 : 8, VibrationEffect.DEFAULT_AMPLITUDE));
        }
    }
}
