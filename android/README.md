# Master Baiter Tuner Android app

The APK contains the phone UI. The robot provides a small HTTP control API and
a batched TCP telemetry stream; it no longer serves the tuner HTML or writes
sample logs to flash.

## Build

Open this `android` directory in Android Studio and build the `app` debug
variant, or run:

```powershell
.\gradlew.bat assembleDebug
```

The APK is written to `app\build\outputs\apk\debug\app-debug.apk`.

## Connect and record

1. Open Master Baiter Tuner and press **Connect**. The app requests the robot's
   **Robot-Tuner** Wi-Fi network by name, then Android may ask you to approve
   joining it. Enter the Wi-Fi password the first time; the app encrypts it
   with Android Keystore and reuses it on later connections.
2. The robot's IP address defaults to `192.168.4.1` in access-point mode; edit
   it in the address field if your robot uses another address.
3. Use the robot-defined tabs to operate and tune the robot. The app fetches
   tabs, controls, chart definitions, and signal names from the robot when
   connecting; adding a supported control or logger signal does not require
   rebuilding the APK.
4. Set **Logging** to **ON** in the Control tab to display live charts and
   readings in the robot-defined **Logs** tab and save samples to a phone CSV.
   When the robot starts, the connected phone automatically creates the file;
   stopping the robot or turning Logging off closes it. With Logging OFF, live
   data is not displayed and no CSV is recorded. There is no separate Live tab
   or Telemetry switch.
5. In **Files**, use **Refresh** to list phone logs, **View** to graph one,
   **Export** to save a complete CSV through Android's document picker, or
   **Rename** to rename a completed log, or **Delete** to remove it after
   confirmation. The active recording cannot be renamed or deleted.
   The visual preview is downsampled to at most 2,000 points; export contains
   every recorded row. Logs charts retain the most recent session after the
   robot stops and reset when a new run starts. Reopening Logs shows a bounded,
   downsampled preview of the session when Telemetry is enabled; the CSV is
   unaffected. Pinch to zoom, drag to pan, tap or drag to inspect a sample's
   timestamp and values. Use **Reset view** to restore the full chart; double-tap
   also resets it.

The foreground telemetry service keeps recording when the activity is in the
background and holds a partial wake lock while connected or retrying. This
uses battery. Allow notifications so its status and storage errors are visible.
Use **Disconnect** to stop the service. Android may still stop a foreground
service (including the Android 15 data-sync time limit), or the user may
force-stop the app. A phone must be connected and the service running to record:
data produced while disconnected cannot be recovered because the robot does
not store it. The service retries a lost connection and starts a new file
after reconnecting; gaps and queue overflows can lose samples.

The control API uses HTTP on port 80. Telemetry uses a length-delimited TCP
stream on port 4210. Protocol version 4 uses a 20-byte header: `MBT`, version,
sample count, signal count, two reserved bytes, a little-endian 32-bit queue
drop count, a little-endian 32-bit recording state, and a 32-bit run state. A zero-sample frame
reports state changes when the queue is idle. Each sample has little-endian
32-bit sequence, elapsed milliseconds, recording state, run state, and one 32-bit float
per signal in schema order. The low state bit indicates recording, and the
remaining bits identify the recording session. The run state likewise uses
its low bit for running and its remaining bits to identify the robot run,
independently of the Logging control. Frames contain up to 10
samples and wait at most 50 ms to batch them. The robot queues
samples in RAM and sends them from a separate FreeRTOS task, so socket writes
and phone storage do not run in the control loop. Queue overflow is reported
by sequence gaps and the cumulative drop counter in each frame.

The APK and firmware must be updated together because older firmware uses a
different telemetry protocol. Register signals with `telemetry.add(...)`,
then add optional `tuner.addChart(...)` or `tuner.addRaw(...)` views to place
them in the desired tab and category. CSV files always contain all registered
signals, even if they have no chart.

`telemetry.setSampleIntervalLoops(N)` samples once every `N` calls to the
firmware loop: `1` samples on every loop, `2` samples every second loop, and so
on. This is a loop count, not milliseconds; elapsed sample timestamps still use
milliseconds. The sensor and control loop cadence is unchanged.
