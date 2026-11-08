package com.szymonfrompoland.masterbaiter;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;
import android.net.wifi.WifiConfiguration;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import android.net.wifi.WifiNetworkSpecifier;
import android.net.wifi.SupplicantState;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;

import java.lang.ref.WeakReference;

final class RobotWifiConnection {
    static final String ROBOT_SSID = "Robot-Tuner";

    interface Listener {
        void onRobotWifiConnected();
        void onRobotWifiError(String message);
    }

    private static RobotWifiConnection instance;

    static synchronized RobotWifiConnection getInstance(Context context) {
        if (instance == null) {
            instance = new RobotWifiConnection(context.getApplicationContext());
        }
        return instance;
    }

    private final ConnectivityManager connectivityManager;
    private final WifiManager wifiManager;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private WeakReference<Listener> listener = new WeakReference<>(null);
    private ConnectivityManager.NetworkCallback networkCallback;
    private Network boundNetwork;
    private Thread legacyConnectThread;
    private int legacyNetworkId = -1;
    private int generation;

    private RobotWifiConnection(Context context) {
        connectivityManager = (ConnectivityManager)
                context.getSystemService(Context.CONNECTIVITY_SERVICE);
        wifiManager = (WifiManager) context.getApplicationContext()
                .getSystemService(Context.WIFI_SERVICE);
    }

    synchronized void setListener(Listener value) {
        listener = new WeakReference<>(value);
    }

    void connect(String passphrase) {
        String cleanupError = release();
        if (cleanupError != null) {
            postError(cleanupError);
            return;
        }

        int attempt = currentGeneration();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            requestSpecifierNetwork(passphrase, attempt);
        } else {
            connectLegacy(passphrase, attempt);
        }
    }

    String disconnect() {
        return release();
    }

    @android.annotation.TargetApi(Build.VERSION_CODES.Q)
    private void requestSpecifierNetwork(String passphrase, int attempt) {
        try {
            WifiNetworkSpecifier specifier = new WifiNetworkSpecifier.Builder()
                    .setSsid(ROBOT_SSID)
                    .setWpa2Passphrase(passphrase)
                    .build();
            NetworkRequest request = new NetworkRequest.Builder()
                    .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
                    .removeCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
                    .setNetworkSpecifier(specifier)
                    .build();
            ConnectivityManager.NetworkCallback callback =
                    new ConnectivityManager.NetworkCallback() {
                        @Override
                        public void onAvailable(Network network) {
                            synchronized (RobotWifiConnection.this) {
                                if (generation != attempt) return;
                                if (!connectivityManager.bindProcessToNetwork(network)) {
                                    fail(attempt, "Android could not route app traffic through robot Wi-Fi.");
                                    return;
                                }
                                boundNetwork = network;
                            }
                            postConnected(attempt);
                        }

                        @Override
                        public void onUnavailable() {
                            fail(attempt, "Could not connect to " + ROBOT_SSID +
                                    ". Check the Wi-Fi password and approve Android's network request.");
                        }

                        @Override
                        public void onLost(Network network) {
                            synchronized (RobotWifiConnection.this) {
                                if (generation != attempt || boundNetwork != network) return;
                            }
                            fail(attempt, "The connection to " + ROBOT_SSID + " was lost.");
                        }
                    };
            synchronized (this) {
                if (generation != attempt) return;
                networkCallback = callback;
            }
            connectivityManager.requestNetwork(request, callback, 60_000);
        } catch (IllegalArgumentException | SecurityException exception) {
            fail(attempt, "Could not request robot Wi-Fi: " + errorText(exception));
        }
    }

    private void connectLegacy(String passphrase, int attempt) {
        Thread thread = new Thread(() -> connectLegacyNetwork(passphrase, attempt),
                "robot-wifi-connect");
        synchronized (this) {
            if (generation != attempt) return;
            legacyConnectThread = thread;
        }
        thread.start();
    }

    @SuppressWarnings("deprecation")
    private void connectLegacyNetwork(String passphrase, int attempt) {
        try {
            if (wifiManager == null) {
                throw new IllegalStateException("Wi-Fi is unavailable on this device.");
            }
            if (!wifiManager.isWifiEnabled() && !wifiManager.setWifiEnabled(true)) {
                throw new IllegalStateException("Could not turn on Wi-Fi.");
            }

            String quotedSsid = quote(ROBOT_SSID);
            WifiConfiguration configuration = new WifiConfiguration();
            configuration.SSID = quotedSsid;
            configuration.preSharedKey = quote(passphrase);
            configuration.allowedKeyManagement.set(WifiConfiguration.KeyMgmt.WPA_PSK);

            int networkId;
            synchronized (this) {
                networkId = legacyNetworkId;
            }
            if (networkId >= 0) {
                configuration.networkId = networkId;
                networkId = wifiManager.updateNetwork(configuration);
            }
            if (networkId < 0) networkId = wifiManager.addNetwork(configuration);
            if (networkId < 0 || !wifiManager.enableNetwork(networkId, true)) {
                throw new IllegalStateException("Could not select " + ROBOT_SSID + ".");
            }
            wifiManager.disconnect();
            wifiManager.reconnect();

            for (int i = 0; i < 60 && isCurrent(attempt); i++) {
                WifiInfo info = wifiManager.getConnectionInfo();
                if (info != null && info.getNetworkId() == networkId &&
                        info.getSupplicantState() == SupplicantState.COMPLETED) {
                    Network network = findWifiNetwork();
                    if (network != null) {
                        synchronized (this) {
                            if (generation != attempt) return;
                            if (!connectivityManager.bindProcessToNetwork(network)) {
                                throw new IllegalStateException(
                                        "Android could not route app traffic through robot Wi-Fi.");
                            }
                            boundNetwork = network;
                            legacyNetworkId = networkId;
                            legacyConnectThread = null;
                        }
                        postConnected(attempt);
                        return;
                    }
                }
                Thread.sleep(500);
            }
            if (isCurrent(attempt)) {
                fail(attempt, "Timed out connecting to " + ROBOT_SSID + ".");
            }
        } catch (InterruptedException exception) {
            Thread.currentThread().interrupt();
        } catch (IllegalArgumentException | IllegalStateException | SecurityException exception) {
            fail(attempt, "Could not connect to robot Wi-Fi: " + errorText(exception));
        }
    }

    private Network findWifiNetwork() {
        for (Network network : connectivityManager.getAllNetworks()) {
            NetworkCapabilities capabilities = connectivityManager.getNetworkCapabilities(network);
            if (capabilities != null && capabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)) {
                return network;
            }
        }
        return null;
    }

    private static String quote(String value) {
        return "\"" + value.replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
    }

    private void fail(int attempt, String message) {
        if (!isCurrent(attempt)) return;
        String cleanupError = release();
        if (cleanupError != null) message += " " + cleanupError;
        int completedGeneration = currentGeneration();
        String result = message;
        mainHandler.post(() -> {
            if (!isCurrent(completedGeneration)) return;
            Listener target = currentListener();
            if (target != null) target.onRobotWifiError(result);
        });
    }

    private void postConnected(int attempt) {
        mainHandler.post(() -> {
            if (!isCurrent(attempt)) return;
            Listener target = currentListener();
            if (target != null) target.onRobotWifiConnected();
        });
    }

    private void postError(String message) {
        mainHandler.post(() -> {
            Listener target = currentListener();
            if (target != null) target.onRobotWifiError(message);
        });
    }

    private Listener currentListener() {
        synchronized (this) {
            return listener.get();
        }
    }

    private boolean isCurrent(int attempt) {
        synchronized (this) {
            return generation == attempt;
        }
    }

    private int currentGeneration() {
        synchronized (this) {
            return generation;
        }
    }

    private String release() {
        ConnectivityManager.NetworkCallback callback;
        Network network;
        Thread connectThread;
        String error = null;
        synchronized (this) {
            generation++;
            callback = networkCallback;
            networkCallback = null;
            network = boundNetwork;
            boundNetwork = null;
            connectThread = legacyConnectThread;
            legacyConnectThread = null;
            if (network != null && !connectivityManager.bindProcessToNetwork(null)) {
                error = "Android could not release the robot Wi-Fi route.";
            }
        }
        if (connectThread != null && connectThread != Thread.currentThread()) {
            connectThread.interrupt();
        }
        if (callback != null) {
            try {
                connectivityManager.unregisterNetworkCallback(callback);
            } catch (IllegalArgumentException exception) {
                if (error == null) {
                    error = "Android could not release the robot Wi-Fi request: " + errorText(exception);
                }
            }
        }
        return error;
    }

    private static String errorText(Exception exception) {
        String message = exception.getMessage();
        return message == null || message.isEmpty()
                ? exception.getClass().getSimpleName() : message;
    }
}
