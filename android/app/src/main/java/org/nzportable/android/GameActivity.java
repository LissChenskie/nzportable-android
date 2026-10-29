// SPDX-License-Identifier: GPL-2.0-or-later
package org.nzportable.android;
import android.os.Build;
import android.os.Bundle;
import android.os.PowerManager;
import android.util.Log;
import android.view.View;
import android.view.WindowManager;
import org.libsdl.app.SDLActivity;

public final class GameActivity extends SDLActivity {
    private PowerManager power;
    private RetroTouchBridge touch;
    // Callbacks only enqueue SDL user-events. Engine data is never mutated on the UI thread.
    static native void nativeTouchAction(String action, boolean pressed);
    static native void nativeTouchMove(float x, float y);
    static native void nativeTouchLook(float dx, float dy);
    static native void nativeTouchNeutral();

    @Override protected String[] getLibraries() { return new String[]{"SDL2", "SDL2_mixer", "main"}; }
    @Override protected String[] getArguments() {
        // 960x540 FBO ceiling produces ~960x432 on a 20:9 A51. Native vid_width/height
        // settings remain adjustable after benchmarking.
        return new String[]{"-basedir", new java.io.File(getFilesDir(),"game").getAbsolutePath(),
            "+cl_maxfps", "60", "+vid_width", "960", "+vid_height", "540"};
    }
    @Override protected void onCreate(Bundle state) {
        power=(PowerManager)getSystemService(POWER_SERVICE);
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if(Build.VERSION.SDK_INT>=28){WindowManager.LayoutParams p=getWindow().getAttributes();
            p.layoutInDisplayCutoutMode=WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_NEVER;getWindow().setAttributes(p);}
        touch = new RetroTouchBridge(this);
        touch.setPlaying(false);
        Log.i("NZPortable","Device="+Build.MODEL+" hardware="+Build.HARDWARE+" ABI="+android.os.Process.is64Bit());
        immersive();
    }
    // Called from the SDL render thread on state changes, not on every frame.
    public void setTouchControlsVisible(final boolean playing) {
        runOnUiThread(new Runnable() {
            @Override public void run() { if(touch!=null) touch.setPlaying(playing); }
        });
    }
    private void immersive(){getWindow().getDecorView().setSystemUiVisibility(
        View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY|View.SYSTEM_UI_FLAG_FULLSCREEN|View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
        |View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN|View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION|View.SYSTEM_UI_FLAG_LAYOUT_STABLE);}
    @Override public void onWindowFocusChanged(boolean focused){super.onWindowFocusChanged(focused);if(focused)immersive();}
    public int getThermalStatus(){return Build.VERSION.SDK_INT>=29&&power!=null?power.getCurrentThermalStatus():0;}
    @Override protected void onPause(){if(touch!=null)touch.reset();super.onPause();}
    @Override protected void onDestroy(){
        boolean finished=isFinishing()&&!isChangingConfigurations();
        if(touch!=null){touch.reset();touch=null;}
        super.onDestroy();
        if(finished)android.os.Process.killProcess(android.os.Process.myPid());
    }
}
