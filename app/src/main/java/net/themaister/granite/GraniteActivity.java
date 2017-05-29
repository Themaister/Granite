package net.themaister.granite;

import android.hardware.SensorManager;
import android.view.Display;
import android.view.SurfaceHolder;
import android.view.View;

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

    public int getDisplayRotation()
    {
        Display display = getWindowManager().getDefaultDisplay();
        if (display == null)
            return 0;

        return display.getRotation();
    }
}