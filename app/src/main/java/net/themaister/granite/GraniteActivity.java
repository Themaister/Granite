package net.themaister.granite;

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
}