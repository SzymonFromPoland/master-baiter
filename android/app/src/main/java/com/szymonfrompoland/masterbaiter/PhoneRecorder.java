package com.szymonfrompoland.masterbaiter;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStreamWriter;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;

final class PhoneRecorder implements AutoCloseable {
    private static final Object FILE_LOCK = new Object();
    private static final Set<File> OPEN_FILES = new HashSet<>();
    private final File directory;
    private final List<String> signals;
    private BufferedWriter writer;
    private File currentFile;
    private long state = -1;

    PhoneRecorder(File directory, List<String> signals) {
        this.directory = directory;
        this.signals = signals;
    }

    boolean applyState(long nextState) throws IOException {
        if (state == nextState) return false;
        closeWriter();
        state = nextState;
        if ((state & 1) != 0) {
            if (!directory.exists() && !directory.mkdirs()) throw new IOException("Cannot create log folder.");
            String stamp = new SimpleDateFormat("yyyyMMdd_HHmmss_SSS", Locale.US).format(new Date());
            File file;
            synchronized (FILE_LOCK) {
                file = File.createTempFile("robot_" + stamp + "_", ".csv", directory);
                OPEN_FILES.add(file);
                currentFile = file;
            }
            writer = new BufferedWriter(new OutputStreamWriter(
                    new FileOutputStream(file), StandardCharsets.UTF_8));
            writer.write("elapsed_ms,sequence");
            for (String signal : signals) writer.write("," + signal);
            writer.write('\n');
            writer.flush();
        }
        return true;
    }

    boolean isRecording() { return writer != null; }

    void append(TelemetryFrame.Sample sample) throws IOException {
        if (writer == null) return;
        if (sample.values.length != signals.size()) throw new IOException("Sample signal count changed.");
        writer.write(Long.toString(sample.elapsedMs));
        writer.write(',');
        writer.write(Long.toString(Integer.toUnsignedLong(sample.sequence)));
        for (float value : sample.values) {
            writer.write(',');
            writer.write(Float.toString(value));
        }
        writer.write('\n');
    }

    void flush() throws IOException {
        if (writer != null) writer.flush();
    }

    private void closeWriter() throws IOException {
        synchronized (FILE_LOCK) {
            try {
                if (writer != null) {
                    BufferedWriter current = writer;
                    writer = null;
                    current.close();
                }
            } finally {
                if (currentFile != null) OPEN_FILES.remove(currentFile);
                currentFile = null;
            }
        }
    }

    static void deleteClosedLog(File file) throws IOException {
        synchronized (FILE_LOCK) {
            if (OPEN_FILES.contains(file)) {
                throw new IOException("This log is currently being recorded. Stop the robot or disable Logging first.");
            }
            if (!file.delete()) {
                throw new IOException("The log could not be deleted. Refresh the file list and try again.");
            }
        }
    }

    static File renameClosedLog(File file, String name) throws IOException {
        String trimmed = name.trim();
        if (trimmed.isEmpty() || trimmed.equals(".") || trimmed.equals("..") ||
                trimmed.length() > 120 || trimmed.matches(".*[\\\\/:*?\"<>|\\p{Cntrl}].*")) {
            throw new IOException("Enter a valid filename without path separators.");
        }
        if (!trimmed.toLowerCase(Locale.ROOT).endsWith(".csv")) trimmed += ".csv";
        File destination = new File(file.getParentFile(), trimmed);
        synchronized (FILE_LOCK) {
            if (OPEN_FILES.contains(file)) {
                throw new IOException("This log is currently being recorded. Stop the robot or disable Logging first.");
            }
            if (destination.equals(file)) return file;
            if (destination.exists()) throw new IOException("A log with that name already exists.");
            if (!file.renameTo(destination)) throw new IOException("The log could not be renamed.");
            return destination;
        }
    }

    @Override
    public void close() throws IOException { closeWriter(); }
}
