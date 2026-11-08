package com.szymonfrompoland.masterbaiter;

import java.io.DataInputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

final class TelemetryFrame {
    static final int MAX_SIGNALS = 24;
    static final int MAX_SAMPLES = 10;

    static final class Sample {
        final int sequence;
        final long elapsedMs;
        final long state;
        final long runState;
        final float[] values;

        Sample(int sequence, long elapsedMs, long state, long runState, float[] values) {
            this.sequence = sequence;
            this.elapsedMs = elapsedMs;
            this.state = state;
            this.runState = runState;
            this.values = values;
        }
    }

    final long dropped;
    final long state;
    final long runState;
    final Sample[] samples;

    private TelemetryFrame(long dropped, long state, long runState, Sample[] samples) {
        this.dropped = dropped;
        this.state = state;
        this.runState = runState;
        this.samples = samples;
    }

    static TelemetryFrame read(DataInputStream input, int expectedSignals) throws IOException {
        byte[] header = new byte[20];
        input.readFully(header);
        if (header[0] != 'M' || header[1] != 'B' || header[2] != 'T' || header[3] != 4) {
            throw new IOException("Unsupported telemetry frame.");
        }
        int count = header[4] & 0xff;
        int signalCount = header[5] & 0xff;
        if (count > MAX_SAMPLES || expectedSignals < 1 || expectedSignals > MAX_SIGNALS ||
                (count == 0 ? signalCount != 0 : signalCount != expectedSignals)) {
            throw new IOException("Telemetry frame does not match the robot schema.");
        }
        ByteBuffer headerData = ByteBuffer.wrap(header).order(ByteOrder.LITTLE_ENDIAN);
        headerData.position(8);
        long dropped = Integer.toUnsignedLong(headerData.getInt());
        long state = Integer.toUnsignedLong(headerData.getInt());
        long runState = Integer.toUnsignedLong(headerData.getInt());
        byte[] payload = new byte[count * (16 + 4 * signalCount)];
        input.readFully(payload);
        ByteBuffer data = ByteBuffer.wrap(payload).order(ByteOrder.LITTLE_ENDIAN);
        Sample[] samples = new Sample[count];
        for (int i = 0; i < count; i++) {
            int sequence = data.getInt();
            long elapsed = Integer.toUnsignedLong(data.getInt());
            long sampleState = Integer.toUnsignedLong(data.getInt());
            long sampleRunState = Integer.toUnsignedLong(data.getInt());
            float[] values = new float[signalCount];
            for (int j = 0; j < signalCount; j++) values[j] = data.getFloat();
            samples[i] = new Sample(sequence, elapsed, sampleState, sampleRunState, values);
        }
        return new TelemetryFrame(dropped, state, runState, samples);
    }
}
