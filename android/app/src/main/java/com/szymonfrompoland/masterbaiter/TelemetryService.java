package com.szymonfrompoland.masterbaiter;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.os.Binder;
import android.os.Build;
import android.os.Environment;
import android.os.IBinder;
import android.os.PowerManager;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.BufferedInputStream;
import java.io.BufferedReader;
import java.io.DataInputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStreamReader;
import java.net.InetSocketAddress;
import java.net.HttpURLConnection;
import java.net.Socket;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

public final class TelemetryService extends Service {
    interface Listener {
        void onStatus(String message, boolean connected, boolean recording);
        void onSample(TelemetryFrame.Sample sample, long dropped, List<String> signals, long revision);
        void onRunStarted(long runId, long revision);
    }

    final class LocalBinder extends Binder {
        TelemetryService service() { return TelemetryService.this; }
    }

    private static final String CHANNEL = "robot_telemetry";
    private static final int NOTIFICATION_ID = 1;
    private final IBinder binder = new LocalBinder();
    private final SessionHistory sessionHistory = new SessionHistory();
    private volatile Listener listener;
    private volatile Socket socket;
    private volatile boolean running;
    private volatile boolean connected;
    private volatile boolean recording;
    private volatile int generation;
    private volatile String status = "Connecting to telemetry.";
    private volatile String currentHost;
    private volatile List<String> currentSignals;
    private Thread worker;
    private PowerManager.WakeLock wakeLock;

    @Override
    public void onCreate() {
        super.onCreate();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel channel = new NotificationChannel(CHANNEL, "Robot telemetry",
                    NotificationManager.IMPORTANCE_LOW);
            getSystemService(NotificationManager.class).createNotificationChannel(channel);
        }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent == null) {
            stopSelf();
            return START_NOT_STICKY;
        }
        String host = intent.getStringExtra("host");
        ArrayList<String> signals = intent.getStringArrayListExtra("signals");
        if (host == null || signals == null || signals.isEmpty()) {
            stopSelf();
            return START_NOT_STICKY;
        }
        startForeground(NOTIFICATION_ID, notification("Connecting to robot."));
        stopStreaming();
        PowerManager power = (PowerManager) getSystemService(POWER_SERVICE);
        wakeLock = power.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "RobotTuner:Telemetry");
        wakeLock.acquire();
        currentHost = host;
        currentSignals = new ArrayList<>(signals);
        connected = false;
        recording = false;
        running = true;
        final int currentGeneration = ++generation;
        worker = new Thread(() -> runStream(host, signals, currentGeneration), "robot-telemetry");
        worker.start();
        return START_NOT_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) { return binder; }

    void setListener(Listener value) {
        listener = value;
        if (value != null) value.onStatus(status, connected, recording);
    }

    String host() { return currentHost; }
    SessionHistory.Snapshot history() { return sessionHistory.snapshot(); }
    boolean matchesSignals(List<String> signals) {
        return currentSignals != null && currentSignals.equals(signals);
    }

    void stopStreaming() {
        running = false;
        generation++;
        Socket current = socket;
        if (current != null) {
            try {
                current.close();
            } catch (IOException exception) {
                publish("Could not close telemetry: " + message(exception));
            }
        }
        Thread currentWorker = worker;
        if (currentWorker != null) currentWorker.interrupt();
        if (wakeLock != null && wakeLock.isHeld()) wakeLock.release();
        wakeLock = null;
    }

    private boolean active(int currentGeneration) {
        return running && generation == currentGeneration;
    }

    private void runStream(String host, List<String> signals, int currentGeneration) {
        while (active(currentGeneration)) {
            Socket current = new Socket();
            File documents = getExternalFilesDir(Environment.DIRECTORY_DOCUMENTS);
            PhoneRecorder recorder = new PhoneRecorder(
                    new File(documents == null ? getFilesDir() : documents, "robot-logs"), signals);
            try {
                verifySignals(host, signals);
                socket = current;
                current.connect(new InetSocketAddress(host, 4210), 5000);
                if (!active(currentGeneration)) break;
                current.setTcpNoDelay(true);
                connected = true;
                publish("Connected. Waiting for robot samples.");
                DataInputStream input = new DataInputStream(new BufferedInputStream(current.getInputStream()));
                while (active(currentGeneration)) {
                    TelemetryFrame frame = TelemetryFrame.read(input, signals.size());
                    if (!active(currentGeneration)) break;
                    if (frame.samples.length == 0) {
                        if (sessionHistory.observeRun(frame.runState)) {
                            Listener currentListener = listener;
                            if (currentListener != null) currentListener.onRunStarted(
                                    frame.runState & ~1L, sessionHistory.revision());
                        }
                        if (recorder.applyState(frame.state)) {
                            recording = recorder.isRecording();
                            publish(recording ? "Automatically recording on the phone." :
                                    "Waiting for robot start with Logging enabled.");
                        }
                    }
                    for (TelemetryFrame.Sample sample : frame.samples) {
                        if (!active(currentGeneration)) break;
                        if (sessionHistory.add(sample)) {
                            Listener currentListener = listener;
                            if (currentListener != null) currentListener.onRunStarted(
                                    sample.runState & ~1L, sessionHistory.revision());
                        }
                        if (recorder.applyState(sample.state)) {
                            recording = recorder.isRecording();
                            publish(recording ? "Automatically recording on the phone." :
                                    "Waiting for robot start with Logging enabled.");
                        }
                        recorder.append(sample);
                        Listener currentListener = listener;
                        if (currentListener != null) currentListener.onSample(
                                sample, frame.dropped, signals, sessionHistory.revision());
                    }
                    if (frame.samples.length > 0) recorder.flush();
                }
            } catch (IOException exception) {
                if (active(currentGeneration)) {
                    connected = false;
                    recording = false;
                    publish("Telemetry or phone storage error; retrying: " + message(exception));
                }
            } finally {
                try {
                    recorder.close();
                } catch (IOException exception) {
                    publish("Could not save phone log: " + message(exception));
                }
                try {
                    current.close();
                } catch (IOException exception) {
                    publish("Could not close telemetry: " + message(exception));
                }
                if (generation == currentGeneration) {
                    socket = null;
                    connected = false;
                    recording = false;
                }
            }
            if (active(currentGeneration)) {
                try {
                    Thread.sleep(1000);
                } catch (InterruptedException exception) {
                    Thread.currentThread().interrupt();
                    break;
                }
            }
        }
    }

    private void verifySignals(String host, List<String> signals) throws IOException {
        HttpURLConnection connection = (HttpURLConnection) new URL("http://" + host + "/api/schema").openConnection();
        connection.setConnectTimeout(5000);
        connection.setReadTimeout(5000);
        try {
            if (connection.getResponseCode() != 200) throw new IOException("Robot schema is unavailable.");
            StringBuilder text = new StringBuilder();
            try (BufferedReader reader = new BufferedReader(new InputStreamReader(
                    connection.getInputStream(), StandardCharsets.UTF_8))) {
                String line;
                while ((line = reader.readLine()) != null) text.append(line);
            }
            JSONArray received = new JSONObject(text.toString()).getJSONArray("signals");
            if (received.length() != signals.size()) {
                throw new IOException("Robot signals changed; reconnect in the app.");
            }
            for (int i = 0; i < signals.size(); i++) {
                if (!signals.get(i).equals(received.getString(i))) {
                    throw new IOException("Robot signals changed; reconnect in the app.");
                }
            }
        } catch (JSONException exception) {
            throw new IOException("Invalid robot signal schema.", exception);
        } finally {
            connection.disconnect();
        }
    }

    private void publish(String message) {
        status = message;
        Listener current = listener;
        if (current != null) current.onStatus(message, connected, recording);
        getSystemService(NotificationManager.class).notify(NOTIFICATION_ID, notification(message));
    }

    private Notification notification(String message) {
        Intent launch = new Intent(this, MainActivity.class);
        PendingIntent pending = PendingIntent.getActivity(this, 0, launch,
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        Notification.Builder builder = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                ? new Notification.Builder(this, CHANNEL) : new Notification.Builder(this);
        return builder.setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
                .setContentTitle("Robot Tuner").setContentText(message)
                .setContentIntent(pending).setOngoing(true).build();
    }

    private static String message(Exception exception) {
        return exception.getMessage() == null ? exception.getClass().getSimpleName() : exception.getMessage();
    }

    @Override
    public void onDestroy() {
        stopStreaming();
        super.onDestroy();
    }

    @Override
    public void onTimeout(int startId, int foregroundServiceType) {
        publish("Android stopped the recording service after its background time limit.");
        stopStreaming();
        stopSelf(startId);
    }
}
