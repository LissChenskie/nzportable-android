// SPDX-License-Identifier: GPL-2.0-or-later
package org.nzportable.android;
import android.content.Context;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.util.Log;

/**
 * Keeps Wi-Fi in a state that is usable for real-time UDP co-op between phones
 * (one hosting a hotspot, the others joining it):
 *  - low-latency Wi-Fi lock: no 100-300 ms power-save wakeup jitter;
 *  - multicast lock: some devices drop broadcast frames (LAN discovery) otherwise;
 *  - publishes the Wi-Fi Network handle so native client sockets can be bound
 *    to it. Without this, a hotspot without internet makes Android route app
 *    traffic through mobile data and connecting to 192.168.x.x fails.
 * Every call is best-effort: a missing permission or OEM quirk never crashes.
 */
final class LanNetwork {
    private static final String TAG = "NZPortable";
    // WifiManager.WIFI_MODE_FULL_LOW_LATENCY (API 29) / WIFI_MODE_FULL_HIGH_PERF.
    private static final int MODE_LOW_LATENCY = 4, MODE_HIGH_PERF = 3;
    static native void nativeNetworkChanged(long handle);

    private final Context context;
    private WifiManager.MulticastLock multicast;
    private WifiManager.WifiLock wifiLock;
    private ConnectivityManager connectivity;
    private ConnectivityManager.NetworkCallback callback;
    private Network current;

    LanNetwork(Context context) { this.context = context.getApplicationContext(); }

    synchronized void start() {
        try {
            WifiManager wifi = (WifiManager) context.getSystemService(Context.WIFI_SERVICE);
            if (wifi != null) {
                if (multicast == null) {
                    multicast = wifi.createMulticastLock("nzp-lan-discovery");
                    multicast.setReferenceCounted(false);
                }
                if (!multicast.isHeld()) multicast.acquire();
                if (wifiLock == null) {
                    wifiLock = wifi.createWifiLock(Build.VERSION.SDK_INT >= 29 ? MODE_LOW_LATENCY : MODE_HIGH_PERF,
                        "nzp-lan-game");
                    wifiLock.setReferenceCounted(false);
                }
                if (!wifiLock.isHeld()) wifiLock.acquire();
            }
        } catch (RuntimeException e) { Log.w(TAG, "Wi-Fi locks unavailable: " + e); }
        try {
            if (connectivity == null)
                connectivity = (ConnectivityManager) context.getSystemService(Context.CONNECTIVITY_SERVICE);
            if (connectivity != null && callback == null) {
                // Deliberately no NET_CAPABILITY_VALIDATED: a hotspot without internet must match.
                NetworkRequest request = new NetworkRequest.Builder()
                    .addTransportType(NetworkCapabilities.TRANSPORT_WIFI).build();
                callback = new ConnectivityManager.NetworkCallback() {
                    @Override public void onAvailable(Network network) { publish(network); }
                    @Override public void onLost(Network network) {
                        synchronized (LanNetwork.this) {
                            if (network.equals(current)) { current = null; push(0L); }
                        }
                    }
                };
                connectivity.registerNetworkCallback(request, callback);
            }
        } catch (RuntimeException e) { Log.w(TAG, "Wi-Fi network tracking unavailable: " + e); callback = null; }
    }

    synchronized void stop() {
        try { if (callback != null && connectivity != null) connectivity.unregisterNetworkCallback(callback); }
        catch (RuntimeException ignored) { }
        callback = null; current = null; push(0L);
        try { if (multicast != null && multicast.isHeld()) multicast.release(); } catch (RuntimeException ignored) { }
        try { if (wifiLock != null && wifiLock.isHeld()) wifiLock.release(); } catch (RuntimeException ignored) { }
    }

    private synchronized void publish(Network network) {
        current = network;
        push(network.getNetworkHandle());
    }

    private static void push(long handle) {
        try { nativeNetworkChanged(handle); }
        catch (UnsatisfiedLinkError e) { Log.w(TAG, "Native library not ready: " + e.getMessage()); }
    }
}
