package net.themaister.granite;

import android.app.ActionBar;
import android.os.Bundle;
import android.view.Display;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;

public class GraniteActivity extends android.app.NativeActivity
{
    private final static String TAG = "Granite";
    public void finishFromThread()
    {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                finish();
            }
        });
    }

    private void setImmersiveMode()
    {
        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE |
                        View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION |
                        View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN |
                        View.SYSTEM_UI_FLAG_HIDE_NAVIGATION |
                        View.SYSTEM_UI_FLAG_FULLSCREEN |
                        View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
        );
    }

    @Override
    protected void onCreate(Bundle savedState)
    {
        super.onCreate(savedState);
        setImmersiveMode();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus)
    {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus)
            setImmersiveMode();
    }

    public int getDisplayRotation()
    {
        Display display = getWindowManager().getDefaultDisplay();
        if (display == null)
            return 0;

        return display.getRotation();
    }
}