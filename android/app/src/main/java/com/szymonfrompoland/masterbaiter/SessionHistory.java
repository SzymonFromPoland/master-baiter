package com.szymonfrompoland.masterbaiter;

import java.util.ArrayList;
import java.util.List;

final class SessionHistory {
    private static final int MAX_POINTS = 2000;
    private final List<TelemetryFrame.Sample> points = new ArrayList<>();
    private long runId = -1;
    private long revision;
    private long count;
    private long stride = 1;
    private TelemetryFrame.Sample latest;

    synchronized boolean observeRun(long state) {
        long id = state & ~1L;
        if ((state & 1) == 0 || runId == id) return false;
        runId = id;
        revision++;
        count = 0;
        stride = 1;
        points.clear();
        latest = null;
        return true;
    }

    synchronized boolean add(TelemetryFrame.Sample sample) {
        boolean changed = observeRun(sample.runState);
        if ((sample.runState & ~1L) != runId || (sample.runState & 1) == 0) return changed;
        if (count % stride == 0) points.add(sample);
        count++;
        latest = sample;
        if (points.size() > MAX_POINTS) {
            List<TelemetryFrame.Sample> reduced = new ArrayList<>();
            for (int i = 0; i < points.size(); i += 2) reduced.add(points.get(i));
            points.clear();
            points.addAll(reduced);
            stride *= 2;
        }
        return changed;
    }

    synchronized Snapshot snapshot() {
        List<TelemetryFrame.Sample> copy = new ArrayList<>(points);
        if (latest != null && (copy.isEmpty() || copy.get(copy.size() - 1) != latest)) copy.add(latest);
        return new Snapshot(runId, revision, copy, count);
    }

    synchronized long revision() { return revision; }

    static final class Snapshot {
        final long runId;
        final long revision;
        final List<TelemetryFrame.Sample> samples;
        final long total;

        Snapshot(long runId, long revision, List<TelemetryFrame.Sample> samples, long total) {
            this.runId = runId;
            this.revision = revision;
            this.samples = samples;
            this.total = total;
        }
    }
}
