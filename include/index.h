#ifndef INDEX_H
#define INDEX_H

#include <Arduino.h>

const char *ssid = "Master-Baiter";
const char *password = "123456789";

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Master Baiter HUD</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body {
      font-family: Arial;
      text-align: center;
      padding: 20px;
      background-color: #836bffff;
      color: white;
    }
    h1 { font-size: 2em; margin-bottom: 0.5em; }
    p { font-size: 1.4em; margin: 0.3em; }
    input { font-size: 1.2em; padding: 5px; margin: 5px; width: 80px; text-align: right; }
    button { font-size: 1.2em; padding: 10px 20px; margin: 10px; border-radius: 10px; border: none; background-color: #222; color: white; cursor: pointer; }
    .section { background-color: #06b1c4; padding: 15px; margin: 10px auto; border-radius: 10px; max-width: 400px; }
    #dist { font-family: monospace; letter-spacing: 6px; display: inline-block; min-width: 200px; }
  </style>
</head>
<body>
  <h1>Master Baiter HUD</h1>

  <div class="section">
    <p><strong>Dists:</strong><br><span id="dist">%D1% %D2% %D3% %D4% %D5%</span></p>
    <p><strong>Error:</strong> <span id="error">%ERROR%</span></p>
    <p><strong>Output:</strong> <span id="output">%OUTPUT%</span></p>
    <p><strong>Kp:</strong> <span id="Kp_val">%KP%</span> | <strong>Kd:</strong> <span id="Kd_val">%KD%</span></p>
  </div>

  <div class="section">
    <form id="pidForm">
      <label>Kp: <input type="number" step="0.1" id="Kp" name="Kp" value="%KP%"></label>
      <label>Kd: <input type="number" step="0.1" id="Kd" name="Kd" value="%KD%"></label><br>
      <button type="button" onclick="updatePID()">Update PID</button>
    </form>
  </div>

  <div class="section">
    <button onclick="control('Start')">Start</button>
    <button onclick="control('Stop')">Stop</button>
  </div>
  
  <script>
    function fetchData() {
      fetch('/data')
        .then(resp => resp.json())
        .then(data => {
          document.getElementById('dist').innerText = data.dist;
          document.getElementById('error').innerText = data.error.toFixed(2);
          document.getElementById('output').innerText = data.output.toFixed(2);
        })
        .catch(err => console.log("Fetch error:", err));
    }

    setInterval(fetchData, 300); // update every 0.3s

    function updatePID() {
      const Kp = document.getElementById('Kp').value;
      const Kd = document.getElementById('Kd').value;
      fetch(`/update?Kp=${Kp}&Kd=${Kd}`)
        .then(() => {
          document.getElementById('Kp_val').innerText = Kp;
          document.getElementById('Kd_val').innerText = Kd;
        })
        .catch(err => console.log("PID update error:", err));
    }

    function control(action) {
      fetch(`/control?action=${action}`)
        .then(resp => resp.text())
        .then(status => console.log("Action:", status))
        .catch(err => console.log("Control error:", err));
    }
  </script>
</body>
</html>
)rawliteral";

#endif
