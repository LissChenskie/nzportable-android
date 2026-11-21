// SPDX-License-Identifier: GPL-2.0-or-later
package org.nzportable.android;
import android.content.Context;
import android.net.ConnectivityManager;
import android.net.LinkAddress;
import android.net.LinkProperties;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.util.Log;

import java.net.Inet4Address;
import java.util.LinkedHashSet;

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
    private static final String TAG = "NZP_LAN";
    // WifiManager.WIFI_MODE_FULL_LOW_LATENCY (API 29) / WIFI_MODE_FULL_HIGH_PERF.
    private static final int MODE_LOW_LATENCY = 4, MODE_HIGH_PERF = 3;
    static native void nativeNetworkChanged(long handle);

    private final Context context;
    private WifiManager.MulticastLock multicast;
    private WifiManager.WifiLock wifiLock;
    private ConnectivityManager connectivity;
    private ConnectivityManager.NetworkCallback callback;
    private Network current;
    /** Every Wi-Fi client network currently reported, oldest first. */
    private final LinkedHashSet<Network> wifi = new LinkedHashSet<>();

    LanNetwork(Context context) { this.context = context.getApplicationContext(); }

    synchronized void start() {
        Log.i(TAG, "LanNetwork.start()");
        try {
            WifiManager wifiMgr = (WifiManager) context.getSystemService(Context.WIFI_SERVICE);
            if (wifiMgr != null) {
                if (multicast == null) {
                    multicast = wifiMgr.createMulticastLock("nzp-lan-discovery");
                    multicast.setReferenceCounted(false);
                }
                if (!multicast.isHeld()) multicast.acquire();
                if (wifiLock == null) {
                    wifiLock = wifiMgr.createWifiLock(Build.VERSION.SDK_INT >= 29 ? MODE_LOW_LATENCY : MODE_HIGH_PERF,
                        "nzp-lan-game");
                    wifiLock.setReferenceCounted(false);
                }
                if (!wifiLock.isHeld()) wifiLock.acquire();
                Log.i(TAG, "Wi-Fi locks held: multicast=" + multicast.isHeld() + " game=" + wifiLock.isHeld());
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
                    @Override public void onAvailable(Network network) { add(network, "onAvailable"); }
                    @Override public void onLinkPropertiesChanged(Network network, LinkProperties props) {
                        add(network, "onLinkPropertiesChanged");
                    }
                    @Override public void onLost(Network network) { remove(network); }
                };
                connectivity.registerNetworkCallback(request, callback);
                // onAvailable is asynchronous: without this the first sockets of a session can be
                // created while the handle is still 0 and then use mobile data.
                syncFromSystem();
            }
        } catch (RuntimeException e) { Log.w(TAG, "Wi-Fi network tracking unavailable: " + e); callback = null; }
    }

    /** The callback is released on pause; the last handle is kept (native re-validates it on resume). */
    synchronized void stop() {
        Log.i(TAG, "LanNetwork.stop()");
        try { if (callback != null && connectivity != null) connectivity.unregisterNetworkCallback(callback); }
        catch (RuntimeException ignored) { }
        callback = null;
        wifi.clear();
        try { if (multicast != null && multicast.isHeld()) multicast.release(); } catch (RuntimeException ignored) { }
        try { if (wifiLock != null && wifiLock.isHeld()) wifiLock.release(); } catch (RuntimeException ignored) { }
    }

    @SuppressWarnings("deprecation")
    private void syncFromSystem() {
        try {
            for (Network n : connectivity.getAllNetworks()) {
                NetworkCapabilities caps = connectivity.getNetworkCapabilities(n);
                if (caps != null && caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)
                        && !caps.hasTransport(NetworkCapabilities.TRANSPORT_VPN)) add(n, "initial scan");
            }
            if (wifi.isEmpty()) {
                Log.i(TAG, "no Wi-Fi client network (hotspot host or Wi-Fi off): sockets use default routing");
                publishLocked(null);
            }
        } catch (RuntimeException e) { Log.w(TAG, "initial Wi-Fi scan failed: " + e); }
    }

    private boolean hasIpv4(Network network) {
        try {
            LinkProperties lp = connectivity.getLinkProperties(network);
            if (lp == null) return false;
            for (LinkAddress a : lp.getLinkAddresses())
                if (a.getAddress() instanceof Inet4Address && !a.getAddress().isLinkLocalAddress()) return true;
        } catch (RuntimeException ignored) { }
        return false;
    }

    private synchronized void add(Network network, String why) {
        boolean v4 = hasIpv4(network);
        Log.i(TAG, "Wi-Fi network " + why + ": handle=" + network.getNetworkHandle() + " ipv4=" + v4
            + describe(network));
        if (!v4) { wifi.remove(network); if (network.equals(current)) reselect(); return; }
        wifi.remove(network);
        wifi.add(network); // most recently (re)appeared network wins
        publishLocked(network);
    }

    private synchronized void remove(Network network) {
        Log.i(TAG, "Wi-Fi network LOST: handle=" + network.getNetworkHandle());
        wifi.remove(network);
        if (network.equals(current)) reselect();
    }

    private void reselect() {
        Network best = null;
        for (Network n : wifi) best = n; // last = newest
        publishLocked(best);
    }

    private String describe(Network network) {
        try {
            NetworkCapabilities caps = connectivity.getNetworkCapabilities(network);
            LinkProperties lp = connectivity.getLinkProperties(network);
            return " iface=" + (lp != null ? lp.getInterfaceName() : "?")
                + " validated=" + (caps != null && caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED))
                + " addrs=" + (lp != null ? lp.getLinkAddresses() : "?");
        } catch (RuntimeException e) { return ""; }
    }

    private void publishLocked(Network network) {
        current = network;
        push(network == null ? 0L : network.getNetworkHandle());
    }

    private static void push(long handle) {
        try { nativeNetworkChanged(handle); }
        catch (UnsatisfiedLinkError e) { Log.w(TAG, "Native library not ready: " + e.getMessage()); }
    }
}
