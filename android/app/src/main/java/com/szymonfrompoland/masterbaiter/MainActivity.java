package com.szymonfrompoland.masterbaiter;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.ComponentName;
import android.content.ServiceConnection;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.os.IBinder;
import android.os.Build;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.GestureDetector;
import android.view.MotionEvent;
import android.view.ScaleGestureDetector;
import android.view.View;
import android.view.WindowInsets;
import android.text.InputType;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.HorizontalScrollView;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;
import java.security.GeneralSecurityException;
import java.text.DecimalFormat;
import java.text.DecimalFormatSymbols;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class MainActivity extends Activity implements RobotWifiConnection.Listener {
    private static final int EXPORT_REQUEST = 41;
    private static final int[] CHART_COLORS = {
            0xff4fc3f7, 0xffffb74d, 0xff81c784, 0xffef5350,
            0xffba68c8, 0xffffee58, 0xff4db6ac, 0xfff06292,
            0xff7986cb, 0xffaed581, 0xffff8a65, 0xff90a4ae,
            0xff9575cd, 0xffdce775, 0xff4dd0e1, 0xffffd54f,
            0xffa1887f, 0xff64b5f6, 0xffe57373, 0xff80cbc4,
            0xffc5e1a5, 0xffffcc80, 0xffce93d8, 0xffb0bec5
    };

    private final Handler uiHandler = new Handler(Looper.getMainLooper());
    private final ExecutorService apiExecutor = Executors.newSingleThreadExecutor();
    private final ExecutorService statusExecutor = Executors.newSingleThreadExecutor();
    private volatile boolean notificationRequestPending;
    private TelemetryService telemetryService;
    private RobotWifiConnection robotWifiConnection;
    private boolean telemetryBound;
    private AlertDialog robotNotification;
    private String notificationMessage;
    private String pendingWifiPassphrase;
    private boolean usingSavedWifiPassphrase;
    private final TelemetryService.Listener telemetryListener = new TelemetryService.Listener() {
        @Override
        public void onStatus(String message, boolean connected, boolean isRecording) {
            uiHandler.post(() -> showStatus(message));
        }

        @Override
        public void onSample(TelemetryFrame.Sample sample, long dropped, List<String> signals, long revision) {
            remoteDroppedSamples = dropped;
            acceptSample(sample, signals, revision);
        }

        @Override
        public void onRunStarted(long runId, long revision) {
            synchronized (chartsLock) {
                for (LiveChart chart : liveCharts) chart.startRun(runId, revision);
                for (LiveRaw raw : liveRawViews) raw.startRun(runId, revision);
            }
            uiHandler.post(() -> {
                if (logsMetrics != null) {
                    logsMetrics.setText(loggingEnabled
                            ? "Waiting for samples from the new run."
                            : "Logging is off. Enable it in Control to display and save data.");
                }
            });
        }
    };
    private final ServiceConnection telemetryConnection = new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName name, IBinder binder) {
            telemetryService = ((TelemetryService.LocalBinder) binder).service();
            telemetryService.setListener(telemetryListener);
            if (host == null && telemetryService.host() != null) {
                host = telemetryService.host();
                hostInput.setText(host);
                loadRobotSchema(host, false);
            }
        }

        @Override
        public void onServiceDisconnected(ComponentName name) {
            telemetryService = null;
            showStatus("Telemetry service disconnected.");
        }
    }; //this nigga
    private final Runnable notificationPoller = new Runnable() {
        @Override
        public void run() {
            final String requestedHost = host;
            if (requestedHost != null && !notificationRequestPending) {
                notificationRequestPending = true;
                statusExecutor.execute(() -> {
                    try {
                        String notification = requestText(requestedHost, "/notification");
                        lastNotificationPollError = null;
                        uiHandler.post(() -> showRobotNotification(notification));
                    } catch (Exception exception) {
                        String message = errorText(exception);
                        if (!message.equals(lastNotificationPollError)) {
                            lastNotificationPollError = message;
                            uiHandler.post(() -> showStatus("Robot status unavailable: " + message));
                        }
                    } finally {
                        notificationRequestPending = false;
                    }
                });
            }
            uiHandler.postDelayed(this, 1500);
        }
    };

    private LinearLayout pageBody;
    private EditText hostInput;
    private Button connectButton;
    private TextView statusView;
    private TextView logsMetrics;
    private LinearLayout tabButtons;
    private final Object chartsLock = new Object();
    private final List<LiveChart> liveCharts = new ArrayList<>();
    private final List<LiveRaw> liveRawViews = new ArrayList<>();
    private volatile List<String> signalNames = Collections.emptyList();

    private volatile String host;
    private volatile boolean loggingEnabled;
    private File pendingExportFile;

    private JSONObject schema;
    private final List<JSONObject> parameters = new ArrayList<>();
    private String selectedControlTab;
    private String currentPage = "controls";
    private String lastNotificationPollError;
    private long receivedSamples;
    private long missingSamples;
    private long lastSequence = -1;
    private long remoteDroppedSamples;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        robotWifiConnection = RobotWifiConnection.getInstance(this);
        robotWifiConnection.setListener(this);
        buildUi();
        showPage("controls");
        telemetryBound = bindService(new Intent(this, TelemetryService.class), telemetryConnection, 0);
        if (Build.VERSION.SDK_INT >= 33 &&
                checkSelfPermission(android.Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{android.Manifest.permission.POST_NOTIFICATIONS}, 42);
        }
    }

    private void showRobotNotification(String message) {
        if (message.isEmpty()) {
            notificationMessage = null;
            if (robotNotification != null) {
                robotNotification.dismiss();
                robotNotification = null;
            }
        } else if (!message.equals(notificationMessage)) {
            notificationMessage = message;
            if (robotNotification != null) robotNotification.dismiss();
            robotNotification = new AlertDialog.Builder(this)
                    .setMessage(message)
                    .setCancelable(false)
                    .setPositiveButton("Dismiss", (dialog, which) -> runApiAction("/notification/cancel"))
                    .show();
        }
    }

    private void buildUi() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        int horizontalPadding = dp(20);
        int topPadding = dp(12);
        int bottomPadding = dp(12);
        root.setPadding(horizontalPadding, topPadding, horizontalPadding, bottomPadding);
        root.setOnApplyWindowInsetsListener((view, insets) -> {
            int statusBarInset = Build.VERSION.SDK_INT >= Build.VERSION_CODES.R
                    ? insets.getInsets(WindowInsets.Type.statusBars()).top
                    : insets.getSystemWindowInsetTop();
            view.setPadding(horizontalPadding, topPadding + statusBarInset,
                    horizontalPadding, bottomPadding);
            return insets;
        });
        root.setBackgroundColor(Color.rgb(17, 17, 17));

        TextView title = new TextView(this);
        title.setText("Master Baiter Tuner");
        title.setTextSize(24);
        title.setTextColor(Color.rgb(238, 238, 238));
        title.setTypeface(null, Typeface.BOLD);
        title.setPadding(0, 0, 0, dp(8));
        root.addView(title);

        LinearLayout connectionRow = new LinearLayout(this);
        connectionRow.setOrientation(LinearLayout.HORIZONTAL);
        hostInput = new EditText(this);
        hostInput.setSingleLine(true);
        hostInput.setHint("Robot IP address");
        hostInput.setText("192.168.4.1");
        connectionRow.addView(hostInput, new LinearLayout.LayoutParams(0, dp(52), 1.0f));

        connectButton = new Button(this);
        connectButton.setText("Connect");
        connectButton.setOnClickListener(view -> connectToRobot());
        connectionRow.addView(connectButton, new LinearLayout.LayoutParams(
                dp(92), dp(52)));
        Button disconnect = new Button(this);
        disconnect.setText("Disconnect");
        disconnect.setAllCaps(false);
        disconnect.setTextSize(12);
        disconnect.setOnClickListener(view -> {
            uiHandler.removeCallbacks(notificationPoller);
            stopTelemetry();
            pendingWifiPassphrase = null;
            usingSavedWifiPassphrase = false;
            String wifiError = robotWifiConnection.disconnect();
            host = null;
            showStatus(wifiError == null
                    ? "Disconnected. Phone recording has stopped."
                    : "Disconnected, but " + wifiError);
        });
        connectionRow.addView(disconnect, new LinearLayout.LayoutParams(dp(96), dp(52)));
        root.addView(connectionRow);

        statusView = new TextView(this);
        statusView.setText("Press Connect to join Robot-Tuner Wi-Fi and connect to the robot.");
        statusView.setTextSize(13);
        statusView.setTextColor(Color.LTGRAY);
        statusView.setPadding(0, dp(4), 0, dp(8));
        root.addView(statusView);

        HorizontalScrollView tabScroll = new HorizontalScrollView(this);
        tabScroll.setHorizontalScrollBarEnabled(false);
        tabButtons = new LinearLayout(this);
        tabButtons.setOrientation(LinearLayout.HORIZONTAL);
        tabScroll.addView(tabButtons);
        root.addView(tabScroll);

        ScrollView scrollView = new ScrollView(this);
        pageBody = new LinearLayout(this);
        pageBody.setOrientation(LinearLayout.VERTICAL);
        pageBody.setPadding(0, dp(8), 0, dp(16));
        scrollView.addView(pageBody);
        root.addView(scrollView, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1.0f));
        setContentView(root);
        root.requestApplyInsets();
    }

    private void addPageButton(String label, boolean selected, Runnable action) {
        Button button = new Button(this);
        button.setText(label);
        button.setAllCaps(false);
        button.setTextColor(Color.rgb(238, 238, 238));
        button.setBackground(rounded(selected ? 0xff1769aa : 0xff333333, 6));
        button.setPadding(dp(16), 0, dp(16), 0);
        button.setOnClickListener(view -> action.run());
        LinearLayout.LayoutParams layout = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, dp(42));
        layout.setMargins(0, 0, dp(8), 0);
        tabButtons.addView(button, layout);
    }

    private GradientDrawable rounded(int color, int radius) {
        GradientDrawable background = new GradientDrawable();
        background.setColor(color);
        background.setCornerRadius(dp(radius));
        return background;
    }

    private List<String> controlTabs() {
        List<String> tabs = new ArrayList<>();
        for (JSONObject parameter : parameters) {
            String tab = parameter.optString("tab", "Main");
            if (!tabs.contains(tab)) {
                tabs.add(tab);
            }
        }
        return tabs;
    }

    private void rebuildTabs() {
        tabButtons.removeAllViews();
        for (String tab : controlTabs()) {
            addPageButton(tab, "controls".equals(currentPage) && tab.equals(selectedControlTab),
                    () -> {
                        selectedControlTab = tab;
                        showPage("controls");
                    });
        }
        addPageButton("Files", "files".equals(currentPage), () -> showPage("files"));
    }

    private void connectToRobot() {
        String enteredHost = hostInput.getText().toString().trim();
        if (enteredHost.startsWith("http://")) {
            enteredHost = enteredHost.substring("http://".length());
        }
        while (enteredHost.endsWith("/")) {
            enteredHost = enteredHost.substring(0, enteredHost.length() - 1);
        }
        if (enteredHost.isEmpty() || enteredHost.contains("/")) {
            showStatus("Enter a robot host or IP address without a URL path.");
            return;
        }

        final String robotHost = enteredHost;
        try {
            String savedPassphrase = WifiPasswordStore.load(this);
            if (savedPassphrase != null) {
                usingSavedWifiPassphrase = true;
                beginRobotConnection(robotHost, savedPassphrase);
                return;
            }
        } catch (GeneralSecurityException | IOException exception) {
            String message = "Could not read the saved Wi-Fi password: " + errorText(exception);
            try {
                WifiPasswordStore.clear(this);
            } catch (IOException clearException) {
                message += " " + errorText(clearException);
            }
            showStatus(message);
        }

        usingSavedWifiPassphrase = false;
        showWifiPasswordDialog(robotHost);
    }

    private void showWifiPasswordDialog(String robotHost) {
        EditText passwordInput = new EditText(this);
        passwordInput.setSingleLine(true);
        passwordInput.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
        passwordInput.setHint("Wi-Fi password");
        AlertDialog passwordDialog = new AlertDialog.Builder(this)
                .setTitle("Connect to " + RobotWifiConnection.ROBOT_SSID)
                .setMessage("Enter the robot Wi-Fi password. It will be stored encrypted on this phone after a successful connection. Android may ask you to approve the connection.")
                .setView(passwordInput)
                .setNegativeButton("Cancel", (dialog, which) -> connectButton.setEnabled(true))
                .setPositiveButton("Connect", null)
                .create();
        passwordDialog.setOnCancelListener(dialog -> connectButton.setEnabled(true));
        passwordDialog.setOnShowListener(dialog -> passwordDialog.getButton(AlertDialog.BUTTON_POSITIVE)
                .setOnClickListener(view -> {
                    String passphrase = passwordInput.getText().toString();
                    if (passphrase.length() < 8 || passphrase.length() > 63) {
                        passwordInput.setError("Use an 8–63 character Wi-Fi password.");
                        return;
                    }
                    passwordDialog.dismiss();
                    beginRobotConnection(robotHost, passphrase);
                }));
        connectButton.setEnabled(false);
        passwordDialog.show();
    }

    private void beginRobotConnection(String enteredHost, String passphrase) {
        uiHandler.removeCallbacks(notificationPoller);
        stopTelemetry();
        connectButton.setEnabled(false);
        host = enteredHost;
        schema = null;
        parameters.clear();
        signalNames = Collections.emptyList();
        selectedControlTab = null;
        pendingWifiPassphrase = passphrase;
        showPage(currentPage);
        showStatus("Connecting to " + RobotWifiConnection.ROBOT_SSID + " Wi-Fi...");
        robotWifiConnection.connect(passphrase);
    }

    @Override
    public void onRobotWifiConnected() {
        if (host == null) return;
        String passphrase = pendingWifiPassphrase;
        pendingWifiPassphrase = null;
        usingSavedWifiPassphrase = false;
        if (passphrase != null) {
            try {
                WifiPasswordStore.save(this, passphrase);
            } catch (GeneralSecurityException | IOException exception) {
                Toast.makeText(this,
                        "Wi-Fi connected, but the password could not be saved securely: " +
                                errorText(exception),
                        Toast.LENGTH_LONG).show();
            }
        }
        showStatus("Connected to robot Wi-Fi. Connecting to robot API...");
        loadRobotSchema(host, true);
    }

    @Override
    public void onRobotWifiError(String message) {
        pendingWifiPassphrase = null;
        if (usingSavedWifiPassphrase) {
            try {
                WifiPasswordStore.clear(this);
            } catch (IOException exception) {
                message += " Could not clear the saved password: " + errorText(exception);
            }
        }
        usingSavedWifiPassphrase = false;
        connectButton.setEnabled(true);
        showStatus(message);
    }

    private void loadRobotSchema(final String requestedHost, final boolean startStream) {
        connectButton.setEnabled(false);
        showStatus("Connecting to robot API...");

        apiExecutor.execute(() -> {
            try {
                JSONObject receivedSchema = new JSONObject(requestText(requestedHost, "/api/schema"));
                JSONArray configuredParameters = receivedSchema.getJSONArray("parameters");
                List<JSONObject> parsedParameters = new ArrayList<>();
                for (int i = 0; i < configuredParameters.length(); i++) {
                    parsedParameters.add(configuredParameters.getJSONObject(i));
                }
                JSONArray configuredSignals = receivedSchema.getJSONArray("signals");
                if (configuredSignals.length() < 1 || configuredSignals.length() > TelemetryFrame.MAX_SIGNALS) {
                    throw new IOException("Robot has an invalid number of telemetry signals.");
                }
                List<String> parsedSignals = new ArrayList<>();
                for (int i = 0; i < configuredSignals.length(); i++) {
                    String signal = configuredSignals.getString(i);
                    if (!signal.matches("[A-Za-z0-9_-]+") || parsedSignals.contains(signal)) {
                        throw new IOException("Invalid or duplicate signal name: " + signal);
                    }
                    parsedSignals.add(signal);
                }
                List<String> streamSignals = Collections.unmodifiableList(parsedSignals);

                uiHandler.post(() -> {
                    if (!requestedHost.equals(host)) {
                        return;
                    }
                    schema = receivedSchema;
                    signalNames = streamSignals;
                    parameters.clear();
                    parameters.addAll(parsedParameters);
                    loggingEnabled = readLoggingEnabled(parsedParameters);
                    selectedControlTab = null;
                    connectButton.setEnabled(true);
                    showStatus("Connected to robot API. Opening live data...");
                    showPage(currentPage);
                    uiHandler.removeCallbacks(notificationPoller);
                    uiHandler.post(notificationPoller);
                    if (startStream) {
                        startTelemetry(requestedHost, streamSignals);
                    } else if (telemetryService != null && !telemetryService.matchesSignals(streamSignals)) {
                        showStatus("Robot signal schema changed. Connect again to resume recording safely.");
                        stopTelemetry();
                    }
                });
            } catch (Exception exception) {
                uiHandler.post(() -> {
                    if (requestedHost.equals(host)) {
                        connectButton.setEnabled(true);
                        showStatus("Connection failed: " + errorText(exception));
                    }
                });
            }
        });
    }

    private void startTelemetry(String requestedHost, List<String> streamSignals) {
        lastSequence = -1;
        receivedSamples = 0;
        missingSamples = 0;
        remoteDroppedSamples = 0;
        Intent intent = new Intent(this, TelemetryService.class);
        intent.putExtra("host", requestedHost);
        intent.putStringArrayListExtra("signals", new ArrayList<>(streamSignals));
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) startForegroundService(intent);
        else startService(intent);
        if (!telemetryBound) {
            telemetryBound = bindService(intent, telemetryConnection, BIND_AUTO_CREATE);
            if (!telemetryBound) showStatus("Could not bind telemetry service.");
        }
    }

    private boolean readLoggingEnabled(List<JSONObject> controls) {
        for (JSONObject parameter : controls) {
            if ("Logging".equalsIgnoreCase(parameter.optString("name"))) {
                return isOptionOn(parameter, parameter.optInt("selected", 0));
            }
        }
        return false;
    }

    private boolean isOptionOn(JSONObject parameter, int selectedIndex) {
        JSONArray options = parameter.optJSONArray("options");
        if (options == null || selectedIndex < 0 || selectedIndex >= options.length()) {
            return false;
        }
        JSONObject option = options.optJSONObject(selectedIndex);
        return option != null && "ON".equalsIgnoreCase(option.optString("label"));
    }

    private long lastMetricsUpdate;

    private void acceptSample(TelemetryFrame.Sample sample, List<String> streamSignals, long revision) {
        long sequenceNumber = Integer.toUnsignedLong(sample.sequence);
        if (lastSequence >= 0) {
            long delta = (sequenceNumber - lastSequence + 0x100000000L) % 0x100000000L;
            if (delta > 1 && delta < 0x80000000L) {
                missingSamples += delta - 1;
            }
        }
        lastSequence = sequenceNumber;
        receivedSamples++;

        if (!loggingEnabled) return;

        synchronized (chartsLock) {
            for (LiveChart chart : liveCharts) {
                chart.addSample(sample, revision);
            }
            for (LiveRaw raw : liveRawViews) {
                raw.addSample(sample, revision);
            }
        }

        long now = android.os.SystemClock.elapsedRealtime();
        if (now - lastMetricsUpdate >= 100) {
            lastMetricsUpdate = now;
            StringBuilder summary = new StringBuilder(String.format(Locale.US,
                    "Samples: %d   Missing: %d   Robot queue drops: %d\nElapsed: %.3f s",
                    receivedSamples, missingSamples, remoteDroppedSamples, sample.elapsedMs / 1000.0));
            for (int i = 0; i < sample.values.length; i++) {
                summary.append('\n').append(streamSignals.get(i)).append(": ").append(formatNumber(sample.values[i]));
            }
            String text = summary.toString();
            uiHandler.post(() -> {
                if (logsMetrics != null && loggingEnabled) {
                    logsMetrics.setText(text);
                }
            });
        }
    }

    private void stopTelemetry() {
        if (telemetryService != null) {
            telemetryService.setListener(null);
            telemetryService.stopStreaming();
            telemetryService = null;
        }
        if (telemetryBound) unbindService(telemetryConnection);
        telemetryBound = false;
        stopService(new Intent(this, TelemetryService.class));
    }

    private void showPage(String page) {
        currentPage = page;
        synchronized (chartsLock) {
            liveCharts.clear();
            liveRawViews.clear();
        }
        logsMetrics = null;
        pageBody.removeAllViews();
        rebuildTabs();
        if ("controls".equals(page)) {
            buildControlsPage();
        } else if ("files".equals(page)) {
            buildFilesPage();
        }
    }

    private File logDirectory() throws IOException {
        File documents = getExternalFilesDir(Environment.DIRECTORY_DOCUMENTS);
        File directory = new File(documents == null ? getFilesDir() : documents, "robot-logs");
        if (!directory.exists() && !directory.mkdirs()) {
            throw new IOException("Could not create the phone log folder.");
        }
        return directory;
    }

    private void buildControlsPage() {
        if (schema == null || parameters.isEmpty()) {
            TextView disconnected = new TextView(this);
            disconnected.setText("Connect to the robot to load its controls.");
            pageBody.addView(disconnected);
            return;
        }

        List<String> tabs = controlTabs();
        if (tabs.isEmpty()) {
            return;
        }
        if (selectedControlTab == null || !tabs.contains(selectedControlTab)) {
            selectedControlTab = tabs.get(0);
        }

        rebuildTabs();

        if (!loggingEnabled) {
            for (JSONObject parameter : parameters) {
                if (!selectedControlTab.equals(parameter.optString("tab", "Main"))) continue;
                String type = parameter.optString("type", "");
                if ("chart".equals(type) || "raw".equals(type) || "display".equals(type)) {
                    TextView notice = new TextView(this);
                    notice.setText("Logging is OFF. Turn it ON in Control to display live data and save CSV files.");
                    notice.setTextColor(0xffbbbbbb);
                    notice.setPadding(dp(12), dp(10), dp(12), dp(10));
                    pageBody.addView(notice);
                    break;
                }
            }
        }

        String previousCategory = null;
        for (JSONObject parameter : parameters) {
            if (!selectedControlTab.equals(parameter.optString("tab", "Main"))) {
                continue;
            }
            String category = parameter.optString("category", "");
            if (!category.equals(previousCategory) && !category.isEmpty()) {
                pageBody.addView(sectionTitle(category));
            }
            previousCategory = category;
            addParameterControl(parameter);
        }

    }

    private void addParameterControl(JSONObject parameter) {
        String type = parameter.optString("type", "");
        String name = parameter.optString("name", "Control");
        int id = parameter.optInt("id", -1);

        if ("button".equals(type)) {
            Button button = new Button(this);
            button.setText(name);
            button.setAllCaps(false);
            button.setOnClickListener(view -> runApiAction("/button?id=" + id));
            LinearLayout card = parameterCard();
            card.addView(button, new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT, dp(52)));
            return;
        }

        if ("dropdown".equals(type)) {
            addDropdown(parameter);
            return;
        }

        if ("slider".equals(type)) {
            addSlider(parameter);
        } else if ("angle".equals(type)) {
            LinearLayout card = parameterCard();
            card.addView(parameterLabel(name));
            card.addView(new AngleView(parameter), new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT, dp(300)));
        } else if ("chart".equals(type) || "raw".equals(type)) {
            addChartControl(parameter, true, null);
        } else if ("display".equals(type)) {
            if (!loggingEnabled) return;
            LinearLayout card = parameterCard();
            card.addView(parameterLabel(name));
            logsMetrics = new TextView(this);
            logsMetrics.setTextColor(0xff9cff9c);
            logsMetrics.setBackground(rounded(0xff050505, 8));
            logsMetrics.setPadding(dp(12), dp(12), dp(12), dp(12));
            logsMetrics.setText("Waiting for live samples.");
            if (telemetryService != null) {
                SessionHistory.Snapshot history = telemetryService.history();
                if (!history.samples.isEmpty()) {
                    TelemetryFrame.Sample latest = history.samples.get(history.samples.size() - 1);
                    StringBuilder text = new StringBuilder(String.format(Locale.US, "Elapsed: %.3f s", latest.elapsedMs / 1000.0));
                    for (int i = 0; i < latest.values.length && i < signalNames.size(); i++) {
                        text.append('\n').append(signalNames.get(i)).append(": ").append(formatNumber(latest.values[i]));
                    }
                    logsMetrics.setText(text.toString());
                }
            }
            card.addView(logsMetrics);
        }
    }

    private void addDropdown(JSONObject parameter) {
        LinearLayout card = parameterCard();
        card.addView(parameterLabel(parameter.optString("name", "Option")));

        JSONArray options = parameter.optJSONArray("options");
        if (options == null || options.length() == 0) {
            return;
        }

        List<String> labels = new ArrayList<>();
        for (int i = 0; i < options.length(); i++) {
            labels.add(options.optJSONObject(i).optString("label", "Option " + i));
        }
        Spinner spinner = new Spinner(this);
        ArrayAdapter<String> adapter = new ArrayAdapter<>(
                this, android.R.layout.simple_spinner_item, labels);
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        spinner.setAdapter(adapter);
        int selected = parameter.optInt("selected", 0);
        spinner.setSelection(Math.max(0, Math.min(selected, labels.size() - 1)));

        final boolean[] initialized = {false};
        spinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View view, int position, long itemId) {
                if (initialized[0]) {
                    try {
                        parameter.put("selected", position);
                    } catch (org.json.JSONException exception) {
                        showStatus("Could not update selection: " + errorText(exception));
                    }
                    if ("Logging".equalsIgnoreCase(parameter.optString("name"))) {
                        loggingEnabled = isOptionOn(parameter, position);
                    }
                    runApiAction(updatePath(parameter.optInt("id", -1), position));
                }
                initialized[0] = true;
            }

            @Override
            public void onNothingSelected(AdapterView<?> parent) {
            }
        });
        card.addView(spinner);
    }

    private void addSlider(JSONObject parameter) {
        double minimum = parameter.optDouble("min", 0.0);
        double maximum = parameter.optDouble("max", 1.0);
        double step = parameter.optDouble("step", 0.01);
        double current = parameter.optDouble("value", minimum);

        LinearLayout card = parameterCard();
        card.addView(parameterLabel(parameter.optString("name", "Value")));
        LinearLayout row = new LinearLayout(this);
        row.setGravity(Gravity.CENTER_VERTICAL);
        card.addView(row);
        EditText valueInput = new EditText(this);
        valueInput.setSingleLine(true);
        valueInput.setInputType(android.text.InputType.TYPE_CLASS_NUMBER |
                android.text.InputType.TYPE_NUMBER_FLAG_DECIMAL |
                android.text.InputType.TYPE_NUMBER_FLAG_SIGNED);
        valueInput.setText(formatNumber(current));

        SeekBar seekBar = new SeekBar(this);
        seekBar.setMax(1000);
        seekBar.setProgress(toProgress(current, minimum, maximum));
        seekBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar bar, int progress, boolean fromUser) {
                if (fromUser) {
                    double value = sliderValue(progress, minimum, maximum, step);
                    valueInput.setText(formatNumber(value));
                }
            }

            @Override
            public void onStartTrackingTouch(SeekBar bar) {
            }

            @Override
            public void onStopTrackingTouch(SeekBar bar) {
                double value = sliderValue(bar.getProgress(), minimum, maximum, step);
                rememberValue(parameter, value);
                runApiAction(updatePath(parameter.optInt("id", -1), value));
            }
        });
        row.addView(seekBar, new LinearLayout.LayoutParams(0, dp(48), 1));
        row.addView(valueInput, new LinearLayout.LayoutParams(dp(85), dp(48)));
        valueInput.setOnFocusChangeListener((view, hasFocus) -> {
            if (hasFocus) {
                return;
            }
            try {
                double value = Double.parseDouble(valueInput.getText().toString());
                if (!Double.isFinite(value) || value < minimum || value > maximum) {
                    throw new NumberFormatException("outside allowed range");
                }
                if (step > 0) {
                    value = Math.max(minimum, Math.min(maximum,
                            minimum + Math.round((value - minimum) / step) * step));
                }
                valueInput.setText(formatNumber(value));
                seekBar.setProgress(toProgress(value, minimum, maximum));
                if (Math.abs(value - parameter.optDouble("value", current)) > 0.000001) {
                    rememberValue(parameter, value);
                    runApiAction(updatePath(parameter.optInt("id", -1), value));
                }
            } catch (NumberFormatException exception) {
                valueInput.setText(formatNumber(parameter.optDouble("value", current)));
                showStatus("Enter a value between " + minimum + " and " + maximum + ".");
            }
        });
    }

    private void rememberValue(JSONObject parameter, double value) {
        try {
            parameter.put("value", value);
        } catch (org.json.JSONException exception) {
            showStatus("Could not update value: " + errorText(exception));
        }
    }

    private LinearLayout parameterCard() {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setBackground(rounded(0xff222222, 8));
        card.setPadding(dp(12), dp(12), dp(12), dp(12));
        LinearLayout.LayoutParams layout = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        layout.setMargins(0, dp(4), 0, dp(4));
        pageBody.addView(card, layout);
        return card;
    }

    private TextView parameterLabel(String text) {
        TextView label = new TextView(this);
        label.setText(text);
        label.setTextColor(0xffeeeeee);
        label.setTextSize(16);
        label.setPadding(0, 0, 0, dp(8));
        return label;
    }

    private int toProgress(double value, double minimum, double maximum) {
        if (maximum <= minimum) {
            return 0;
        }
        double fraction = (value - minimum) / (maximum - minimum);
        return (int) Math.round(Math.max(0.0, Math.min(1.0, fraction)) * 1000.0);
    }

    private double sliderValue(int progress, double minimum, double maximum, double step) {
        if (maximum <= minimum) {
            return minimum;
        }
        double value = minimum + (maximum - minimum) * progress / 1000.0;
        if (step > 0.0) {
            value = minimum + Math.round((value - minimum) / step) * step;
        }
        return Math.max(minimum, Math.min(maximum, value));
    }

    private void buildFilesPage() {
        pageBody.addView(sectionTitle("Phone log files"));
        Button refresh = new Button(this);
        refresh.setText("Refresh");
        refresh.setOnClickListener(view -> showPage("files"));
        pageBody.addView(refresh);
        Button deleteAllLogs = new Button(this);
        deleteAllLogs.setText("Delete all logs");
        deleteAllLogs.setEnabled(false);
        deleteAllLogs.setOnClickListener(view -> confirmDeleteAllLogs());
        pageBody.addView(deleteAllLogs);

        try {
            File[] files = logDirectory().listFiles((directory, name) -> name.endsWith(".csv"));
            if (files == null) {
                throw new IOException("The phone log folder could not be read.");
            }
            deleteAllLogs.setEnabled(files.length > 0);
            if (files.length == 0) {
                TextView empty = new TextView(this);
                empty.setText("No phone log files yet.");
                pageBody.addView(empty);
                return;
            }
            Arrays.sort(files, new Comparator<File>() {
                @Override
                public int compare(File left, File right) {
                    if (left.lastModified() == right.lastModified()) {
                        return 0;
                    }
                    return left.lastModified() < right.lastModified() ? 1 : -1;
                }
            });
            for (File file : files) {
                LinearLayout row = new LinearLayout(this);
                row.setOrientation(LinearLayout.VERTICAL);
                TextView details = new TextView(this);
                details.setText(file.getName() + "\n" + file.length() + " bytes");
                details.setPadding(dp(8), dp(8), dp(8), dp(4));
                row.addView(details);

                LinearLayout actions = new LinearLayout(this);
                actions.setOrientation(LinearLayout.HORIZONTAL);
                Button export = new Button(this);
                export.setText("Export");
                export.setOnClickListener(view -> exportFile(file));
                Button view = new Button(this);
                view.setText("View");
                view.setOnClickListener(ignored -> viewLogFile(file));
                Button delete = new Button(this);
                delete.setText("Delete");
                delete.setOnClickListener(ignored -> confirmDeleteLog(file));
                Button rename = new Button(this);
                rename.setText("Rename");
                rename.setOnClickListener(ignored -> renameLog(file));
                actions.addView(view, new LinearLayout.LayoutParams(0, dp(52), 1));
                actions.addView(export, new LinearLayout.LayoutParams(0, dp(52), 1));
                actions.addView(rename, new LinearLayout.LayoutParams(0, dp(52), 1));
                actions.addView(delete, new LinearLayout.LayoutParams(0, dp(52), 1));
                row.addView(actions);
                pageBody.addView(row);
            }
        } catch (IOException exception) {
            TextView error = new TextView(this);
            error.setText("Could not list phone logs: " + errorText(exception));
            pageBody.addView(error);
        }
    }

    private void confirmDeleteLog(File file) {
        new AlertDialog.Builder(this)
                .setTitle("Delete phone log?")
                .setMessage("Permanently delete " + file.getName() + " from this phone?")
                .setNegativeButton("Cancel", null)
                .setPositiveButton("Delete", (dialog, which) -> {
                    try {
                        PhoneRecorder.deleteClosedLog(file);
                        showStatus("Deleted " + file.getName());
                        showPage("files");
                    } catch (IOException exception) {
                        showStatus("Could not delete log: " + errorText(exception));
                    }
                })
                .show();
    }

    private void confirmDeleteAllLogs() {
        new AlertDialog.Builder(this)
                .setTitle("Delete all phone logs?")
                .setMessage("Permanently delete all phone log files from this phone? This cannot be undone. Stop recording first to delete every file.")
                .setNegativeButton("Cancel", null)
                .setPositiveButton("Delete all", (dialog, which) -> {
                    try {
                        int deleted = PhoneRecorder.deleteClosedLogs(logDirectory());
                        showStatus("Deleted " + deleted + (deleted == 1 ? " phone log." : " phone logs."));
                    } catch (IOException exception) {
                        showStatus("Could not delete all phone logs: " + errorText(exception));
                    }
                    showPage("files");
                })
                .show();
    }

    private void renameLog(File file) {
        EditText input = new EditText(this);
        input.setSingleLine(true);
        input.setText(file.getName());
        new AlertDialog.Builder(this)
                .setTitle("Rename phone log")
                .setView(input)
                .setNegativeButton("Cancel", null)
                .setPositiveButton("Rename", (dialog, which) -> {
                    try {
                        File renamed = PhoneRecorder.renameClosedLog(file, input.getText().toString());
                        showStatus("Renamed to " + renamed.getName());
                        showPage("files");
                    } catch (IOException exception) {
                        showStatus("Could not rename log: " + errorText(exception));
                    }
                })
                .show();
    }

    private static final class SavedLog {
        final List<String> columns;
        final List<float[]> rows;
        final long totalRows;

        SavedLog(List<String> columns, List<float[]> rows, long totalRows) {
            this.columns = columns;
            this.rows = rows;
            this.totalRows = totalRows;
        }
    }

    private SavedLog readLog(File file) throws IOException {
        try (BufferedReader reader = new BufferedReader(new InputStreamReader(
                new FileInputStream(file), StandardCharsets.UTF_8))) {
            String header = reader.readLine();
            if (header == null) {
                throw new IOException("The log is empty.");
            }
            List<String> columns = Arrays.asList(header.split(",", -1));
            if (columns.size() < 3 || !columns.get(0).equals("elapsed_ms") ||
                    !columns.get(1).equals("sequence")) {
                throw new IOException("Invalid log header.");
            }
            List<float[]> rows = new ArrayList<>();
            long total = 0;
            long stride = 1;
            String line;
            while ((line = reader.readLine()) != null) {
                String[] fields = line.split(",", -1);
                if (fields.length != columns.size()) {
                    throw new IOException("Invalid CSV row " + (total + 2));
                }
                float[] row = new float[fields.length];
                try {
                    for (int i = 0; i < fields.length; i++) {
                        row[i] = Float.parseFloat(fields[i]);
                    }
                } catch (NumberFormatException exception) {
                    throw new IOException("Invalid number in CSV row " + (total + 2), exception);
                }
                if (total % stride == 0) {
                    rows.add(row);
                }
                total++;
                if (rows.size() > 2000) {
                    List<float[]> reduced = new ArrayList<>();
                    for (int i = 0; i < rows.size(); i += 2) {
                        reduced.add(rows.get(i));
                    }
                    rows = reduced;
                    stride *= 2;
                }
            }
            return new SavedLog(columns, rows, total);
        }
    }

    private void viewLogFile(File file) {
        showStatus("Reading " + file.getName() + "...");
        apiExecutor.execute(() -> {
            try {
                SavedLog log = readLog(file);
                uiHandler.post(() -> {
                    if (!"files".equals(currentPage)) {
                        return;
                    }
                    pageBody.removeAllViews();
                    Button back = new Button(this);
                    back.setText("Back to files");
                    back.setOnClickListener(view -> showPage("files"));
                    pageBody.addView(back);
                    pageBody.addView(sectionTitle(file.getName()));
                    TextView summary = new TextView(this);
                    summary.setText("Rows: " + log.totalRows + " (showing " +
                            log.rows.size() + " evenly sampled points; export retains all rows)");
                    pageBody.addView(summary);
                    List<String> displayed = new ArrayList<>();
                    for (JSONObject parameter : parameters) {
                        if ("chart".equals(parameter.optString("type")) ||
                                "raw".equals(parameter.optString("type"))) {
                            JSONArray series = parameter.optJSONArray("series");
                            boolean allPresent = series != null && series.length() > 0;
                            if (series != null) {
                                for (int i = 0; i < series.length(); i++) {
                                    allPresent &= log.columns.contains(series.optString(i));
                                }
                                if (allPresent) {
                                    for (int i = 0; i < series.length(); i++) {
                                        displayed.add(series.optString(i));
                                    }
                                }
                            }
                            addChartControl(parameter, false, log);
                        }
                    }
                    for (int i = 2; i < log.columns.size(); i++) {
                        String name = log.columns.get(i);
                        if (!displayed.contains(name)) {
                            JSONObject chart = new JSONObject();
                            try {
                                chart.put("name", name);
                                chart.put("type", "chart");
                                chart.put("series", new JSONArray().put(name));
                                addChartControl(chart, false, log);
                            } catch (org.json.JSONException exception) {
                                showStatus("Could not display signal: " + errorText(exception));
                            }
                        }
                    }
                    showStatus("Showing " + file.getName());
                });
            } catch (IOException exception) {
                uiHandler.post(() -> showStatus("Could not read log: " + errorText(exception)));
            }
        });
    }

    private void exportFile(File file) {
        pendingExportFile = file;
        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("text/csv");
        intent.putExtra(Intent.EXTRA_TITLE, file.getName());
        startActivityForResult(intent, EXPORT_REQUEST);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != EXPORT_REQUEST || resultCode != RESULT_OK ||
                data == null || data.getData() == null || pendingExportFile == null) {
            return;
        }

        Uri destination = data.getData();
        File source = pendingExportFile;
        apiExecutor.execute(() -> {
            try {
                OutputStream destinationStream = getContentResolver().openOutputStream(destination);
                if (destinationStream == null) {
                    throw new IOException("The selected destination could not be opened.");
                }
                try (InputStream input = new BufferedInputStream(new FileInputStream(source));
                     OutputStream output = new BufferedOutputStream(destinationStream)) {
                    byte[] buffer = new byte[8192];
                    int read;
                    while ((read = input.read(buffer)) != -1) {
                        output.write(buffer, 0, read);
                    }
                    output.flush();
                }
                uiHandler.post(() -> showStatus("Exported " + source.getName()));
            } catch (Exception exception) {
                uiHandler.post(() -> showStatus("Export failed: " + errorText(exception)));
            }
        });
    }

    private void runApiAction(String path) {
        final String requestedHost = host;
        if (requestedHost == null) {
            showStatus("Connect to the robot first.");
            return;
        }
        apiExecutor.execute(() -> {
            try {
                requestText(requestedHost, path);
                uiHandler.post(() -> showStatus("Robot updated."));
            } catch (Exception exception) {
                uiHandler.post(() -> {
                    showStatus("Robot command failed: " + errorText(exception));
                    if (requestedHost.equals(host)) loadRobotSchema(requestedHost, false);
                });
            }
        });
    }

    private String updatePath(int parameterId, double value) {
        return updatePath(parameterId, Double.toString(value));
    }

    private String updatePath(int parameterId, int optionIndex) {
        return updatePath(parameterId, Integer.toString(optionIndex));
    }

    private String updatePath(int parameterId, String value) {
        try {
            return "/update?id=" + parameterId + "&value=" +
                    URLEncoder.encode(value, "UTF-8");
        } catch (Exception exception) {
            throw new IllegalStateException(exception);
        }
    }

    private String requestText(String requestedHost, String path) throws IOException {
        return requestText(requestedHost, path, "GET", null);
    }

    private String requestText(
            String requestedHost,
            String path,
            String method,
            String body) throws IOException {
        HttpURLConnection connection = null;
        try {
            connection = (HttpURLConnection) new URL("http://" + requestedHost + path).openConnection();
            connection.setRequestMethod(method);
            connection.setConnectTimeout(3000);
            connection.setReadTimeout(5000);
            connection.setUseCaches(false);
            if (body != null) {
                connection.setDoOutput(true);
                connection.setRequestProperty(
                        "Content-Type",
                        "application/x-www-form-urlencoded; charset=UTF-8");
                try (OutputStream output = connection.getOutputStream()) {
                    output.write(body.getBytes(StandardCharsets.UTF_8));
                }
            }
            int status = connection.getResponseCode();
            InputStream responseStream = status >= 200 && status < 300
                    ? connection.getInputStream() : connection.getErrorStream();
            StringBuilder response = new StringBuilder();
            if (responseStream != null) {
                try (BufferedReader reader = new BufferedReader(
                        new InputStreamReader(responseStream, StandardCharsets.UTF_8))) {
                    String line;
                    while ((line = reader.readLine()) != null) {
                        response.append(line);
                    }
                }
            }
            if (status < 200 || status >= 300) {
                throw new IOException("HTTP " + status + (response.length() == 0 ? "" : ": " + response));
            }
            return response.toString();
        } finally {
            if (connection != null) {
                connection.disconnect();
            }
        }
    }

    private TextView sectionTitle(String text) {
        TextView title = new TextView(this);
        title.setText(text);
        title.setTextSize(22);
        title.setTextColor(0xffeeeeee);
        title.setTypeface(null, Typeface.BOLD);
        title.setPadding(0, dp(25), 0, dp(10));
        return title;
    }

    private String formatNumber(double value) {
        return String.format(Locale.US, "%.3f", value);
    }

    private int dp(int value) {
        return (int) (value * getResources().getDisplayMetrics().density + 0.5f);
    }

    private void showStatus(String message) {
        if (statusView != null) {
            if (Looper.myLooper() == Looper.getMainLooper()) {
                statusView.setText(message);
            } else {
                uiHandler.post(() -> {
                    if (statusView != null) {
                        statusView.setText(message);
                    }
                });
            }
        }
    }

    private String errorText(Exception exception) {
        String message = exception.getMessage();
        return message == null || message.isEmpty()
                ? exception.getClass().getSimpleName() : message;
    }

    @Override
    protected void onDestroy() {
        uiHandler.removeCallbacks(notificationPoller);
        robotWifiConnection.setListener(null);
        if (telemetryService != null) telemetryService.setListener(null);
        if (telemetryBound) unbindService(telemetryConnection);
        apiExecutor.shutdown();
        statusExecutor.shutdownNow();
        super.onDestroy();
    }

    private void addChartControl(JSONObject parameter, boolean live, SavedLog log) {
        if (live && !loggingEnabled) return;
        JSONArray series = parameter.optJSONArray("series");
        if (series == null || series.length() == 0) {
            return;
        }
        List<String> columns = live ? signalNames : log.columns;
        int[] indexes = new int[series.length()];
        List<String> names = new ArrayList<>();
        for (int i = 0; i < indexes.length; i++) {
            String name = series.optString(i);
            indexes[i] = columns.indexOf(name);
            names.add(name);
        }
        LinearLayout card = parameterCard();
        card.addView(parameterLabel(parameter.optString("name", "Chart")));
        boolean missing = false;
        for (int index : indexes) {
            missing |= index < 0;
        }
        if (missing) {
            TextView error = new TextView(this);
            error.setText("This log does not contain all requested signals: " + names);
            card.addView(error);
            return;
        }
        int[] seriesColors = new int[names.size()];
        for (int i = 0; i < seriesColors.length; i++) {
            seriesColors[i] = CHART_COLORS[i % CHART_COLORS.length];
        }
        if ("raw".equals(parameter.optString("type"))) {
            TextView raw = new TextView(this);
            raw.setTextColor(0xff9cff9c);
            raw.setBackground(rounded(0xff050505, 6));
            raw.setPadding(dp(12), dp(12), dp(12), dp(12));
            if (live) {
                raw.setText("Waiting for samples.");
                card.addView(raw);
                synchronized (chartsLock) {
                    LiveRaw liveRaw = new LiveRaw(raw, indexes, names);
                    SessionHistory.Snapshot history = telemetryService == null ? null : telemetryService.history();
                    if (history != null) {
                        liveRaw.startRun(history.runId, history.revision);
                        int start = Math.max(0, history.samples.size() - 20);
                        for (int i = start; i < history.samples.size(); i++) liveRaw.addSample(history.samples.get(i), history.revision);
                        liveRaw.render();
                    }
                    liveRawViews.add(liveRaw);
                }
                return;
            }
            StringBuilder text = new StringBuilder();
            int start = Math.max(0, log.rows.size() - 100);
            for (int i = start; i < log.rows.size(); i++) {
                float[] row = log.rows.get(i);
                text.append(formatNumber(row[0])).append(" ms: ");
                for (int j = 0; j < indexes.length; j++) {
                    if (j > 0) text.append(", ");
                    text.append(names.get(j)).append('=').append(formatNumber(row[indexes[j]]));
                }
                text.append('\n');
            }
            raw.setText(text.length() == 0 ? "No samples." : text.toString());
            card.addView(raw);
            return;
        }
        TextView cursorReadout = new TextView(this);
        cursorReadout.setText("Tap or drag across the chart to inspect a sample.");
        cursorReadout.setTextColor(0xffdddddd);
        cursorReadout.setTextSize(12);
        cursorReadout.setBackground(rounded(0xff050505, 6));
        cursorReadout.setPadding(dp(10), dp(8), dp(10), dp(8));
        ChartView chart = new ChartView(names, seriesColors, live,
                parameter.has("min") ? (float) parameter.optDouble("min") : Float.NaN,
                parameter.has("max") ? (float) parameter.optDouble("max") : Float.NaN,
                cursorReadout);
        card.addView(chart, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(260)));
        LinearLayout chartActions = new LinearLayout(this);
        chartActions.setGravity(Gravity.END);
        Button resetView = new Button(this);
        resetView.setText("Reset view");
        resetView.setAllCaps(false);
        resetView.setOnClickListener(view -> chart.resetView());
        chartActions.addView(resetView, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, dp(44)));
        card.addView(chartActions);
        card.addView(cursorReadout);
        TextView legend = new TextView(this);
        android.text.SpannableStringBuilder legendText = new android.text.SpannableStringBuilder();
        for (int i = 0; i < names.size(); i++) {
            if (i > 0) legendText.append("   ");
            int markerStart = legendText.length();
            legendText.append("\u25CF");
            legendText.setSpan(new android.text.style.ForegroundColorSpan(seriesColors[i]),
                    markerStart, legendText.length(),
                    android.text.Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);
            legendText.append(' ');
            int labelStart = legendText.length();
            legendText.append(names.get(i));
            legendText.setSpan(new android.text.style.ForegroundColorSpan(seriesColors[i]),
                    labelStart, legendText.length(),
                    android.text.Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);
        }
        legend.setText(legendText);
        legend.setTextColor(0xffbbbbbb);
        legend.setTextSize(12);
        card.addView(legend);
        TextView gestureHint = new TextView(this);
        gestureHint.setText("Pinch to zoom | Drag to pan | Tap to inspect");
        gestureHint.setTextColor(0xff888888);
        gestureHint.setTextSize(11);
        card.addView(gestureHint);
        if (live) {
            synchronized (chartsLock) {
                LiveChart liveChart = new LiveChart(chart, indexes);
                SessionHistory.Snapshot history = telemetryService == null ? null : telemetryService.history();
                if (history != null) {
                    liveChart.startRun(history.runId, history.revision);
                    for (TelemetryFrame.Sample sample : history.samples) liveChart.addSample(sample, history.revision);
                }
                liveCharts.add(liveChart);
            }
        } else {
            for (float[] row : log.rows) {
                float[] points = new float[indexes.length];
                for (int i = 0; i < indexes.length; i++) points[i] = row[indexes[i]];
                chart.addSample((long) row[0], points);
            }
        }
    }

    private final class LiveChart {
        private final ChartView chart;
        private final int[] indexes;
        private long runId = -1;
        private long revision = -1;
        private long lastSequence = -1;
        LiveChart(ChartView chart, int[] indexes) {
            this.chart = chart;
            this.indexes = indexes;
        }
        void startRun(long id, long newRevision) {
            if (newRevision <= revision) return;
            revision = newRevision;
            runId = id;
            lastSequence = -1;
            chart.clear();
        }
        void addSample(TelemetryFrame.Sample sample, long sampleRevision) {
            if (sampleRevision < revision) return;
            long id = sample.runState & ~1L;
            if (sampleRevision > revision) startRun(id, sampleRevision);
            if (id != runId) return;
            long sequence = Integer.toUnsignedLong(sample.sequence);
            if (lastSequence >= 0 && ((sequence - lastSequence) & 0xffffffffL) >= 0x80000000L) return;
            if (lastSequence == sequence) return;
            lastSequence = sequence;
            float[] points = new float[indexes.length];
            for (int i = 0; i < indexes.length; i++) points[i] = sample.values[indexes[i]];
            chart.addSample(sample.elapsedMs, points);
        }
    }

    private final class LiveRaw {
        private final TextView view;
        private final int[] indexes;
        private final List<String> names;
        private final ArrayDeque<String> lines = new ArrayDeque<>();
        private long runId = -1;
        private long revision = -1;
        private long lastSequence = -1;
        private long lastUpdate;

        LiveRaw(TextView view, int[] indexes, List<String> names) {
            this.view = view;
            this.indexes = indexes;
            this.names = names;
        }

        void startRun(long id, long newRevision) {
            if (newRevision <= revision) return;
            revision = newRevision;
            runId = id;
            lastSequence = -1;
            lines.clear();
            render();
        }

        void addSample(TelemetryFrame.Sample sample, long sampleRevision) {
            if (sampleRevision < revision) return;
            long id = sample.runState & ~1L;
            if (sampleRevision > revision) startRun(id, sampleRevision);
            if (id != runId) return;
            long sequence = Integer.toUnsignedLong(sample.sequence);
            if (lastSequence >= 0 && ((sequence - lastSequence) & 0xffffffffL) >= 0x80000000L) return;
            if (lastSequence == sequence) return;
            lastSequence = sequence;
            StringBuilder line = new StringBuilder().append(sample.elapsedMs).append(" ms: ");
            for (int i = 0; i < indexes.length; i++) {
                if (i > 0) line.append(", ");
                line.append(names.get(i)).append('=').append(formatNumber(sample.values[indexes[i]]));
            }
            if (lines.size() >= 20) lines.removeFirst();
            lines.addLast(line.toString());
            long now = android.os.SystemClock.elapsedRealtime();
            if (now - lastUpdate >= 100) { lastUpdate = now; render(); }
        }

        void render() {
            String text = lines.isEmpty() ? "Waiting for samples." : android.text.TextUtils.join("\n", lines);
            uiHandler.post(() -> view.setText(text));
        }
    }

    private final class AngleView extends View {
        private final JSONObject parameter;
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Path arrowHead = new Path();
        private float angle;
        private float angleBeforeDrag;

        AngleView(JSONObject parameter) {
            super(MainActivity.this);
            this.parameter = parameter;
            angle = (float) parameter.optDouble("value", 0);
        }

        @Override
        protected void onDraw(Canvas canvas) {
            super.onDraw(canvas);
            float x = getWidth() / 2f, y = getHeight() / 2f;
            float radius = Math.min(getWidth(), getHeight()) * .34f;
            paint.setColor(Color.WHITE);
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(dp(3));
            canvas.drawCircle(x, y, radius, paint);
            double radians = Math.toRadians(angle);
            float directionX = (float) Math.sin(radians);
            float directionY = -(float) Math.cos(radians);
            float pointerRadius = radius * 74f / 82f;
            float tipX = x + directionX * pointerRadius;
            float tipY = y + directionY * pointerRadius;
            canvas.drawLine(x, y, tipX, tipY, paint);
            paint.setStyle(Paint.Style.FILL);
            float scale = radius / 82f;
            float baseX = tipX - directionX * 12f * scale;
            float baseY = tipY - directionY * 12f * scale;
            arrowHead.reset();
            arrowHead.moveTo(tipX, tipY);
            arrowHead.lineTo(baseX - directionY * 5f * scale, baseY + directionX * 5f * scale);
            arrowHead.lineTo(baseX + directionY * 5f * scale, baseY - directionX * 5f * scale);
            arrowHead.close();
            canvas.drawPath(arrowHead, paint);
            paint.setTextSize(dp(18));
            paint.setTextAlign(Paint.Align.CENTER);
            canvas.drawText("0", x, y - radius - dp(10), paint);
            String value = String.format(Locale.US, "%.1f", angle);
            paint.setTextSize((value.length() > 4 ? 14f : 16f) * scale);
            float labelRadius = Math.max(8f * scale, Math.min(44f * scale,
                    pointerRadius - 12f * scale - paint.measureText(value) / 2f - 4f * scale));
            float rotation = angle < 0 ? angle + 90f : angle - 90f;
            double textDirection = Math.toRadians(rotation);
            float labelX = x + directionX * labelRadius + (float) Math.sin(textDirection) * 20f * scale;
            float labelY = y + directionY * labelRadius - (float) Math.cos(textDirection) * 20f * scale;
            canvas.save();
            canvas.rotate(rotation, labelX, labelY);
            canvas.drawText(value, labelX, labelY - (paint.ascent() + paint.descent()) / 2f, paint);
            canvas.restore();
        }

        @Override
        public boolean onTouchEvent(MotionEvent event) {
            int action = event.getActionMasked();
            if (action == MotionEvent.ACTION_DOWN) {
                angleBeforeDrag = angle;
                getParent().requestDisallowInterceptTouchEvent(true);
            }
            if (action == MotionEvent.ACTION_DOWN ||
                    action == MotionEvent.ACTION_MOVE ||
                    action == MotionEvent.ACTION_UP) {
                angle = (float) Math.toDegrees(Math.atan2(
                        event.getX() - getWidth() / 2f, getHeight() / 2f - event.getY()));
                angle = Math.round(angle * 10f) / 10f;
                invalidate();
                if (action == MotionEvent.ACTION_UP) {
                    getParent().requestDisallowInterceptTouchEvent(false);
                    rememberValue(parameter, angle);
                    runApiAction(updatePath(parameter.optInt("id", -1), angle));
                    performClick();
                }
                return true;
            }
            if (action == MotionEvent.ACTION_CANCEL) {
                getParent().requestDisallowInterceptTouchEvent(false);
                angle = angleBeforeDrag;
                invalidate();
                return true;
            }
            return true;
        }

        @Override
        public boolean performClick() {
            super.performClick();
            return true;
        }
    }

    private final class ChartView extends View {
        private final class Point {
            final long elapsedMs;
            final float[] values;
            Point(long elapsedMs, float[] values) {
                this.elapsedMs = elapsedMs;
                this.values = values;
            }
        }

        private final ArrayDeque<Point> values = new ArrayDeque<>();
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final List<String> seriesNames;
        private final TextView cursorReadout;
        private final int limit;
        private final float fixedMin;
        private final float fixedMax;
        private final int[] seriesColors;
        private final DecimalFormat axisFormat = new DecimalFormat(
                "0.######", DecimalFormatSymbols.getInstance(Locale.US));
        private final ScaleGestureDetector scaleGestureDetector;
        private final GestureDetector gestureDetector;
        private float zoom = 1f;
        private float offset;
        private float downX;
        private float downY;
        private float lastTouchX;
        private boolean horizontalDragging;
        private boolean gestureMoved;
        private boolean hasCursor;
        private long cursorElapsedMs;

        ChartView(List<String> names, int[] seriesColors, boolean live, float minimum, float maximum,
                  TextView cursorReadout) {
            super(MainActivity.this);
            seriesNames = new ArrayList<>(names);
            this.seriesColors = seriesColors.clone();
            this.cursorReadout = cursorReadout;
            limit = live ? 2001 : 2000;
            fixedMin = minimum;
            fixedMax = maximum;
            setClickable(true);
            scaleGestureDetector = new ScaleGestureDetector(MainActivity.this,
                    new ScaleGestureDetector.SimpleOnScaleGestureListener() {
                        @Override
                        public boolean onScale(ScaleGestureDetector detector) {
                            float focusX = detector.getFocusX();
                            zoomAt(detector.getScaleFactor(), focusX);
                            setCursorAt(focusX);
                            return true;
                        }
                    });
            gestureDetector = new GestureDetector(MainActivity.this,
                    new GestureDetector.SimpleOnGestureListener() {
                        @Override
                        public boolean onDown(MotionEvent event) {
                            return true;
                        }

                        @Override
                        public boolean onDoubleTap(MotionEvent event) {
                            resetView();
                            setCursorAt(event.getX());
                            return true;
                        }
                    });
        }

        void addSample(long elapsedMs, float[] sample) {
            synchronized (values) {
                if (values.size() >= limit) values.removeFirst();
                values.addLast(new Point(elapsedMs, sample));
            }
            postInvalidateOnAnimation();
        }

        void clear() {
            synchronized (values) {
                values.clear();
            }
            Runnable resetView = () -> {
                zoom = 1f;
                offset = 0f;
                hasCursor = false;
                cursorReadout.setText("Waiting for samples.");
                postInvalidateOnAnimation();
            };
            if (Looper.myLooper() == Looper.getMainLooper()) resetView.run();
            else uiHandler.post(resetView);
        }

        private int firstVisible(int count) {
            return Math.min(count - 1, (int) (offset * count));
        }

        private int lastVisible(int count, int start) {
            return Math.min(count, Math.max(start + 2,
                    start + (int) Math.ceil(count / (double) zoom)));
        }

        private float pointX(List<Point> points, int index, int start, int end,
                             float left, float right, long firstTime, long lastTime) {
            float fraction = lastTime > firstTime
                    ? (points.get(index).elapsedMs - firstTime) / (float) (lastTime - firstTime)
                    : (index - start) / Math.max(1f, end - start - 1f);
            return left + (right - left) * fraction;
        }

        private float pointY(float value, float minimum, float maximum,
                             float top, float bottom) {
            return bottom - (value - minimum) * (bottom - top) / (maximum - minimum);
        }

        private void setCursorAt(float touchX) {
            List<Point> points;
            synchronized (values) {
                points = new ArrayList<>(values);
            }
            if (points.size() < 2) return;

            float left = dp(50);
            float right = getWidth() - dp(12);
            if (right <= left) return;
            int start = firstVisible(points.size());
            int end = lastVisible(points.size(), start);
            long firstTime = points.get(start).elapsedMs;
            long lastTime = points.get(end - 1).elapsedMs;
            float targetX = Math.max(left, Math.min(right, touchX));
            int nearest = start;
            float nearestDistance = Float.POSITIVE_INFINITY;
            for (int i = start; i < end; i++) {
                float distance = Math.abs(pointX(points, i, start, end, left, right,
                        firstTime, lastTime) - targetX);
                if (distance < nearestDistance) {
                    nearest = i;
                    nearestDistance = distance;
                }
            }

            Point selected = points.get(nearest);
            cursorElapsedMs = selected.elapsedMs;
            hasCursor = true;
            StringBuilder readout = new StringBuilder(String.format(Locale.US,
                    "Time: %.3f s", selected.elapsedMs / 1000.0));
            for (int i = 0; i < seriesNames.size() && i < selected.values.length; i++) {
                readout.append('\n').append(seriesNames.get(i)).append(": ")
                        .append(formatNumber(selected.values[i]));
            }
            cursorReadout.setText(readout.toString());
            postInvalidateOnAnimation();
        }

        private int cursorIndex(List<Point> points, int start, int end) {
            if (!hasCursor) return -1;
            long firstTime = points.get(start).elapsedMs;
            long lastTime = points.get(end - 1).elapsedMs;
            if (cursorElapsedMs < Math.min(firstTime, lastTime) ||
                    cursorElapsedMs > Math.max(firstTime, lastTime)) {
                return -1;
            }
            int nearest = start;
            long nearestDistance = Long.MAX_VALUE;
            for (int i = start; i < end; i++) {
                long distance = Math.abs(points.get(i).elapsedMs - cursorElapsedMs);
                if (distance < nearestDistance) {
                    nearest = i;
                    nearestDistance = distance;
                }
            }
            return nearest;
        }

        private void clampOffset() {
            offset = Math.max(0f, Math.min(1f - 1f / zoom, offset));
        }

        private void panBy(float deltaX) {
            float left = dp(50);
            float right = getWidth() - dp(12);
            if (right <= left) return;
            offset -= deltaX / (right - left) / zoom;
            clampOffset();
            postInvalidateOnAnimation();
        }

        private void zoomAt(float factor, float focusX) {
            float newZoom = Math.max(1f, Math.min(32f, zoom * factor));
            if (newZoom == zoom) return;
            float left = dp(50);
            float right = getWidth() - dp(12);
            if (right <= left) return;
            float focusFraction = Math.max(0f, Math.min(1f,
                    (focusX - left) / (right - left)));
            float anchoredData = offset + focusFraction / zoom;
            zoom = newZoom;
            offset = anchoredData - focusFraction / zoom;
            clampOffset();
            postInvalidateOnAnimation();
        }

        private void resetView() {
            zoom = 1f;
            offset = 0f;
            postInvalidateOnAnimation();
        }

        private void requestParentInterception(boolean disallow) {
            if (getParent() != null) {
                getParent().requestDisallowInterceptTouchEvent(disallow);
            }
        }

        @Override
        public boolean onTouchEvent(MotionEvent event) {
            scaleGestureDetector.onTouchEvent(event);
            gestureDetector.onTouchEvent(event);

            int action = event.getActionMasked();
            if (action == MotionEvent.ACTION_DOWN) {
                downX = lastTouchX = event.getX();
                downY = event.getY();
                horizontalDragging = false;
                gestureMoved = false;
                return true;
            }
            if (action == MotionEvent.ACTION_POINTER_DOWN) {
                gestureMoved = true;
                requestParentInterception(true);
                return true;
            }
            if (action == MotionEvent.ACTION_MOVE) {
                if (event.getPointerCount() > 1 || scaleGestureDetector.isInProgress()) {
                    gestureMoved = true;
                    requestParentInterception(true);
                    setCursorAt(scaleGestureDetector.getFocusX());
                    return true;
                }

                float x = event.getX();
                float deltaFromDown = x - downX;
                float verticalFromDown = event.getY() - downY;
                if (!horizontalDragging && Math.abs(deltaFromDown) > dp(8) &&
                        Math.abs(deltaFromDown) > Math.abs(verticalFromDown)) {
                    horizontalDragging = true;
                    requestParentInterception(true);
                } else if (Math.abs(verticalFromDown) > dp(8) &&
                        Math.abs(verticalFromDown) > Math.abs(deltaFromDown)) {
                    gestureMoved = true;
                    return true;
                }

                if (horizontalDragging) {
                    panBy(x - lastTouchX);
                    lastTouchX = x;
                    gestureMoved = true;
                    setCursorAt(x);
                }
                return true;
            }
            if (action == MotionEvent.ACTION_POINTER_UP) {
                int lifted = event.getActionIndex();
                int remaining = lifted == 0 ? 1 : 0;
                if (remaining < event.getPointerCount()) {
                    downX = lastTouchX = event.getX(remaining);
                    downY = event.getY(remaining);
                }
                horizontalDragging = false;
                return true;
            }
            if (action == MotionEvent.ACTION_UP) {
                requestParentInterception(false);
                if (!gestureMoved) {
                    setCursorAt(event.getX());
                    performClick();
                }
                return true;
            }
            if (action == MotionEvent.ACTION_CANCEL) {
                requestParentInterception(false);
                return true;
            }
            return true;
        }

        @Override
        public boolean performClick() {
            super.performClick();
            return true;
        }

        @Override
        protected void onDraw(Canvas canvas) {
            super.onDraw(canvas);
            canvas.drawColor(0xff050505);
            float left = dp(50);
            float top = dp(15);
            float right = getWidth() - dp(12);
            float bottom = getHeight() - dp(25);
            if (right <= left || bottom <= top) return;

            paint.setStyle(Paint.Style.STROKE);
            paint.setColor(0xff333333);
            paint.setStrokeWidth(dp(1));
            for (int i = 0; i <= 4; i++) {
                float y = top + (bottom - top) * i / 4f;
                canvas.drawLine(left, y, right, y, paint);
            }

            List<Point> points;
            synchronized (values) {
                points = new ArrayList<>(values);
            }
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(0xffbbbbbb);
            paint.setTextSize(dp(11));
            if (points.size() < 2) {
                canvas.drawText(points.isEmpty() ? "Waiting for samples" : "Need at least two samples",
                        left, (top + bottom) / 2f, paint);
                return;
            }

            int start = firstVisible(points.size());
            int end = lastVisible(points.size(), start);
            float minimum = fixedMin;
            float maximum = fixedMax;
            if (!Float.isFinite(minimum) || !Float.isFinite(maximum) || minimum >= maximum) {
                minimum = Float.POSITIVE_INFINITY;
                maximum = Float.NEGATIVE_INFINITY;
                for (int i = start; i < end; i++) {
                    for (float value : points.get(i).values) {
                        if (Float.isFinite(value)) {
                            minimum = Math.min(minimum, value);
                            maximum = Math.max(maximum, value);
                        }
                    }
                }
                if (!Float.isFinite(minimum)) return;
                if (maximum - minimum < .0001f) {
                    maximum += 1;
                    minimum -= 1;
                }
            }

            canvas.drawText(axisFormat.format(maximum), dp(2), top + dp(4), paint);
            canvas.drawText(axisFormat.format(minimum), dp(2), bottom, paint);
            long firstTime = points.get(start).elapsedMs;
            long lastTime = points.get(end - 1).elapsedMs;
            canvas.drawText(String.format(Locale.US, "%.2fs", firstTime / 1000.0),
                    left, bottom + dp(16), paint);
            canvas.drawText(String.format(Locale.US, "%.2fs", lastTime / 1000.0),
                    right - dp(45), bottom + dp(16), paint);

            canvas.save();
            canvas.clipRect(left, top, right, bottom);
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(dp(2));
            for (int series = 0; series < seriesNames.size(); series++) {
                paint.setColor(seriesColors[series]);
                Path path = new Path();
                boolean previous = false;
                for (int i = start; i < end; i++) {
                    float value = points.get(i).values[series];
                    if (!Float.isFinite(value)) {
                        previous = false;
                        continue;
                    }
                    float x = pointX(points, i, start, end, left, right, firstTime, lastTime);
                    float y = pointY(value, minimum, maximum, top, bottom);
                    if (previous) path.lineTo(x, y);
                    else path.moveTo(x, y);
                    previous = true;
                }
                canvas.drawPath(path, paint);
            }

            int selectedIndex = cursorIndex(points, start, end);
            if (selectedIndex >= 0) {
                float cursorX = pointX(points, selectedIndex, start, end,
                        left, right, firstTime, lastTime);
                paint.setColor(0xbfffffff);
                paint.setStrokeWidth(dp(1));
                canvas.drawLine(cursorX, top, cursorX, bottom, paint);
                paint.setStyle(Paint.Style.FILL);
                for (int series = 0; series < seriesNames.size(); series++) {
                    float value = points.get(selectedIndex).values[series];
                    if (!Float.isFinite(value)) continue;
                    float y = pointY(value, minimum, maximum, top, bottom);
                    if (y >= top && y <= bottom) {
                        paint.setColor(seriesColors[series]);
                        canvas.drawCircle(cursorX, y, dp(4), paint);
                    }
                }
            }
            canvas.restore();
        }
    }
}
