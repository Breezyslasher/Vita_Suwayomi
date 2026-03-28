package org.vitasuwayomi.app;

import android.content.pm.ActivityInfo;
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
        // Keep Android in one fixed landscape orientation. Samsung foldables
        // were relaunching the task through alternate landscape sensor states,
        // which produced letterboxed bounds and SDL buffer-size mismatches.
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
        super.onCreate(savedInstanceState);
        // SDLActivity starts non-fullscreen and only later switches to
        // immersive mode, which creates an initial 2160x1651 surface on this
        // device before resizing it to 2160x1856. Force fullscreen now so the
        // first surface size matches the final window bounds.
        SDLActivity.setWindowStyle(true);
        mSurface.getHolder().setFormat(PixelFormat.RGBA_8888);
        // Keep the SDL render surface on top so Borealis text/widgets are
        // actually visible on devices that otherwise present a blank frame.
        mSurface.setZOrderOnTop(true);

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

        // Android does not recommend using exit(0) directly,
        // but borealis heavily uses static variables,
        // which can cause some problems when reloading the program.

        // In SDL3, we can use SDL_HINT_ANDROID_ALLOW_RECREATE_ACTIVITY to control the behavior

        // In SDL2, Force exit of the app.
        System.exit(0);
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
