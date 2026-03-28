package org.vitasuwayomi.app;

import android.database.ContentObserver;
import android.graphics.PixelFormat;
import android.os.Bundle;
import android.os.Handler;
import android.provider.Settings;
import android.view.Surface;
import android.view.SurfaceView;

import org.libsdl.app.BorealisHandler;
import org.libsdl.app.PlatformUtils;
import org.libsdl.app.SDLActivity;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

public class VitaSuwayomiActivity extends SDLActivity
{
    protected static SurfaceView mpvSurface;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        mSurface.getHolder().setFormat(PixelFormat.RGBA_8888);
        mSurface.setZOrderOnTop(true);
        mpvSurface = new SurfaceView(this);
        mLayout.addView(mpvSurface, 0);

        PlatformUtils.borealisHandler = new BorealisHandler();
        _setAppScreenBrightness(_getSystemScreenBrightness());
        getContentResolver().registerContentObserver(
                Settings.System.getUriFor(Settings.System.SCREEN_BRIGHTNESS),
                true,
                brightnessObserver);
    }

    private void _setAppScreenBrightness(float value) {
        PlatformUtils.setAppScreenBrightness(this, value);
    }

    private float _getSystemScreenBrightness() {
        return PlatformUtils.getSystemScreenBrightness(this);
    }

    private final ContentObserver brightnessObserver = new ContentObserver(new Handler()) {
        @Override
        public void onChange(boolean selfChange) {
            _setAppScreenBrightness(_getSystemScreenBrightness());
        }
    };

    public static Surface getMpvSurface() {
        if (mpvSurface == null && mSingleton instanceof VitaSuwayomiActivity) {
            VitaSuwayomiActivity activity = (VitaSuwayomiActivity) mSingleton;
            CountDownLatch latch = new CountDownLatch(1);

            activity.runOnUiThread(() -> {
                if (mpvSurface == null) {
                    mpvSurface = new SurfaceView(activity);
                    activity.mLayout.addView(mpvSurface, 0);
                }
                latch.countDown();
            });

            try {
                latch.await(2, TimeUnit.SECONDS);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
        }

        if (mpvSurface == null) {
            return null;
        }
        return mpvSurface.getHolder().getSurface();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();

        getContentResolver().unregisterContentObserver(brightnessObserver);

        // Do not force-exit the process here. Android may destroy/recreate the
        // activity during normal lifecycle events, and killing the process can
        // interrupt Borealis navigation and look like a frozen transition.
    }

    @Override
    protected String[] getLibraries() {
        // Load SDL2 and borealis demo app
        return new String[] {
                "curl",
                "SDL2",
                "VitaSuwayomi"
        };
    }

}
