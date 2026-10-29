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
    @Override protected String[] getLibraries() { return new String[]{"SDL2", "SDL2_mixer", "main"}; }
    @Override protected String[] getArguments() {
        return new String[]{"-basedir", new java.io.File(getFilesDir(),"game").getAbsolutePath(),
            "+cl_maxfps", "60", "+vid_width", "1280", "+vid_height", "720"};
    }
    @Override protected void onCreate(Bundle state) {
        power=(PowerManager)getSystemService(POWER_SERVICE);
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        // Keep controls outside cutouts rather than pretending all A51 variants share insets.
        if(Build.VERSION.SDK_INT>=28){WindowManager.LayoutParams p=getWindow().getAttributes();
            p.layoutInDisplayCutoutMode=WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_NEVER;getWindow().setAttributes(p);}
        Log.i("NZPortable","Device="+Build.MODEL+" hardware="+Build.HARDWARE+" ABI="+android.os.Process.is64Bit());
        immersive();
    }
    private void immersive(){getWindow().getDecorView().setSystemUiVisibility(
        View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY|View.SYSTEM_UI_FLAG_FULLSCREEN|View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
        |View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN|View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION|View.SYSTEM_UI_FLAG_LAYOUT_STABLE);}
    @Override public void onWindowFocusChanged(boolean focused){super.onWindowFocusChanged(focused);if(focused)immersive();}
    // Called once/second by the native frame governor. No per-frame JNI or Java overlay.
    public int getThermalStatus(){return Build.VERSION.SDK_INT>=29&&power!=null?power.getCurrentThermalStatus():0;}
    @Override protected void onDestroy(){
        // Vril's global allocator/VM state is process-lifetime state. The launcher
        // remains in the main app process; each completed game gets a fresh VM.
        boolean finished=isFinishing()&&!isChangingConfigurations();
        super.onDestroy();
        if(finished)android.os.Process.killProcess(android.os.Process.myPid());
    }
}
