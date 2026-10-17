package com.thor8126.sevenkaa;

import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.HapticFeedbackConstants;
import android.view.KeyEvent;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.RelativeLayout;
import android.widget.TextView;

import org.libsdl.app.SDLActivity;

/**
 * Seven Kingdoms: Ancient Adversaries on Android.
 *
 * The game itself runs in libmain.so (the unchanged game sources plus the
 * Android glue in app/jni/src). This activity only adds a few touch buttons
 * in the black bars beside the 4:3 game screen:
 *
 *   left:  [Right click] toggle - while on, every tap is a right click
 *          [Shift]       toggle - held Shift, e.g. to add units to a selection
 *          [Pause]       pause / resume (Space)
 *   right: [Ctrl+A]      select the whole army
 *          [< Prev]      previous unit / building / village of the same kind
 *                        of your own (Left arrow key); long press: of any
 *                        nation (Up arrow key)
 *          [Next >]      the next one (Right arrow key); long press: of any
 *                        nation (Down arrow key)
 *          [Esc]         same as the Back button
 */
public class SevenKaaActivity extends SDLActivity {

    private static native void nativeSetRightClickMode(boolean on);

    private boolean rightClickMode;
    private boolean shiftHeld;

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (mLayout == null || mBrokenLibraries) {
            return;
        }
        addTouchButtons();
    }

    private int dp(float v) {
        return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v,
                getResources().getDisplayMetrics());
    }

    private TextView makeButton(String label) {
        TextView b = new TextView(this);
        b.setText(label);
        b.setTextColor(Color.WHITE);
        b.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        b.setGravity(Gravity.CENTER);
        b.setFocusable(false);
        b.setFocusableInTouchMode(false);
        b.setPadding(dp(6), dp(4), dp(6), dp(4));
        setButtonLit(b, false);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(dp(72), dp(52));
        lp.setMargins(dp(4), dp(6), dp(4), dp(6));
        b.setLayoutParams(lp);
        return b;
    }

    /** Press and release a key in the game. */
    private static void tapKey(int keyCode) {
        SDLActivity.onNativeKeyDown(keyCode);
        SDLActivity.onNativeKeyUp(keyCode);
    }

    private TextView makeKeyButton(String label, final int keyCode, final int longPressKeyCode) {
        TextView b = makeButton(label);
        b.setOnClickListener(v -> {
            tapKey(keyCode);
            v.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
        });
        if (longPressKeyCode != 0) {
            b.setOnLongClickListener(v -> {
                tapKey(longPressKeyCode);
                v.performHapticFeedback(HapticFeedbackConstants.LONG_PRESS);
                return true;
            });
        }
        return b;
    }

    private void setButtonLit(TextView b, boolean lit) {
        GradientDrawable bg = new GradientDrawable();
        bg.setCornerRadius(dp(8));
        bg.setColor(lit ? 0xCCB8860B : 0x66333333);
        bg.setStroke(dp(1), lit ? 0xFFFFD700 : 0x99AAAAAA);
        b.setBackground(bg);
    }

    private LinearLayout makeColumn(int alignRule) {
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setGravity(Gravity.CENTER);
        RelativeLayout.LayoutParams lp = new RelativeLayout.LayoutParams(
                RelativeLayout.LayoutParams.WRAP_CONTENT,
                RelativeLayout.LayoutParams.MATCH_PARENT);
        lp.addRule(alignRule);
        col.setLayoutParams(lp);
        return col;
    }

    private void addTouchButtons() {
        LinearLayout left = makeColumn(RelativeLayout.ALIGN_PARENT_LEFT);
        LinearLayout right = makeColumn(RelativeLayout.ALIGN_PARENT_RIGHT);

        final TextView rightClick = makeButton("Right\nclick");
        rightClick.setOnClickListener(v -> {
            rightClickMode = !rightClickMode;
            setButtonLit(rightClick, rightClickMode);
            nativeSetRightClickMode(rightClickMode);
            v.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
        });
        left.addView(rightClick);

        final TextView shift = makeButton("Shift");
        shift.setOnClickListener(v -> {
            shiftHeld = !shiftHeld;
            setButtonLit(shift, shiftHeld);
            if (shiftHeld) {
                SDLActivity.onNativeKeyDown(KeyEvent.KEYCODE_SHIFT_LEFT);
            } else {
                SDLActivity.onNativeKeyUp(KeyEvent.KEYCODE_SHIFT_LEFT);
            }
            v.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
        });
        left.addView(shift);

        left.addView(makeKeyButton("Pause", KeyEvent.KEYCODE_SPACE, 0));

        TextView army = makeButton("Army\nCtrl+A");
        army.setOnClickListener(v -> {
            SDLActivity.onNativeKeyDown(KeyEvent.KEYCODE_CTRL_LEFT);
            SDLActivity.onNativeKeyDown(KeyEvent.KEYCODE_A);
            SDLActivity.onNativeKeyUp(KeyEvent.KEYCODE_A);
            SDLActivity.onNativeKeyUp(KeyEvent.KEYCODE_CTRL_LEFT);
            v.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
        });
        right.addView(army);

        right.addView(makeKeyButton("◀ Prev", KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_DPAD_UP));
        right.addView(makeKeyButton("Next ▶", KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_DPAD_DOWN));

        TextView esc = makeButton("Esc");
        esc.setOnClickListener(v -> {
            SDLActivity.onNativeKeyDown(KeyEvent.KEYCODE_ESCAPE);
            SDLActivity.onNativeKeyUp(KeyEvent.KEYCODE_ESCAPE);
            v.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
        });
        right.addView(esc);

        mLayout.addView(left);
        mLayout.addView(right);
    }

    /** Called from native code (android_touch.cpp) when a long press becomes a right click. */
    public static void hapticTick() {
        final View surface = mSurface;
        if (surface != null) {
            surface.post(() -> surface.performHapticFeedback(HapticFeedbackConstants.LONG_PRESS));
        }
    }
}
