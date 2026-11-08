package com.szymonfrompoland.masterbaiter;

import java.io.ByteArrayInputStream;
import java.io.DataInputStream;
import java.io.File;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Arrays;
import java.util.List;

public final class TelemetryProtocolTest {
    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    private static TelemetryFrame frame(byte[] bytes, int signals) throws IOException {
        return TelemetryFrame.read(new DataInputStream(new ByteArrayInputStream(bytes)), signals);
    }

    public static void main(String[] args) throws Exception {
        ByteBuffer bytes = ByteBuffer.allocate(20 + 2 * (16 + 3 * 4)).order(ByteOrder.LITTLE_ENDIAN);
        bytes.put(new byte[]{'M', 'B', 'T', 4, 2, 3, 0, 0}).putInt(7).putInt(3).putInt(3);
        bytes.putInt(-1).putInt(123).putInt(3).putInt(3).putFloat(1.5f).putFloat(-2).putFloat(9);
        bytes.putInt(0).putInt(124).putInt(2).putInt(3).putFloat(3).putFloat(4).putFloat(5);
        TelemetryFrame decoded = frame(bytes.array(), 3);
        check(decoded.dropped == 7 && decoded.state == 3 && decoded.samples.length == 2, "frame header");
        check(decoded.samples[0].sequence == -1 && decoded.samples[0].elapsedMs == 123 &&
                decoded.samples[0].state == 3 && decoded.samples[0].values[1] == -2, "first sample");
        check(decoded.samples[1].state == 2 && decoded.samples[1].values[2] == 5, "second sample");

        ByteBuffer heartbeat = ByteBuffer.allocate(20).order(ByteOrder.LITTLE_ENDIAN);
        heartbeat.put(new byte[]{'M', 'B', 'T', 4, 0, 0, 0, 0}).putInt(0).putInt(2).putInt(2);
        check(frame(heartbeat.array(), 3).samples.length == 0, "status-only frame");
        ByteBuffer maximum = ByteBuffer.allocate(20 + 16 + 24 * 4).order(ByteOrder.LITTLE_ENDIAN);
        maximum.put(new byte[]{'M', 'B', 'T', 4, 1, 24, 0, 0}).putInt(0).putInt(3).putInt(3);
        maximum.putInt(1).putInt(2).putInt(3).putInt(3);
        for (int i = 0; i < 24; i++) maximum.putFloat(i);
        check(frame(maximum.array(), 24).samples[0].values[23] == 23, "maximum signal count");
        try {
            frame(bytes.array(), 2);
            throw new AssertionError("Mismatched signal count accepted");
        } catch (IOException expected) {
            check(expected.getMessage().contains("schema"), "mismatch error");
        }
        try {
            frame(Arrays.copyOf(bytes.array(), bytes.capacity() - 1), 3);
            throw new AssertionError("Truncated frame accepted");
        } catch (IOException expected) {
            // Expected.
        }

        File directory = Files.createTempDirectory("robot-recorder-test").toFile();
        List<String> signals = Arrays.asList("a", "b", "new_signal");
        try {
            PhoneRecorder recorder = new PhoneRecorder(directory, signals);
            recorder.applyState(0);
            recorder.append(decoded.samples[0]);
            check(directory.listFiles().length == 0, "telemetry-only must not record");
            recorder.applyState(3);
            recorder.append(decoded.samples[0]);
            recorder.flush();
            File[] files = directory.listFiles();
            check(files.length == 1, "recording created automatically");
            try {
                PhoneRecorder.renameClosedLog(files[0], "active");
                throw new AssertionError("Active recording was renamed");
            } catch (IOException expected) {
                check(expected.getMessage().contains("currently"), "active log rename error");
            }
            try {
                PhoneRecorder.deleteClosedLog(files[0]);
                throw new AssertionError("Active recording was deleted");
            } catch (IOException expected) {
                check(expected.getMessage().contains("currently"), "active log delete error");
            }
            String contents = new String(Files.readAllBytes(files[0].toPath()), StandardCharsets.UTF_8);
            check(contents.equals("elapsed_ms,sequence,a,b,new_signal\n123,4294967295,1.5,-2.0,9.0\n"),
                    "live CSV content");
            recorder.applyState(2);
            File renamed = PhoneRecorder.renameClosedLog(files[0], "my session");
            check(renamed.getName().equals("my session.csv") && renamed.exists(), "closed log renamed");
            File collision = new File(directory, "existing.csv");
            check(collision.createNewFile(), "created collision fixture");
            try {
                PhoneRecorder.renameClosedLog(renamed, "existing.csv");
                throw new AssertionError("Existing log was overwritten");
            } catch (IOException expected) {
                check(renamed.exists() && collision.exists(), "collision preserves both logs");
            }
            Files.delete(collision.toPath());
            try {
                PhoneRecorder.renameClosedLog(renamed, "../outside");
                throw new AssertionError("Path traversal accepted");
            } catch (IOException expected) {
                check(renamed.exists(), "invalid name preserves log");
            }
            PhoneRecorder.deleteClosedLog(renamed);
            check(directory.listFiles().length == 0, "closed log deleted");
            recorder.append(decoded.samples[1]);
            recorder.applyState(5);
            recorder.append(decoded.samples[1]);
            recorder.close();
            check(directory.listFiles().length == 1, "new run creates new file");
        } finally {
            File[] files = directory.listFiles();
            if (files != null) for (File file : files) Files.delete(file.toPath());
            Files.delete(directory.toPath());
        }
        SessionHistory history = new SessionHistory();
        check(history.add(decoded.samples[0]), "first run detected");
        history.add(decoded.samples[1]);
        check(!history.observeRun(2) && history.snapshot().samples.size() == 2, "stop retains session");
        check(!history.observeRun(3) && history.snapshot().samples.size() == 2, "same run remains");
        long oldRevision = history.snapshot().revision;
        check(history.observeRun(5) && history.snapshot().samples.isEmpty(), "new run clears session");
        check(history.snapshot().revision > oldRevision, "new run advances revision");
        for (int i = 0; i < 10000; i++) {
            history.add(new TelemetryFrame.Sample(i, i, 5, 5, new float[]{i}));
        }
        SessionHistory.Snapshot snapshot = history.snapshot();
        check(snapshot.samples.size() <= 2001 && snapshot.total == 10000, "bounded whole-session history");
        check(snapshot.samples.get(0).elapsedMs == 0 &&
                snapshot.samples.get(snapshot.samples.size() - 1).elapsedMs == 9999, "history spans full run");
        System.out.println("Telemetry protocol and automatic recording tests passed.");
    }
}
