package org.libsdl.app;

import android.app.Activity;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkInfo;
import android.net.Uri;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import android.os.BatteryManager;
import android.os.Build;
import android.os.Message;
import android.provider.Settings;
import android.util.Log;
import android.view.Window;
import android.view.WindowManager;

public class PlatformUtils {
    public static boolean isBatterySupported() {
        Context context = SDLActivity.getContext();
        Intent batteryIntent = context.registerReceiver(null, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
        return batteryIntent != null;
    }

    public static int getBatteryLevel() {
        Context context = SDLActivity.getContext();

        Intent batteryIntent = context.registerReceiver(null, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
        if (batteryIntent == null) {
            return 0;
        }
        int level = batteryIntent.getIntExtra(BatteryManager.EXTRA_LEVEL, -1);
        int scale = batteryIntent.getIntExtra(BatteryManager.EXTRA_SCALE, -1);

        if (level >= 0 && scale > 0) {
            return (level * 100) / scale;
        }

        return 0;
    }

    public static boolean isBatteryCharging() {
        Context context = SDLActivity.getContext();

        IntentFilter filter = new IntentFilter(Intent.ACTION_BATTERY_CHANGED);
        Intent batteryStatus = context.registerReceiver(null, filter);

        int status = batteryStatus.getIntExtra(BatteryManager.EXTRA_STATUS, -1);
        return status == BatteryManager.BATTERY_STATUS_CHARGING ||
                status == BatteryManager.BATTERY_STATUS_FULL;
    }

    public static boolean isEthernetConnected() {
        Context context = SDLActivity.getContext();

        ConnectivityManager connectivityManager = (ConnectivityManager) context.getSystemService(Context.CONNECTIVITY_SERVICE);
        Network[] networks = connectivityManager.getAllNetworks();
        for (Network network : networks) {
            NetworkCapabilities capabilities = connectivityManager.getNetworkCapabilities(network);
            if (capabilities != null && capabilities.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET)) {
                return true;
            }
        }
        return false;
    }

    public static boolean isWifiSupported() {
        Context context = SDLActivity.getContext();

        WifiManager wifiManager = (WifiManager) context.getApplicationContext().getSystemService(Context.WIFI_SERVICE);
        return wifiManager != null && wifiManager.isWifiEnabled();
    }

    public static boolean isWifiConnected() {
        Context context = SDLActivity.getContext();

        ConnectivityManager connectivityManager = (ConnectivityManager) context.getSystemService(Context.CONNECTIVITY_SERVICE);
        NetworkInfo wifiInfo = connectivityManager.getNetworkInfo(ConnectivityManager.TYPE_WIFI);
        return wifiInfo != null && wifiInfo.isConnected();
    }

    public static int getWifiSignalStrength() {
        Context context = SDLActivity.getContext();

        WifiManager wifiManager = (WifiManager) context.getApplicationContext().getSystemService(Context.WIFI_SERVICE);
        WifiInfo wifiInfo = wifiManager.getConnectionInfo();
        return wifiInfo.getRssi();
    }

    public static void openBrowser(String url) {
        Context context = SDLActivity.getContext();

        Uri webpage = Uri.parse(url);
        Intent intent = new Intent(Intent.ACTION_VIEW, webpage);
        if (intent.resolveActivity(context.getPackageManager()) != null) {
            context.startActivity(intent);
        }
    }

    public static float getSystemScreenBrightness(Context context) {
        ContentResolver contentResolver = context.getContentResolver();
        return Settings.System.getInt(contentResolver,
                Settings.System.SCREEN_BRIGHTNESS, 125) * 1.0f / 255.0f;
    }

    public static BorealisHandler borealisHandler = null;

    public static void setAppScreenBrightness(Activity activity, float value) {
        Message message = Message.obtain();
        message.obj = activity;
        message.arg1 = (int)(value * 255);
        message.what = 0;
        if(borealisHandler != null) borealisHandler.sendMessage(message);
    }

    public static float getAppScreenBrightness(Activity activity) {
        Window window = activity.getWindow();
        WindowManager.LayoutParams lp = window.getAttributes();
        if (lp.screenBrightness < 0) return getSystemScreenBrightness(activity);
        return lp.screenBrightness;
    }

    public static String getAndroidId() {
        Context context = SDLActivity.getContext();
        return Settings.Secure.getString(context.getContentResolver(), Settings.Secure.ANDROID_ID);
    }

    /**
     * Hand a downloaded APK to the system package installer (in-app update).
     *
     * Returns true when the installer was launched, false when the user still
     * has to act (they were sent to the "install unknown apps" screen) or the
     * hand-off failed — the caller shows an explanation in that case.
     *
     * Installing from outside a store on API 26+ needs BOTH
     * REQUEST_INSTALL_PACKAGES in the manifest AND a per-app user grant. Older
     * Android auto-prompted for that grant when the install intent launched;
     * newer Android — Android TV especially — does NOT: without the grant the
     * installer just shows "Staging app… (Unknown)" and silently disappears. So
     * check canRequestPackageInstalls() first and route the user to enable it.
     */
    public static boolean installApk(String path) {
        Context context = SDLActivity.getContext();
        try {
            if (path == null || !new java.io.File(path).isFile()) {
                Log.e("VitaSuwayomi", "installApk: missing APK: " + path);
                return false;
            }

            // Android 8+ (API 26): installing an APK from outside the store
            // needs the per-app "install unknown apps" grant. Declaring
            // REQUEST_INSTALL_PACKAGES in the manifest is necessary but NOT
            // sufficient — the user must enable this app as a source. On phones
            // the install intent often auto-prompts; on Android TV it does not,
            // so without routing the user there they are never asked and the
            // installer just stages and vanishes. Send them to enable it, then
            // they press Update again (canRequestPackageInstalls() is true on
            // the retry and the install proceeds).
            if (Build.VERSION.SDK_INT >= 26
                    && !context.getPackageManager().canRequestPackageInstalls()) {
                PackageManager pm = context.getPackageManager();
                Intent grant = new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                        Uri.parse("package:" + context.getPackageName()));
                grant.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                // Some Android TV builds don't expose the per-app source screen;
                // fall back to the global unknown-sources, then security, screen.
                if (grant.resolveActivity(pm) == null) {
                    grant = new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES)
                            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                }
                if (grant.resolveActivity(pm) == null) {
                    grant = new Intent(Settings.ACTION_SECURITY_SETTINGS)
                            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                }
                try {
                    context.startActivity(grant);
                } catch (Exception e) {
                    Log.e("VitaSuwayomi", "cannot open install-permission settings", e);
                }
                return false;   // user enables it, then retries the update
            }

            Intent intent = new Intent(Intent.ACTION_VIEW);
            Uri uri;
            if (Build.VERSION.SDK_INT >= 24) {
                // file:// throws FileUriExposedException on API 24+; stream the
                // APK through our own provider instead (ApkProvider).
                uri = Uri.parse("content://" + context.getPackageName() + ".apkprovider/update.apk");
                intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            } else {
                uri = Uri.fromFile(new java.io.File(path));
            }
            intent.setDataAndType(uri, "application/vnd.android.package-archive");
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            context.startActivity(intent);

            // Deliberately NO self-kill here. The installer reads the APK from
            // the in-process provider above while staging; killing ourselves
            // would tear it down and abort the install. Android force-stops this
            // package on its own once the update commits.
            return true;
        } catch (Exception e) {
            Log.e("VitaSuwayomi", "installApk failed", e);
            return false;
        }
    }
}