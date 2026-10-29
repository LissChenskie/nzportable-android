// SPDX-License-Identifier: GPL-2.0-or-later
package org.nzportable.android;
import android.os.Build;
import android.os.Bundle;
import android.os.PowerManager;
import android.util.Log;
import android.view.View;
import android.app.AlertDialog;
import android.widget.EditText;
import android.text.InputType;
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
    static native void nativeLanConnect(String address);
    static native void nativeLanSetName(String name);
    static native void nativeLanSetPlayerName(String name);

    @Override protected String[] getLibraries() { return new String[]{"SDL2", "SDL2_mixer", "main"}; }
    @Override protected String[] getArguments() {
        // No hard-coded framebuffer dimensions or command-line overrides:
        // archived graphics settings win on every subsequent launch.
        return new String[]{"-basedir", new java.io.File(getFilesDir(),"game").getAbsolutePath()};
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
    // Called by the SDL engine, dialogs run on Android's UI thread.
    public void openLanAddressDialog() {
        runOnUiThread(() -> {
            EditText field = new EditText(this);
            field.setSingleLine(true);
            field.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);
            field.setHint("192.168.1.10:26000");
            new AlertDialog.Builder(this)
                .setTitle("CONNECT TO LAN SERVER")
                .setView(field)
                .setPositiveButton("CONNECT", (dialog, which) ->
                    nativeLanConnect(field.getText().toString().trim()))
                .setNegativeButton("CANCEL", null).show();
        });
    }
    public void openLanServerNameDialog() {
        runOnUiThread(() -> {
            EditText field = new EditText(this);
            field.setSingleLine(true);
            field.setHint("NZPortable LAN");
            new AlertDialog.Builder(this)
                .setTitle("LAN SERVER NAME")
                .setView(field)
                .setPositiveButton("SAVE", (dialog, which) ->
                    nativeLanSetName(field.getText().toString().trim()))
                .setNegativeButton("CANCEL", null).show();
        });
    }
    public void promptLanPlayerName(final String previous) {
        runOnUiThread(() -> {
            EditText field=new EditText(this);
            field.setSingleLine(true);
            field.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PERSON_NAME);
            field.setFilters(new android.text.InputFilter[]{new android.text.InputFilter.LengthFilter(15)});
            field.setText(previous==null?"player":previous);
            field.selectAll();
            new AlertDialog.Builder(this)
                .setTitle("CO-OP PLAYER NICKNAME")
                .setMessage("Up to 15 characters. Visible to other LAN players.")
                .setView(field)
                .setPositiveButton("SAVE", (dialog,which) ->
                    nativeLanSetPlayerName(field.getText().toString().trim()))
                .setNegativeButton("CANCEL",null).show();
        });
    }
    // Compatibility entry points for the extended LAN co-op menu.
    public void promptLanAddress() { openLanAddressDialog(); }
    public void promptLanName() { openLanServerNameDialog(); }
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
